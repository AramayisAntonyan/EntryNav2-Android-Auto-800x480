// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// The ids of hmiapp/CarGauges.xml (written by hmiapp/make_gauges_xml.py with the same numbers) and of the Gauges app's
// images (hmiapp/make_db.py). The dashboard is pages of cards, as ID6 apps' BoostDeck shows its values: each card a
// tile with the gauge's name, its value and a bar. "Choose gauges" ticks any catalogue entry onto it.
#ifndef ENTRY_AAD_HMIAPP_GAUGE_IDS_HPP
#define ENTRY_AAD_HMIAPP_GAUGE_IDS_HPP

namespace evo {
namespace gauges {

// iDrive keeps states and components in one id table: a state whose id a component also has never opens
// (Car tests was 170, a Shift light label's id). States are in the 900s, which nothing else uses.
// ---- states ---------------------------------------------------------------------------------------------------------
const unsigned kPreviewState = 900;  // the entry's preview card: a car picture
const unsigned kMenuState = 901;     // the app's first page: a scrolling list of its pages (no OPTION menus)
const unsigned kDashState0 = 910;    // 910 .. 910 + kPages - 1: the dashboard pages
const unsigned kPages = 3;
const unsigned kShiftState = 920;
const unsigned kCodesState = 930;
const unsigned kColourState = 940;
const unsigned kShiftPointState = 950;
const unsigned kChooseState = 960;
const unsigned kTestState = 970;        // Car tests: a scrolling list of the tests, one per press
const unsigned kTestResultState = 971;  // Test results: the running test's output, opened by the press
const unsigned kLiveState = 980;        // Live values: the chosen gauges as a scrolling list, one row each

// ---- the cards ----------------------------------------------------------------------------------------------------
const unsigned kCardsPerPage = 8;  // 4 x 2 on the 800x480 screen
const unsigned kCards = kCardsPerPage * kPages;
const unsigned kCardTitle0 = 300;  // Richtext models, card c: + c
const unsigned kCardValue0 = 330;
const unsigned kCardBar0 = 360;    // imageIdModels
const unsigned kCardTile0 = 1100;  // components: the tile image, then the title, value and bar
const unsigned kCardTitleComp0 = 1130;
const unsigned kCardValueComp0 = 1160;
const unsigned kCardBarComp0 = 1190;

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

// ---- the Gauges list and Live values ----------------------------------------------------------------------------
const unsigned kMenuDash2Comp = 1601;  // the list's Dashboard 2 and 3 buttons, hidden while they have no card
const unsigned kMenuDash3Comp = 1602;
const unsigned kLiveRow0 = 400;       // Richtext models: + card
const unsigned kLiveRowComp0 = 1700;  // button rows: + card
const unsigned kActLiveRow = 36;      // a row's press: nothing
const unsigned kActDme = 37;          // the list's "Engine values (DME)" checkbox
const unsigned kDmeModel = 125;       // its raBoolModel
const unsigned kDmeNote = 126;        // the line under it: what the engine values are doing

// ---- other pages ----------------------------------------------------------------------------------------------------
const unsigned kActShiftPoint0 = 30;  // 30..33: kShiftPoints
const unsigned kActReadCodes = 35;
const unsigned kActColour0 = 40;  // 40..48: kColourNames
const unsigned kShiftBar = 90;
const unsigned kShiftRpm = 91;
const unsigned kShiftSpeed = 92;
const unsigned kShiftTarget = 93;
const unsigned kCodeLine0 = 95;  // 95..102
const unsigned kCodeLines = 8;
const unsigned kColourSelected = 103;
const unsigned kShiftPointSelected = 104;

// ---- images -------------------------------------------------------------------------------------------------------
// 58001..58003, not 55001..: the Android Auto app (EntryAad) uses those, and iDrive may share image ids between apps.
const unsigned kImgIcon = 58001;        // 63x54, the entry's car icon
const unsigned kImgCard = 58002;        // 549x484, the preview card
const unsigned kImgTile = 58003;        // 189x180, a card's dark tile
const unsigned kImgBar0 = 56000;        // + colour * 32 + level: 165x10 card bars, levels 0..kBarLevels - 1
const unsigned kBarColourStride = 32;   // kBarLevels <= this
const unsigned kImgShift0 = 57000;      // + level 0..32: 760x40 rpm bar
const unsigned kImgShiftWarn0 = 57100;  // + level: the same bar past the shift point
const unsigned kShiftLevels = 33;       // 250 rpm a step, 0..8000

// ---- colours ------------------------------------------------------------------------------------------------------
const unsigned kColours = 8;      // drawn bar colours (make_db.py GAUGE_COLOURS, same order)
const unsigned kMulticolour = 8;  // each card its own colour
const char* const kColourNames[] = {"BMW Orange", "Blue", "Green", "White",      "Purple",
                                    "Pink",       "Cyan", "Red",   "Multicolour"};
const unsigned kShiftPoints[] = {5000, 5500, 6000, 6500};

}  // namespace gauges
}  // namespace evo

#endif  // ENTRY_AAD_HMIAPP_GAUGE_IDS_HPP
