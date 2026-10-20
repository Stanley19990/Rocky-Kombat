"""
rkc_probe_api3.py - third engine API probe: proves the exact calls the importer will use.

Fixes from probe 2:
  * helper functions are defined before use
  * property enumeration via __mro__.__dict__ does not work in UE 5.8 Python, so every
    property is probed by attempting the set and reporting the exact error instead
  * PaperSpriteFactory has no `initial_texture` property, so the sprite is created empty
    and filled with a real texture afterwards (candidate names are tried in order)
  * TranslucencyLightingMode.TLM_UNLIT does not exist in 5.8 (volumetric/surface only)

Every capability is stored as {ok, value|error} so the importer can branch instead of crash.
Nothing is left behind: all throwaway assets are deleted at the end.

Run:
  UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script=<this> -unattended -nosplash -nullrhi
"""

import json
import os
import traceback

import unreal

OUT = os.path.join(unreal.Paths.project_saved_dir(), "Evidence", "API_probe3.json")
PROBE_DIR = "/Game/RockyKombat/Art/_Probe"
PACK = "C:/RockyKombat/ArtSource/RockyKombatPack_v1.0"
SAMPLE_PNG = PACK + "/Characters/Rocky/Idle/Rocky_Idle_00.png"

RESULT = {"engine_version": unreal.SystemLibrary.get_engine_version(),
          "checks": {},
          "created": [],
          "notes": []}

TOOLS = unreal.AssetToolsHelpers.get_asset_tools()


def record(name, fn):
    try:
        RESULT["checks"][name] = {"ok": True, "value": fn()}
    except Exception as exc:  # noqa: BLE001
        RESULT["checks"][name] = {
            "ok": False,
            "error": "%s: %s" % (type(exc).__name__, exc),
            "trace": traceback.format_exc().splitlines()[-4:],
        }


def try_set(obj, names, value):
    """Set the first property name that exists; return {used, errors}."""
    out = {"candidates": list(names), "errors": {}}
    for n in names:
        try:
            obj.set_editor_property(n, value)
            out["used"] = n
            return out
        except Exception as exc:  # noqa: BLE001
            out["errors"][n] = "%s: %s" % (type(exc).__name__, exc)
    out["used"] = None
    return out


def try_get(obj, name):
    try:
        return str(obj.get_editor_property(name))
    except Exception as exc:  # noqa: BLE001
        return "ERR: %s" % exc


def enum_props(cls, names):
    """Read back a list of candidate property names, reporting which resolve."""
    return {n: try_get(cls, n) for n in names}


# ------------------------------------------------------------------ 1. texture import
def _import_texture():
    """Import one real PNG through AssetImportTask + TextureFactory, then configure it."""
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", SAMPLE_PNG)
    task.set_editor_property("destination_path", PROBE_DIR)
    task.set_editor_property("destination_name", "T_ProbeIdle00")
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)

    factory = unreal.TextureFactory()
    set_report = {}
    set_report["srgb"] = try_set(factory, ["srgb"], True)
    set_report["compression"] = try_set(
        factory, ["compression_settings"], unreal.TextureCompressionSettings.TC_BC7)
    set_report["mips"] = try_set(
        factory, ["mip_gen_settings"], unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    task.set_editor_property("factory", factory)

    TOOLS.import_asset_tasks([task])
    paths = list(task.get_editor_property("imported_object_paths") or [])
    out = {"imported_paths": [str(p) for p in paths], "factory_props": set_report}
    if not paths:
        return out

    tex = unreal.EditorAssetLibrary.find_asset_data(str(paths[0])).get_asset()
    out["asset_class"] = tex.get_class().get_name() if tex else None
    if tex is None:
        return out

    # now push the real settings onto the texture asset itself
    out["asset_sets"] = {
        "compression_settings": try_set(
            tex, ["compression_settings"], unreal.TextureCompressionSettings.TC_BC7),
        "srgb": try_set(tex, ["srgb"], True),
        "mip_gen_settings": try_set(
            tex, ["mip_gen_settings"], unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS),
        "filter": try_set(tex, ["filter"], unreal.TextureFilter.TF_BILINEAR),
        "lod_group": try_set(tex, ["lod_group"], unreal.TextureGroup.TEXTUREGROUP_PIXELS2D),
        "address_x": try_set(tex, ["address_x"], unreal.TextureAddress.TA_CLAMP),
        "address_y": try_set(tex, ["address_y"], unreal.TextureAddress.TA_CLAMP),
        "compression_no_alpha": try_set(tex, ["compression_no_alpha"], False),
        "max_texture_size": try_set(tex, ["max_texture_size"], 0),
        "never_stream": try_set(tex, ["never_stream"], True),
    }
    out["readback"] = enum_props(tex, ["compression_settings", "srgb", "mip_gen_settings",
                                       "filter", "lod_group", "address_x", "address_y",
                                       "compression_no_alpha", "max_texture_size"])
    out["size"] = {"x": try_get(tex, "blueprint_get_size_x"),
                   "y": try_get(tex, "blueprint_get_size_y")}
    out["has_alpha"] = try_get(tex, "has_alpha_channel")
    out["asset_path"] = tex.get_path_name()
    return out


record("texture.import_and_configure", _import_texture)


# ------------------------------------------------------- 2. sprite material creation
def _build_material(name, blend, wire_opacity, opacity_mask):
    mat = TOOLS.create_asset(name, PROBE_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        return {"created": False}
    RESULT["created"].append(mat.get_path_name())

    tex = unreal.MaterialEditingLibrary.create_material_expression(
        mat, unreal.MaterialExpressionTextureSampleParameter2D, -500, 0)
    try_set(tex, ["parameter_name"], "SpriteTexture")
    # a texture object parameter must point at something valid or the material errors
    default_tex = unreal.EditorAssetLibrary.find_asset_data(
        "/Engine/EngineResources/DefaultTexture").get_asset()
    if default_tex is not None:
        try_set(tex, ["texture"], default_tex)

    res = {"created": True}
    res["blend"] = try_set(mat, ["blend_mode"], blend)
    res["shading"] = try_set(mat, ["shading_model"], unreal.MaterialShadingModel.MSM_UNLIT)
    res["two_sided"] = try_set(mat, ["two_sided"], True)
    res["domain"] = try_set(mat, ["material_domain"], unreal.MaterialDomain.MD_SURFACE)
    res["emissive"] = None
    try:
        res["emissive"] = bool(unreal.MaterialEditingLibrary.connect_material_property(
            tex, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR))
    except Exception as exc:  # noqa: BLE001
        res["emissive_error"] = str(exc)

    if wire_opacity:
        try:
            res["opacity"] = bool(unreal.MaterialEditingLibrary.connect_material_property(
                tex, "A", unreal.MaterialProperty.MP_OPACITY))
        except Exception as exc:  # noqa: BLE001
            res["opacity_error"] = str(exc)

    if opacity_mask:
        clip = unreal.MaterialEditingLibrary.create_material_expression(
            mat, unreal.MaterialExpressionScalarParameter, -500, 250)
        try_set(clip, ["parameter_name"], "OpacityClip")
        try_set(clip, ["default_value"], opacity_mask)
        try:
            res["opacity_mask"] = bool(unreal.MaterialEditingLibrary.connect_material_property(
                clip, "", unreal.MaterialProperty.MP_OPACITY_MASK))
        except Exception as exc:  # noqa: BLE001
            res["opacity_mask_error"] = str(exc)

    try:
        res["recompiled"] = bool(unreal.MaterialEditingLibrary.recompile_material(mat))
    except Exception as exc:  # noqa: BLE001
        res["recompile_error"] = str(exc)

    res["readback"] = enum_props(mat, ["blend_mode", "shading_model", "two_sided",
                                       "material_domain"])
    try:
        unreal.EditorAssetLibrary.save_asset(mat.get_path_name())
        res["saved"] = True
    except Exception as exc:  # noqa: BLE001
        res["saved"] = "ERR: %s" % exc
    return res


record("material.masked", lambda: _build_material(
    "M_ProbeMasked", unreal.BlendMode.BLEND_MASKED, True, 0.333))
record("material.translucent", lambda: _build_material(
    "M_ProbeTranslucent", unreal.BlendMode.BLEND_TRANSLUCENT, True, None))
record("material.opaque", lambda: _build_material(
    "M_ProbeOpaque", unreal.BlendMode.BLEND_OPAQUE, False, None))


# -------------------------------------------------------- 3. sprite + flipbook creation
def _make_sprite(name, tex):
    sprite = TOOLS.create_asset(name, PROBE_DIR, unreal.PaperSprite,
                                unreal.PaperSpriteFactory())
    if sprite is None:
        return None, {"created": False}
    RESULT["created"].append(sprite.get_path_name())
    out = {"created": True}
    out["source_texture"] = try_set(sprite, ["source_texture"], tex)
    out["source_uv"] = try_set(sprite, ["source_uv"], unreal.IntPoint(0, 0))
    out["source_dimension"] = try_set(sprite, ["source_dimension"], unreal.IntPoint(384, 320))
    out["pivot_mode"] = try_set(sprite, ["pivot_mode"], unreal.SpritePivotMode.CUSTOM)
    out["custom_pivot"] = try_set(sprite, ["custom_pivot_point"], unreal.Vector2D(192.0, 300.0))
    out["collision_domain"] = try_set(
        sprite, ["sprite_collision_domain"], unreal.SpriteCollisionMode.NONE)
    try:
        geo = sprite.get_editor_property("render_geometry")
        geo.set_editor_property("geometry_type", unreal.SpritePolygonMode.SOURCE_BOUNDING_BOX)
        sprite.set_editor_property("render_geometry", geo)
        out["geometry"] = "ok"
    except Exception as exc:  # noqa: BLE001
        out["geometry"] = "ERR: %s" % exc
    out["ppuu"] = try_set(sprite, ["pixels_per_unreal_unit"], 1.0)
    out["default_material"] = try_set(
        sprite, ["default_material"],
        unreal.EditorAssetLibrary.load_asset(PROBE_DIR + "/M_ProbeTranslucent"))
    out["readback"] = enum_props(sprite, ["source_uv", "source_dimension", "pivot_mode",
                                          "custom_pivot_point", "pixels_per_unreal_unit",
                                          "sprite_collision_domain", "default_material"])
    try:
        unreal.EditorAssetLibrary.save_asset(sprite.get_path_name())
        out["saved"] = True
    except Exception as exc:  # noqa: BLE001
        out["saved"] = "ERR: %s" % exc
    return sprite, out


def _probe_sprite_flipbook():
    tex = unreal.EditorAssetLibrary.load_asset(PROBE_DIR + "/T_ProbeIdle00")
    if tex is None:
        return {"texture_found": False}
    sprite, out = _make_sprite("SPR_ProbeIdle00", tex)
    if sprite is None:
        return out

    fb = TOOLS.create_asset("FB_ProbeIdle", PROBE_DIR, unreal.PaperFlipbook,
                            unreal.PaperFlipbookFactory())
    out["flipbook_created"] = fb is not None
    if fb is None:
        return out
    RESULT["created"].append(fb.get_path_name())

    kfs = []
    for _ in range(4):
        kf = unreal.PaperFlipbookKeyFrame()
        try_set(kf, ["sprite"], sprite)
        try_set(kf, ["frame_run"], 1)
        kfs.append(kf)
    out["set_key_frames"] = try_set(fb, ["key_frames"], kfs)
    out["set_fps"] = try_set(fb, ["frames_per_second"], 8.0)
    try:
        out["key_frames_readback"] = len(fb.get_editor_property("key_frames"))
    except Exception as exc:  # noqa: BLE001
        out["key_frames_readback"] = "ERR: %s" % exc
    out["fps_readback"] = try_get(fb, "frames_per_second")
    # flipbook length is derived from keyframes/fps; also check the sprite accessor
    try:
        out["sprite_at_0"] = str(fb.get_sprite_at_frame(0))
    except Exception as exc:  # noqa: BLE001
        out["sprite_at_0"] = "ERR: %s" % exc
    try:
        unreal.EditorAssetLibrary.save_asset(fb.get_path_name())
        out["saved"] = True
    except Exception as exc:  # noqa: BLE001
        out["saved"] = "ERR: %s" % exc
    return out


record("smoke.sprite_flipbook", _probe_sprite_flipbook)


# ------------------------------------------------- 4. runtime component / actor support
record("class.PaperFlipbookComponent_methods", lambda: sorted(
    m for m in dir(unreal.PaperFlipbookComponent) if not m.startswith("_"))[:60])
record("enum.SpriteCollisionMode", lambda: [e.name for e in unreal.SpriteCollisionMode])
record("enum.SpritePivotMode", lambda: [e.name for e in unreal.SpritePivotMode])

# -------------------------------------------------------------------- 5. cleanup
try:
    unreal.EditorAssetLibrary.delete_directory(PROBE_DIR)
    RESULT["cleanup"] = "deleted " + PROBE_DIR
except Exception as exc:  # noqa: BLE001
    RESULT["cleanup"] = "ERR: %s" % exc

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w", encoding="utf-8") as fh:
    json.dump(RESULT, fh, indent=1)

unreal.log("[RKC] probe3 written to %s" % OUT)
for name, rec in RESULT["checks"].items():
    if rec["ok"]:
        unreal.log("[RKC] OK   %s -> %s" % (name, str(rec["value"])[:400]))
    else:
        unreal.log_error("[RKC] FAIL %s -> %s" % (name, rec["error"]))
