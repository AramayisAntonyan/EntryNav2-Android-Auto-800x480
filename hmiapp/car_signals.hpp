// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// The car's CDS properties the app follows: gear, the two phone lines, day/night, driving mode. Each change goes to
// the service as a control message and to the hold machine (driving mode: logged only).
//   driving.gear                     "gear <n>": in reverse the camera shows and Android Auto steps aside
//   communication.callStatus0 / 1    "call 1" / "call 0": the phone keys follow iDrive's call
//   vehicle.dayNightChangeOver       "daynight <n>" (1 day, 2 night), also written to /tmp/entry-aad-daynight
//   driving.mode / driving.mode35up  logged only: on the F22 the mode shows in the cluster, iDrive opens nothing,
//                                    so Android Auto stays on screen (it used to step aside 3.5 s per change)
// Every handler runs on the app thread.
#ifndef ENTRY_AAD_HMIAPP_CAR_SIGNALS_HPP
#define ENTRY_AAD_HMIAPP_CAR_SIGNALS_HPP

#include "bmw_framework.hpp"
#include "hold_machine.hpp"

namespace evo {
namespace app {

// Registers the handlers; their changes also go to hold.
void subscribeCarSignals(bmw::PluginApp* app, HoldMachine& hold);
bool callActive();  // either phone line is in a call
int lastGear();     // kGearUnknown until the first driving.gear value

}  // namespace app
}  // namespace evo

#endif
