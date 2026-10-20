#!/usr/bin/env python3
"""
rkc_alpha_hist.py - Characterise the alpha matte of the supplied sprite pack.

Purpose: decide between a MASKED (opacity mask, thresholded, z-writable) and a
TRANSLUCENT (alpha blended, sort-dependent) sprite material on measured evidence
rather than assumption.

Outputs a coarse 16-bin alpha histogram plus per-frame "core" statistics so we can
tell the difference between:
  (a) crisp art with antialiased edges  -> bimodal alpha, big a>=254 core, few mid values
  (b) soft airbrush / fur art           -> broad alpha spread, small opaque core
  (c) background-keyed (JPEG->PNG) art  -> large mid-band haze across the silhouette
"""

import json
import os
import struct
import sys
import zlib
from collections import Counter

PACK = r"C:\RockyKombat\ArtSource\RockyKombatPack_v1.0"
OUT = r"C:\RockyKombat\Saved\Evidence\Alpha_hist.json"

# ---------------------------------------------------------------- PNG decoding
def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def read_png_alpha(path):
    """Return (w, h, alpha_bytes) for an 8-bit RGBA/greyscale+alpha PNG."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")

    pos = 8
    w = h = bit_depth = colour_type = None
    idat = bytearray()
    palette = None
    trns = None
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        ctype = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if ctype == b"IHDR":
            w, h, bit_depth, colour_type, comp, filt, interlace = struct.unpack(">IIBBBBB", chunk)
            if interlace:
                raise ValueError("interlaced PNG unsupported")
        elif ctype == b"PLTE":
            palette = chunk
        elif ctype == b"tRNS":
            trns = chunk
        elif ctype == b"IDAT":
            idat += chunk
        elif ctype == b"IEND":
            break

    if bit_depth != 8:
        raise ValueError("bit depth %d unsupported" % bit_depth)

    raw = zlib.decompress(bytes(idat))
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[colour_type]
    stride = w * channels
    out = bytearray(stride * h)
    prev = bytearray(stride)

    p = 0
    for y in range(h):
        ftype = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if ftype == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif ftype == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ftype == 3:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif ftype == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                c = prev[i - channels] if i >= channels else 0
                line[i] = (line[i] + _paeth(a, prev[i], c)) & 0xFF
        out[y * stride:(y + 1) * stride] = line
        prev = line

    # derive alpha plane
    if colour_type == 6:
        alpha = bytes(out[3::4])
    elif colour_type == 4:
        alpha = bytes(out[1::2])
    elif colour_type == 3:
        # palette + optional tRNS
        n = len(palette) // 3
        table = bytearray([255] * (n + 1))
        if trns:
            for i, v in enumerate(trns[:n]):
                table[i] = v
        alpha = bytes(table[px] for px in out)
    elif colour_type == 0:
        alpha = bytes([255]) * (w * h)
    else:  # RGB, no alpha
        alpha = bytes([255]) * (w * h)
    return w, h, alpha


# ------------------------------------------------------------------ inspection
def main():
    # manifest-driven frame list
    manifest_path = os.path.join(PACK, "asset_manifest.json")
    frames = []
    if os.path.exists(manifest_path):
        with open(manifest_path, "r", encoding="utf-8") as f:
            man = json.load(f)
        # walk manifest for .png entries, falling back to disk scan
        def walk(node):
            if isinstance(node, dict):
                for k, v in node.items():
                    if isinstance(v, str) and v.lower().endswith(".png"):
                        frames.append(v)
                    else:
                        walk(v)
            elif isinstance(node, list):
                for v in node:
                    walk(v)
        walk(man)

    if not frames:
        for root, _dirs, files in os.walk(os.path.join(PACK, "Characters")):
            for fn in sorted(files):
                if fn.lower().endswith(".png"):
                    frames.append(os.path.relpath(os.path.join(root, fn), PACK).replace("\\", "/"))

    frames = sorted(set(f.replace("\\", "/") for f in frames))

    hist = [0] * 16                    # 16 buckets of 16 alpha levels
    total_px = 0
    per_frame = []
    opaque_core = 0
    edge_band = 0
    haze_band = 0

    for rel in frames:
        path = os.path.join(PACK, rel.replace("/", os.sep))
        if not os.path.exists(path):
            per_frame.append({"file": rel, "error": "missing"})
            continue
        w, h, alpha = read_png_alpha(path)
        local = Counter(alpha)
        n_vis = 0
        n_opaque = 0
        n_edge = 0          # 200..254  (antialias / fur tip)
        n_haze = 0          # 1..84     (needs a low mask threshold or translucent)
        for v, c in local.items():
            if v == 0:
                continue
            n_vis += c
            hist[v >> 4] += c
            total_px += c
            if v >= 254:
                n_opaque += 1 * c
            if 200 <= v <= 253:
                n_edge += c
            if 1 <= v <= 84:
                n_haze += c
        opaque_core += n_opaque
        edge_band += n_edge
        haze_band += n_haze
        per_frame.append({
            "file": rel, "w": w, "h": h,
            "visible": n_vis, "opaque": n_opaque,
            "edge_200_253": n_edge, "haze_1_84": n_haze,
            "core_ratio": round(n_opaque / n_vis, 4) if n_vis else 0.0,
        })

    vis = sum(v for v in hist)
    report = {
        "pack": PACK,
        "frames": len(frames),
        "visible_px": vis,
        "alpha_histogram_16bins": hist,
        "histogram_pct": [round(100.0 * v / vis, 3) if vis else 0 for v in hist],
        "opaque_ge254": opaque_core,
        "opaque_pct_of_visible": round(100.0 * opaque_core / vis, 3) if vis else 0,
        "edge_200_253_pct": round(100.0 * edge_band / vis, 3) if vis else 0,
        "haze_1_84_pct": round(100.0 * haze_band / vis, 3) if vis else 0,
        "per_frame": per_frame,
    }

    # decision rules, evidence driven
    opaque_pct = report["opaque_pct_of_visible"]
    haze_pct = report["haze_1_84_pct"]
    if opaque_pct >= 45.0 and haze_pct <= 12.0:
        verdict = "MASKED_SUPPORTED"
        rationale = ("A solid majority of visible pixels are fully opaque (>=254) and only a "
                     "small minority sit in the low-alpha haze band, so an opacity mask with a "
                     "low threshold keeps the silhouette solid and gains early-z depth writes.")
    elif haze_pct >= 25.0 or opaque_pct < 20.0:
        verdict = "TRANSLUCENT_REQUIRED"
        rationale = ("The silhouette has little or no fully-opaque core and a large low-alpha "
                     "haze band, so masking would carve visible holes out of the art; blended "
                     "translucency is required and draw order must be controlled explicitly.")
    else:
        verdict = "EITHER_WITH_TUNING"
        rationale = ("Alpha is spread across the mid band, so both work: masked with a low "
                     "(~0.06) threshold keeps feet/hands solid, translucent keeps gradients. "
                     "Masked is preferred for depth writes if edge erosion is acceptable.")

    report["verdict"] = verdict
    report["rationale"] = rationale

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=1)

    print("=" * 68)
    print("ALPHA HISTOGRAM (16 bins, 16 alpha levels each, %% of visible px)")
    print("=" * 68)
    for i, pct in enumerate(report["histogram_pct"]):
        lo = i * 16
        hi = lo + 15 if i < 15 else 255
        bar = "#" * int(pct / 2)
        print("  a=%3d-%3d  %7.3f%%  %s" % (lo, hi, pct, bar))
    print()
    print("visible px            : %d" % vis)
    print("fully opaque (>=254)  : %d  (%.3f%% of visible)" % (opaque_core, opaque_pct))
    print("edge band 200-253     : %.3f%%" % report["edge_200_253_pct"])
    print("haze band   1-84      : %.3f%%" % haze_pct)
    print("VERDICT               : %s" % verdict)
    print("rationale             : %s" % rationale)
    print("report                : %s" % OUT)
    print("=" * 68)
    return 0


if __name__ == "__main__":
    sys.exit(main())
