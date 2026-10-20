#!/usr/bin/env python3
"""
rkc_alpha_verify.py - Cross-check the alpha measurement with an independent PNG
decoder (Pillow) so the masked vs translucent decision is not resting on a single
hand-rolled decoder. Also dumps an ASCII alpha silhouette for visual confirmation.
"""

import json
import os
import sys

PACK = r"C:\RockyKombat\ArtSource\RockyKombatPack_v1.0"
OUT = r"C:\RockyKombat\Saved\Evidence\Alpha_verify.json"

try:
    from PIL import Image
    HAVE_PIL = True
except Exception as exc:  # pragma: no cover
    HAVE_PIL = False
    PIL_ERR = repr(exc)

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def collect_frames():
    frames = []
    for root, _d, files in os.walk(os.path.join(PACK, "Characters")):
        for fn in sorted(files):
            if fn.lower().endswith(".png"):
                frames.append(os.path.relpath(os.path.join(root, fn), PACK).replace("\\", "/"))
    return sorted(set(frames))


def main():
    if not HAVE_PIL:
        print("Pillow unavailable: %s" % PIL_ERR)
        return 2

    frames = collect_frames()
    hist = [0] * 16
    vis = 0
    opaque = 0
    per_frame = []
    sizes = {}
    modes = {}

    for rel in frames:
        path = os.path.join(PACK, rel.replace("/", os.sep))
        im = Image.open(path)
        sizes[im.size] = sizes.get(im.size, 0) + 1
        modes[im.mode] = modes.get(im.mode, 0) + 1
        a = im.getchannel("A") if "A" in im.getbands() else None
        if a is None:
            per_frame.append({"file": rel, "error": "no alpha band", "mode": im.mode})
            continue
        h = a.histogram()
        n_vis = sum(h) - h[0]
        vis += n_vis
        opaque += h[255]
        for v in range(1, 256):
            hist[v >> 4] += h[v]
        per_frame.append({
            "file": rel, "mode": im.mode, "size": list(im.size),
            "visible": n_vis, "opaque255": h[255],
            "opaque_pct": round(100.0 * h[255] / n_vis, 3) if n_vis else 0.0,
            "below_26": sum(h[1:26]),
            "below_85": sum(h[1:85]),
        })

    report = {
        "pack": PACK,
        "decoder": "Pillow %s" % getattr(Image, "__version__", "?"),
        "frames": len(frames),
        "unique_sizes": {str(k): v for k, v in sizes.items()},
        "unique_modes": modes,
        "visible_px": vis,
        "opaque255": opaque,
        "opaque_pct_of_visible": round(100.0 * opaque / vis, 3) if vis else 0,
        "hist_pct": [round(100.0 * v / vis, 3) if vis else 0.0 for v in hist],
        "cut_at_alpha26_pct": round(100.0 * sum(hist[0:2]) / vis, 3) if vis else 0,
        "cut_at_alpha85_pct": round(100.0 * sum(hist[0:6]) / vis, 3) if vis else 0,
        "per_frame": per_frame,
        "pil_version": getattr(__import__("PIL"), "__version__", "?"),
    }

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=1)

    print("=" * 68)
    print("PILLOW CROSS-CHECK  (%d frames)" % len(frames))
    print("=" * 68)
    print("sizes : %s" % report["unique_sizes"])
    print("modes : %s" % report["unique_modes"])
    print("visible px        : %d" % vis)
    print("alpha==255        : %d  (%.3f%%)" % (opaque, report["opaque_pct_of_visible"]))
    for i, pct in enumerate(report["hist_pct"]):
        lo = i * 16
        hi = lo + 15 if i < 15 else 255
        print("  a=%3d-%3d %7.3f%% %s" % (lo, hi, pct, "#" * int(pct / 2)))
    print("cut at alpha<26   : %.3f%% of visible" % report["cut_at_alpha26_pct"])
    print("cut at alpha<85   : %.3f%% of visible" % report["cut_at_alpha85_pct"])
    print("report: %s" % OUT)

    # ASCII silhouette of one representative frame
    sample = "Characters/Rocky/Idle/Rocky_Idle_00.png"
    im = Image.open(os.path.join(PACK, sample.replace("/", os.sep))).convert("RGBA")
    w, h = im.size
    a = im.getchannel("A")
    px = a.load()
    cols, rows = 48, 32
    print()
    print("silhouette: %s (%dx%d) downsampled to %dx%d" % (sample, w, h, cols, rows))
    print("  ' '=0  '.'=<64  ':'=<160  '+'=<224  '#'=>=224")
    footy = None
    for ry in range(rows):
        line = []
        y0 = ry * h // rows
        y1 = max(y0 + 1, (ry + 1) * h // rows)
        for rx in range(cols):
            x0 = rx * w // cols
            x1 = max(x0 + 1, (rx + 1) * w // cols)
            m = 0
            for y in range(y0, y1):
                for x in range(x0, x1):
                    v = px[x, y]
                    if v > m:
                        m = v
            line.append(" " if m == 0 else "." if m < 64 else ":" if m < 160 else "+" if m < 224 else "#")
        print("  %3d %s" % (y0, "".join(line)))
    # locate lowest non-empty row overall (foot line) on full res
    last = -1
    for y in range(h - 1, -1, -1):
        rowmax = max(px[x, y] for x in range(w))
        if rowmax > 25:
            last = y
            break
    print("  lowest row with alpha>25 : y=%d" % last)
    return 0


if __name__ == "__main__":
    sys.exit(main())
