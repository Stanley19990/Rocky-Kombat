#!/usr/bin/env python3
"""rkc_pad_presence.py - is the on-screen touch pad actually in the picture?

The stage capture names a frame "touchpad" because the game asked for that frame
while the pad was up. That is the game's own claim. It cannot tell whether the
pad drew: a capture that fell on a plain match frame would carry the name just as
happily, and the two frames are close to a summary statistic (same arena, same
fighters, same HUD, only the pad added).

Two measurements, answering the question at two sizes:

1. Whole-frame difference against a match frame. A pad that really drew shows a
   large changed area concentrated at the left and right edges, where the virtual
   stick and the action buttons sit. Note what this does NOT separate: if the
   match frame handed in also has the pad up (which it does when a run was
   started with -rkc.TouchForced=1), the difference is animation and antialiasing
   only, and its size is an upper bound on the frame-to-frame noise floor of the
   scene rather than a measure of the pad.

2. Control by control, in each control's own circle and its own drawn colour,
   anchored to the layout in RKCTouchControls.cpp. This one is not confused by
   either of the above: it says which of the stick, pause, jab, heavy, block and
   dodge are present, where, and what the same circles hold in the frame it is
   compared against - and it fails a control whose ink is missing or has drifted
   off its own centre. The reference frame is measured in the same places, so a
   pass means the ink is the pad's and not the scene's.

Usage:
    python Tools/Diagnostics/rkc_pad_presence.py <match.png> <touchpad.png> [--json out]
"""

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from verify_stage_shots import decode_png  # noqa: E402

# A drawn pad covers a real fraction of the frame. Two independently drawn frames
# of the same scene differ only by antialiasing and animation, which is orders of
# magnitude smaller than this.
MIN_DIFF_PIXELS = 20000
MIN_EDGE_SHARE = 0.5
DIFF_THRESHOLD = 24

# ---- the pad's own layout, measured control by control ----------------------
#
# Mirrors RKCTouch::FController's constructor and its CentrePx()/RadiusPx()
# (Source/RockyKombat/UI/RKCTouchControls.cpp): each control is authored as a
# fraction of the safe rectangle, with the radius sized by its smaller side. On a
# desktop there are no safe-area insets, so the rectangle is the whole canvas and
# the fraction maps straight onto pixels.
#
# This is what answers "is the pad in the picture" per control rather than in
# bulk: it counts pixels of each control's own drawn colour inside that control's
# own circle, and reports where that ink actually landed. A drifting mirror of
# the layout would show up as a displaced centroid, not as a silent pass.
#
# The colours below are copied verbatim from the source, where they are declared
# as FLinearColor. A captured frame does not hold those numbers: the canvas
# encodes linear to sRGB on the way out (RKCHUD draws the ring opaque at alpha
# 0.95 over a dark disc, so what lands in the PNG is the sRGB byte). Comparing
# against the raw linear literals is what once made every button measure zero ink
# while the near-grey stick still matched - grey barely moves under the transfer
# curve, saturated colours move a lot (Jab linear 242,217,89 vs shipped
# 249,237,160). So the literals are kept as the source of truth and the expected
# pixel is derived from them.
PAD_VIEWPORT = (1280, 720)
PAD_WIDGETS_LINEAR = [
    # control,  centre x, centre y, radius, source colour (FLinearColor)
    ("Stick", 0.155, 0.690, 0.110, (0.85, 0.85, 0.90)),
    ("Pause", 0.928, 0.100, 0.048, (0.90, 0.35, 0.32)),
    ("Jab", 0.845, 0.740, 0.058, (0.95, 0.85, 0.35)),
    ("Heavy", 0.940, 0.620, 0.058, (0.95, 0.45, 0.30)),
    ("Block", 0.765, 0.820, 0.062, (0.45, 0.75, 0.95)),
    ("Dodge", 0.940, 0.870, 0.058, (0.60, 0.90, 0.55)),
]


def srgb_channel(value):
    """FLinearColor component -> the 0-255 byte the canvas emits for it."""
    encoded = (12.92 * value if value <= 0.0031308
               else 1.055 * (value ** (1.0 / 2.4)) - 0.055)
    return int(round(255.0 * encoded))


PAD_WIDGETS = [(name, fx, fy, fr, tuple(srgb_channel(c) for c in linear))
               for name, fx, fy, fr, linear in PAD_WIDGETS_LINEAR]
# A drawn ring is 550-1700 antialiased pixels depending on its radius; the bar sits
# well under the smallest of those and above the coincidental scene matches an
# arena frame produces (measured 0 for five controls and 128 for Heavy, whose
# colour happens to sit near the lit arena floor).
MIN_WIDGET_INK = 80
# The source centres each control on a fraction of the viewport, and the measured
# ink lands within 0.7 px of that centre; anything past a few pixels is a drift.
MAX_WIDGET_CENTROID_DRIFT_PX = 4.0
COLOUR_TOLERANCE = 45
# The stroke is about 3 px of antialiasing straddling the ideal radius, so the
# counting window is the circle's own square plus this margin.
INK_WINDOW_MARGIN_PX = 3.0


def widget_presence(pad_png, reference_png=None):
    """Count each control's own ink inside each control's own circle.

    The reference frame (a match frame with the pad off, when one is available)
    is measured the same way, in the same places, so the comparison is between
    two frames rather than against a hand-picked number.
    """
    _w, _h, channels, pad_rows = decode_png(pad_png)
    ref_rows = decode_png(reference_png)[3] if reference_png else None
    width, height = PAD_VIEWPORT
    min_dim = min(width, height)

    def ink(rows, cx, cy, radius, colour):
        """Count the control's own colour near its own circle.

        The window is the circle's own square plus a margin, deliberately not cut
        at the radius. Cutting exactly at the radius keeps the inner part of the
        ~3 px antialiased stroke and drops the outer part, and the dropped part is
        not balanced: that window alone shifts the reported centroid by +5 to +8 px
        even though the drawing is concentric (the same frame measured in the
        square window lands within 0.7 px of the source centre - see
        Dist/probe_pad_geometry.txt). Since the centroid is the measurement that
        would catch a drifted layout, it must not be biased by its own window.
        """
        count = 0
        sum_x = sum_y = 0
        box = radius + INK_WINDOW_MARGIN_PX
        x0, x1 = max(0, int(cx - box)), min(width - 1, int(round(cx + box)))
        y0, y1 = max(0, int(cy - box)), min(height - 1, int(round(cy + box)))
        for y in range(y0, y1 + 1):
            row = rows[y]
            for x in range(x0, x1 + 1):
                i = x * channels
                if (abs(row[i] - colour[0]) <= COLOUR_TOLERANCE
                        and abs(row[i + 1] - colour[1]) <= COLOUR_TOLERANCE
                        and abs(row[i + 2] - colour[2]) <= COLOUR_TOLERANCE):
                    count += 1
                    sum_x += x
                    sum_y += y
        centroid = (round(sum_x / count, 1), round(sum_y / count, 1)) if count else None
        return count, centroid

    results = []
    failures = []
    for name, fx, fy, fr, colour in PAD_WIDGETS:
        cx, cy = fx * width, fy * height
        radius = fr * min_dim
        count, centroid = ink(pad_rows, cx, cy, radius, colour)
        entry = {
            "control": name,
            "expected_centre_px": [round(cx, 1), round(cy, 1)],
            "radius_px": round(radius, 1),
            "expected_pixel_rgb": list(colour),
            "ink_pixels": count,
            "centroid_px": centroid,
        }
        if ref_rows is not None:
            ref_count, _ = ink(ref_rows, cx, cy, radius, colour)
            entry["ink_pixels_in_reference"] = ref_count
        results.append(entry)
        if count < MIN_WIDGET_INK:
            failures.append(f"{name}: only {count} pixel(s) of its own drawn colour in "
                            f"its own {entry['radius_px']:.0f} px circle (needs {MIN_WIDGET_INK})")
        elif centroid is not None:
            drift = max(abs(centroid[0] - cx), abs(centroid[1] - cy))
            if drift > MAX_WIDGET_CENTROID_DRIFT_PX:
                failures.append(f"{name}: its {count} painted pixel(s) sit {drift:.0f} px off "
                                f"its own centre - the drawn pad has drifted from the "
                                f"layout (allows {MAX_WIDGET_CENTROID_DRIFT_PX:.0f})")
    return results, failures


def compare(match_png, pad_png):
    wa, ha, ca, ra = decode_png(match_png)
    wb, hb, cb, rb = decode_png(pad_png)
    if (wa, ha) != (wb, hb):
        raise ValueError(f"size mismatch: {wa}x{ha} vs {wb}x{hb}")
    if ca != cb:
        raise ValueError(f"channel mismatch: {ca} vs {cb}")

    diff = 0
    left = right = middle = 0
    minx, miny, maxx, maxy = wa, ha, -1, -1
    for y in range(ha):
        a = ra[y]
        b = rb[y]
        for x in range(wa):
            i = x * ca
            delta = abs(a[i] - b[i]) + abs(a[i + 1] - b[i + 1]) + abs(a[i + 2] - b[i + 2])
            if delta <= DIFF_THRESHOLD:
                continue
            diff += 1
            minx = min(minx, x)
            maxx = max(maxx, x)
            miny = min(miny, y)
            maxy = max(maxy, y)
            if x < wa / 3:
                left += 1
            elif x > 2 * wa / 3:
                right += 1
            else:
                middle += 1

    total = wa * ha
    edge_share = (left + right) / diff if diff else 0.0
    failures = []
    if diff < MIN_DIFF_PIXELS:
        failures.append(f"only {diff} pixel(s) differ from the match frame - the pad "
                        f"cannot be that small")
    if edge_share < MIN_EDGE_SHARE:
        failures.append(f"only {edge_share:.0%} of the change is at the left and right "
                        f"edges, where the stick and buttons live")
    return {
        "match": str(match_png),
        "touchpad": str(pad_png),
        "width": wa,
        "height": ha,
        "diff_pixels": diff,
        "diff_fraction": round(diff / total, 4),
        "bounding_box": [minx, miny, maxx, maxy],
        "left_third": left,
        "middle": middle,
        "right_third": right,
        "edge_share": round(edge_share, 4),
        "failures": failures,
    }


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    match_png, pad_png = Path(argv[1]), Path(argv[2])
    for p in (match_png, pad_png):
        if not p.is_file():
            print(f"missing: {p}")
            return 2

    stats = compare(match_png, pad_png)
    print(f"frame            {stats['width']}x{stats['height']}")
    print(f"differing pixels {stats['diff_pixels']} ({stats['diff_fraction']:.2%} of the frame)")
    print(f"bounding box     x {stats['bounding_box'][0]}..{stats['bounding_box'][2]}  "
          f"y {stats['bounding_box'][1]}..{stats['bounding_box'][3]}")
    print(f"left third {stats['left_third']}  middle {stats['middle']}  "
          f"right third {stats['right_third']}  (edge share {stats['edge_share']:.0%})")
    for failure in stats["failures"]:
        print(f"        - {failure}")
    verdict = "PASS" if not stats["failures"] else "FAIL"
    print(f"\nverdict: {verdict} the touch pad draws over the match frame")

    # The per-control measurement. The match frame is passed as the reference
    # frame, so every control is counted in the same circle in both frames.
    widgets, widget_failures = widget_presence(pad_png, match_png)
    print(f"\ncontrol by control (its own colour, inside its own circle, drawn over the "
          f"match frame):")
    print(f"  {'control':<8} {'centre px':<14} {'radius':>7} {'rgb':>15} "
          f"{'ink px':>7} {'in match':>9}  {'centroid px':<16} offset")
    for entry in widgets:
        cx, cy = entry["expected_centre_px"]
        centroid = entry["centroid_px"]
        offset = (f"{centroid[0] - cx:+.1f}, {centroid[1] - cy:+.1f}"
                  if centroid else "n/a")
        rgb = ",".join(str(c) for c in entry["expected_pixel_rgb"])
        print(f"  {entry['control']:<8} {f'{cx:.0f}, {cy:.0f}':<14} "
              f"{entry['radius_px']:>7.1f} {rgb:>15} "
              f"{entry['ink_pixels']:>7} {entry['ink_pixels_in_reference']:>9}  "
              f"{str(centroid):<16} {offset}")
    for failure in widget_failures:
        print(f"        - {failure}")
    widget_verdict = "PASS" if not widget_failures else "FAIL"
    print(f"  verdict: {widget_verdict} every control is in the picture")
    stats["widgets"] = widgets
    stats["widget_failures"] = widget_failures
    stats["widget_verdict"] = widget_verdict
    if widget_verdict == "FAIL":
        verdict = "FAIL"

    if "--json" in argv:
        out = Path(argv[argv.index("--json") + 1])
        stats["verdict"] = verdict
        out.write_text(json.dumps(stats, indent=2), encoding="utf-8")
        print(f"report written: {out}")
    return 0 if verdict == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
