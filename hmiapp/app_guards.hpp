// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// What keeps the app from harming iDrive, and the automatic start they protect. Files in /var/local/data/AA:
//   app.disable   present: the app stays off (iDrive starts without it)
//   app-guard     crash guard: one date per crash of iDrive shortly after loading the app; 3 within 15 minutes and the
//                 app refuses to start until they are 15 minutes old. A run shorter than 2 minutes is a crash only
//                 when iDrive restarted while the unit kept running, or when the unit stored a new core dump
//                 (ResetManager/CoreDump): a restart of the whole unit (coding, holding the volume knob, ignition) is
//                 not counted. app-guard-load notes the running load. Both removed once the HMI has been up 2 minutes
//                 with the app.
//   autostart     present: Android Auto starts by itself 30 s after iDrive
//   usb           present: Android Auto over a USB cable instead of wireless (docs/USB.md)
//   bootguard     the automatic starts of the last 15 minutes; at 3, the automatic start is skipped (Start in the menu
//                 still works). Removed by a manual start and once the HMI has been up 2 minutes.
#ifndef ENTRY_AAD_HMIAPP_APP_GUARDS_HPP
#define ENTRY_AAD_HMIAPP_APP_GUARDS_HPP

namespace evo {
namespace app {

// When iDrive creates the app; each logs why it keeps the app off.
bool appDisabled();       // app.disable is present
bool crashGuardAllows();  // judges how the last load ended; false after 3 crashes within 15 minutes

// The crash guard's judgement of the previous load, which did not reach 2 minutes (pure, tested on the host):
// systemUptimeS is the unit's uptime now; the core lists are the unit's core dumps then and now.
enum class LastRunEnd { kUnitRestart, kHmiRestart, kNewCoreDump };
LastRunEnd judgeLastRun(double systemUptimeS, const char* coresThen, const char* coresNow);

// Once per status poll, initTime being when onInit ran on the monotonic clock (mono_clock.hpp), 0 before: the car's
// date can move back hours after iDrive starts, which held these delays back as long (#86).
void resetGuardsWhenStable(long initTime);  // 2 minutes up: not a crash loop
void serviceAutostart(long initTime);       // the automatic start, once, 30 s after onInit

void forgetBootGuard();  // a start by hand
bool autostartOn();
void toggleAutostart();  // the menu's checkbox
// The menu's other checkbox. Ticking creates the empty file (every USB port); unticking removes it, with the
// port name someone may have written in it by hand.
bool usbCableOn();
void toggleUsbCable();
// "Directions at any distance" (NBT-AAD #205): the cluster shows the next maneuver as soon as the phone sends it.
// Read when Android Auto starts (platform/kombi_nav.cpp).
bool anyDistanceOn();
void toggleAnyDistance();
// The "keep logs" box: the maintenance profile's file (log_profile.hpp). The app's log follows at once, the
// service's from its next start. Unticking keeps the logs already written, for a copy to a USB stick.
bool keepLogsOn();
void toggleKeepLogs();

}  // namespace app
}  // namespace evo

#endif
