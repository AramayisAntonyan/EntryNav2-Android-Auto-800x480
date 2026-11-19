#!/usr/bin/env python3
# Copyright (c) 2026 Jiraiya43
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
"""Build the iDrive app's text and image databases.

EntryAad_ALL_Texts.zip holds <lang>.txt ("id=text" lines, same format as BMW's PluginsCoded apps) and
EntryAad_ALL_Images.zip holds <imageId>.png, from hmiapp/res. Both are reproducible: same input, same bytes.

Usage: make_db.py <res dir> <output dir>
"""

import struct
import sys
import zipfile
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gauge_art  # noqa: E402
import gauge_catalogue  # noqa: E402

LANGS = {
    "de": "Deutsch",
    "en-UK": "English (UK)",
    "en-US": "English (US)",
    "es-ES": "Espanol",
    "es-US": "Espanol (US)",
    "fr-FR": "Francais",
    "fr-CA": "Francais (CA)",
    "it": "Italiano",
    "pt": "Portugues",
    "ru": "Russian",
}
TEXTS = {
    1: "Android Auto",
    2: "Android Auto: Connecting...",
    10: "Start Android Auto",
    11: "Stop Android Auto",
    13: "Connecting to the phone...",
    14: "Open Android Auto",
    15: "Start automatically",
    16: "Help",
    17: "USB cable instead of wireless",
    20: "NAV, MAP: Android Auto navigation",
    21: "MEDIA: Android Auto media",
    22: "COM: Android Auto phone",
    23: "MENU: iDrive - BACK: back",
    24: "Phone paired, sound over Bluetooth",
    26: "Free: github.com/Jiraiya43",
    30: " ",
}  # blank title of the page held under Android Auto (no "Android Auto" tab during the volume)
LANG_TEXTS = {  # ASCII only
    "fr-FR": {
        2: "Android Auto : connexion...",
        10: "Demarrer Android Auto",
        11: "Arreter Android Auto",
        13: "Connexion au telephone...",
        14: "Ouvrir Android Auto",
        15: "Demarrage automatique",
        16: "Aide",
        17: "Cable USB au lieu du sans-fil",
        20: "NAV, MAP : navigation Android Auto",
        21: "MEDIA : musique Android Auto",
        22: "COM : telephone Android Auto",
        23: "MENU : iDrive - BACK : retour",
        24: "Telephone appaire, son en Bluetooth",
        26: "Gratuit : github.com/Jiraiya43",
    },
    "de": {
        2: "Android Auto: Verbinden...",
        10: "Android Auto starten",
        11: "Android Auto beenden",
        13: "Verbindung zum Telefon...",
        14: "Android Auto oeffnen",
        15: "Automatisch starten",
        16: "Hilfe",
        17: "USB-Kabel statt kabellos",
        20: "NAV, MAP: Android Auto Navigation",
        21: "MEDIA: Android Auto Medien",
        22: "COM: Android Auto Telefon",
        23: "MENU: iDrive - BACK: zurueck",
        24: "Telefon gekoppelt, Ton per Bluetooth",
        26: "Kostenlos: github.com/Jiraiya43",
    },
}
LANG_TEXTS["fr-CA"] = LANG_TEXTS["fr-FR"]
FIXED_DATE = (2026, 11, 2, 0, 0, 0)  # the same date in every archive: reproducible files


def add(archive, name, data):
    """Store one member, compressed, with the fixed date."""
    info = zipfile.ZipInfo(name, FIXED_DATE)
    info.compress_type = zipfile.ZIP_DEFLATED
    archive.writestr(info, data)


def write_texts(path):
    """One <lang>.txt per language: English, overridden by the language's own texts."""
    with zipfile.ZipFile(path, "w") as archive:
        for lang, langname in LANGS.items():
            texts = dict(TEXTS)
            texts.update(LANG_TEXTS.get(lang, {}))
            body = f"100000={langname}\n" + "".join(f"{key}={value}\n" for key, value in sorted(texts.items()))
            if not body.isascii():
                raise SystemExit(f"FAIL: {lang} texts must stay ASCII")
            add(archive, lang + ".txt", body.encode("utf-8"))


def clear_png(w, h):
    # Fully transparent RGBA PNG (no image library): hides the big Android Auto logo of the blank state held under
    # Android Auto (seen while it is dimmed for the volume bar). An opaque black image looked like a window frame.
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    raw = b"".join(bytes(1 + 4 * w) for _ in range(h))  # filter byte 0 + transparent pixels, per row
    return (
        bytes([0x89])
        + b"PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )


def write_images(path, res):
    """The entry icon, the preview, the clear page background and the 8 frames of the connecting animation."""
    with zipfile.ZipFile(path, "w") as archive:
        add(archive, "55001.png", (res / "aa_row_icon_63x54.png").read_bytes())
        add(archive, "55002.png", (res / "aa_content_preview_549x484.png").read_bytes())
        add(archive, "55003.png", clear_png(549, 484))
        for frame in range(8):
            add(archive, f"{55010 + frame}.png", (res / "spinner" / f"spin_{frame}.png").read_bytes())


def rgba_png(w, h, pixel):
    """An RGBA PNG from pixel(x, y) -> (r, g, b, a), no image library."""

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


# The Gauges app (hmiapp/gauge_app.cpp, CarGauges.xml, ids in hmiapp/gauge_ids.hpp): its entry icon and preview card
# under My Vehicle, its bars in every colour, and the shift light's rpm bar.
GAUGE_ICON_SIZE = (63, 54)  # as the Android Auto row icon
GAUGE_CARD_SIZE = (549, 484)  # as the Android Auto preview card
GAUGE_BAR_SIZE = gauge_art.CARD_BAR.size  # a card's bar (165 x 12)
GAUGE_BAR_LEVELS = 25  # gauge_values.hpp kBarLevels: images 56000 + colour * 32 + level
GAUGE_COLOURS = (  # gauge_ids.hpp kColourNames, BoostDeck's palette
    (240, 125, 0),  # BMW Orange
    (30, 136, 229),  # Blue
    (67, 160, 71),  # Green
    (235, 235, 235),  # White
    (142, 68, 173),  # Purple
    (233, 30, 99),  # Pink
    (0, 188, 212),  # Cyan
    (229, 57, 53),  # Red
)
SHIFT_LEVELS = 33  # gauge_ids.hpp kShiftLevels: 57000 + level, past the shift point 57100 + level
SHIFT_FILL, SHIFT_WARN = (240, 125, 0), (229, 57, 53)
# The big looks (hmiapp/gauge_layout.hpp): images + colour * FINE_STRIDE + level (gauge_ids.hpp kFineLevels ...)
FINE_LEVELS, FINE_STRIDE = 41, 64
IMG_WIDE_BAR, IMG_TALL_BAR, IMG_NEEDLE, IMG_WIDE_NEEDLE = 59000, 59500, 60000, 60500
IMG_FACE, IMG_WIDE_FACE, IMG_WIDE_TRACK, IMG_TALL_TRACK, IMG_CARD_ICON = 61000, 61100, 61200, 61300, 61400
IMG_ARC_NEEDLE, IMG_WIDE_ARC_NEEDLE, IMG_ARC_FACE, IMG_WIDE_ARC_FACE = 62000, 62500, 63000, 63100


def car_png(size, box, samples):
    """A car seen from the side, white on transparent, drawn in the 63x54 icon's units and scaled into box
    (x, y, w, h) of an image of the given size, samples x samples per pixel: body, glasshouse, two wheels."""
    w, h = size
    bx, by, bw, bh = box
    sx, sy = bw / GAUGE_ICON_SIZE[0], bh / GAUGE_ICON_SIZE[1]

    def inside_poly(px, py, pts):
        hit = False
        for i in range(len(pts)):
            (x1, y1), (x2, y2) = pts[i], pts[(i + 1) % len(pts)]
            if (y1 > py) != (y2 > py) and px < (x2 - x1) * (py - y1) / (y2 - y1) + x1:
                hit = not hit
        return hit

    body = [(3, 33), (6, 27), (17, 24), (25, 15), (44, 15), (52, 24), (59, 26), (60, 34), (57, 38), (6, 38), (3, 36)]
    window_a = [(19, 24), (26, 17), (34, 17), (34, 24)]
    window_b = [(37, 17), (43, 17), (49, 24), (37, 24)]
    wheels = [(16, 38), (47, 38)]

    def covered(px, py):
        for cx, cy in wheels:
            d = (px - cx) ** 2 + (py - cy) ** 2
            if d <= 5.5**2:
                return d > 2.5**2  # tyre ring, hub left open
            if d <= 7.5**2:
                return False  # a gap between the wheel and the body
        if inside_poly(px, py, window_a) or inside_poly(px, py, window_b):
            return False
        return inside_poly(px, py, body)

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


def car_icon_png():
    return car_png(GAUGE_ICON_SIZE, (0, 0, *GAUGE_ICON_SIZE), 4)


def car_card_png():
    """The preview card: the same car, 7x larger, centred."""
    w, h = GAUGE_CARD_SIZE
    cw, ch = GAUGE_ICON_SIZE[0] * 7, GAUGE_ICON_SIZE[1] * 7
    return car_png(GAUGE_CARD_SIZE, ((w - cw) // 2, (h - ch) // 2, cw, ch), 2)


def write_gauge_images(path):
    """CarGauges_ALL_Images.zip: the entry icon and card, the bars in every colour, the shift light's bar."""
    with zipfile.ZipFile(path, "w") as archive:
        add(archive, "58001.png", car_icon_png())
        # The owner's own car on the card when present (.local/, never in git: it shows the number plate), made
        # from a photo on a white background; else the drawn car.
        own = Path(__file__).resolve().parents[1] / ".local" / "gauges_card_549x484.png"
        add(archive, "58002.png", own.read_bytes() if own.is_file() else car_card_png())
        # No card tiles any more (the owner: no background is better): 58003..58005 are gone.
        for colour, fill in enumerate(GAUGE_COLOURS):
            for level in range(GAUGE_BAR_LEVELS):
                add(
                    archive,
                    f"{56000 + colour * 32 + level}.png",
                    gauge_art.whole_bar(gauge_art.CARD_BAR, level, GAUGE_BAR_LEVELS, fill),
                )
        for level in range(SHIFT_LEVELS):
            for first, fill in ((57000, SHIFT_FILL), (57100, SHIFT_WARN)):
                add(
                    archive, f"{first + level}.png", gauge_art.whole_bar(gauge_art.SHIFT_BAR, level, SHIFT_LEVELS, fill)
                )
        write_big_looks(archive)


def write_big_looks(archive):
    """The dials and the wide and tall bars: needles and bar values in every colour; per gauge its dial faces, bar
    tracks and card icon, each with the gauge's icon and scale."""
    for colour, fill in enumerate(GAUGE_COLOURS):
        for level in range(FINE_LEVELS):
            n = colour * FINE_STRIDE + level
            add(archive, f"{IMG_WIDE_BAR + n}.png", gauge_art.WIDE_BAR.value(level, FINE_LEVELS, fill))
            add(archive, f"{IMG_TALL_BAR + n}.png", gauge_art.TALL_BAR.value(level, FINE_LEVELS, fill))
    dials = (
        (gauge_art.SMALL_DIAL, IMG_NEEDLE),
        (gauge_art.WIDE_DIAL, IMG_WIDE_NEEDLE),
        (gauge_art.SMALL_ARC, IMG_ARC_NEEDLE),
        (gauge_art.WIDE_ARC, IMG_WIDE_ARC_NEEDLE),
    )
    for dial, first in dials:
        masks = dial.needle_masks(FINE_LEVELS)
        for colour, fill in enumerate(GAUGE_COLOURS):
            for level, mask in enumerate(masks):
                add(archive, f"{first + colour * FINE_STRIDE + level}.png", dial.needle(mask, fill))
    entries = gauge_catalogue.entries()
    if len(entries) > 100:
        raise SystemExit("FAIL: more than 100 gauges: the image ranges of gauge_ids.hpp overlap")
    for n, e in enumerate(entries):
        scale, icon = gauge_catalogue.scale_key(e), e["icon"]
        add(archive, f"{IMG_FACE + n}.png", gauge_art.SMALL_DIAL.face(scale, icon))
        add(archive, f"{IMG_WIDE_FACE + n}.png", gauge_art.WIDE_DIAL.face(scale, icon))
        add(archive, f"{IMG_WIDE_TRACK + n}.png", gauge_art.WIDE_BAR.track(scale, icon))
        add(archive, f"{IMG_TALL_TRACK + n}.png", gauge_art.TALL_BAR.track(scale, icon))
        add(archive, f"{IMG_CARD_ICON + n}.png", gauge_art.card_icon(icon))
        add(archive, f"{IMG_ARC_FACE + n}.png", gauge_art.SMALL_ARC.face(scale, icon))
        add(archive, f"{IMG_WIDE_ARC_FACE + n}.png", gauge_art.WIDE_ARC.face(scale, icon))


def main():
    if len(sys.argv) != 3:
        raise SystemExit("Usage: make_db.py <res dir> <output dir>")
    res, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    write_texts(out / "EntryAad_ALL_Texts.zip")
    write_images(out / "EntryAad_ALL_Images.zip", res)
    write_gauge_images(out / "CarGauges_ALL_Images.zip")
    print("wrote EntryAad_ALL_Texts.zip, EntryAad_ALL_Images.zip, CarGauges_ALL_Images.zip in", out)


if __name__ == "__main__":
    main()
