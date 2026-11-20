// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// The ids of hmiapp/CarGauges.xml (written by hmiapp/make_gauges_xml.py with the same numbers) and of the Gauges app's
// pictures (hmiapp/make_db.py). The entry opens the dashboard: pages of 4 x 2 cells, each gauge in one of the looks of
// hmiapp/gauge_layout.hpp. OPTION holds the other views and two groups of settings: Gauge settings and Diagnostics.
// Red needles and white bars: no colour setting (owner, 2026-11-20).
#ifndef ENTRY_AAD_HMIAPP_GAUGE_IDS_HPP
#define ENTRY_AAD_HMIAPP_GAUGE_IDS_HPP

#include "gauge_layout.hpp"

namespace evo {
namespace gauges {

// iDrive keeps states and components in one id table: a state whose id a component also has never opens
// (Car tests was 170, a Shift light label's id). States are in the 900s, which nothing else uses.
// ---- states ---------------------------------------------------------------------------------------------------------
const unsigned kPreviewState = 900;   // the entry's preview card: a car picture
const unsigned kSettingsState = 902;  // OPTION -> Gauge settings: Choose gauges, Gauge styles, shift point
const unsigned kDiagState = 903;      // OPTION -> Diagnostics: engine values switch, fault codes, car tests
const unsigned kDashState0 = 910;     // 910 .. 910 + kPages - 1: the dashboard pages, 910 the app's first page
const unsigned kPages = 3;
const unsigned kShiftState = 920;
const unsigned kCodesState = 930;
const unsigned kClearState = 931;  // "Clear the engine's codes?": what it does, and the one button that does it
const unsigned kShiftPointState = 950;
const unsigned kChooseState = 960;
const unsigned kLookState = 961;        // Gauge styles: one row per ticked gauge, a press changes its look
const unsigned kTestState = 970;        // Car tests: a scrolling list of the tests, one per press
const unsigned kTestResultState = 971;  // Test results: the running test's output, opened by the press
const unsigned kLiveState = 980;        // Live values: the chosen gauges as a scrolling list, one row each

// ---- the entry
// ------------------------------------------------------------------------------------------------------- A
// combinedAction (11): an raAction that tells the app, then an hmiAction that opens the dashboard. An entry's plain
// raAction never reaches a My Vehicle app (seen on the car 2026-11-20), and the old list page opened by a plain
// hmiAction never reported being shown.
const unsigned kActEntry = 19;  // the combinedAction's raAction

// ---- the dashboard cells
// --------------------------------------------------------------------------------------------- Cell c (0 .. kCells -
// 1): page c / 8, row c % 8 / 4, column c % 4. A gauge is drawn by the components of its look at its first cell; every
// look has its own components there, hidden while another look (or none) uses the cell. Wide looks exist at columns
// 0..2 only, the tall look in row 0 only.
const unsigned kCardsPerPage = kCellsPerPage;
const unsigned kCards = kCardsPerPage * kPages;
// Each look's labels have their own Richtext models (cell c: + c), empty while the look is not at that cell: labels
// are never hidden. The pictures share one front and one back model per cell; a look's pictures are hidden (property
// 3, as Android Auto's album cover) while it is not at the cell.
const unsigned kLookTitle0[kLooks] = {300, 130, 190, 250, 530, 870, 918};
const unsigned kLookValue0[kLooks] = {330, 160, 220, 274, 560, 894, 942};
const unsigned kCardBar0 = 360;   // raImageModels: the front picture (a bar, a dial's needle)
const unsigned kCardBack0 = 424;  // raImageModels: the back picture (a dial's face, a bar's scale, a card's icon)
const unsigned kLookImages = 2;
// The picture components of each look at cell c: base + c, the back picture (per gauge: its icon, dial face or bar
// track) then the front one (a bar's value, a needle). No tiles: the owner prefers iDrive's own background. Its labels:
// title kLookTitleComp0 + c, value kLookValueComp0 + c.
const unsigned kLookImage0[kLooks][kLookImages] = {
    {1100, 1190},  // card: icon, bar
    {2000, 2030},  // dial: face, needle
    {2150, 2180},  // wide bar: track, value
    {2270, 2300},  // wide dial: face, needle
    {2420, 2450},  // tall bar: track, value
    {2800, 2830},  // arc: face, needle
    {2920, 2950},  // wide arc: face, needle
};
const unsigned kLookTitleComp0[kLooks] = {1130, 2060, 2210, 2330, 2480, 2860, 2980};
const unsigned kLookValueComp0[kLooks] = {1160, 2090, 2240, 2360, 2510, 2890, 3010};
inline bool lookFits(unsigned look, unsigned cell) {
    return cell % kCols + lookWidth(look) <= kCols && cell % kCellsPerPage / kCols + lookHeight(look) <= kRows;
}

// ---- Choose gauges: one checkbox per catalogue entry ---------------------------------------------------------------
const unsigned kChoiceModel0 = 600;   // raBoolModel: + entry
const unsigned kChoiceAction0 = 700;  // raAction: + entry
const unsigned kChoiceComp0 = 1300;   // checkbox component: + entry
const unsigned kChooseNote = 105;     // Richtext above the list
const unsigned kTestStatus = 109;     // Test results: the test's name and state
const unsigned kTestLine0 = 110;      // 110..119: the output's last lines
const unsigned kTestLines = 10;
const unsigned kEvTestResults = 60;  // focusEvent -> kTestResultState
const unsigned kActTest0 = 50;       // 50..: one raAction per test (make_gauges_xml.py CAR_TESTS, same order)
const unsigned kTripLine0 = 106;     // 106..108: the preview card's trip lines (consumption, range, speed)

// ---- Gauge styles: one row per ticked gauge -------------------------------------------------------------------------
const unsigned kLookNote = 474;       // Richtext above the rows
const unsigned kLookRow0 = 450;       // Richtext models: + row
const unsigned kLookRowComp0 = 2701;  // button rows: + row
const unsigned kActLookRow0 = 480;    // raActions: + row
const unsigned kLookRows = kCards;

// ---- OPTION (on the dashboards, Live values, Shift light) and Live values ----------------------------------------
// The option buttons of view v (make_gauges_xml.py VIEWS order): kOptionComp0 + v * 10 + item; Dashboard 2 and 3 are
// hidden while they have no gauge.
const unsigned kOptionComp0 = 2600;
const unsigned kOptionDash2 = 1;  // items 1, 2: Dashboard 2, Dashboard 3
const unsigned kOptionDash3 = 2;
const unsigned kViews = 5;            // the dashboards, Live values, Shift light
const unsigned kLiveRow0 = 400;       // Richtext models: + row
const unsigned kLiveRowComp0 = 1700;  // button rows: + row
const unsigned kActLiveRow = 36;      // a row's press: nothing
const unsigned kDmeNote = 126;        // Diagnostics' first line: what the engine values are doing

// ---- other pages ----------------------------------------------------------------------------------------------------
const unsigned kActShiftPoint0 = 30;  // 30..33: kShiftPoints
const unsigned kActReadCodes = 35;
const unsigned kActAskClear = 17;    // hmiAction: Engine fault codes -> kClearState (the XML's, no handler)
const unsigned kActClearCodes = 18;  // raAction: the confirmation page's "Yes, clear"
const unsigned kEvCodes = 88;        // focusEvent -> kCodesState: the clear's result (an id nothing else uses)
const unsigned kClearNote0 = 127;    // 127..128: the confirmation page's text
const unsigned kClearNotes = 2;
const unsigned kShiftBar = 90;
const unsigned kShiftRpm = 91;
const unsigned kShiftSpeed = 92;
const unsigned kShiftTarget = 93;
const unsigned kCodeLine0 = 95;  // 95..102
const unsigned kCodeLines = 8;
const unsigned kShiftPointSelected = 104;

// ---- images -------------------------------------------------------------------------------------------------------
// iDrive's image archive holds only these (iDrive refuses an archive of 1.5 MB or more: the app then never opens);
// 58001..: the Android Auto app (EntryAad) uses 55001.., and iDrive may share image ids between apps.
const unsigned kImgIcon = 58001;        // 63x54, the entry's car icon
const unsigned kImgCard = 58002;        // 549x484, the preview card
const unsigned kImgShift0 = 57000;      // + level 0..32: 760x40 rpm bar
const unsigned kImgShiftWarn0 = 57100;  // + level: the same bar past the shift point
const unsigned kShiftLevels = 33;       // 250 rpm a step, 0..8000
// Every other picture is in CarGauges_Art.pack, sent through raImageModels (as Android Auto's album cover), under the
// same kind of ids. The big looks move in finer steps: + level, levels 0..kFineLevels - 1.
const unsigned kFineLevels = 41;
const unsigned kImgBar0 = 56000;         // + level: 165x12 card bars, levels 0..kBarLevels - 1
const unsigned kImgWideBar0 = 59000;     // 372x17 at the cell's (12, 112): the value's segments over the track
const unsigned kImgTallBar0 = 59500;     // 18x252 at (40, 104), filled from the bottom
const unsigned kImgNeedle0 = 60000;      // 189x180, a dial's needle (transparent but the blade)
const unsigned kImgWideNeedle0 = 60500;  // 386x180
// One picture per gauge (+ catalogue entry): its icon, scale and numbers.
const unsigned kImgFace0 = 61000;       // 189x180, a dial's face
const unsigned kImgWideFace0 = 61100;   // 386x180
const unsigned kImgWideTrack0 = 61200;  // 386x180, a wide bar's track, notches, numbers and icon
const unsigned kImgTallTrack0 = 61300;  // 189x368
const unsigned kImgCardIcon0 = 61400;   // 22x22, a card's icon
// The arcs (the fuel gauge's shape): needles + level, faces + catalogue entry.
const unsigned kImgArcNeedle0 = 62000;      // 189x180
const unsigned kImgWideArcNeedle0 = 62500;  // 386x180
const unsigned kImgArcFace0 = 63000;        // 189x180
const unsigned kImgWideArcFace0 = 63100;    // 386x180
const unsigned kArtFirst = 56000;           // the pack's ids: kArtFirst .. kArtFirst + kArtIds - 1
const unsigned kArtIds = 8000;

const unsigned kShiftPoints[] = {5000, 5500, 6000, 6500};

}  // namespace gauges
}  // namespace evo

#endif  // ENTRY_AAD_HMIAPP_GAUGE_IDS_HPP
