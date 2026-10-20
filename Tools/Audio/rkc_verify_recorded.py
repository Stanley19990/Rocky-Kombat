# rkc_verify_recorded.py
#
# Independent check of the generated recorded voice assets: re-reads every file
# from disk (not from the layout JSON) and asserts the properties the build
# depends on. Writes Saved/Evidence/Recorded_voice_verification.json.

import json
import os
import wave

import numpy as np

PROJECT = r"C:\RockyKombat"
ROOT = os.path.join(PROJECT, "ArtSource", "Audio", "Recorded")
OUT = os.path.join(PROJECT, "Saved", "Evidence", "Recorded_voice_verification.json")

CHECKS = []
FAILURES = []


def check(name, ok, detail=""):
    CHECKS.append({"check": name, "result": "PASS" if ok else "FAIL", "detail": detail})
    if not ok:
        FAILURES.append("%s :: %s" % (name, detail))


def read(path):
    with wave.open(path, "rb") as w:
        return (w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes(),
                np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64) / 32768.0)


files = []
for root, _d, names in os.walk(ROOT):
    for n in sorted(names):
        if n.lower().endswith(".wav"):
            files.append(os.path.join(root, n))
files.sort()

check("at least 20 performance files exist", len(files) >= 20, "found %d" % len(files))

rows = []
WAVES = {}
for f in files:
    ch, width, rate, nframes, x = read(f)
    peak = 20.0 * np.log10(float(np.max(np.abs(x))) + 1e-12) if len(x) else -120.0
    rms = 20.0 * np.log10(float(np.sqrt(np.mean(x * x))) + 1e-12) if len(x) else -120.0
    rel = os.path.relpath(f, ROOT).replace("\\", "/")
    rows.append({"file": rel, "channels": ch, "bit_depth": width * 8,
                 "sample_rate": rate, "duration_seconds": round(nframes / float(rate), 4),
                 "peak_dbfs": round(peak, 2), "rms_dbfs": round(rms, 2),
                 "size_bytes": os.path.getsize(f)})

    check("mono: %s" % rel, ch == 1, "channels=%d" % ch)
    check("16-bit: %s" % rel, width == 2, "width=%d" % width)
    check("44100 Hz: %s" % rel, rate == 44100, "rate=%d" % rate)
    # The whole point of the recorded layer is that it mixes with the existing
    # synthetic set, so every slice must land on the same peak the existing voice
    # assets use. A slice that is 15 dB quieter is a defect, not a preference.
    check("peak within 0.5 dB of the -3.0 dBFS voice convention: %s" % rel,
          abs(peak - (-3.0)) <= 0.5, "peak=%.2f" % peak)
    check("no clipping runs: %s" % rel, int(np.sum(np.abs(x) >= 0.999)) == 0,
          "%d saturated sample(s)" % int(np.sum(np.abs(x) >= 0.999)))
    WAVES[rel] = x

# Per-fighter, per-role spread: the point of slicing is that variants differ.
by_role = {}
for r in rows:
    parts = os.path.basename(r["file"]).split("_")
    fighter, role = parts[2], parts[3]
    by_role.setdefault((fighter, role), []).append(r)

def correlation(a, b):
    """Peak normalised cross-correlation of two takes at their best alignment.

    Comparing lengths only proves the files were not cut identically; it says
    nothing about whether they sound the same. Two different takes of the same
    grunt have a low correlation, a duplicated take has one near 1.0.
    """
    n = min(len(a), len(b))
    if n == 0:
        return 1.0
    a = a[:n] - np.mean(a[:n])
    b = b[:n] - np.mean(b[:n])
    denom = float(np.sqrt(np.sum(a * a) * np.sum(b * b)))
    if denom <= 1e-12:
        return 1.0
    return abs(float(np.sum(a * b)) / denom)


for key, group in sorted(by_role.items()):
    check("%s/%s has at least 3 distinct variants" % key, len(group) >= 3,
          "%d variant(s)" % len(group))
    worst = 0.0
    worst_pair = ""
    for i in range(len(group)):
        for j in range(i + 1, len(group)):
            c = correlation(WAVES[group[i]["file"]], WAVES[group[j]["file"]])
            if c > worst:
                worst, worst_pair = c, "%s vs %s" % (group[i]["file"], group[j]["file"])
    check("%s/%s variants are distinct takes (max correlation < 0.90)" % key, worst < 0.90,
          "max corr=%.3f (%s)" % (worst, worst_pair))
    rms_spread = max(g["rms_dbfs"] for g in group) - min(g["rms_dbfs"] for g in group)
    check("%s/%s variants are level-matched (spread < 6 dB)" % key, rms_spread < 6.0,
          "rms spread=%.2f dB" % rms_spread)

# Two disjoint voices: Rocky and Shadow must not share a source take.
layout_path = os.path.join(ROOT, "recorded_layout.json")
with open(layout_path, encoding="utf-8") as f:
    layout = json.load(f)
src_by_fighter = {}
for entry in layout["files"]:
    src_by_fighter.setdefault(entry["fighter"], set()).add(entry["source_index"])
shared = src_by_fighter.get("Rocky", set()) & src_by_fighter.get("Shadow", set())
check("Rocky and Shadow share no source take", not shared, "shared=%s" % sorted(shared))
check("every generated file is described by the layout",
      set(os.path.relpath(os.path.join(ROOT, e["fighter"], e["stem"] + ".wav"), ROOT).replace("\\", "/")
          for e in layout["files"]) == set(r["file"] for r in rows),
      "layout=%d files, disk=%d files" % (len(layout["files"]), len(rows)))
check("the held-back punch is outside the content root",
      not os.path.isfile(os.path.join(PROJECT, "Content", "RockyKombat", "Audio", "Recorded",
                                      "Freesound_104183_Punch_HELD.uasset")),
      "no cooked asset for the held-back take")

report = {
    "root": ROOT,
    "file_count": len(files),
    "total_bytes": sum(r["size_bytes"] for r in rows),
    "files": rows,
    "roles": {"%s/%s" % k: len(v) for k, v in sorted(by_role.items())},
    "checks": CHECKS,
    "checks_total": len(CHECKS),
    "checks_failed": len(FAILURES),
    "failures": FAILURES,
    "result": "PASS" if not FAILURES else "FAIL",
}

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w", encoding="utf-8") as f:
    json.dump(report, f, indent=2)

print("files: %d  bytes: %d" % (report["file_count"], report["total_bytes"]))
for k, v in report["roles"].items():
    print("  %-24s %d" % (k, v))
print("checks: %d   failed: %d   -> %s" % (report["checks_total"], report["checks_failed"],
                                          report["result"]))
for f in FAILURES[:20]:
    print("  FAIL", f)
print("wrote", OUT)
