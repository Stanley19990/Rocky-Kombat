#!/usr/bin/env python3
"""verify_stage_shots.py - check the stage capture screenshots without eyes.

WHAT THIS PROVES, AND WHAT IT DOES NOT
The game writes the pictures and the manifest, and only the game can say which
screen was up when it did (the manifest records that, with the HUD's own draw
counts). What the game cannot check is whether the file it asked for actually
contains an image of that frame, or a black rectangle or a single flat colour.
That is what this does: it decodes each PNG and asserts the properties a blank
frame cannot have - the requested size, real colour variety, and the HUD's own
white text present in the pixels.

What pixels alone cannot do is separate a fresh frame from a stale copy of an
earlier frame: a stale buffer holds a real picture, so it decodes like a real
one. That part is bounded by the game's own manifest instead (one HUD DREW row
per stage, each naming the screen and the frame age), not by this script.

The folder may be a stage-capture run folder (manifest.txt, the game's own run
report) or the shipped picture folder (CAPTURE_MANIFEST.txt, provenance only).
The run report's completeness rule is applied when manifest.txt is the one in
use; for the shipped folder the script additionally requires that the provenance
manifest names every picture it checked.

Usage:
    python Tools/Diagnostics/verify_stage_shots.py <folder> [--json <out>]
"""

import json
import struct
import sys
import zlib
from collections import Counter
from pathlib import Path

# HUD palette (Source/RockyKombat/UI/RKCHUD.cpp). Accent is the warm heading
# colour, primary is the near-white body text.
EXPECTED_SIZE = (1280, 720)
MIN_DISTINCT_COLOURS = 8
MAX_NEAR_BLACK_FRACTION = 0.85

# The bar for HUD text in the pixels, and why it sits this low.
#
# text_pixels counts pixels with every channel >= 200: the near-white body text
# the HUD draws. It deliberately does NOT count the warm accent headings. The
# accent is (1.00, 0.82, 0.30) as drawn, whose blue channel is about 77, so
# min(r,g,b) >= 200 can never be true for accent ink however bright the heading
# looks. The accent count is therefore reported beside the text count, and only
# the white text is asserted.
#
# Measured on the seven shipped guide pictures (2026-10-19, every one decodes as
# 1280x720 RGBA) and recorded in Tools\Dist\Screens\CAPTURE_MANIFEST.txt, which
# ships in the pack, under "what was measured in these files":
#   every channel >= 200     154 (02_loading) .. 3652 (07_touchpad)
#   every channel >= 170     298 .. 8246
#   every channel >= 160     373 .. 10279
#   distinct colour buckets   67 (01_title) .. 1463 (05_match)
# The five front-end cards carry a thin antialiased white heading over a dark
# wash, so most of their heading pixels fall below 200 and they measure in the
# hundreds, while the match and touch-pad frames carry HUD body text and measure
# in the thousands. A blank or single-colour frame measures 0 text pixels and 1
# colour bucket, so a bar of 100 still separates "the HUD drew" from "nothing
# drew" by two orders of magnitude, without failing a real front-end card for
# having a light heading. The guide checker (rkc_guide_check.py) asserts the same
# idea with a looser ink bar (every channel >= 160, minimum 300) because it only
# has to rule out a blank card.
MIN_TEXT_PIXELS = 100


def decode_png(path):
    """Return (width, height, channels, rows) with PNG filters undone.

    Only the subset the engine writes is supported (8-bit, non-interlaced, RGB or
    RGBA). Anything else is reported rather than guessed at.
    """
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG file")

    pos = 8
    idat = bytearray()
    width = height = depth = colour_type = interlace = None
    while pos + 12 <= len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, colour_type, _comp, _filt, interlace = struct.unpack(
                ">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break

    channels = {0: 1, 2: 3, 4: 2, 6: 4}.get(colour_type)
    if depth != 8 or channels is None or interlace != 0:
        raise ValueError(f"unsupported PNG form: depth={depth} colour={colour_type} "
                         f"interlace={interlace}")

    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    rows = []
    previous = bytearray(stride)
    offset = 0
    for _ in range(height):
        filter_type = raw[offset]
        offset += 1
        line = bytearray(raw[offset:offset + stride])
        offset += stride
        if filter_type == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif filter_type == 2:
            for i in range(stride):
                line[i] = (line[i] + previous[i]) & 0xFF
        elif filter_type == 3:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + previous[i]) >> 1)) & 0xFF
        elif filter_type == 4:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                up = previous[i]
                upleft = previous[i - channels] if i >= channels else 0
                p = left + up - upleft
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - upleft)
                pred = left if (pa <= pb and pa <= pc) else (up if pb <= pc else upleft)
                line[i] = (line[i] + pred) & 0xFF
        elif filter_type != 0:
            raise ValueError(f"unknown PNG filter {filter_type}")
        rows.append(bytes(line))
        previous = line
    return width, height, channels, rows


def analyse(path):
    width, height, channels, rows = decode_png(path)

    total = 0
    near_black = 0
    light_text = 0
    warm_accent = 0
    luminance_sum = 0
    colours = Counter()

    for row in rows:
        for i in range(0, len(row), channels):
            r = row[i]
            g = row[i + 1] if channels >= 3 else row[i]
            b = row[i + 2] if channels >= 3 else row[i]
            total += 1
            if r <= 8 and g <= 8 and b <= 8:
                near_black += 1
            if min(r, g, b) >= 200:
                light_text += 1
            # The accent heading (1.00, 0.82, 0.30) as drawn, allowing for an
            # sRGB round trip and for the text's antialiased edge.
            if r >= 200 and g >= 140 and b <= 190 and (r - b) >= 60:
                warm_accent += 1
            luminance_sum += (r * 299 + g * 587 + b * 114) // 1000
            colours[(r >> 4, g >> 4, b >> 4)] += 1

    return {
        "file": path.name,
        "width": width,
        "height": height,
        "channels": channels,
        "pixels": total,
        "distinct_colours": len(colours),
        "near_black_fraction": round(near_black / total, 4) if total else 1.0,
        "mean_luminance": round(luminance_sum / total, 2) if total else 0.0,
        "text_pixels": light_text,
        "accent_pixels": warm_accent,
        "top_colours": [
            {"rgb": [c[0] << 4, c[1] << 4, c[2] << 4], "share": round(n / total, 4)}
            for c, n in colours.most_common(5)
        ],
    }


def verdict(stats):
    """The checks a black or blank capture cannot pass (staleness is the manifest's)."""
    failures = []
    if (stats["width"], stats["height"]) != EXPECTED_SIZE:
        failures.append(f"size is {stats['width']}x{stats['height']}, not "
                        f"{EXPECTED_SIZE[0]}x{EXPECTED_SIZE[1]}")
    if stats["distinct_colours"] < MIN_DISTINCT_COLOURS:
        failures.append(f"only {stats['distinct_colours']} distinct colours (a blank frame)")
    if stats["near_black_fraction"] > MAX_NEAR_BLACK_FRACTION:
        failures.append(f"{stats['near_black_fraction']:.0%} of the frame is near black")
    if stats["text_pixels"] < MIN_TEXT_PIXELS:
        failures.append(f"only {stats['text_pixels']} light text pixels (no HUD text)")
    return failures


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2

    folder = Path(argv[1])
    if not folder.is_dir():
        print(f"no such folder: {folder}")
        return 2

    json_out = None
    if "--json" in argv:
        json_out = Path(argv[argv.index("--json") + 1])

    shots = sorted(folder.glob("*.png"))
    if not shots:
        print(f"no screenshots in {folder}")
        return 2

    report = {"folder": str(folder), "shots": [], "failed": []}
    for shot in shots:
        try:
            stats = analyse(shot)
        except Exception as exc:  # noqa: BLE001 - reported, never swallowed
            report["failed"].append(shot.name)
            print(f"FAIL  {shot.name}: could not be read as a PNG ({exc})")
            continue

        failures = verdict(stats)
        stats["failures"] = failures
        report["shots"].append(stats)
        if failures:
            report["failed"].append(shot.name)

        mark = "PASS" if not failures else "FAIL"
        print(f"{mark}  {stats['file']:<16} {stats['width']}x{stats['height']}  "
              f"colours {stats['distinct_colours']:>5}  near-black "
              f"{stats['near_black_fraction']:>6.1%}  mean luma {stats['mean_luminance']:>6.2f}  "
              f"text px {stats['text_pixels']:>7}  accent px {stats['accent_pixels']:>7}")
        for failure in failures:
            print(f"        - {failure}")

    # A stage-capture folder carries the game's own manifest.txt: one row per
    # stage with the screen name, the frame age and the HUD's draw report, ending
    # in "the plan is complete". The shipped picture folder carries
    # CAPTURE_MANIFEST.txt instead, which is provenance rather than a run report
    # and has no HUD rows. Both are accepted; the completeness rule belongs to
    # the run report, and the provenance document has to name every picture.
    stage_manifest = folder / "manifest.txt"
    cap_manifest = folder / "CAPTURE_MANIFEST.txt"
    report["manifest_present"] = False
    report["manifest_kind"] = None
    report["manifest_names_all_shots"] = None

    if stage_manifest.is_file():
        text = stage_manifest.read_text(encoding="utf-8", errors="replace")
        report["manifest_present"] = True
        report["manifest_kind"] = "stage"
        report["manifest_says_complete"] = "the plan is complete" in text
        report["manifest_hud_drew_rows"] = text.count("HUD DREW")
        print(f"\nmanifest: {stage_manifest.name} (stage run report), HUD DREW rows "
              f"{report['manifest_hud_drew_rows']}, complete "
              f"{'yes' if report['manifest_says_complete'] else 'no'}")
    elif cap_manifest.is_file():
        text = cap_manifest.read_text(encoding="utf-8", errors="replace")
        unnamed = [s.name for s in shots if s.name not in text]
        report["manifest_present"] = True
        report["manifest_kind"] = "capture"
        report["manifest_names_all_shots"] = not unnamed
        print(f"\nmanifest: {cap_manifest.name} (shipped-picture provenance, "
              f"{cap_manifest.stat().st_size} bytes); not a stage run report, so no "
              f"HUD rows are expected")
        if unnamed:
            print(f"        - the manifest does not name: {', '.join(unnamed)}")
    else:
        print("\nmanifest: MISSING (neither manifest.txt nor CAPTURE_MANIFEST.txt)")

    ok = (not report["failed"]) and report["manifest_present"]
    if report["manifest_kind"] == "stage":
        ok = ok and report.get("manifest_says_complete", False) and \
            report.get("manifest_hud_drew_rows", 0) >= len(shots)
    elif report["manifest_kind"] == "capture":
        ok = ok and bool(report["manifest_names_all_shots"])
    print(f"\n{len(shots) - len(report['failed'])} of {len(shots)} screenshot(s) verified; "
          f"overall {'PASS' if ok else 'FAIL'}")
    report["overall"] = "PASS" if ok else "FAIL"

    if json_out is not None:
        json_out.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(f"report written: {json_out}")

    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
