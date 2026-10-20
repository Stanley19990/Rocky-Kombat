"""rkc_frame_analyze.py - read-only frame inspector for the "fighters turn black" report.

Modes:
  map   <png> [cols]      ASCII luminance map of one frame (layout reconnaissance)
  stats <png...>          per-frame luminance / colour / edge statistics
  scan  <dir> [--roi x0,y0,x1,y1]
                          flag outlier frames inside a capture burst

The capture burst is the evidence; this only measures it. Nothing is mutated.
"""

import argparse
import glob
import os
import sys

import numpy as np
from PIL import Image

RAMP = " .:-=+*#%@"


def load(path):
    img = Image.open(path).convert("RGB")
    return np.asarray(img, dtype=np.float32)


def luma(rgb):
    return 0.2126 * rgb[:, :, 0] + 0.7152 * rgb[:, :, 1] + 0.0722 * rgb[:, :, 2]


def downsample(values, cols):
    h, w = values.shape
    rows = max(1, int(round(cols * h / w * 0.5)))
    ys = np.linspace(0, h, rows + 1).astype(int)
    xs = np.linspace(0, w, cols + 1).astype(int)
    out = np.zeros((rows, cols), dtype=np.float32)
    for j in range(rows):
        for i in range(cols):
            block = values[ys[j]:ys[j + 1], xs[i]:xs[i + 1]]
            out[j, i] = block.mean() if block.size else 0.0
    return out


def cmd_map(args):
    rgb = load(args.png)
    L = luma(rgb)
    small = downsample(L, args.cols)
    lo, hi = float(small.min()), float(small.max())
    span = max(1e-6, hi - lo)
    print(f"{os.path.basename(args.png)}  {rgb.shape[1]}x{rgb.shape[0]}  luma {L.mean():.1f} (min {lo:.0f} max {hi:.0f})")
    for j in range(small.shape[0]):
        row = ""
        for i in range(small.shape[1]):
            t = (small[j, i] - lo) / span
            row += RAMP[min(len(RAMP) - 1, int(t * len(RAMP)))]
        print("|" + row + "|")


def roi_stats(rgb, roi):
    L = luma(rgb)
    if roi:
        x0, y0, x1, y1 = roi
        L = L[y0:y1, x0:x1]
        rgb = rgb[y0:y1, x0:x1]
    gx = np.abs(np.diff(L, axis=1)).mean()
    gy = np.abs(np.diff(L, axis=0)).mean()
    chroma = (rgb.max(axis=2) - rgb.min(axis=2)).mean()
    return {
        "mean": float(L.mean()),
        "std": float(L.std()),
        "edge": float((gx + gy) / 2.0),
        "chroma": float(chroma),
        "dark_frac": float((L < 16).mean()),
    }


def cmd_stats(args):
    roi = None
    if args.roi:
        roi = tuple(int(v) for v in args.roi.split(","))
    print("file,mean,std,edge,chroma,dark_frac")
    for path in args.pngs:
        s = roi_stats(load(path), roi)
        print(f"{os.path.basename(path)},{s['mean']:.2f},{s['std']:.2f},{s['edge']:.2f},"
              f"{s['chroma']:.2f},{s['dark_frac']:.4f}")


def cmd_scan(args):
    roi = tuple(int(v) for v in args.roi.split(",")) if args.roi else None
    files = sorted(glob.glob(os.path.join(args.dir, "frame_*.png")))
    if not files:
        print(f"NO_FRAMES in {args.dir}")
        return 1
    rows = []
    for path in files:
        rgb = load(path)
        s = roi_stats(rgb, roi)
        s["file"] = os.path.basename(path)
        rows.append(s)
    arr = {k: np.array([r[k] for r in rows]) for k in ("mean", "std", "edge", "chroma")}
    print(f"frames: {len(rows)}   roi: {roi if roi else 'full frame'}")
    for k, v in arr.items():
        print(f"{k:8s} median={np.median(v):8.2f} min={v.min():8.2f} max={v.max():8.2f} p05={np.percentile(v,5):8.2f}")
    # A fighter that renders black loses both contrast and edge energy inside its box.
    med_edge = np.median(arr["edge"])
    med_std = np.median(arr["std"])
    flagged = [r for r in rows
               if r["edge"] < med_edge * 0.55 and r["std"] < med_std * 0.55]
    print(f"flagged_outlier_frames: {len(flagged)}")
    for r in flagged[:40]:
        print(f"  {r['file']} mean={r['mean']:.2f} std={r['std']:.2f} edge={r['edge']:.2f} chroma={r['chroma']:.2f}")
    return 0


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("map")
    m.add_argument("png")
    m.add_argument("cols", nargs="?", type=int, default=76)
    m.set_defaults(func=cmd_map)
    s = sub.add_parser("stats")
    s.add_argument("pngs", nargs="+")
    s.add_argument("--roi")
    s.set_defaults(func=cmd_stats)
    c = sub.add_parser("scan")
    c.add_argument("dir")
    c.add_argument("--roi")
    c.set_defaults(func=cmd_scan)
    args = ap.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
