#!/usr/bin/env python3
"""verify_arena_framing.py - check the arena image really is the only thing on screen.

WHAT THIS PROVES, AND WHAT IT DOES NOT
The acceptance question for this game's arena is "exactly one visible arena, the
supplied Midnight Rooftop Ring image, filling the view, with the fighters' feet on
its painted floor". Three parts of that can be checked without eyes, and this
checks them against the pictures a run actually produced:

  1. PIXELS. Every stage screenshot must have real content in the bands where the
     picture is supposed to reach - the bottom, the top and both sides of the
     frame. A picture that covers the frame puts content there; a picture that is
     too small, or missing, leaves the renderer's flat clear colour, which shows up
     as one colour across a whole band. The earlier build failed this: its picture
     covered 54% of the frame's width and stopped 1274 cm above the bottom edge of
     the frame, so two side bands and the bottom band were flat.
  2. THE MANIFEST'S OWN NUMBERS. The arena reports, per run, the picture's size and
     aspect, the screen position of the fighters' feet, the screen position of the
     picture's painted floor line, and how far the picture reaches past the frame's
     tightest edge. This re-checks that arithmetic from the recorded numbers: the
     aspect ratio must be the source image's, the two screen positions must agree,
     and the cover margin must be positive.
  3. THE TWO RINGS. The manifest must say the imported image is in use and the
     procedural greybox ring is hidden, at every stage. That is the "no duplicate
     arena" claim, recorded by the game and re-read here.

WHAT IT DOES NOT PROVE, AND CANNOT: that the picture is the approved artwork, that
it is the right way up, or that it looks right. Those are judgements about the
image, and a human has to make them by opening the screenshots. This tool narrows
that job - it rules out the mechanical failures - it does not replace it.

Usage:
    python Tools/Diagnostics/verify_arena_framing.py <stage-folder> [--json <out>]
                                                     [--band 0.07] [--side 0.03]

Run it on a capture taken at a landscape size of 4:3 or wider (the project's own
capture_stages.bat uses 1280x720). A portrait run is letterboxed on purpose, so its
top and bottom bands are the bars and the band check would be reporting the design
rather than a defect.
"""

import json
import struct
import sys
import zlib
from collections import Counter
from pathlib import Path

# The bands are fractions of the frame's height (top/bottom) and width (sides).
DEFAULT_BAND_FRACTION = 0.07
DEFAULT_SIDE_FRACTION = 0.03

# A band of a real picture has many colours in it even when it is a dark night sky,
# because a photograph is never perfectly flat. A band that is one colour is the
# renderer's clear colour and nothing else drew there.
MIN_DISTINCT_COLOURS = 12
FLAT_BAND_DISTINCT_COLOURS = 3
FLAT_BAND_MODAL_SHARE = 0.995

SOURCE_IMAGE_ASPECT = 1672.0 / 941.0
ASPECT_TOLERANCE = 0.005
ALIGNMENT_TOLERANCE = 0.001


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


def band_stats(rows, channels, row_range, col_range):
    """Colour variety of one rectangle of the frame."""
    colours = Counter()
    pixels = 0
    for y in row_range:
        row = rows[y]
        for x in col_range:
            i = x * channels
            r = row[i]
            g = row[i + 1] if channels >= 3 else row[i]
            b = row[i + 2] if channels >= 3 else row[i]
            colours[(r >> 3, g >> 3, b >> 3)] += 1
            pixels += 1
    modal = colours.most_common(1)[0][1] if colours else 0
    return {
        "pixels": pixels,
        "distinct_colours": len(colours),
        "modal_share": round(modal / pixels, 4) if pixels else 1.0,
        "top_colours": [
            {"rgb": [c[0] << 3, c[1] << 3, c[2] << 3], "share": round(n / pixels, 4)}
            for c, n in colours.most_common(3)
        ],
    }


def analyse_bands(path, band_fraction, side_fraction):
    width, height, channels, rows = decode_png(path)

    band_rows = max(1, int(round(height * band_fraction)))
    side_cols = max(1, int(round(width * side_fraction)))
    every_row = range(height)
    every_col = range(width)

    bands = {
        "top": band_stats(rows, channels, range(0, band_rows), every_col),
        "bottom": band_stats(rows, channels, range(height - band_rows, height), every_col),
        "left": band_stats(rows, channels, every_row, range(0, side_cols)),
        "right": band_stats(rows, channels, every_row, range(width - side_cols, width)),
    }

    failures = []
    for name, stats in bands.items():
        if stats["distinct_colours"] <= FLAT_BAND_DISTINCT_COLOURS and \
                stats["modal_share"] >= FLAT_BAND_MODAL_SHARE:
            failures.append(
                f"{name} band is one flat colour ({stats['top_colours'][0]['rgb']}) - "
                f"nothing was drawn there, so the arena picture does not reach that edge")
        elif stats["distinct_colours"] < MIN_DISTINCT_COLOURS:
            failures.append(
                f"{name} band has only {stats['distinct_colours']} distinct colours - "
                f"too flat to be a picture reaching that edge")

    return {
        "file": path.name,
        "width": width,
        "height": height,
        "bands": bands,
        "failures": failures,
    }


def parse_manifest(folder):
    """Read the arena numbers the game recorded, if the manifest is there."""
    manifest = folder / "manifest.txt"
    if not manifest.is_file():
        return None, ["manifest.txt is missing, so the arena's own numbers are unavailable"]

    text = manifest.read_text(encoding="utf-8", errors="replace")
    lines = [line for line in text.splitlines() if line.strip().startswith("arena:")]
    if not lines:
        return None, ["the manifest has no 'arena:' line - this run was made by a build "
                      "without the arena framing record"]

    failures = []
    for line in lines:
        if "imported image in use" not in line:
            failures.append(f"the manifest does not say the imported image is in use: {line.strip()}")
        if "procedural greybox ring hidden" not in line:
            failures.append(f"the procedural greybox ring was not hidden: {line.strip()}")

    return {"count": len(lines), "first": lines[0].strip(), "failures": failures}, failures


def parse_arena_numbers(record):
    """Pull the numbers back out of the manifest line and re-check the arithmetic."""
    import re

    if record is None:
        return None

    text = record["first"]
    patterns = {
        "picture_aspect": r"aspect ([0-9.]+), source",
        "source_aspect": r"source 1672 x 941 = ([0-9.]+)",
        "view_aspect": r"framed for view aspect ([0-9.]+)",
        "feet": r"feet at ([+-][0-9.]+) of the half-frame",
        "floor_line": r"picture floor line at ([+-][0-9.]+)",
        "cover_margin": r"narrowest cover margin (-?[0-9.]+) cm",
    }
    numbers = {}
    for key, pattern in patterns.items():
        match = re.search(pattern, text)
        if match is None:
            return {"error": f"could not read '{key}' from the manifest line"}
        numbers[key] = float(match.group(1))

    return numbers


def check_numbers(numbers):
    failures = []
    if not numbers or "error" in numbers:
        return ["the manifest's arena numbers could not be read: "
                f"{numbers.get('error') if numbers else 'nothing to read'}"]

    if abs(numbers["picture_aspect"] - numbers["source_aspect"]) > ASPECT_TOLERANCE:
        failures.append(
            f"the picture's aspect ratio is {numbers['picture_aspect']:.4f}, not the "
            f"source image's {numbers['source_aspect']:.4f} - it was not preserved")
    if abs(numbers["source_aspect"] - SOURCE_IMAGE_ASPECT) > ASPECT_TOLERANCE:
        failures.append("the source image's aspect ratio is not 1672x941 any more")

    delta = abs(numbers["feet"] - numbers["floor_line"])
    if delta > ALIGNMENT_TOLERANCE:
        failures.append(
            f"the fighters' feet ({numbers['feet']:+.4f}) and the picture's painted floor "
            f"line ({numbers['floor_line']:+.4f}) are on different screen lines "
            f"(delta {delta:.4f}) - the fighters are not standing on the painted floor")

    if numbers["cover_margin"] <= 0.0:
        failures.append(
            f"the picture falls {abs(numbers['cover_margin']):.0f} cm short of the frame on "
            f"its tightest edge - there is empty space beside or below the image")

    return failures


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2

    folder = Path(argv[1])
    if not folder.is_dir():
        print(f"no such folder: {folder}")
        return 2

    band_fraction = DEFAULT_BAND_FRACTION
    side_fraction = DEFAULT_SIDE_FRACTION
    if "--band" in argv:
        band_fraction = float(argv[argv.index("--band") + 1])
    if "--side" in argv:
        side_fraction = float(argv[argv.index("--side") + 1])

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
            stats = analyse_bands(shot, band_fraction, side_fraction)
        except Exception as exc:  # noqa: BLE001 - reported, never swallowed
            report["failed"].append(shot.name)
            print(f"FAIL  {shot.name}: could not be read as a PNG ({exc})")
            continue

        report["shots"].append(stats)
        if stats["failures"]:
            report["failed"].append(shot.name)

        bands = stats["bands"]
        mark = "PASS" if not stats["failures"] else "FAIL"
        print(f"{mark}  {stats['file']:<20} {stats['width']}x{stats['height']}  "
              f"colours top {bands['top']['distinct_colours']:>4}  "
              f"bottom {bands['bottom']['distinct_colours']:>4}  "
              f"left {bands['left']['distinct_colours']:>4}  "
              f"right {bands['right']['distinct_colours']:>4}")
        for failure in stats["failures"]:
            print(f"        - {failure}")

    print()
    record, manifest_failures = parse_manifest(folder)
    report["manifest"] = record
    numbers = parse_arena_numbers(record)
    report["arena_numbers"] = numbers
    number_failures = check_numbers(numbers)
    report["number_failures"] = number_failures + manifest_failures

    if record is not None:
        print(f"manifest arena records: {record['count']}")
        print(f"  {record['first']}")
    if numbers and "error" not in numbers:
        print(f"  picture aspect {numbers['picture_aspect']:.4f} "
              f"(source {numbers['source_aspect']:.4f}), view aspect {numbers['view_aspect']:.4f}")
        print(f"  feet {numbers['feet']:+.4f}, painted floor line {numbers['floor_line']:+.4f}, "
              f"delta {abs(numbers['feet'] - numbers['floor_line']):.5f}")
        print(f"  narrowest cover margin {numbers['cover_margin']:.0f} cm")
    for failure in report["number_failures"]:
        print(f"  - {failure}")

    ok = not report["failed"] and not report["number_failures"]
    print(f"\n{len(shots) - len(report['failed'])} of {len(shots)} screenshot(s) reached every "
          f"edge of the frame; overall {'PASS' if ok else 'FAIL'}")
    print("Remember: this rules out the mechanical failures only. Open the pictures and check "
          "that the arena is the approved artwork, the right way up, with both fighters "
          "standing on its painted floor.")
    report["overall"] = "PASS" if ok else "FAIL"

    if json_out is not None:
        json_out.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(f"report written: {json_out}")

    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
