"""Numeric scan of captured frames for dark-on-bright sprite faults.

For every frame PNG in a run directory this reports:
  * mean luminance
  * the count and bounding box of near-black pixels (all channels <= 24)
  * the count of near-black pixels inside the lower-central play area
The bounding box is what distinguishes a black fighter (a compact, sprite sized
blob on the fight line) from a dark backdrop (which spans the frame).
"""
import glob
import os
import sys

from PIL import Image

run = sys.argv[1] if len(sys.argv) > 1 else r"C:\RockyKombat\Saved\Evidence\blackwatch\20261017_203348"
files = sorted(glob.glob(os.path.join(run, "frame_*.png")))
print("frames:", len(files))

for path in files:
    im = Image.open(path).convert("RGB")
    w, h = im.size
    px = im.load()
    dark = 0
    minx, miny, maxx, maxy = w, h, -1, -1
    total = 0
    lum_sum = 0
    step = 2
    for y in range(0, h, step):
        for x in range(0, w, step):
            r, g, b = px[x, y]
            lum = (r * 299 + g * 587 + b * 114) // 1000
            total += 1
            lum_sum += lum
            if r <= 24 and g <= 24 and b <= 24:
                dark += 1
                if x < minx:
                    minx = x
                if y < miny:
                    miny = y
                if x > maxx:
                    maxx = x
                if y > maxy:
                    maxy = y
    mean = lum_sum / max(total, 1)
    frac = 100.0 * dark / max(total, 1)
    box = "none" if maxx < 0 else "{},{} - {},{}".format(minx, miny, maxx, maxy)
    print("{:22s} {:4d}x{:4d} mean={:6.1f} dark={:6d} ({:5.2f}%) box={}".format(
        os.path.basename(path), w, h, mean, dark, frac, box))
