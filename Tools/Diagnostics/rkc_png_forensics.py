#!/usr/bin/env python3
"""rkc_png_forensics.py

Objective check of a captured frame, independent of any image viewer:

  * PNG container facts (bit depth, colour type, interlace, filters)
  * unique colour count and luminance histogram
  * horizontal / vertical self-similarity, to detect a repeating tile
    (a HUD glyph atlas smeared over the frame repeats at a fixed period,
    a genuinely rendered 3D scene does not)
  * a viewer-safe downscaled copy (RGB PNG and JPEG) so the picture can be
    inspected without relying on the decoder that may have mangled it

Usage: python Tools/Diagnostics/rkc_png_forensics.py <shot.png> [more.png ...]
"""

import os
import struct
import sys

try:
    from PIL import Image
except ImportError:
    print("PIL (pillow) is required: pip install pillow")
    raise SystemExit(2)


def png_header(path):
    with open(path, "rb") as fh:
        head = fh.read(33)
    if head[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    width, height, depth, ctype, comp, filt, interlace = struct.unpack(">IIBBBBB", head[16:29])
    colour = {0: "grey", 2: "RGB", 3: "palette", 4: "grey+alpha", 6: "RGBA"}.get(ctype, "?%d" % ctype)
    # first IHDR-adjacent chunk tells us the filter byte of row 0
    return dict(width=width, height=height, depth=depth, colour=colour,
                interlace=interlace, filters=filt)


def period_score(values, maxlag):
    """Normalised correlation of a 1-D signal with itself at each lag."""
    n = len(values)
    mean = sum(values) / n
    var = sum((v - mean) ** 2 for v in values)
    if var <= 0:
        return []
    out = []
    for lag in range(8, maxlag):
        num = 0.0
        for i in range(n - lag):
            num += (values[i] - mean) * (values[i + lag] - mean)
        out.append((lag, num / var))
    return out


def analyse(path):
    info = png_header(path)
    print("=" * 72)
    print(path)
    if info:
        print("  PNG: %dx%d depth=%d colour=%s interlace=%d"
              % (info["width"], info["height"], info["depth"], info["colour"], info["interlace"]))
    else:
        print("  not a PNG header (re-encoded copy?)")

    img = Image.open(path)
    print("  decoder: mode=%s size=%s" % (img.mode, img.size))
    rgb = img.convert("RGB")
    w, h = rgb.size
    px = rgb.load()

    colours = rgb.getcolors(maxcolors=1 << 24)
    print("  unique colours: %d" % (len(colours) if colours else -1))
    if colours:
        top = sorted(colours, reverse=True)[:6]
        print("  most common: " + ", ".join("#%02x%02x%02x x%d" % (c[1][0], c[1][1], c[1][2], c[0]) for c in top))

    # row / column fingerprints, coarsely averaged so compression noise is not
    # mistaken for structure
    row_mean = []
    for y in range(h):
        s = 0
        for x in range(0, w, 4):
            r, g, b = px[x, y]
            s += (r * 299 + g * 587 + b * 114) // 1000
        row_mean.append(s / len(range(0, w, 4)))

    col_mean = []
    for x in range(w):
        s = 0
        for y in range(0, h, 4):
            r, g, b = px[x, y]
            s += (r * 299 + g * 587 + b * 114) // 1000
        col_mean.append(s / len(range(0, h, 4)))

    for name, signal, limit in (("vertical (row) repeat", row_mean, min(h // 2, 200)),
                                ("horizontal (col) repeat", col_mean, min(w // 2, 200))):
        scores = period_score(signal, limit)
        best = sorted(scores, key=lambda t: -t[1])[:4]
        print("  %s: strongest lags %s"
              % (name, ", ".join("lag %d r=%.2f" % t for t in best)))
        strong = [t for t in scores if t[1] > 0.60]
        print("      lags with correlation >0.60: %d %s"
              % (len(strong), [t[0] for t in strong[:12]]))

    lum_black = 0
    lum_dark = 0
    for y in range(0, h, 2):
        for x in range(0, w, 2):
            r, g, b = px[x, y]
            lum = (r * 299 + g * 587 + b * 114) // 1000
            if lum <= 10:
                lum_black += 1
            elif lum <= 32:
                lum_dark += 1
    total = (h // 2) * (w // 2)
    print("  near-black(<=10) %.1f%%   dark(11..32) %.1f%%"
          % (100.0 * lum_black / total, 100.0 * lum_dark / total))

    base = os.path.splitext(os.path.basename(path))[0]
    outdir = os.path.join(os.path.dirname(path), "forensics")
    os.makedirs(outdir, exist_ok=True)
    small = rgb.resize((w // 2, h // 2), Image.LANCZOS)
    png_out = os.path.join(outdir, base + "_half_rgb.png")
    jpg_out = os.path.join(outdir, base + "_half.jpg")
    small.save(png_out)
    small.save(jpg_out, quality=92)
    print("  wrote %s" % png_out)
    print("  wrote %s" % jpg_out)
    print()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        raise SystemExit(2)
    for p in sys.argv[1:]:
        analyse(p)
