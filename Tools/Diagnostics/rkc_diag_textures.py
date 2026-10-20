"""rkc_diag_textures.py - read-back of the streaming-relevant flags on every
texture the fighters and the arena draw with.

WHY: UPaperFlipbookComponent::GetStreamingRenderAssetInfo() and
UPaperSpriteComponent::GetStreamingRenderAssetInfo() are both engine TODOs - they
delegate to UPrimitiveComponent, which reports only the textures reachable from
GetUsedMaterials(). Paper2D binds the frame texture through
FSpriteTextureOverrideRenderProxy (a dynamic render proxy), not through the
material asset, so the fighters' textures are never reported as "used" and the
streaming manager is free to evict them. If a texture is streamable and evicted,
the unlit masked sprite samples a black texture and the fighter renders as a
black silhouette.

This script reports, per texture, the flags that decide whether that can happen:
never_stream (takes the texture out of the streaming manager entirely),
mip_gen_settings, compression, srgb, lod_group, size and the on-disk size.

Read-only. Writes Saved/Evidence/Presentation_textures.json.
"""

import json
import os

import unreal

ART = "/Game/RockyKombat"
OUT = r"C:\RockyKombat\Saved\Evidence\Presentation_textures.json"

PROPS = (
    "never_stream",
    "mip_gen_settings",
    "compression_settings",
    "srgb",
    "lod_group",
    "max_texture_size",
    "address_x",
    "address_y",
    "filter",
    "compression_no_alpha",
    "virtual_texture_streaming",
)


def describe(path, tex):
    info = {"path": path}
    for prop in PROPS:
        try:
            info[prop] = str(tex.get_editor_property(prop))
        except Exception as exc:  # noqa: BLE001
            info[prop] = "<unreadable: %s>" % exc
    for fn in ("get_size_x", "get_size_y"):
        try:
            info[fn] = int(getattr(tex, fn)())
        except Exception as exc:  # noqa: BLE001
            info[fn] = "<unreadable: %s>" % exc
    try:
        info["blueprint_class"] = str(tex.get_class().get_name())
    except Exception as exc:  # noqa: BLE001
        info["blueprint_class"] = "<unreadable: %s>" % exc
    return info


def main():
    report = {"engine_version": unreal.SystemLibrary.get_engine_version(),
              "textures": []}
    paths = unreal.EditorAssetLibrary.list_assets(ART, recursive=True, include_folder=False)
    for p in paths:
        a = unreal.EditorAssetLibrary.load_asset(p)
        if a is None:
            continue
        cls = str(a.get_class().get_name())
        if "Texture2D" not in cls:
            continue
        report["textures"].append(describe(p, a))

    streamable = [t["path"] for t in report["textures"]
                  if t.get("never_stream") == "False"]
    report["summary"] = {
        "texture_count": len(report["textures"]),
        "streamable_count": len(streamable),
        "streamable": streamable,
        "never_stream_true": [t["path"] for t in report["textures"]
                              if t.get("never_stream") == "True"],
        "mip_gen_settings_seen": sorted({str(t.get("mip_gen_settings"))
                                         for t in report["textures"]}),
        "compression_seen": sorted({str(t.get("compression_settings"))
                                    for t in report["textures"]}),
        "target_folder": ART,
    }

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=2)
    unreal.log("RKC_DIAG_TEXTURES_WRITTEN " + OUT)
    print(json.dumps(report["summary"], indent=2))


main()
