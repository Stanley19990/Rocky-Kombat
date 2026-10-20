"""rkc_reencode_stages.py - decide whether a stage PNG is corrupt or the viewer is.

WHY THIS EXISTS
Some tools display a captured RGBA PNG by replicating one horizontal band across
the frame, which makes a perfectly good screenshot look like tiled, repeated
text. That is a viewer fault, not a capture fault, but the two are easy to
confuse - and a guide built from mis-read screens would be wrong.

This script decides with measurements rather than by eye:

  * row_uniqueness - how many of the image's rows are distinct. A genuine
    band-replication fault produces long runs of byte-identical adjacent rows.
  * longest_run    - the longest run of byte-identical adjacent rows.
  * a re-encoded JPEG copy of the same picture, which is what a viewer that
    mishandles this particular PNG encoding will happily display.

Usage:
    python rkc_reencode_stages.py <stage_dir> [<out_dir>]
"""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
from PIL import Image


def row_report(path: Path) -> dict:
    with Image.open(path) as im:
        im.load()
        mode = im.mode
        a = np.asarray(im.convert("RGB"), dtype=np.uint8)

    h = a.shape[0]
    # Hash each row so identical rows can be recognised cheaply.
    rows = [hash(a[y].tobytes()) for y in range(h)]
    unique = len(set(rows))
    longest = 1
    run = 1
    for y in range(1, h):
        if rows[y] == rows[y - 1]:
            run += 1
            longest = max(longest, run)
        else:
            run = 1
    return {"mode": mode, "height": h, "width": a.shape[1],
            "unique_rows": unique, "longest_run": longest,
            "unique_frac": unique / h}


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    src = Path(sys.argv[1])
    dst = Path(sys.argv[2]) if len(sys.argv) > 2 else src / "readable"
    dst.mkdir(parents=True, exist_ok=True)

    shots = sorted([p for p in src.glob("*.png")])
    if not shots:
        print(f"NO_PNG in {src}")
        return 3

    print(f"{'file':<18}{'mode':<6}{'size':<12}{'uniq_rows':>10}{'uniq_frac':>11}{'longest_run':>12}")
    worst = 0
    uniform = 0
    suspected = []
    for p in shots:
        r = row_report(p)
        print(f"{p.name:<18}{r['mode']:<6}{r['width']}x{r['height']:<6}"
              f"{r['unique_rows']:>10}{r['unique_frac']:>11.3f}{r['longest_run']:>12}")

        # A frame whose rows are ALL identical is legitimately flat (a black
        # launch frame, a solid card) - not evidence of a replication fault, so
        # it must not be allowed to drive the verdict below.
        if r["unique_rows"] == 1:
            uniform += 1
        else:
            # A replication fault keeps real content AND repeats a band, so the
            # tell is a long identical run inside a picture that is not uniform.
            worst = max(worst, r["longest_run"])
            if r["longest_run"] > 64:
                suspected.append(p.name)

        # Re-encode: a viewer that mishandles the original will usually read this.
        with Image.open(p) as im:
            re_encoded = im.convert("RGB")
            re_encoded.save(dst / (p.stem + ".jpg"), "JPEG", quality=92)

    print()
    print(f"UNIFORM_FRAMES {uniform} (all rows identical - flat by construction, not a fault)")
    print(f"LONGEST_IDENTICAL_ROW_RUN_IN_NON_UNIFORM_FRAMES {worst}")
    verdict = (f"CAPTURE band-replication fault suspected in {suspected}" if suspected
               else "no band-replication in the files - a viewer fault")
    print(f"VERDICT: {verdict}")
    print(f"WROTE {dst}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
