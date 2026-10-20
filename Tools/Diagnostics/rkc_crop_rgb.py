#!/usr/bin/env python3
"""rkc_crop_rgb.py

Extracts a rectangle from a captured frame and writes it as a plain 8-bit RGB
PNG, optionally upscaled with nearest-neighbour.

Why: the game writes RGBA screenshots that some image viewers decode badly
(tiled/smeared output). Crops re-encoded to plain RGB render correctly.

Usage:
  python Tools/Diagnostics/rkc_crop_rgb.py <src.png> <x0> <y0> <x1> <y1> <out.png> [scale]
"""

import sys

from PIL import Image

src, x0, y0, x1, y1, out = sys.argv[1:7]
scale = int(sys.argv[7]) if len(sys.argv) > 7 else 1

img = Image.open(src).convert("RGB")
w, h = img.size
x0, y0, x1, y1 = max(0, int(x0)), max(0, int(y0)), min(w, int(x1)), min(h, int(y1))
crop = img.crop((x0, y0, x1, y1))
if scale > 1:
    crop = crop.resize((crop.width * scale, crop.height * scale), Image.NEAREST)

crop.save(out)

# small numeric summary so the crop can be compared without eyeballing it
px = crop.load()
n = crop.width * crop.height
lum_sum = 0
warm = 0
for y in range(crop.height):
    for x in range(crop.width):
        r, g, b = px[x, y]
        lum_sum += (r * 299 + g * 587 + b * 114) // 1000
        if r > g + 20 and r > b + 20:
            warm += 1

print("%s  crop %dx%d from (%d,%d) scale %d" % (out, crop.width, crop.height, x0, y0, scale))
print("  mean luminance %.1f   warm (r>g,b) pixel share %.1f%%" % (1.0 * lum_sum / n, 100.0 * warm / n))
