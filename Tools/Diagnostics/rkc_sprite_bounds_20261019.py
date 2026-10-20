#!/usr/bin/env python3
"""Task-local: character bounds inside the supplied sprite frames.

The flipbook is drawn at 1 sprite pixel = 1 cm, so the drawn character's size in
centimetres is exactly its pixel bounds inside the frame. This reports that, so
the drawn size can be compared with the fighter's hitbox height (Z 55..178 cm).

Read-only. Not part of the deliverable.
"""
import glob
import os

from PIL import Image

ROOT = "ArtSource/RockyKombatPack_v1.0/Characters"

for who in ("Rocky", "Shadow"):
    for clip in ("Idle", "Jab", "Walk"):
        files = sorted(glob.glob(os.path.join(ROOT, who, clip, "*.png")))
        if not files:
            continue
        f = files[0]
        im = Image.open(f).convert("RGBA")
        W, H = im.size
        a = im.split()[3]
        bbox = a.getbbox()
        # Near-black opaque pixel share, for the "baked shadow" question.
        px = im.load()
        black = 0
        opaque = 0
        for y in range(0, H, 2):
            for x in range(0, W, 2):
                r, g, b, al = px[x, y]
                if al > 200:
                    opaque += 1
                    if max(r, g, b) < 45:
                        black += 1
        print("%-6s %-6s %-28s size %dx%d  char bbox %s -> %dx%d px (cm)"
              % (who, clip, os.path.basename(f), W, H, bbox,
                 bbox[2] - bbox[0], bbox[3] - bbox[1]))
        print("        floor gap below char: %d px   near-black opaque: %.1f%%"
              % (H - bbox[3], 100.0 * black / max(1, opaque)))
