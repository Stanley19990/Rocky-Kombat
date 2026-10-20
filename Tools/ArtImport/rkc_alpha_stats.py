#!/usr/bin/env python3
"""
rkc_alpha_stats.py - Decide masked vs translucent sprite material from data.

The pack's import prompt asks for a masked-vs-translucent comparison on this
"soft fur" artwork instead of copying generic settings. Masked blending cuts
every pixel below the opacity-mask threshold, which destroys genuinely soft
edges but avoids translucent sorting problems. Translucent preserves softness
at the cost of sort order.

This script measures how much of each frame's silhouette actually depends on
partial alpha, so the choice is based on the artwork rather than preference.

Writes Saved/Evidence/Alpha_stats.json.
"""

import json
import os

from PIL import Image

PACK = os.environ.get("RKC_PACK", r"C:\RockyKombat\ArtSource\RockyKombatPack_v1.0")
OUT = os.environ.get("RKC_OUT", r"C:\RockyKombat\Saved\Evidence\Alpha_stats.json")

CHARS = ["Rocky", "Shadow"]
ACTIONS = ["Idle", "Walk", "Jab", "HeavyPunch", "Block", "Dodge", "Knockdown", "Victory"]


def main():
    frames = []
    tot_opaque = tot_partial = tot_transparent = 0
    # Alpha values that a masked material would still clip at a 0.333 threshold.
    clipped_by_mask = 0

    for char in CHARS:
        for action in ACTIONS:
            for i in range(4):
                rel = f"Characters/{char}/{action}/{char}_{action}_{i:02d}.png"
                path = os.path.join(PACK, rel.replace("/", os.sep))
                if not os.path.isfile(path):
                    continue
                with Image.open(path) as im:
                    hist = im.convert("RGBA").getchannel("A").histogram()
                opaque = hist[255]
                partial = sum(hist[1:255])
                transparent = hist[0]
                # Masked alpha test at the engine's default 0.333 cut:
                # alpha <= 85 becomes fully invisible.
                below_cut = sum(hist[1:86])
                tot_opaque += opaque
                tot_partial += partial
                tot_transparent += transparent
                clipped_by_mask += below_cut
                frames.append({
                    "file": rel,
                    "opaque": opaque,
                    "partial": partial,
                    "transparent": transparent,
                    "partial_frac_of_visible": round(partial / max(1, opaque + partial), 5),
                    "below_mask_cut": below_cut,
                })

    visible = max(1, tot_opaque + tot_partial)
    soft_frac = tot_partial / visible
    clipped_frac = clipped_by_mask / visible

    verdict = "MASKED_ACCEPTABLE"
    rationale = (
        "Partial-alpha pixels are a thin anti-aliasing fringe dominated by "
        "high alpha values; masking clips little of the visible silhouette and "
        "avoids translucent sort-order risk between the two fighters."
    )
    if soft_frac > 0.25:
        verdict = "TRANSLUCENT_PREFERRED"
        rationale = (
            "A large share of visible pixels is partial alpha, so an opacity mask "
            "would visibly erode the soft edges; translucent blending preserves "
            "the artwork and sort order must be handled explicitly."
        )
    elif soft_frac > 0.10:
        verdict = "REVIEW_BOTH"
        rationale = (
            "Partial alpha is significant enough that masking may harden edges; "
            "render both and compare before locking the choice."
        )

    out = {
        "pack": PACK,
        "frames_measured": len(frames),
        "totals": {
            "opaque": tot_opaque,
            "partial": tot_partial,
            "transparent": tot_transparent,
            "partial_fraction_of_visible": round(soft_frac, 5),
            "masked_cut_fraction_of_visible_0333": round(clipped_frac, 5),
        },
        "verdict": verdict,
        "rationale": rationale,
        "frames": frames,
    }

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as fh:
        json.dump(out, fh, indent=2)

    print("=" * 68)
    print("ALPHA SOFTNESS ANALYSIS (masked vs translucent decision)")
    print("=" * 68)
    print(f"frames measured            : {len(frames)}")
    print(f"opaque px (a=255)          : {tot_opaque}")
    print(f"partial px (1..254)        : {tot_partial}")
    print(f"partial / visible          : {soft_frac:.4%}")
    print(f"visible cut by mask @0.333 : {clipped_frac:.4%}")
    print(f"VERDICT                    : {verdict}")
    print(f"rationale                  : {rationale}")
    print(f"report                     : {OUT}")
    print("=" * 68)
    worst = sorted(frames, key=lambda f: -f["partial_frac_of_visible"])[:5]
    for f in worst:
        print(f"  softest: {f['file']} partial={f['partial_frac_of_visible']:.4%}")


main()
