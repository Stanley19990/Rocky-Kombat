"""rkc_arena_text_check.py - does the supplied arena artwork contain baked text?

WHY THIS EXISTS
An earlier note in docs/06 claimed the arena PNG carried a faint, repeated
"MIDDLEWEIGHT" wordmark tiled across the picture. A later investigation showed
the visible "ROCKY / MIDDLEWEIGHT" text in shipped frames came from the fighter
component ARKCFighter::NameText, not from the image. This tool settles what the
image itself contains by looking at it away from the fighters:

  * crops a wide band of the arena floor and writes it out for inspection;
  * reports how much high-frequency contrast the band carries, which is what
    lettering looks like in an otherwise smooth painted floor.

Usage:
    python rkc_arena_text_check.py <arena.png> [out.png]
"""
from __future__ import annotations

import sys

from PIL import Image, ImageFilter


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: rkc_arena_text_check.py <arena.png> [out.png]")
        return 2
    src = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else "arena_floor_band.png"

    img = Image.open(src).convert("RGB")
    w, h = img.size
    print(f"IMAGE {src}  {w}x{h}")

    # Lower half of the picture: the painted floor, away from the skyline.
    band = img.crop((0, int(h * 0.55), w, int(h * 0.95)))
    band.save(out)
    print(f"wrote {out} ({band.size[0]}x{band.size[1]})")

    # High-pass the band: any baked lettering survives as sharp edges, a smooth
    # painted floor does not. Report the share of pixels above a fixed threshold.
    edges = band.convert("L").filter(ImageFilter.FIND_EDGES)
    px = edges.load()
    strong = 0
    total = edges.size[0] * edges.size[1]
    for y in range(edges.size[1]):
        for x in range(edges.size[0]):
            if px[x, y] > 60:
                strong += 1
    print(f"strong edge pixels: {strong} of {total} ({100.0 * strong / total:.2f}%)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
