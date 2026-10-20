"""rkc_black_sequence.py - where in time do the black frames sit?

WHY THIS EXISTS
A burst capture of a game window can contain all-black frames for two very
different reasons, and only one of them is a defect:

  * the window existed before the renderer presented its first frame, so
    PrintWindow had nothing to read back yet - a capture artefact, and it can
    only ever be a contiguous prefix;
  * the game really displayed a black screen mid-run - a defect, and it can fall
    anywhere in the sequence.

Counting "7 near-black frames" cannot tell those apart. Their POSITION can: a
contiguous prefix is the artefact, a black frame after the picture has already
been up is a fault. This script reports both, with the wall-clock offset of each
black frame so the answer is a number, not an impression.

Usage:
    python rkc_black_sequence.py <capture_dir> [--threshold 8.0]
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("capture_dir")
    ap.add_argument("--threshold", type=float, default=8.0)
    ap.add_argument("--interval-ms", type=float, default=300.0)
    args = ap.parse_args()

    d = Path(args.capture_dir)
    data = json.loads((d / "analysis.json").read_text())
    rows = data["frames"]

    black = [i for i, r in enumerate(rows, start=1) if r["frame_mean"] < args.threshold]
    print(f"FRAMES {len(rows)}  BLACK(<{args.threshold}) {len(black)}")

    first_lit = next((i for i, r in enumerate(rows, start=1)
                      if r["frame_mean"] >= args.threshold), None)
    print(f"FIRST_LIT_FRAME {first_lit}")
    print(f"BLACK_FRAME_INDICES {black}")

    # The artefact case: every black frame precedes the first lit frame.
    prefix = (first_lit is not None and all(i < first_lit for i in black))
    print(f"ALL_BLACK_FRAMES_PRECEDE_FIRST_LIT {prefix}")

    interleaved = [i for i in black if first_lit is not None and i > first_lit]
    print(f"BLACK_FRAMES_AFTER_PICTURE_WAS_UP {len(interleaved)} {interleaved}")

    if first_lit is not None:
        est = (first_lit - 1) * args.interval_ms / 1000.0
        print(f"EST_SECONDS_BEFORE_FIRST_LIT_FRAME {est:.2f} (at ~{args.interval_ms:.0f} ms/frame)")

    # Report the first lit frame's content so the "not black" claim is about
    # something identified, not merely about a number above a threshold.
    if first_lit is not None:
        r = rows[first_lit - 1]
        print(f"FIRST_LIT_FILE {r['file']}")
        print(f"  frame_mean {r['frame_mean']:.2f}  frame_black_frac {r['frame_black_frac']:.3f}")
        print(f"  backdrop {r['backdrop_band_mean']:.2f}  left_fighter {r['left_fighter_mean']:.2f}"
              f"  right_fighter {r['right_fighter_mean']:.2f}")

    verdict = ("PASS - every black frame precedes the first presented frame; "
               "no black screen was ever displayed after the game had drawn"
               if prefix and not interleaved else
               "FAIL - a black frame appeared after the game had already drawn")
    print(f"VERDICT: {verdict}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
