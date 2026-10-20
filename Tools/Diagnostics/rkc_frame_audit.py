#!/usr/bin/env python3
"""rkc_frame_audit.py

Audits the SUPPLIED art frames for content that should not be there: baked-in
opaque black regions and baked-in text-like bands. The character frames are drawn
straight onto the arena, so anything baked into a frame appears in the game
exactly as it appears here.

For each image it reports:
  - canvas size and the bounding box of the non-transparent pixels (the part that
    will actually be drawn);
  - the colour composition INSIDE that bounding box, including the share of
    near-black opaque pixels;
  - the widest horizontal near-black run per row band, so a baked black panel can
    be located rather than inferred.

Usage: python Tools/Diagnostics/rkc_frame_audit.py <image.png> [more.png ...]
"""

import sys

from PIL import Image


def audit(path):
    image = Image.open(path)
    mode = image.mode
    rgba = image.convert("RGBA")
    w, h = rgba.size
    px = rgba.load()

    minx, miny, maxx, maxy = w, h, -1, -1
    opaque = 0
    black = 0
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a <= 10:
                continue
            opaque += 1
            if minx > x: minx = x
            if miny > y: miny = y
            if maxx < x: maxx = x
            if maxy < y: maxy = y
            if r <= 12 and g <= 12 and b <= 12:
                black += 1

    print("%s  mode=%s  %dx%d" % (path.split("/")[-1], mode, w, h))
    if opaque == 0:
        print("   fully transparent")
        return
    print("   opaque pixels %d (%.1f%% of canvas)" % (opaque, 100.0 * opaque / (w * h)))
    print("   drawn bounding box x %d..%d  y %d..%d  (%dx%d)"
          % (minx, maxx, miny, maxy, maxx - minx + 1, maxy - miny + 1))
    print("   near-black opaque pixels %d (%.1f%% of the drawn area)"
          % (black, 100.0 * black / opaque))

    # Widest near-black run per 10% row band, inside the bounding box.
    bands = 10
    band_h = max(1, (maxy - miny + 1) // bands)
    worst = []
    for b_i in range(bands):
        y0 = miny + b_i * band_h
        y1 = min(maxy + 1, y0 + band_h)
        best = 0
        for y in range(y0, y1):
            run = 0
            best_row = 0
            for x in range(minx, maxx + 1):
                r, g, b_, a = px[x, y]
                if a > 10 and r <= 12 and g <= 12 and b_ <= 12:
                    run += 1
                    best_row = max(best_row, run)
                else:
                    run = 0
            best = max(best, best_row)
        if best > (maxx - minx) * 0.15:
            worst.append((b_i * 10, best))
    if worst:
        print("   row bands with a wide near-black run (band %%, widest run px):")
        for pct, run in worst:
            print("      %3d%% down the drawn area: %d px" % (pct, run))
    print()


if __name__ == "__main__":
    for p in sys.argv[1:]:
        audit(p)
