"""
rkc_probe_api2.py - Second, focused engine API probe.

The first probe confirmed the Paper2D classes exist but could not enumerate their
properties (unreal.ObjectLibrary is missing in 5.8). This probe answers only the
questions the importer actually depends on, and it answers them defensively: every
capability is reported as present/absent with the error text, so the importer can
branch instead of crashing.

Run:
  UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script=<this> -unattended -nosplash
"""

import json
import os
import sys
import traceback

import unreal  # noqa: E402

OUT = os.path.join(unreal.Paths.project_saved_dir(), "Evidence", "API_probe2.json")
RESULT = {"engine_version": unreal.SystemLibrary.get_engine_version(), "checks": {}}


def record(name, fn):
    try:
        RESULT["checks"][name] = {"ok": True, "value": fn()}
    except Exception as exc:  # noqa: BLE001
        RESULT["checks"][name] = {"ok": False, "error": "%s: %s" % (type(exc).__name__, exc),
                                  "trace": traceback.format_exc().splitlines()[-3:]}


def props(cls):
    """Editor-exposed property names for a UStruct/UClass, via the generated API."""
    out = {}
    try:
        for entry in unreal.EditorAssetLibrary.__dict__:
            pass
    except Exception:
        pass
    # unreal exposes Python property descriptors; walk the class dict up the MRO
    for klass in cls.__mro__:
        for key, val in getattr(klass, "__dict__", {}).items():
            if isinstance(val, property):
                out[key] = "property"
    return sorted(out.keys())


# ------------------------------------------------------------------ modules
record("module.MaterialEditingLibrary", lambda: bool(unreal.MaterialEditingLibrary))
record("module.MaterialFactoryNew", lambda: bool(unreal.MaterialFactoryNew))
record("module.TextureSampleParameter2D", lambda: bool(unreal.MaterialExpressionTextureSampleParameter2D))
record("module.MaterialExpressionScalarParameter", lambda: bool(unreal.MaterialExpressionScalarParameter))
record("module.AssetToolsHelpers", lambda: bool(unreal.AssetToolsHelpers.get_asset_tools()))
record("module.EditorAssetLibrary", lambda: bool(unreal.EditorAssetLibrary))
record("module.EditorLevelLibrary", lambda: bool(unreal.EditorLevelLibrary))
record("module.ScreenshotRequest", lambda: bool(unreal.AutomationLibrary))

# ------------------------------------------------------------------ enums
record("enum.BlendMode", lambda: [e.name for e in unreal.BlendMode])
record("enum.MaterialShadingModel", lambda: [e.name for e in unreal.MaterialShadingModel])
record("enum.MaterialDomain", lambda: [e.name for e in unreal.MaterialDomain])
record("enum.TranslucencyLightingMode", lambda: [e.name for e in unreal.TranslucencyLightingMode])

# ------------------------------------------------------------ material edits
record("material.expression_creation", lambda: _probe_material())


def _probe_material():
    """Actually build a throwaway material graph and read the result back."""
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    path = "/Game/RockyKombat/Art/_Probe"
    name = "M_RKC_ProbeThrowaway"
    mat = unreal.MaterialEditingLibrary.create_material_expression  # attribute presence
    factory = unreal.MaterialFactoryNew()
    asset = tools.create_asset(name, path, unreal.Material, factory)
    if asset is None:
        return {"created": False}
    tex = unreal.MaterialEditingLibrary.create_material_expression(
        asset, unreal.MaterialExpressionTextureSampleParameter2D, -400, 0)
    tex.set_editor_property("parameter_name", "SpriteTexture")
    conn_emissive = unreal.MaterialEditingLibrary.connect_material_property(
        tex, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    conn_opacity = unreal.MaterialEditingLibrary.connect_material_property(
        tex, "A", unreal.MaterialProperty.MP_OPACITY)
    asset.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    asset.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    asset.set_editor_property("two_sided", True)
    try:
        asset.set_editor_property("translucency_lighting_mode",
                                  unreal.TranslucencyLightingMode.TLM_UNLIT)
    except Exception as exc:
        RESULT["checks"]["material.translucency_lighting_mode"] = {"ok": False, "error": str(exc)}
    unreal.MaterialEditingLibrary.recompile_material(asset)
    unreal.EditorAssetLibrary.save_asset(asset.get_path_name())
    info = {
        "created": True,
        "emissive_connected": bool(conn_emissive),
        "opacity_connected": bool(conn_opacity),
        "blend_mode_readback": str(asset.get_editor_property("blend_mode")),
        "asset_path": asset.get_path_name(),
    }
    unreal.EditorAssetLibrary.delete_asset(asset.get_path_name())
    return info


# ------------------------------------------------------------------ textures
def _probe_texture():
    tex = unreal.EditorAssetLibrary.find_asset_data(
        "/Engine/EngineResources/DefaultTexture").get_asset()
    if tex is None:
        return {"found": False}
    names = ["compression_settings", "srgb", "mip_gen_settings", "filter", "lod_group",
             "max_texture_size", "address_x", "address_y", "compression_no_alpha",
             "never_stream", "virtual_texture_streaming"]
    got = {}
    for n in names:
        try:
            got[n] = str(tex.get_editor_property(n))
        except Exception as exc:  # noqa: BLE001
            got[n] = "ERR: %s" % exc
    return got


record("texture.property_names", _probe_texture)

# ------------------------------------------------------------------- sprites
record("sprite.property_names", lambda: props(unreal.PaperSprite))
record("flipbook.property_names", lambda: props(unreal.PaperFlipbook))
record("flipbook_keyframe.property_names", lambda: props(unreal.PaperFlipbookKeyFrame))
record("sprite_factory.property_names", lambda: props(unreal.PaperSpriteFactory))
record("flipbook_factory.property_names", lambda: props(unreal.PaperFlipbookFactory))
record("flipbook_component.property_names", lambda: props(unreal.PaperFlipbookComponent))
record("geometry_collection.property_names", lambda: props(unreal.SpriteGeometryCollection))

# ------------------------------------------- end-to-end sprite+flipbook smoke
def _probe_sprite_flipbook():
    """Build a real sprite and flipbook from a real engine texture, then delete them."""
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    base = "/Game/RockyKombat/Art/_Probe"
    tex = unreal.EditorAssetLibrary.find_asset_data(
        "/Engine/EngineResources/DefaultTexture").get_asset()
    if tex is None:
        return {"found_texture": False}

    sf = unreal.PaperSpriteFactory()
    try:
        sf.set_editor_property("initial_texture", tex)
    except Exception as exc:  # noqa: BLE001
        return {"initial_texture_error": str(exc)}
    sprite = tools.create_asset("SPR_ProbeThrowaway", base, unreal.PaperSprite, sf)
    out = {"sprite_created": sprite is not None}
    if sprite:
        # full canvas region, custom pivot
        try:
            sprite.set_editor_property("source_uv", unreal.IntPoint(0, 0))
        except Exception as exc:  # noqa: BLE001
            out["source_uv_error"] = str(exc)
        try:
            sprite.set_editor_property("source_dimension", unreal.IntPoint(384, 320))
        except Exception as exc:  # noqa: BLE001
            out["source_dimension_error"] = str(exc)
        try:
            sprite.set_editor_property("pivot_mode", unreal.SpritePivotMode.CUSTOM)
            sprite.set_editor_property("custom_pivot_point", unreal.Vector2D(192.0, 300.0))
            out["pivot_readback"] = str(sprite.get_editor_property("custom_pivot_point"))
        except Exception as exc:  # noqa: BLE001
            out["pivot_error"] = str(exc)
        try:
            sprite.set_editor_property("sprite_collision_domain", unreal.SpriteCollisionMode.NONE)
        except Exception as exc:  # noqa: BLE001
            out["collision_error"] = str(exc)
        try:
            geo = sprite.get_editor_property("render_geometry")
            geo.set_editor_property("geometry_type", unreal.SpritePolygonMode.SOURCE_BOUNDING_BOX)
            sprite.set_editor_property("render_geometry", geo)
            out["geometry_type"] = str(sprite.get_editor_property("render_geometry").get_editor_property("geometry_type"))
        except Exception as exc:  # noqa: BLE001
            out["geometry_error"] = str(exc)
        try:
            out["pixels_per_unreal_unit"] = str(sprite.get_editor_property("pixels_per_unreal_unit"))
        except Exception as exc:  # noqa: BLE001
            out["ppuu_error"] = str(exc)
        unreal.EditorAssetLibrary.save_asset(sprite.get_path_name())

    ff = unreal.PaperFlipbookFactory()
    fb = tools.create_asset("FB_ProbeThrowaway", base, unreal.PaperFlipbook, ff)
    out["flipbook_created"] = fb is not None
    if fb and sprite:
        kf = unreal.PaperFlipbookKeyFrame()
        kf.set_editor_property("sprite", sprite)
        kf.set_editor_property("frame_run", 1)
        try:
            fb.set_editor_property("key_frames", [kf])
            fb.set_editor_property("frames_per_second", 8.0)
            out["key_frames_readback"] = len(fb.get_editor_property("key_frames"))
            out["fps_readback"] = str(fb.get_editor_property("frames_per_second"))
        except Exception as exc:  # noqa: BLE001
            out["flipbook_set_error"] = str(exc)
        unreal.EditorAssetLibrary.save_asset(fb.get_path_name())

    for p in ("/Game/RockyKombat/Art/_Probe"):
        try:
            unreal.EditorAssetLibrary.delete_directory(p)
        except Exception:  # noqa: BLE001
            pass
    return out


record("smoke.sprite_and_flipbook", _probe_sprite_flipbook)

# ------------------------------------------------------------------- output
os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w", encoding="utf-8") as fh:
    json.dump(RESULT, fh, indent=1)

unreal.log("[RKC] probe2 written to %s" % OUT)
for name, rec in RESULT["checks"].items():
    if rec["ok"]:
        unreal.log("[RKC] OK   %s -> %s" % (name, str(rec["value"])[:300]))
    else:
        unreal.log_error("[RKC] FAIL %s -> %s" % (name, rec["error"]))
sys.exit(0)
