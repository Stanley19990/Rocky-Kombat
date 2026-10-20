"""Crop the HUD text area of a stage capture and emit rotated copies.

Purpose: decide, from pixels rather than from an impression, whether the HUD
glyphs are rotated. If the crop is readable only after a 90 degree rotation,
the text is drawn rotated and the layout itself is not at fault.

Usage: python rkc_text_orientation_check.py <png> [x0 y0 x1 y1] [outdir]
"""
import sys
import os
from PIL import Image

png = sys.argv[1]
box = tuple(int(v) for v in sys.argv[2:6]) if len(sys.argv) >= 6 else (0, 0, 430, 140)
outdir = sys.argv[6] if len(sys.argv) >= 7 else os.path.join(os.path.dirname(png), "_textcheck")

os.makedirs(outdir, exist_ok=True)
img = Image.open(png)
crop = img.crop(box)
scale = 3
big = crop.resize((crop.width * scale, crop.height * scale), Image.NEAREST)

base = os.path.splitext(os.path.basename(png))[0]
big.save(os.path.join(outdir, base + "_crop.png"))
big.rotate(-90, expand=True).save(os.path.join(outdir, base + "_rot90cw.png"))
big.rotate(90, expand=True).save(os.path.join(outdir, base + "_rot90ccw.png"))
big.rotate(180, expand=True).save(os.path.join(outdir, base + "_rot180.png"))

print("image      :", png, img.size)
print("crop box   :", box, "->", crop.size, "scaled x", scale)
print("written to :", outdir)
for name in ("_crop", "_rot90cw", "_rot90ccw", "_rot180"):
    print("  ", base + name + ".png")
