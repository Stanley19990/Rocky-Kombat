"""
rkc_import_audio.py - import the generated Rocky Kombat audio set into UE content.

Imports, into /Game/RockyKombat/Audio, exactly the layout that
Source/RockyKombat/Audio/RKCAudio.cpp builds at runtime:

    <AudioContentRoot>/<Folder>/<Stem>.<Stem>

so Music/MUS_Title.wav becomes
    /Game/RockyKombat/Audio/Music/MUS_Title.MUS_Title

The four looping assets (three music beds and the rooftop ambience) get
bLooping set on the imported SoundWave. The runtime loader can force the flag
itself, but an asset that is correct on disk is better than a runtime patch and
keeps the "was not flagged as looping" warning out of the logs.

Also verifies every imported SoundWave by loading it back and reading its
duration, so "imported" means "resolvable at the runtime path".

Run:
  UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script=<this> \
      -unattended -nosplash -nullrhi

Idempotent: an existing destination asset is deleted and re-imported, never
duplicated.
"""

import json
import os
import traceback

import unreal

# --------------------------------------------------------------------------- paths
PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()).rstrip("/")
SRC_ROOT = PROJECT_DIR + "/ArtSource/Audio"
DEST_ROOT = "/Game/RockyKombat/Audio"

# The catalog's looping entries (folder/stem), matching RKCAudio.cpp.
LOOPING = {
    "Music/MUS_Title",
    "Music/MUS_Menu",
    "Music/MUS_Arena",
    "Ambience/AMB_Rooftop",
}

REPORT_PATH = PROJECT_DIR + "/Saved/Evidence/Audio_import_report.json"

REPORT = {
    "engine_version": unreal.SystemLibrary.get_engine_version(),
    "source_root": SRC_ROOT,
    "dest_root": DEST_ROOT,
    "found": 0,
    "imported": [],
    "failed": [],
    "looping_set": [],
    "looping_warning": [],
    "verified": [],
    "verify_failed": [],
    "notes": [],
}


def discover():
    """Every .wav under the source root, as (absolute path, folder, stem)."""
    found = []
    for root, _dirs, files in os.walk(SRC_ROOT):
        for name in sorted(files):
            if not name.lower().endswith(".wav"):
                continue
            full = os.path.join(root, name).replace("\\", "/")
            rel = os.path.relpath(full, SRC_ROOT).replace("\\", "/")
            folder = os.path.dirname(rel)
            stem = os.path.splitext(os.path.basename(rel))[0]
            found.append((full, folder, stem))
    # Deterministic order: folder then stem, so the report is comparable run to run.
    found.sort(key=lambda t: (t[1], t[2]))
    return found


def set_looping(asset, key):
    """Set bLooping on an imported SoundWave, trying both spellings.

    Returns None on success, or a short reason string on failure. Nothing here is
    guessed silently: an unsettable flag is recorded in the report.
    """
    for prop in ("looping", "bLooping"):
        try:
            if asset.get_editor_property(prop) is None:
                continue
            asset.set_editor_property(prop, True)
            if bool(asset.get_editor_property(prop)):
                return None
        except Exception:  # noqa: BLE001 - property name varies by engine build
            continue
    return "no writable looping property on SoundWave for '%s'" % key


def main():
    tools = unreal.AssetToolsHelpers.get_asset_tools()

    assets = discover()
    REPORT["found"] = len(assets)
    if not assets:
        REPORT["notes"].append("no .wav files found under %s" % SRC_ROOT)
        write_report()
        return

    for full, folder, stem in assets:
        key = "%s/%s" % (folder, stem)
        dest_path = "%s/%s" % (DEST_ROOT, folder)
        dest_object_path = "%s/%s.%s" % (dest_path, stem, stem)

        # Import one file per task: a single bad WAV then cannot take the whole
        # batch down, and the failure is attributable to a named file.
        try:
            if unreal.EditorAssetLibrary.does_asset_exist(dest_object_path):
                unreal.EditorAssetLibrary.delete_asset(dest_object_path)

            task = unreal.AssetImportTask()
            task.set_editor_property("filename", full)
            task.set_editor_property("destination_path", dest_path)
            task.set_editor_property("destination_name", stem)
            task.set_editor_property("automated", True)
            task.set_editor_property("replace_existing", True)
            task.set_editor_property("save", True)

            tools.import_asset_tasks([task])

            imported_paths = list(task.get_editor_property("imported_object_paths") or [])
            if not imported_paths:
                REPORT["failed"].append({"file": full, "error": "no imported_object_paths"})
                continue

            actual = imported_paths[0]
            if actual != dest_object_path:
                # Not fatal, but the runtime path lookup depends on the name, so
                # record a rename if the importer chose a different one.
                if unreal.EditorAssetLibrary.does_asset_exist(actual):
                    unreal.EditorAssetLibrary.rename_asset(actual, dest_object_path)

            REPORT["imported"].append({"file": full, "asset": dest_object_path})
        except Exception as exc:  # noqa: BLE001 - report, do not abort the batch
            REPORT["failed"].append({"file": full, "error": "%s: %s" % (type(exc).__name__, exc)})
            continue

        # Configure and verify.
        try:
            asset = unreal.EditorAssetLibrary.load_asset(dest_object_path)
            if asset is None:
                REPORT["verify_failed"].append({"asset": dest_object_path, "error": "load returned None"})
                continue

            if key in LOOPING:
                problem = set_looping(asset, key)
                if problem is None:
                    REPORT["looping_set"].append(dest_object_path)
                    unreal.EditorAssetLibrary.save_loaded_asset(asset)
                else:
                    REPORT["looping_warning"].append({"asset": dest_object_path, "error": problem})

            duration = None
            try:
                duration = float(asset.get_editor_property("duration"))
            except Exception:  # noqa: BLE001
                REPORT["notes"].append("duration not readable for %s" % dest_object_path)

            if duration is not None and duration <= 0.0:
                REPORT["verify_failed"].append(
                    {"asset": dest_object_path, "error": "duration reported %r" % duration}
                )
                continue

            REPORT["verified"].append({"asset": dest_object_path, "duration_s": duration})
        except Exception as exc:  # noqa: BLE001
            REPORT["verify_failed"].append(
                {"asset": dest_object_path, "error": "%s: %s" % (type(exc).__name__, exc)}
            )

    write_report()


def write_report():
    REPORT["summary"] = {
        "found": REPORT["found"],
        "imported": len(REPORT["imported"]),
        "failed": len(REPORT["failed"]),
        "verified": len(REPORT["verified"]),
        "verify_failed": len(REPORT["verify_failed"]),
        "looping_set": len(REPORT["looping_set"]),
        "looping_warning": len(REPORT["looping_warning"]),
    }
    REPORT["pass"] = (
        REPORT["summary"]["failed"] == 0
        and REPORT["summary"]["verify_failed"] == 0
        and REPORT["summary"]["verified"] == REPORT["summary"]["found"]
        and REPORT["summary"]["found"] > 0
    )

    os.makedirs(os.path.dirname(REPORT_PATH), exist_ok=True)
    with open(REPORT_PATH, "w", encoding="utf-8") as handle:
        json.dump(REPORT, handle, indent=2, sort_keys=True)

    unreal.log("RKC AUDIO IMPORT report: %s" % REPORT_PATH)
    unreal.log("RKC AUDIO IMPORT summary: %s" % json.dumps(REPORT["summary"], sort_keys=True))
    unreal.log("RKC AUDIO IMPORT %s" % ("PASS" if REPORT["pass"] else "FAIL"))


if __name__ == "__main__":
    try:
        main()
    except Exception:  # noqa: BLE001
        REPORT["fatal"] = traceback.format_exc()
        try:
            write_report()
        except Exception:  # noqa: BLE001
            pass
        unreal.log_error("RKC AUDIO IMPORT fatal: %s" % REPORT["fatal"])
