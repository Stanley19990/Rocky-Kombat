# rkc_probe_freesound.py
#
# Read-only probe of the two Freesound recordings supplied by the project owner.
# Measures format, level and silence structure so the slicer can be planned from
# measurements rather than guesses. Writes JSON next to itself; changes nothing.

import json
import os
import sys
import wave

import numpy as np

SOURCES = [
    ("104183_ekokubza123_punch",
     r"C:\Users\DELL\Downloads\104183__ekokubza123__punch.wav"),
    ("511023_ale-batec_male-fight-grunts",
     r"C:\Users\DELL\Downloads\511023__ale-batec__male-fight-grunts.wav"),
]

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "freesound_probe.json")


def load(path):
    with wave.open(path, "rb") as w:
        n_ch = w.getnchannels()
        width = w.getsampwidth()
        rate = w.getframerate()
        n = w.getnframes()
        raw = w.readframes(n)
    if width == 2:
        data = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    elif width == 1:
        data = (np.frombuffer(raw, dtype=np.uint8).astype(np.float64) - 128.0) / 128.0
    elif width == 4:
        data = np.frombuffer(raw, dtype="<i4").astype(np.float64) / 2147483648.0
    else:
        raise ValueError("unsupported sample width %d" % width)
    if n_ch > 1:
        data = data.reshape(-1, n_ch).mean(axis=1)
    return rate, n_ch, width, n, data


def segments(data, rate, thresh_db=-42.0, min_gap_s=0.06, min_len_s=0.03):
    """Energy-gated segmentation: returns [(start_sample, end_sample)]."""
    win = max(1, int(rate * 0.005))            # 5 ms analysis window
    n_win = len(data) // win
    if n_win == 0:
        return [], win
    frames = data[:n_win * win].reshape(n_win, win)
    rms = np.sqrt(np.mean(frames * frames, axis=1) + 1e-12)
    db = 20.0 * np.log10(rms + 1e-12)

    # Threshold relative to the file's own loud percentile, so a quiet upload
    # is not classified as entirely silent.
    loud = np.percentile(db, 95.0)
    thr = max(thresh_db, loud - 30.0)
    active = db > thr

    min_gap = max(1, int(min_gap_s * rate / win))
    min_len = max(1, int(min_len_s * rate / win))

    segs = []
    i = 0
    while i < n_win:
        if not active[i]:
            i += 1
            continue
        j = i
        gap = 0
        while j < n_win:
            if active[j]:
                gap = 0
            else:
                gap += 1
                if gap >= min_gap:
                    break
            j += 1
        end = j - gap + 1
        if end - i >= min_len:
            segs.append((i * win, min(len(data), end * win)))
        i = j

    # Merge any pair whose true silence gap is shorter than min_gap.
    merged = []
    for s in segs:
        if merged and s[0] - merged[-1][1] < int(min_gap_s * rate):
            merged[-1] = (merged[-1][0], s[1])
        else:
            merged.append(list(s))
    return [tuple(m) for m in merged], win


def measure(x):
    peak = float(np.max(np.abs(x))) if len(x) else 0.0
    rms = float(np.sqrt(np.mean(x * x))) if len(x) else 0.0
    peak_db = 20.0 * np.log10(peak + 1e-12)
    rms_db = 20.0 * np.log10(rms + 1e-12)
    spec = np.abs(np.fft.rfft(x * np.hanning(len(x)))) if len(x) > 8 else np.zeros(2)
    freqs = np.fft.rfftfreq(len(x), 1.0 / 44100.0) if len(x) > 8 else np.zeros(2)
    centroid = float(np.sum(spec * freqs) / max(1e-12, np.sum(spec))) if np.sum(spec) > 0 else 0.0
    return peak_db, rms_db, centroid


report = {"sources": []}
for name, path in SOURCES:
    entry = {"id": name, "path": path, "exists": os.path.isfile(path)}
    if entry["exists"]:
        rate, n_ch, width, n, data = load(path)
        entry.update({
            "sample_rate": rate,
            "channels": n_ch,
            "sample_width_bytes": width,
            "frames": n,
            "duration_seconds": round(n / float(rate), 4),
            "size_bytes": os.path.getsize(path),
        })
        segs, win = segments(data, rate)
        entry["segment_count"] = len(segs)
        seg_list = []
        for (s, e) in segs:
            seg = data[s:e]
            p, r, c = measure(seg)
            seg_list.append({
                "start_s": round(s / float(rate), 4),
                "end_s": round(e / float(rate), 4),
                "dur_s": round((e - s) / float(rate), 4),
                "peak_dbfs": round(p, 2),
                "rms_dbfs": round(r, 2),
                "centroid_hz": round(c, 1),
            })
        entry["segments"] = seg_list
        segs_sorted = sorted(seg_list, key=lambda x: x["dur_s"])
        if segs_sorted:
            ds = [x["dur_s"] for x in segs_sorted]
            entry["dur_min_s"] = ds[0]
            entry["dur_max_s"] = ds[-1]
            entry["dur_median_s"] = round(float(np.median(ds)), 4)
            entry["silence_ratio"] = round(
                1.0 - (sum(ds) / (n / float(rate))), 4)
    report["sources"].append(entry)

with open(OUT, "w", encoding="utf-8") as f:
    json.dump(report, f, indent=2)

for s in report["sources"]:
    print("=" * 70)
    print(s["id"], "exists=%s" % s["exists"])
    if not s["exists"]:
        continue
    print("  %.3f s  %d Hz  %d ch  %d bit" % (
        s["duration_seconds"], s["sample_rate"], s["channels"],
        s["sample_width_bytes"] * 8))
    print("  segments=%d  dur min/med/max = %.3f / %.3f / %.3f  silence=%.1f%%" % (
        s["segment_count"], s["dur_min_s"], s["dur_median_s"], s["dur_max_s"],
        100.0 * s["silence_ratio"]))
    for i, seg in enumerate(s["segments"]):
        print("   [%02d] %7.3f -> %7.3f  dur=%6.3f  peak=%6.1f dB  rms=%6.1f dB  centroid=%7.0f Hz"
              % (i, seg["start_s"], seg["end_s"], seg["dur_s"],
                 seg["peak_dbfs"], seg["rms_dbfs"], seg["centroid_hz"]))
print()
print("wrote", OUT)
