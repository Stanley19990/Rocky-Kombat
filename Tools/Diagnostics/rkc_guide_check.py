#!/usr/bin/env python3
"""rkc_guide_check.py - check the player-facing first-round guide against the build.

The guide makes three kinds of claim, and each of them can silently rot as the
game changes: the pictures it shows (they might not be in the pack, or might be
blank), the menu wording it quotes (the front end could be reworded), and the
keys it hands the player (a binding could move). A guide that is confidently
wrong is worse than no guide, so this checks all three against the pack and
against the code, and it is run as part of pack verification.

What it checks
  1. the guide is in the pack and is not empty
  2. every Screens/ reference in the guide resolves to a real file in the pack
  3. every file in the pack's Screens folder is used by the guide (no leftovers)
  4. every picture decodes as 1280x720 with real colour variety and enough light
     pixels to have drawn something (the bar a black, stale or flat frame fails)
  5. the guide and every picture it uses are covered by SHA256SUMS.txt
  6. the touch-pad picture really shows the pad (it differs from the match frame)
  7. every menu label the guide quotes exists in the front end's source
  8. every keyboard binding the engine promises is named in the guide
  9. the two plain-text notes the guide points at are in the pack

Reads the project source for checks 7 and 8, so run it from the project.

Usage:
    python Tools/Diagnostics/rkc_guide_check.py [pack-root] [--json out]
Default pack root: C:\\RockyKombat\\Dist\\RockyKombat-Win64-1.0.0
"""

import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from collections import Counter  # noqa: E402

from rkc_pad_presence import compare as compare_pad  # noqa: E402
from verify_stage_shots import decode_png  # noqa: E402

PROJECT = Path(r"C:\RockyKombat")
DEFAULT_PACK = PROJECT / "Dist" / "RockyKombat-Win64-1.0.0"
FRONT_END = PROJECT / "Source" / "RockyKombat" / "UI" / "RKCFrontEnd.cpp"
CONTROLLER = PROJECT / "Source" / "RockyKombat" / "Match" / "RKCPlayerController.cpp"
PAD_LABELS_SRC = PROJECT / "Source" / "RockyKombat" / "UI" / "RKCTouchControls.cpp"
MIN_GUIDE_BYTES = 1000

# The bar a picture in the guide has to clear. It is deliberately about the
# failure that would make the pictures worthless - a black frame, a stale buffer,
# a flat fill - rather than about the exact brightness of the HUD text, which
# varies with whatever fade of a front-end card the capture landed in.
#
# Measured on the six front-end and match pictures in the pack (2026-10-19):
#   distinct colour buckets   67 (01_title, dimmest) .. 1463 (05_match)
#   pixels >= 160 in all channels  373 (02_loading, dimmest) .. 6104 (05_match)
#   near-black fraction        0.0% on the five UI cards, 7.1% on the match
# The thresholds below sit under those minima and far above a blank frame, which
# measures 1 colour bucket and 0 light pixels.
EXPECTED_SIZE = (1280, 720)
MIN_DISTINCT_COLOURS = 8
MAX_NEAR_BLACK_FRACTION = 0.85
MIN_LIGHT_INK_PIXELS = 300


def picture_stats(path):
    """Decode once and report everything the guide-picture bar needs."""
    width, height, channels, rows = decode_png(path)
    total = 0
    near_black = 0
    light_ink = 0
    colours = Counter()
    luma_sum = 0
    for row in rows:
        for i in range(0, len(row), channels):
            r, g, b = row[i], row[i + 1], row[i + 2]
            total += 1
            if r <= 8 and g <= 8 and b <= 8:
                near_black += 1
            if min(r, g, b) >= 160:
                light_ink += 1
            luma_sum += (r * 299 + g * 587 + b * 114) // 1000
            colours[(r >> 4, g >> 4, b >> 4)] += 1
    return {
        "width": width, "height": height, "pixels": total,
        "distinct_colours": len(colours),
        "near_black_fraction": round(near_black / total, 4) if total else 1.0,
        "mean_luminance": round(luma_sum / total, 2) if total else 0.0,
        "light_ink_pixels": light_ink,
    }


def picture_failures(name, stats):
    bad = []
    if (stats["width"], stats["height"]) != EXPECTED_SIZE:
        bad.append(f"is {stats['width']}x{stats['height']}, not "
                   f"{EXPECTED_SIZE[0]}x{EXPECTED_SIZE[1]}")
    if stats["distinct_colours"] < MIN_DISTINCT_COLOURS:
        bad.append(f"only {stats['distinct_colours']} distinct colours (a flat frame)")
    if stats["near_black_fraction"] > MAX_NEAR_BLACK_FRACTION:
        bad.append(f"{stats['near_black_fraction']:.0%} of the frame is near black")
    if stats["light_ink_pixels"] < MIN_LIGHT_INK_PIXELS:
        bad.append(f"only {stats['light_ink_pixels']} light pixels (nothing legible drawn)")
    return [f"Screens/{name}: {b}" for b in bad]

# The wording the guide quotes. Every one of these is what a player will actually
# read on screen, so the front end must still contain it.
GUIDE_LABELS = [
    "PLAY", "TRAINING", "CONTROLS / HOW TO PLAY", "SETTINGS", "EXIT GAME",
    "ROCKY BOXING ARENA", "STREET FIGHTS", "ROCKY UNIVERSE",
    "PLAY AGAINST COMPUTER", "PLAY WITH ANOTHER PLAYER",
    "ROOKIE", "CONTENDER", "CHAMPION", "FIGHT",
    "REMATCH", "CHANGE FIGHTERS", "MAIN MENU", "TOUCH CONTROLS",
    "ROCKYCAT ECOSYSTEM", "presents",
    "3 rounds - 30 s - 3-knockdown rule",
]
PAD_LABELS = ["JAB", "HEAVY", "BLOCK", "DODGE"]

# How the engine names a key, and how the guide has to say it. Any engine key not
# in here (and not a pad button) is a gap: the guide would be naming a binding
# that the reader cannot find.
KEY_TO_GUIDE_TEXT = {
    "A": "A", "D": "D", "W": "W", "S": "S", "Z": "Z", "X": "X", "C": "C",
    "R": "R", "P": "P", "F1": "F1", "F10": "F10",
    "Enter": "Enter", "SpaceBar": "Space", "Escape": "Escape",
    "BackSpace": "Backspace",
    "Up": "arrow keys", "Down": "Down", "Left": "Left", "Right": "Right",
    "NumPadOne": "Numpad 1", "NumPadTwo": "Numpad 2", "NumPadThree": "Numpad 3",
    "Comma": ",", "Period": ".", "Slash": "/",
}

BINDING_RE = re.compile(r'\{TEXT\("([^"]+)"\),\s*TEXT\("([^"]+)"\)')


class Checks:
    def __init__(self):
        self.results = []

    def add(self, name, failures, detail=""):
        self.results.append({"check": name, "pass": not failures,
                             "failures": list(failures), "detail": detail})

    def failed(self):
        return [r for r in self.results if not r["pass"]]


def read(path):
    return path.read_text(encoding="utf-8", errors="replace")


def main(argv):
    pack = Path(argv[1]) if len(argv) > 1 and not argv[1].startswith("--") else DEFAULT_PACK
    json_out = Path(argv[argv.index("--json") + 1]) if "--json" in argv else None
    checks = Checks()

    guide_path = pack / "FIRST_ROUND_GUIDE.md"
    if not guide_path.is_file():
        print(f"MISSING_GUIDE: {guide_path}")
        return 2
    guide = read(guide_path)
    checks.add("the guide is in the pack and is not empty",
               [] if len(guide) >= MIN_GUIDE_BYTES else
               [f"only {len(guide)} bytes"], f"{len(guide)} bytes")

    # 2 + 3: references and orphans.
    refs = sorted(set(re.findall(r"\(Screens/([A-Za-z0-9_.\-]+)\)", guide)) |
                  set(re.findall(r"`(Screens/[A-Za-z0-9_.\-]+)`", guide)))
    refs = sorted({r.split("/")[-1] for r in refs})
    missing = [r for r in refs if not (pack / "Screens" / r).is_file()]
    checks.add("every picture the guide points at is in the pack",
               [f"missing Screens/{m}" for m in missing], f"{len(refs)} reference(s)")

    screens_dir = pack / "Screens"
    present = sorted(p.name for p in screens_dir.iterdir() if p.is_file()) \
        if screens_dir.is_dir() else []
    orphans = [p for p in present if p not in refs]
    checks.add("no picture is left in the pack unused by the guide",
               [f"unused Screens/{o}" for o in orphans], f"{len(present)} file(s)")

    # 4: the pictures are real pictures.
    pictures = {}
    failures = []
    for name in refs:
        if not name.lower().endswith(".png"):
            continue
        path = screens_dir / name
        if not path.is_file():
            continue
        try:
            stats = picture_stats(path)
        except Exception as exc:  # noqa: BLE001 - reported, never swallowed
            failures.append(f"Screens/{name}: cannot be read as a PNG ({exc})")
            continue
        pictures[name] = stats
        failures += picture_failures(name, stats)
    checks.add("every picture is a 1280x720 frame with real content, not a blank",
               failures, f"{len(pictures)} decoded")
    for name, stats in sorted(pictures.items()):
        print(f"      {name:<18} {stats['width']}x{stats['height']}  colours "
              f"{stats['distinct_colours']:>5}  near-black {stats['near_black_fraction']:>6.1%}  "
              f"luma {stats['mean_luminance']:>6.2f}  light px {stats['light_ink_pixels']:>7}")

    # 5: the hash manifest covers them.
    sums_path = pack / "SHA256SUMS.txt"
    failures = []
    if not sums_path.is_file():
        failures.append("SHA256SUMS.txt is not in the pack")
        sums = ""
    else:
        sums = read(sums_path)
        for rel in ["FIRST_ROUND_GUIDE.md"] + [f"Screens/{r}" for r in refs]:
            if rel not in sums:
                failures.append(f"{rel} is not covered by SHA256SUMS.txt")
    checks.add("the guide and its pictures are covered by SHA256SUMS.txt",
               failures, f"{len(sums.splitlines()) if sums else 0} hash row(s)")

    # 6: the pad picture shows the pad.
    pad_name, match_name = "07_touchpad.png", "05_match.png"
    failures = []
    detail = ""
    if (screens_dir / pad_name).is_file() and (screens_dir / match_name).is_file():
        try:
            pad = compare_pad(screens_dir / match_name, screens_dir / pad_name)
            detail = (f"{pad['diff_pixels']} px differ "
                      f"({pad['diff_fraction']:.2%}), edge share {pad['edge_share']:.0%}")
            failures = pad["failures"]
        except Exception as exc:  # noqa: BLE001
            failures = [f"{pad_name} could not be compared: {exc}"]
    else:
        failures = [f"needs both {match_name} and {pad_name} in the pack"]
    checks.add("the touch-pad picture really has the pad drawn on it", failures, detail)

    # 7: the quoted menu wording exists in the front end.
    failures = []
    if not FRONT_END.is_file():
        failures.append(f"cannot read {FRONT_END}")
    else:
        source = read(FRONT_END)
        for label in GUIDE_LABELS:
            if f'TEXT("{label}")' not in source:
                failures.append(f'the guide quotes "{label}" but the front end does not contain it')
        pad_src = read(PAD_LABELS_SRC) if PAD_LABELS_SRC.is_file() else ""
        for label in PAD_LABELS:
            if f'TEXT("{label}")' not in pad_src:
                failures.append(f'the guide names a "{label}" pad button the pad does not draw')
    checks.add("every label the guide quotes still exists in the game's source",
               failures, f"{len(GUIDE_LABELS) + len(PAD_LABELS)} label(s)")

    # 8: every promised keyboard binding is named in the guide.
    failures = []
    unmapped = set()
    action_count = 0
    if not CONTROLLER.is_file():
        failures.append(f"cannot read {CONTROLLER}")
    else:
        # Only the promised-binding table, not the key display-name map further
        # down the same file.
        source = read(CONTROLLER)
        start = source.find("RKCBindingTable()")
        end = source.find("return Table;", start)
        region = source[start:end] if start >= 0 and end > start else ""
        table = {}
        for action, key in BINDING_RE.findall(region):
            table.setdefault(action, set()).add(key)
        action_count = len(table)
        for action, keys in sorted(table.items()):
            for key in sorted(keys):
                if key.startswith("Gamepad_"):
                    continue  # the guide states the pad limitation instead
                guide_text = KEY_TO_GUIDE_TEXT.get(key)
                if guide_text is None:
                    unmapped.add(key)
                    continue
                # The guide presents every key as a key (**Z**, **Numpad 1**), so
                # that is what it has to do for each binding the engine promises.
                # Matching bare words instead would let "hold it down" stand in
                # for the Down key.
                if f"**{guide_text}**".lower() not in guide.lower():
                    failures.append(f'{action} is bound to "{key}" but the guide never '
                                    f'presents "{guide_text}" as a key')
    for key in sorted(unmapped):
        failures.append(f'no guide wording is defined for the engine key "{key}"')
    checks.add("every keyboard binding the engine promises is named in the guide",
               failures, f"{action_count} action(s) in the binding table")

    # 9: the notes the guide sends the reader to.
    failures = [f"{n} is missing from the pack" for n in ("START_HERE.txt", "KNOWN_ISSUES.txt")
                if not (pack / n).is_file()]
    checks.add("the notes the guide points at are in the pack", failures)

    print(f"pack: {pack}")
    for result in checks.results:
        mark = "PASS" if result["pass"] else "FAIL"
        suffix = f"  ({result['detail']})" if result["detail"] else ""
        print(f"{mark}  {result['check']}{suffix}")
        for failure in result["failures"]:
            print(f"        - {failure}")

    failed = checks.failed()
    print(f"\n{len(checks.results) - len(failed)} of {len(checks.results)} check(s) passed; "
          f"overall {'PASS' if not failed else 'FAIL'}")

    if json_out is not None:
        payload = {"pack": str(pack), "results": checks.results,
                   "pictures": pictures,
                   "overall": "PASS" if not failed else "FAIL"}
        json_out.write_text(json.dumps(payload, indent=2), encoding="utf-8")
        print(f"report written: {json_out}")

    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
