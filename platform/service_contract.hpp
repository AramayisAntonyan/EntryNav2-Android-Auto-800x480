// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// What the iDrive app, the supervisor (aa-supervisor) and the service (entry-aad) share: file paths and the control
// port.
//
// The installation procedure (docs/INSTALLATION.md, docs/MAINTENANCE.md) names the same paths: change both together.
#ifndef ENTRY_AAD_PLATFORM_SERVICE_CONTRACT_HPP
#define ENTRY_AAD_PLATFORM_SERVICE_CONTRACT_HPP

#include <stdint.h>

namespace evo {
namespace contract {

// ---- programs, on the system partition ----------------------------------------------------------------------------
constexpr char kSupervisor[] = "/opt/aa/aa-supervisor";
constexpr char kService[] = "/opt/aa/entry-aad";

// ---- settings and logs, on the data partition (kept across restarts) ----------------------------------------------
constexpr char kDataDir[] = "/var/local/data/AA";
constexpr char kAutostart[] = "/var/local/data/AA/autostart";         // present: Android Auto starts with iDrive
constexpr char kEntryDirect[] = "/var/local/data/AA/entry-direct";    // present: the iDrive row opens Android Auto
constexpr char kMaintenance[] = "/var/local/data/AA/maintenance";     // present: the app and the service keep logs
constexpr char kKombiOff[] = "/var/local/data/AA/kombi-off";          // present: nothing is sent to the cluster
constexpr char kKombiShown[] = "/var/local/data/AA/kombi-nav.shown";  // an arrow may still be on the cluster
constexpr char kPhoneGps[] = "/var/local/data/AA/gps-telephone";      // present: the phone keeps its own GPS
constexpr char kVehicle[] = "/var/local/data/AA/vehicle";             // model=... and year=... told to the phone
// Present: send only the next maneuver's graph to the cluster.
constexpr char kKombiSingle[] = "/var/local/data/AA/kombi-single-maneuver";
// Present: the arrows go to an F-series 6WA cluster (MOST HUDTerminal) instead of a G-series KOMBI. Log only for now:
// the calls are written to the service log, nothing is sent. Set by hand, read when the service starts.
constexpr char kKombi6wa[] = "/var/local/data/AA/kombi-6wa";
// Present (with kombi-6wa): the 6WA calls are SENT to the cluster, through car-test --serve (a process of its own,
// as the vMOST library keeps one connection per process). Its content: the vMOST channel (0 when empty).
constexpr char kKombi6waLive[] = "/var/local/data/AA/kombi-6wa-live";
constexpr char kHudSender[] = "/opt/hmi/data/ro/Plugins/CarGauges/car-test";
constexpr char kHudSenderLog[] = "/tmp/hud-sender.log";
// Show directions at any distance (the iDrive app's menu): no approach, the next maneuver shows at once.
constexpr char kKombiAnyDistance[] = "/var/local/data/AA/kombi-anydist";
// The 6WA's junction view near a maneuver off (on by default). Set by hand.
constexpr char kKombiJunctionOff[] = "/var/local/data/AA/kombi-6wa-junction-off";
// When the 6WA shows a maneuver (NBT-AAD's approach rule): "time [warn s [main s]]" (the default, time 50 20),
// "distance" (within 1 mi) or "off" (at once). kKombiAnyDistance wins. Set by hand.
constexpr char kKombi6waApproach[] = "/var/local/data/AA/kombi-6wa-approach";
// The phone's lanes on the 6WA, in the LaneGuidance layers 9/10 (off by default: where they show is a guess).
constexpr char kKombi6waLanes[] = "/var/local/data/AA/kombi-6wa-lanes";
// OPTION while Android Auto is on screen: "nudge" (default, no file), "tap", a key code, or "menu" (the iDrive app's
// Android Auto menu, as before). Read at service start (option_key.hpp). Set by hand.
constexpr char kOptionKeyFile[] = "/var/local/data/AA/option-key";
// Present: Android Auto over a USB cable instead of wireless. Empty, or the one USB port to use ("1-1").
constexpr char kUsb[] = "/var/local/data/AA/usb";
// Present: in USB mode, the phone's accessory interface is taken from another program holding it (usbfs only; on the
// EntryNav2 the media player's CarLife module claims every accessory). Set by hand.
constexpr char kUsbTakeover[] = "/var/local/data/AA/usb-takeover";
constexpr char kAppLog[] = "/var/local/data/AA/app.log";
constexpr char kAppLogOld[] = "/var/local/data/AA/app.old.log";
constexpr char kAppDisable[] = "/var/local/data/AA/app.disable";  // present: the app stays off (set by hand)
constexpr char kAppGuard[] = "/var/local/data/AA/app-guard";  // crash guard: 3 crashes of iDrive and the app stays off
// The crash guard's note of the running load (time, the unit's crash records then); gone after 2 stable minutes.
constexpr char kAppGuardLoad[] = "/var/local/data/AA/app-guard-load";
// Where the unit's ResetManager keeps the core dumps of crashed processes (one folder per dump: 00, 01, 02).
constexpr char kCoreDumpDir[] = "/var/local/data/ResetMgr/CoreDump";
constexpr char kBootGuard[] = "/var/local/data/AA/bootguard";  // automatic starts within 15 minutes

// ---- run time, in /tmp (memory, gone at the next start) -----------------------------------------------------------
constexpr char kServiceDir[] = "/tmp/entry-aa-service";
constexpr char kServiceStop[] = "/tmp/entry-aa-service/STOP";  // created by the app: the supervisor stops
constexpr char kServiceLog[] = "/tmp/entry-aa-service.log";
// The phone's current track, written by the service when it changes: "title\nartist\nalbum\ncover path or empty\n".
constexpr char kTrackFile[] = "/tmp/entry-aa-service/track";
constexpr char kCoverJpg[] = "/tmp/entry-aa-service/cover.jpg";   // the track's cover as the phone sent it: one of the
constexpr char kCoverPng[] = "/tmp/entry-aa-service/cover.png";   // two at most, none when the track has no cover
constexpr char kStateFile[] = "/tmp/entry-aad-state";             // "state pid overlay", written by the service
constexpr char kAppKeyFile[] = "/tmp/entry-aad-appkey";           // NAV/MEDIA/COM/BACK kept Android Auto up
constexpr char kDayNightFile[] = "/tmp/entry-aad-daynight";       // day or night, written by the app
constexpr char kControllerFile[] = "/tmp/entry-aad-controller";   // the car's key coding, written by the app
constexpr char kBrandFile[] = "/tmp/entry-aad-brand";             // "BMW" or "MINI", as iDrive is coded
constexpr char kWirelessLock[] = "/tmp/entry-aad-wireless.lock";  // one wireless start at a time
// One-shot double-MEDIA recall request: process id and monotonic timestamp, checked by the iDrive app.
constexpr char kMediaRecallFile[] = "/tmp/entry-aad-media-recall";

// ---- control messages from the app to the service: a Unix datagram socket, abstract name (control_address.hpp) ----
constexpr char kControlName[] = "entry-aad-control";

}  // namespace contract
}  // namespace evo

#endif
