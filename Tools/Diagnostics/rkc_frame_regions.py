#!/usr/bin/env python3
"""rkc_frame_regions.py

Finds where things actually are in a captured match frame, without guessing at
coordinates:

  * clusters of saturated red pixels (the fighters' trunks)
  * the brightest saturated warm cluster (skin/gloves)
  * a coarse 8x8 average-luminance map of the frame

It also prints the PNG's real mode/bit depth, so a decode surprise can be told
apart from a rendering problem.

Usage: python Tools/Diagnostics/rkc_frame_regions.py <shot.png>
"""

import sys
from collections import deque

from PIL import Image

path = sys.argv[1]
img = Image.open(path)
print("%s  mode=%s size=%s info=%s" % (path, img.mode, img.size,
                                       {k: img.info[k] for k in list(img.info)[:6]}))

rgb = img.convert("RGB")
w, h = rgb.size
px = rgb.load()


def is_trunk(r, g, b):
    # The trunks are a saturated red/magenta: clearly more red than green and blue.
    return r > 90 and r > g + 45 and r > b + 30


def is_warm_bright(r, g, b):
    # Skin, gloves and the lit side of a fighter.
    return r > 140 and g > 90 and r >= g and g >= b - 10


def clusters(test, min_pixels=60):
    seen = [[False] * w for _ in range(0)]
    seen = [[False] * w for _ in range(h)]
    boxes = []
    for y0 in range(0, h, 2):
        for x0 in range(0, w, 2):
            if seen[y0][x0]:
                continue
            r, g, b = px[x0, y0]
            if not test(r, g, b):
                continue
            q = deque([(x0, y0)])
            seen[y0][x0] = True
            n = 0
            xmin, xmax, ymin, ymax = x0, x0, y0, y0
            while q:
                x, y = q.popleft()
                n += 1
                xmin, xmax = min(xmin, x), max(xmax, x)
                ymin, ymax = min(ymin, y), max(ymax, y)
                for dx, dy in ((2, 0), (-2, 0), (0, 2), (0, -2)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < w and 0 <= ny < h and not seen[ny][nx]:
                        r2, g2, b2 = px[nx, ny]
                        if test(r2, g2, b2):
                            seen[ny][nx] = True
                            q.append((nx, ny))
            if n >= min_pixels:
                boxes.append((n, xmin, ymin, xmax, ymax))
    boxes.sort(reverse=True)
    return boxes


print("\ntrunk-coloured clusters (pixels, x0,y0 -> x1,y1):")
for n, x0, y0, x1, y1 in clusters(is_trunk)[:6]:
    print("  %6d  (%4d,%4d) -> (%4d,%4d)   centre (%4d,%4d)  %dx%d"
          % (n, x0, y0, x1, y1, (x0 + x1) // 2, (y0 + y1) // 2, x1 - x0, y1 - y0))

print("\nbright-warm clusters (pixels, x0,y0 -> x1,y1):")
for n, x0, y0, x1, y1 in clusters(is_warm_bright, min_pixels=120)[:6]:
    print("  %6d  (%4d,%4d) -> (%4d,%4d)   centre (%4d,%4d)  %dx%d"
          % (n, x0, y0, x1, y1, (x0 + x1) // 2, (y0 + y1) // 2, x1 - x0, y1 - y0))

print("\n8x8 average luminance map (rows top to bottom):")
for gy in range(8):
    row = []
    for gx in range(8):
        s = 0
        c = 0
        for y in range(gy * h // 8, (gy + 1) * h // 8, 3):
            for x in range(gx * w // 8, (gx + 1) * w // 8, 3):
                r, g, b = px[x, y]
                s += (r * 299 + g * 587 + b * 114) // 1000
                c += 1
        row.append("%3d" % (s // max(c, 1)))
    print("  " + " ".join(row))
