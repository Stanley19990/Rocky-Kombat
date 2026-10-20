#!/usr/bin/env python3
"""rkc_frame_diff_precise.py

Locates exactly WHERE two captured frames differ, instead of only reporting a
global "changed N of M pixels". The earlier probe used a whole-frame comparison
that iterated over zero pixels (its output read "changed 0/0"), so a real menu
highlight move could have been missed.

Reports, for each pair:
  - total changed pixels and their bounding box
  - the same restricted to named regions (menu rows / sprite band / HUD)

Usage:
  python rkc_frame_diff_precise.py <base.png> <cmp.png> [<cmp2.png> ...]
"""

import sys

import numpy as np
from PIL import Image

# Regions of the 1280x720 front end. Rows are the item list; the fighter
# sprites stand left and right of it.
REGIONS = [
    ("title band", 0, 0, 1280, 100),
    ("menu rows", 390, 240, 890, 380),
    ("left sprite (Shadow)", 240, 270, 470, 520),
    ("right sprite (Rocky)", 890, 270, 1110, 520),
    ("footer band", 0, 650, 1280, 720),
    ("whole frame", 0, 0, 1280, 720),
]

# A pixel counts as changed only if it moves by more than this in any channel,
# so PNG/encode noise does not masquerade as content.
THRESHOLD = 8


CROP = None


def load(path):
    img = Image.open(path).convert("RGB")
    if CROP is not None:
        img = img.crop(CROP)
    return np.asarray(img).astype(np.int16)


def report(base, cmp_path):
    a = load(base)
    b = load(cmp_path)
    if a.shape != b.shape:
        print(f"  shape mismatch {a.shape} vs {b.shape} - cannot compare")
        return

    delta = np.abs(a - b)
    changed = (delta.max(axis=2) > THRESHOLD)

    total = int(changed.sum())
    print(f"  vs {cmp_path}")
    print(f"    full-frame changed: {total} px ({100.0 * total / changed.size:.3f}%)")
    if total:
        ys, xs = np.nonzero(changed)
        print(f"    bbox of change: x {xs.min()}..{xs.max()}  y {ys.min()}..{ys.max()}")

    for name, x0, y0, x1, y1 in REGIONS:
        sub = changed[y0:y1, x0:x1]
        n = int(sub.sum())
        area = sub.size
        if n:
            ys, xs = np.nonzero(sub)
            extra = (f" bbox x {x0 + xs.min()}..{x0 + xs.max()}"
                     f" y {y0 + ys.min()}..{y0 + ys.max()}")
        else:
            extra = ""
        print(f"    {name:22s} {n:6d} / {area:6d} px ({100.0 * n / area:6.3f}%){extra}")


def main():
    global CROP
    args = sys.argv[1:]
    # An optional leading  x0,y0,x1,y1  crops every frame before comparing, so a
    # full-desktop grab can be reduced to the game window rectangle.
    if args and "," in args[0]:
        x0, y0, x1, y1 = (int(v) for v in args[0].split(","))
        CROP = (x0, y0, x1, y1)
        print(f"crop: ({x0},{y0})-({x1},{y1})")
        args = args[1:]
    if len(args) < 2:
        print(__doc__)
        return 1
    base = args[0]
    for cmp_path in args[1:]:
        report(base, cmp_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
