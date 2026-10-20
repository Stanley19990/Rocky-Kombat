"""
rkc_bbox_analyze.py - per-frame alpha silhouette measurement for the Rocky Kombat pack.

Answers three questions that the importer and the fighter wiring depend on:

  1. Is the documented pivot (192, 300) actually on the feet line, or does the
     artwork sit higher/lower on its 384x320 canvas?
  2. How tall is the standing silhouette in pixels? (the pack claims ~200 px, and
     texture scale is chosen from the measurement)
  3. Which Shadow Dodge frame is the low fallen pose that START_HERE.txt warns about,
     so a safe subset can be built and the substitution recorded.

Silhouette threshold is alpha > 8 (alpha=1..8 is haze that the pack itself reports).
Writes Saved/Evidence/BBox_analysis.json.
"""

import json
import os
import sys

from PIL import Image

PACK = "C:/RockyKombat/ArtSource/RockyKombatPack_v1.0"
OUT = "C:/RockyKombat/Saved/Evidence/BBox_analysis.json"
ACTIONS = ["Idle", "Walk", "Jab", "HeavyPunch", "Block", "Dodge", "Knockdown", "Victory"]
CHARS = ["Rocky", "Shadow"]
ALPHA_GATE = 8  # texture-space gate; everything below this is pack-reported haze


def measure(path):
    with Image.open(path) as im:
        im = im.convert("RGBA")
        w, h = im.size
        alpha = im.getchannel("A")
        bbox = alpha.point(lambda v: 255 if v > ALPHA_GATE else 0).getbbox()
    if bbox is None:
        return {"w": w, "h": h, "empty": True}
    x0, y0, x1, y1 = bbox
    return {
        "w": w,
        "h": h,
        "empty": False,
        "bbox": [x0, y0, x1, y1],
        "sil_w": x1 - x0,
        "sil_h": y1 - y0,
        "feet_y": y1,          # exclusive bottom row of the silhouette
        "head_y": y0,
        "center_x": (x0 + x1) / 2.0,
        "center_dx_from_pivot": ((x0 + x1) / 2.0) - (w / 2.0),
        "feet_dy_from_pivot": y1 - 300,
    }


def main():
    if not os.path.isdir(PACK):
        print("pack not found: %s" % PACK)
        return 2

    report = {"pack": PACK, "alpha_gate": ALPHA_GATE, "actions": {}, "findings": {}}

    for ch in CHARS:
        report["actions"][ch] = {}
        for act in ACTIONS:
            d = os.path.join(PACK, "Characters", ch, act)
            if not os.path.isdir(d):
                report["actions"][ch][act] = {"error": "missing dir %s" % d}
                continue
            files = sorted(f for f in os.listdir(d) if f.lower().endswith(".png"))
            rows = []
            for f in files:
                m = measure(os.path.join(d, f))
                m["file"] = "%s/%s/%s" % (ch, act, f)
                rows.append(m)
            tall = [r["sil_h"] for r in rows if not r.get("empty")]
            report["actions"][ch][act] = {
                "frames": rows,
                "count": len(rows),
                "sil_h_min": min(tall) if tall else None,
                "sil_h_max": max(tall) if tall else None,
                "feet_y_min": min(r["feet_y"] for r in rows if not r.get("empty")),
                "feet_y_max": max(r["feet_y"] for r in rows if not r.get("empty")),
            }

    # ---- question 3: which Shadow Dodge frame is the fallen pose? -----------
    dodge = report["actions"]["Shadow"].get("Dodge", {}).get("frames", [])
    if dodge:
        heights = [r["sil_h"] for r in dodge if not r.get("empty")]
        med = sorted(heights)[len(heights) // 2]
        low = [r for r in dodge if not r.get("empty") and r["sil_h"] < med * 0.80]
        report["findings"]["shadow_dodge"] = {
            "median_sil_h": med,
            "heights": heights,
            "low_pose_files": [r["file"] for r in low],
            "low_pose_detail": [{"file": r["file"], "sil_h": r["sil_h"],
                                 "sil_w": r["sil_w"], "head_y": r["head_y"]}
                                for r in low],
            "verdict": ("fallen pose detected" if low else "no clear outlier"),
        }

    # ---- question 1/2: pivot and height across all standing poses ----------
    feet_dev, heights, centers = [], [], []
    for ch in CHARS:
        for act, a in report["actions"][ch].items():
            for r in a.get("frames", []):
                if r.get("empty"):
                    continue
                heights.append(r["sil_h"])
                centers.append(r["center_dx_from_pivot"])
                if act in ("Idle", "Walk"):
                    feet_dev.append(r["feet_dy_from_pivot"])
    if heights:
        heights_sorted = sorted(heights)
        report["findings"]["silhouette"] = {
            "sil_h_min": heights_sorted[0],
            "sil_h_median": heights_sorted[len(heights_sorted) // 2],
            "sil_h_max": heights_sorted[-1],
            "idle_walk_feet_dy_from_pivot_300": {
                "min": min(feet_dev), "max": max(feet_dev),
                "mean": sum(feet_dev) / len(feet_dev),
            },
            "center_dx_from_canvas_mid_min": min(centers),
            "center_dx_from_canvas_mid_max": max(centers),
        }

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=1)

    print("== silhouette ==")
    print(json.dumps(report["findings"].get("silhouette", {}), indent=1))
    print("== shadow dodge ==")
    print(json.dumps(report["findings"].get("shadow_dodge", {}), indent=1))
    print("wrote %s" % OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
