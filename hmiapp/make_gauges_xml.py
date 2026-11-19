#!/usr/bin/env python3
# Copyright (c) 2026 Jiraiya43
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
"""Write hmiapp/CarGauges.xml, the Gauges app's description, for the 800x480 iDrive.

The ids are those of hmiapp/gauge_ids.hpp; the "Choose gauges" list is built from hmiapp/gauge_catalogue.py. The
dashboard shows its values as cards, as ID6 apps' BoostDeck does (MIT): a dark tile with the name, the value and a
bar. No XML comments: the car's parser has only ever seen BMW's files and ours without them.

Usage: make_gauges_xml.py <output file>
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gauge_catalogue  # noqa: E402

PAGES, PER_PAGE = 3, 8
TILE_X = (8, 205, 402, 599)
TILE_Y = (8, 196)
TILE_W, TILE_H = 189, 180
COLOURS = ("BMW Orange", "Blue", "Green", "White", "Purple", "Pink", "Cyan", "Red", "Multicolour")
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
# The Gauges list (state 901), the app's first page, in order: (hmiAction, target state, text). The app hides
# Dashboard 2 and 3 while they have no card (gauge_ids.hpp kMenuDash2Comp).
MENU = (
    (10, 910, "Dashboard"),
    (21, 911, "Dashboard 2"),
    (22, 912, "Dashboard 3"),
    (25, 980, "Live values"),
    (24, 960, "Choose gauges"),
    (14, 940, "Gauge colour"),
    (12, 920, "Shift light"),
    (16, 950, "Shift point"),
    (13, 930, "Engine fault codes"),
    (26, 970, "Car tests"),
)
MENU_TARGETS = tuple((a, t) for a, t, _ in MENU)


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


def image(comp, model, x, y, w, h):
    return f'          <image id="{comp}" model="{model}">\n{props(x, y, w, h, SHOWN)}          </image>\n'


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


def card(c):
    """Card c of the dashboard: tile, title, value, bar."""
    slot = c % PER_PAGE
    x, y = TILE_X[slot % 4], TILE_Y[slot // 4]
    return (
        image(1100 + c, 7, x, y, TILE_W, TILE_H)
        + label(1130 + c, 300 + c, x + 10, y + 8, TILE_W - 20, 40)
        + label(1160 + c, 330 + c, x + 10, y + 56, TILE_W - 20, 55)
        + image(1190 + c, 360 + c, x + 12, y + 140, 165, 10)
    )


def labelrow(comp, model):
    """A text row of a scrolling list: no position, the iDrive stacks it as a list row."""
    return f'          <label id="{comp}" model="{model}"/>\n'


def modelbutton(comp, action, model):
    """A list row whose text the app sets (a Richtext model)."""
    return f'          <button id="{comp}" action="{action}" model="{model}"/>\n'


def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: make_gauges_xml.py <output file>")
    entries = gauge_catalogue.entries()
    models = [
        '      <imageIdModel id="4" imageId="58001"/>',
        '      <raPreviewModel id="5" modelType="StatePreview" previewState="900"/>',
        '      <imageIdModel id="6" imageId="58002"/>',
        '      <imageIdModel id="7" imageId="58003"/>',
    ]
    for c in range(PAGES * PER_PAGE):
        models += [
            f'      <raDataModel id="{300 + c}" modelType="Richtext"/>',
            f'      <raDataModel id="{330 + c}" modelType="Richtext"/>',
            f'      <imageIdModel id="{360 + c}" imageId="56000"/>',
            f'      <raDataModel id="{400 + c}" modelType="Richtext"/>',
        ]
    models.append('      <imageIdModel id="90" imageId="57000"/>')
    models += [
        f'      <raDataModel id="{m}" modelType="Richtext"/>' for m in (91, 92, 93, 103, 104, 105, 106, 107, 108)
    ]
    models += [f'      <raDataModel id="{95 + i}" modelType="Richtext"/>' for i in range(8)]
    models += [f'      <raBoolModel id="{600 + i}"/>' for i in range(len(entries))]
    models += [f'      <raDataModel id="{109 + i}" modelType="Richtext"/>' for i in range(1 + TEST_LINES)]
    models += ['      <raBoolModel id="125"/>', '      <raDataModel id="126" modelType="Richtext"/>']
    models += [f'      <raDataModel id="{m}" modelType="Richtext"/>' for m in (127, 128)]  # the clear's page

    # Every page is reached from the Gauges list (state 98): no OPTION menus.
    nav = [(11, 901)] + [(a, t) for a, t in MENU_TARGETS] + [(17, 931)]  # 17: Engine fault codes -> clear page
    actions = [f'      <hmiAction id="{a}" target="{t}"/>' for a, t in nav]
    actions += [f'      <raAction id="{a}"/>' for a in (18, 30, 31, 32, 33, 35, 36, 37, *range(40, 49))]
    actions += [f'      <raAction id="{700 + i}"/>' for i in range(len(entries))]
    actions += [f'      <raAction id="{50 + i}"/>' for i in range(len(CAR_TESTS))]
    events = (
        '      <focusEvent id="60" target="971"/>\n'  # a car test started: its results page
        '      <focusEvent id="88" target="930"/>'  # the clear started: the codes page shows its result
    )

    preview = image(89, 6, 0, 0, 549, 484) + "".join(
        label(236 + i, 106 + i, 20, 345 + 45 * i, 509, 45) for i in range(3)
    )  # the trip lines, as BMW's own My Vehicle card shows on larger screens
    states = state(900, "Gauges", preview)
    menu = "".join(button(1600 + i, a, text) for i, (a, _, text) in enumerate(MENU))
    # the owner's switch for the engine computer's values (read only): gauges-dme, the gateway found when needed
    menu += '          <checkbox id="1610" action="37" model="125" text="Engine values (DME)"/>\n'
    menu += labelrow(1611, 126)  # what the engine values are doing
    states += state(901, "Gauges", menu)
    for page in range(PAGES):
        cards = "".join(card(page * PER_PAGE + s) for s in range(PER_PAGE))
        title = "Dashboard" if page == 0 else f"Dashboard {page + 1}"
        states += state(910 + page, title, cards, parent=901)
    live = "".join(modelbutton(1700 + c, 36, 400 + c) for c in range(PAGES * PER_PAGE))
    states += state(980, "Live values", live, parent=901)
    shift = (
        image(220, 90, 20, 40, 760, 40)
        + label(170, 91, 20, 100, 760)
        + label(171, 92, 20, 170, 760)
        + label(172, 93, 20, 240, 760)
    )
    states += state(920, "Shift light", shift, parent=901)
    codes = (
        button(1441, 35, "Read again")
        + button(1442, 17, "Clear the codes...")
        + "".join(labelrow(180 + i, 95 + i) for i in range(8))
    )
    states += state(930, "Engine fault codes", codes, parent=901)
    # The clear asks first: what it does, then the one button that sends it (iDrive's Back leaves without).
    clear = labelrow(1443, 127) + labelrow(1444, 128) + button(1445, 18, "Yes, clear the engine's codes")
    states += state(931, "Clear engine codes?", clear, parent=930)
    colours = labelrow(190, 103) + "".join(button(191 + i, 40 + i, n) for i, n in enumerate(COLOURS))
    states += state(940, "Gauge colour", colours, parent=901)
    points = labelrow(234, 104) + "".join(
        button(230 + i, 30 + i, f"Shift at {rpm} rpm") for i, rpm in enumerate(SHIFT_POINTS)
    )
    states += state(950, "Shift point", points, parent=901)
    source = {"cds": "", "obd": " (OBD)", "did": " (DME)"}
    choose = labelrow(235, 105) + "".join(
        f'          <checkbox id="{1300 + i}" action="{700 + i}" model="{600 + i}" '
        f'text="{e["title"]}{source[e["source"]] if "(" not in e["title"] else ""}"/>\n'
        for i, e in enumerate(entries)
    )
    states += state(960, "Choose gauges", choose, parent=901)
    tests = "".join(button(1510 + i, 50 + i, t) for i, t in enumerate(CAR_TESTS))
    states += state(970, "Car tests", tests, parent=901)
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
