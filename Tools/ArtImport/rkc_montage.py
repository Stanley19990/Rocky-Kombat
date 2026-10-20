"""
rkc_montage.py - build labelled contact strips from the pack so poses can be inspected.

The bounding-box measurement cannot separate two poses whose silhouettes are nearly the
same height, so Shadow's Dodge needs eyes on it. Each strip is written to
Saved/Evidence/strips/<Char>_<Action>.png with a mid-grey background so alpha edges and
dark costume detail are both visible, plus a reference line at the documented pivot
y=300 drawn as a red rule.

Usage: python rkc_montage.py <Char> <Action> [<Char> <Action> ...]
       python rkc_montage.py all
"""

import os
import sys

from PIL import Image, ImageDraw

PACK = "C:/RockyKombat/ArtSource/RockyKombatPack_v1.0"
OUTDIR = "C:/RockyKombat/Saved/Evidence/strips"
ACTIONS = ["Idle", "Walk", "Jab", "HeavyPunch", "Block", "Dodge", "Knockdown", "Victory"]
SCALE = 0.62
GAP = 8
BG = (96, 96, 104)
RULE = (255, 64, 64)


def strip(char, action):
    d = os.path.join(PACK, "Characters", char, action)
    files = sorted(f for f in os.listdir(d) if f.lower().endswith(".png"))
    imgs = [Image.open(os.path.join(d, f)).convert("RGBA") for f in files]
    w = int(imgs[0].width * SCALE)
    h = int(imgs[0].height * SCALE)
    total_w = len(imgs) * w + (len(imgs) + 1) * GAP
    total_h = h + 2 * GAP + 22
    sheet = Image.new("RGBA", (total_w, total_h), BG + (255,))
    for i, im in enumerate(imgs):
        x = GAP + i * (w + GAP)
        sheet.alpha_composite(im.resize((w, h), Image.LANCZOS), (x, GAP))
        draw = ImageDraw.Draw(sheet)
        draw.rectangle([x, GAP, x + w - 1, GAP + h - 1], outline=(200, 200, 200, 255))
        draw.line([x, GAP + int(300 * SCALE), x + w, GAP + int(300 * SCALE)], fill=RULE)
        draw.text((x + 4, GAP + h + 4), files[i].replace(".png", ""), fill=(255, 255, 255, 255))
    os.makedirs(OUTDIR, exist_ok=True)
    out = os.path.join(OUTDIR, "%s_%s.png" % (char, action))
    sheet.convert("RGB").save(out)
    print(out)


def main(argv):
    if not argv or argv[0] == "all":
        pairs = [(c, a) for c in ("Rocky", "Shadow") for a in ACTIONS]
    elif len(argv) % 2 == 0:
        pairs = list(zip(argv[0::2], argv[1::2]))
    else:
        print("usage: rkc_montage.py <Char> <Action> [...] | all")
        return 2
    for c, a in pairs:
        strip(c, a)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
