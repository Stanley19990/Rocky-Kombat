"""rkc_nameplate_probe.py - what colour does a given region of a shipped frame render?

WHY THIS EXISTS
`ARKCFighter::NameText` used to be a UTextRenderComponent whose text is
"<name>\\n<division>" and whose colour was set per roster: gold (255,196,120) for
Rocky, light blue (150,172,255) for Shadow. In shipped frames the text read as
near-black on a dark floor, so this tool measured the region instead of trusting
an impression, and that measurement is what justified removing the component on
2026-10-20 (near-black share fell from 49.83% to 17.31% once it was gone; see
docs/06-known-issues-and-remaining-work.md section 6). The component no longer
exists, so the tool is now a general 'what is actually in this rectangle'
measurement - useful for checking that a region is floor and not glyphs:


  * the darkest and brightest pixel in the region, with their coordinates;
  * how many pixels are close to the two colours the code asks for;
  * the mean colour of the darkest tenth of the region (the glyph body).

Usage:
    python rkc_nameplate_probe.py <image.png> <x0> <y0> <x1> <y1>
"""
from __future__ import annotations

import sys

from PIL import Image


def main() -> int:
    path, x0, y0, x1, y1 = sys.argv[1], *[int(v) for v in sys.argv[2:6]]
    img = Image.open(path).convert("RGB")
    w, h = img.size
    x0, y0 = max(0, x0), max(0, y0)
    x1, y1 = min(w, x1), min(h, y1)

    px = img.load()
    pixels = []
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b = px[x, y]
            lum = 0.2126 * r + 0.7152 * g + 0.0722 * b
            pixels.append((lum, r, g, b, x, y))

    if not pixels:
        print("EMPTY REGION")
        return 2

    pixels.sort()
    n = len(pixels)
    dark = pixels[: max(1, n // 10)]

    def close(rgb, target, tol=40):
        return all(abs(c - t) <= tol for c, t in zip(rgb, target))

    GOLD = (255, 196, 120)
    BLUE = (150, 172, 255)
    gold_n = sum(1 for p in pixels if close((p[1], p[2], p[3]), GOLD))
    blue_n = sum(1 for p in pixels if close((p[1], p[2], p[3]), BLUE))
    near_black = sum(1 for p in pixels if p[0] < 12.0)

    dmean = tuple(round(sum(p[i] for p in dark) / len(dark), 1) for i in (1, 2, 3))

    print(f"IMAGE {path}  region ({x0},{y0})-({x1},{y1})  {x1 - x0}x{y1 - y0} px, {n} sampled")
    lo = pixels[0]
    hi = pixels[-1]
    print(f"DARKEST   rgb {lo[1]},{lo[2]},{lo[3]}  lum {lo[0]:.1f}  at ({lo[4]},{lo[5]})")
    print(f"BRIGHTEST rgb {hi[1]},{hi[2]},{hi[3]}  lum {hi[0]:.1f}  at ({hi[4]},{hi[5]})")
    print(f"MEAN OF DARKEST 10%  rgb {dmean[0]},{dmean[1]},{dmean[2]}")
    print(f"pixels within 40 of gold {GOLD}: {gold_n} ({100.0 * gold_n / n:.2f}%)")
    print(f"pixels within 40 of blue {BLUE}: {blue_n} ({100.0 * blue_n / n:.2f}%)")
    print(f"near-black pixels (lum<12): {near_black} ({100.0 * near_black / n:.2f}%)")

    gold_verdict = "PRESENT" if gold_n > 0 else "ABSENT"
    print(f"VERDICT: requested gold colour is {gold_verdict} in this region; "
          f"near-black share {100.0 * near_black / n:.1f}%")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
