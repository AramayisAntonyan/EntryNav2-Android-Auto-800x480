# Copyright (c) 2026 Jiraiya43
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
"""Write hmiapp/AuxCamera_ALL_Images.zip, the Camera app's image archive (hmiapp/camera_app.cpp, AuxCamera.xml):
the entry's icon (54001, 63x54, as the other apps' row icons) and its preview card (54002, 549x484), a video camera
drawn white on transparent as the Gauges app's car. Reproducible: same input, same bytes. No image library.

Usage: make_camera_db.py <output folder>
"""

import struct
import sys
import zipfile
import zlib
from pathlib import Path

FIXED_DATE = (2026, 11, 2, 0, 0, 0)  # as hmiapp/make_db.py: the same date in every archive
ICON_SIZE = (63, 54)
CARD_SIZE = (549, 484)
IMG_ICON, IMG_CARD = 54001, 54002  # camera_ids.hpp kImgIcon, kImgCard (EntryAad 55001+, Gauges 56000+)


def rgba_png(w, h, pixel):
    """An RGBA PNG from pixel(x, y) -> (r, g, b, a)."""

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    raw = b"".join(b"\0" + b"".join(bytes(pixel(x, y)) for x in range(w)) for y in range(h))
    return (
        bytes([0x89])
        + b"PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )


def covered(px, py):
    """A video camera in the 63x54 icon's units: a rounded body with a lens ring, the viewfinder wedge on the right."""
    # body: a rounded rectangle 6..42 x 15..41, corner radius 5
    x0, y0, x1, y1, r = 6, 15, 42, 41, 5
    if x0 <= px <= x1 and y0 <= py <= y1:
        cx = min(max(px, x0 + r), x1 - r)
        cy = min(max(py, y0 + r), y1 - r)
        if (px - cx) ** 2 + (py - cy) ** 2 <= r * r:
            d = (px - 21) ** 2 + (py - 28) ** 2
            return not (4.0**2 < d <= 7.0**2)  # the lens: a ring cut out of the body
    # the wedge: from the body's side to the right, wider at the far end
    if 45 <= px <= 58:
        half = 5 + (px - 45) * 7 / 13
        return abs(py - 28) <= half
    return False


def glyph_png(size, box, samples):
    w, h = size
    bx, by, bw, bh = box
    sx, sy = bw / ICON_SIZE[0], bh / ICON_SIZE[1]
    n2 = samples * samples

    def pixel(x, y):
        if not (bx <= x < bx + bw and by <= y < by + bh):
            return (0, 0, 0, 0)
        n = sum(
            covered((x - bx + (i + 0.5) / samples) / sx, (y - by + (j + 0.5) / samples) / sy)
            for i in range(samples)
            for j in range(samples)
        )
        return (255, 255, 255, n * 255 // n2)

    return rgba_png(w, h, pixel)


def add(archive, name, data):
    info = zipfile.ZipInfo(name, FIXED_DATE)
    info.compress_type = zipfile.ZIP_DEFLATED
    archive.writestr(info, data)


def write_camera_images(path):
    w, h = CARD_SIZE
    cw, ch = ICON_SIZE[0] * 6, ICON_SIZE[1] * 6
    with zipfile.ZipFile(path, "w") as archive:
        add(archive, f"{IMG_ICON}.png", glyph_png(ICON_SIZE, (0, 0, *ICON_SIZE), 4))
        add(archive, f"{IMG_CARD}.png", glyph_png(CARD_SIZE, ((w - cw) // 2, (h - ch) // 2, cw, ch), 2))


def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: make_camera_db.py <output folder>")
    out = Path(sys.argv[1]) / "AuxCamera_ALL_Images.zip"
    write_camera_images(out)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
