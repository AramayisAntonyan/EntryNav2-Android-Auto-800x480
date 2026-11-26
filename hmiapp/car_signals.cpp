// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// The CDS handlers: car gear, park assist, phone calls, day/night, driving mode and the iDrive screen, to the service
// and the hold (car_signals.hpp).
#include "car_signals.hpp"

#include <cxxabi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../platform/atomic_file.hpp"
#include "../platform/service_contract.hpp"
#include "app_log.hpp"
#include "cds_parse.hpp"
#include "idrive_coding.hpp"
#include "mono_clock.hpp"
#include "popup_peek.hpp"
#include "service_link.hpp"

namespace evo {
namespace app {

namespace {

const char* const kGearProperty = "driving.gear";
// Park assist, published as '{"PDCStatus":<n>}' (1 = on). It also comes on with its button, without reverse.
const char* const kPdcProperty = "sensors.PDCStatus";
// The iDrive screen shown: '{"graphicalContext":{"visibleContext":<n>,...}}'.
const char* const kScreenProperty = "hmi.graphicalContext";
const char* const kCallProps[2] = {"communication.callStatus0", "communication.callStatus1"};
// Car day/night and driving mode (CDS names present in this HU's libhmiremoting).
const char* const kDayNightProperty = "vehicle.dayNightChangeOver";
const char* const kDriveModeProps[2] = {"driving.mode", "driving.mode35up"};

HoldMachine* g_hold = nullptr;

// ---- car gear -> service (reverse: the camera shows, Android Auto steps aside) --------------------------------------
int g_lastGear = kGearUnknown;
bool onGearChanged(void*, const bmw::BasicString<unsigned short>& value) {
    try {
        if (!g_hold) {
            return true;
        }
        char text[160];
        narrow(value, text, sizeof(text));
        int gear = -1;
        for (size_t i = 0; text[i]; ++i) {
            if (text[i] >= '0' && text[i] <= '9') {
                gear = atoi(text + i);
                break;
            }
        }
        if (gear != g_lastGear) {
            alog("CDS %s = '%s' -> gear %d", kGearProperty, text, gear);
            g_lastGear = gear;
            if (g_hold->gearChanged(gear, monoSeconds())) {
                char msg[32];
                snprintf(msg, sizeof(msg), "gear %d", gear);
                sendToService(msg);
            }
        }
    } catch (abi::__forced_unwind&) {
        throw;  // a thread being cancelled
    } catch (...) {
        alog("exception in onGearChanged");
    }
    return true;
}

// ---- park assist -> hold machine (its view shows, Android Auto steps aside)
// ------------------------------------------
int g_lastPdc = kPdcUnknown;
bool onPdcChanged(void*, const bmw::BasicString<unsigned short>& value) {
    try {
        if (!g_hold) {
            return true;
        }
        char text[160];
        narrow(value, text, sizeof(text));
        const int pdc = aa::cdsIntField(text, "PDCStatus", -1);
        if (pdc != g_lastPdc) {
            alog("CDS %s = '%s' -> park assist %d", kPdcProperty, text, pdc);
            g_lastPdc = pdc;
            g_hold->pdcChanged(pdc);
        }
    } catch (abi::__forced_unwind&) {
        throw;  // a thread being cancelled
    } catch (...) {
        alog("exception in onPdcChanged");
    }
    return true;
}

// ---- the iDrive screen shown: logged, and told to the hold machine (its radio page, #138) ---------------------------
// Which numbers the camera and panorama views have on this head unit is not known: the log tells, screen by screen.
int g_lastScreen = -1000;
bool onScreenChanged(void*, const bmw::BasicString<unsigned short>& value) {
    try {
        if (!g_hold) {
            return true;
        }
        char text[240];
        narrow(value, text, sizeof(text));
        const int screen = aa::cdsIntField(text, "visibleContext", -1);
        if (screen != g_lastScreen) {
            g_lastScreen = screen;
            alog("CDS %s = '%s'", kScreenProperty, text);
            g_hold->screenChanged(screen);
        }
    } catch (abi::__forced_unwind&) {
        throw;  // a thread being cancelled
    } catch (...) {
        alog("exception in onScreenChanged");
    }
    return true;
}

// ---- phone calls: iDrive may show its own call screen; Android Auto shows the call too ------------------------------
bool g_callLine[2] = {false, false};
bool onCallStatus(int line, const bmw::BasicString<unsigned short>& value) {
    try {
        if (!g_hold) {
            return true;
        }
        char text[200];
        narrow(value, text, sizeof(text));
        bool active = aa::cdsCallActive(text);
        bool was = callActive();
        g_callLine[line] = active;
        g_hold->callChanged(callActive(), monoSeconds());
        alog("CDS %s = '%s' -> %s", kCallProps[line], text, active ? "in a call" : "idle");
        if (was != callActive()) {
            sendToService(callActive() ? "call 1" : "call 0");  // phone keys follow iDrive's call
        }
    } catch (abi::__forced_unwind&) {
        throw;  // a thread being cancelled
    } catch (...) {
        alog("exception in onCallStatus");
    }
    return true;
}
bool onCallStatus0(void*, const bmw::BasicString<unsigned short>& v) {
    return onCallStatus(0, v);
}
bool onCallStatus1(void*, const bmw::BasicString<unsigned short>& v) {
    return onCallStatus(1, v);
}

// ---- day and night --------------------------------------------------------------------------------------------------
int g_lastDayNight = -1000;
bool onDayNightChanged(void*, const bmw::BasicString<unsigned short>& value) {
    try {
        if (!g_hold) {
            return true;
        }
        char text[160];
        narrow(value, text, sizeof(text));
        int v = aa::cdsIntField(text, "dayNightChangeOver", -1);
        if (v == g_lastDayNight) {
            return true;
        }
        g_lastDayNight = v;
        alog("CDS %s = '%s' -> %d (1 day, 2 night, 0 no change, 3 invalid)", kDayNightProperty, text, v);
        if (v != 1 && v != 2) {
            return true;
        }
        char line[32];
        const int n = snprintf(line, sizeof(line), "daynight %d\n", v);
        if (n > 0 && n < static_cast<int>(sizeof(line))) {
            writeAtomicFile(contract::kDayNightFile, line, static_cast<size_t>(n));
        }
        char msg[32];
        snprintf(msg, sizeof(msg), "daynight %d", v);
        sendToService(msg);
    } catch (abi::__forced_unwind&) {
        throw;  // a thread being cancelled
    } catch (...) {
        alog("exception in onDayNightChanged");
    }
    return true;
}

// ---- driving mode ---------------------------------------------------------------------------------------------------
int g_lastDriveMode[2] = {-1000, -1000};
bool onDriveMode(int prop, const bmw::BasicString<unsigned short>& value) {
    try {
        if (!g_hold) {
            return true;
        }
        char text[160];
        narrow(value, text, sizeof(text));
        int v = aa::cdsIntField(text, "mode", -1);
        if (v == g_lastDriveMode[prop]) {
            return true;
        }
        const int invalid = prop == 1 ? 63 : 15;  // "not valid" values
        alog("CDS %s = '%s' -> mode %d%s", kDriveModeProps[prop], text, v, v == invalid || v < 0 ? " (not valid)" : "");
        g_lastDriveMode[prop] = v;
        // Logged only (this fork): the F22 shows the mode in the cluster and iDrive opens nothing, so Android Auto
        // stays on screen; should a popup open after all, the ordinary popup peek steps aside for it.
    } catch (abi::__forced_unwind&) {
        throw;  // a thread being cancelled
    } catch (...) {
        alog("exception in onDriveMode");
    }
    return true;
}
bool onDriveMode0(void*, const bmw::BasicString<unsigned short>& v) {
    return onDriveMode(0, v);
}
bool onDriveMode1(void*, const bmw::BasicString<unsigned short>& v) {
    return onDriveMode(1, v);
}

// ---- observed only: logged on change, to learn their JSON on the car ------------------------------------------------
// The phone's Bluetooth connection (to start Android Auto when it connects) and the iDrive's audio source (to switch
// it to Bluetooth audio when Android Auto starts). Nothing acts on them yet. Then two slow car values for the cluster
// gauges: engine.temperature, {"temperature":{"engine":coolant,"oil":oil}} as AAIdrive reads it, and the battery.
const char* const kObservedProps[] = {"communication.phoneStatus",
                                      "communication.phone",
                                      "entertainment.descriptionCurrentSource",
                                      "entertainment.statusAvailableSources",
                                      "engine.temperature",
                                      "sensors.battery"};
const size_t kObservedCount = sizeof(kObservedProps) / sizeof(kObservedProps[0]);
char g_observedLast[kObservedCount][320];

template <size_t N>
bool onObserved(void*, const bmw::BasicString<unsigned short>& value) {
    char text[320];
    narrow(value, text, sizeof(text));
    if (strcmp(text, g_observedLast[N]) != 0) {
        snprintf(g_observedLast[N], sizeof(g_observedLast[N]), "%s", text);
        alog("CDS %s = '%s' (observed)", kObservedProps[N], text);
    }
    return true;
}

}  // namespace

void subscribeCarSignals(bmw::PluginApp* app, HoldMachine& hold) {
    g_hold = &hold;
    addCdsHandler(app, kGearProperty, &onGearChanged);
    alog("CDS handler for %s registered", kGearProperty);
    addCdsHandler(app, kPdcProperty, &onPdcChanged);
    alog("CDS handler for %s registered", kPdcProperty);
    addCdsHandler(app, kScreenProperty, &onScreenChanged);
    alog("CDS handler for %s registered", kScreenProperty);
    for (int i = 0; i < 2; ++i) {
        addCdsHandler(app, kCallProps[i], i == 0 ? &onCallStatus0 : &onCallStatus1);
        alog("CDS handler for %s registered", kCallProps[i]);
    }
    addCdsHandler(app, kDayNightProperty, &onDayNightChanged);
    alog("CDS handler for %s registered", kDayNightProperty);
    for (int i = 0; i < 2; ++i) {
        addCdsHandler(app, kDriveModeProps[i], i == 0 ? &onDriveMode0 : &onDriveMode1);
        alog("CDS handler for %s registered", kDriveModeProps[i]);
    }
    static_assert(kObservedCount == 6, "one handler per observed property");
    addCdsHandler(app, kObservedProps[0], &onObserved<0>);
    addCdsHandler(app, kObservedProps[1], &onObserved<1>);
    addCdsHandler(app, kObservedProps[2], &onObserved<2>);
    addCdsHandler(app, kObservedProps[3], &onObserved<3>);
    addCdsHandler(app, kObservedProps[4], &onObserved<4>);
    addCdsHandler(app, kObservedProps[5], &onObserved<5>);
    alog("CDS handlers for %u observed properties registered", static_cast<unsigned>(kObservedCount));
}

void detachCarSignals() {
    g_hold = nullptr;
}

bool callActive() {
    return g_callLine[0] || g_callLine[1];
}

int lastGear() {
    return g_lastGear;
}

}  // namespace app
}  // namespace evo
