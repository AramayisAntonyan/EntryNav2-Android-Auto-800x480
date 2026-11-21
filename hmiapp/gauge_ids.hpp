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
// The Choose pages, one per source (car data, engine DID, OBD), and their notes above the list.
constexpr unsigned kChooseStates[3] = {960, 962, 963};
const unsigned kChooseNotes[3] = {105, 129, 130};
inline bool isChooseState(unsigned state) {
    return state == kChooseStates[0] || state == kChooseStates[1] || state == kChooseStates[2];
}
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
// Only the card writes its value as text (Richtext, cell c: + c; empty while another look is at the cell: labels are
// never hidden). The other looks are pictures alone, as the M5's gauges. A look's pictures are hidden (property 3, as
// Android Auto's album cover) while it is not at the cell.
const unsigned kCardValue0 = 330;
// iDrive draws only about the first 64 components of a page (2026-11-21: with one pair of pictures per look and
// cell, the cells past the 57th component stayed empty). So the looks of one size share their picture components:
// the small looks (card, dial, arc) one pair over the cell, the wide looks one pair over two cells across, the tall
// bar one pair over two cells high; every picture fills its component, and only the pictures change with the look.
// 44 components a page. Each component has its own raImageModel (cell c: + c): shared between components, a picture
// went to the wrong one (2026-11-20).
enum LookGroup { kGroupSmall, kGroupWide, kGroupTall, kGroups };
inline unsigned lookGroup(unsigned look) {
    return lookHeight(look) > 1 ? kGroupTall : lookWidth(look) > 1 ? kGroupWide : kGroupSmall;
}
const unsigned kLookImages = 2;
// The components of each group at cell c: base + c, the back picture (a face, a bar's track, a card's head) then the
// front one (a needle, a bar's value). No tiles: the owner prefers iDrive's own background.
const unsigned kGroupImage0[kGroups][kLookImages] = {{1100, 1190}, {2150, 2180}, {2420, 2450}};
const unsigned kGroupLook[kGroups] = {kLookCard, kLookWide, kLookTall};  // a look of the group (where it fits)
// Their models, by look (the looks of a group share them): front (a needle, a bar) and back (a face, a track).
const unsigned kLookFront0[kLooks] = {1100, 1100, 1220, 1220, 1340, 1100, 1220};
const unsigned kLookBack0[kLooks] = {1130, 1130, 1250, 1250, 1370, 1130, 1250};
inline bool lookFits(unsigned look, unsigned cell) {
    return cell % kCols + lookWidth(look) <= kCols && cell % kCellsPerPage / kCols + lookHeight(look) <= kRows;
}

// ---- Choose gauges: one checkbox per catalogue entry ---------------------------------------------------------------
const unsigned kChoiceModel0 = 600;   // raBoolModel: + entry
const unsigned kChoiceAction0 = 700;  // raAction: + entry
const unsigned kChoiceComp0 = 1300;   // checkbox component: + entry
const unsigned kTestStatus = 109;     // Test results: the test's name and state
const unsigned kTestLine0 = 110;      // 110..119: the output's last lines
const unsigned kTestLines = 10;
const unsigned kEvTestResults = 358;  // focusEvent -> kTestResultState
const unsigned kActTest0 = 50;        // 50..: one raAction per test (make_gauges_xml.py CAR_TESTS, same order)
const unsigned kTripLine0 = 106;      // 106..108: the preview card's trip lines (consumption, range, speed)

// ---- Gauge styles: one row per ticked gauge -------------------------------------------------------------------------
const unsigned kLookNote = 474;       // Richtext above the rows
const unsigned kLookRow0 = 450;       // Richtext models: + row
const unsigned kLookRowComp0 = 2701;  // button rows: + row
const unsigned kActLookRow0 = 480;    // raActions: + row
const unsigned kLookRows = kCards;

// ---- Gauge ranges: one row per ticked gauge; a press gives it its next scale (gauges-range) -------------------------
const unsigned kRangeState = 966;
const unsigned kRangeNote = 305;      // Richtext above the rows
const unsigned kRangeRow0 = 155;      // Richtext models: + row
const unsigned kRangeRowComp0 = 191;  // button rows: + row
const unsigned kActRangeRow0 = 245;   // raActions: + row

// ---- Gauge places: one row per ticked gauge; a press opens its places (gauges-place) --------------------------------
const unsigned kPlacesState = 964;       // the gauges with their places
const unsigned kPlaceState = 965;        // the places one gauge can take
const unsigned kPlacesNote = 355;        // Richtext above the rows
const unsigned kPlaceNote = 356;         // Richtext above the places
const unsigned kPlaceRow0 = 131;         // Richtext models: + row
const unsigned kPlaceRowComp0 = 100;     // button rows: + row
const unsigned kActPlaceRow0 = 191;      // raActions: + row
const unsigned kPlaceChoice0 = 221;      // Richtext models: + cell
const unsigned kPlaceChoiceComp0 = 130;  // buttons: + cell
const unsigned kActPlaceChoice0 = 281;   // raActions: + cell
const unsigned kActResetPlaces = 357;    // every gauge back to the automatic places
const unsigned kEvPlace = 359;           // focusEvent -> kPlaceState

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
const unsigned kFineLevels = 121;        // finer than 41: the needles move smoother
const unsigned kImgBar0 = 56000;         // + level: a card's bar in its cell (189x180), 0..kBarLevels - 1
const unsigned kImgWideBar0 = 59000;     // 386x180: the value's segments over the track
const unsigned kImgTallBar0 = 59500;     // 189x368, filled from the bottom
const unsigned kImgNeedle0 = 60000;      // 189x180, a dial's needle (transparent but the blade)
const unsigned kImgWideNeedle0 = 60500;  // 386x180
// One picture per gauge (+ catalogue entry): its icon, scale and numbers.
const unsigned kImgFace0 = 61000;       // 189x180, a dial's face
const unsigned kImgWideFace0 = 61100;   // 386x180
const unsigned kImgWideTrack0 = 61200;  // 386x180, a wide bar's track, notches, numbers and icon
const unsigned kImgTallTrack0 = 61300;  // 189x368
const unsigned kImgCardIcon0 = 61400;   // a card's head (icon and name) in its cell (189x180)
// The arcs (the fuel gauge's shape): needles + level, faces + catalogue entry.
const unsigned kImgArcNeedle0 = 62000;      // 189x180
const unsigned kImgWideArcNeedle0 = 62500;  // 386x180
const unsigned kImgArcFace0 = 63000;        // 189x180
const unsigned kImgWideArcFace0 = 63100;    // 386x180
// The faces and tracks of a gauge's other scales (Gauge ranges): + (scale - 1) * 1000 + kind * 100 + gauge, kind as
// kRangeKind below.
const unsigned kImgRange0 = 64000;
enum RangeKind { kKindFace, kKindWideFace, kKindWideTrack, kKindTallTrack, kKindArcFace, kKindWideArcFace };
const unsigned kArtFirst = 56000;  // the pack's ids: kArtFirst .. kArtFirst + kArtIds - 1
const unsigned kArtIds = 11000;

const unsigned kShiftPoints[] = {5000, 5500, 6000, 6500};

}  // namespace gauges
}  // namespace evo

#endif  // ENTRY_AAD_HMIAPP_GAUGE_IDS_HPP
