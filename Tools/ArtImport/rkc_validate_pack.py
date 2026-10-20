#!/usr/bin/env python3
"""
rkc_validate_pack.py - Rocky Kombat art pack validator (standalone, no Unreal).

Validates the supplied Rocky Kombat Import Pack v1.0 against its own manifest:
  - every referenced PNG exists and decodes
  - frame canvases are exactly 384x320, atlases 1536x2560, arena 1672x941
  - alpha channel present and actually used (transparency, not opaque)
  - manifest / frame_map path integrity for both fighters
  - measured silhouette bounding box and standing height per frame
  - pivot (192,300) sanity against measured feet baseline

Writes Saved/Evidence/ArtValidate_report.json and prints a human summary.
Read-only: never modifies the art pack.
"""

import json
import os
import sys

try:
    from PIL import Image
except ImportError:
    print("FATAL: Pillow not available")
    sys.exit(2)

PACK = os.environ.get(
    "RKC_PACK",
    r"C:\RockyKombat\ArtSource\RockyKombatPack_v1.0",
)
OUT = os.environ.get(
    "RKC_OUT",
    r"C:\RockyKombat\Saved\Evidence\ArtValidate_report.json",
)

FRAME_W, FRAME_H = 384, 320
PIVOT = (192, 300)
ATLAS = (1536, 2560)
ARENA = (1672, 941)
ACTIONS = ["Idle", "Walk", "Jab", "HeavyPunch", "Block", "Dodge", "Knockdown", "Victory"]
CHARS = ["Rocky", "Shadow"]

checks = []
failures = []


def check(name, ok, detail=""):
    checks.append({"check": name, "ok": bool(ok), "detail": str(detail)})
    if not ok:
        failures.append(f"{name}: {detail}")


def measure(path):
    """Return geometry + alpha stats for one frame."""
    with Image.open(path) as im:
        im.load()
        size = im.size
        mode = im.mode
        rgba = im.convert("RGBA")
    alpha = rgba.getchannel("A")
    bbox = alpha.getbbox()
    hist = alpha.histogram()
    n_transparent = hist[0]
    n_opaque = hist[255]
    n_partial = sum(hist[1:255])
    total = size[0] * size[1]
    return {
        "size": list(size),
        "mode": mode,
        "bbox": list(bbox) if bbox else None,
        "px_total": total,
        "px_transparent": n_transparent,
        "px_opaque": n_opaque,
        "px_partial": n_partial,
        "coverage": (total - n_transparent) / total if total else 0.0,
    }


def main():
    report = {
        "pack": PACK,
        "pack_present": os.path.isdir(PACK),
        "frame_size_expected": [FRAME_W, FRAME_H],
        "pivot_expected": list(PIVOT),
        "characters": {},
        "atlases": {},
        "arena": {},
    }

    check("pack_directory_exists", os.path.isdir(PACK), PACK)
    if not os.path.isdir(PACK):
        finish(report)
        return

    manifest_path = os.path.join(PACK, "asset_manifest.json")
    manifest = {}
    if os.path.isfile(manifest_path):
        with open(manifest_path, "r", encoding="utf-8") as fh:
            manifest = json.load(fh)
        check("manifest_loaded", True, manifest_path)
    else:
        check("manifest_loaded", False, manifest_path)

    # --- manifest declares what we expect -------------------------------------
    check("manifest_frame_size", manifest.get("frame_size") == [FRAME_W, FRAME_H],
          manifest.get("frame_size"))
    check("manifest_pivot", manifest.get("pivot_pixels") == list(PIVOT),
          manifest.get("pivot_pixels"))
    check("manifest_facing_right", manifest.get("facing") == "right",
          manifest.get("facing"))
    check("manifest_engine_tested_false", manifest.get("engine_tested") is False,
          "pack self-declares no engine verification (honest)")

    # --- per-character frames -------------------------------------------------
    total_frames = 0
    for cname in CHARS:
        cinfo = (manifest.get("characters") or {}).get(cname)
        if not cinfo:
            check(f"manifest_character_{cname}", False, "missing from manifest")
            continue
        char_entry = {"actions": {}, "frame_count": 0}
        for action in ACTIONS:
            ainfo = (cinfo.get("animations") or {}).get(action)
            if not ainfo:
                check(f"{cname}/{action}_in_manifest", False, "missing")
                continue
            frames = ainfo.get("frames", [])
            check(f"{cname}/{action}_frame_count_4", len(frames) == 4, len(frames))
            act_entry = {
                "loop": ainfo.get("loop"),
                "suggested_fps": ainfo.get("suggested_fps"),
                "frames": [],
            }
            for rel in frames:
                p = os.path.join(PACK, rel.replace("/", os.sep))
                if not os.path.isfile(p):
                    check(f"exists_{rel}", False, "missing on disk")
                    continue
                m = measure(p)
                total_frames += 1
                char_entry["frame_count"] += 1
                ok_size = tuple(m["size"]) == (FRAME_W, FRAME_H)
                check(f"size_{os.path.basename(rel)}", ok_size, m["size"])
                ok_alpha = m["px_transparent"] > 0
                check(f"alpha_transparency_{os.path.basename(rel)}", ok_alpha,
                      f"transparent={m['px_transparent']} partial={m['px_partial']}")
                act_entry["frames"].append({
                    "file": rel,
                    "size": m["size"],
                    "bbox": m["bbox"],
                    "coverage": round(m["coverage"], 4),
                    "px_transparent": m["px_transparent"],
                    "px_partial": m["px_partial"],
                    "px_opaque": m["px_opaque"],
                })
            char_entry["actions"][action] = act_entry
        report["characters"][cname] = char_entry

    check("total_individual_frames_64", total_frames == 64, total_frames)

    # --- atlases --------------------------------------------------------------
    for cname in CHARS:
        rel = f"Characters/{cname}/{cname}_Atlas_4x8.png"
        p = os.path.join(PACK, rel.replace("/", os.sep))
        if os.path.isfile(p):
            with Image.open(p) as im:
                im.load()
                size = im.size
                mode = im.mode
            report["atlases"][cname] = {"file": rel, "size": list(size), "mode": mode}
            check(f"atlas_size_{cname}", tuple(size) == ATLAS, size)
            check(f"atlas_grid_integral_{cname}",
                  size[0] % 4 == 0 and size[1] % 8 == 0,
                  f"{size[0]}x{size[1]} -> cell {size[0]//4}x{size[1]//8}")
            check(f"atlas_cell_matches_frame_{cname}",
                  (size[0] // 4, size[1] // 8) == (FRAME_W, FRAME_H),
                  f"cell {(size[0]//4, size[1]//8)}")
        else:
            check(f"atlas_present_{cname}", False, rel)

    # --- arena ----------------------------------------------------------------
    rel = "Arena/Midnight_Rooftop_Ring.png"
    p = os.path.join(PACK, rel.replace("/", os.sep))
    if os.path.isfile(p):
        with Image.open(p) as im:
            im.load()
            size = im.size
            mode = im.mode
        report["arena"] = {
            "file": rel,
            "size": list(size),
            "mode": mode,
            "ground_y_frac_declared": 0.88,
            "ground_y_px_if_used": int(size[1] * 0.88),
        }
        check("arena_size", tuple(size) == ARENA, size)
        check("arena_aspect",
              abs((size[0] / size[1]) - (1672 / 941)) < 0.001,
              f"{size[0]/size[1]:.4f} vs {1672/941:.4f}")
    else:
        check("arena_present", False, rel)

    # --- pivot sanity: feet baseline should sit near y=300 --------------------
    feet_samples = []
    for cname, cdata in report["characters"].items():
        for action in ("Idle", "Walk", "Block"):
            a = cdata["actions"].get(action)
            if not a:
                continue
            for fr in a["frames"]:
                if fr["bbox"]:
                    feet_samples.append({
                        "char": cname, "action": action,
                        "file": os.path.basename(fr["file"]),
                        "bbox_bottom": fr["bbox"][3],
                        "delta_from_pivot_y": fr["bbox"][3] - PIVOT[1],
                    })
    report["pivot_sanity"] = feet_samples
    if feet_samples:
        worst = max(abs(s["delta_from_pivot_y"]) for s in feet_samples)
        check("pivot_feet_within_40px_of_y300", worst <= 40,
              f"worst |bbox_bottom-300| = {worst}px across {len(feet_samples)} idle/walk/block frames")

    check("all_checks_passed", not failures, f"{len(failures)} failure(s)")
    finish(report)


def finish(report):
    report["checks"] = checks
    report["failure_count"] = len(failures)
    report["check_count"] = len(checks)
    report["failures"] = failures
    report["result"] = "PASS" if not failures else "FAIL"
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=2)

    print("=" * 68)
    print("ROCKY KOMBAT - ART PACK VALIDATION")
    print("=" * 68)
    print(f"pack            : {report['pack']}")
    print(f"checks run      : {len(checks)}")
    print(f"failures        : {len(failures)}")
    print(f"result          : {report['result']}")
    print(f"report          : {OUT}")
    print("-" * 68)
    for cname, cdata in report.get("characters", {}).items():
        print(f"{cname:8s} frames={cdata['frame_count']:3d}")
        for action, a in cdata["actions"].items():
            covs = [f["coverage"] for f in a["frames"]]
            bots = [f["bbox"][3] for f in a["frames"] if f["bbox"]]
            print(f"   {action:11s} loop={str(a['loop']):5s} fps={a['suggested_fps']:2d} "
                  f"coverage={min(covs):.3f}-{max(covs):.3f} "
                  f"feet_y={min(bots) if bots else '-'}-{max(bots) if bots else '-'}")
    if report.get("atlases"):
        print("-" * 68)
        for cname, a in report["atlases"].items():
            print(f"atlas {cname:8s} {a['size']} {a['mode']}")
    if report.get("arena"):
        a = report["arena"]
        print(f"arena          {a['size']} {a['mode']} ground_y~{a['ground_y_px_if_used']}px")
    if failures:
        print("-" * 68)
        for f in failures[:40]:
            print("FAIL:", f)
    print("=" * 68)
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
