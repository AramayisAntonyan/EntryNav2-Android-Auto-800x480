# Copyright (c) 2026 Jiraiya43
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
"""The iDrive apps' descriptions: no id used twice, every reference resolves, no image id shared between the apps.

A duplicate model id (40, the NativeStateID model and a checkbox model) once reached the car; iDrive's parser does not
complain, it just binds one of them. Checks hmiapp/EntryAad.xml, the Gauges description make_gauges_xml.py writes
and hmiapp/AuxCamera.xml.
"""

import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
failures = []


def check(cond, what):
    if not cond:
        failures.append(what)


def components(state):
    for part in state:
        for c in part:
            if c.get("id"):
                yield c


def check_app(name, path):
    app = ET.parse(path).getroot().find("pluginApp")
    ids = {}
    for section in ("models", "actions", "events"):
        node = app.find(section)
        # a combinedAction's own actions are in the same table: counted too
        members = [e for e in node.iter() if e is not node and e.get("id")] if node is not None else []
        counted = Counter(e.get("id") for e in members)
        dups = sorted(k for k, n in counted.items() if n > 1)
        check(not dups, f"{name}: {section} ids used twice: {dups}")
        ids[section] = set(counted)
    all_states = list(app.find("hmiStates"))  # hmiState, popupHmiState, ...
    states = {s.get("id") for s in all_states}
    comps = Counter(c.get("id") for s in all_states for c in components(s))
    check(not [k for k, n in comps.items() if n > 1], f"{name}: component ids used twice")
    # iDrive keeps states and components in one table: a state sharing a component's id never opens (Car tests, 170)
    clash = sorted(states & set(comps), key=int)
    check(not clash, f"{name}: state ids also used by components: {clash}")
    # actions and models share one table too: the any-distance checkbox (action 31, also a model) never fired
    shared = sorted(ids["actions"] & ids["models"], key=int)
    check(not shared, f"{name}: action ids also used by models: {shared}")
    entry = app.find("entryButton")
    check(entry.get("action") in ids["actions"], f"{name}: entry action {entry.get('action')} missing")
    for s in all_states:
        check(not s.get("parentId") or s.get("parentId") in states, f"{name}: state {s.get('id')} parent missing")
        for c in components(s):
            for attr in ("model", "textModel", "imageModel"):
                ref = c.get(attr)
                check(not ref or ref in ids["models"], f"{name}: component {c.get('id')} {attr} {ref} missing")
            ref = c.get("action")
            check(not ref or ref in ids["actions"], f"{name}: component {c.get('id')} action {ref} missing")
    events = app.find("events")
    for e in list(app.find("actions").iter()) + (list(events) if events is not None else []):
        ref = e.get("target")
        check(not ref or ref in states, f"{name}: {e.tag} {e.get('id')} target state {ref} missing")


def camera_image_ids():
    """The image ids the Camera app's XML names (imageIdModel) and its archive holds: 54001 and up only."""
    app = ET.parse(ROOT / "hmiapp/AuxCamera.xml").getroot().find("pluginApp")
    return {e.get("imageId") for e in app.iter("imageIdModel")}


def check_camera_images(archives):
    named = camera_image_ids()
    check(all(54001 <= int(i) < 55000 for i in named), f"AuxCamera: image ids outside 54001..54999: {sorted(named)}")
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([sys.executable, "-I", str(ROOT / "hmiapp/make_camera_db.py"), tmp], check=True)
        held = {n[:-4] for n in zipfile.ZipFile(Path(tmp) / "AuxCamera_ALL_Images.zip").namelist()}
    check(named <= held, f"AuxCamera: images named but not in its archive: {sorted(named - held)}")
    for a in archives:
        if a.is_file():
            shared = held & {n[:-4] for n in zipfile.ZipFile(a).namelist()}
            check(not shared, f"image ids in AuxCamera and {a.name}: {sorted(shared)}")


def main():
    check_app("EntryAad", ROOT / "hmiapp/EntryAad.xml")
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "CarGauges.xml"
        subprocess.run([sys.executable, "-I", str(ROOT / "hmiapp/make_gauges_xml.py"), str(out)], check=True)
        check_app("CarGauges", out)
    check_app("AuxCamera", ROOT / "hmiapp/AuxCamera.xml")
    archives = [ROOT / "hmiapp/EntryAad_ALL_Images.zip", ROOT / "hmiapp/CarGauges_ALL_Images.zip"]
    if all(a.is_file() for a in archives):
        shared = set(zipfile.ZipFile(archives[0]).namelist()) & set(zipfile.ZipFile(archives[1]).namelist())
        check(not shared, f"image ids in both apps: {sorted(shared)}")
    check_camera_images(archives)
    for f in failures:
        print("FAIL", f)
    print("PASS rhmi ids" if not failures else f"{len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
