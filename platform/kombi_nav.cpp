// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// The arrows in the service: worker thread, setting and marker files, session hooks, the BMW navigation watch.
#include "kombi_nav.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>

#include "evo_log.hpp"
#include "kombi_most.hpp"
#include "service_contract.hpp"

namespace evo {
namespace kombi {

namespace {

const char* const kTag = "KombiNav";
const double kTickS = 0.25;
const double kSettingEveryS = 1.0;  // how often the off file is looked at
const double kWatchEveryS = 2.0;    // how often the BMW navigation is asked
const useconds_t kSliceUs = 50000;  // sleeps are cut in slices, so stop() is quick
const double kRetryLinkS = 30.0;
const int kLinkTries = 5;              // failed sends in a row without ever drawing: no KOMBI there
const double kCancelRetryForS = 60.0;  // the cancel goes on through a link blip, except when quitting
const double kCancelPauseS = 5.0;
const int kExitTimeoutMs = 1500;  // Force Stop sends SIGKILL 10 s after 'quit': a short exit cancel

volatile sig_atomic_t g_arrowMayBeUp = 0;  // read by the fatal-signal handler

// Sleeps `seconds` in slices, as long as `self` isn't quitting.
void doze(double seconds, bool (*quitting)(void*), void* self) {
    for (double slept = 0; slept < seconds && !quitting(self); slept += kSliceUs / 1e6) {
        usleep(kSliceUs);
    }
}

}  // namespace

ArrowsConfig::ArrowsConfig()
    : clusterAddress("160.48.199.96"),
      clusterPort(30502),
      handle(0xC8),
      offFile(evo::contract::kKombiOff),
      markerFile(evo::contract::kKombiShown),
      singleFile(evo::contract::kKombiSingle),
      hud6waFile(evo::contract::kKombi6wa),
      hud6waLiveFile(evo::contract::kKombi6waLive),
      anyDistanceFile(evo::contract::kKombiAnyDistance),
      junctionOffFile(evo::contract::kKombiJunctionOff),
      approachFile(evo::contract::kKombi6waApproach),
      lanesFile(evo::contract::kKombi6waLanes),
      mediaFile(evo::contract::kKombi6waMedia) {}

SendOutcome Arrows::Output::send(const std::vector<Request>& requests, std::string* why) {
    return arrows_.link_.send(requests, why);
}

bool Arrows::Output::cancel(std::string* why) {
    const double retryFor = quitting(&arrows_) ? 0.0 : kCancelRetryForS;
    return arrows_.link_.cancel(arrows_.graph_, retryFor, kCancelPauseS, &Arrows::quitting, &arrows_, why);
}

// kombi-6wa-live's content: the vMOST channel for the cluster calls (0 when empty or unreadable).
static unsigned liveChannel(const std::string& path) {
    unsigned channel = 0;
    FILE* f = path.empty() ? nullptr : fopen(path.c_str(), "r");
    if (f) {
        if (fscanf(f, "%u", &channel) != 1) {
            channel = 0;
        }
        fclose(f);
    }
    return channel;
}

// The setting files read once, before the guidance takes its copy of the settings.
ArrowsConfig withSettingFiles(const ArrowsConfig& config) {
    ArrowsConfig c = config;
    if (!c.anyDistanceFile.empty() && access(c.anyDistanceFile.c_str(), F_OK) == 0) {
        c.guide.approachMiles = 0;
    }
    return c;
}

Arrows::Arrows(const ArrowsConfig& config)
    : config_(withSettingFiles(config)),
      singleManeuver_(access(config_.singleFile.c_str(), F_OK) == 0),
      graph_(config_.handle, config_.guide.hideBar ? 255 : RouteGraph::kTemplateBar, singleManeuver_),
      link_(config_.clusterAddress, config_.clusterPort),
      output_(*this),
      guide_(graph_, output_, config_.guide),
      hud6wa_(!config_.hud6waFile.empty() && access(config_.hud6waFile.c_str(), F_OK) == 0),
      pipeSink_(evo::contract::kHudSender, evo::contract::kHudSenderLog, liveChannel(config_.hud6waLiveFile)),
      hudSink_(!config_.hud6waLiveFile.empty() && access(config_.hud6waLiveFile.c_str(), F_OK) == 0
                   ? static_cast<hudterm::HudSink&>(pipeSink_)
                   : static_cast<hudterm::HudSink&>(shadowSink_)),
      hud_(hudSink_, hudterm::k6wa, config_.guide),
      probe_(nullptr),
      lastMessageAt_(0),
      sessionEnded_(false),
      quit_(false),
      started_(false),
      inboxRevision_(0),
      guiding_(false),
      enabled_(false),
      settingCheckedAt_(-1e9),
      offUntilNextSession_(false),
      retryAt_(0),
      noSecondsNoted_(false),
      linkFailures_(0),
      marked_(false),
      most_(nullptr),
      bmwGuiding_(false),
      watching_(false) {
    pthread_mutex_init(&mu_, nullptr);
    pthread_mutex_init(&watchMu_, nullptr);
    const bool junction = config_.junctionOffFile.empty() || access(config_.junctionOffFile.c_str(), F_OK) != 0;
    hud_.setJunction(junction, 300);
    hud_.setLanes(!config_.lanesFile.empty() && access(config_.lanesFile.c_str(), F_OK) == 0);
    mediaOn_ = hud6wa_ && !config_.mediaFile.empty() && access(config_.mediaFile.c_str(), F_OK) == 0;
    if (config_.guide.approachMiles > 0 && !config_.approachFile.empty()) {  // any distance wins
        FILE* f = fopen(config_.approachFile.c_str(), "r");
        if (f) {
            char text[64] = "";
            if (!fgets(text, sizeof(text), f)) {
                text[0] = 0;
            }
            fclose(f);
            hudterm::HudApproach a = hud_.approach();
            if (hudterm::parseApproach(text, a)) {
                hud_.setApproach(a);
            } else {
                ELOG(kTag, "%s: not understood (time [warn s [main s]], distance or off): time 50 20 kept",
                     config_.approachFile.c_str());
            }
        }
    }
}

Arrows::~Arrows() {
    stop();
    pthread_mutex_destroy(&watchMu_);
    pthread_mutex_destroy(&mu_);
}

bool Arrows::settingOn() const {
    return access(config_.offFile.c_str(), F_OK) != 0;
}

bool Arrows::quitting(void* self) {
    Arrows* a = static_cast<Arrows*>(self);
    pthread_mutex_lock(&a->mu_);
    const bool quit = a->quit_;
    pthread_mutex_unlock(&a->mu_);
    return quit;
}

void Arrows::start(BmwProbe probe) {
    if (started_) {
        return;
    }
    probe_ = probe;
    link_.armEmergencyCancel(graph_);
    const GuideSettings& s = config_.guide;
    ELOG(kTag, "route graph: %s", singleManeuver_ ? "next maneuver only (14 requests)" : "whole captured route");
    ELOG(kTag,
         "cluster/HUD arrows %s; KOMBI %s:%u handle 0x%X, approach %g mi, bar %s, BMW-guidance yield %s, lanes %s, "
         "in-place %d",
         settingOn() ? "ON" : "off (kombi-off)", config_.clusterAddress.c_str(),
         static_cast<unsigned>(config_.clusterPort), config_.handle, s.approachMiles, s.hideBar ? "hidden" : "factory",
         probe_ ? "on" : "off", s.lanes ? "on" : "off", s.inPlace);
    if (hud6wa_) {
        const bool junction = config_.junctionOffFile.empty() || access(config_.junctionOffFile.c_str(), F_OK) != 0;
        ELOG(kTag,
             "kombi-6wa: the arrows go to an F-series 6WA cluster (MOST HUDTerminal), not the KOMBI above; %s; "
             "junction view %s; directions %s; approach %s (warn %g s, main prompt %g s, off-ramps and forks %g / %g "
             "m); "
             "lanes %s",
             hudSink_.live() ? "SENT to the cluster (kombi-6wa-live, car-test --serve)" : "log only, nothing is sent",
             junction ? "within 300 m" : "off (kombi-6wa-junction-off)",
             s.approachMiles > 0 ? "within the approach" : "at any distance",
             hudterm::approachName(hud_.approach().mode), hud_.approach().warnS, hud_.approach().mainS,
             hud_.approach().mwayWarnM, hud_.approach().mwayMainM, hud_.lanesOn() ? "on (kombi-6wa-lanes)" : "off");
        ELOG(kTag, "6WA song title when idle: %s", mediaOn_ ? "on (kombi-6wa-media)" : "off");
    }
    if (pthread_create(&worker_, nullptr, &Arrows::workerMain, this) != 0) {
        ELOG(kTag, "cannot start the worker thread: arrows disabled");
        return;
    }
    started_ = true;
    if (probe_) {
        if (pthread_create(&watcher_, nullptr, &Arrows::watchMain, this) == 0) {
            watching_ = true;
        } else {
            ELOG(kTag, "cannot start the BMW guidance watch: no yield");
        }
    }
}

void Arrows::stop() {
    if (!started_) {
        return;
    }
    pthread_mutex_lock(&mu_);
    quit_ = true;
    pthread_mutex_unlock(&mu_);
    if (watching_) {
        pthread_join(watcher_, nullptr);
        watching_ = false;
    }
    pthread_join(worker_, nullptr);
    started_ = false;
}

void* Arrows::watchMain(void* self) {
    static_cast<Arrows*>(self)->watchBmwGuidance();
    return nullptr;
}

// A thread of its own: the probe may block on D-Bus, the worker must not.
void Arrows::watchBmwGuidance() {
    int last = -2;
    while (!quitting(this)) {
        const int state = probe_();
        if (state != last) {
            ELOG(kTag, "BMW navigation: %s",
                 state == 1   ? "guiding, Android Auto arrows paused"
                 : state == 0 ? "not guiding"
                              : "state unknown (Android Auto arrows allowed)");
            last = state;
        }
        pthread_mutex_lock(&watchMu_);
        bmwGuiding_ = state == 1;
        pthread_mutex_unlock(&watchMu_);
        if (most_) {
            most_->setBmwGuiding(state == 1);
        }
        doze(kWatchEveryS, &Arrows::quitting, this);
    }
}

void Arrows::onNavStatus(uint16_t id, const uint8_t* body, size_t size, void* arrows) {
    Arrows* a = static_cast<Arrows*>(arrows);
    if (!a) {
        return;
    }
    pthread_mutex_lock(&a->mu_);
    if (a->inbox_.keep(id, body ? body : reinterpret_cast<const uint8_t*>(""), body ? size : 0)) {
        a->lastMessageAt_ = upSec();
    }
    pthread_mutex_unlock(&a->mu_);
}

void Arrows::setNowPlaying(const std::string& title) {
    pthread_mutex_lock(&mu_);
    nowPlaying_ = title;
    pthread_mutex_unlock(&mu_);
}

void Arrows::onSessionEnded() {
    pthread_mutex_lock(&mu_);
    inbox_.clear();
    nowPlaying_.clear();
    sessionEnded_ = true;
    pthread_mutex_unlock(&mu_);
}

void Arrows::onFatalSignal() {
    if (g_arrowMayBeUp) {
        ClusterLink::emergencyCancel();
    }
}

// The marker file and the fatal-signal flag follow whether an arrow may be up. They go only after an acknowledged
// cancel, or with force (a yield: the factory graph owns the models), so the next start retries a cancel that failed.
void Arrows::markArrow(bool up, bool force) {
    if (up == marked_) {
        return;
    }
    if (up) {
        const int fd = open(config_.markerFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            close(fd);
        }
        g_arrowMayBeUp = 1;
        marked_ = true;
    } else if (force || guide_.lastCancelOk()) {
        unlink(config_.markerFile.c_str());
        g_arrowMayBeUp = 0;
        marked_ = false;
    }
}

void* Arrows::workerMain(void* self) {
    static_cast<Arrows*>(self)->work();
    return nullptr;
}

void Arrows::work() {
    // An arrow left up by a killed service (SIGKILL after Force Stop, or the head unit alone losing power): one cancel.
    if (!hud6wa_ && access(config_.markerFile.c_str(), F_OK) == 0) {
        std::string why;
        const bool ok = link_.cancel(graph_, 0.0, kCancelPauseS, &Arrows::quitting, this, &why);
        ELOG(kTag, "arrow possibly left by the previous run: cancel %s%s%s", ok ? "OK" : "FAILED", ok ? "" : ": ",
             ok ? "" : why.c_str());
        if (ok) {
            unlink(config_.markerFile.c_str());
        } else {
            g_arrowMayBeUp = 1;  // the file stays: the crash handler and the next start try again
        }
    }
    while (!quitting(this)) {
        tick(upSec());
        doze(kTickS, &Arrows::quitting, this);
    }
    link_.setTimeoutMs(kExitTimeoutMs);
    if (guide_.showing() || guide_.sentAny()) {
        guide_.clear("entry-aad exiting");
    }
    hud_.clear("entry-aad exiting");
    markArrow(false);
}

void Arrows::tick(double now) {
    if (now - settingCheckedAt_ >= kSettingEveryS) {
        settingCheckedAt_ = now;
        const bool on = settingOn();
        if (on != enabled_) {
            ELOG(kTag, "cluster/HUD arrows turned %s", on ? "ON" : "off");
            enabled_ = on;
            if (!on && (guide_.showing() || guide_.sentAny())) {
                guide_.clear("turned off");
            }
            if (!on) {
                hud_.clear("turned off");
            }
        }
    }
    markArrow(guide_.showing() || guide_.sentAny());
    if (!enabled_) {
        return;
    }

    // The messages are copied and read again only when one came in, not on every tick.
    pthread_mutex_lock(&mu_);
    const bool newMessages = inbox_.revision() != inboxRevision_;
    NavInbox inbox;
    if (newMessages) {
        inbox = inbox_;
        inboxRevision_ = inbox_.revision();
    }
    const double lastMessageAt = lastMessageAt_;
    const bool ended = sessionEnded_;
    sessionEnded_ = false;
    pthread_mutex_unlock(&mu_);
    if (newMessages) {
        guiding_ = inbox.guiding() && inbox.nextManeuver(next_);
    }

    if (ended) {
        if (guide_.showing() || guide_.sentAny()) {
            guide_.clear("Android Auto session ended");
        }
        guide_.resumeAfterStop();
        hud_.clear("Android Auto session ended");
        hud_.resume();
        noSecondsNoted_ = false;
        offUntilNextSession_ = false;
        retryAt_ = 0;
        linkFailures_ = 0;
        markArrow(guide_.showing() || guide_.sentAny());
        return;
    }
    if (offUntilNextSession_ || now < retryAt_) {
        return;
    }
    pthread_mutex_lock(&watchMu_);
    const bool bmwGuiding = bmwGuiding_;
    pthread_mutex_unlock(&watchMu_);
    if (bmwGuiding) {
        guide_.yieldToFactory();
        hud_.yieldToFactory();
        markArrow(false, true);
        return;
    }
    const Maneuver* shown = nullptr;
    const char* why = "guidance ended";
    if (guiding_) {
        if (now - lastMessageAt > config_.guide.staleS) {
            why = "navigation status frozen";
        } else {
            shown = &next_;
        }
    }
    if (hud6wa_) {
        hud_.step(shown, now, why);
        if (mediaOn_ && !shown) {
            pthread_mutex_lock(&mu_);
            const std::string playing = nowPlaying_;
            pthread_mutex_unlock(&mu_);
            hud_.showIdleText(hud_.idleText(playing), now);
        }
        if (hud_.takeNoSeconds() && !noSecondsNoted_) {
            noSecondsNoted_ = true;
            ELOG(kTag, "the phone sends no seconds to the next turn: the 6WA uses the distance rule (once a session)");
        }
        if (hud_.stopped()) {
            ELOG(kTag, "the 6WA driver stopped: arrows off until the next Android Auto session");
            offUntilNextSession_ = true;
        }
        return;
    }
    guide_.step(shown, now, why);
    if (guide_.showing()) {
        linkFailures_ = 0;
        if (most_) {
            most_->setEthernetSeen();  // a G-series KOMBI: the MOST cluster is left alone
        }
    }
    if (guide_.stopped() == Guide::kStoppedRefused) {
        ELOG(kTag,
             "the KOMBI rejected a frame: arrows off until the next Android Auto session (another instance "
             "handle on this car?)");
        offUntilNextSession_ = true;
        guide_.resumeAfterStop();
    } else if (guide_.stopped() == Guide::kStoppedLink) {
        guide_.resumeAfterStop();
        if (++linkFailures_ >= kLinkTries) {
            ELOG(kTag, "no KOMBI at %s:%u after %d tries: arrows off until the next Android Auto session",
                 config_.clusterAddress.c_str(), static_cast<unsigned>(config_.clusterPort), linkFailures_);
            offUntilNextSession_ = true;
        } else {
            ELOG(kTag, "KOMBI link failed: retrying in %.0f s", kRetryLinkS);
            retryAt_ = now + kRetryLinkS;
        }
    }
    markArrow(guide_.showing() || guide_.sentAny());
}

}  // namespace kombi
}  // namespace evo
