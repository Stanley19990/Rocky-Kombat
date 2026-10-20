"""
rkc_probe_api.py - Probe the live Unreal Python API surface for Paper2D.

Run inside the editor before importing art, so the importer uses real observed
names instead of guesses. Writes Saved/Evidence/API_probe.json.
"""

import json
import os
import sys

import unreal

OUT = os.path.join(unreal.Paths.project_saved_dir(), "Evidence", "API_probe.json")

report = {
    "engine_version": unreal.SystemLibrary.get_engine_version(),
    "project_dir": unreal.Paths.project_dir(),
    "classes": {},
    "enums": {},
    "materials": {},
    "existing_art_assets": [],
    "errors": [],
}


def values_of_enum(name):
    e = getattr(unreal, name, None)
    if e is None:
        return None
    out = []
    for attr in dir(e):
        if attr.startswith("_"):
            continue
        try:
            v = getattr(e, attr)
        except Exception:
            continue
        if isinstance(v, e):
            out.append(attr)
    return sorted(out)


def describe_class(name):
    cls = getattr(unreal, name, None)
    if cls is None:
        return {"present": False}
    props = []
    try:
        for p in unreal.ObjectLibrary.get_class_properties(cls.static_class() if hasattr(cls, "static_class") else cls):
            props.append(p.get_editor_property("name"))
    except Exception as exc:
        report["errors"].append(f"props({name}): {exc}")
    return {"present": True, "properties": sorted(set(props))}


def main():
    # --- enums we depend on -------------------------------------------------
    for ename in (
        "SpritePivotMode",
        "SpritePolygonMode",
        "SpriteCollisionMode",
        "SpriteShapeType",
        "TextureCompressionSettings",
        "TextureMipGenSettings",
        "TextureFilter",
        "TextureAddress",
        "TextureGroup",
        "TextureSamplerFilter",
    ):
        report["enums"][ename] = values_of_enum(ename)

    # --- classes we depend on ----------------------------------------------
    for cname in (
        "PaperSprite",
        "PaperFlipbook",
        "PaperFlipbookKeyFrame",
        "PaperSpriteFactory",
        "PaperFlipbookFactory",
        "PaperSpriteComponent",
        "PaperFlipbookComponent",
        "PaperSpriteActor",
        "PaperFlipbookActor",
        "TextureFactory",
        "AssetImportTask",
        "AssetTools",
        "EditorAssetLibrary",
        "Texture2D",
    ):
        report["classes"][cname] = describe_class(cname)

    # --- keyframe construction test ----------------------------------------
    try:
        kf = unreal.PaperFlipbookKeyFrame()
        kf.set_editor_property("sprite", None)
        kf.set_editor_property("frame_run", 1)
        report["keyframe_construct"] = {
            "ok": True,
            "frame_run_readback": kf.get_editor_property("frame_run"),
        }
    except Exception as exc:
        report["keyframe_construct"] = {"ok": False, "error": str(exc)}

    # --- Paths helper -------------------------------------------------------
    try:
        report["project_content_dir"] = unreal.Paths.project_content_dir()
    except Exception as exc:
        report["errors"].append(f"project_content_dir: {exc}")

    # --- Paper2D bundled materials -----------------------------------------
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    try:
        assets = ar.get_assets_by_path("/Paper2D", recursive=True)
        report["paper2d_assets"] = sorted({str(a.package_name) for a in assets})
    except Exception as exc:
        report["errors"].append(f"asset_registry /Paper2D: {exc}")

    for mat_path in (
        "/Paper2D/DefaultSpriteMaterial",
        "/Paper2D/DefaultLitSpriteMaterial",
    ):
        try:
            mat = unreal.load_asset(mat_path)
            if mat is None:
                report["materials"][mat_path] = {"exists": False}
                continue
            entry = {"exists": True}
            for prop in (
                "blend_mode",
                "shading_model",
                "two_sided",
                "material_domain",
                "blendable_location",
            ):
                try:
                    entry[prop] = str(mat.get_editor_property(prop))
                except Exception as exc:
                    entry[prop] = f"<err {exc}>"
            report["materials"][mat_path] = entry
        except Exception as exc:
            report["materials"][mat_path] = {"exists": False, "error": str(exc)}

    # --- existing art in the project (idempotency check) --------------------
    try:
        for a in ar.get_assets_by_path("/Game/RockyKombat/Art", recursive=True):
            report["existing_art_assets"].append(str(a.package_name))
    except Exception as exc:
        report["errors"].append(f"asset_registry /Game/RockyKombat/Art: {exc}")

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=2)

    print("PROBE_WROTE", OUT)
    print("engine", report["engine_version"])
    for k, v in report["enums"].items():
        print(f"enum {k}: {v}")
    for k, v in report["classes"].items():
        print(f"class {k}: present={v.get('present')}")
    print("keyframe:", report.get("keyframe_construct"))
    for k, v in report["materials"].items():
        print(f"material {k}: {v}")
    print("paper2d assets:", len(report.get("paper2d_assets", [])))
    for e in report["errors"]:
        print("ERROR:", e)


main()
