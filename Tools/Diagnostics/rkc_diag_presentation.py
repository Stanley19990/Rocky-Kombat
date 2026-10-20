"""
rkc_diag_presentation.py - dump the real on-disk state of every imported
presentation asset, so the "fighters intermittently turn black" report can be
diagnosed from measured asset data instead of assumption.

WHY THIS EXISTS: the importer configures material properties through a
helper (try_set) that swallows exceptions. If any of those property writes
silently failed - shading_model, two_sided, or the material assignment itself -
the asset on disk will not match the importer's intent, and the failure is
invisible in the import report. This script reads the committed assets back and
reports what they ACTUALLY are.

It also checks the engine's own DefaultSpriteMaterial for comparison, because
"what a sprite renders with when no material is assigned" is the other half of
the question.

Nothing here mutates the project. Read-only.

Run:
  UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script=<this> \
      -unattended -nosplash -nullrhi

Writes: Saved/Evidence/Presentation_diag.json
"""

import json
import os
import traceback

import unreal

ART = "/Game/RockyKombat/Art"
MATS = [
    ART + "/Materials/M_RKC_SpriteMasked",
    ART + "/Materials/M_RKC_SpriteTranslucent",
    ART + "/Materials/M_RKC_Backdrop",
    # The engine's own fallback, for comparison against the project materials.
    "/Paper2D/DefaultSpriteMaterial",
    "/Engine/EngineMaterials/DefaultMaterial",
]

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()).rstrip("/")
OUT_PATH = PROJECT_DIR + "/Saved/Evidence/Presentation_diag.json"

REPORT = {
    "engine_version": unreal.SystemLibrary.get_engine_version(),
    "materials": {},
    "flipbooks": {},
    "sprites": {},
    "textures": {},
    "summary": {},
    "errors": [],
}


def log(msg):
    unreal.log("[RKC-DIAG] " + str(msg))


def record_error(where, exc):
    entry = "%s: %s: %s" % (where, type(exc).__name__, exc)
    REPORT["errors"].append(entry)
    unreal.log_error("[RKC-DIAG] FAIL " + entry)


def enum_name(value):
    """Editor property enums come back as unreal enum wrappers or plain ints."""
    if value is None:
        return None
    for attr in ("name", "value"):
        try:
            got = getattr(value, attr)
            if isinstance(got, str):
                return got
        except Exception:  # noqa: BLE001
            pass
    return str(value)


def asset_path_of(obj):
    if obj is None:
        return None
    try:
        return str(obj.get_path_name())
    except Exception:  # noqa: BLE001
        try:
            return str(obj.get_name())
        except Exception:  # noqa: BLE001
            return "<unprintable>"


def describe_material(path):
    """Read back every property the importer tried to set, plus the graph."""
    info = {"path": path, "exists": False}
    try:
        if not unreal.EditorAssetLibrary.does_asset_exist(path):
            info["error"] = "asset does not exist"
            return info

        mat = unreal.EditorAssetLibrary.load_asset(path)
        if mat is None:
            info["error"] = "load_asset returned None"
            return info

        info["exists"] = True
        info["class"] = str(mat.get_class().get_name())

        for prop in ("blend_mode", "shading_model", "two_sided", "material_domain",
                     "opacity_mask_clip_value", "used_with_skeletal_mesh",
                     "used_with_static_lighting", "used_with_particle_sprites",
                     "used_with_instanced_static_meshes"):
            try:
                info[prop] = enum_name(mat.get_editor_property(prop))
            except Exception as exc:  # noqa: BLE001
                info[prop] = "<unreadable: %s>" % exc

        # Which property inputs actually have something connected. This is what
        # decides whether alpha is honoured at all.
        connected = {}
        for prop_name in ("MP_BASE_COLOR", "MP_EMISSIVE_COLOR", "MP_OPACITY",
                          "MP_OPACITY_MASK", "MP_METALLIC", "MP_ROUGHNESS",
                          "MP_SPECULAR", "MP_NORMAL"):
            prop = getattr(unreal.MaterialProperty, prop_name, None)
            if prop is None:
                continue
            try:
                node = unreal.MaterialEditingLibrary.get_material_property_input_node(mat, prop)
                connected[prop_name] = (asset_path_of(node) if node
                                        else None)
                # If a scalar/constant is driving it, its default value matters.
                if node is not None:
                    try:
                        connected[prop_name + "_default_value"] = node.get_editor_property("default_value")
                    except Exception:  # noqa: BLE001
                        try:
                            connected[prop_name + "_default_value"] = node.get_editor_property("r")
                        except Exception:  # noqa: BLE001
                            pass
            except Exception as exc:  # noqa: BLE001
                connected[prop_name] = "<unreadable: %s>" % exc
        info["connected_inputs"] = connected

        # Every expression in the graph, so a wrong wiring is visible rather
        # than inferred.
        try:
            exprs = unreal.MaterialEditingLibrary.get_material_expressions(mat)
            info["expressions"] = [
                {"class": str(e.get_class().get_name()),
                 "x": getattr(e, "material_expression_editor_x", None),
                 "y": getattr(e, "material_expression_editor_y", None)}
                for e in exprs
            ]
        except Exception as exc:  # noqa: BLE001
            info["expressions"] = "<unreadable: %s>" % exc

    except Exception as exc:  # noqa: BLE001
        record_error("material " + path, exc)
        info["error"] = str(exc)
    return info


def describe_sprite(path, obj=None):
    info = {"path": path}
    try:
        spr = obj or unreal.EditorAssetLibrary.load_asset(path)
        if spr is None:
            info["error"] = "load failed"
            return info
        for prop in ("default_material", "source_texture", "source_dimension",
                     "source_uv", "pixels_per_unreal_unit", "custom_pivot_point",
                     "pivot_mode", "sprite_collision_domain"):
            try:
                val = spr.get_editor_property(prop)
                if prop in ("default_material", "source_texture"):
                    info[prop] = asset_path_of(val)
                else:
                    info[prop] = str(val)
            except Exception as exc:  # noqa: BLE001
                info[prop] = "<unreadable: %s>" % exc

        try:
            geo = spr.get_editor_property("render_geometry")
            info["render_geometry_type"] = enum_name(geo.get_editor_property("geometry_type"))
            info["render_geometry_vertices"] = len(
                list(geo.get_editor_property("vertices") or []))
            info["render_geometry_triangles"] = len(
                list(geo.get_editor_property("triangles") or []))
        except Exception as exc:  # noqa: BLE001
            info["render_geometry"] = "<unreadable: %s>" % exc
    except Exception as exc:  # noqa: BLE001
        record_error("sprite " + path, exc)
        info["error"] = str(exc)
    return info


def describe_flipbook(path):
    info = {"path": path, "key_frames": []}
    try:
        fb = unreal.EditorAssetLibrary.load_asset(path)
        if fb is None:
            info["error"] = "load failed"
            return info

        # The property the component falls back to when it has no override.
        try:
            info["default_material"] = asset_path_of(fb.get_editor_property("default_material"))
        except Exception as exc:  # noqa: BLE001
            info["default_material"] = "<unreadable: %s>" % exc

        try:
            kfs = list(fb.get_editor_property("key_frames") or [])
        except Exception as exc:  # noqa: BLE001
            info["error"] = "key_frames unreadable: %s" % exc
            return info

        info["num_key_frames"] = len(kfs)
        try:
            info["frames_per_second"] = fb.get_editor_property("frames_per_second")
        except Exception as exc:  # noqa: BLE001
            info["frames_per_second"] = "<unreadable: %s>" % exc

        for kf in kfs:
            entry = {}
            try:
                spr = kf.get_editor_property("sprite")
                entry["sprite"] = asset_path_of(spr)
                try:
                    entry["frame_run"] = kf.get_editor_property("frame_run")
                except Exception:  # noqa: BLE001
                    entry["frame_run"] = None
                if spr is not None:
                    # Each keyframe's own sprite material is what the renderer
                    # would use if the flipbook has no material of its own.
                    try:
                        entry["sprite_default_material"] = asset_path_of(
                            spr.get_editor_property("default_material"))
                    except Exception as exc:  # noqa: BLE001
                        entry["sprite_default_material"] = "<unreadable: %s>" % exc
            except Exception as exc:  # noqa: BLE001
                entry["error"] = str(exc)
            info["key_frames"].append(entry)

        info["num_frames_total"] = sum(
            int(e.get("frame_run") or 1) for e in info["key_frames"])
    except Exception as exc:  # noqa: BLE001
        record_error("flipbook " + path, exc)
        info["error"] = str(exc)
    return info


def describe_texture(path):
    info = {"path": path}
    try:
        tex = unreal.EditorAssetLibrary.load_asset(path)
        if tex is None:
            info["error"] = "load failed"
            return info
        for prop in ("compression_settings", "mip_gen_settings", "srgb", "lod_group",
                     "never_stream", "address_x", "address_y", "filter",
                     "compression_no_alpha", "max_texture_size", "is_virtual"):
            try:
                info[prop] = enum_name(tex.get_editor_property(prop))
            except Exception as exc:  # noqa: BLE001
                info[prop] = "<unreadable: %s>" % exc
        for prop in ("blueprint_get_size_x", "blueprint_get_size_y"):
            try:
                fn = getattr(tex, prop)
                info[prop] = fn() if callable(fn) else fn
            except Exception as exc:  # noqa: BLE001
                info[prop] = "<unreadable: %s>" % exc
        try:
            info["resource_size_bytes"] = tex.get_resource_size()
        except Exception:  # noqa: BLE001
            pass
    except Exception as exc:  # noqa: BLE001
        record_error("texture " + path, exc)
        info["error"] = str(exc)
    return info


def main():
    log("starting presentation diagnostic")

    # ---------------------------------------------------------------- materials
    for path in MATS:
        REPORT["materials"][path] = describe_material(path)

    # ------------------------------------------------- flipbooks and their sprites
    try:
        fb_assets = unreal.EditorAssetLibrary.list_assets(
            ART + "/Characters", recursive=True, include_folder=False)
        fb_paths = sorted({str(p).split(".")[0] for p in fb_assets})
    except Exception as exc:  # noqa: BLE001
        record_error("list_assets Characters", exc)
        fb_paths = []

    sprite_paths = []
    for path in fb_paths:
        short = path.rsplit("/", 1)[-1]
        if short.startswith("FB_"):
            REPORT["flipbooks"][path] = describe_flipbook(path)
        elif short.startswith("SPR_"):
            sprite_paths.append(path)

    for path in sprite_paths:
        REPORT["sprites"][path] = describe_sprite(path)

    # ---------------------------------------------------------------- textures
    tex_paths = []
    try:
        a = unreal.EditorAssetLibrary.list_assets(ART, recursive=True, include_folder=False)
        tex_paths = sorted({str(p).split(".")[0] for p in a
                            if str(p).rsplit("/", 1)[-1].startswith("T_")})
    except Exception as exc:  # noqa: BLE001
        record_error("list_assets Art", exc)

    for path in tex_paths:
        REPORT["textures"][path] = describe_texture(path)

    # ------------------------------------------------------------------ summary
    # The specific questions that decide the black-fighter diagnosis.
    masked = REPORT["materials"].get(MATS[0], {})
    transl = REPORT["materials"].get(MATS[1], {})
    engine_default = REPORT["materials"].get("/Paper2D/DefaultSpriteMaterial", {})

    fbs_without_material = []
    fbs_with_material = []
    sprite_material_counts = {}
    for path, fb in REPORT["flipbooks"].items():
        if fb.get("default_material"):
            fbs_with_material.append(path)
        else:
            fbs_without_material.append(path)
        for kf in fb.get("key_frames", []):
            m = kf.get("sprite_default_material")
            sprite_material_counts[m] = sprite_material_counts.get(m, 0) + 1

    conn_masked = masked.get("connected_inputs", {}) or {}
    conn_transl = transl.get("connected_inputs", {}) or {}

    REPORT["summary"] = {
        "art_available": bool(REPORT["flipbooks"]),
        "flipbook_count": len(REPORT["flipbooks"]),
        "sprite_count": len(REPORT["sprites"]),
        "texture_count": len(REPORT["textures"]),
        "error_count": len(REPORT["errors"]),

        "masked_blend_mode": masked.get("blend_mode"),
        "masked_shading_model": masked.get("shading_model"),
        "masked_two_sided": masked.get("two_sided"),
        "masked_opacity_clip": masked.get("opacity_mask_clip_value"),
        "masked_has_emissive": bool(conn_masked.get("MP_EMISSIVE_COLOR")),
        "masked_has_basecolor": bool(conn_masked.get("MP_BASE_COLOR")),
        "masked_has_opacity": bool(conn_masked.get("MP_OPACITY")),
        "masked_has_opacity_mask": bool(conn_masked.get("MP_OPACITY_MASK")),
        "masked_opacity_node_is_constant": str(
            conn_masked.get("MP_OPACITY_MASK") or "").find("ScalarParameter") >= 0,
        "masked_opacity_mask_default": conn_masked.get("MP_OPACITY_MASK_default_value"),

        "translucent_blend_mode": transl.get("blend_mode"),
        "translucent_shading_model": transl.get("shading_model"),
        "translucent_two_sided": transl.get("two_sided"),
        "translucent_has_emissive": bool(conn_transl.get("MP_EMISSIVE_COLOR")),
        "translucent_has_opacity": bool(conn_transl.get("MP_OPACITY")),

        "engine_default_sprite_material_blend": engine_default.get("blend_mode"),
        "engine_default_sprite_material_shading": engine_default.get("shading_model"),
        "engine_default_sprite_material_two_sided": engine_default.get("two_sided"),

        "flipbooks_without_default_material": fbs_without_material,
        "flipbooks_with_default_material_count": len(fbs_with_material),
        "sprite_material_distribution": sprite_material_counts,
    }

    os.makedirs(os.path.dirname(OUT_PATH), exist_ok=True)
    with open(OUT_PATH, "w", encoding="utf-8") as fh:
        json.dump(REPORT, fh, indent=2, default=str)

    log("wrote " + OUT_PATH)
    log("summary: " + json.dumps(REPORT["summary"], indent=2, default=str))
    log("errors: %d" % len(REPORT["errors"]))


try:
    main()
except Exception as exc:  # noqa: BLE001
    record_error("main", exc)
    traceback.print_exc()
    try:
        with open(OUT_PATH, "w", encoding="utf-8") as fh:
            json.dump(REPORT, fh, indent=2, default=str)
    except Exception:  # noqa: BLE001
        pass
