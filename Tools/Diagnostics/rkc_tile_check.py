"""rkc_tile_check.py

Answers one question about a match screenshot that looks like a grid of repeated
panels: is the repetition in the SUPPLIED ARENA ARTWORK, or is the game drawing the
same HUD block many times across the screen?

Method (no guessing, two independent measurements):

  1) Artwork residual - resize the source backdrop image to the shot's size and
     subtract it from the shot. If the repetition is baked into the artwork, the
     residual is small and contains only what the game added on top (fighters, HUD,
     ring ropes drawn in 3D). If the game were tiling its own canvas, the residual
     would carry the repetition.

  2) Tile self-similarity - cut the shot and the artwork into an 8x4 grid of cells
     and measure how strongly every cell correlates with the top-left cell. High
     correlation in the ARTWORK and high correlation in the SHOT means the shot is
     faithfully showing a tiled artwork. High correlation only in the shot would
     indicate the game itself repeated content.

Usage: python Tools/Diagnostics/rkc_tile_check.py <shot.png> [artwork.png]
Writes an amplified residual next to the shot as <shot>_residual.png.
"""

import sys
import os

from PIL import Image, ImageChops, ImageStat

GRID_X, GRID_Y = 8, 4


def cell_correlations(image, grid_x=GRID_X, grid_y=GRID_Y):
    """Mean absolute difference between each cell and the top-left cell, plus a
    cheap correlation proxy: the fraction of cells whose mean luma is within 5%."""
    w, h = image.size
    cw, ch = w // grid_x, h // grid_y
    ref = image.crop((0, 0, cw, ch)).convert("L")
    ref_stat = ImageStat.Stat(ref)
    ref_mean = ref_stat.mean[0]
    rows = []
    for gy in range(grid_y):
        row = []
        for gx in range(grid_x):
            cell = image.crop((gx * cw, gy * ch, (gx + 1) * cw, (gy + 1) * ch)).convert("L")
            diff = ImageChops.difference(ref, cell)
            mad = ImageStat.Stat(diff).mean[0]
            mean = ImageStat.Stat(cell).mean[0]
            within = abs(mean - ref_mean) <= max(5.0, 0.05 * max(ref_mean, 1.0))
            row.append((mad, within))
        rows.append(row)
    flat = [c for row in rows for c in row]
    first = flat[0]
    others = flat[1:]
    mean_mad = sum(c[0] for c in others) / len(others)
    frac_similar_mean = sum(1 for c in others if c[1]) / len(others)
    return first, mean_mad, frac_similar_mean, rows


def main():
    if len(sys.argv) < 2:
        print("usage: rkc_tile_check.py <shot.png> [artwork.png]")
        return 2

    shot_path = sys.argv[1]
    shot = Image.open(shot_path).convert("RGB")
    print("shot:    %s  %dx%d" % (os.path.basename(shot_path), shot.size[0], shot.size[1]))

    ref, mm, frac, _ = cell_correlations(shot)
    print()
    print("SHOT tile self-similarity vs its own top-left cell:")
    print("  mean |difference| between cells: %.2f of 255" % mm)
    print("  cells whose mean luma is within tolerance of the top-left cell: %.0f%%" % (100.0 * frac))

    if len(sys.argv) >= 3:
        art_path = sys.argv[2]
        art = Image.open(art_path).convert("RGB")
        art_scaled = art.resize(shot.size, Image.LANCZOS)
        print()
        print("artwork: %s  %dx%d (scaled to the shot for comparison)"
              % (os.path.basename(art_path), art.size[0], art.size[1]))

        aref, amm, afrac, _ = cell_correlations(art_scaled)
        print("ARTWORK tile self-similarity vs its own top-left cell:")
        print("  mean |difference| between cells: %.2f of 255" % amm)
        print("  cells whose mean luma is within tolerance of the top-left cell: %.0f%%" % (100.0 * afrac))

        diff = ImageChops.difference(shot, art_scaled)
        stat = ImageStat.Stat(diff)
        mean_rgb = stat.mean
        print()
        print("shot minus artwork (whole frame):")
        print("  mean |difference| per channel: R %.2f  G %.2f  B %.2f" % tuple(mean_rgb))
        bright = ImageChops.lighter(diff, diff)
        scaled = bright.point(lambda v: min(255, v * 4))
        out = os.path.splitext(shot_path)[0] + "_residual.png"
        scaled.save(out)
        print("  amplified residual (x4) written: %s" % out)

        verdict = []
        if afrac >= 0.75 and frac >= 0.5:
            verdict.append("the repetition is present in the supplied artwork and the shot shows it")
        if frac >= 0.75 and afrac < 0.5:
            verdict.append("the SHOT repeats content that the artwork does not: the game is drawing it")
        if not verdict:
            verdict.append("inconclusive from these two numbers alone; inspect the residual image")
        print()
        print("reading: " + "; ".join(verdict))
    return 0


if __name__ == "__main__":
    sys.exit(main())
