#!/usr/bin/env python3
"""Task-local diagnostic (throwaway, not part of the deliverable).

Re-implements rkc_arena_verify.py's M2 "flat saturated" predicate exactly
(saturated: max-min >= 90 AND identical to all four neighbours) and reports
WHERE those pixels sit: per 5% row band, plus the bounding boxes of the
connected clusters, plus the same for near-black pixels (M3's BLACK_MAX luma).

Purpose: decide whether the M2/M3 FAILs on the packaged match frame are driven
by the arena, by HUD elements, or by a genuine void band.
"""
import json
import struct
import sys
import zlib

SAT_MIN = 90
BLACK_MAX = 12


def decode_png(path):
    with open(path, "rb") as fh:
        data = fh.read()
    pos, idat = 8, bytearray()
    w = h = bd = ct = None
    while pos < len(data):
        ln = struct.unpack(">I", data[pos:pos + 4])[0]
        typ = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + ln]
        if typ == b"IHDR":
            w, h, bd, ct = struct.unpack(">IIBB", chunk[:10])
        elif typ == b"IDAT":
            idat += chunk
        elif typ == b"IEND":
            break
        pos += 12 + ln
    if bd != 8 or ct not in (0, 2, 4, 6):
        raise SystemExit("unsupported PNG bd=%s ct=%s" % (bd, ct))
    ch = {0: 1, 2: 3, 4: 2, 6: 4}[ct]
    raw = zlib.decompress(bytes(idat))
    stride = w * ch
    rows = []
    prev = bytearray(stride)
    p = 0
    for _ in range(h):
        ft = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if ft == 1:
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
                b = prev[i]
                c = prev[i - ch] if i >= ch else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        rows.append(bytes(line))
        prev = line
    return w, h, ch, rows


def rgb(rows, ch, x, y):
    o = x * ch
    r = rows[y][o]
    if ch <= 2:
        return r, r, r
    return r, rows[y][o + 1], rows[y][o + 2]


def luma(px):
    r, g, b = px
    return (r * 299 + g * 587 + b * 114) // 1000


def analyse(path):
    w, h, ch, rows = decode_png(path)
    band_y0, band_y1 = int(h * 0.12), int(h * 0.82)
    step = max(1, h // 20)
    fs_bands = {}
    dk_bands = {}
    fs_pts = []
    dk_pts = []
    band_total = 0
    fs_total = 0
    for y in range(h):
        for x in range(w):
            here = rgb(rows, ch, x, y)
            lum = luma(here)
            rb = (y // step) * step
            if lum < BLACK_MAX:
                dk_bands[rb] = dk_bands.get(rb, 0) + 1
                dk_pts.append((x, y))
            if band_y0 <= y < band_y1 and 0 < x < w - 1 and 0 < y < h - 1:
                band_total += 1
                mx, mn = max(here), min(here)
                if mx - mn >= SAT_MIN:
                    if (rgb(rows, ch, x - 1, y) == here and rgb(rows, ch, x + 1, y) == here
                            and rgb(rows, ch, x, y - 1) == here and rgb(rows, ch, x, y + 1) == here):
                        fs_total += 1
                        fs_bands[rb] = fs_bands.get(rb, 0) + 1
                        fs_pts.append((x, y))
    def bbox(pts):
        if not pts:
            return None
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        return {"x": [min(xs), max(xs)], "y": [min(ys), max(ys)], "n": len(pts)}
    out = {
        "path": path, "width": w, "height": h,
        "band_rows": "%d-%d" % (band_y0, band_y1),
        "M2_flat_sat_fraction": round(fs_total / float(band_total), 6),
        "M2_flat_sat_pixels": fs_total,
        "M2_flat_sat_by_rowband": {("%d-%d" % (k, k + step - 1)): v
                                   for k, v in sorted(fs_bands.items())},
        "M2_flat_sat_bbox": bbox(fs_pts),
        "M3_bottom_band_rows": "%d-%d" % (int(h * 0.70), int(h * 0.94)),
        "M3_bottom_band_dark_fraction": round(
            sum(v for k, v in dk_bands.items()
                if int(h * 0.70) <= k < int(h * 0.94)) / float((int(h * 0.94) - int(h * 0.70)) * w), 5),
        "M3_near_black_by_rowband": {("%d-%d" % (k, k + step - 1)): v
                                     for k, v in sorted(dk_bands.items())},
        "M3_near_black_bbox": bbox(dk_pts),
    }
    return out


if __name__ == "__main__":
    for path in sys.argv[1:]:
        print(json.dumps(analyse(path), indent=2))
        print()
