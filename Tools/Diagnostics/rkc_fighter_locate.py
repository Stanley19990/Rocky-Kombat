#!/usr/bin/env python3
"""rkc_fighter_locate.py

Measures, in a captured match frame:
  - where each fighter actually is (by its fur/glove colours), so "are they
    centred?", "are they at the bottom?", "do they drift to one end?" become
    numbers rather than impressions;
  - the pure-black regions and their bounding boxes, so the black bands seen at
    the fighters' feet can be attributed to something specific.

Usage: python Tools/Diagnostics/rkc_fighter_locate.py <shot.png> [more.png ...]
"""

import sys

from PIL import Image

# Orange tabby fur: strong red, mid green, low blue.
def is_fur(r, g, b):
    return r > 140 and b < 110 and (r - g) > 45 and (g - b) > 15

# Red glove / trunks: very red, low green and blue.
def is_glove(r, g, b):
    return r > 120 and g < 70 and b < 70 and (r - g) > 70


def scan(path):
    image = Image.open(path).convert("RGB")
    w, h = image.size
    px = image.load()

    col_fur = [0] * w
    col_glove = [0] * w
    fur_pts = []
    glove_pts = []
    black_cols = [0] * w
    black_rows = [0] * h
    black_pts = 0

    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            if is_fur(r, g, b):
                col_fur[x] += 1
                fur_pts.append((x, y))
            elif is_glove(r, g, b):
                col_glove[x] += 1
                glove_pts.append((x, y))
            lum = (r * 299 + g * 587 + b * 114) // 1000
            if lum <= 10:
                black_cols[x] += 1
                black_rows[y] += 1
                black_pts += 1

    # Cluster the fur columns into fighters (a fighter is a contiguous run of
    # columns with fur, separated from the other by an empty gap).
    occupied = [c > 2 for c in col_fur]
    clusters = []
    start = None
    for x in range(w):
        if occupied[x] and start is None:
            start = x
        elif not occupied[x] and start is not None:
            if x - start > 8:
                clusters.append((start, x - 1))
            start = None
    if start is not None and w - start > 8:
        clusters.append((start, w - 1))

    print("%s  %dx%d" % (path.split("/")[-1], w, h))
    print("  fur pixels %d, glove pixels %d, near-black pixels %d (%.1f%%)"
          % (len(fur_pts), len(glove_pts), black_pts, 100.0 * black_pts / (w * h)))

    for i, (x0, x1) in enumerate(clusters):
        ys = [p[1] for p in fur_pts if x0 <= p[0] <= x1]
        cx = (x0 + x1) / 2.0
        gy = [p[1] for p in glove_pts if x0 - 20 <= p[0] <= x1 + 20]
        print("  fighter %d: fur columns %d-%d  centre x=%.0f  (%.1f%% across, screen centre is %.0f%%)"
              % (i + 1, x0, x1, cx, 100.0 * cx / w, 50.0))
        print("              fur vertical span y=%d..%d  (feet at %.1f%% down)"
              % (min(ys), max(ys), 100.0 * max(ys) / h))
        if gy:
            print("              gloves y=%d..%d" % (min(gy), max(gy)))

    # Black bands: rows where a large contiguous run of black exists.
    print("  heaviest near-black rows:")
    rows = sorted(range(h), key=lambda y: -black_rows[y])[:6]
    for y in sorted(rows):
        if black_rows[y] > w * 0.05:
            runs = []
            run = 0
            best = 0
            best_start = 0
            cur_start = 0
            for x in range(w):
                if black_cols[x] > 0 and px[x, y][0] <= 10 and px[x, y][1] <= 10 and px[x, y][2] <= 10:
                    if run == 0:
                        cur_start = x
                    run += 1
                    if run > best:
                        best = run
                        best_start = cur_start
                else:
                    run = 0
            print("     y=%-4d black px=%-5d longest horizontal black run=%d px at x=%d..%d"
                  % (y, black_rows[y], best, best_start, best_start + best - 1))
    print()


if __name__ == "__main__":
    for p in sys.argv[1:]:
        scan(p)
