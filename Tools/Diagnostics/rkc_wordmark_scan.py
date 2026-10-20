"""rkc_wordmark_scan.py - measure a repeating watermark in a source image.

WHY THIS EXISTS
The supplied arena art carries a faint repeated wordmark. "Remove the watermark"
is not actionable until the pattern is described with numbers: how big one tile is,
how much brighter the strokes are than the surface under them, and where the tiles
sit. This tool reports those three things and nothing else, so a decision to edit
the art is made from measurements instead of a squint.

METHOD
  1. High-pass the luminance (subtract a wide box blur) so slow art gradients -
     sky, floor falloff - drop out and only strokes, grain and edges remain.
  2. Autocorrelate the high-pass along x (averaged over rows) and along y
     (averaged over columns). A repeating pattern puts a peak at its period;
     the peaks are reported with their normalised correlation.
  3. Fold the high-pass into tiles of the detected period and average them. A
     real repetition survives averaging; random grain averages towards zero. The
     surviving template is the watermark, and its peak-to-peak amplitude is the
     visibility figure that matters.

Usage:
    python rkc_wordmark_scan.py <image.png> [--max-period 800]
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


def luminance(im: Image.Image) -> np.ndarray:
    a = np.asarray(im.convert("RGB"), dtype=np.float32)
    return 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]


def box_blur_1d(a: np.ndarray, radius: int, axis: int) -> np.ndarray:
    """Separable box blur via cumulative sums - O(n), no scipy dependency."""
    if radius < 1:
        return a
    n = a.shape[axis]
    pad = [(0, 0)] * a.ndim
    pad[axis] = (radius, radius)
    p = np.pad(a, pad, mode="edge")
    c = np.cumsum(p, axis=axis, dtype=np.float64)
    zero_shape = list(c.shape)
    zero_shape[axis] = 1
    c = np.concatenate([np.zeros(zero_shape, dtype=np.float64), c], axis=axis)
    hi = np.take(c, np.arange(2 * radius + 1, 2 * radius + 1 + n), axis=axis)
    lo = np.take(c, np.arange(0, n), axis=axis)
    return ((hi - lo) / float(2 * radius + 1)).astype(np.float32)


def high_pass(lum: np.ndarray, radius: int) -> np.ndarray:
    return lum - box_blur_1d(box_blur_1d(lum, radius, 0), radius, 1)


def profile(hp: np.ndarray, axis: int) -> np.ndarray:
    """Mean high-pass along the other axis: one value per column (or row)."""
    return hp.mean(axis=1 - axis)


def autocorr_peaks(sig: np.ndarray, max_period: int, top: int = 6,
                   min_period: int = 8) -> list[dict]:
    """Lags below min_period are pixel-level texture and edges, not a repeating
    wordmark, and they always outrank the real period on raw correlation. The
    floor is explicit so the reported period is the artwork's, not the grain's."""
    s = sig.astype(np.float64)
    s = s - s.mean()
    denom = float((s * s).sum())
    if denom <= 1e-9:
        return []
    n = len(s)
    limit = min(max_period, n - 2)
    out = []
    for lag in range(max(2, min_period), limit + 1):
        a = s[: n - lag]
        b = s[lag:]
        out.append((lag, float((a * b).sum() / denom)))
    out.sort(key=lambda kv: kv[1], reverse=True)
    return [{"period": p, "correlation": round(c, 4)} for p, c in out[:top]]


def fold_template(hp: np.ndarray, px: int, py: int) -> np.ndarray:
    """Average all whole tiles of size (py, px) into one template."""
    h, w = hp.shape
    ny, nx = h // py, w // px
    if ny < 2 or nx < 2:
        return np.zeros((py, px), dtype=np.float32)
    acc = np.zeros((ny * py, nx * px), dtype=np.float32)
    acc[:] = hp[: ny * py, : nx * px]
    acc = acc.reshape(ny, py, nx, px).mean(axis=(0, 2))
    return acc


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--max-period", type=int, default=800)
    ap.add_argument("--blur-radius", type=int, default=12)
    ap.add_argument("--min-period", type=int, default=8)
    ap.add_argument("--out", default=None)
    ap.add_argument("--save-template", default=None,
                    help="write the folded tile as a contrast-stretched PNG so the "
                         "repeat can be read by eye instead of trusted from a number")
    args = ap.parse_args()

    path = Path(args.image)
    im = Image.open(path)
    lum = luminance(im)
    h, w = lum.shape

    hp = high_pass(lum, args.blur_radius)
    px = profile(hp, axis=1)   # per column
    py = profile(hp, axis=0)   # per row

    peaks_x = autocorr_peaks(px, args.max_period, min_period=args.min_period)
    peaks_y = autocorr_peaks(py, args.max_period, min_period=args.min_period)

    print(f"IMAGE {path.name} {w}x{h}")
    print(f"HIGH_PASS std {hp.std():.4f}  mean |hp| {np.abs(hp).mean():.4f}")
    print()
    print("PERIOD_X (columns)")
    for r in peaks_x:
        print(f"  period {r['period']:5d}  corr {r['correlation']:+.4f}")
    print("PERIOD_Y (rows)")
    for r in peaks_y:
        print(f"  period {r['period']:5d}  corr {r['correlation']:+.4f}")

    result = {
        "image": str(path),
        "width": w,
        "height": h,
        "high_pass_std": round(float(hp.std()), 4),
        "peaks_x": peaks_x,
        "peaks_y": peaks_y,
    }

    if peaks_x and peaks_y:
        bx, by = peaks_x[0]["period"], peaks_y[0]["period"]
        tpl = fold_template(hp, bx, by)
        amp = float(tpl.max() - tpl.min())
        std = float(tpl.std())
        print()
        print(f"TILE {bx} x {by} columns x rows")
        print(f"  template amplitude (p-p) {amp:.3f} luminance levels")
        print(f"  template std             {std:.4f}")
        print(f"  template max             {tpl.max():.3f}   min {tpl.min():.3f}")
        # Where in the tile the bright strokes sit, in relative terms.
        ty, tx = np.unravel_index(int(np.argmax(tpl)), tpl.shape)
        print(f"  brightest point at tile row {ty}, column {tx}")
        if args.save_template:
            # Stretch the template to full 0-255 span so the strokes are legible;
            # the stretch changes display contrast only, not the measurement.
            lo, hi = float(tpl.min()), float(tpl.max())
            span = max(hi - lo, 1e-6)
            stretched = ((tpl - lo) / span * 255.0).astype(np.uint8)
            Image.fromarray(stretched, mode="L").resize(
                (bx * 3, by * 3), Image.NEAREST).save(args.save_template)
            print(f"  wrote template image {args.save_template}")

        result.update({
            "tile_w": bx,
            "tile_h": by,
            "template_amplitude": round(amp, 3),
            "template_std": round(std, 4),
            "brightest_tile_point": [int(ty), int(tx)],
        })

    if args.out:
        Path(args.out).write_text(json.dumps(result, indent=2))
        print(f"\nWROTE {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
