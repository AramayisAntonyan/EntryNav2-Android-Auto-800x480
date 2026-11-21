#!/usr/bin/env python3
# Copyright (c) 2026 Jiraiya43
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
"""Write hmiapp/CarGauges.xml, the Gauges app's description, for the 800x480 iDrive.

The ids are those of hmiapp/gauge_ids.hpp; the "Choose gauges" list is built from hmiapp/gauge_catalogue.py. The entry
opens the dashboard: pages of 4 x 2 cells, each gauge drawn in its look (hmiapp/gauge_layout.hpp): a card as ID6 apps'
BoostDeck shows its values (MIT), a dial (a half circle, as BMW's clusters), a wide bar, a wide dial or a tall bar.
OPTION opens the other views and two groups of settings. No XML comments: the car's parser has only ever seen BMW's
files and ours without them.

Usage: make_gauges_xml.py <output file>
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gauge_catalogue  # noqa: E402

PAGES, PER_PAGE, COLS = 3, 8, 4
CELLS = PAGES * PER_PAGE
TILE_X = (8, 205, 402, 599)
TILE_Y = (8, 196)
TILE_W, TILE_H = 189, 180
WIDE_W = TILE_X[1] + TILE_W - TILE_X[0]  # 386: two cells across
TALL_H = TILE_Y[1] + TILE_H - TILE_Y[0]  # 368: two cells high
SHIFT_POINTS = (5000, 5500, 6000, 6500)
# The Car tests page (hmiapp/gauge_app.cpp kCarTests, same order): one raAction each, from 50.
CAR_TESTS = (
    "Cluster: settings read (read only)",
    "Cluster: function list (auto)",
    "Cluster: function list (0101)",
    "Cluster: size of area 5 (auto)",
    "Cluster: size of area 5 (0101)",
    "Cluster: draw test arrow (auto)",
    "Cluster: draw test arrow (0101)",
    "Cluster: draw, high channel (0101)",
    "Cluster: size of area 5, tx 4 (0101)",
    "Cluster: draw, tx 4 (0101)",
    "Listen to cluster blocks 20 s",
    "Gateway: find it",
    "Gateway: probe the engine",
    "Gateway: supported OBD values",
    "Engine via MOST tunnel (read only)",
    "Listen to the diagnosis block 20 s",
    "Car data: show all values",
)
TEST_LINES = 10  # the results page's output lines, models 110..119 (109: the test and its state)

# The views OPTION switches between and the settings groups, as (hmiAction, target state, text). Every view's OPTION
# lists them all but itself; its buttons are 2600 + view * 10 + item (gauge_ids.hpp kOptionComp0; the app hides
# Dashboard 2 and 3, items 1 and 2, while they have no gauge).
OPTIONS = (
    (10, 910, "Dashboard"),
    (21, 911, "Dashboard 2"),
    (22, 912, "Dashboard 3"),
    (25, 980, "Live values"),
    (12, 920, "Shift light"),
    (23, 902, "Gauge settings"),
    (27, 903, "Diagnostics"),
)
VIEWS = (910, 911, 912, 980, 920)
SETTINGS = (
    (24, 960, "Choose: car data"),
    (29, 962, "Choose: engine (DME)"),
    (34, 963, "Choose: OBD values"),
    (28, 961, "Gauge styles"),
    (354, 964, "Gauge places"),
    (306, 966, "Gauge ranges"),
    (16, 950, "Shift point"),
)
# The Choose pages, one per source (gauge_ids.hpp kChooseStates): state, title, source, note label, note model
CHOOSE = (
    (960, "Choose: car data", "cds", 235, 105),
    (962, "Choose: engine (DME)", "did", 1612, 129),
    (963, "Choose: OBD values", "obd", 1613, 130),
)
DIAGNOSTICS = ((13, 930, "Engine fault codes"), (26, 970, "Car tests"))

# Each look's picture components at cell c (gauge_ids.hpp kGroupImage0). Only the card writes its value as text
# (component 1160 + c, model kCardValue0 330 + c): the other looks are pictures alone, as the M5's gauges (the owner,
# 2026-11-20: the number should not always show).
CARD_VALUE, CARD_VALUE_COMP = 330, 1160
CARD, DIAL, WIDE, WIDE_DIAL, TALL, ARC, WIDE_ARC = range(7)
# Each look's own picture models (gauge_ids.hpp kLookFront0, kLookBack0): shared models showed in the wrong component.
LOOK_FRONT = (1100, 1160, 1220, 1280, 1340, 1400, 1460)
LOOK_BACK = (1130, 1190, 1250, 1310, 1370, 1430, 1490)
STYLE_ROW_MODEL, STYLE_ROW_COMP, STYLE_ROW_ACTION, STYLE_NOTE = 450, 2701, 480, 474
# Gauge places (gauge_ids.hpp kPlace*): rows of the ticked gauges (components 100 + i, models 131 + i, raActions
# 191 + i) and the places one gauge can take (components 130 + c, models 221 + c, raActions 281 + c); the notes above
# them (labels 188, 189; models 355, 356), the reset (button 190, raAction 357), focusEvent 359 opens the places.
PLACE_ROW_COMP, PLACE_ROW_MODEL, PLACE_ROW_ACTION = 100, 131, 191
PLACE_COMP, PLACE_MODEL, PLACE_ACTION = 130, 221, 281
PLACES_NOTE, PLACE_NOTE, RESET_PLACES, PLACE_EVENT = 355, 356, 357, 359
# Gauge ranges (gauge_ids.hpp kRange*): rows of the ticked gauges (components 191 + i, models 155 + i, raActions
# 245 + i), the note above them (label 239, model 305); a press gives the gauge its next scale.
RANGE_ROW_COMP, RANGE_ROW_MODEL, RANGE_ROW_ACTION, RANGE_NOTE = 191, 155, 245, 305
# The range of one gauge set by hand (gauge_ids.hpp kRangeEdit*): state 967, opened by focusEvent 326; its label 263
# (model 307), nine buttons 264 + i (texts 308 + i, raActions 317 + i).
EDIT_STATE, EDIT_EVENT, EDIT_NOTE, EDIT_TEXT, EDIT_ACTION, EDIT_NOTE_COMP, EDIT_COMP, EDIT_STEPS = (
    967,
    326,
    307,
    308,
    317,
    263,
    264,
    9,
)


def props(x, y, w, h, extra=""):
    return (
        "            <properties>\n"
        f'              <property id="20" value="{x}"/>\n'
        f'              <property id="21" value="{y}"/>\n'
        f'              <property id="9" value="{w}"/>\n'
        f'              <property id="10" value="{h}"/>\n{extra}'
        "            </properties>\n"
    )


SHOWN = '              <property id="3" value="true"/>\n'
HIDDEN = '              <property id="3" value="false"/>\n'


def image(comp, model, x, y, w, h, shown=True):
    shows = props(x, y, w, h, SHOWN if shown else HIDDEN)
    return f'          <image id="{comp}" model="{model}">\n{shows}          </image>\n'


def label(comp, model, x, y, w, h=55):
    return f'          <label id="{comp}" model="{model}">\n{props(x, y, w, h)}          </label>\n'


def button(comp, action, text):
    return f'          <button id="{comp}" action="{action}" text="{text}"/>\n'


def state(sid, text, components, options="", parent=None):
    p = f' parentId="{parent}"' if parent else ""
    opt = f"        <optionComponents>\n{options}        </optionComponents>\n" if options else ""
    return (
        f'      <hmiState id="{sid}"{p} text="{text}">\n{opt}'
        f"        <components>\n{components}        </components>\n      </hmiState>\n"
    )


def fits(look, c):
    slot = c % PER_PAGE
    wide, tall = look in (WIDE, WIDE_DIAL, WIDE_ARC), look == TALL
    return slot % COLS + (2 if wide else 1) <= COLS and slot // COLS + (2 if tall else 1) <= 2


def cell(c):
    """Every look's components at cell c: pictures first (drawn below), then the labels."""
    slot = c % PER_PAGE
    x, y = TILE_X[slot % COLS], TILE_Y[slot // COLS]
    # iDrive draws only about the first 64 components of a page (2026-11-21: with a pair per look, the cells past the
    # 57th component stayed empty): the looks of one size share a pair of pictures that fill their cells (gauge_ids.hpp
    # kGroupImage0), 44 components a page. Small looks (card, dial, arc): back and front over the cell, the card's
    # value as text over them.
    out = image(1100 + c, LOOK_BACK[CARD] + c, x, y, TILE_W, TILE_H, False)
    out += image(1190 + c, LOOK_FRONT[CARD] + c, x, y, TILE_W, TILE_H, False)
    out += label(CARD_VALUE_COMP + c, CARD_VALUE + c, x + 10, y + 44, TILE_W - 20, 55)
    if fits(WIDE, c):  # wide bar, wide dial, wide arc: over both cells across
        out += image(2150 + c, LOOK_BACK[WIDE] + c, x, y, WIDE_W, TILE_H, False)
        out += image(2180 + c, LOOK_FRONT[WIDE] + c, x, y, WIDE_W, TILE_H, False)
    if fits(TALL, c):  # the tall bar: over both cells high
        out += image(2420 + c, LOOK_BACK[TALL] + c, x, y, TILE_W, TALL_H, False)
        out += image(2450 + c, LOOK_FRONT[TALL] + c, x, y, TILE_W, TALL_H, False)
    return out


def labelrow(comp, model):
    """A text row of a scrolling list: no position, the iDrive stacks it as a list row."""
    return f'          <label id="{comp}" model="{model}"/>\n'


def modelbutton(comp, action, model):
    """A list row whose text the app sets (a Richtext model)."""
    return f'          <button id="{comp}" action="{action}" model="{model}"/>\n'


def options(view):
    """A view's OPTION menu: the other views, then Gauge settings and Diagnostics."""
    v = VIEWS.index(view)
    return "".join(button(2600 + v * 10 + i, a, text) for i, (a, t, text) in enumerate(OPTIONS) if t != view)


def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: make_gauges_xml.py <output file>")
    entries = gauge_catalogue.entries()
    models = [
        '      <imageIdModel id="4" imageId="58001"/>',
        '      <raPreviewModel id="5" modelType="StatePreview" previewState="900"/>',
        '      <imageIdModel id="6" imageId="58002"/>',
    ]
    for c in range(CELLS):
        models.append(f'      <raDataModel id="{CARD_VALUE + c}" modelType="Richtext"/>')
        models += [
            # the gauge pictures come from the app (CarGauges_Art.pack), not from iDrive's image archive
            f'      <raDataModel id="{400 + c}" modelType="Richtext"/>',
            f'      <raDataModel id="{STYLE_ROW_MODEL + c}" modelType="Richtext"/>',
        ]
    for first in (
        LOOK_FRONT[CARD],
        LOOK_BACK[CARD],
        LOOK_FRONT[WIDE],
        LOOK_BACK[WIDE],
        LOOK_FRONT[TALL],
        LOOK_BACK[TALL],
    ):
        models += [f'      <raImageModel id="{first + c}"/>' for c in range(CELLS)]
    models.append(f'      <raDataModel id="{STYLE_NOTE}" modelType="Richtext"/>')
    models += [
        f'      <raDataModel id="{m + c}" modelType="Richtext"/>'
        for m in (PLACE_ROW_MODEL, PLACE_MODEL)
        for c in range(CELLS)
    ]
    models += [f'      <raDataModel id="{m}" modelType="Richtext"/>' for m in (PLACES_NOTE, PLACE_NOTE)]
    models += [f'      <raDataModel id="{RANGE_ROW_MODEL + c}" modelType="Richtext"/>' for c in range(CELLS)]
    models.append(f'      <raDataModel id="{RANGE_NOTE}" modelType="Richtext"/>')
    models += [f'      <raDataModel id="{m}" modelType="Richtext"/>' for m in [EDIT_NOTE]]
    models += [f'      <raDataModel id="{EDIT_TEXT + i}" modelType="Richtext"/>' for i in range(EDIT_STEPS)]
    models.append('      <imageIdModel id="90" imageId="57000"/>')
    models += [f'      <raDataModel id="{m}" modelType="Richtext"/>' for m in (91, 92, 93, 104, 105, 106, 107, 108)]
    models += [f'      <raDataModel id="{95 + i}" modelType="Richtext"/>' for i in range(8)]
    models += [f'      <raBoolModel id="{600 + i}"/>' for i in range(len(entries))]
    models += [f'      <raDataModel id="{109 + i}" modelType="Richtext"/>' for i in range(1 + TEST_LINES)]
    models += ['      <raDataModel id="126" modelType="Richtext"/>']
    models += [f'      <raDataModel id="{note}" modelType="Richtext"/>' for *_, note in CHOOSE[1:]]
    models += [f'      <raDataModel id="{m}" modelType="Richtext"/>' for m in (127, 128)]  # the clear's page

    # The entry is a combinedAction (11): raAction 19 tells the app, hmiAction 20 opens the dashboard. An entry's
    # plain raAction never reached the app on the car (My Vehicle, 2026-11-20).
    nav = [(a, t) for a, t, _ in OPTIONS + SETTINGS + DIAGNOSTICS] + [(17, 931)]  # 17: fault codes -> clear page
    actions = [
        '      <combinedAction id="11">\n        <actions>\n          <raAction id="19"/>\n'
        '          <hmiAction id="20" target="910"/>\n        </actions>\n      </combinedAction>'
    ]
    actions += [f'      <hmiAction id="{a}" target="{t}"/>' for a, t in nav]
    actions += [f'      <raAction id="{a}"/>' for a in (18, 30, 31, 32, 33, 35, 36)]
    actions += [f'      <raAction id="{700 + i}"/>' for i in range(len(entries))]
    actions += [f'      <raAction id="{50 + i}"/>' for i in range(len(CAR_TESTS))]
    actions += [f'      <raAction id="{STYLE_ROW_ACTION + i}"/>' for i in range(CELLS)]
    actions += [f'      <raAction id="{a + c}"/>' for a in (PLACE_ROW_ACTION, PLACE_ACTION) for c in range(CELLS)]
    actions.append(f'      <raAction id="{RESET_PLACES}"/>')
    actions += [f'      <raAction id="{RANGE_ROW_ACTION + c}"/>' for c in range(CELLS)]
    actions += [f'      <raAction id="{EDIT_ACTION + i}"/>' for i in range(EDIT_STEPS)]
    events = (
        '      <focusEvent id="358" target="971"/>\n'  # a car test started: its results page
        '      <focusEvent id="88" target="930"/>\n'  # the clear started: the codes page shows its result
        f'      <focusEvent id="{PLACE_EVENT}" target="965"/>\n'  # a gauge pressed on Gauge places: its places
        f'      <focusEvent id="{EDIT_EVENT}" target="{EDIT_STATE}"/>'  # a gauge pressed on Gauge ranges: its range
    )

    preview = image(89, 6, 0, 0, 549, 484) + "".join(
        label(236 + i, 106 + i, 20, 345 + 45 * i, 509, 45) for i in range(3)
    )  # the trip lines, as BMW's own My Vehicle card shows on larger screens
    states = state(900, "Gauges", preview)
    for page in range(PAGES):
        cells = "".join(cell(page * PER_PAGE + s) for s in range(PER_PAGE))
        sid = 910 + page
        states += state(
            sid, "Gauges" if page == 0 else f"Gauges {page + 1}", cells, options(sid), None if page == 0 else 910
        )
    live = "".join(modelbutton(1700 + c, 36, 400 + c) for c in range(CELLS))
    states += state(980, "Live values", live, options(980), parent=910)
    shift = (
        image(220, 90, 20, 40, 760, 40)
        + label(170, 91, 20, 100, 760)
        + label(171, 92, 20, 170, 760)
        + label(172, 93, 20, 240, 760)
    )
    states += state(920, "Shift light", shift, options(920), parent=910)
    # Gauge settings: what the dashboard shows and how
    settings = "".join(button(2750 + i, a, text) for i, (a, _, text) in enumerate(SETTINGS))
    states += state(902, "Gauge settings", settings, parent=910)
    # Diagnostics: what the engine values are doing (read whenever an engine gauge shows; no switch, the owner
    # 2026-11-20), the fault codes, the car tests
    diag = labelrow(1611, 126)
    diag += "".join(button(2760 + i, a, text) for i, (a, _, text) in enumerate(DIAGNOSTICS))
    states += state(903, "Diagnostics", diag, parent=910)
    codes = (
        button(1441, 35, "Read again")
        + button(1442, 17, "Clear the codes...")
        + "".join(labelrow(180 + i, 95 + i) for i in range(8))
    )
    states += state(930, "Engine fault codes", codes, parent=903)
    # The clear asks first: what it does, then the one button that sends it (iDrive's Back leaves without).
    clear = labelrow(1443, 127) + labelrow(1444, 128) + button(1445, 18, "Yes, clear the engine's codes")
    states += state(931, "Clear engine codes?", clear, parent=930)
    points = labelrow(234, 104) + "".join(
        button(230 + i, 30 + i, f"Shift at {rpm} rpm") for i, rpm in enumerate(SHIFT_POINTS)
    )
    states += state(950, "Shift point", points, parent=902)
    source = {"cds": "", "obd": " (OBD)", "did": " (DME)"}
    for sid, title, src, note_comp, note in CHOOSE:  # one page per source: the full list was too long to find in
        choose = labelrow(note_comp, note) + "".join(
            f'          <checkbox id="{1300 + i}" action="{700 + i}" model="{600 + i}" '
            f'text="{e["title"]}{source[e["source"]] if "(" not in e["title"] else ""}"/>\n'
            for i, e in enumerate(entries)
            if e["source"] == src
        )
        states += state(sid, title, choose, parent=902)
    # Gauge styles: one row per ticked gauge ("Water: Dial"), a press gives it the next look
    styles = labelrow(2700, STYLE_NOTE) + "".join(
        modelbutton(STYLE_ROW_COMP + i, STYLE_ROW_ACTION + i, STYLE_ROW_MODEL + i) for i in range(CELLS)
    )
    states += state(961, "Gauge styles", styles, parent=902)
    # Gauge places: a press on a gauge opens its places (a wide gauge takes two cells across, a tall bar two high:
    # only the places it fits are shown); a taken place swaps the two
    places = (
        labelrow(188, PLACES_NOTE)
        + button(190, RESET_PLACES, "Automatic places for all")
        + "".join(modelbutton(PLACE_ROW_COMP + i, PLACE_ROW_ACTION + i, PLACE_ROW_MODEL + i) for i in range(CELLS))
    )
    states += state(964, "Gauge places", places, parent=902)
    choices = labelrow(189, PLACE_NOTE) + "".join(
        modelbutton(PLACE_COMP + c, PLACE_ACTION + c, PLACE_MODEL + c) for c in range(CELLS)
    )
    states += state(965, "Choose a place", choices, parent=964)
    scales = labelrow(239, RANGE_NOTE) + "".join(
        modelbutton(RANGE_ROW_COMP + i, RANGE_ROW_ACTION + i, RANGE_ROW_MODEL + i) for i in range(CELLS)
    )
    states += state(966, "Gauge ranges", scales, parent=902)
    edit = labelrow(EDIT_NOTE_COMP, EDIT_NOTE) + "".join(
        modelbutton(EDIT_COMP + i, EDIT_ACTION + i, EDIT_TEXT + i) for i in range(EDIT_STEPS)
    )
    states += state(EDIT_STATE, "Set the range", edit, parent=966)
    tests = "".join(button(1510 + i, 50 + i, t) for i, t in enumerate(CAR_TESTS))
    states += state(970, "Car tests", tests, parent=903)
    # The results: the test and its state, then the output's last lines, placed so the whole page is one photo.
    results = label(1499, 109, 20, 4, 760, 40) + "".join(
        label(1500 + i, 110 + i, 20, 48 + 42 * i, 760, 40) for i in range(TEST_LINES)
    )
    states += state(971, "Test results", results, parent=970)

    xml = (
        '<?xml version="1.0" encoding="UTF-8"?>\n<pluginApps>\n'
        '  <pluginApp applicationType="VehicleInformation" applicationWeight="1" text="Gauges">\n'
        '    <entryButton id="1" action="11" imageModel="4" previewModel="5">\n'
        '      <properties>\n        <property id="23" value="2"/>\n      </properties>\n    </entryButton>\n'
        "    <actions>\n" + "\n".join(actions) + "\n    </actions>\n"
        "    <models>\n" + "\n".join(models) + "\n    </models>\n"
        "    <events>\n" + events + "\n    </events>\n"
        "    <hmiStates>\n" + states + "    </hmiStates>\n  </pluginApp>\n</pluginApps>\n"
    )
    Path(sys.argv[1]).write_bytes(xml.encode("utf-8"))


if __name__ == "__main__":
    main()
