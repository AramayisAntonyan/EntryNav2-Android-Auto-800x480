# Copyright (c) 2026 Jiraiya43
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
"""The Gauges app's drawn pictures: dial faces and needles, and bars with their scales. No card background (the owner:
the gauges stand on iDrive's own background).

Dials follow BMW's cluster gauges (the F-series fuel gauge, the F87 M2): one rim line all the way, turning in at both
ends; the notches sit inside it with a small gap, a longer one where a number stands; the needle is a blade with a
dark slot and no hub. Two shapes: a half circle over its pivot, or the fuel gauge's arc under it.
Bars follow the current M5's (G90) gauges: a wedge, thin to thick, cut into slanted segments; a notch under each cut,
numbers at the start, middle and end. The track (per scale) is the back picture, the value's segments (per colour
and level) the front one, both the same size so they line up.

Shapes are anti-aliased (a pixel's coverage from its distance to the shape's edge); the figures are pictures rendered
once from a typeface (tools/make_gauge_figures.py: hmiapp/res/gauge_figures.png). hmiapp/make_db.py stores the
results; the ids are in hmiapp/gauge_ids.hpp.
"""

import math
import struct
import zlib
from pathlib import Path

WHITE = (235, 235, 235)
RED = (229, 57, 53)
TRACK = (120, 122, 128)  # a bar's segments above the value
FIGURES = Path(__file__).resolve().parent / "res" / "gauge_figures"
ICONS = Path(__file__).resolve().parent / "res" / "gauge_icons"


def png(rgba, w, h):
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    raw = b"".join(b"\0" + bytes(rgba[y * w * 4 : (y + 1) * w * 4]) for y in range(h))
    return (
        bytes([0x89])
        + b"PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )


def unpng(data):
    """An RGBA PNG written by png() above (filter 0 on every row) back to (rgba, w, h)."""
    pos, idat, w, h = 8, b"", 0, 0
    while pos < len(data):
        n = struct.unpack(">I", data[pos : pos + 4])[0]
        kind, body = data[pos + 4 : pos + 8], data[pos + 8 : pos + 8 + n]
        if kind == b"IHDR":
            w, h = struct.unpack(">II", body[:8])
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    out = bytearray()
    for y in range(h):
        row = raw[y * (w * 4 + 1) : (y + 1) * (w * 4 + 1)]
        if row[0] != 0:
            raise ValueError("unfiltered rows only")
        out += row[1:]
    return out, w, h


class Figures:
    """The figure pictures: per size, per character, its place in the sheet and how far it moves the pen."""

    def __init__(self):
        self.alpha, self.w, _ = unpng(FIGURES.with_suffix(".png").read_bytes())
        self.glyphs = {}
        for line in FIGURES.with_suffix(".txt").read_text(encoding="ascii").splitlines():
            if line.startswith("#") or not line.strip():
                continue
            size, ch, x, y, w, h, left, advance = line.split()
            self.glyphs[(int(size), ch)] = (int(x), int(y), int(w), int(h), int(left), float(advance))
        self.sizes = sorted({s for s, _ in self.glyphs})

    def size(self, height):
        return min(self.sizes, key=lambda s: abs(s - height))

    def width(self, s, height):
        size = self.size(height)
        return sum(self.glyphs[(size, ch)][5] for ch in s) + 0.6 * (len(s) - 1)


_figures = None
_icons = None


def icons():
    """The icon pictures (tools/make_gauge_figures.py): {(name, size): (x, y)} and the sheet."""
    global _icons
    if _icons is None:
        alpha, w, _ = unpng(ICONS.with_suffix(".png").read_bytes())
        places = {}
        for line in ICONS.with_suffix(".txt").read_text(encoding="ascii").splitlines():
            if line.strip() and not line.startswith("#"):
                name, size, x, y = line.split()
                places[(name, int(size))] = (int(x), int(y))
        _icons = (places, alpha, w)
    return _icons


def figures():
    global _figures
    if _figures is None:
        _figures = Figures()
    return _figures


def text_width(s, height):
    return figures().width(s, height)


class Mask:
    """Coverage 0..1 per pixel, with the box that holds anything (so empty parts cost nothing later)."""

    def __init__(self, w, h):
        self.w, self.h = w, h
        self.a = [0.0] * (w * h)
        self.box = None

    def _grow(self, x0, y0, x1, y1):
        b = self.box
        self.box = (x0, y0, x1, y1) if b is None else (min(b[0], x0), min(b[1], y0), max(b[2], x1), max(b[3], y1))

    def _cover(self, x, y, cov):
        if cov > 0 and 0 <= x < self.w and 0 <= y < self.h:
            i = y * self.w + x
            if cov > self.a[i]:
                self.a[i] = 1.0 if cov > 1 else cov

    def stroke(self, x1, y1, x2, y2, r1, r2=None):
        """A segment with round ends, radius r1 at (x1, y1) narrowing to r2 at (x2, y2)."""
        r2 = r1 if r2 is None else r2
        rm = max(r1, r2) + 1
        x0, xe = max(0, int(min(x1, x2) - rm)), min(self.w - 1, int(max(x1, x2) + rm) + 1)
        y0, ye = max(0, int(min(y1, y2) - rm)), min(self.h - 1, int(max(y1, y2) + rm) + 1)
        if x0 > xe or y0 > ye:
            return
        dx, dy = x2 - x1, y2 - y1
        ll = dx * dx + dy * dy
        for y in range(y0, ye + 1):
            py = y + 0.5 - y1
            for x in range(x0, xe + 1):
                px = x + 0.5 - x1
                t = 0.0 if ll == 0 else max(0.0, min(1.0, (px * dx + py * dy) / ll))
                ex, ey = px - t * dx, py - t * dy
                self._cover(x, y, r1 + (r2 - r1) * t + 0.5 - math.sqrt(ex * ex + ey * ey))
        self._grow(x0, y0, xe, ye)

    def polyline(self, pts, r):
        for (xa, ya), (xb, yb) in zip(pts, pts[1:]):
            self.stroke(xa, ya, xb, yb, r)

    def polygon(self, pts):
        """Fills a convex polygon, anti-aliased (a pixel's coverage from its distance to the nearest edge)."""
        if len(pts) < 3:
            return
        area = sum(xa * yb - xb * ya for (xa, ya), (xb, yb) in zip(pts, pts[1:] + pts[:1]))
        sgn = 1 if area > 0 else -1
        edges = []
        for (xa, ya), (xb, yb) in zip(pts, pts[1:] + pts[:1]):
            ln = math.hypot(xb - xa, yb - ya)
            if ln > 1e-9:
                edges.append((xa, ya, sgn * -(yb - ya) / ln, sgn * (xb - xa) / ln))  # inward normal
        if len(edges) < 3 or abs(area) < 1e-6:
            return  # nothing to fill
        x0, x1 = max(0, int(min(p[0] for p in pts))), min(self.w - 1, int(max(p[0] for p in pts)) + 1)
        y0, y1 = max(0, int(min(p[1] for p in pts))), min(self.h - 1, int(max(p[1] for p in pts)) + 1)
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self._cover(x, y, min((x + 0.5 - ex) * nx + (y + 0.5 - ey) * ny for ex, ey, nx, ny in edges) + 0.5)
        self._grow(x0, y0, x1, y1)

    def text(self, s, x, y, height, anchor="centre"):
        """The figures of s, about height px high, centred on (x, y); "left" / "right": that edge at x."""
        f = figures()
        size = f.size(height)
        width = f.width(s, height)
        left = {"left": x, "right": x - width}.get(anchor, x - width / 2)
        left = max(0.0, min(self.w - width, left))
        top = int(round(y - size / 2))
        pen = left
        for ch in s:
            gx, gy, gw, gh, ink, advance = f.glyphs[(size, ch)]
            ox = int(round(pen + ink))
            for j in range(gh):
                for i in range(gw):
                    self._cover(ox + i, top + j, f.alpha[((gy + j) * f.w + gx + i) * 4 + 3] / 255.0)
            pen += advance + 0.6
        self._grow(max(0, int(left)), max(0, top), min(self.w - 1, int(left + width) + 1), min(self.h - 1, top + size))


def draw_icon(mask, name, cx, cy, size):
    """The icon `name` (a size x size picture) centred on (cx, cy); nothing for None."""
    if not name:
        return
    places, alpha, sw = icons()
    x0, y0 = places[(name, size)]
    left, top = int(round(cx - size / 2)), int(round(cy - size / 2))
    for j in range(size):
        for i in range(size):
            mask._cover(left + i, top + j, alpha[((y0 + j) * sw + x0 + i) * 4 + 3] / 255.0)
    mask._grow(max(0, left), max(0, top), min(mask.w - 1, left + size), min(mask.h - 1, top + size))


def paint(rgba, w, mask, colour, opacity=1.0):
    """Draws colour over rgba wherever the mask covers (straight alpha, "over"), at the given opacity."""
    if mask.box is None:
        return
    x0, y0, x1, y1 = mask.box
    a = mask.a
    for y in range(y0, y1 + 1):
        row = y * w
        for x in range(x0, x1 + 1):
            ca = a[row + x] * opacity
            if ca <= 0:
                continue
            i = (row + x) * 4
            da = rgba[i + 3] / 255.0
            oa = ca + da * (1 - ca)
            for k in range(3):
                rgba[i + k] = int((colour[k] * ca + rgba[i + k] * da * (1 - ca)) / oa + 0.5)
            rgba[i + 3] = int(oa * 255 + 0.5)


def crop(data, box):
    """A picture's part (x, y, w, h): the front pictures keep only what can be lit, the image archive stays small."""
    rgba, w, _ = unpng(data)
    x0, y0, cw, ch = box
    out = bytearray()
    for y in range(y0, y0 + ch):
        out += rgba[(y * w + x0) * 4 : (y * w + x0 + cw) * 4]
    return png(out, cw, ch)


def picture(w, h, layers):
    """A transparent picture with each (mask, colour) painted in order."""
    rgba = bytearray(w * h * 4)
    for mask, colour in layers:
        paint(rgba, w, mask, colour)
    return png(rgba, w, h)


# ---- scales ---------------------------------------------------------------------------------------------------------
def ticks(lo, hi, most):
    """The numbered ticks: the finest 1/2/2.5/5 step with at most `most` intervals, and the minor step."""
    for k in range(-3, 8):
        for m in (1, 2, 2.5, 5):
            step = m * 10.0**k
            first = math.ceil(lo / step - 1e-9) * step
            n = int(math.floor((hi - first) / step + 1e-9)) + 1
            if n - 1 <= most:
                minor = step / (4 if m == 2 else 5)
                while (hi - lo) / minor > 48:
                    minor *= 2
                return [first + i * step for i in range(n)], minor
    raise ValueError((lo, hi))


def divisor(lo, hi):
    """Big numbers are written in thousands ("x1000" on the dial), as BMW's rev counters."""
    return 1000 if max(abs(lo), abs(hi)) >= 2000 else 1


def figure(v, div, decimals=None):
    if decimals is not None:
        return f"{v / div:.{decimals}f}"
    s = f"{v / div:.2f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def minors(lo, hi, minor):
    first = math.ceil(lo / minor - 1e-9)
    last = math.floor(hi / minor + 1e-9)
    return [i * minor for i in range(first, last + 1)]


# ---- dials ----------------------------------------------------------------------------------------------------------
class Dial:
    """A dial: pivot (cx, cy), rim radius, the rim from angle a0 (the scale's start) to a1 (radians, counter-clockwise,
    0 to the right), the notches' lengths and gap to the rim, the figures' height and gap, how many numbers at most,
    the needle (where it starts and ends, from the pivot; half width; the slot's half width)."""

    def __init__(self, size, cx, cy, radius, a0, a1, major, minor, gap, figure_h, figure_gap, most, needle):
        self.size, self.cx, self.cy, self.radius, self.a0, self.a1 = size, cx, cy, radius, a0, a1
        self.major, self.minor, self.gap = major, minor, gap
        self.figure_h, self.figure_gap, self.most = figure_h, figure_gap, most
        self.pointer = needle
        self.icon_size = 22 if size[0] < 300 else 30
        if a0 > a1:  # over the pivot: the icon just above the pivot, "x1000" above it
            self.marks = ((cx, cy - radius * 0.55), (cx, cy - radius * 0.27))
        else:  # under the pivot: inside the arc's middle, "x1000" just above the numbers
            self.marks = (self.point(0.5, radius * 0.78), self.point(0.5, radius * 0.62))

    def angle(self, t):
        return self.a0 + (self.a1 - self.a0) * t

    def point(self, t, r):
        a = self.angle(t)
        return self.cx + r * math.cos(a), self.cy - r * math.sin(a)

    def face(self, scale, icon=None):
        lo, hi, red, marks = scale
        w, h = self.size
        white, reds = Mask(w, h), Mask(w, h)
        red_from = hi - 1000 if red else None
        t_of = lambda v: (v - lo) / (hi - lo)  # noqa: E731
        hot = lambda v: red_from is not None and v >= red_from - 1e-9  # noqa: E731
        # the rim: one line all the way, turning in at both ends
        t_red = t_of(red_from) if red_from is not None else 1.0
        steps = 120
        rim = [self.point(i / steps, self.radius) for i in range(steps + 1)]
        cut = int(round(t_red * steps))
        white.polyline(rim[: cut + 1], 1.1)
        reds.polyline(rim[cut:], 1.1)
        turn = self.major * 0.9
        white.polyline([self.point(0, self.radius), self.point(0, self.radius - turn)], 1.1)
        (reds if red_from is not None else white).polyline(
            [self.point(1, self.radius), self.point(1, self.radius - turn)], 1.1
        )

        div = divisor(lo, hi)
        fh = self.figure_h
        inner = self.radius - self.gap - self.major  # where the long notches end

        def place(v):
            """The figure's centre: its box clear of the long notch's inner end, whatever the angle."""
            a = self.angle(t_of(v))
            tw = text_width(figure(v, div), fh)
            r = inner - self.figure_gap - abs(math.cos(a)) * tw / 2 - abs(math.sin(a)) * fh / 2
            x, y = self.cx + r * math.cos(a), self.cy - r * math.sin(a)
            if abs(math.sin(a)) < 0.2:  # beside a level notch a figure reads as negative ("-40"): above it instead
                y = min(y, self.cy - r * math.sin(a) - fh / 2 - 4)
            return x, y, tw

        for most in range(self.most, 1, -1):  # as many numbers as fit without two touching
            numbered, minor = (list(marks), 1.0) if marks else ticks(lo, hi, most)
            boxes = [place(v) for v in numbered]
            if marks or all(
                abs(xa - xb) >= (wa + wb) / 2 + 4 or abs(ya - yb) >= fh + 3
                for (xa, ya, wa), (xb, yb, wb) in zip(boxes, boxes[1:])
            ):
                break
        majors = {round(v, 9) for v in numbered}
        for v in minors(lo, hi, minor):
            if round(v, 9) in majors or t_of(v) < 1e-6 or t_of(v) > 1 - 1e-6:
                continue  # the ends are the rim's turns
            (xa, ya), (xb, yb) = (
                self.point(t_of(v), self.radius - self.gap),
                self.point(t_of(v), self.radius - self.gap - self.minor),
            )
            (reds if hot(v) else white).stroke(xa, ya, xb, yb, 0.8)
        for v, (fx, fy, _) in zip(numbered, boxes):
            m = reds if hot(v) else white
            if 1e-6 < t_of(v) < 1 - 1e-6:
                (xa, ya), (xb, yb) = self.point(t_of(v), self.radius - self.gap), self.point(t_of(v), inner)
                m.stroke(xa, ya, xb, yb, 1.1)
            m.text(figure(v, div), fx, fy, fh)
        if div != 1:
            white.text("x1000", *self.marks[0], fh * 0.7)
        draw_icon(white, icon, *self.marks[1], self.icon_size)
        return picture(w, h, [(white, WHITE), (reds, RED)])

    def needle_masks(self, levels):
        """Per level, the needle (F87 M2): a long blade with a dark slot along its middle, no hub."""
        w, h = self.size
        start, tip, half, slot = self.pointer
        out = []
        for level in range(levels):
            t = level / (levels - 1)
            blade, groove = Mask(w, h), Mask(w, h)
            base, waist, end = self.point(t, start), self.point(t, start + (tip - start) * 0.8), self.point(t, tip)
            blade.stroke(base[0], base[1], waist[0], waist[1], half, half * 0.8)
            blade.stroke(waist[0], waist[1], end[0], end[1], half * 0.8, 0.3)
            a, b = self.point(t, start + (tip - start) * 0.12), self.point(t, start + (tip - start) * 0.72)
            groove.stroke(a[0], a[1], b[0], b[1], slot, slot * 0.7)
            out.append((blade, groove))
        return out

    def needle(self, masks, colour):
        blade, groove = masks
        return picture(*self.size, [(blade, colour), (groove, tuple(int(c * 0.35) for c in colour))])


D = math.radians
# Half circle over the pivot: the hub at (94.5, 100) / (193, 128), the text under it.
SMALL_DIAL = Dial((189, 180), 94.5, 100, 76, math.pi, 0, 9, 5, 3, 13, 3, 5, (-4, 72, 2.4, 0.7))
WIDE_DIAL = Dial((386, 180), 193, 128, 116, math.pi, 0, 13, 7, 4, 18, 4, 8, (-6, 110, 3.4, 1.0))
# The fuel gauge's arc under its pivot (out of the picture, above): a short needle near the rim, the text above.
SMALL_ARC = Dial((189, 180), 94.5, -40, 160, D(240), D(300), 9, 5, 3, 13, 3, 4, (112, 158, 2.4, 0.7))
WIDE_ARC = Dial((386, 180), 193, -150, 300, D(234), D(306), 13, 7, 4, 18, 4, 6, (250, 298, 3.4, 1.0))


# ---- bars (the M5's) ------------------------------------------------------------------------------------------------
class Bar:
    """A wedge bar of `count` slanted segments, length long, thickness thin -> thick along it, with notches and numbers
    beside it. Drawn along u (0 .. long) and across v (0 at the wedge's straight edge, growing away from the scale);
    to_xy maps (u, v) to the picture."""

    def __init__(self, size, long, thin, thick, count, to_xy, notch_at, figure_at, anchors, icon_at=None):
        self.size, self.long, self.thin, self.thick, self.count = size, long, thin, thick, count
        self.to_xy, self.notch_at, self.figure_at, self.anchors = to_xy, notch_at, figure_at, anchors
        self.icon_at = icon_at  # the icon's centre: top left, before the gauge's name, as the M5 shows it

    def thickness(self, u):
        return self.thin + (self.thick - self.thin) * u / self.long

    def segment(self, k, upto=None):
        """Segment k's corners, cut at u <= upto when given (the value's part of it)."""
        gap = 3.0
        size = (self.long - gap * (self.count - 1)) / self.count
        a, b = k * (size + gap), k * (size + gap) + size
        lean = 0.6  # the cuts lean: the far edge's corner moves on by lean * thickness

        def edge(u):
            return [(u, 0.0), (u + lean * self.thickness(u), self.thickness(u))]

        (a0, a1), (b0, b1) = edge(a), edge(b)
        pts = [a0, b0, b1, a1]
        if upto is not None:  # clip to u <= upto (Sutherland-Hodgman against one line)
            out = []
            for p, q in zip(pts, pts[1:] + pts[:1]):
                pin, qin = p[0] <= upto, q[0] <= upto
                if pin:
                    out.append(p)
                if pin != qin:
                    t = (upto - p[0]) / (q[0] - p[0])
                    out.append((upto, p[1] + t * (q[1] - p[1])))
            pts = out
        return [self.to_xy(u, v) for u, v in pts]

    def cuts(self):
        gap = 3.0
        size = (self.long - gap * (self.count - 1)) / self.count
        return [k * (size + gap) - gap / 2 for k in range(1, self.count)]

    def track(self, scale, icon=None, decimals=None):
        """The back picture: every segment in the track's grey (the last red for a rev counter), the notches and the
        numbers at the start, middle and end."""
        lo, hi, red, marks = scale
        w, h = self.size
        grey, white, reds = Mask(w, h), Mask(w, h), Mask(w, h)
        if marks:
            decimals = 0  # the grades: "92 95 98"
        for k in range(self.count):
            (reds if red and k == self.count - 1 else grey).polygon(self.segment(k))
        for u in [0.0] + self.cuts() + [self.long]:
            (xa, ya), (xb, yb) = self.notch_at(u)
            white.stroke(xa, ya, xb, yb, 0.8)
        div = divisor(lo, hi)
        if decimals is None:  # as few as write the three numbers exactly; small ranges "0.0 1.0 2.0", as BMW
            three = [v / div for v in (lo, (lo + hi) / 2, hi)]
            decimals = next((d for d in (0, 1) if all(abs(v * 10**d - round(v * 10**d)) < 1e-6 for v in three)), 2)
            decimals = max(decimals, 1) if (hi - lo) / div < 10 else decimals
        for frac, anchor in zip((0.0, 0.5, 1.0), self.anchors):
            v = lo + (hi - lo) * frac
            x, y = self.figure_at(frac * self.long)
            (reds if red and frac == 1.0 else white).text(figure(v, div, decimals), x, y, 13, anchor)
        if self.icon_at:
            draw_icon(white, icon, *self.icon_at, 22)
        return picture(w, h, [(grey, TRACK), (white, WHITE), (reds, RED)])

    def value(self, level, levels, colour, box=None):
        """The front picture: the segments up to the level, in colour."""
        w, h = self.size
        m = Mask(w, h)
        upto = self.long * level / (levels - 1)
        for k in range(self.count):
            if level > 0:
                pts = self.segment(k, upto)
                if len(pts) >= 3:
                    m.polygon(pts)
        full = picture(w, h, [(m, colour)])
        return crop(full, box) if box else full


# The wide and tall bars' pictures cover their two cells, so the icon can stand at the top left before the name.
# Wide (386 x 180): the wedge from x 13, its straight edge at y 127; notches and numbers under it.
WIDE_BAR = Bar(
    (386, 180),
    360,
    6,
    14,
    6,
    lambda u, v: (u + 13, 127 - v),
    lambda u: ((u + 13, 129), (u + 10.5, 134)),
    lambda u: (u + 14, 145),
    ("left", "centre", "right"),
    (24, 28),
)
# Tall (189 x 368): the wedge upwards from y 354, its straight edge at x 56 facing the notches and numbers right of it.
TALL_BAR = Bar(
    (189, 368),
    240,
    6,
    14,
    6,
    lambda u, v: (56 - v, 354 - u),
    lambda u: ((58, 354 - u), (63, 354 - u + 1.5)),
    lambda u: (68, 354 - u),
    ("left", "left", "left"),
    (24, 28),
)


def card_icon(name):
    """A card's icon, 22 x 22, before its name."""
    m = Mask(22, 22)
    draw_icon(m, name, 11, 11, 22)
    return picture(22, 22, [(m, WHITE)])


# The bars' front pictures hold only the wedge (x, y, w, h in the cell): wide and tall.
WIDE_VALUE_BOX = (12, 112, 372, 17)
TALL_VALUE_BOX = (40, 104, 18, 252)
NEEDLE = (232, 40, 32)  # every needle red, as the F87 M2's: the colour setting is for the bars


# A card's bar (165 x 12) and the shift light's (760 x 40): the wedge only, track and value in one picture per colour
# and level.
CARD_BAR = Bar((165, 12), 158, 4, 10, 6, lambda u, v: (u + 1, 11 - v), None, None, None)
SHIFT_BAR = Bar((760, 40), 740, 12, 34, 16, lambda u, v: (u + 1, 37 - v), None, None, None)


def whole_bar(bar, level, levels, colour):
    w, h = bar.size
    grey, value = Mask(w, h), Mask(w, h)
    upto = bar.long * level / (levels - 1)
    for k in range(bar.count):
        grey.polygon(bar.segment(k))
        if level > 0:
            pts = bar.segment(k, upto)
            if len(pts) >= 3:
                value.polygon(pts)
    return picture(w, h, [(grey, TRACK), (value, colour)])
