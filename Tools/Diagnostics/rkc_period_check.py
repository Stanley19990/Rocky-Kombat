"""rkc_period_check.py

Decides whether an image is a MOSAIC (the same panel repeated in a grid) by looking
for a repeating period in its row/column brightness profiles.

A mosaic of N cells across shows a strong autocorrelation peak at the cell pitch and
at its multiples. Ordinary artwork - even busy artwork - does not.

Usage: python Tools/Diagnostics/rkc_period_check.py <image.png> [more.png ...]
"""

import sys
import os

from PIL import Image, ImageStat


def profile(image, axis):
    """Mean luma per column (axis 0) or per row (axis 1)."""
    gray = image.convert("L")
    w, h = gray.size
    px = gray.load()
    out = []
    if axis == 0:
        for x in range(w):
            s = 0
            for y in range(h):
                s += px[x, y]
            out.append(s / float(h))
    else:
        for y in range(h):
            s = 0
            for x in range(w):
                s += px[x, y]
            out.append(s / float(w))
    return out


def autocorr_peaks(series, min_lag, max_lag):
    n = len(series)
    mean = sum(series) / float(n)
    dev = [v - mean for v in series]
    var = sum(v * v for v in dev)
    if var <= 0:
        return []
    peaks = []
    for lag in range(min_lag, min(max_lag, n // 2)):
        s = 0.0
        for i in range(n - lag):
            s += dev[i] * dev[i + lag]
        r = (s / (n - lag)) / (var / n)
        peaks.append((r, lag))
    peaks.sort(reverse=True)
    return peaks


def report(path):
    image = Image.open(path).convert("RGB")
    w, h = image.size
    print("%s  %dx%d" % (os.path.basename(path), w, h))

    for axis, name in ((0, "columns (horizontal pitch)"), (1, "rows (vertical pitch)")):
        series = profile(image, axis)
        peaks = autocorr_peaks(series, 16, max(32, len(series) // 3))
        if not peaks:
            print("  %s: no variation to test" % name)
            continue
        top = peaks[:3]
        shown = "  ".join("r=%.2f at %d px" % (r, lag) for r, lag in top)
        print("  %s: top autocorrelation %s" % (name, shown))
        best_r, best_lag = top[0]
        if best_r >= 0.5 and best_lag >= 24:
            print("     -> repeating structure with a %.0f px pitch (mosaic-like)" % best_lag)
        else:
            print("     -> no strong repeating pitch (ordinary artwork)")


def main():
    if len(sys.argv) < 2:
        print("usage: rkc_period_check.py <image.png> [more.png ...]")
        return 2
    for p in sys.argv[1:]:
        report(p)
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
