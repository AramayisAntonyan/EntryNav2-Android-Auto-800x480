#!/usr/bin/env python3
# Copyright (c) 2026 Jiraiya43
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
"""The head unit draws the scales the owner sets by hand (hmiapp/gauge_draw.cpp): its faces and tracks must match
what hmiapp/gauge_art.py draws on the PC for the same scale. Draws a set of them both ways and compares the pixels.

Usage: test_gauge_draw.py <gauge_draw_dump binary>   (writes its own CarGauges_Glyphs.bin)
"""

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "hmiapp"))
import gauge_art  # noqa: E402
import gauge_catalogue  # noqa: E402
import make_db  # noqa: E402

KINDS = (  # gauge_draw.hpp Kind order
    lambda sk, i, t, u: gauge_art.SMALL_DIAL.face(sk, i, t, u),
    lambda sk, i, t, u: gauge_art.WIDE_DIAL.face(sk, i, t, u),
    lambda sk, i, t, u: gauge_art.WIDE_BAR.track(sk, i, t, u),
    lambda sk, i, t, u: gauge_art.TALL_BAR.track(sk, i, t, u),
    lambda sk, i, t, u: gauge_art.SMALL_ARC.face(sk, i, t, u),
    lambda sk, i, t, u: gauge_art.WIDE_ARC.face(sk, i, t, u),
)


def main():
    dump = sys.argv[1]
    entries = gauge_catalogue.entries()
    by_title = {e["title"]: n for n, e in enumerate(entries)}
    cases = [  # (kind, title, lo, hi): the catalogue's scales and hand-set ones
        (0, "Water", 40, 130),
        (4, "Oil", 40, 150),
        (1, "RPM", 0, 7000),
        (5, "RPM", 0, 6500),
        (2, "Boost", 0, 2.5),
        (3, "Boost", 0, 1.6),
        (3, "Oil pressure", 0, 8),
        (0, "Steering angle", -540, 540),
        (2, "Speed", 0, 260),
        (0, "Boost", -0.8, 1.6),
    ]
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        glyphs = str(Path(tmp) / "glyphs.bin")
        make_db.write_glyphs(glyphs)
        for k, (kind, title, lo, hi) in enumerate(cases):
            e = entries[by_title[title]]
            red = gauge_catalogue.red_from(e, lo, hi)
            icon, unit = e["icon"], e["unit"]
            name_title = None if icon else e["title"]
            subprocess.run(
                [dump, glyphs, tmp, str(kind), str(by_title[title]), repr(float(lo)), repr(float(hi)),
                 "1" if red is not None else "0", repr(float(red or 0)), e["title"], unit, f"c{k}"],
                check=True,
            )  # fmt: skip
            ours, w, h = gauge_art.unpng((Path(tmp) / f"c{k}.png").read_bytes())
            ref, rw, rh = gauge_art.unpng(KINDS[kind]((lo, hi, red, None), icon, name_title, unit))
            if (w, h) != (rw, rh):
                print(f"FAIL {title} kind {kind}: size {w}x{h}, expected {rw}x{rh}")
                failures += 1
                continue
            diff = [abs(a - b) for a, b in zip(ours, ref)]
            big = sum(1 for d in diff if d > 24)
            if big > len(diff) // 2000:  # a few edge pixels may round the other way
                print(f"FAIL {title} {lo}..{hi} kind {kind}: {big} channel values differ by more than 24")
                failures += 1
        # A gauge past its alert flashes its all-red face: nothing white is left on it, and it is drawn at all.
        for k, (kind, title, lo, hi, at) in enumerate([(0, "Water", 40, 130, 110), (3, "Boost", 0, 1.6, 1.2)]):
            e = entries[by_title[title]]
            subprocess.run(
                [dump, glyphs, tmp, str(kind), str(by_title[title]), repr(float(lo)), repr(float(hi)), "1",
                 repr(float(at)), e["title"], e["unit"], f"a{k}", "1"],
                check=True,
            )  # fmt: skip
            px, w, h = gauge_art.unpng((Path(tmp) / f"a{k}.png").read_bytes())
            quads = [px[i : i + 4] for i in range(0, len(px), 4)]
            white = sum(1 for r, g, b, a in quads if a > 128 and r > 200 and g > 200 and b > 200)
            red = sum(1 for r, g, b, a in quads if a > 128 and r > 200 and g < 90 and b < 90)
            if white or red < 200:
                print(f"FAIL {title} alert face: {white} white pixels, {red} red ones")
                failures += 1
    # Past its alert a bar flashes red: the pack's red value bars (make_db.py IMG_*_BAR_RED) have no white segment.
    full = make_db.GAUGE_BAR_LEVELS - 1
    for name, data in (
        ("card", gauge_art.whole_bar(gauge_art.CARD_BAR, full, make_db.GAUGE_BAR_LEVELS, gauge_art.RED)),
        ("wide", gauge_art.WIDE_BAR.value(make_db.FINE_LEVELS - 1, make_db.FINE_LEVELS, tint=gauge_art.RED)),
        ("tall", gauge_art.TALL_BAR.value(make_db.FINE_LEVELS - 1, make_db.FINE_LEVELS, tint=gauge_art.RED)),
    ):
        px, _, _ = gauge_art.unpng(data)
        quads = [px[i : i + 4] for i in range(0, len(px), 4)]
        white = sum(1 for r, g, b, a in quads if a > 128 and r > 200 and g > 200 and b > 200)
        red = sum(1 for r, g, b, a in quads if a > 128 and r > 200 and g < 90 and b < 90)
        if white or red < 100:
            print(f"FAIL {name} red bar: {white} white pixels, {red} red ones")
            failures += 1
    print(
        "PASS gauge draw: the unit draws hand-set scales as the PC does, an alert's face all red, red alert bars"
        if not failures
        else f"{failures} failed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
