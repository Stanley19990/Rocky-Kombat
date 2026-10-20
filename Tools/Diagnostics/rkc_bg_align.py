#!/usr/bin/env python3
"""rkc_bg_align.py

Finds how the supplied arena artwork was actually mapped into a captured frame,
by sliding the artwork vertically over the frame and scoring the match on a
narrow column strip that contains as little game content as possible.

Once the mapping is known, the question "are the fighters standing where the
artist painted the ring floor, or below it, in the foreground?" becomes a number:
the feet's row in the FRAME converts to a row in the ARTWORK, and that row can be
compared with where the ring's ropes and floor actually are in the artwork.

Usage: python Tools/Diagnostics/rkc_bg_align.py <artwork.png> <shot.png>
"""

import sys

from PIL import Image, ImageStat


def strip(image, x_frac0, x_frac1, y0, y1):
    w, h = image.size
    return image.crop((int(w * x_frac0), y0, int(w * x_frac1), y1)).convert("L")


def main():
    art = Image.open(sys.argv[1]).convert("RGB")
    shot = Image.open(sys.argv[2]).convert("RGB")
    W, H = shot.size
    art_fit = art.resize((W, H), Image.LANCZOS)

    # Use a strip near the left edge: the HUD keeps its plates away from there in
    # the match layout, so the pixels are almost pure backdrop.
    a = strip(art_fit, 0.02, 0.12, 0, H)
    s = strip(shot, 0.02, 0.12, 0, H)
    ap, sp = a.load(), s.load()
    aw, ah = a.size

    best = []
    for dy in range(-160, 161, 2):
        tot = 0
        n = 0
        for y in range(0, ah, 3):
            sy = y + dy
            if 0 <= sy < ah:
                for x in range(0, aw, 3):
                    tot += abs(ap[x, y] - sp[x, sy])
                    n += 1
        if n:
            best.append((tot / float(n), dy))
    best.sort()

    print("artwork %dx%d fitted to frame %dx%d" % (art.size[0], art.size[1], W, H))
    print("best vertical offsets of the artwork over the frame (mean |diff|, offset):")
    for mad, dy in best[:5]:
        print("   offset %+d px  mean|diff| %.2f   (artwork row r appears at frame row r%+d)"
              % (dy, mad, dy))

    mad, dy = best[0]
    print()
    print("adopted: artwork row r -> frame row r%+d" % dy)

    # Feet row: measured from the earlier locate pass, but recompute here from the
    # fighter's own shadow-free bottom by looking for the last strongly orange row
    # in each half.
    def feet_row(x0, x1):
        spx = shot.load()
        last = None
        for y in range(H - 1, 0, -1):
            cnt = 0
            for x in range(x0, x1):
                r, g, b = spx[x, y]
                if r > 140 and b < 110 and (r - g) > 45 and (g - b) > 15:
                    cnt += 1
            if cnt > 4:
                last = y
                break
        return last

    left_feet = feet_row(200, 420)
    right_feet = feet_row(880, 1100)
    for label, fy in (("left fighter", left_feet), ("right fighter", right_feet)):
        if fy is None:
            print("%s: feet not found" % label)
            continue
        art_row = fy - dy
        print("%s: feet at frame row %d (%.1f%% down) = artwork row %d (%.1f%% down of %d)"
              % (label, fy, 100.0 * fy / H, art_row, 100.0 * art_row / H, H))
    print()
    print("Reference rows measured in the artwork itself (1672x941):")
    print("   ring ropes (reddest) around 48.6%, 52.0%, 54.7% down")
    print("   so an artwork row below ~60% is foreground floor OUTSIDE the ring,")
    print("   and rows ~50-58% are the ring's own canvas area.")


if __name__ == "__main__":
    main()
