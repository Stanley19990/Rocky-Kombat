#!/usr/bin/env python3
"""rkc_floorline_check.py

Answers two framing questions with measurements instead of impressions:

  1) Where is the ring in the supplied artwork?  The rope bundle is the most
     saturated red/magenta horizontal feature in the image, so the rows with the
     strongest red dominance locate it.  The rows where the dark wet floor starts
     (below the skyline) locate the floor's back edge.

  2) How is the artwork mapped into the captured frame, and where do the
     fighters' feet sit relative to the artwork's own floor?

The game sizes the backdrop from a constant (ArenaFloorLineUpFraction), not from
the artwork's pixels, so comparing the two shows whether that constant matches
where the artist actually painted the standing area.

Usage: python Tools/Diagnostics/rkc_floorline_check.py <artwork.png> <shot.png>
"""

import sys

from PIL import Image


def redness_rows(image, band=1):
    """Mean (R - max(G,B)) per row: the rope is the reddest thing in the frame."""
    small = image.convert("RGB")
    w, h = small.size
    px = small.load()
    rows = []
    for y in range(h):
        acc = 0.0
        for x in range(0, w, 2):
            r, g, b = px[x, y]
            acc += r - (g if g > b else b)
        rows.append(acc / (w / 2.0))
    if band > 1:
        rows = [max(rows[max(0, i - band):i + band + 1]) for i in range(len(rows))]
    return rows


def brightest_rows(rows, count, min_gap):
    order = sorted(range(len(rows)), key=lambda i: -rows[i])
    picked = []
    for i in order:
        if all(abs(i - p) >= min_gap for p in picked):
            picked.append(i)
        if len(picked) >= count:
            break
    return sorted(picked)


def floor_start_row(image):
    """First row, scanning down, where the image turns into low-contrast dark
    floor and stays that way for a long run: the back edge of the ring floor."""
    gray = image.convert("L")
    w, h = gray.size
    px = gray.load()
    run_needed = max(20, h // 12)
    for y in range(int(h * 0.30), h - run_needed):
        dark = 0
        for x in range(0, w, 4):
            if px[x, y] < 95:
                dark += 1
        if dark / float(w / 4.0) > 0.90:
            ok = True
            for yy in range(y, min(h, y + run_needed), 4):
                cnt = 0
                for x in range(0, w, 4):
                    if px[x, yy] < 120:
                        cnt += 1
                if cnt / float(w / 4.0) < 0.85:
                    ok = False
                    break
            if ok:
                return y, h
    return None, h


def report(path, label):
    image = Image.open(path).convert("RGB")
    w, h = image.size
    rows = redness_rows(image)
    ropes = brightest_rows(rows, 6, max(8, h // 40))
    floor_y, _ = floor_start_row(image)
    print("%s  %dx%d" % (label, w, h))
    print("  reddest rows (candidate rope bundle), as %% down the image:")
    for y in ropes:
        print("     y=%-5d  %.1f%%   redness %.1f" % (y, 100.0 * y / h, rows[y]))
    if floor_y is not None:
        print("  floor back edge around y=%d  (%.1f%% down)" % (floor_y, 100.0 * floor_y / h))
    else:
        print("  floor back edge: not detected")
    print()
    return {"w": w, "h": h, "ropes": ropes, "floor": floor_y}


if __name__ == "__main__":
    art = report(sys.argv[1], "ARTWORK")
    shot = report(sys.argv[2], "SHOT   ")

    # Where the artwork's rope rows would land if the artwork were fitted to the
    # shot's frame, versus where the shot's rope rows actually are.
    art_rope = art["ropes"][:3]
    shot_rope = shot["ropes"][:3]
    for ay in art_rope:
        fitted = ay * (shot["h"] / float(art["h"]))
        print("artwork rope row %d (%.1f%% down) fits to shot row %.0f (%.1f%% down)"
              % (ay, 100.0 * ay / art["h"], fitted, 100.0 * fitted / shot["h"]))
    print("shot rope rows: %s" % ", ".join("%d (%.1f%% down)" % (y, 100.0 * y / shot["h"])
                                           for y in shot_rope))
    if art["floor"] and shot["floor"]:
        print("artwork floor back edge %.1f%% down -> shot %.1f%% down"
              % (100.0 * art["floor"] / art["h"], 100.0 * shot["floor"] / shot["h"]))
