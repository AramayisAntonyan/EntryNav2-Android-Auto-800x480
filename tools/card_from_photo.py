#!/usr/bin/env python3
# Copyright (c) 2026 Jiraiya43
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Developer tool, needs Pillow: python3 tools/card_from_photo.py <photo> .local/gauges_card_549x484.png
# A PNG already cut out (real transparency) is only darkened and fitted; a photo on a white background is cut:
#   1. the backdrop: near-pure white flood-filled from the edges (strict, so the headlights' white and grey stay);
#   2. the floor shadow: grey pixels below the car's lowest point in each column (paint, lights or black trims and
#      tyres), kept as soft black;
#   3. the car darkened to sit with the iDrive's own car pictures; cropped and fitted into the top 549x330.
import sys
from collections import deque
from PIL import Image

src, dst = sys.argv[1], sys.argv[2]
DARKEN = 0.80
src_im = Image.open(src)
if src_im.mode == "RGBA" and src_im.getchannel("A").getextrema()[0] == 0:
    # Already cut out (real transparency): only darkened and fitted.
    cut = src_im.copy()
    r, g, b, a = cut.split()
    cut = Image.merge("RGBA", [c.point(lambda v: int(v * DARKEN)) for c in (r, g, b)] + [a])
    cut = cut.crop(a.point(lambda v: 255 if v > 24 else 0).getbbox())
    scale = min((549 - 16) / cut.width, 330 / cut.height)
    cut = cut.resize((max(1, int(cut.width * scale)), max(1, int(cut.height * scale))), Image.LANCZOS)
    card = Image.new("RGBA", (549, 484), (0, 0, 0, 0))
    card.alpha_composite(cut, ((549 - cut.width) // 2, 10 + (330 - cut.height) // 2))
    card.save(dst, optimize=True)
    print("cutout", cut.size, "->", dst)
    sys.exit(0)
im = src_im.convert("RGB")
w, h = im.size
px = im.load()

def white(p):
    r, g, b = p
    return min(r, g, b) >= 246 and max(r, g, b) - min(r, g, b) <= 6

back = bytearray(w * h)
q = deque((x, y) for x in range(w) for y in (0, h - 1))
q.extend((x, y) for y in range(h) for x in (0, w - 1))
while q:
    x, y = q.popleft()
    if x < 0 or y < 0 or x >= w or y >= h:
        continue
    i = y * w + x
    if back[i] or not white(px[x, y]):
        continue
    back[i] = 1
    q.extend(((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)))

def car_pixel(p):
    r, g, b = p
    return max(r, g, b) - min(r, g, b) > 28 or (r + g + b) / 3 < 60

lowest = [-1] * w
for x in range(w):
    for y in range(h - 1, -1, -1):
        if not back[y * w + x] and car_pixel(px[x, y]):
            lowest[x] = y
            break

out = Image.new("RGBA", (w, h))
op = out.load()
for y in range(h):
    for x in range(w):
        r, g, b = px[x, y]
        i = y * w + x
        if back[i]:
            op[x, y] = (0, 0, 0, 0)
        elif lowest[x] < 0 or y > lowest[x]:
            lum = (r + g + b) / 3  # the floor shadow under the car: soft black
            op[x, y] = (0, 0, 0, max(0, min(255, int((246 - lum) * 2.0))))
        else:
            op[x, y] = (int(r * DARKEN), int(g * DARKEN), int(b * DARKEN), 255)
alpha = out.getchannel("A").point(lambda v: 255 if v > 24 else 0)
car = out.crop(alpha.getbbox())
box_w, box_h = 549 - 16, 330
scale = min(box_w / car.width, box_h / car.height)
car = car.resize((max(1, int(car.width * scale)), max(1, int(car.height * scale))), Image.LANCZOS)
card = Image.new("RGBA", (549, 484), (0, 0, 0, 0))
card.alpha_composite(car, ((549 - car.width) // 2, 10 + (box_h - car.height) // 2))
card.save(dst, optimize=True)
print("backdrop", sum(back), "of", w * h, "pixels; car", car.size, "->", dst)
