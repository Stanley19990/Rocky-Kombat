#!/usr/bin/env python3
"""rkc_pixel_probe.py

Prints the colour of five fixed probe points in a captured frame, so the reading
can be compared against an independent decoder (Tools/Diagnostics/rkc_probe_decode.ps1
reads the same five points through .NET). Two decoders agreeing on the same pixels
is what turns "the picture looks odd" into either "the file really says that" or
"the viewer is at fault".

Usage: python Tools/Diagnostics/rkc_pixel_probe.py <shot.png>
"""

import sys

from PIL import Image

path = sys.argv[1]
img = Image.open(path)
rgb = img.convert("RGB")
px = rgb.load()
print("%s  mode=%s size=%s" % (path, img.mode, img.size))
for x, y in ((300, 330), (700, 300), (975, 330), (640, 200), (100, 600)):
    r, g, b = px[x, y]
    print("pixel (%d,%d) = %d,%d,%d" % (x, y, r, g, b))
