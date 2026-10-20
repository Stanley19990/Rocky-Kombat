"""
rkc_verify_art.py - verify the imported Paper2D assets by reading them back.

Checks, per character:
  * every animation has a flipbook
  * each flipbook's keyframe count matches the source frame count
  * one sprite per animation still carries the pack's pivot (192, 300) and
    pixels_per_unreal_unit = 1.0, i.e. the pivot bake really did take
  * the sprite's default_material resolved to the masked sprite material
Also checks the arena texture and both materials exist.

Writes Saved/Evidence/Art_verify_report.json.
"""

import json
import os

import unreal

ART = "/Game/RockyKombat/Art"
PACK = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()).rstrip("/") \
    + "/ArtSource/RockyKombatPack_v1.0"

EAL = unreal.EditorAssetLibrary

RESULT = {"checks": [], "failures": [], "flipbook_counts": {}}


def check(name, condition, detail=""):
    ok = bool(condition)
    RESULT["checks"].append({"name": name, "ok": ok, "detail": str(detail)})
    if not ok:
        RESULT["failures"].append("%s: %s" % (name, detail))
    return ok


def main():
    frame_maps = {}
    for roster in ("Rocky", "Shadow"):
        with open(PACK + "/Characters/" + roster + "/frame_map.json", "r",
                  encoding="utf-8") as fh:
            frame_maps[roster] = json.load(fh)

    # ---- materials -------------------------------------------------------
    for label, path in (("masked", "/Game/RockyKombat/Art/Materials/M_RKC_SpriteMasked"),
                        ("translucent", "/Game/RockyKombat/Art/Materials/M_RKC_SpriteTranslucent"),
                        ("backdrop", "/Game/RockyKombat/Art/Materials/M_RKC_Backdrop")):
        check("material." + label, EAL.does_asset_exist(path), path)

    # ---- arena texture ---------------------------------------------------
    arena = ART + "/Arena/T_RKC_Arena_MidnightRooftop"
    arena_ok = EAL.does_asset_exist(arena)
    check("arena.texture_exists", arena_ok, arena)
    if arena_ok:
        tex = EAL.load_asset(arena)
        check("arena.texture_size",
              (tex.blueprint_get_size_x(), tex.blueprint_get_size_y()) == (1672, 941),
              "%dx%d" % (tex.blueprint_get_size_x(), tex.blueprint_get_size_y()))

    # ---- characters ------------------------------------------------------
    for roster, fmap in frame_maps.items():
        for anim, entry in fmap.items():
            expected = len(entry["frames"])
            fb_path = "%s/Characters/%s/Flipbooks/FB_%s_%s" % (ART, roster, roster, anim)
            if not check("flipbook.%s.%s.exists" % (roster, anim), EAL.does_asset_exist(fb_path),
                         fb_path):
                continue
            fb = EAL.load_asset(fb_path)
            keys = fb.get_editor_property("key_frames")
            check("flipbook.%s.%s.frames" % (roster, anim), len(keys) == expected,
                  "got %d want %d" % (len(keys), expected))
            check("flipbook.%s.%s.fps" % (roster, anim),
                  abs(fb.get_editor_property("frames_per_second") - float(entry["suggested_fps"])) < 0.01,
                  str(fb.get_editor_property("frames_per_second")))
            RESULT["flipbook_counts"]["%s/%s" % (roster, anim)] = len(keys)

        # sprite geometry, on the first sprite of each animation
        for anim, entry in fmap.items():
            spr_path = "%s/Characters/%s/Sprites/SPR_%s_%s_00" % (ART, roster, roster, anim)
            if not check("sprite.%s.%s.exists" % (roster, anim), EAL.does_asset_exist(spr_path),
                         spr_path):
                continue
            spr = EAL.load_asset(spr_path)
            dim = spr.get_editor_property("source_dimension")
            piv = spr.get_editor_property("custom_pivot_point")
            ppuu = spr.get_editor_property("pixels_per_unreal_unit")
            mode = spr.get_editor_property("pivot_mode")
            mat = spr.get_editor_property("default_material")
            check("sprite.%s.%s.dimension" % (roster, anim),
                  (dim.x, dim.y) == (384.0, 320.0), "(%.0f,%.0f)" % (dim.x, dim.y))
            check("sprite.%s.%s.pivot" % (roster, anim),
                  (piv.x, piv.y) == (192.0, 300.0), "(%.0f,%.0f)" % (piv.x, piv.y))
            check("sprite.%s.%s.ppuu" % (roster, anim), abs(ppuu - 1.0) < 1e-4, str(ppuu))
            check("sprite.%s.%s.pivot_mode" % (roster, anim),
                  str(mode).find("CUSTOM") >= 0, str(mode))
            check("sprite.%s.%s.material" % (roster, anim),
                  mat is not None and "M_RKC_SpriteMasked" in mat.get_path_name(),
                  mat.get_path_name() if mat else "None")

    # ---- safe dodge ------------------------------------------------------
    safe = ART + "/Characters/Shadow/Flipbooks/FB_Shadow_Dodge_Safe"
    if check("shadow.dodge_safe.exists", EAL.does_asset_exist(safe), safe):
        fb = EAL.load_asset(safe)
        keys = fb.get_editor_property("key_frames")
        check("shadow.dodge_safe.frames", len(keys) == 2, "got %d want 2" % len(keys))

    RESULT["summary"] = {
        "checks": len(RESULT["checks"]),
        "failures": len(RESULT["failures"]),
    }
    out_dir = os.path.join(unreal.Paths.project_saved_dir(), "Evidence")
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, "Art_verify_report.json")
    with open(out, "w", encoding="utf-8") as fh:
        json.dump(RESULT, fh, indent=1)
    unreal.log("[RKC-VERIFY] %d checks, %d failures -> %s"
               % (len(RESULT["checks"]), len(RESULT["failures"]), out))


if __name__ == "__main__":
    main()
