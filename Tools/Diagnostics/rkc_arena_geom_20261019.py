#!/usr/bin/env python3
"""Task-local measurement: where is the ring painted inside the arena artwork?

Reports, for the supplied Midnight Rooftop Ring image:
  * image size and aspect
  * per-column top-of-bright-pixel profile (finds the corner posts)
  * the painted floor line (the y where the canvas surface starts)
  * the horizontal centre of the painted ring, as a fraction of image width

Read-only. Not part of the deliverable.
"""
import sys

from PIL import Image

SRC = sys.argv[1] if len(sys.argv) > 1 else (
    "ArtSource/RockyKombatPack_v1.0/Arena/Midnight_Rooftop_Ring.png"
)

im = Image.open(SRC).convert("RGB")
W, H = im.size
px = im.load()
print("image %dx%d aspect %.4f" % (W, H, W / float(H)))

# --- per-column top-of-bright-pixel profile ---------------------------------
# The posts are the tallest bright structures; the ropes are bright too but sit
# lower. A column's "height" is H - (first y with a bright pixel).
prof = []
for x in range(0, W, 4):
    top = None
    for y in range(0, H, 2):
        r, g, b = px[x, y]
        if max(r, g, b) > 165:
            top = y
            break
    prof.append((x, H - top if top is not None else 0))

# Print a coarse profile so the shape of the ring is visible as numbers.
print("\ncol  height_from_bottom")
for x, h in prof:
    if x % 80 == 0:
        print("%4d  %5d %s" % (x, h, "#" * (h // 12)))

tall = [x for x, h in prof if h > 0.55 * H]
if tall:
    print("\ncolumns with a tall structure (>55%% of image height): %d..%d of 0..%d"
          % (min(tall), max(tall), W))
    print("leftmost tall column  %.3f of width" % (min(tall) / float(W)))
    print("rightmost tall column %.3f of width" % (max(tall) / float(W)))
    print("centre of tall span   %.3f of width" % ((min(tall) + max(tall)) / 2.0 / W))

# --- painted floor line -----------------------------------------------------
# Walk each row and measure how "warm/light" it is; the canvas surface reads as
# a lighter, more saturated band than the dark stage floor below the apron.
print("\nrow  mean_rgb  frac_warm(>80)")
for y in range(0, H, 20):
    rs = gs = bs = 0
    warm = 0
    n = 0
    for x in range(0, W, 6):
        r, g, b = px[x, y]
        rs += r
        gs += g
        bs += b
        if r > 80:
            warm += 1
        n += 1
    print("%4d  %5.1f %5.1f %5.1f   %.3f" % (y, rs / float(n), gs / float(n),
                                             bs / float(n), warm / float(n)))
