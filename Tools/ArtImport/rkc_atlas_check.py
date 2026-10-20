#!/usr/bin/env python3
"""
rkc_atlas_check.py - Determine whether the two supplied 4x8 atlases are usable.

The manifest advertises Characters/<Name>/<Name>_Atlas_4x8.png as a 4 cols x 8 rows
sheet of 384x320 tiles (1536x2560). The frame-level PNGs carry a real alpha matte,
so the atlas must carry the same matte if it is usable as an import source or as a
cross-check. This script measures RGB and alpha content tile by tile.
"""

import json
import os
import sys

from PIL import Image, ImageChops

PACK = r"C:\RockyKombat\ArtSource\RockyKombatPack_v1.0"
OUT = r"C:\RockyKombat\Saved\Evidence\Atlas_check.json"

ATLASES = [
    "Characters/Rocky/Rocky_Atlas_4x8.png",
    "Characters/Shadow/Shadow_Atlas_4x8.png",
]
TILE = (384, 320)
GRID = (4, 8)


def analyse(path):
    im = Image.open(path)
    rec = {"file": os.path.relpath(path, PACK).replace("\\", "/"),
           "mode": im.mode, "size": list(im.size), "expected_size": [TILE[0] * GRID[0], TILE[1] * GRID[1]]}
    rgb = im.convert("RGB")
    rec["size_matches_grid"] = list(im.size) == rec["expected_size"]

    has_alpha = "A" in im.getbands()
    rec["has_alpha_channel"] = has_alpha

    # RGB content: how many pixels are not pure black / differ from the corner pixel
    hist_rgb = rgb.convert("L").histogram()
    nonblack = sum(hist_rgb[1:])
    rec["nonblack_rgb_px"] = nonblack
    rec["nonblack_rgb_pct"] = round(100.0 * nonblack / (im.size[0] * im.size[1]), 3)
    rec["corner_rgb"] = list(rgb.getpixel((0, 0)))

    if has_alpha:
        a = im.getchannel("A")
        h = a.histogram()
        rec["alpha_zero_pct"] = round(100.0 * h[0] / (im.size[0] * im.size[1]), 3)
        rec["alpha_255"] = h[255]
        rec["alpha_nonzero_px"] = sum(h) - h[0]
        rec["alpha_mean"] = round(sum(i * c for i, c in enumerate(h)) / (im.size[0] * im.size[1]), 3)

        # per-tile visible-pixel counts
        tiles = []
        cols, rows = GRID
        for r in range(rows):
            row = []
            for c in range(cols):
                box = (c * TILE[0], r * TILE[1], (c + 1) * TILE[0], (r + 1) * TILE[1])
                ta = a.crop(box)
                th = ta.histogram()
                row.append(sum(th) - th[0])
            tiles.append(row)
        rec["tile_visible_px"] = tiles
        rec["tiles_with_content"] = sum(1 for row in tiles for v in row if v > 0)
        rec["tiles_required"] = cols * rows

        # does the atlas tile match the standalone frame at the same grid slot?
        # manifest order: Idle, Walk, Jab, HeavyPunch, Block, Dodge, Knockdown, Victory
        order = ["Idle", "Walk", "Jab", "HeavyPunch", "Block", "Dodge", "Knockdown", "Victory"]
        name = os.path.basename(path).split("_")[0]
        cmp_rows = []
        for r, action in enumerate(order):
            for c in range(cols):
                box = (c * TILE[0], r * TILE[1], (c + 1) * TILE[0], (r + 1) * TILE[1])
                tile = im.convert("RGBA").crop(box)
                fpath = os.path.join(PACK, "Characters", name, action, "%s_%s_%02d.png" % (name, action, c))
                if not os.path.exists(fpath):
                    cmp_rows.append({"action": action, "frame": c, "error": "frame png missing"})
                    continue
                fr = Image.open(fpath).convert("RGBA")
                diff = ImageChops.difference(tile, fr)
                bbox = diff.getbbox()
                # max absolute channel difference
                mx = max(diff.getchannel(ch).getextrema()[1] for ch in ("R", "G", "B", "A"))
                cmp_rows.append({"action": action, "frame": c,
                                 "identical": bbox is None, "max_channel_delta": mx})
        rec["atlas_vs_frame_png"] = cmp_rows
        rec["atlas_matches_frames"] = all(x.get("identical") for x in cmp_rows if "identical" in x)
    return rec


def main():
    out = {"atlases": []}
    for rel in ATLASES:
        p = os.path.join(PACK, rel.replace("/", os.sep))
        if not os.path.exists(p):
            out["atlases"].append({"file": rel, "error": "missing"})
            continue
        out["atlases"].append(analyse(p))

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(out, f, indent=1)

    for rec in out["atlases"]:
        print("=" * 68)
        print(rec.get("file"), "->", rec.get("error", "ok"))
        if "error" in rec:
            continue
        print("  mode %s  size %s  matches 4x8x384x320: %s" % (rec["mode"], rec["size"], rec["size_matches_grid"]))
        print("  has alpha channel      : %s" % rec["has_alpha_channel"])
        print("  nonblack RGB px        : %d (%.2f%%)  corner=%s" % (rec["nonblack_rgb_px"], rec["nonblack_rgb_pct"], rec["corner_rgb"]))
        if rec["has_alpha_channel"]:
            print("  alpha==0 pct           : %.2f%%   alpha==255 count: %d   mean alpha: %.2f" % (rec["alpha_zero_pct"], rec["alpha_255"], rec["alpha_mean"]))
            print("  tiles with content     : %d / %d" % (rec["tiles_with_content"], rec["tiles_required"]))
            for i, row in enumerate(rec["tile_visible_px"]):
                print("    row %d: %s" % (i, " ".join("%6d" % v for v in row)))
            print("  atlas tile == frame png: %s" % rec["atlas_matches_frames"])
            bad = [x for x in rec["atlas_vs_frame_png"] if not x.get("identical")]
            if bad:
                print("  mismatching slots      : %d (first: %s)" % (len(bad), bad[0]))
    print("=" * 68)
    print("report: %s" % OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
