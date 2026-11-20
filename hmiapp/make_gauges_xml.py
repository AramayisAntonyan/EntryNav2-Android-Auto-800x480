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
SETTINGS = ((24, 960, "Choose gauges"), (28, 961, "Gauge styles"), (16, 950, "Shift point"))
DIAGNOSTICS = ((13, 930, "Engine fault codes"), (26, 970, "Car tests"))

# Each look's components at cell c (gauge_ids.hpp kLookImage0, kLookTitleComp0, kLookValueComp0) and label models
# (kLookTitle0, kLookValue0); every look's pictures use the cell's front (360 + c) and back (424 + c) image models.
LOOK_TITLE = (300, 130, 190, 250, 530, 761, 809)
LOOK_VALUE = (330, 160, 220, 274, 560, 785, 833)
LOOK_TITLE_COMP = (1130, 2060, 2210, 2330, 2480, 2860, 2980)
LOOK_VALUE_COMP = (1160, 2090, 2240, 2360, 2510, 2890, 3010)
CARD, DIAL, WIDE, WIDE_DIAL, TALL, ARC, WIDE_ARC = range(7)
FRONT, BACK = 360, 424
STYLE_ROW_MODEL, STYLE_ROW_COMP, STYLE_ROW_ACTION, STYLE_NOTE = 450, 2701, 480, 474


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
    tw, vw = LOOK_TITLE_COMP, LOOK_VALUE_COMP
    out = (  # card: icon, name, value, bar (no tile: the owner prefers iDrive's own background)
        image(1100 + c, BACK + c, x + 12, y + 17, 22, 22)
        + label(tw[CARD] + c, LOOK_TITLE[CARD] + c, x + 40, y + 8, TILE_W - 48, 40)
        + label(vw[CARD] + c, LOOK_VALUE[CARD] + c, x + 10, y + 56, TILE_W - 20, 55)
        + image(1190 + c, FRONT + c, x + 12, y + 140, 165, 12)
    )
    # dial: face and needle over the whole cell; the hub at (94, 100), the text under it
    out += (
        image(2000 + c, BACK + c, x, y, TILE_W, TILE_H, False)
        + image(2030 + c, FRONT + c, x, y, TILE_W, TILE_H, False)
        + label(tw[DIAL] + c, LOOK_TITLE[DIAL] + c, x + 10, y + 140, TILE_W - 20, 38)
        + label(vw[DIAL] + c, LOOK_VALUE[DIAL] + c, x + 10, y + 104, TILE_W - 20, 38)
    )
    # arc: the fuel gauge's shape, its pivot above the cell; the value over it, the name under it
    out += (
        image(2800 + c, BACK + c, x, y, TILE_W, TILE_H, False)
        + image(2830 + c, FRONT + c, x, y, TILE_W, TILE_H, False)
        + label(tw[ARC] + c, LOOK_TITLE[ARC] + c, x + 10, y + 140, TILE_W - 20, 38)
        + label(vw[ARC] + c, LOOK_VALUE[ARC] + c, x + 10, y + 4, TILE_W - 20, 38)
    )
    if fits(WIDE_ARC, c):  # the name top left, the value top right, the icon between them
        out += (
            image(2920 + c, BACK + c, x, y, WIDE_W, TILE_H, False)
            + image(2950 + c, FRONT + c, x, y, WIDE_W, TILE_H, False)
            + label(tw[WIDE_ARC] + c, LOOK_TITLE[WIDE_ARC] + c, x + 12, y + 4, 166, 40)
            + label(vw[WIDE_ARC] + c, LOOK_VALUE[WIDE_ARC] + c, x + 232, y + 4, WIDE_W - 244, 40)
        )
    if fits(WIDE, c):  # the pictures over both cells: icon, track, notches, numbers; the value's segments over them
        out += (
            image(2150 + c, BACK + c, x, y, WIDE_W, TILE_H, False)
            + image(2180 + c, FRONT + c, x + 12, y + 112, 372, 17, False)  # gauge_art.WIDE_VALUE_BOX
            + label(tw[WIDE] + c, LOOK_TITLE[WIDE] + c, x + 42, y + 8, WIDE_W - 54, 40)
            + label(vw[WIDE] + c, LOOK_VALUE[WIDE] + c, x + 12, y + 52, WIDE_W - 24, 55)
        )
    if fits(WIDE_DIAL, c):  # the hub at (193, 128), the name left and the value right under it
        out += (
            image(2270 + c, BACK + c, x, y, WIDE_W, TILE_H, False)
            + image(2300 + c, FRONT + c, x, y, WIDE_W, TILE_H, False)
            + label(tw[WIDE_DIAL] + c, LOOK_TITLE[WIDE_DIAL] + c, x + 12, y + 136, 214, 40)
            + label(vw[WIDE_DIAL] + c, LOOK_VALUE[WIDE_DIAL] + c, x + 232, y + 136, WIDE_W - 244, 40)
        )
    if fits(TALL, c):
        out += (
            image(2420 + c, BACK + c, x, y, TILE_W, TALL_H, False)
            + image(2450 + c, FRONT + c, x + 40, y + 104, 18, 252, False)  # gauge_art.TALL_VALUE_BOX
            + label(tw[TALL] + c, LOOK_TITLE[TALL] + c, x + 40, y + 8, TILE_W - 48, 40)
            + label(vw[TALL] + c, LOOK_VALUE[TALL] + c, x + 10, y + 52, TILE_W - 20, 55)
        )
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
        models += [f'      <raDataModel id="{m + c}" modelType="Richtext"/>' for m in LOOK_TITLE + LOOK_VALUE]
        models += [
            # the gauge pictures come from the app (CarGauges_Art.pack), not from iDrive's image archive
            f'      <raImageModel id="{FRONT + c}"/>',
            f'      <raImageModel id="{BACK + c}"/>',
            f'      <raDataModel id="{400 + c}" modelType="Richtext"/>',
            f'      <raDataModel id="{STYLE_ROW_MODEL + c}" modelType="Richtext"/>',
        ]
    models.append(f'      <raDataModel id="{STYLE_NOTE}" modelType="Richtext"/>')
    models.append('      <imageIdModel id="90" imageId="57000"/>')
    models += [f'      <raDataModel id="{m}" modelType="Richtext"/>' for m in (91, 92, 93, 104, 105, 106, 107, 108)]
    models += [f'      <raDataModel id="{95 + i}" modelType="Richtext"/>' for i in range(8)]
    models += [f'      <raBoolModel id="{600 + i}"/>' for i in range(len(entries))]
    models += [f'      <raDataModel id="{109 + i}" modelType="Richtext"/>' for i in range(1 + TEST_LINES)]
    models += ['      <raDataModel id="126" modelType="Richtext"/>']
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
    events = (
        '      <focusEvent id="60" target="971"/>\n'  # a car test started: its results page
        '      <focusEvent id="88" target="930"/>'  # the clear started: the codes page shows its result
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
    choose = labelrow(235, 105) + "".join(
        f'          <checkbox id="{1300 + i}" action="{700 + i}" model="{600 + i}" '
        f'text="{e["title"]}{source[e["source"]] if "(" not in e["title"] else ""}"/>\n'
        for i, e in enumerate(entries)
    )
    states += state(960, "Choose gauges", choose, parent=902)
    # Gauge styles: one row per ticked gauge ("Water: Dial"), a press gives it the next look
    styles = labelrow(2700, STYLE_NOTE) + "".join(
        modelbutton(STYLE_ROW_COMP + i, STYLE_ROW_ACTION + i, STYLE_ROW_MODEL + i) for i in range(CELLS)
    )
    states += state(961, "Gauge styles", styles, parent=902)
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
