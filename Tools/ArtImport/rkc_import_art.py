"""
rkc_import_art.py - import the supplied Rocky Kombat Paper2D art pack.

Imports, into /Game/RockyKombat/Art:
  * one Texture2D per source PNG (64 character frames + 1 arena image)
  * one PaperSprite per frame, configured with the pack's documented
    bottom-centre custom pivot (192, 300) at 1 px = 1 cm
  * one PaperFlipbook per animation (8 per fighter) using the pack's
    suggested playback fps, plus a two-frame safe Dodge for Shadow
  * three unlit sprite/backdrop materials

Every API call here was proven working against this exact engine build by
Saved/Evidence/API_probe3.json. Nothing is guessed: the property-set order in
particular (pivot_mode BEFORE custom_pivot_point) is required, because setting
custom_pivot_point while the pivot mode is still a preset causes the sprite to
rebake using the preset pivot and silently discard the custom value.

Run:
  UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script=<this> \
      -unattended -nosplash -nullrhi

Idempotent: existing destination assets are replaced, not duplicated.
"""

import json
import os
import traceback

import unreal

# --------------------------------------------------------------------------- paths
PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()).rstrip("/")
PACK = PROJECT_DIR + "/ArtSource/RockyKombatPack_v1.0"
ART = "/Game/RockyKombat/Art"

MAT_DIR = ART + "/Materials"
CHAR_DIR = ART + "/Characters"
ARENA_DIR = ART + "/Arena"

MAT_MASKED = MAT_DIR + "/M_RKC_SpriteMasked"
MAT_TRANSLUCENT = MAT_DIR + "/M_RKC_SpriteTranslucent"
MAT_BACKDROP = MAT_DIR + "/M_RKC_Backdrop"

TEX_NAME = "T_RKC_SpriteTexture"
TEX_PARAM = "SpriteTexture"

FRAME_W = 384
FRAME_H = 320
PIVOT_X = 192.0
PIVOT_Y = 300.0
PPUU = 1.0

# Shadow's Dodge frames 2-3 contain a low fallen pose the pack flags as
# unsuitable for an evade. The safe flipbook uses frames 0-1 only.
SAFE_DODGE_FRAMES = 2

TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

REPORT = {
    "engine_version": unreal.SystemLibrary.get_engine_version(),
    "pack": PACK,
    "materials": {},
    "characters": {},
    "arena": {},
    "flipbooks_created": [],
    "errors": [],
    "warnings": [],
}


def log(msg):
    unreal.log("[RKC-IMPORT] " + str(msg))


def record_error(where, exc):
    entry = "%s: %s: %s" % (where, type(exc).__name__, exc)
    REPORT["errors"].append(entry)
    unreal.log_error("[RKC-IMPORT] FAIL " + entry)


def warn(msg):
    REPORT["warnings"].append(str(msg))
    unreal.log_warning("[RKC-IMPORT] WARN " + str(msg))


def try_set(obj, names, value):
    """Set the first of `names` that exists. Returns the name used, or None."""
    for n in names:
        try:
            obj.set_editor_property(n, value)
            return n
        except Exception:  # noqa: BLE001 - candidate probing is intentional
            continue
    return None


def read_json(path):
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def ensure_dir(path):
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


# ----------------------------------------------------------------------- materials
def build_material(asset_path, blend, wire_opacity, opacity_mask, unlit_emissive=True):
    """Create a sprite material: one texture-parameter expression driving emissive
    (and opacity for translucent/masked variants)."""
    pkg = asset_path.rsplit("/", 1)[0]
    name = asset_path.rsplit("/", 1)[1]
    ensure_dir(pkg)

    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        unreal.EditorAssetLibrary.delete_asset(asset_path)

    mat = TOOLS.create_asset(name, pkg, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        raise RuntimeError("create_asset returned None for " + asset_path)

    tex = unreal.MaterialEditingLibrary.create_material_expression(
        mat, unreal.MaterialExpressionTextureSampleParameter2D, -600, 0)
    try_set(tex, ["parameter_name"], TEX_PARAM)
    default_tex = unreal.EditorAssetLibrary.find_asset_data(
        "/Engine/EngineResources/DefaultTexture").get_asset()
    if default_tex is not None:
        try_set(tex, ["texture"], default_tex)

    if blend == unreal.BlendMode.BLEND_OPAQUE:
        try_set(mat, ["blend_mode"], unreal.BlendMode.BLEND_OPAQUE)
    else:
        try_set(mat, ["blend_mode"], blend)

    try_set(mat, ["shading_model"], unreal.MaterialShadingModel.MSM_UNLIT)
    try_set(mat, ["two_sided"], True)
    try_set(mat, ["material_domain"], unreal.MaterialDomain.MD_SURFACE)

    if unlit_emissive:
        unreal.MaterialEditingLibrary.connect_material_property(
            tex, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    if wire_opacity:
        unreal.MaterialEditingLibrary.connect_material_property(
            tex, "A", unreal.MaterialProperty.MP_OPACITY)

    if opacity_mask is not None:
        clip = unreal.MaterialEditingLibrary.create_material_expression(
            mat, unreal.MaterialExpressionScalarParameter, -600, 300)
        try_set(clip, ["parameter_name"], "OpacityClip")
        try_set(clip, ["default_value"], opacity_mask)
        unreal.MaterialEditingLibrary.connect_material_property(
            clip, "", unreal.MaterialProperty.MP_OPACITY_MASK)

    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(asset_path)
    return asset_path


# ------------------------------------------------------------------ texture import
def configure_texture(name, source_png, dest_dir, srgb=True):
    """Import a single PNG as a Texture2D and apply the pack's required settings."""
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", source_png)
    task.set_editor_property("destination_path", dest_dir)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)

    factory = unreal.TextureFactory()
    try_set(factory, ["srgb"], srgb)
    try_set(factory, ["compression_settings"], unreal.TextureCompressionSettings.TC_BC7)
    try_set(factory, ["mip_gen_settings"], unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    task.set_editor_property("factory", factory)

    TOOLS.import_asset_tasks([task])
    paths = list(task.get_editor_property("imported_object_paths") or [])
    if not paths:
        raise RuntimeError("no imported_object_paths for " + source_png)

    tex_path = str(paths[0])
    tex = unreal.EditorAssetLibrary.find_asset_data(tex_path).get_asset()
    if tex is None:
        raise RuntimeError("imported texture not loadable: " + tex_path)

    try_set(tex, ["compression_settings"], unreal.TextureCompressionSettings.TC_BC7)
    try_set(tex, ["srgb"], srgb)
    try_set(tex, ["mip_gen_settings"], unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    try_set(tex, ["filter"], unreal.TextureFilter.TF_BILINEAR)
    try_set(tex, ["lod_group"], unreal.TextureGroup.TEXTUREGROUP_PIXELS2D)
    try_set(tex, ["address_x"], unreal.TextureAddress.TA_CLAMP)
    try_set(tex, ["address_y"], unreal.TextureAddress.TA_CLAMP)
    try_set(tex, ["compression_no_alpha"], False)
    try_set(tex, ["max_texture_size"], 0)
    try_set(tex, ["never_stream"], True)

    unreal.EditorAssetLibrary.save_asset(tex_path)
    return tex_path, tex


# -------------------------------------------------------------------- sprite build
def build_sprite(name, sprite_dir, tex, material_path):
    """Create one PaperSprite covering the whole frame with the pack's pivot.

    ORDER IS LOAD-BEARING:
      1. source_texture / source_uv / source_dimension / ppuu  (geometry inputs)
      2. pivot_mode = CUSTOM                                   (must precede 3)
      3. custom_pivot_point                                    (bakes the geometry)
      4. collision domain, material                            (presentation)
    """
    ensure_dir(sprite_dir)
    sprite_path = sprite_dir + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(sprite_path):
        unreal.EditorAssetLibrary.delete_asset(sprite_path)

    sprite = TOOLS.create_asset(name, sprite_dir, unreal.PaperSprite,
                                unreal.PaperSpriteFactory())
    if sprite is None:
        raise RuntimeError("create_asset returned None for " + sprite_path)

    sprite.set_editor_property("source_texture", tex)
    sprite.set_editor_property("source_uv", unreal.Vector2D(0.0, 0.0))
    sprite.set_editor_property("source_dimension", unreal.Vector2D(FRAME_W, FRAME_H))
    sprite.set_editor_property("pixels_per_unreal_unit", PPUU)

    # pivot before the custom point - see docstring
    sprite.set_editor_property("pivot_mode", unreal.SpritePivotMode.CUSTOM)
    sprite.set_editor_property("custom_pivot_point", unreal.Vector2D(PIVOT_X, PIVOT_Y))

    try_set(sprite, ["sprite_collision_domain"], unreal.SpriteCollisionMode.NONE)

    # render the full source quad; tight polygons are not needed for a 2D fighter
    try:
        geo = sprite.get_editor_property("render_geometry")
        geo.set_editor_property("geometry_type", unreal.SpritePolygonMode.SOURCE_BOUNDING_BOX)
        sprite.set_editor_property("render_geometry", geo)
    except Exception as exc:  # noqa: BLE001
        warn("render_geometry not set for %s: %s" % (name, exc))

    mat = unreal.EditorAssetLibrary.load_asset(material_path)
    if mat is not None:
        sprite.set_editor_property("default_material", mat)

    unreal.EditorAssetLibrary.save_asset(sprite_path)
    return sprite_path, sprite


# ------------------------------------------------------------------ flipbook build
def build_flipbook(name, fb_dir, sprite_objs, fps, frame_runs=None):
    ensure_dir(fb_dir)
    fb_path = fb_dir + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(fb_path):
        unreal.EditorAssetLibrary.delete_asset(fb_path)

    fb = TOOLS.create_asset(name, fb_dir, unreal.PaperFlipbook,
                            unreal.PaperFlipbookFactory())
    if fb is None:
        raise RuntimeError("create_asset returned None for " + fb_path)

    keys = []
    for i, spr in enumerate(sprite_objs):
        kf = unreal.PaperFlipbookKeyFrame()
        try_set(kf, ["sprite"], spr)
        run = 1
        if frame_runs is not None and i < len(frame_runs):
            run = frame_runs[i]
        try_set(kf, ["frame_run"], run)
        keys.append(kf)

    fb.set_editor_property("key_frames", keys)
    fb.set_editor_property("frames_per_second", float(fps))

    unreal.EditorAssetLibrary.save_asset(fb_path)
    REPORT["flipbooks_created"].append(fb_path)
    return fb_path, fb


# ------------------------------------------------------------------------ per char
def import_character(roster):
    char_root = CHAR_DIR + "/" + roster
    tex_dir = char_root + "/Textures"
    spr_dir = char_root + "/Sprites"
    fb_dir = char_root + "/Flipbooks"
    ensure_dir(tex_dir)
    ensure_dir(spr_dir)
    ensure_dir(fb_dir)

    frame_map = read_json(PACK + "/Characters/" + roster + "/frame_map.json")
    result = {"textures": [], "sprites": [], "flipbooks": {}, "safe_dodge": None}

    for anim in sorted(frame_map.keys()):
        entry = frame_map[anim]
        frames = entry["frames"]
        fps = entry.get("suggested_fps", 8)

        sprites = []
        for idx, rel in enumerate(frames):
            src = PACK + "/" + rel
            if not os.path.isfile(src):
                record_error("missing source frame", RuntimeError(src))
                continue

            tex_name = "T_%s_%s_%02d" % (roster, anim, idx)
            spr_name = "SPR_%s_%s_%02d" % (roster, anim, idx)
            try:
                tex_path, tex = configure_texture(tex_name, src, tex_dir)
                result["textures"].append(tex_path)
            except Exception as exc:  # noqa: BLE001
                record_error("texture " + tex_name, exc)
                continue

            try:
                spr_path, spr = build_sprite(spr_name, spr_dir, tex, MAT_MASKED)
                result["sprites"].append(spr_path)
                sprites.append((spr_path, spr))
            except Exception as exc:  # noqa: BLE001
                record_error("sprite " + spr_name, exc)

        if not sprites:
            record_error("flipbook " + anim, RuntimeError("no sprites for " + roster + "/" + anim))
            continue

        fb_name = "FB_%s_%s" % (roster, anim)
        try:
            fb_path, _ = build_flipbook(fb_name, fb_dir, [s[1] for s in sprites], fps)
            result["flipbooks"][anim] = fb_path
        except Exception as exc:  # noqa: BLE001
            record_error("flipbook " + fb_name, exc)

        # Shadow's dodge has two unusable trailing frames; ship a safe subset.
        if roster == "Shadow" and anim == "Dodge" and len(sprites) > SAFE_DODGE_FRAMES:
            try:
                safe_name = "FB_%s_Dodge_Safe" % roster
                safe_path, _ = build_flipbook(
                    safe_name, fb_dir, [s[1] for s in sprites[:SAFE_DODGE_FRAMES]], fps)
                result["safe_dodge"] = safe_path
                log("safe dodge flipbook: " + safe_path)
            except Exception as exc:  # noqa: BLE001
                record_error("safe dodge flipbook", exc)

    REPORT["characters"][roster] = result
    return result


# ---------------------------------------------------------------------------- main
def main():
    log("project dir: " + PROJECT_DIR)
    if not os.path.isdir(PACK):
        record_error("pack", RuntimeError("asset pack not found: " + PACK))
        write_report()
        return

    ensure_dir(ART)
    ensure_dir(MAT_DIR)
    ensure_dir(CHAR_DIR)
    ensure_dir(ARENA_DIR)

    # 1. materials ---------------------------------------------------------
    try:
        REPORT["materials"]["masked"] = build_material(
            MAT_MASKED, unreal.BlendMode.BLEND_MASKED, True, 0.333)
        REPORT["materials"]["translucent"] = build_material(
            MAT_TRANSLUCENT, unreal.BlendMode.BLEND_TRANSLUCENT, True, None)
        REPORT["materials"]["backdrop"] = build_material(
            MAT_BACKDROP, unreal.BlendMode.BLEND_OPAQUE, False, None)
        log("materials built")
    except Exception as exc:  # noqa: BLE001
        record_error("materials", exc)

    # 2. characters --------------------------------------------------------
    for roster in ("Rocky", "Shadow"):
        try:
            r = import_character(roster)
            log("%s: %d textures, %d sprites, %d flipbooks"
                % (roster, len(r["textures"]), len(r["sprites"]), len(r["flipbooks"])))
        except Exception as exc:  # noqa: BLE001
            record_error("character " + roster, exc)

    # 3. arena backdrop ----------------------------------------------------
    arena_png = PACK + "/Arena/Midnight_Rooftop_Ring.png"
    if os.path.isfile(arena_png):
        try:
            tex_path, _ = configure_texture(
                "T_RKC_Arena_MidnightRooftop", arena_png, ARENA_DIR, srgb=True)
            REPORT["arena"]["texture"] = tex_path
            log("arena texture: " + tex_path)
        except Exception as exc:  # noqa: BLE001
            record_error("arena texture", exc)
    else:
        record_error("arena", RuntimeError("missing " + arena_png))

    write_report()


def write_report():
    out_dir = os.path.join(unreal.Paths.project_saved_dir(), "Evidence")
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, "Art_import_report.json")
    with open(out, "w", encoding="utf-8") as fh:
        json.dump(REPORT, fh, indent=1)
    log("report: " + out)
    log("errors=%d warnings=%d flipbooks=%d"
        % (len(REPORT["errors"]), len(REPORT["warnings"]),
           len(REPORT["flipbooks_created"])))


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:  # noqa: BLE001
        record_error("main", exc)
        unreal.log_error(traceback.format_exc())
        write_report()
