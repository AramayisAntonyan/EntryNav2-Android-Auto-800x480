// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// The Camera app, an iDrive app of its own under My Vehicle (libAuxCamera.so, AuxCamera.xml): the picture of an
// analog camera through a USB "EasyCap" grabber (MacroSilicon 534d:0021, UVC with MJPEG), on the car's USB port.
//   Camera   the newest picture, a few times a second, while the page shows; a line over it when there is none
//            (no adapter, no signal); a status line under it (size, pictures per second, what iDrive did with them)
//   OPTION   the picture size (480 x 320, 640 x 480, 720 x 480: the adapter's MJPEG sizes), restart the capture
// The adapter has one input (composite: its descriptors have no selector unit), so there is no input choice.
//
// The kernel has no uvcvideo: a thread of the app drives the adapter through usbfs (platform/uvc_usbfs.cpp, as
// uvc-probe --usbfs-grab), only ever the device with that vendor:product id (the phone may use the same port), only
// while the page shows. It writes the newest JPEG to /tmp/aux-camera/frame.jpg (a new file renamed over the old: a
// reader never sees half a picture); the app thread sends it through an raImageModel by its path, as Android Auto's
// album cover is sent. Whether iDrive draws a JPEG there is the first thing to check on the car: the status line says
// "sent" when bmw::Image read the file, app.log has the first pictures' results.
//
// Kept in /var/local/data/AA: camera-size (0..2). /var/local/data/AA/camera-off keeps the app from being created.
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../platform/service_contract.hpp"
#include "../platform/uvc_usbfs.hpp"
#include "app_log.hpp"
#include "bmw_framework.hpp"
#include "camera_ids.hpp"

namespace evo {
namespace app {
namespace {

using namespace camera;

const unsigned kVendor = 0x534d, kProduct = 0x0021;  // MacroSilicon "AV TO USB2.0"
const char kFrameDir[] = "/tmp/aux-camera";
const char kFramePath[] = "/tmp/aux-camera/frame.jpg";
const char kFrameTmp[] = "/tmp/aux-camera/frame.tmp";
const char kSizeFile[] = "/var/local/data/AA/camera-size";
const char kOffFile[] = "/var/local/data/AA/camera-off";
const unsigned kRefreshMs = 250;  // the app thread sends the newest picture at most 4 times a second
const unsigned kWriteMs = 200;    // the capture thread writes one at most every 200 ms (the adapter gives 25)
const unsigned kIdleMs = 3000;    // no picture for this long: the stream is restarted
const long kMaxFrameBytes = 512 * 1024;
const unsigned kSizes[kSizeCount][2] = {{480, 320}, {640, 480}, {720, 480}};

bmw::PluginApp* g_app = nullptr;
unsigned g_size = 0;   // kSizes index
bool g_shown = false;  // the camera page shows
int g_imageShown[kSizeCount] = {-1, -1, -1};
char g_shownText[2][128];

uint64_t monoMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000u + static_cast<uint64_t>(ts.tv_nsec) / 1000000u;
}

// ---- the capture thread ---------------------------------------------------------------------------------------------
enum CaptureState {
    kIdle = 0,
    kLooking,    // looking for the adapter
    kNoAdapter,  // not on USB
    kStreaming,  // streaming (pictures may still be missing)
    kNoSignal,   // streaming started, no picture came
    kRefused,    // open, claim or the format refused
};

pthread_t g_thread;
bool g_threadStarted = false;
int g_stop = 0;            // atomic: the thread ends
int g_state = kIdle;       // atomic: CaptureState
unsigned g_written = 0;    // atomic: pictures written
unsigned g_wantSize = 0;   // the size the thread streams (set before it starts)
unsigned g_attempt = 0;    // the thread's stream attempts since a picture came (its log stays short)
uint64_t g_lastWrite = 0;  // the thread's last write

void setState(int state) {
    __atomic_store_n(&g_state, state, __ATOMIC_RELEASE);
}

bool stopping() {
    return __atomic_load_n(&g_stop, __ATOMIC_ACQUIRE) != 0;
}

// Sleeps up to ms, ending early when the thread is to stop.
void nap(unsigned ms) {
    for (unsigned t = 0; t < ms && !stopping(); t += 100) {
        usleep(100 * 1000);
    }
}

// The stream's log lines, only for the first attempts after a picture (a missing adapter is retried for as long as
// the page shows).
void streamLog(const char* fmt, ...) {
    if (g_attempt > 2) {
        return;
    }
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    alog("camera: %s", line);
}

bool writeFrame(void*, const unsigned char* jpeg, size_t size, unsigned number) {
    if (stopping()) {
        return false;
    }
    setState(kStreaming);
    g_attempt = 0;
    const uint64_t now = monoMs();
    if (number > 2 && now - g_lastWrite >= kWriteMs && size <= static_cast<size_t>(kMaxFrameBytes)) {
        FILE* f = fopen(kFrameTmp, "wb");
        bool ok = f && fwrite(jpeg, 1, size, f) == size;
        if (f) {
            ok = fclose(f) == 0 && ok;
        }
        if (ok && rename(kFrameTmp, kFramePath) == 0) {
            g_lastWrite = now;
            __atomic_add_fetch(&g_written, 1, __ATOMIC_RELEASE);
        }
    }
    return !stopping();
}

// Picks the adapter's MJPEG frame size matching the wanted one (1..), 0 (the largest) when it has none.
unsigned frameIndexFor(const uvc::Info& info, unsigned size) {
    for (unsigned i = 0; i < info.frameCount; ++i) {
        if (info.frames[i][0] == kSizes[size][0] && info.frames[i][1] == kSizes[size][1]) {
            return i + 1;
        }
    }
    return 0;
}

void* captureMain(void*) {
    mkdir(kFrameDir, 0755);
    g_attempt = 0;
    bool loggedMissing = false;
    while (!stopping()) {
        char port[32];
        setState(kLooking);
        if (!uvc::findDevice(kVendor, kProduct, port, sizeof(port))) {
            setState(kNoAdapter);
            if (!loggedMissing) {
                alog("camera: no adapter %04x:%04x on USB (retried while the page shows)", kVendor, kProduct);
                loggedMissing = true;
            }
            nap(1000);
            continue;
        }
        loggedMissing = false;
        ++g_attempt;
        uvc::Info info;
        unsigned index = 0;
        if (uvc::readInfo(port, &info)) {
            index = frameIndexFor(info, g_wantSize);
            streamLog("adapter on %s, %u MJPEG sizes, %ux%u %s", port, info.frameCount, kSizes[g_wantSize][0],
                      kSizes[g_wantSize][1], index ? "offered" : "not offered: the largest instead");
        }
        uvc::Stats st;
        const uvc::StreamResult r =
            uvc::stream(port, index, &writeFrame, nullptr, &g_stop, 0, kIdleMs, &streamLog, &st);
        if (g_attempt <= 2 || r == uvc::kStreamEnded) {
            alog("camera: stream on %s ended (%d): %u packets, %u frames, %u errors, %u pictures written", port,
                 static_cast<int>(r), st.packets, st.frames, st.errors, __atomic_load_n(&g_written, __ATOMIC_ACQUIRE));
        }
        if (stopping()) {
            break;
        }
        setState(r == uvc::kStreamTimeout ? kNoSignal : kRefused);
        nap(g_attempt < 3 ? 1000 : 5000);  // a refusal is not retried quickly
    }
    setState(kIdle);
    return nullptr;
}

void startCapture() {
    if (g_threadStarted) {
        return;
    }
    __atomic_store_n(&g_stop, 0, __ATOMIC_RELEASE);
    g_wantSize = g_size;
    g_lastWrite = 0;
    setState(kLooking);
    g_threadStarted = pthread_create(&g_thread, nullptr, &captureMain, nullptr) == 0;
    alog("camera: capture %s (%ux%u)", g_threadStarted ? "started" : "NOT started", kSizes[g_size][0],
         kSizes[g_size][1]);
}

// Waits for the thread (it checks every 200 ms at most).
void stopCapture() {
    if (!g_threadStarted) {
        return;
    }
    __atomic_store_n(&g_stop, 1, __ATOMIC_RELEASE);
    pthread_join(g_thread, nullptr);
    g_threadStarted = false;
    setState(kIdle);
    alog("camera: capture stopped");
}

// ---- the page
// --------------------------------------------------------------------------------------------------------
void setText(unsigned which, const char* text) {
    if (!g_app || !strcmp(g_shownText[which], text)) {
        return;
    }
    g_app->updateModel(which == 0 ? kMessageModel : kStatusModel, bmw::BasicString<char>(text));
    snprintf(g_shownText[which], sizeof(g_shownText[which]), "%s", text);
}

void showImage(unsigned size, bool visible) {
    if (g_app && g_imageShown[size] != (visible ? 1 : 0)) {
        g_app->setProperty(kFrameImage0 + size, kPropVisible, bmw::Parcel(visible));
        g_imageShown[size] = visible ? 1 : 0;
    }
}

void forgetShown() {
    memset(g_shownText, 0, sizeof(g_shownText));
    for (unsigned s = 0; s < kSizeCount; ++s) {
        g_imageShown[s] = -1;
    }
}

unsigned g_sentSeq = 0;    // the last picture written that was sent
unsigned g_sent = 0;       // pictures sent since the page showed
unsigned g_notLoaded = 0;  // pictures bmw::Image could not read
uint64_t g_lastSent = 0;   // when the last one was sent
uint64_t g_rateSince = 0;  // pictures per second, over the last few seconds
unsigned g_rateCount = 0;
double g_rate = 0;

// The newest picture to iDrive, when there is a new one. The first pictures' results go to app.log, then every 100th.
void sendFrame() {
    const unsigned seq = __atomic_load_n(&g_written, __ATOMIC_ACQUIRE);
    if (seq == g_sentSeq) {
        return;
    }
    g_sentSeq = seq;
    struct stat st;
    if (stat(kFramePath, &st) != 0 || st.st_size <= 0 || st.st_size > kMaxFrameBytes) {
        return;
    }
    bmw::Image image{bmw::BasicString<char>(kFramePath)};
    const bool loaded = image.isLoaded();
    if (loaded) {
        g_app->updateModel(kFrameModel0 + g_size, image);
        ++g_sent;
        ++g_rateCount;
        g_lastSent = monoMs();
    } else {
        ++g_notLoaded;
    }
    const unsigned n = g_sent + g_notLoaded;
    if (n <= 5 || n % 100 == 0) {
        alog("camera: picture %u (%ld bytes, %ux%u) %s", n, static_cast<long>(st.st_size), kSizes[g_size][0],
             kSizes[g_size][1], loaded ? "sent to iDrive" : "NOT read by bmw::Image");
    }
}

void showPage() {
    const uint64_t now = monoMs();
    if (now - g_rateSince >= 3000) {
        g_rate = g_rateSince ? g_rateCount * 1000.0 / static_cast<double>(now - g_rateSince) : 0;
        g_rateSince = now;
        g_rateCount = 0;
    }
    const bool live = g_lastSent && now - g_lastSent < 2000;
    for (unsigned s = 0; s < kSizeCount; ++s) {
        showImage(s, live && s == g_size);
    }
    const int state = __atomic_load_n(&g_state, __ATOMIC_ACQUIRE);
    const char* message = "";
    if (!live) {
        switch (state) {
            case kNoAdapter:
                message = "No camera adapter on USB (EasyCap 534d:0021)";
                break;
            case kNoSignal:
                message = "No signal from the camera adapter";
                break;
            case kRefused:
                message = "The camera adapter refused the capture";
                break;
            case kStreaming:
                message = g_notLoaded ? "iDrive did not take the picture" : "Waiting for a picture...";
                break;
            default:
                message = "Looking for the camera adapter...";
        }
    }
    setText(0, message);
    char status[128];
    snprintf(status, sizeof(status), "%u x %u, composite input   %.1f pictures/s   sent %u, not read %u",
             kSizes[g_size][0], kSizes[g_size][1], g_rate, g_sent, g_notLoaded);
    setText(1, status);
}

AppRunnable g_refreshRunnable;
bool g_refreshPosted = false;

void refreshRun(void*) {
    g_refreshPosted = false;
    if (!g_app || !g_shown) {
        return;
    }
    sendFrame();
    showPage();
    g_refreshPosted = true;
    postRunnable(g_app, g_refreshRunnable, kRefreshMs);
}

void pageVisible(bool visible) {
    alog("camera: page %s", visible ? "shown" : "hidden");
    if (!visible) {
        g_shown = false;
        stopCapture();
        return;
    }
    g_shown = true;
    forgetShown();  // the HMI may have dropped the models while hidden: everything sent again
    g_sent = g_notLoaded = 0;
    g_sentSeq = __atomic_load_n(&g_written, __ATOMIC_ACQUIRE);  // an old picture is not shown again
    g_lastSent = 0;
    g_rateSince = 0;
    startCapture();
    showPage();
    if (!g_refreshPosted) {
        g_refreshPosted = true;
        postRunnable(g_app, g_refreshRunnable, kRefreshMs);
    }
}

void saveSize() {
    FILE* f = fopen(kSizeFile, "w");
    if (f) {
        fprintf(f, "%u\n", g_size);
        fclose(f);
    }
}

void loadSize() {
    FILE* f = fopen(kSizeFile, "r");
    unsigned v = 0;
    if (f) {
        if (fscanf(f, "%u", &v) != 1 || v >= kSizeCount) {
            v = 0;
        }
        fclose(f);
    }
    g_size = v;
}

// ---- iDrive's notifications
// ------------------------------------------------------------------------------------------ The entry: the
// combinedAction's raAction, its hmiAction opens the page. Its own visibility notice may never come (as the Gauges
// dashboard's): treated as shown from here.
bool onEntry(void*, const bmw::Parcel&) {
    alog("camera: entry pressed");
    pageVisible(true);
    return true;
}

bool onVisible(void*, const bmw::Parcel& value) {
    const bool visible = parcelBool(value);
    if (visible != g_shown) {
        pageVisible(visible);
    }
    return true;
}

void restartCapture(const char* why) {
    alog("camera: %s: capture restarted", why);
    const bool was = g_threadStarted;
    stopCapture();
    if (was || g_shown) {
        startCapture();
    }
}

template <unsigned Size>
bool onSize(void*, const bmw::Parcel&) {
    if (Size != g_size) {
        g_size = Size;
        saveSize();
        g_sent = g_notLoaded = 0;
        g_lastSent = 0;
        char why[48];
        snprintf(why, sizeof(why), "size %ux%u chosen", kSizes[Size][0], kSizes[Size][1]);
        restartCapture(why);
    }
    if (g_shown) {
        showPage();
    }
    return true;
}

bool onRestart(void*, const bmw::Parcel&) {
    g_sent = g_notLoaded = 0;
    g_lastSent = 0;
    restartCapture("OPTION Restart");
    if (g_shown) {
        showPage();
    }
    return true;
}

// ---- the app object
// ---------------------------------------------------------------------------------------------------
bool cameraOnInit(bmw::PluginApp* self) {
    addAction(self, kActEntry, &onEntry);
    addStateHandler(self, kCameraState, kHandlerVisible, &onVisible);
    addAction(self, kActSize0, &onSize<0>);
    addAction(self, kActSize0 + 1, &onSize<1>);
    addAction(self, kActSize0 + 2, &onSize<2>);
    addAction(self, kActRestart, &onRestart);
    loadSize();
    makeRunnable(g_refreshRunnable, &refreshRun);
    const bool ok = self->onInit();
    alog("camera: onInit -> %d, size %ux%u", static_cast<int>(ok), kSizes[g_size][0], kSizes[g_size][1]);
    return ok;
}

void cameraOnError(bmw::PluginApp*, unsigned code, const bmw::BasicString<char>& text) {
    const char* s = text.raw();
    alog("camera: onError %u: %s", code, s ? s : "(null)");
}

void cameraOnFinalize(bmw::PluginApp* self) {
    alog("camera: onFinalize");
    g_shown = false;
    stopCapture();
    self->onFinalize();
}

bmw::PluginApp* createCamera(bmw::IDispatcher& dispatcher, const char* name, const bmw::Bundle& params,
                             const bmw::Bundle& config) {
    mkdir(contract::kDataDir, 0755);
    alog("camera: create(name=%s), HMI pid %d", name ? name : "(null)", static_cast<int>(getpid()));
    if (access(kOffFile, F_OK) == 0) {
        alog("camera: %s exists: not created", kOffFile);
        return nullptr;
    }
    if (g_app) {
        return nullptr;
    }
    forgetShown();
    const PluginAppHooks hooks = {&cameraOnInit, &cameraOnError, &cameraOnFinalize};
    void* base = nullptr;
    g_app = newPluginApp(dispatcher, name, params, config, hooks, &base);
    return g_app;
}

void destroyCamera(void* app) {
    alog("camera: destroy(%p)", app);
    if (!app) {
        return;
    }
    if (app == g_app) {
        g_shown = false;
        stopCapture();
        g_app = nullptr;
    }
    deletePluginApp(app);
}

}  // namespace
}  // namespace app
}  // namespace evo

// What ManagedPluginApp::init reads with dlsym (as in entry_aa_app.cpp): create at +0x10, destroy at +0x14.
extern "C" {
struct CameraPluginAppInfo {
    uint16_t versionMajor;
    uint16_t versionMinor;
    const char* name;
    const char* vendor;
    uint32_t rhmiInterfaceVersion;
    bmw::PluginApp* (*create)(bmw::IDispatcher&, const char*, const bmw::Bundle&, const bmw::Bundle&);
    void (*destroy)(void*);
};
__attribute__((visibility("default"))) CameraPluginAppInfo pluginAppInfo = {
    1, 0, "Camera", "AuxCamera", 1, &evo::app::createCamera, &evo::app::destroyCamera};
}
