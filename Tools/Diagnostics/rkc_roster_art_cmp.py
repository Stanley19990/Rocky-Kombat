"""rkc_roster_art_cmp.py

Question answered: is the Shadow artwork actually a DIFFERENT character from
Rocky, or is the same picture shipped under both names?

The user reports "selecting Shadow still gives the human Rocky". One way that
can be true on screen is that the source pack itself carries identical frames
for both rosters. This script compares the two rosters frame by frame with
hashlib (md5sum is unusable in this MSYS shell: "failed to set file descriptor
text/binary mode"), and if Pillow is importable also compares decoded pixel
content, so a byte-difference that is only metadata cannot be mistaken for a
different drawing.

Run:  python rkc_roster_art_cmp.py [pack_dir]
Exit code 0 = the two rosters are distinct drawings; 1 = identical art found.
"""

import hashlib
import json
import os
import sys

PACK = sys.argv[1] if len(sys.argv) > 1 else r"C:\RockyKombat\ArtSource\RockyKombatPack_v1.0"
ANIMS = ["Idle", "Walk", "Jab", "HeavyPunch", "Block", "Dodge", "Knockdown", "Victory"]


def md5(path):
    h = hashlib.md5()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def pixels(path):
    try:
        from PIL import Image
    except Exception:
        return None
    try:
        with Image.open(path) as im:
            im = im.convert("RGBA")
            return hashlib.md5(im.tobytes()).hexdigest(), im.size
    except Exception as exc:  # noqa: BLE001
        return ("error:%s" % exc, None)


def main():
    report = {"pack": PACK, "pairs": [], "identical_files": [], "identical_pixels": []}
    have_pil = pixels(os.path.join(PACK, "Characters", "Rocky", "Idle", "Rocky_Idle_00.png")) is not None
    report["pillow_available"] = have_pil

    for anim in ANIMS:
        for idx in range(4):
            r = os.path.join(PACK, "Characters", "Rocky", anim, "Rocky_%s_%02d.png" % (anim, idx))
            s = os.path.join(PACK, "Characters", "Shadow", anim, "Shadow_%s_%02d.png" % (anim, idx))
            if not (os.path.exists(r) and os.path.exists(s)):
                report["pairs"].append({"anim": anim, "frame": idx, "status": "MISSING"})
                continue
            hr, hs = md5(r), md5(s)
            entry = {
                "anim": anim,
                "frame": idx,
                "rocky_md5": hr,
                "shadow_md5": hs,
                "rocky_bytes": os.path.getsize(r),
                "shadow_bytes": os.path.getsize(s),
                "file_identical": hr == hs,
            }
            if have_pil:
                pr = pixels(r)
                ps = pixels(s)
                entry["rocky_px"] = pr[0]
                entry["shadow_px"] = ps[0]
                entry["rocky_size"] = pr[1]
                entry["shadow_size"] = ps[1]
                entry["pixel_identical"] = pr[0] == ps[0]
                if entry["pixel_identical"]:
                    report["identical_pixels"].append("%s_%02d" % (anim, idx))
            if entry["file_identical"]:
                report["identical_files"].append("%s_%02d" % (anim, idx))
            report["pairs"].append(entry)

    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "roster_art_cmp.json")
    with open(out, "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=2)

    print("pack                : %s" % PACK)
    print("pillow available    : %s" % have_pil)
    print("byte-identical pairs: %d %s" % (len(report["identical_files"]), report["identical_files"]))
    if have_pil:
        print("pixel-identical pairs: %d %s" % (len(report["identical_pixels"]),
                                                report["identical_pixels"]))
    for e in report["pairs"]:
        if e.get("status") == "MISSING":
            print("  MISSING %s_%02d" % (e["anim"], e["frame"]))
    print("report -> %s" % out)
    same = bool(report["identical_files"]) or bool(report.get("identical_pixels"))
    print("VERDICT: %s" % ("THE TWO ROSTERS SHIP IDENTICAL ART" if same
                           else "the two rosters are distinct drawings"))
    return 1 if same else 0


if __name__ == "__main__":
    sys.exit(main())
