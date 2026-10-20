"""Summarise Saved/Evidence/Presentation_params.json.

Prints the recorded state of textures, sprites, materials and flipbooks so the
black-fighter investigation can be argued from the assets as they exist on disk
rather than from the import script's intentions.
"""
import json

PATH = r"C:\RockyKombat\Saved\Evidence\Presentation_params.json"

with open(PATH, "r", encoding="utf-8", errors="replace") as fh:
    data = json.load(fh)

print("top-level keys:", list(data.keys()))
print()
print(json.dumps(data.get("summary", {}), indent=2, default=str)[:3000])
print()

for key in ("materials", "flipbooks", "sprites"):
    obj = data.get(key)
    print("=" * 70)
    print(key, type(obj).__name__)
    print("=" * 70)
    if isinstance(obj, dict):
        print("keys:", list(obj.keys())[:20])
    elif isinstance(obj, list):
        print("count:", len(obj))
        for item in obj[:2]:
            print(json.dumps(item, indent=2, default=str)[:2500])
    print()
