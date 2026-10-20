"""rkc_diag_params.py - name-level read-back of the presentation materials.

WHY: Paper2D binds a sprite's own texture into the material ONLY through a
texture parameter literally named "SpriteTexture"
(FSpriteTextureOverrideRenderProxy::TextureParameterName). If a material calls
that parameter something else, the atlas never reaches it and the material
samples its own default value instead - an unset texture parameter renders
black. This script reports the actual parameter names and default texture
values, and which material each flipbook resolves to at runtime via
UPaperFlipbookComponent::GetMaterial() -> SourceFlipbook->GetDefaultMaterial().

Read-only. Writes Saved/Evidence/Presentation_params.json.
"""

import json
import os

import unreal

ART = "/Game/RockyKombat/Art"
MATS = [
    ART + "/Materials/M_RKC_SpriteMasked",
    ART + "/Materials/M_RKC_SpriteTranslucent",
    ART + "/Materials/M_RKC_Backdrop",
    "/Paper2D/DefaultSpriteMaterial",
    "/Paper2D/MaskedUnlitSpriteMaterial",
    "/Engine/EngineMaterials/DefaultMaterial",
]
SPRITE_MAT = ART + "/Materials/M_RKC_SpriteMasked"
OUT = r"C:\RockyKombat\Saved\Evidence\Presentation_params.json"


def asset_path_of(obj):
    if obj is None:
        return None
    try:
        return obj.get_path_name()
    except Exception:  # noqa: BLE001
        return str(obj)


def describe_material(path):
    info = {"path": path, "exists": False}
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        info["error"] = "asset does not exist"
        return info
    mat = unreal.EditorAssetLibrary.load_asset(path)
    if mat is None:
        info["error"] = "load_asset returned None"
        return info
    info["exists"] = True
    for prop in ("blend_mode", "shading_model", "two_sided", "opacity_mask_clip_value"):
        try:
            info[prop] = str(mat.get_editor_property(prop))
        except Exception as exc:  # noqa: BLE001
            info[prop] = "<unreadable: %s>" % exc

    tex_params = []
    scalar_params = []
    try:
        for e in unreal.MaterialEditingLibrary.get_material_expressions(mat):
            cls = str(e.get_class().get_name())
            entry = {"class": cls, "node": asset_path_of(e)}
            if "TextureSampleParameter" in cls:
                for prop in ("parameter_name", "texture", "sampler_type",
                             "is_default_meshpaint_texture"):
                    try:
                        val = e.get_editor_property(prop)
                        entry[prop] = asset_path_of(val) if prop == "texture" else str(val)
                    except Exception as exc:  # noqa: BLE001
                        entry[prop] = "<unreadable: %s>" % exc
                tex_params.append(entry)
            elif "ScalarParameter" in cls:
                for prop in ("parameter_name", "default_value"):
                    try:
                        entry[prop] = str(e.get_editor_property(prop))
                    except Exception as exc:  # noqa: BLE001
                        entry[prop] = "<unreadable: %s>" % exc
                scalar_params.append(entry)
    except Exception as exc:  # noqa: BLE001
        info["expression_error"] = str(exc)
    info["texture_parameters"] = tex_params
    info["scalar_parameters"] = scalar_params

    connected = {}
    for prop_name in ("MP_BASE_COLOR", "MP_EMISSIVE_COLOR", "MP_OPACITY",
                      "MP_OPACITY_MASK"):
        prop = getattr(unreal.MaterialProperty, prop_name, None)
        if prop is None:
            continue
        try:
            node = unreal.MaterialEditingLibrary.get_material_property_input_node(mat, prop)
            connected[prop_name] = asset_path_of(node)
        except Exception as exc:  # noqa: BLE001
            connected[prop_name] = "<unreadable: %s>" % exc
    info["connected_inputs"] = connected
    return info


def collect_assets(root, cls_name_filter):
    found = []
    try:
        paths = unreal.EditorAssetLibrary.list_assets(root, recursive=True, include_folder=False)
    except Exception as exc:  # noqa: BLE001
        return [{"error": str(exc)}]
    for p in paths:
        a = unreal.EditorAssetLibrary.load_asset(p)
        if a is None:
            continue
        if cls_name_filter in str(a.get_class().get_name()):
            found.append((p, a))
    return found


def describe_flipbooks():
    out = []
    for p, fb in collect_assets(ART + "/Characters", "PaperFlipbook"):
        entry = {"path": p}
        for prop in ("default_material", "frames_per_second"):
            try:
                val = fb.get_editor_property(prop)
                entry[prop] = asset_path_of(val) if prop == "default_material" else str(val)
            except Exception as exc:  # noqa: BLE001
                entry[prop] = "<unreadable: %s>" % exc
        try:
            kf = fb.get_editor_property("key_frames")
            entry["key_frame_count"] = len(kf) if kf is not None else 0
            sprites = []
            for k in (kf or []):
                sp = k.get_editor_property("sprite")
                sprites.append(asset_path_of(sp))
            entry["sprites"] = sprites
        except Exception as exc:  # noqa: BLE001
            entry["keyframe_error"] = str(exc)
        out.append(entry)
    return out


def describe_sprites():
    out = []
    for p, sp in collect_assets(ART + "/Characters", "PaperSprite"):
        entry = {"path": p}
        for prop in ("default_material", "source_texture"):
            try:
                entry[prop] = asset_path_of(sp.get_editor_property(prop))
            except Exception as exc:  # noqa: BLE001
                entry[prop] = "<unreadable: %s>" % exc
        out.append(entry)
    return out


def main():
    report = {"engine_version": unreal.SystemLibrary.get_engine_version()}
    report["materials"] = {p: describe_material(p) for p in MATS}
    report["flipbooks"] = describe_flipbooks()
    report["sprites"] = describe_sprites()

    names = set()
    for m in report["materials"].values():
        for tp in m.get("texture_parameters") or []:
            names.add(tp.get("parameter_name"))
    report["summary"] = {
        "texture_parameter_names": sorted(n for n in names if n),
        "engine_binds_atlas_to": "SpriteTexture",
        "atlas_binding_ok": names == {"SpriteTexture"},
        "flipbooks_total": len(report["flipbooks"]),
        "flipbooks_with_null_default_material": [
            f["path"] for f in report["flipbooks"] if not f.get("default_material")],
        "flipbooks_not_using_sprite_material": [
            f["path"] for f in report["flipbooks"]
            if f.get("default_material") and SPRITE_MAT not in str(f.get("default_material"))],
    }

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=2)
    unreal.log("RKC_DIAG_PARAMS_WRITTEN " + OUT)
    print(json.dumps(report["summary"], indent=2))


main()
