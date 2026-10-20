# rkc_slice_freesound.py
#
# Cuts the project owner's supplied Freesound recordings into individual
# performance assets and assigns a DISTINCT performance to each fighter.
#
# WHAT IS AND IS NOT CLAIMED HERE
# ------------------------------
# The only recorded human content supplied was one "male fight grunts" take
# (Freesound 511023) plus one punch impact (Freesound 104183). The grunts take is
# a real human voice and is the only source used for the recorded voice layer.
# No synthetic asset is described as a real voice anywhere in this pipeline, and
# nothing is ripped from Street Fighter, King of Fighters or any other game.
#
# The punch take is NOT shipped: its Freesound page offers CC0, but a public
# comment on that page states the upload is derived from a CC BY-NC 4.0
# recording. That conflict is recorded, and the slice is written to a hold area
# outside the content root so it cannot be cooked into any build.
#
# ASSIGNMENT RULE (measured, not arbitrary)
# -----------------------------------------
# Rocky is specified as "warm, rounded, mid-low"; Shadow as "sharper, brighter,
# tighter". So the performances are sorted by measured spectral centroid and the
# darker half is given to Rocky while the brighter half is given to Shadow. A
# performance is used by exactly one fighter, so the two never sound alike.
# Within a fighter, the durations are ranked and split into a light exertion, a
# reaction and a heavy exertion band, so a repeated action does not play the
# same take twice and a light and a heavy cue cannot be confused.
#
# Deterministic: same input, same output, no RNG.

import json
import os
import wave

import numpy as np

PROJECT = r"C:\RockyKombat"
SRC = {
    "grunts": r"C:\Users\DELL\Downloads\511023__ale-batec__male-fight-grunts.wav",
    "punch": r"C:\Users\DELL\Downloads\104183__ekokubza123__punch.wav",
}
DEST_RECORDED = os.path.join(PROJECT, "ArtSource", "Audio", "Recorded")
DEST_HOLD = os.path.join(PROJECT, "ArtSource", "AudioHolds", "ThirdParty")
LAYOUT = os.path.join(DEST_RECORDED, "recorded_layout.json")

TARGET_RATE = 44100
# Every existing voice asset in this project is peak-normalised to -3.0 dBFS
# (see ArtSource/Audio/provenance.json, category 'voice'). The recorded layer has
# to sit at the same level or the real grunts would be drowned out by the
# synthetic impacts they are mixed with, so the same target is used here.
VOICE_PEAK_TARGET_DBFS = -3.0
FADE_SECONDS = 0.005
# Takes whose crest factor is so different from the rest of the band that they
# cannot be level-matched without sounding wrong are reported, not silently kept.
MAX_BAND_RMS_SPREAD_DB = 6.0
MIN_SEG_SECONDS = 0.080
MIN_SEG_PEAK_DBFS = -28.0


# --------------------------------------------------------------------- io
def read_wav(path):
    with wave.open(path, "rb") as w:
        n_ch = w.getnchannels()
        width = w.getsampwidth()
        rate = w.getframerate()
        raw = w.readframes(w.getnframes())
    if width == 2:
        x = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    elif width == 1:
        x = (np.frombuffer(raw, dtype=np.uint8).astype(np.float64) - 128.0) / 128.0
    elif width == 4:
        x = np.frombuffer(raw, dtype="<i4").astype(np.float64) / 2147483648.0
    else:
        raise ValueError("unsupported sample width: %d" % width)
    if n_ch > 1:
        x = x.reshape(-1, n_ch).mean(axis=1)
    return rate, x


def write_wav(path, x, rate=TARGET_RATE):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    clipped = np.clip(x, -1.0, 1.0)
    ints = np.round(clipped * 32767.0).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(ints.tobytes())
    return os.path.getsize(path)


def resample_linear(x, src_rate, dst_rate):
    if src_rate == dst_rate or len(x) == 0:
        return x
    n_out = int(round(len(x) * float(dst_rate) / float(src_rate)))
    if n_out <= 1:
        return x[:1]
    src_idx = np.linspace(0.0, len(x) - 1.0, n_out)
    return np.interp(src_idx, np.arange(len(x), dtype=np.float64), x)


# --------------------------------------------------------------- measurement
def peak_dbfs(x):
    return 20.0 * np.log10(float(np.max(np.abs(x))) + 1e-12) if len(x) else -120.0


def rms_dbfs(x):
    return 20.0 * np.log10(float(np.sqrt(np.mean(x * x))) + 1e-12) if len(x) else -120.0


def centroid_hz(x, rate):
    if len(x) < 8:
        return 0.0
    win = x * np.hanning(len(x))
    spec = np.abs(np.fft.rfft(win))
    total = float(np.sum(spec))
    if total <= 0.0:
        return 0.0
    freqs = np.fft.rfftfreq(len(x), 1.0 / float(rate))
    return float(np.sum(spec * freqs) / total)


# ------------------------------------------------------------- segmentation
def segments(x, rate, thresh_db=-42.0, min_gap_s=0.06, min_len_s=0.03):
    """Energy-gated segmentation with sample-accurate boundaries.

    Uses a moving-average envelope computed with a cumulative sum, so the
    decision is made per sample rather than per 5 ms block. Block resolution was
    the first version's flaw: it quantised every slice length to a multiple of
    5 ms, which merged distinct takes into identical durations and cut onsets up
    to 5 ms late or early.
    """
    n = len(x)
    win = max(1, int(rate * 0.005))
    if n < win * 2:
        return []

    absx = np.abs(x)
    csum = np.concatenate(([0.0], np.cumsum(absx)))
    env = (csum[win:] - csum[:-win]) / float(win)   # env[i] covers [i, i+win)
    db = 20.0 * np.log10(env + 1e-12)

    loud = np.percentile(db, 95.0)
    thr = max(thresh_db, loud - 30.0)
    active = db > thr

    min_gap = max(1, int(min_gap_s * rate))
    min_len = max(1, int(min_len_s * rate))

    raw = []
    i = 0
    n_env = len(active)
    while i < n_env:
        if not active[i]:
            i += 1
            continue
        j = i
        gap = 0
        while j < n_env:
            if active[j]:
                gap = 0
            else:
                gap += 1
                if gap >= min_gap:
                    break
            j += 1
        end = j - gap + 1
        # env[i] covers samples [i, i+win), so a run ends at end-1+win.
        s_sample = i
        e_sample = min(n, end - 1 + win)
        if e_sample - s_sample >= min_len:
            raw.append([s_sample, e_sample])
        i = j

    merged = []
    for s in raw:
        if merged and s[0] - merged[-1][1] < min_gap:
            merged[-1][1] = s[1]
        else:
            merged.append(s)

    # A small pad so the onset and the decay are not clipped off.
    pad = int(rate * FADE_SECONDS)
    out = []
    for s, e in merged:
        out.append((max(0, s - pad), min(n, e + pad)))
    return out


# ------------------------------------------------------------- shaping
def normalise_to_peak(x, target_peak_dbfs):
    """Peak-normalise one take to the project's voice convention.

    Peak, not RMS: the rest of the audio set is built that way, short vocal
    transients read loudness by their peak, and matching the existing numbers is
    what keeps a real grunt and a synthetic impact in the same mix.
    """
    p = peak_dbfs(x)
    if p < -100.0:
        return x
    return x * (10.0 ** ((target_peak_dbfs - p) / 20.0))


def apply_fades(x, rate):
    n = int(rate * FADE_SECONDS)
    if len(x) > 2 * n and n > 1:
        ramp = np.linspace(0.0, 1.0, n)
        x = x.copy()
        x[:n] *= ramp
        x[-n:] *= ramp[::-1]
    return x


# ------------------------------------------------------------- main
def thirds(n):
    """Rank ranges for (light, react, heavy) so every take lands in one band."""
    if n <= 0:
        return (0, 0), (0, 0), (0, 0)
    a = n // 3
    b = (n - a) // 2
    return (0, a), (a, a + b), (a + b, n)


def main():
    rate, sig = read_wav(SRC["grunts"])
    sig = resample_linear(sig, rate, TARGET_RATE)
    rate = TARGET_RATE

    spans = segments(sig, rate)
    measured = []
    for idx, (s, e) in enumerate(spans):
        seg = sig[s:e]
        measured.append({
            "index": idx,
            "start": s,
            "end": e,
            "dur": (e - s) / float(rate),
            "peak": peak_dbfs(seg),
            "rms": rms_dbfs(seg),
            "centroid": centroid_hz(seg, rate),
        })

    kept, rejected = [], []
    for m in measured:
        if m["dur"] < MIN_SEG_SECONDS or m["peak"] < MIN_SEG_PEAK_DBFS:
            rejected.append(dict(m, why="too short or too quiet to be a performance"))
        else:
            kept.append(m)

    # Sort by brightness: darker half to Rocky, brighter half to Shadow.
    kept.sort(key=lambda m: m["centroid"])
    half = len(kept) // 2
    rocky = kept[:half]
    shadow = kept[half:]

    out_files = []
    summary = {}

    for fighter, takes in (("Rocky", rocky), ("Shadow", shadow)):
        takes = sorted(takes, key=lambda m: m["dur"])
        (l0, l1), (r0, r1), (h0, h1) = thirds(len(takes))
        bands = {
            "ExertLight": takes[l0:l1],
            "React": takes[r0:r1],
            "ExertHeavy": takes[h0:h1],
        }
        role_dir = os.path.join(DEST_RECORDED, fighter)

        for role, group in bands.items():
            if not group:
                continue
            for n, g in enumerate(group, start=1):
                seg = sig[g["start"]:g["end"]]
                seg = normalise_to_peak(seg, VOICE_PEAK_TARGET_DBFS)
                seg = apply_fades(seg, rate)
                stem = "VO_Rec_%s_%s_%02d" % (fighter, role, n)
                path = os.path.join(role_dir, stem + ".wav")
                size = write_wav(path, seg, rate)
                out_files.append({
                    "fighter": fighter,
                    "role": role,
                    "variant": n,
                    "stem": stem,
                    "source_index": g["index"],
                    "source_start_s": round(g["start"] / float(rate), 4),
                    "source_end_s": round(g["end"] / float(rate), 4),
                    "duration_seconds": round(len(seg) / float(rate), 4),
                    "peak_dbfs": round(peak_dbfs(seg), 2),
                    "rms_dbfs": round(rms_dbfs(seg), 2),
                    "spectral_centroid_hz": round(centroid_hz(seg, rate), 1),
                    "size_bytes": size,
                    "peak_target_dbfs": VOICE_PEAK_TARGET_DBFS,
                })

        summary[fighter] = {r: len(g) for r, g in bands.items()}
        summary[fighter]["total"] = sum(len(g) for g in bands.values())

    # ------------------------------------------------ held-back punch impact
    hold_note = None
    if os.path.isfile(SRC["punch"]):
        prate, psig = read_wav(SRC["punch"])
        prate, psig = TARGET_RATE, resample_linear(psig, prate, TARGET_RATE)
        pspans = segments(psig, prate)
        if pspans:
            s, e = max(pspans, key=lambda t: t[1] - t[0])
            seg = psig[s:e]
            seg = normalise_to_peak(seg, -18.0)
            seg = apply_fades(seg, prate)
            path = os.path.join(DEST_HOLD, "Freesound_104183_Punch_HELD.wav")
            write_wav(path, seg, prate)  # held-back; -18 dBFS on purpose so it cannot
                                         # be mistaken for a production asset
            hold_note = {
                "file": path.replace("/", "\\"),
                "source": "Freesound 104183 'punch.wav' by Ekokubza123",
                "held_back_reason":
                    "page licence reads CC0, but a public comment on that page states the "
                    "upload derives from Freesound 43586 (Syna-Max, CC BY-NC 4.0, "
                    "NonCommercial). Not connected to any cue and written outside "
                    "ArtSource/Audio so it cannot be imported or cooked until the "
                    "provenance is resolved.",
                "duration_seconds": round(len(seg) / float(prate), 4),
            }

    band_spread = {}
    for fighter in ("Rocky", "Shadow"):
        for role in ("ExertLight", "React", "ExertHeavy"):
            band = [f for f in out_files if f["fighter"] == fighter and f["role"] == role]
            if len(band) >= 2:
                spread = max(b["rms_dbfs"] for b in band) - min(b["rms_dbfs"] for b in band)
                band_spread["%s/%s" % (fighter, role)] = round(spread, 2)
                if spread > MAX_BAND_RMS_SPREAD_DB:
                    print("WARNING: %s/%s rms spread %.2f dB exceeds %.1f dB" % (
                        fighter, role, spread, MAX_BAND_RMS_SPREAD_DB))

    layout = {
        "generator": "Tools/Audio/rkc_slice_freesound.py",
        "band_rms_spread_db": band_spread,
        "max_band_rms_spread_db": MAX_BAND_RMS_SPREAD_DB,
        "source_recording": {
            "freesound_id": 511023,
            "title": "Male Fight Grunts",
            "uploader": "ale-batec",
            "url": "https://freesound.org/s/511023/",
            "licence": "Creative Commons 0 (CC0 1.0 Public Domain Dedication)",
            "licence_url": "https://creativecommons.org/publicdomain/zero/1.0/",
            "retrieved": "2026-10-19",
            "original_sample_rate": 44100,
            "original_channels": 1,
            "original_duration_seconds": 34.069,
        },
        "output_format": {
            "sample_rate": TARGET_RATE,
            "channels": 1,
            "bit_depth": 16,
            "peak_target_dbfs": VOICE_PEAK_TARGET_DBFS,
            "peak_target_matches_existing_voice_assets": True,
            "fade_seconds": FADE_SECONDS,
        },
        "assignment_rule":
            "Takes ranked by measured spectral centroid; the darker half is assigned to "
            "Rocky and the brighter half to Shadow. A take is used by exactly one fighter. "
            "Within a fighter, takes are ranked by duration into ExertLight, React and "
            "ExertHeavy bands.",
        "rejected_segments": rejected,
        "per_fighter_counts": summary,
        "files": out_files,
        "held_back": hold_note,
    }

    os.makedirs(DEST_RECORDED, exist_ok=True)
    with open(LAYOUT, "w", encoding="utf-8") as f:
        json.dump(layout, f, indent=2)

    print("source segments: %d   kept: %d   rejected: %d" % (
        len(measured), len(kept), len(rejected)))
    for fighter in ("Rocky", "Shadow"):
        print("  %-7s %s" % (fighter, summary[fighter]))
    print("wrote %d performance files under %s" % (len(out_files), DEST_RECORDED))
    if hold_note:
        print("held back: %s" % hold_note["file"])
    print("layout: %s" % LAYOUT)


if __name__ == "__main__":
    main()
