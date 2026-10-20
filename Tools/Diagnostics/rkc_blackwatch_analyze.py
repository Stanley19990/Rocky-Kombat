"""rkc_blackwatch_analyze.py - measure black-frame faults in a blackwatch capture run.

Reads frame_*.png from a capture directory and reports, per frame, the mean
luminance of the backdrop band, the left/right fighter boxes and the whole
frame, plus the near-black pixel fraction inside each fighter box. Frames that
share a pixel hash are flagged as duplicates so a static/desktop capture cannot
be mistaken for a game frame.

Usage:
    python rkc_blackwatch_analyze.py <capture_dir> [--out out.json]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def lum(rgb: np.ndarray) -> np.ndarray:
    return 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]


def analyse(path: Path) -> dict:
    im = Image.open(path).convert("RGB")
    a = np.asarray(im, dtype=np.float32)
    h, w, _ = a.shape
    L = lum(a)

    # Boxes are fractions of the window so the tool is resolution independent.
    # 1280x720 reference: fighters sit around x 220-420 / 820-1080, y 300-560.
    boxes = {
        "backdrop_band": (0.05, 0.30, 0.95, 0.40),   # above both fighters
        "mid_gap": (0.44, 0.42, 0.56, 0.80),         # between the fighters
        "floor_band": (0.05, 0.85, 0.95, 0.97),      # canvas below the action
        "left_fighter": (0.17, 0.42, 0.33, 0.78),
        "right_fighter": (0.64, 0.42, 0.85, 0.78),
    }

    out: dict = {"file": path.name, "width": w, "height": h}
    out["frame_mean"] = float(L.mean())
    out["frame_black_frac"] = float((L < 12.0).mean())
    out["sha1_16"] = hashlib.sha1(a.astype(np.uint8).tobytes()).hexdigest()[:16]

    for name, (x0f, y0f, x1f, y1f) in boxes.items():
        x0, x1 = int(x0f * w), int(x1f * w)
        y0, y1 = int(y0f * h), int(y1f * h)
        sub = L[y0:y1, x0:x1]
        out[name + "_mean"] = float(sub.mean())
        out[name + "_black_frac"] = float((sub < 12.0).mean())
        out[name + "_max"] = float(sub.max())
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("capture_dir")
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    d = Path(args.capture_dir)
    frames = sorted(d.glob("frame_*.png"))
    if not frames:
        print(f"NO_FRAMES in {d}")
        return 2

    rows = [analyse(p) for p in frames]
    out_path = Path(args.out) if args.out else d / "analysis.json"
    out_path.write_text(json.dumps({"capture_dir": str(d), "frames": rows}, indent=2))

    hashes = [r["sha1_16"] for r in rows]
    dupes = len(hashes) - len(set(hashes))

    def col(key):
        return np.array([r[key] for r in rows], dtype=np.float64)

    print(f"FRAMES {len(rows)}  UNIQUE {len(set(hashes))}  DUPLICATE_FRAMES {dupes}")
    for key in ("frame_mean", "frame_black_frac", "backdrop_band_mean", "mid_gap_mean",
                "floor_band_mean", "left_fighter_mean", "right_fighter_mean"):
        c = col(key)
        print(f"{key:22s} min={c.min():8.2f} mean={c.mean():8.2f} max={c.max():8.2f}")

    # A black-fighter fault: the fighter box is essentially black while the rest
    # of the frame is lit, so the HUD and the other fighter prove the frame is real.
    faults = []
    for r in rows:
        for side in ("left_fighter", "right_fighter"):
            if r[side + "_black_frac"] > 0.90 and r["frame_mean"] > 20.0 and r["backdrop_band_mean"] > 20.0:
                faults.append((r["file"], side, round(r[side + "_mean"], 2)))
    print(f"FIGHTER_BLACK_FRAMES {len(faults)}")
    for f in faults:
        print("  ", f)

    black_bg = [(r["file"], round(r["backdrop_band_mean"], 2)) for r in rows
                if r["backdrop_band_mean"] < 6.0 and r["left_fighter_mean"] > 25.0]
    print(f"BACKDROP_BLACK_FRAMES {len(black_bg)}")
    for b in black_bg:
        print("  ", b)

    print(f"WROTE {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
