#!/usr/bin/env python3
"""
rkc_arena_verify.py - offline pixel checks for the Rocky Kombat "one visible arena" rule.

Standard library only (no Pillow/numpy). Decodes 8-bit, non-interlaced PNGs of
colour type 0/2/4/6 (grey, RGB, grey+alpha, RGBA), which covers UE screenshots
and the supplied arena artwork.

WHAT THIS PROVES AND WHAT IT DOES NOT
  It measures whether the captured frames contain the visual signature of an
  untextured procedural greybox ring drawn on top of / under the supplied
  Nighttime Rooftop artwork. It is a heuristic screen, not a semantic image
  understanding. A PASS means "no duplicate-ring signature found in the
  captures supplied"; the human visual comparison against the artwork remains
  mandatory for sign-off.

METRICS
  M1 liveness          frame is neither black nor blown out
  M2 flat_sat_arena    fraction of flat, highly saturated pixels in the arena
                       band. Untextured greybox primitives (ropes, posts,
                       turnbuckles, apron, canvas, ringside floor) are exact
                       colour fills; painted artwork is textured and scores far
                       lower. This is the duplicate-ring probe.
                       Rows whose flat fills span most of the frame width are
                       HUD overlays (health/stamina bars, plate fills), not
                       arena geometry. They appear identically in the pre-fix
                       control, so they are excluded from the arena figure and
                       reported separately as flat_sat_hud_fraction / hud_rows.
  M3 bottom_band_dark  fraction of near-black pixels below the fighters' feet
                       line, combined with the texture count in the same band.
                       An empty band is dark AND uniform; a painted night floor
                       is dark but richly textured, so M3 fails only when both
                       signatures are present (see M4). This avoids flagging the
                       legitimately dark Midnight Rooftop floor, which measures
                       a high near-black fraction in the supplied artwork too.
  M4 bottom_band_tex   distinct colours in the same band; a flat single-colour
                       band indicates filler geometry or an empty gap.

USAGE
  python rkc_arena_verify.py ^
      --art "ArtSource/RockyKombatPack_v1.0/Arena/Midnight_Rooftop_Ring.png" ^
      --control "Saved/Evidence/Stages/20261017_235858/05_match.png" ^
      --shot "Saved/Evidence/Arena/round_start.png" ^
      --shot "Saved/Evidence/Arena/gameplay.png" ^
      --out "Saved/Evidence/Arena/arena_verify_report.json"

  Or point it at a whole capture folder (recommended; this is what the harness
  does), which checks every *.png in it in one pass:

  python rkc_arena_verify.py ^
      --art "ArtSource/RockyKombatPack_v1.0/Arena/Midnight_Rooftop_Ring.png" ^
      --control "Saved/Evidence/Stages/20261017_235858/05_match.png" ^
      --shotdir "Saved/Evidence/Arena" ^
      --out "Saved/Evidence/Arena/arena_verify_report.json"

  Exit code is 0 when every frame passes M1..M4, 1 when any frame fails, 2 on a
  usage/reference error, so it can gate a script.

  --control is a KNOWN PRE-FIX capture that still shows the procedural ring. When
  supplied, M2 becomes a relative test (new frames must drop >=65% against it),
  which is far more sensitive than any absolute threshold.
"""

import argparse
import glob
import json
import os
import struct
import sys
import zlib

# ---------------------------------------------------------------- PNG decoding

_CHANNELS = {0: 1, 2: 3, 4: 2, 6: 4}


def _paeth(a, b, c):
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def decode_png(path):
    """Return (width, height, channels, rows) for an 8-bit non-interlaced PNG."""
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s: not a PNG file" % path)

    pos = 8
    idat = bytearray()
    width = height = bitdepth = colourtype = interlace = None
    while pos + 8 <= len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        ctype = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if ctype == b"IHDR":
            width, height, bitdepth, colourtype, _comp, _filt, interlace = struct.unpack(
                ">IIBBBBB", body)
        elif ctype == b"IDAT":
            idat += body
        elif ctype == b"IEND":
            break

    if width is None:
        raise ValueError("%s: missing IHDR" % path)
    if bitdepth != 8:
        raise ValueError("%s: only 8-bit PNGs are supported (got %d)" % (path, bitdepth))
    if interlace != 0:
        raise ValueError("%s: interlaced PNGs are not supported" % path)
    if colourtype not in _CHANNELS:
        raise ValueError("%s: unsupported colour type %d (palette PNGs are not "
                         "supported; re-export as RGB/RGBA)" % (path, colourtype))

    ch = _CHANNELS[colourtype]
    raw = zlib.decompress(bytes(idat))
    stride = width * ch
    expected = height * (stride + 1)
    if len(raw) < expected:
        raise ValueError("%s: truncated image data (%d of %d bytes)"
                         % (path, len(raw), expected))

    rows = []
    prev = bytearray(stride)
    p = 0
    for _y in range(height):
        ft = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if ft == 0:
            pass
        elif ft == 1:
            for i in range(ch, stride):
                line[i] = (line[i] + line[i - ch]) & 0xFF
        elif ft == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ft == 3:
            for i in range(stride):
                a = line[i - ch] if i >= ch else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif ft == 4:
            for i in range(stride):
                a = line[i - ch] if i >= ch else 0
                c = prev[i - ch] if i >= ch else 0
                line[i] = (line[i] + _paeth(a, prev[i], c)) & 0xFF
        else:
            raise ValueError("%s: bad filter type %d on row %d" % (path, ft, _y))
        rows.append(line)
        prev = line

    return width, height, ch, rows


def pixel(rows, ch, x, y):
    o = x * ch
    r = rows[y][o]
    if ch == 1 or ch == 2:
        return r, r, r
    return r, rows[y][o + 1], rows[y][o + 2]


def luma(px):
    r, g, b = px
    return (r * 299 + g * 587 + b * 114) // 1000


# -------------------------------------------------------------------- metrics

def metrics(path):
    w, h, ch, rows = decode_png(path)

    total = w * h
    sum_luma = 0
    near_black = 0

    band_y0 = int(h * 0.12)
    band_y1 = int(h * 0.82)
    band_total = 0
    flat_sat = 0
    row_fs = {}           # flat-saturated pixel count per absolute row

    low_y0 = int(h * 0.70)
    low_y1 = max(low_y0 + 1, int(h * 0.94))
    low_total = 0
    low_black = 0
    low_colours = set()

    SAT_MIN = 90          # max(channel) - min(channel)
    BLACK_MAX = 12        # luma of a "black bar" pixel

    for y in range(h):
        row = rows[y]
        for x in range(w):
            o = x * ch
            r = row[o]
            if ch == 1 or ch == 2:
                g = b = r
            else:
                g = row[o + 1]
                b = row[o + 2]
            lum = (r * 299 + g * 587 + b * 114) // 1000
            sum_luma += lum
            if lum < BLACK_MAX:
                near_black += 1

            if low_y0 <= y < low_y1:
                low_total += 1
                if lum < BLACK_MAX:
                    low_black += 1
                if len(low_colours) < 4096:
                    low_colours.add((r, g, b))

            if band_y0 <= y < band_y1 and 0 < x < w - 1 and 0 < y < h - 1:
                band_total += 1
                mx = r if r >= g and r >= b else (g if g >= b else b)
                mn = r if r <= g and r <= b else (g if g <= b else b)
                if mx - mn >= SAT_MIN:
                    # exact flat fill: identical to the 4-neighbourhood.
                    # pixel() keeps this correct for grey/RGB/RGBA inputs.
                    here = (r, g, b)
                    if (pixel(rows, ch, x - 1, y) == here
                            and pixel(rows, ch, x + 1, y) == here
                            and pixel(rows, ch, x, y - 1) == here
                            and pixel(rows, ch, x, y + 1) == here):
                        flat_sat += 1
                        row_fs[y] = row_fs.get(y, 0) + 1

    # HUD rows: a solid saturated fill running across most of the width is a
    # health/stamina bar or plate fill, not arena geometry.
    hud_rows = sorted(y for y, n in row_fs.items()
                      if n >= HUD_ROW_MIN_WIDTH_FRACTION * w)
    hud_flat_sat = sum(row_fs[y] for y in hud_rows)
    hud_band_pixels = len([y for y in hud_rows if band_y0 <= y < band_y1]) * max(0, w - 2)
    arena_flat_sat = flat_sat - hud_flat_sat
    arena_band_total = max(1, band_total - hud_band_pixels)

    return {
        "path": path,
        "width": w,
        "height": h,
        "aspect": round(w / float(h), 4),
        "mean_luma": round(sum_luma / float(total), 2),
        "near_black_fraction": round(near_black / float(total), 5),
        "flat_sat_arena_fraction": round(flat_sat / float(band_total), 6),
        "flat_sat_hud_fraction": round(hud_flat_sat / float(band_total), 6),
        "hud_row_count": len(hud_rows),
        "hud_row_range": [hud_rows[0], hud_rows[-1]] if hud_rows else None,
        "flat_sat_arena_excl_hud": round(arena_flat_sat / float(arena_band_total), 6),
        "bottom_band_dark_fraction": round(low_black / float(low_total), 5),
        "bottom_band_distinct_colours": len(low_colours),
    }


# --------------------------------------------------------------------- checks

SUPPORTED_ASPECTS = [("4:3", 4 / 3.0), ("16:9", 16 / 9.0), ("16:10", 1.6),
                     ("3:2", 1.5), ("9:16", 9 / 16.0), ("3:4", 0.75),
                     ("16:9 portrait-ish 0.5625", 0.5625)]

M2_RELATIVE_DROP = 0.65   # new frame must be <= 35% of the pre-fix control
M2_ABSOLUTE_MAX = 0.02    # used only when no control capture is supplied
M3_MAX_DARK = 0.35
M4_MIN_COLOURS = 150
HUD_ROW_MIN_WIDTH_FRACTION = 0.30   # a row this full of one flat fill is a HUD bar


def named_aspect(a):
    for name, val in SUPPORTED_ASPECTS:
        if abs(a - val) / val <= 0.02:
            return name
    return "unsupported/odd"


def main():
    ap = argparse.ArgumentParser(description="Rocky Kombat arena screenshot checks")
    ap.add_argument("--art", help="supplied Midnight_Rooftop_Ring.png (reference)")
    ap.add_argument("--control", help="known pre-fix capture that still shows the greybox ring")
    ap.add_argument("--shot", action="append", default=[],
                    help="captured frame to check; repeat for round start, gameplay, results, rematch")
    ap.add_argument("--shotdir", action="append", default=[],
                    help="folder of captured PNGs to check; repeatable")
    ap.add_argument("--out", default=None, help="write the JSON report here")
    args = ap.parse_args()

    # ---- collect captures from --shot and --shotdir, without double-counting the
    # reference artwork or the pre-fix control capture.
    exclude = set()
    for ref_path in (args.art, args.control):
        if ref_path:
            exclude.add(os.path.normcase(os.path.abspath(ref_path)))

    shots = []
    for path in args.shot:
        shots.append(path)
    for folder in args.shotdir:
        if not os.path.isdir(folder):
            print("ERROR: --shotdir not found: %s" % folder, file=sys.stderr)
            return 2
        for path in sorted(glob.glob(os.path.join(folder, "*.png"))):
            if os.path.normcase(os.path.abspath(path)) not in exclude:
                shots.append(path)
    args.shot = shots

    if not args.shot:
        print("ERROR: supply at least one --shot capture or a populated --shotdir",
              file=sys.stderr)
        return 2

    report = {"art": None, "control": None, "shots": [], "checks": [],
              "verdict": "UNKNOWN"}

    ref = None
    if args.art:
        if not os.path.isfile(args.art):
            print("ERROR: --art not found: %s" % args.art, file=sys.stderr)
            return 2
        ref = metrics(args.art)
        report["art"] = ref
        ref["supported_aspect"] = named_aspect(ref["aspect"])

    ctl = None
    if args.control:
        if not os.path.isfile(args.control):
            print("ERROR: --control not found: %s" % args.control, file=sys.stderr)
            return 2
        ctl = metrics(args.control)
        report["control"] = ctl

    failed = 0
    for shot in args.shot:
        if not os.path.isfile(shot):
            print("ERROR: --shot not found: %s" % shot, file=sys.stderr)
            return 2
        m = metrics(shot)
        m["supported_aspect"] = named_aspect(m["aspect"])
        m["checks"] = []

        def rec(name, ok, detail):
            m["checks"].append({"check": name, "result": "PASS" if ok else "FAIL",
                                "detail": detail})
            return ok

        c1 = rec("M1_frame_is_rendered", 6.0 <= m["mean_luma"] <= 245.0,
                 "mean_luma=%.2f (expect 6..245; 0 => black render)" % m["mean_luma"])

        if ctl is not None:
            # Compare the arena itself: the HUD bars are in both frames, so
            # leaving them in makes the relative drop unreachable.
            limit = ctl["flat_sat_arena_excl_hud"] * (1.0 - M2_RELATIVE_DROP)
            c2 = rec("M2_no_duplicate_ring_vs_control",
                     m["flat_sat_arena_excl_hud"] <= limit,
                     "arena flat_sat=%.6f (HUD rows %s excluded), control arena=%.6f, "
                     "limit=%.6f (>=%.0f%% reduction required); raw flat_sat=%.6f of which "
                     "HUD=%.6f"
                     % (m["flat_sat_arena_excl_hud"], m["hud_row_range"],
                        ctl["flat_sat_arena_excl_hud"], limit, M2_RELATIVE_DROP * 100,
                        m["flat_sat_arena_fraction"], m["flat_sat_hud_fraction"]))
        else:
            c2 = rec("M2_no_duplicate_ring_absolute",
                     m["flat_sat_arena_excl_hud"] <= M2_ABSOLUTE_MAX,
                     "arena flat_sat=%.6f, limit=%.6f (no --control supplied: relative test "
                     "skipped, absolute screen is far weaker - supply the pre-fix capture)"
                     % (m["flat_sat_arena_excl_hud"], M2_ABSOLUTE_MAX))

        # A void band is dark AND uniform. A dark but richly textured band is a
        # painted floor (this artwork's night floor is legitimately dark), so the
        # empty-band signature needs both conditions.
        band_dark = m["bottom_band_dark_fraction"] > M3_MAX_DARK
        band_uniform = m["bottom_band_distinct_colours"] < M4_MIN_COLOURS
        if band_dark and band_uniform:
            m3_why = "VOID BAND: dark and uniform"
        elif band_dark:
            m3_why = ("dark but textured (%d colours) -> painted night floor, not a void band"
                      % m["bottom_band_distinct_colours"])
        else:
            m3_why = "not dark"
        c3 = rec("M3_no_empty_band_below_feet",
                 not (band_dark and band_uniform),
                 "near_black in rows 70-94%% = %.5f (limit %.2f), distinct colours = %d "
                 "(void needs < %d): %s"
                 % (m["bottom_band_dark_fraction"], M3_MAX_DARK,
                    m["bottom_band_distinct_colours"], M4_MIN_COLOURS, m3_why))

        c4 = rec("M4_bottom_band_has_texture",
                 m["bottom_band_distinct_colours"] >= M4_MIN_COLOURS,
                 "distinct colours in rows 70-94%% = %d, minimum=%d"
                 % (m["bottom_band_distinct_colours"], M4_MIN_COLOURS))

        m["all_script_checks_pass"] = all([c1, c2, c3, c4])
        if not m["all_script_checks_pass"]:
            failed += 1
        report["shots"].append(m)
        report["checks"].append({"file": shot,
                                 "result": "PASS" if m["all_script_checks_pass"] else "FAIL"})

    report["verdict"] = ("SCRIPT CHECKS PASS - human visual comparison against the "
                         "artwork is still required before sign-off"
                         if failed == 0 else
                         "SCRIPT CHECKS FAIL (%d frame(s))" % failed)

    # ---- human readable summary
    print("=" * 78)
    print("Rocky Kombat - single-arena screenshot screen")
    print("=" * 78)
    if ref:
        print("reference artwork : %s" % ref["path"])
        print("                    %dx%d  aspect %.4f (%s)  flat_sat=%.6f  "
              "bottom_band_dark=%.5f"
              % (ref["width"], ref["height"], ref["aspect"], ref["supported_aspect"],
                 ref["flat_sat_arena_fraction"], ref["bottom_band_dark_fraction"]))
    if ctl:
        print("pre-fix control   : %s" % ctl["path"])
        print("                    %dx%d  flat_sat=%.6f  (this frame is known to contain "
              "the procedural ring)" % (ctl["width"], ctl["height"],
                                        ctl["flat_sat_arena_fraction"]))
    for m in report["shots"]:
        print("-" * 78)
        print("%s  %dx%d  aspect %.4f (%s)"
              % (m["path"], m["width"], m["height"], m["aspect"], m["supported_aspect"]))
        for c in m["checks"]:
            print("  [%s] %-34s %s" % (c["result"], c["check"], c["detail"]))
    print("=" * 78)
    print(report["verdict"])
    print("Reminder: this screen only looks for the greybox colour signature. It cannot")
    print("confirm that the visible ring IS the supplied artwork, that both fighters'")
    print("feet sit on the painted floor, or that no second ring appears in stages you")
    print("did not capture. Those are human checks (docs/08-playtest-checklist.md).")
    print("=" * 78)

    if args.out:
        d = os.path.dirname(os.path.abspath(args.out))
        if d and not os.path.isdir(d):
            os.makedirs(d)
        with open(args.out, "w") as fh:
            json.dump(report, fh, indent=2, sort_keys=True)
        print("report written: %s" % args.out)

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
