#!/usr/bin/env python3
# Copyright (c) 2026 Jiraiya43
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
"""Render the figures the Gauges app's dials and bar scales carry, once, from a real typeface: Barlow Semi Condensed
SemiBold (github.com/jpt/barlow, SIL Open Font License 1.1), close to BMW's cluster lettering. Writes
hmiapp/res/gauge_figures.png (white figures, coverage in alpha, one row per size) and gauge_figures.txt (where each
figure is); with Material Symbols Outlined also the gauges' icons (gauge_icons.png, .txt). hmiapp/gauge_art.py places
them. Needs Pillow (run in docker: apt-get install python3-pil) and the font files.

Usage: make_gauge_figures.py <font.ttf> <hmiapp/res> [<MaterialSymbolsOutlined.ttf> <its .codepoints>]
"""

import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "hmiapp"))
import gauge_art  # noqa: E402

CHARS = "0123456789.-/x"
SIZES = (10, 11, 13, 15, 18)  # the height of a figure in pixels


def font_for(path, height):
    """The font size whose "0" is `height` pixels high."""
    best = None
    for px in range(height, height * 2 + 4):
        font = ImageFont.truetype(path, px)
        left, t, r, b = font.getbbox("0")
        if best is None or abs((b - t) - height) < abs(best[1] - height):
            best = (font, b - t)
    return best[0]


ICON_SIZES = (22, 30)  # the icons' box in pixels: bars and cards, dials


def icons(font_path, codepoints, out):
    """The gauges' icons (hmiapp/gauge_catalogue.py ICONS) from Material Symbols Outlined (Google, Apache License 2.0):
    hmiapp/res/gauge_icons.png (white, coverage in alpha, a row per size) and gauge_icons.txt (name size x y)."""
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "hmiapp"))
    import gauge_catalogue

    names = list(gauge_catalogue.ICONS)
    points = dict(line.split() for line in Path(codepoints).read_text(encoding="utf-8").splitlines() if line.strip())
    width, height = max(ICON_SIZES) * len(names), sum(ICON_SIZES)
    rgba = bytearray(width * height * 4)
    lines = ["# name size x y (a size x size box)"]
    y = 0
    for size in ICON_SIZES:
        font = ImageFont.truetype(font_path, size)
        for k, name in enumerate(names):
            img = Image.new("L", (size, size), 0)
            ch = chr(int(points[name], 16))
            left, t, r, b = font.getbbox(ch)
            at = ((size - (r - left)) / 2 - left, (size - (b - t)) / 2 - t)  # centred in its box
            ImageDraw.Draw(img).text(at, ch, font=font, fill=255)
            px = img.load()
            x = k * max(ICON_SIZES)
            for gy in range(size):
                for gx in range(size):
                    i = ((y + gy) * width + x + gx) * 4
                    rgba[i : i + 4] = bytes((255, 255, 255, px[gx, gy]))
            lines.append(f"{name} {size} {x} {y}")
        y += size
    (out / "gauge_icons.png").write_bytes(gauge_art.png(rgba, width, height))
    (out / "gauge_icons.txt").write_text("\n".join(lines) + "\n", encoding="ascii", newline="\n")
    print(f"wrote gauge_icons.png ({width}x{height}): {len(names)} icons")


def main():
    if len(sys.argv) not in (3, 5):
        raise SystemExit(
            "Usage: make_gauge_figures.py <font.ttf> <hmiapp/res> [<MaterialSymbolsOutlined.ttf> <its .codepoints>]"
        )
    path, out = sys.argv[1], Path(sys.argv[2])
    if len(sys.argv) == 5:
        icons(sys.argv[3], sys.argv[4], out)
    cells = []  # (size, char, image, ink left from the pen, advance)
    for size in SIZES:
        font = font_for(path, size)
        top = font.getbbox("0")[1]
        for ch in CHARS:
            left, t, r, b = font.getbbox(ch)
            w = max(1, r - left)
            img = Image.new("L", (w, size), 0)
            ImageDraw.Draw(img).text((-left, -top), ch, font=font, fill=255)
            cells.append((size, ch, img, left, font.getlength(ch)))
    width = max(sum(c[2].width + 2 for c in cells if c[0] == s) for s in SIZES)
    height = sum(SIZES) + 2 * len(SIZES)
    rgba = bytearray(width * height * 4)
    lines = ["# size char x y width height ink_left advance (pixels; ink_left and advance from the pen)"]
    y = 0
    for size in SIZES:
        x = 0
        for s, ch, img, left, advance in cells:
            if s != size:
                continue
            px = img.load()
            for gy in range(img.height):
                for gx in range(img.width):
                    i = ((y + gy) * width + x + gx) * 4
                    rgba[i : i + 4] = bytes((255, 255, 255, px[gx, gy]))
            lines.append(f"{size} {ch} {x} {y} {img.width} {img.height} {left} {advance:.2f}")
            x += img.width + 2
        y += size + 2
    (out / "gauge_figures.png").write_bytes(gauge_art.png(rgba, width, height))
    (out / "gauge_figures.txt").write_text("\n".join(lines) + "\n", encoding="ascii", newline="\n")
    print(f"wrote gauge_figures.png ({width}x{height}) and gauge_figures.txt: {len(cells)} figures")


if __name__ == "__main__":
    main()
