// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// The entry-aad service's state and callbacks (see service_app.hpp).
#include "service_app.hpp"

#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "aa_protocol.hpp"
#include "evo_log.hpp"
#include "evo_spp_snapshot.hpp"
#include "media_info.hpp"
#include "service_contract.hpp"
#include "spp_advertisement.hpp"

namespace evo {

ServiceApp::ServiceApp(uint16_t p, bool v, volatile sig_atomic_t* quit)
    : knob(nullptr),
      server(p),
      port(p),
      videoEnabled(v),
      stopping(false),
      wirelessDone(false),
      focus(true),
      ready(false),
      tcpPreparing(false),
      reverse(false),
      optionTapX(0),
      optionTapY(0),
      pendingOption(0),
      pendingOptionMs(0),
      inCall(false),
      awaitShow(false),
      asking(false),
      pendingFocus(0),
      sppLinks(0),
      tcpLinks(0),
      usbLinks(0),
      videoFrames(0),
      wppReply(false),
      wppAccepted(false),
      coverPrint(0),
      mediaIdsSeen(0),
      quit_(quit) {
    pthread_mutex_init(&mu, nullptr);
    pthread_mutex_init(&connectMu, nullptr);
    pthread_mutex_init(&videoMu, nullptr);
}

ServiceApp::~ServiceApp() {
    pill.stop();
    server.stop();
    mic.stop();
    kombi.stop();
    session.stop();
    video.stop();
    bt.stop();
    pthread_mutex_destroy(&mu);
    pthread_mutex_destroy(&connectMu);
    pthread_mutex_destroy(&videoMu);
}

void ServiceApp::markReady() {
    pthread_mutex_lock(&mu);
    ready = true;
    pthread_mutex_unlock(&mu);
}

void ServiceApp::requestStop() {
    pthread_mutex_lock(&mu);
    stopping = true;
    mediaPress.cancel();
    unlink(contract::kMediaRecallFile);
    pthread_mutex_unlock(&mu);
}

bool ServiceApp::stopRequested() {
    pthread_mutex_lock(&mu);
    bool v = stopping;
    pthread_mutex_unlock(&mu);
    return v;
}

bool ServiceApp::projectionBusy() {
    pthread_mutex_lock(&mu);
    bool v = tcpPreparing;
    pthread_mutex_unlock(&mu);
    return v || session.isActive();
}

void ServiceApp::sppLinkOpened() {
    pthread_mutex_lock(&mu);
    ++sppLinks;
    pthread_mutex_unlock(&mu);
}

void ServiceApp::handshakeEnded(bool spoke, bool accepted) {
    pthread_mutex_lock(&mu);
    wppReply = wppReply || spoke;
    wppAccepted = wppAccepted || accepted;
    pthread_mutex_unlock(&mu);
}

ServiceApp::Turn ServiceApp::turn(bool service) {
    Turn t;
    pthread_mutex_lock(&mu);
    t.failed = wirelessDone;
    t.active = session.isActive();
    t.onScreen = videoEnabled && t.active && focus && !reverse;  // what the iDrive app is told
    t.asks = t.active && asking && !focus;
    const uint32_t nowMs = monoMs();
    const PeekTiming::Picture picture = peek.turn(nowMs, t.onScreen);
    t.shown = picture.shown;
    if (!t.shown) {
        pill.hide();  // camera, menu, popup: the pill never stays over iDrive
    }
    t.focusMsg = pendingFocus;
    pendingFocus = 0;
    int option = 0;  // an options panel choice, sent half a second after the picture is back
    if (pendingOption && t.shown) {
        if (!pendingOptionMs) {
            pendingOptionMs = nowMs ? nowMs : 1;
        } else if (nowMs - pendingOptionMs >= 500) {
            option = pendingOption;
            pendingOption = 0;
        }
    }
    t.idleForAdvert = !tcpPreparing && !t.active && (service || (!sppLinks && !tcpLinks));
    pthread_mutex_unlock(&mu);
    if (option) {
        optionAction(option);
    }
    t.pictureUp = false;
    t.pictureStarted = false;
    if (videoEnabled) {
        pthread_mutex_lock(&videoMu);
        video.setVisible(t.shown);
        const bool opacityRefused = t.shown && !video.setOpacity(picture.opacity) && picture.opacity < 1.f;
        t.pictureUp = video.isVisible();
        t.pictureStarted = video.isReady();
        pthread_mutex_unlock(&videoMu);
        if (opacityRefused) {
            ELOG("ServiceApp", "the compositor refused the opacity: the volume now steps the picture aside");
            pthread_mutex_lock(&mu);
            peek.refuseOpacity(monoMs(), nowMs);
            pthread_mutex_unlock(&mu);
        }
    }
    return t;
}

bool ServiceApp::refreshAdvertisementIfIdle(bool service, SppSnapshotReader& reader, unsigned slot) {
    // Same lock order as connected(): a TCP session cannot start during an idle refresh.
    bool failed = false;
    pthread_mutex_lock(&connectMu);
    pthread_mutex_lock(&mu);
    bool idle = !tcpPreparing && !session.isActive() && (service || (!sppLinks && !tcpLinks));
    pthread_mutex_unlock(&mu);
    if (idle) {
        failed = !refreshAdvertisement(bt, reader, slot);
    }
    pthread_mutex_unlock(&connectMu);
    return !failed;
}

void ServiceApp::frame(uint32_t, const uint8_t* data, size_t len, uint64_t pts, void* user) {
    ServiceApp* a = static_cast<ServiceApp*>(user);
    if (a->videoEnabled) {
        // Phone frames never wait for mu: the main loop holds it while LayerManagerControl runs (show/hide,
        // opacity), which would stall the picture.
        ++a->videoFrames;
        if (a->video.postVideoFrame(data, len, pts) == FrameFate::kLost) {
            a->session.postKeyframeRequest();  // the session decides whether and when to ask the phone
        }
    }
}

void ServiceApp::codec(const uint8_t* data, size_t len, void* user) {
    ServiceApp* a = static_cast<ServiceApp*>(user);
    if (a->videoEnabled) {
        a->video.postCodecConfig(data, len);
    }
}

void ServiceApp::touchEvent(const aa::TouchEventOut& ev, void* user) {
    static_cast<ServiceApp*>(user)->session.postTouchEvent(ev);
}

// Volume key (knob or steering wheel). With the pill working, the level message that follows shows it.
void ServiceApp::volumeEvent(unsigned steps, void* user) {
    ServiceApp* a = static_cast<ServiceApp*>(user);
    pthread_mutex_lock(&a->mu);
    const uint32_t now = monoMs();
    a->volume.volumeKey(now);
    const bool start = a->peek.volumeKey(now, a->pill.ready());
    const bool refused = a->peek.dimRefused();
    pthread_mutex_unlock(&a->mu);
    if (start) {
        ELOG("ServiceApp", "volume (%u step(s)): %s %u ms", steps, refused ? "AA picture aside" : "AA picture dimmed",
             kVolumePeekMs);
    }
}

// Volume level from the car (vMOST thread): the pill over Android Auto, which stays opaque. Only a change, or a level
// right after a volume key, shows; another source's level never does (volume_state.hpp).
void ServiceApp::volumeLevel(const VolumeLevel& level, void* user) {
    ServiceApp* a = static_cast<ServiceApp*>(user);
    pthread_mutex_lock(&a->mu);
    const bool first = !a->peek.levelSeen();
    const bool ready = a->pill.ready();
    const VolumeState::Shown shown = a->volume.level(level, monoMs());
    if (shown.show && a->peek.volumeLevel(ready)) {
        a->pill.show(shown.level.value, shown.level.min, shown.level.max);  // only takes the pill's own short lock
    }
    const bool seen = a->peek.levelSeen();
    pthread_mutex_unlock(&a->mu);
    if (first && seen) {
        ELOG("ServiceApp", "volume level received from the car: pill %s", ready ? "on" : "unavailable");
    }
}

// The audio knob pressed (vMOST thread): iDrive mutes or unmutes, the pill shows the speaker crossed out or not.
void ServiceApp::audioButton(void* user) {
    ServiceApp* a = static_cast<ServiceApp*>(user);
    pthread_mutex_lock(&a->mu);
    const VolumeState::Shown shown = a->volume.audioButton(monoMs());
    if (shown.show && a->peek.volumeLevel(a->pill.ready())) {
        a->pill.show(shown.level.value, shown.level.min, shown.level.max);
    }
    const bool muted = a->volume.muted();
    pthread_mutex_unlock(&a->mu);
    ELOG("ServiceApp", "audio knob: %s, as iDrive", muted ? "muted" : "sound back");
}

// Hard key iDrive acts on (MENU and OPTION, COM during a call; not forwarded to the phone): the picture steps aside at
// once, so the result does not depend on timing (observed on the car otherwise: iDrive sometimes came back,
// sometimes not). If iDrive stays on the blank Android Auto page the iDrive app opens its main menu. The Android
// Auto row brings the phone back.
void ServiceApp::hardKey(const char* name, void* user) {
    // OPTION opens the Android Auto menu of the iDrive app (Stop, start automatically): see the app's hold logic.
    // NAV, MAP and MEDIA always go to Android Auto (appKey); COM reaches here only during a call.
    static const char* const leave[] = {"MENU", "COM", "OPTIONS"};
    bool match = false;
    for (unsigned i = 0; i < sizeof(leave) / sizeof(leave[0]); ++i) {
        if (!strcmp(name, leave[i])) {
            match = true;
        }
    }
    if (!match) {
        return;
    }
    ServiceApp* a = static_cast<ServiceApp*>(user);
    pthread_mutex_lock(&a->mu);
    const bool was = a->peek.hardKey();
    if (was) {
        a->focus = false;
        a->pendingFocus = 2;
        a->mediaPress.cancel();
        unlink(contract::kMediaRecallFile);
        unlink(contract::kAppKeyFile);  // a BACK just before: iDrive leaves for this key, not to be pulled back
    }
    pthread_mutex_unlock(&a->mu);
    if (was) {
        ELOG("ServiceApp",
             "key %s: AA picture aside, iDrive takes the screen back (the Android Auto row brings it back)", name);
    }
}

void ServiceApp::knobEvent(const aa::CcsEvent& ev, void* user) {
    ServiceApp* a = static_cast<ServiceApp*>(user);
    if (ev.kind == 1) {
        a->session.postKeyEvent(ev.keycode, ev.down != 0);
    } else if (ev.kind == 2) {
        a->session.postRelativeEvent(ev.keycode, ev.delta);
    }
}

// A choice of the iDrive app's options panel (OPTION over Android Auto): Google Maps' hidden buttons back, without a
// touch screen. which: 1 "Show map buttons" (as option_key.hpp configures it), 2 "Tap the map".
void ServiceApp::optionAction(int which) {
    const OptionKey::Mode mode = which == 2 ? OptionKey::kTap : option.mode;
    switch (mode) {
        case OptionKey::kNudge:
            session.postRelativeEvent(aa::AAK_ROTARY_CONTROLLER, 1);
            session.postRelativeEvent(aa::AAK_ROTARY_CONTROLLER, -1);
            ELOG("ServiceApp", "OPTION: one knob step and back to Android Auto (its controls shown)");
            break;
        case OptionKey::kTap: {
            aa::TouchEventOut e;
            memset(&e, 0, sizeof(e));
            e.n = 1;
            e.p[0].id = 0;
            e.p[0].x = optionTapX;
            e.p[0].y = optionTapY;
            e.action = 0;  // PRESS
            session.postTouchEvent(e);
            e.action = 1;  // RELEASE
            session.postTouchEvent(e);
            ELOG("ServiceApp", "OPTION: a tap at %d,%d to Android Auto", optionTapX, optionTapY);
            break;
        }
        case OptionKey::kKey:
            session.postKeyEvent(option.keycode, true);
            session.postKeyEvent(option.keycode, false);
            ELOG("ServiceApp", "OPTION: key %u to Android Auto", option.keycode);
            break;
        case OptionKey::kMenu:
            session.postRelativeEvent(aa::AAK_ROTARY_CONTROLLER, 1);  // "menu" in the file: the default nudge here
            session.postRelativeEvent(aa::AAK_ROTARY_CONTROLLER, -1);
            break;
    }
}

// NAV / MEDIA / COM / steering wheel phone opened an Android Auto app, or BACK / tilt left went to it (evo_knob.cpp):
// Android Auto stays on screen. iDrive may also have switched screens underneath; the iDrive app sees this file and,
// if iDrive left its blank state, puts it back.
void ServiceApp::appKey(const char* name, void* user) {
    (void)user;
    const std::string tmp = std::string(contract::kAppKeyFile) + ".tmp";
    FILE* f = fopen(tmp.c_str(), "w");
    if (f) {
        fprintf(f, "appkey %s\n", name);
        fclose(f);
        rename(tmp.c_str(), contract::kAppKeyFile);
    }
    if (!strcmp(name, "BACK") || !strcmp(name, "TILT_LEFT")) {
        // Every BACK goes through here: logged with the knob's own line only.
        return;
    }
    ELOG("ServiceApp", "key %s: opened in Android Auto (as with CarPlay), iDrive brought back under Android Auto",
         name);
}

void ServiceApp::mediaKey(unsigned state, void* user) {
    ServiceApp* a = static_cast<ServiceApp*>(user);
    pthread_mutex_lock(&a->mu);
    const bool allowed = a->ready && a->videoEnabled && !a->asking && !*a->quit_ &&
                         mediaRecallAllowed(a->session.isActive(), a->focus, a->stopping, a->inCall, a->reverse);
    if (a->mediaPress.feed(state, monoMs(), allowed)) {
        const bool written = writeMediaRecall(contract::kMediaRecallFile, static_cast<int>(getpid()), monoMs());
        ELOG("ServiceApp", "double MEDIA: guarded iDrive recall request %s", written ? "written" : "FAILED");
    }
    pthread_mutex_unlock(&a->mu);
}

// Commands from the iDrive app (service mode): show, hide, await, drivemode, popup, gear, call, daynight, quit.
void ServiceApp::command(const ControlCommand& c, void* user) {
    ServiceApp* a = static_cast<ServiceApp*>(user);
    pthread_mutex_lock(&a->mu);
    if (c.verb == "show" || c.verb == "hide" || c.verb == "await" || c.verb == "quit" ||
        (c.verb == "call" && (c.arg != 0) != a->inCall) || (c.verb == "gear" && (c.arg == 2) != a->reverse)) {
        a->mediaPress.cancel();
        unlink(contract::kMediaRecallFile);
    }
    if (c.verb == "show") {
        a->focus = true;
        a->asking = false;
        a->pendingFocus = 1;
    } else if (c.verb == "hide") {
        a->focus = false;
        a->asking = false;
        a->pendingFocus = 2;
    } else if (c.verb == "await") {
        // iDrive performs no screen request of an app while one of its own windows is on top, and the knob acts on
        // whatever iDrive shows under Android Auto. So the app first brings iDrive to its blank page, then says
        // "show": until then a phone that connects is served, its picture kept aside.
        if (!a->awaitShow) {
            ELOG("ServiceApp", "a phone that connects now waits aside for the iDrive app's \"show\"");
        }
        a->awaitShow = true;
    } else if (c.verb == "drivemode") {
        // Sent by older iDrive apps: ignored. The F22 shows the mode in the cluster, iDrive opens nothing.
    } else if (c.verb == "option") {
        // Chosen in the iDrive app's options panel, which then brings Android Auto back ("show"): sent once it is.
        a->pendingOption = c.hasArg && c.arg == 2 ? 2 : 1;
        a->pendingOptionMs = 0;
        ELOG("ServiceApp", "options panel: %s, once Android Auto is back", a->pendingOption == 2 ? "tap" : "map buttons");
    } else if (c.verb == "popup") {
        const PeekTiming::Popup popup = a->peek.popup(monoMs(), c.arg != 0);
        if (popup == PeekTiming::kPopupShown) {
            ELOG("ServiceApp", "iDrive popup: AA picture aside %u ms at most", kPopupShowMs);
        } else if (popup == PeekTiming::kPopupGone) {
            ELOG("ServiceApp", "iDrive popup closed: AA picture back");
        }
    } else if (c.verb == "gear") {
        const bool r = c.arg == 2;  // CDS driving.gear reverse (2 = reverse), logged by the app
        if (r != a->reverse) {
            ELOG("ServiceApp", "reverse gear %s: AA picture %s", r ? "engaged" : "left", r ? "hidden" : "back");
        }
        a->reverse = r;
    } else if (c.verb == "call") {  // iDrive app (CDS communication.callStatus): phone keys go to iDrive's call screen
        a->inCall = c.arg != 0;
        if (a->knob) {
            a->knob->setPhoneToIdrive(c.arg != 0);
        }
        ELOG("ServiceApp", "call %s: phone keys %s", c.arg ? "in progress" : "ended",
             c.arg ? "left to iDrive" : "to Android Auto");
    }
    pthread_mutex_unlock(&a->mu);
    if (c.verb == "daynight") {
        a->applyDayNight(c.arg, "iDrive app");
    }
    if (c.verb == "quit") {
        *a->quit_ = 1;
    }
}

void ServiceApp::applyDayNight(int v, const char* src) {
    if (v != 1 && v != 2) {
        return;
    }
    session.postNightMode(v == 2);
    ELOG("ServiceApp", "car day/night = %s (%s): Android Auto theme %s", v == 2 ? "night" : "day", src,
         v == 2 ? "dark" : "light");
}

void ServiceApp::videoFocus(bool projected, uint32_t reason, void* user) {
    ServiceApp* a = static_cast<ServiceApp*>(user);
    pthread_mutex_lock(&a->mu);
    if (a->focus != projected) {
        a->mediaPress.cancel();
        unlink(contract::kMediaRecallFile);
    }
    a->focus = projected;
    pthread_mutex_unlock(&a->mu);
    ELOG("ServiceApp", "video focus=%s reason=%u", projected ? "AA" : "BMW", reason);
}

// Phone opened/closed the car mic (Assistant). Session thread: non-blocking.
void ServiceApp::micRequest(bool open, void* user) {
    static_cast<ServiceApp*>(user)->mic.request(open);
    ELOG("ServiceApp", "phone: microphone %s", open ? "open" : "closed");
}

void ServiceApp::micData(const uint8_t* pcm, size_t len, void* user) {
    static_cast<ServiceApp*>(user)->session.postMicData(pcm, len);
}

// AA navigation status (ch 10): next maneuver for the cluster/HUD. Session thread: non-blocking.
void ServiceApp::navStatus(uint16_t msgId, const uint8_t* body, size_t len, void* user) {
    ServiceApp* a = static_cast<ServiceApp*>(user);
    evo::kombi::Arrows::onNavStatus(msgId, body, len, &a->kombi);
}

namespace {

// Replaces `path` whole (temporary file, then rename) so a reader never sees half of it. False on any failure.
bool writeWhole(const char* path, const void* data, size_t size) {
    const std::string tmp = std::string(path) + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) {
        return false;
    }
    const bool written = fwrite(data, 1, size, f) == size;
    if (fclose(f) != 0 || !written || rename(tmp.c_str(), path) != 0) {
        unlink(tmp.c_str());
        return false;
    }
    return true;
}

}  // namespace

// AA media status (ch 12): the track and its cover, kept for the iDrive (covers on the BMW screens). Session thread:
// writes two files in /tmp (memory), and only when the track or the cover changes.
void ServiceApp::mediaStatus(uint16_t msgId, const uint8_t* body, size_t len, void* user) {
    ServiceApp* a = static_cast<ServiceApp*>(user);
    const uint32_t bit = msgId >= 0x8000 && msgId < 0x8020 ? 1u << (msgId - 0x8000) : 0;
    if (bit && !(a->mediaIdsSeen & bit)) {
        a->mediaIdsSeen |= bit;
        ELOG("Media", "phone: media status 0x%04X first seen (%u bytes)", msgId, static_cast<unsigned>(len));
    }
    if (msgId != aa::msg::kPlaybackMetadata) {
        return;
    }
    media::Track t;
    if (!media::readMetadata(body, len, t)) {
        ELOG("Media", "track metadata malformed (%u bytes): ignored", static_cast<unsigned>(len));
        return;
    }
    const uint32_t print = t.cover.empty() ? 0 : media::fingerprint(t.cover);
    const std::string key = t.title + '\n' + t.artist + '\n' + t.album;
    if (print == a->coverPrint && key == a->trackKey) {
        return;
    }
    if (print != a->coverPrint) {
        unlink(contract::kCoverJpg);
        unlink(contract::kCoverPng);
        a->coverPath.clear();
        const std::string type = media::coverType(t.cover);
        const char* path = type == "jpg" ? contract::kCoverJpg : type == "png" ? contract::kCoverPng : nullptr;
        if (path && writeWhole(path, t.cover.data(), t.cover.size())) {
            a->coverPath = path;
        } else if (!t.cover.empty()) {
            ELOG("Media", "cover of %u bytes not saved (%s)", static_cast<unsigned>(t.cover.size()),
                 path ? "write failed" : "neither JPEG nor PNG");
        }
        a->coverPrint = print;
    }
    a->trackKey = key;
    const std::string track = key + '\n' + a->coverPath + '\n';
    if (!writeWhole(contract::kTrackFile, track.data(), track.size())) {
        ELOG("Media", "track file not written");
    }
    ELOG("Media", "track '%s' / '%s', length %lld s, cover %u bytes %s", t.title.c_str(), t.artist.c_str(),
         t.hasLength ? static_cast<long long>(t.lengthSeconds) : -1LL, static_cast<unsigned>(t.cover.size()),
         a->coverPath.empty() ? "(none saved)" : a->coverPath.c_str());
}

void ServiceApp::ended(void* user) {
    ServiceApp* a = static_cast<ServiceApp*>(user);
    // No stop()/join here: this callback runs on the AA session thread.
    a->mic.request(false);
    a->kombi.onSessionEnded();
    pthread_mutex_lock(&a->mu);
    a->focus = false;
    a->asking = false;
    a->mediaPress.cancel();
    unlink(contract::kMediaRecallFile);
    pthread_mutex_unlock(&a->mu);
    a->server.notifySessionEnded();
    ELOG("ServiceApp", "session ended");
}

void ServiceApp::connected(int fd, void* user) {
    static_cast<ServiceApp*>(user)->startProjection(fd, false);
}

void ServiceApp::usbPhoneReady(int fd) {
    startProjection(fd, true);
}

void ServiceApp::startProjection(int fd, bool overUsb) {
    ServiceApp* a = this;
    pthread_mutex_lock(&a->connectMu);
    if (a->stopRequested()) {
        close(fd);
        pthread_mutex_unlock(&a->connectMu);
        return;
    }
    pthread_mutex_lock(&a->mu);
    bool ready = a->ready;
    pthread_mutex_unlock(&a->mu);
    if (!ready) {
        close(fd);
        a->server.notifySessionEnded();
        pthread_mutex_unlock(&a->connectMu);
        return;
    }
    pthread_mutex_lock(&a->mu);
    ++(overUsb ? a->usbLinks : a->tcpLinks);
    a->tcpPreparing = true;
    a->mediaPress.cancel();
    unlink(contract::kMediaRecallFile);
    pthread_mutex_unlock(&a->mu);
    ELOG("ServiceApp", "phone %s connection received; preparing the video and the AA session", overUsb ? "USB" : "TCP");
    a->session.stop();  // join callbacks before destroying/reinitializing video buffers
    bool videoOk = true;
    if (a->videoEnabled) {
        pthread_mutex_lock(&a->videoMu);
        a->video.stop();
        videoOk = a->video.init(a->videoConfig) && a->video.start();
        pthread_mutex_unlock(&a->videoMu);
    }
    pthread_mutex_lock(&a->mu);
    if (!videoOk) {
        close(fd);
        a->server.notifySessionEnded();
        a->tcpPreparing = false;
        pthread_mutex_unlock(&a->mu);
        pthread_mutex_unlock(&a->connectMu);
        return;
    }
    a->focus = !a->awaitShow;
    a->asking = a->awaitShow;
    const bool aside = a->asking;
    pthread_mutex_unlock(&a->mu);
    if (aside) {
        ELOG("ServiceApp", "Android Auto starts aside: the iDrive app shows it once iDrive is on its page");
    }
    if (!overUsb) {
        a->wifiLink.connected(fd);  // before the session owns fd
    }
    if (!a->session.start(fd)) {
        a->server.notifySessionEnded();  // start owns/cleans fd even on failure
    } else {
        a->server.markSessionActive();
        if (!a->session.isActive()) {
            a->server.notifySessionEnded();
        }
    }
    pthread_mutex_lock(&a->mu);
    a->tcpPreparing = false;
    pthread_mutex_unlock(&a->mu);
    pthread_mutex_unlock(&a->connectMu);
}

void* ServiceApp::usb(void* user) {
    ServiceApp* a = static_cast<ServiceApp*>(user);
    UsbLoopSetup setup;
    setup.port = a->usbPort;
    setup.takeover = access(contract::kUsbTakeover, F_OK) == 0;
    if (setup.takeover) {
        ELOG("Usb", "%s present: an accessory held by another program is taken over", contract::kUsbTakeover);
    }
    runUsbLoop(setup, *a);
    pthread_mutex_lock(&a->mu);
    a->wirelessDone = true;
    pthread_mutex_unlock(&a->mu);
    return nullptr;
}

void* ServiceApp::wireless(void* user) {
    ServiceApp* a = static_cast<ServiceApp*>(user);
    runWirelessLoop(a->bt, a->ap, a->port, *a);
    pthread_mutex_lock(&a->mu);
    a->wirelessDone = true;
    pthread_mutex_unlock(&a->mu);
    return nullptr;
}

}  // namespace evo
