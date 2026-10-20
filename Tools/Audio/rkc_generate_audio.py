#!/usr/bin/env python3
"""rkc_generate_audio.py - deterministic procedural audio for Rocky Kombat.

Everything is synthesised from maths at run time: oscillators, noise, envelopes
and FFT-domain filters. No sample libraries, no downloads, no third-party
packages beyond numpy. A fixed seed means re-running reproduces byte-identical
WAV files.

Usage:
    python rkc_generate_audio.py            # assets + provenance.json + AUDIO_MANIFEST.md
    python rkc_generate_audio.py --verify   # also write verification.json

Layout written under <repo>/ArtSource/Audio, per Tools/Audio/AUDIO_SPEC.md.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
import wave
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path

import numpy as np

SR = 44100
SEED = 20261017
PEAK_DBFS = -1.0  # hard ceiling for every delivered file

REPO = Path(__file__).resolve().parents[2]
OUT_ROOT = REPO / "ArtSource" / "Audio"


# ---------------------------------------------------------------------------
# Primitive DSP (numpy only)
# ---------------------------------------------------------------------------

def n_samples(seconds: float) -> int:
    return int(round(seconds * SR))


def t_axis(n: int) -> np.ndarray:
    return np.arange(n, dtype=np.float64) / SR


def db_to_lin(db: float) -> float:
    return 10.0 ** (db / 20.0)


def lin_to_db(x: float) -> float:
    return 20.0 * math.log10(max(float(x), 1e-12))


def _freqs(n: int) -> np.ndarray:
    return np.fft.rfftfreq(n, 1.0 / SR)


def lp_shape(f: np.ndarray, fc: float, order: int = 2) -> np.ndarray:
    return 1.0 / np.sqrt(1.0 + (f / fc) ** (2 * order))


def hp_shape(f: np.ndarray, fc: float, order: int = 2) -> np.ndarray:
    return (f / fc) ** order / np.sqrt(1.0 + (f / fc) ** (2 * order))


def bp_shape(f: np.ndarray, lo: float, hi: float, order: int = 2) -> np.ndarray:
    f = np.maximum(f, 1e-6)
    return lp_shape(f, hi, order) * hp_shape(f, lo, order)


def peak_shape(f: np.ndarray, f0: float, bw_oct: float, gain: float = 1.0) -> np.ndarray:
    """Gaussian resonance in log-frequency: a formant / body resonance."""
    f = np.maximum(f, 1e-6)
    return gain * np.exp(-0.5 * (np.log2(f / f0) / bw_oct) ** 2)


def fft_filter(x: np.ndarray, shape: np.ndarray) -> np.ndarray:
    """Zero-phase FIR filtering by spectral multiplication (circular)."""
    return np.fft.irfft(np.fft.rfft(x) * shape, len(x))


def f_lowpass(x: np.ndarray, fc: float, order: int = 2) -> np.ndarray:
    return fft_filter(x, lp_shape(_freqs(len(x)), fc, order))


def f_highpass(x: np.ndarray, fc: float, order: int = 2) -> np.ndarray:
    return fft_filter(x, hp_shape(_freqs(len(x)), fc, order))


def f_bandpass(x: np.ndarray, lo: float, hi: float, order: int = 2) -> np.ndarray:
    return fft_filter(x, bp_shape(_freqs(len(x)), lo, hi, order))


def circ_convolve(x: np.ndarray, ir: np.ndarray) -> np.ndarray:
    """Circular convolution: a tail that wraps, so any loop stays seamless."""
    return np.fft.irfft(np.fft.rfft(x) * np.fft.rfft(ir), len(x))


def reverb_ir(tau: float, length: int, seed: int, damp: float = 6000.0) -> np.ndarray:
    rng = np.random.default_rng(seed)
    t = t_axis(length)
    ir = rng.standard_normal(length) * np.exp(-t / tau)
    ir = f_lowpass(ir, damp, 2)
    ir[0] += 1.0  # direct sound
    ir /= np.max(np.abs(ir)) + 1e-12
    return ir


def delay_ir(delay_s: float, taps: int, feedback: float, length: int) -> np.ndarray:
    ir = np.zeros(length)
    d = max(1, n_samples(delay_s))
    for k in range(taps):
        i = k * d
        if i >= length:
            break
        ir[i] += feedback ** k
    return ir


def env_ad(t: np.ndarray, attack: float, decay: float, curve: float = 3.0) -> np.ndarray:
    """Attack ramp then exponential decay."""
    a = np.clip(t / max(attack, 1e-6), 0.0, 1.0)
    return a * np.exp(-np.clip((t - attack) / max(decay, 1e-6), 0.0, None) * curve)


def env_ar(t: np.ndarray, attack: float, release: float, curve: float = 3.0) -> np.ndarray:
    """Attack ramp, exponential release that reaches ~0 at the end of the buffer."""
    dur = max(t[-1], 1e-6) if len(t) else 1.0
    a = np.clip(t / max(attack, 1e-6), 0.0, 1.0)
    r = np.exp(-np.clip((t - attack) / max(release, 1e-6), 0.0, None) * curve)
    end = math.exp(-max((dur - attack) / max(release, 1e-6), 0.0) * curve)
    return a * (r - end) / max(1.0 - end, 1e-6)


def smooth(x: np.ndarray, ms: float) -> np.ndarray:
    n = max(1, n_samples(ms / 1000.0))
    k = np.hanning(n)
    k /= k.sum()
    return np.convolve(x, k, mode="same")


def normalize(x: np.ndarray, peak_dbfs: float = PEAK_DBFS) -> np.ndarray:
    x = np.nan_to_num(x)
    x = x - x.mean() if x.ndim == 1 else x - x.mean(axis=0, keepdims=True)
    peak = np.max(np.abs(x)) if x.size else 0.0
    if peak <= 1e-12:
        return x
    return x * (db_to_lin(peak_dbfs) / peak)


def soft_clip(x: np.ndarray, drive: float = 1.0) -> np.ndarray:
    return np.tanh(x * drive) / math.tanh(drive)


def midi_to_hz(m: float) -> float:
    return 440.0 * (2.0 ** ((m - 69.0) / 12.0))


# ---------------------------------------------------------------------------
# Oscillators and instruments
# ---------------------------------------------------------------------------

def phase_of(freq_env: np.ndarray) -> np.ndarray:
    return np.cumsum(freq_env) / SR


def saw(phase: np.ndarray) -> np.ndarray:
    return 2.0 * (phase % 1.0) - 1.0


def band_limited_saw(phase: np.ndarray, f0: float, harmonics: int = 24) -> np.ndarray:
    nyq = SR * 0.5
    kmax = max(1, min(harmonics, int(nyq / max(f0, 1.0))))
    out = np.zeros_like(phase)
    for k in range(1, kmax + 1):
        if k * f0 >= nyq:
            break
        out += np.sin(2.0 * math.pi * k * phase) / k
    return out * (2.0 / math.pi)


def square(phase: np.ndarray, duty: float = 0.5) -> np.ndarray:
    return np.where((phase % 1.0) < duty, 1.0, -1.0)


def triangle(phase: np.ndarray) -> np.ndarray:
    p = phase % 1.0
    return 4.0 * np.abs(p - 0.5) - 1.0


def pluck(freq: float, dur: float, seed: int, bright: float = 0.5) -> np.ndarray:
    """Struck-string-ish tone: additive partials with per-partial decay."""
    n = n_samples(dur)
    t = t_axis(n)
    rng = np.random.default_rng(seed)
    out = np.zeros(n)
    for k in range(1, 13):
        f = freq * k
        if f > SR * 0.45:
            break
        amp = (k ** (-1.0 - bright)) * (1.0 + 0.15 * rng.standard_normal())
        decay = 0.9 / (1.0 + 0.7 * k)
        out += amp * np.sin(2.0 * math.pi * f * t + rng.uniform(0, 2 * math.pi)) * np.exp(
            -t / max(decay, 1e-3))
    return normalize(out, -6.0) * 0.6


def kick(dur: float = 0.34, f_start: float = 128.0, f_end: float = 44.0) -> np.ndarray:
    n = n_samples(dur)
    t = t_axis(n)
    f = f_end + (f_start - f_end) * np.exp(-t / 0.028)
    body = np.sin(2.0 * math.pi * phase_of(f)) * env_ad(t, 0.001, 0.16, 3.2)
    click = f_lowpass(np.random.default_rng(7).standard_normal(n), 4000) * env_ad(t, 0.0005, 0.006, 6.0)
    return normalize(body + 0.25 * click, -2.0)


def snare(dur: float = 0.20, seed: int = 11) -> np.ndarray:
    n = n_samples(dur)
    t = t_axis(n)
    rng = np.random.default_rng(seed)
    noise = f_bandpass(rng.standard_normal(n), 1400, 8200, 2) * env_ad(t, 0.0008, 0.055, 3.5)
    body = np.sin(2.0 * math.pi * 185.0 * t) * env_ad(t, 0.001, 0.035, 4.0) * 0.5
    return normalize(noise + body, -3.0)


def clap(dur: float = 0.24, seed: int = 13) -> np.ndarray:
    n = n_samples(dur)
    rng = np.random.default_rng(seed)
    out = np.zeros(n)
    for i, off in enumerate((0.0, 0.009, 0.019, 0.030)):
        s = n_samples(off)
        seg = n_samples(0.03)
        if s + seg > n:
            break
        burst = f_bandpass(rng.standard_normal(seg), 900, 4200, 2) * np.exp(
            -t_axis(seg) / 0.006) * (1.0 - 0.12 * i)
        out[s:s + seg] += burst
    tail = f_bandpass(rng.standard_normal(n), 900, 3800, 2) * env_ad(t_axis(n), 0.001, 0.09, 3.0) * 0.35
    return normalize(out + tail, -3.0)


def hat(dur: float = 0.06, seed: int = 17, open_hat: bool = False) -> np.ndarray:
    n = n_samples(dur)
    t = t_axis(n)
    rng = np.random.default_rng(seed)
    noise = f_highpass(rng.standard_normal(n), 6500, 2)
    return normalize(noise * env_ad(t, 0.0004, 0.05 if open_hat else 0.012, 4.0), -8.0)


def tom(freq: float, dur: float = 0.22) -> np.ndarray:
    n = n_samples(dur)
    t = t_axis(n)
    f = freq * (1.0 + 0.35 * np.exp(-t / 0.05))
    return normalize(np.sin(2.0 * math.pi * phase_of(f)) * env_ad(t, 0.001, 0.10, 3.0), -4.0)


def synth_note(freq: float, dur: float, seed: int, kind: str = "saw", detune: float = 0.006,
               cutoff: float = 2600.0, attack: float = 0.006, decay: float | None = None,
               harmonics: int = 18) -> np.ndarray:
    """One pitched synth voice: band-limited saw/square, slight detune, lowpass."""
    n = n_samples(dur)
    t = t_axis(n)
    rng = np.random.default_rng(seed)
    if kind == "saw":
        p1 = phase_of(np.full(n, freq))
        p2 = phase_of(np.full(n, freq * (1.0 + detune)))
        p3 = phase_of(np.full(n, freq * (1.0 - detune)))
        y = (band_limited_saw(p1, freq, harmonics) + 0.7 * band_limited_saw(p2, freq, harmonics)
             + 0.7 * band_limited_saw(p3, freq, harmonics)) / 2.4
    elif kind == "square":
        p1 = phase_of(np.full(n, freq))
        p2 = phase_of(np.full(n, freq * (1.0 + detune * 1.5)))
        y = (square(p1, 0.45) + 0.6 * square(p2, 0.5)) / 1.6
    elif kind == "tri":
        y = triangle(phase_of(np.full(n, freq)))
    else:
        y = np.sin(2.0 * math.pi * freq * t) + 0.3 * np.sin(2.0 * math.pi * 2 * freq * t)
    y = f_lowpass(y, cutoff, 2)
    env = env_ar(t, attack, decay) if decay else env_ad(t, attack, dur * 0.5, 2.5)
    return y * env * (1.0 + 0.01 * rng.standard_normal(n))


def pad_chord(freqs: list[float], dur: float, seed: int, cutoff: float = 1800.0) -> np.ndarray:
    out = None
    for i, f in enumerate(freqs):
        v = synth_note(f, dur, seed + i, "saw", detune=0.004, cutoff=cutoff,
                       attack=dur * 0.22, decay=None)
        # slow tremolo keeps a pad from sounding static
        t = t_axis(len(v))
        v = v * (1.0 + 0.06 * np.sin(2.0 * math.pi * 0.22 * t + i))
        out = v if out is None else out + v
    return normalize(out, -8.0) * 0.7


class Track:
    """Stereo buffer whose tail folds back to the start, so loops are seamless."""

    def __init__(self, total: int, tail: int):
        self.total = total
        self.tail = tail
        self.L = np.zeros(total + tail)
        self.R = np.zeros(total + tail)

    def add(self, sig: np.ndarray, start: int, gain: float = 1.0, pan: float = 0.0):
        pan = max(-1.0, min(1.0, pan))
        gl = gain * math.sqrt(0.5 * (1.0 - pan))
        gr = gain * math.sqrt(0.5 * (1.0 + pan))
        s = int(start)
        if s >= len(self.L):
            return
        seg = sig[: len(self.L) - s]
        self.L[s:s + len(seg)] += seg * gl
        self.R[s:s + len(seg)] += seg * gr

    def finish(self, *, mix_low: bool = False) -> np.ndarray:
        L = self.L[: self.total].copy()
        R = self.R[: self.total].copy()
        if self.tail > 0:
            L[: self.tail] += self.L[self.total:self.total + self.tail]
            R[: self.tail] += self.R[self.total:self.total + self.tail]
        out = np.stack([L, R], axis=1)
        return out


@dataclass
class SongSpec:
    name: str
    bpm: float
    bars: int
    prog: list[tuple[int, list[int]]]
    seed: int
    style: str            # "title" | "menu" | "arena"
    tail: float = 2.0
    notes: list[str] = field(default_factory=list)


def render_song(spec: SongSpec) -> np.ndarray:
    beat = 60.0 / spec.bpm
    bar = beat * 4.0
    total = n_samples(bar * spec.bars)
    tail = n_samples(spec.tail)
    tr = Track(total, tail)
    rng = np.random.default_rng(spec.seed)

    def at(bar_i: float, beat_i: float = 0.0) -> int:
        return n_samples((bar_i * bar) + (beat_i * beat))

    style = spec.style
    # --- drums -----------------------------------------------------------------
    for b in range(spec.bars):
        if style == "menu":
            kick_beats = [0, 2.5]
            clap_beats = [1, 3]
            hat_div = 2
        elif style == "title":
            kick_beats = [0, 1.5, 2, 3.5] if b % 4 in (2, 3) else [0, 2]
            clap_beats = [1, 3]
            hat_div = 2
        else:  # arena
            kick_beats = [0, 1, 2, 3] if b % 2 == 0 else [0, 1.5, 2, 3.5]
            clap_beats = [1, 3]
            hat_div = 4

        for k in kick_beats:
            tr.add(kick(), at(b, k), gain=0.95, pan=0.0)
        for c in clap_beats:
            tr.add(clap(seed=spec.seed + b * 7 + int(c)), at(b, c), gain=0.42 if style != "arena" else 0.5)
        for h in range(int(4 * hat_div)):
            pos = h / hat_div
            opn = (h % int(4 * hat_div)) == int(4 * hat_div) - 1 and b % 2 == 1
            tr.add(hat(seed=spec.seed + h, open_hat=opn), at(b, pos),
                   gain=0.20 if h % 2 else 0.13, pan=0.35 if h % 2 else -0.3)
        if style == "arena" and b % 8 == 7:
            tr.add(tom(160.0), at(b, 3.0), gain=0.5, pan=-0.3)
            tr.add(tom(120.0), at(b, 3.5), gain=0.5, pan=0.3)

    # --- bass ------------------------------------------------------------------
    for b in range(spec.bars):
        root, _ = spec.prog[b % len(spec.prog)]
        base = root - 24
        if style == "arena":
            pattern = [(0.0, 0.5), (0.75, 0.25), (1.5, 0.5), (2.0, 0.5), (2.75, 0.25), (3.5, 0.5)]
        elif style == "title":
            pattern = [(0.0, 0.9), (1.5, 0.4), (2.0, 0.9), (3.5, 0.4)]
        else:
            pattern = [(0.0, 1.4), (2.0, 1.4)]
        for beat_pos, dur_beats in pattern:
            f = midi_to_hz(base)
            note = synth_note(f, beat * dur_beats, spec.seed + 31 + int(beat_pos * 4), "saw",
                              detune=0.002, cutoff=420.0, attack=0.004, decay=beat * dur_beats * 0.6,
                              harmonics=10)
            sub = np.sin(2.0 * math.pi * f * t_axis(len(note))) * np.exp(
                -t_axis(len(note)) / (beat * dur_beats * 0.5)) * 0.5
            tr.add(note + sub, at(b, beat_pos), gain=0.9)

    # --- chords -----------------------------------------------------------------
    for b in range(spec.bars):
        root, ivs = spec.prog[b % len(spec.prog)]
        freqs = [midi_to_hz(root + i) for i in ivs]
        if style == "arena":
            for hit in (0.0, 1.5, 2.5):
                tr.add(pad_chord(freqs, beat * 0.45, spec.seed + 61 + int(hit * 10), cutoff=2400.0),
                       at(b, hit), gain=0.30, pan=-0.15)
        else:
            tr.add(pad_chord(freqs, bar * 0.95, spec.seed + 61, cutoff=1700.0), at(b, 0.0),
                   gain=0.34, pan=0.1)

    # --- lead / arp -------------------------------------------------------------
    lead_ir = delay_ir(beat * 0.75, 4, 0.42, total)
    lead = Track(total, 0)
    for b in range(spec.bars):
        root, ivs = spec.prog[b % len(spec.prog)]
        pool = [root + 12, root + ivs[1], root + 12 + ivs[1], root + 24]
        if style == "arena":
            steps = [(i * 0.5, pool[i % len(pool)]) for i in range(8)]
        elif style == "title":
            steps = [(0.0, pool[0]), (1.0, pool[1]), (1.5, pool[2]), (2.5, pool[1]), (3.0, pool[3])]
        else:
            steps = [(0.0, pool[0]), (1.5, pool[1]), (2.5, pool[3])]
        for beat_pos, m in steps:
            f = midi_to_hz(m)
            note = synth_note(f, beat * 0.5, spec.seed + 97 + int(beat_pos * 8), "square",
                              detune=0.003, cutoff=3400.0, attack=0.004, decay=beat * 0.3,
                              harmonics=12)
            lead.add(note, at(b, beat_pos), gain=0.16, pan=0.2 if beat_pos % 1 else -0.2)
    lg = lead.finish()
    lg = np.stack([circ_convolve(lg[:, 0], lead_ir), circ_convolve(lg[:, 1], lead_ir)], axis=1)
    tr.L[:total] += lg[:, 0]
    tr.R[:total] += lg[:, 1]

    dry = tr.finish()
    # --- space ------------------------------------------------------------------
    ir = reverb_ir(0.9, total, spec.seed + 5, damp=5200.0)
    wet = np.stack([circ_convolve(dry[:, 0], ir), circ_convolve(dry[:, 1], ir)], axis=1)
    mix = dry * 0.82 + wet * 0.34
    mix = soft_clip(mix / max(np.max(np.abs(mix)), 1e-9) * 1.15) * 0.85
    return normalize(mix, -1.5)


# ---------------------------------------------------------------------------
# Effects
# ---------------------------------------------------------------------------

def whiff(dur: float, seed: int, heavy: bool = False) -> np.ndarray:
    n = n_samples(dur)
    t = t_axis(n)
    rng = np.random.default_rng(seed)
    noise = rng.standard_normal(n) * 0.7 + rng.standard_normal(n) * 0.3
    # High filter orders and a loud air band keep these clearly airy: a low-order
    # band pass leaks so much low-mid energy that the whoosh reads as a rumble.
    if heavy:
        core = f_bandpass(noise, 320, 1800, 3) * 0.7
        air = f_bandpass(noise, 1800, 6500, 2) * 1.0
    else:
        core = f_bandpass(noise, 900, 4200, 4) * 0.6
        air = f_bandpass(noise, 4200, 11000, 2) * 1.6
    # Smooth swell in and out: airy, deliberately without an impulsive peak.
    shape = np.sin(math.pi * np.clip(t / dur, 0.0, 1.0)) ** 1.15
    y = (core + air) * shape
    y = smooth(y, 6.0)
    y = f_lowpass(y, 11000, 2)
    return normalize(y, -3.0)


def impact(dur: float, seed: int, family: str) -> np.ndarray:
    """family: light | heavy | block | glove | strike"""
    n = n_samples(dur)
    t = t_axis(n)
    rng = np.random.default_rng(seed)
    noise = rng.standard_normal(n)

    if family == "light":
        # Bright, snappy: the high crack carries the sound and the body is short,
        # so the hit reads as a light smack rather than a heavy thud.
        crack = f_bandpass(noise, 1500, 6800, 2) * env_ad(t, 0.0011, 0.034, 4.2)
        crack_gain = 1.35
        body_f, body_decay, body_gain = 112.0, 0.045, 0.62
        click_gain = 0.30
        thud = None
    elif family == "heavy":
        # Weighty: most of the energy is in the sub/body, with a shorter, softer crack.
        crack = f_bandpass(noise, 700, 4200, 2) * env_ad(t, 0.0013, 0.058, 3.8)
        crack_gain = 0.55
        body_f, body_decay, body_gain = 64.0, 0.210, 1.15
        click_gain = 0.12
        thud = (np.sin(2.0 * math.pi * 44.0 * t) * env_ad(t, 0.004, 0.26, 3.0) * 0.70
                + np.sin(2.0 * math.pi * 33.0 * t) * env_ad(t, 0.006, 0.30, 2.6) * 0.35)
    elif family == "block":
        # Duller on purpose: glove on glove. Low-mid noise and a low body only,
        # with no high crack and no click at all, so it cannot be mistaken for a hit.
        crack = f_bandpass(noise, 240, 1000, 2) * env_ad(t, 0.0022, 0.052, 3.4)
        crack_gain = 0.95
        body_f, body_decay, body_gain = 118.0, 0.125, 0.95
        click_gain = 0.0
        thud = np.sin(2.0 * math.pi * 78.0 * t) * env_ad(t, 0.003, 0.12, 3.2) * 0.40
    elif family == "glove":
        # Rocky's padded glove: rounder and duller than the generic heavy hit.
        crack = f_bandpass(noise, 600, 3000, 2) * env_ad(t, 0.0016, 0.052, 3.6)
        crack_gain = 0.85
        body_f, body_decay, body_gain = 82.0, 0.170, 1.05
        click_gain = 0.10
        thud = np.sin(2.0 * math.pi * 55.0 * t) * env_ad(t, 0.003, 0.19, 3.0) * 0.45
    else:  # strike - Shadow: sharper, snappier, higher centroid
        crack = f_bandpass(noise, 2200, 9000, 2) * env_ad(t, 0.0008, 0.026, 4.6)
        crack_gain = 1.50
        body_f, body_decay, body_gain = 150.0, 0.040, 0.60
        click_gain = 0.30
        thud = None

    body = np.sin(2.0 * math.pi * body_f * t) * env_ad(t, 0.0018, body_decay, 3.4) * body_gain
    y = crack * crack_gain + body
    if thud is not None:
        y = y + thud
    if click_gain > 0.0:
        y = y + f_bandpass(noise, 2000, 8000, 2) * env_ad(t, 0.0006, 0.008, 6.0) * click_gain
    return normalize(y, -1.5)


def swish(dur: float, seed: int, low: bool = False) -> np.ndarray:
    n = n_samples(dur)
    t = t_axis(n)
    rng = np.random.default_rng(seed)
    noise = rng.standard_normal(n)
    band = f_bandpass(noise, 200 if low else 400, 1800 if low else 3200, 2)
    y = band * np.sin(math.pi * np.clip(t / dur, 0.0, 1.0)) ** 1.4
    return normalize(smooth(y, 8.0), -6.0)


def footstep(seed: int) -> np.ndarray:
    dur = 0.12
    n = n_samples(dur)
    t = t_axis(n)
    rng = np.random.default_rng(seed)
    scuff = f_bandpass(rng.standard_normal(n), 250, 2200, 2) * env_ad(t, 0.001, 0.030, 4.0)
    thud = np.sin(2.0 * math.pi * 92.0 * t) * env_ad(t, 0.002, 0.026, 4.0) * 0.5
    return normalize(scuff + thud, -7.0)


def land(seed: int) -> np.ndarray:
    dur = 0.35
    n = n_samples(dur)
    t = t_axis(n)
    rng = np.random.default_rng(seed)
    thud = np.sin(2.0 * math.pi * 56.0 * t) * env_ad(t, 0.003, 0.15, 3.0)
    rustle = f_bandpass(rng.standard_normal(n), 400, 3200, 2) * env_ad(t, 0.004, 0.09, 3.2) * 0.4
    return normalize(thud + rustle, -2.5)


def ko_hit(seed: int) -> np.ndarray:
    dur = 0.5
    n = n_samples(dur)
    t = t_axis(n)
    rng = np.random.default_rng(seed)
    boom = np.sin(2.0 * math.pi * phase_of(np.full(n, 48.0) * (1.0 + 0.6 * np.exp(-t / 0.05)))) * \
        env_ad(t, 0.003, 0.26, 2.6)
    crack = f_bandpass(rng.standard_normal(n), 1200, 6000, 2) * env_ad(t, 0.001, 0.04, 4.0) * 0.7
    ring = np.zeros(n)
    for ratio, amp, tau in ((1.0, 0.35, 0.34), (2.41, 0.22, 0.26), (3.87, 0.14, 0.20), (5.31, 0.08, 0.14)):
        ring += amp * np.sin(2.0 * math.pi * 220.0 * ratio * t) * np.exp(-t / tau)
    return normalize(boom + crack + ring * 0.5, -1.5)


def bell(dur: float = 1.8, base: float = 330.0, seed: int = 3) -> np.ndarray:
    n = n_samples(dur)
    t = t_axis(n)
    rng = np.random.default_rng(seed)
    out = np.zeros(n)
    for ratio, amp, tau in ((0.56, 0.45, 1.5), (0.92, 0.55, 1.35), (1.19, 0.40, 1.05),
                            (1.71, 0.30, 0.85), (2.00, 0.26, 0.70), (2.74, 0.18, 0.55),
                            (3.00, 0.14, 0.45), (4.07, 0.09, 0.32)):
        out += amp * np.sin(2.0 * math.pi * base * ratio * (1.0 + 0.0004 * rng.standard_normal()) * t +
                            rng.uniform(0, 6.28)) * np.exp(-t / tau)
    strike = f_bandpass(rng.standard_normal(n), 2000, 9000, 2) * env_ad(t, 0.0006, 0.010, 6.0) * 0.5
    return normalize(out + strike, -2.0)


def stinger(dur: float, seed: int, kind: str) -> np.ndarray:
    n = n_samples(dur)
    t = t_axis(n)
    rng = np.random.default_rng(seed)

    def chord(freqs, amp, atk, rel):
        y = np.zeros(n)
        for i, f in enumerate(freqs):
            v = band_limited_saw(phase_of(np.full(n, f)), f, 20)
            v = f_lowpass(v, 2600, 2)
            y += v * amp / len(freqs)
        return y * env_ar(t, atk, rel)

    if kind == "round_start":
        y = chord([midi_to_hz(45), midi_to_hz(57), midi_to_hz(60), midi_to_hz(64)], 0.9, 0.02, 0.55)
        y += bell(1.2, 494.0, seed + 1)[:n] * 0.35
        y += np.sin(2.0 * math.pi * midi_to_hz(33) * t) * env_ad(t, 0.002, 0.5, 2.5) * 0.4
    elif kind == "round_end":
        y = chord([midi_to_hz(48), midi_to_hz(55), midi_to_hz(60)], 0.8, 0.01, 0.7)
        y += bell(1.5, 262.0, seed + 2)[:n] * 0.5
    elif kind == "ko":
        y = np.zeros(n)
        kh = ko_hit(seed + 3)
        m = min(n, len(kh))
        y[:m] += kh[:m]
        glide = np.linspace(midi_to_hz(57), midi_to_hz(33), n)
        y += np.sin(2.0 * math.pi * phase_of(glide)) * env_ad(t, 0.005, 0.6, 2.2) * 0.55
        y += np.sin(2.0 * math.pi * midi_to_hz(29) * t) * env_ad(t, 0.008, 1.0, 2.0) * 0.5
    elif kind == "victory":
        seq = [57, 60, 64, 69, 72]
        y = np.zeros(n)
        step = dur / (len(seq) + 0.6)
        for i, m in enumerate(seq):
            f = midi_to_hz(m)
            s = n_samples(i * step)
            seg = min(n_samples(step * 1.7), n - s)
            if seg <= 0:
                continue
            note = synth_note(f, seg / SR, seed + 20 + i, "saw", detune=0.005, cutoff=4200.0,
                              attack=0.008, decay=step * 1.2, harmonics=16)[:seg]
            y[s:s + seg] += note * 0.5
            pl = pluck(f * 2, seg / SR, seed + 40 + i)[:seg]
            y[s:s + seg] += pl * 0.35
        y += np.sin(2.0 * math.pi * midi_to_hz(45) * t) * env_ad(t, 0.05, 1.4, 1.8) * 0.35
    elif kind == "defeat":
        seq = [57, 55, 53, 52, 45]
        y = np.zeros(n)
        step = dur / (len(seq) + 0.4)
        for i, m in enumerate(seq):
            f = midi_to_hz(m)
            s = n_samples(i * step)
            seg = min(n_samples(step * 1.45), n - s)
            if seg <= 0:
                continue
            note = synth_note(f, seg / SR, seed + 60 + i, "tri", detune=0.004, cutoff=1600.0,
                              attack=0.02, decay=step * 1.1, harmonics=12)[:seg]
            y[s:s + seg] += note * 0.6
        y += np.sin(2.0 * math.pi * midi_to_hz(33) * t) * env_ad(t, 0.08, 1.6, 1.6) * 0.4
    else:
        y = bell(dur, 262.0, seed)
    return normalize(y, -1.5)


def ui_blip(dur: float, kind: str, seed: int) -> np.ndarray:
    n = n_samples(dur)
    t = t_axis(n)
    if kind == "focus":
        y = 0.6 * np.sin(2.0 * math.pi * 1180.0 * t) * env_ad(t, 0.0015, 0.020, 5.0)
        y += 0.25 * np.sin(2.0 * math.pi * 2360.0 * t) * env_ad(t, 0.001, 0.012, 6.0)
    elif kind == "select":
        half = n // 2
        y = np.zeros(n)
        for i, (f, start) in enumerate(((784.0, 0), (1175.0, half))):
            seg = n - start
            ty = t_axis(seg)
            y[start:] += 0.6 * np.sin(2.0 * math.pi * f * ty) * env_ad(ty, 0.0015, 0.035, 4.5)
    elif kind == "back":
        half = n // 2
        y = np.zeros(n)
        for i, (f, start) in enumerate(((880.0, 0), (587.0, half))):
            seg = n - start
            ty = t_axis(seg)
            y[start:] += 0.55 * np.sin(2.0 * math.pi * f * ty) * env_ad(ty, 0.0015, 0.035, 4.5)
    else:  # disabled
        y = 0.5 * square(phase_of(np.full(n, 180.0)), 0.5) * env_ad(t, 0.002, 0.045, 4.0)
        y = f_lowpass(y, 900, 2)
        y *= 1.0 + 0.5 * np.sin(2.0 * math.pi * 55.0 * t)
    return normalize(y, -4.0)


def ambience(dur: float, seed: int) -> np.ndarray:
    """Seamless rooftop-at-night bed: periodic noise beds and hums only."""
    n = n_samples(dur)
    t = t_axis(n)
    rng = np.random.default_rng(seed)

    wind = f_bandpass(rng.standard_normal(n), 70, 900, 2)
    # Amplitude LFOs whose periods divide the loop length exactly -> seamless.
    lfo = np.ones(n)
    for period, depth, phase in ((dur, 0.35, 0.0), (dur / 2.0, 0.20, 1.1), (dur / 4.0, 0.10, 2.3)):
        lfo += depth * np.sin(2.0 * math.pi * (t / period) + phase)
    wind = wind * np.clip(lfo, 0.25, 2.0) * 0.6

    hiss = f_bandpass(rng.standard_normal(n), 1500, 6000, 2) * 0.05
    hum = np.zeros(n)
    for f, amp in ((50.0, 0.16), (100.0, 0.09), (150.0, 0.04)):
        hum += amp * np.sin(2.0 * math.pi * f * t)
    hum *= 1.0 + 0.25 * np.sin(2.0 * math.pi * (t / (dur / 3.0)))

    traffic = f_bandpass(rng.standard_normal(n), 180, 600, 2)
    swell = 0.5 + 0.5 * np.sin(2.0 * math.pi * (t / (dur / 2.0)) + 0.7)
    traffic = traffic * swell * 0.12

    mono = wind + hiss + hum + traffic
    mono = f_lowpass(mono, 7000, 2)
    left = mono * 0.98 + f_bandpass(rng.standard_normal(n), 300, 1200, 2) * 0.06
    right = mono * 0.98 + f_bandpass(rng.standard_normal(n), 300, 1200, 2) * 0.06
    return normalize(np.stack([left, right], axis=1), -10.0)


# ---------------------------------------------------------------------------
# Character voices (non-verbal, formant-shaped)
# ---------------------------------------------------------------------------

ROCKY = dict(f0=132.0, formants=(520.0, 1080.0, 2400.0), bw=(0.30, 0.34, 0.42), breath=0.055, alpha=1.05)
SHADOW = dict(f0=178.0, formants=(700.0, 1620.0, 3150.0), bw=(0.26, 0.30, 0.38), breath=0.075, alpha=1.18)


def voice(profile: dict, dur: float, seed: int, pitch_curve, formant_shift: float = 1.0,
          amp_curve=None, breath_scale: float = 1.0) -> np.ndarray:
    n = n_samples(dur)
    t = t_axis(n)
    rng = np.random.default_rng(seed)

    f0 = profile["f0"] * pitch_curve(t)
    f0 = f0 * (1.0 + 0.02 * np.sin(2.0 * math.pi * 5.3 * t + rng.uniform(0, 6.28)))
    phase = phase_of(f0)

    src = band_limited_saw(phase, profile["f0"], 28) * 0.9
    src += rng.standard_normal(n) * profile["breath"] * breath_scale
    src = f_lowpass(src, 4500.0, 2)

    f = _freqs(n)
    shape = np.ones(n // 2 + 1)
    for fc, bw, gain in zip(profile["formants"], profile["bw"],
                            (1.0, 0.62, 0.34)):
        shape += peak_shape(f, fc * formant_shift, bw, gain)
    shape += 0.06
    y = fft_filter(src, shape)

    env = amp_curve(t) if amp_curve is not None else env_ar(t, 0.02, dur * 0.45, 2.4)
    y = y * env
    y = f_highpass(y, 90.0, 2)
    return normalize(smooth(y, 2.0), -3.0)


def exhale_curve(dur: float, seed: int):
    rng = np.random.default_rng(seed)
    def curve(t):
        x = np.clip(t / dur, 0.0, 1.0)
        return np.sin(math.pi * x ** 0.6) ** 0.8 * (1.0 - 0.25 * x) * (1.0 + 0.05 * rng.standard_normal(len(t)))
    return curve


def hurt_curve(dur: float, seed: int):
    def curve(t):
        x = np.clip(t / dur, 0.0, 1.0)
        return np.clip(np.sin(math.pi * np.minimum(x * 1.6, 1.0)) ** 0.7, 0, None)
    return curve


def victory_curve(dur: float, seed: int):
    def curve(t):
        x = np.clip(t / dur, 0.0, 1.0)
        return np.sin(math.pi * x ** 0.75) ** 0.9
    return curve


def make_voice_set(profile: dict, prefix: str, seed_base: int) -> list[tuple[str, float, np.ndarray]]:
    out: list[tuple[str, float, np.ndarray]] = []

    for i in range(3):
        dur = 0.25 if prefix == "Rocky" else 0.22
        s = seed_base + i
        rng = np.random.default_rng(s)
        base = profile["f0"] * (0.96 + 0.08 * rng.random())
        p = profile.copy()
        p["f0"] = base

        def pc(t, rng=rng):
            return 1.0 + 0.16 * np.sin(2.0 * math.pi * 3.1 * t) - 0.12 * np.clip(t / dur, 0, 1)

        out.append((f"VO_{prefix}_ExertLight_{i + 1:02d}", dur,
                    voice(p, dur, s, pc, formant_shift=1.0 + 0.05 * i, amp_curve=exhale_curve(dur, s))))

    for i in range(3):
        dur = 0.40 if prefix == "Rocky" else 0.36
        s = seed_base + 10 + i
        rng = np.random.default_rng(s)
        p = profile.copy()
        p["f0"] = profile["f0"] * (0.90 + 0.07 * rng.random())
        p["breath"] = profile["breath"] * 1.35

        def pc(t, rng=rng):
            return 1.0 + 0.10 * np.sin(2.0 * math.pi * 4.2 * t) - 0.20 * np.clip(t / dur, 0, 1)

        out.append((f"VO_{prefix}_ExertHeavy_{i + 1:02d}", dur,
                    voice(p, dur, s, pc, formant_shift=0.94 + 0.05 * i,
                          amp_curve=exhale_curve(dur, s), breath_scale=1.3)))

    for i in range(3):
        dur = 0.30 if prefix == "Rocky" else 0.28
        s = seed_base + 20 + i
        rng = np.random.default_rng(s)
        p = profile.copy()
        p["f0"] = profile["f0"] * (1.06 + 0.08 * rng.random())
        p["breath"] = profile["breath"] * 1.2

        def pc(t, rng=rng):
            return 1.0 - 0.30 * np.clip(t / dur, 0, 1) + 0.05 * np.sin(2.0 * math.pi * 7.0 * t)

        out.append((f"VO_{prefix}_Hurt_{i + 1:02d}", dur,
                    voice(p, dur, s, pc, formant_shift=1.05 + 0.05 * i, amp_curve=hurt_curve(dur, s))))

    for i in range(2):
        dur = 1.20 if prefix == "Rocky" else 1.30
        s = seed_base + 30 + i
        rng = np.random.default_rng(s)
        p = profile.copy()
        p["f0"] = profile["f0"] * (1.0 + 0.06 * rng.random())
        p["breath"] = profile["breath"] * 0.9

        def pc(t, rng=rng):
            x = np.clip(t / dur, 0.0, 1.0)
            return 1.0 + 0.22 * np.sin(math.pi * x) * (1.0 + 0.4 * x) + 0.03 * np.sin(2 * math.pi * 6.0 * t)

        out.append((f"VO_{prefix}_Victory_{i + 1:02d}", dur,
                    voice(p, dur, s, pc, formant_shift=1.02 + 0.06 * i,
                          amp_curve=victory_curve(dur, s))))

    return out


# ---------------------------------------------------------------------------
# Asset table
# ---------------------------------------------------------------------------

@dataclass
class Asset:
    rel: str              # path under ArtSource/Audio, with .wav
    category: str
    character: str | None
    method: str
    loops: bool
    builder: object       # callable -> np.ndarray
    target_seconds: float


def build_assets() -> list[Asset]:
    assets: list[Asset] = []

    songs = [
        SongSpec("MUS_Title", 120.0, 12, [(57, [0, 3, 7]), (53, [0, 4, 7]), (48, [0, 4, 7]),
                                          (55, [0, 4, 7])], SEED + 1, "title", 2.0),
        SongSpec("MUS_Menu", 100.0, 8, [(57, [0, 3, 7]), (55, [0, 4, 7]), (53, [0, 4, 7]),
                                        (52, [0, 4, 7])], SEED + 2, "menu", 2.0),
        SongSpec("MUS_Arena", 128.0, 17, [(50, [0, 3, 7]), (46, [0, 3, 7]), (41, [0, 4, 7]),
                                          (48, [0, 4, 7])], SEED + 3, "arena", 2.0),
    ]
    for s in songs:
        bar = 60.0 / s.bpm * 4.0
        assets.append(Asset(f"Music/{s.name}.wav", "music", None,
                            "additive/subtractive synth: band-limited saw bass, detuned pad, "
                            "delay-fed square arp, synthesised kick/clap/hat, circular reverb wrap",
                            True, (lambda sp=s: render_song(sp)), bar * s.bars))

    assets.append(Asset("Ambience/AMB_Rooftop.wav", "ambience", None,
                        "periodic band-passed noise beds (wind, city hum, distant traffic) "
                        "with amplitude LFOs whose periods divide the loop length",
                        True, lambda: ambience(20.0, SEED + 4), 20.0))

    assets += [
        Asset("Stingers/STG_RoundStart.wav", "stinger", None,
              "saw stack chord + struck bell + sub", False, lambda: stinger(1.2, SEED + 5, "round_start"), 1.2),
        Asset("Stingers/STG_RoundEnd.wav", "stinger", None,
              "saw stack chord + struck bell", False, lambda: stinger(1.5, SEED + 6, "round_end"), 1.5),
        Asset("Stingers/STG_KO.wav", "stinger", None,
              "impact body + descending pitch glide + sub boom", False, lambda: stinger(2.0, SEED + 7, "ko"), 2.0),
        Asset("Stingers/STG_Victory.wav", "stinger", None,
              "rising saw arpeggio (A major) + plucks + sustained sub", False,
              lambda: stinger(3.0, SEED + 8, "victory"), 3.0),
        Asset("Stingers/STG_Defeat.wav", "stinger", None,
              "descending minor motif (A-G-F-E-A) + low pad", False,
              lambda: stinger(2.5, SEED + 9, "defeat"), 2.5),
        Asset("Stingers/STG_Bell.wav", "stinger", None,
              "inharmonic bell partials + strike transient", False, lambda: bell(1.8, 330.0, SEED + 10), 1.8),
    ]

    assets += [
        Asset("UI/UI_Focus.wav", "ui", None, "two sine blips, fast decay", False,
              lambda: ui_blip(0.08, "focus", SEED + 11), 0.08),
        Asset("UI/UI_Select.wav", "ui", None, "rising two-note sine pair", False,
              lambda: ui_blip(0.15, "select", SEED + 12), 0.15),
        Asset("UI/UI_Back.wav", "ui", None, "falling two-note sine pair", False,
              lambda: ui_blip(0.15, "back", SEED + 13), 0.15),
        Asset("UI/UI_Disabled.wav", "ui", None, "dull filtered square with tremolo", False,
              lambda: ui_blip(0.12, "disabled", SEED + 14), 0.12),
    ]

    for i in range(3):
        assets.append(Asset(f"Combat/Whiff_Light_{i + 1:02d}.wav", "combat", None,
                            "band-passed noise swell, smooth attack, no impulse", False,
                            (lambda i=i: whiff(0.18, SEED + 100 + i, False)), 0.18))
    for i in range(3):
        assets.append(Asset(f"Combat/Whiff_Heavy_{i + 1:02d}.wav", "combat", None,
                            "lower, longer band-passed noise swell, no impulse", False,
                            (lambda i=i: whiff(0.30, SEED + 110 + i, True)), 0.30))
    for i in range(4):
        assets.append(Asset(f"Combat/Impact_Light_{i + 1:02d}.wav", "combat", None,
                            "high band-passed noise crack + 112 Hz body thud, 1 ms attack", False,
                            (lambda i=i: impact(0.15, SEED + 120 + i, "light")), 0.15))
    for i in range(4):
        assets.append(Asset(f"Combat/Impact_Heavy_{i + 1:02d}.wav", "combat", None,
                            "mid/high crack + 64 Hz body + 44 Hz sub, longer tail", False,
                            (lambda i=i: impact(0.28, SEED + 130 + i, "heavy")), 0.28))
    for i in range(3):
        assets.append(Asset(f"Combat/Block_{i + 1:02d}.wav", "combat", None,
                            "dull 280-1150 Hz band + 186 Hz low-mid resonance, no high crack", False,
                            (lambda i=i: impact(0.18, SEED + 140 + i, "block")), 0.18))
    for i in range(3):
        assets.append(Asset(f"Combat/Dodge_{i + 1:02d}.wav", "combat", None,
                            "soft low cloth swish, quieter than the whiffs", False,
                            (lambda i=i: swish(0.20, SEED + 150 + i, True)), 0.20))
    for i in range(4):
        assets.append(Asset(f"Combat/Footstep_{i + 1:02d}.wav", "combat", None,
                            "canvas scuff noise + 92 Hz thud", False,
                            (lambda i=i: footstep(SEED + 160 + i)), 0.12))
    for i in range(2):
        assets.append(Asset(f"Combat/Land_{i + 1:02d}.wav", "combat", None,
                            "56 Hz body drop + canvas rustle", False,
                            (lambda i=i: land(SEED + 170 + i)), 0.35))
    for i in range(2):
        assets.append(Asset(f"Combat/KO_{i + 1:02d}.wav", "combat", None,
                            "heavy landing + pitch-swept boom + metallic ring-out", False,
                            (lambda i=i: ko_hit(SEED + 180 + i)), 0.50))

    for name, dur, sig in make_voice_set(ROCKY, "Rocky", SEED + 200):
        assets.append(Asset(f"Rocky/{name}.wav", "voice", "rocky",
                            "glottal pulse train + breath noise shaped by three log-Gaussian formants",
                            False, (lambda s=sig: s), dur))
    for name, dur, sig in make_voice_set(SHADOW, "Shadow", SEED + 300):
        assets.append(Asset(f"Shadow/{name}.wav", "voice", "shadow",
                            "glottal pulse train + breath noise shaped by three higher, tighter formants",
                            False, (lambda s=sig: s), dur))

    for i in range(3):
        assets.append(Asset(f"Rocky/GloveImpact_{i + 1:02d}.wav", "impact", "rocky",
                            "padded glove: 600-3000 Hz band + 82 Hz body", False,
                            (lambda i=i: impact(0.20, SEED + 400 + i, "glove")), 0.20))
    for i in range(3):
        assets.append(Asset(f"Shadow/StrikeImpact_{i + 1:02d}.wav", "impact", "shadow",
                            "snappier strike: 2200-9000 Hz band + 150 Hz body", False,
                            (lambda i=i: impact(0.18, SEED + 420 + i, "strike")), 0.18))

    return assets


# ---------------------------------------------------------------------------
# Measurement
# ---------------------------------------------------------------------------

def spectral_centroid(x: np.ndarray) -> float:
    mono = x.mean(axis=1) if x.ndim > 1 else x
    w = np.hanning(len(mono))
    X = np.abs(np.fft.rfft(mono * w)) ** 2
    f = np.fft.rfftfreq(len(mono), 1.0 / SR)
    total = X.sum()
    return float((f * X).sum() / total) if total > 1e-20 else 0.0


def crack_centroid(x: np.ndarray, hp_hz: float = 250.0) -> float:
    """Spectral centroid of the transient content only, high-passed above the
    body thud. A decaying low body holds most of the total energy of an impact,
    so a full-band centroid mostly reports the body pitch; this measures how
    bright the noise/crack component is, which is what a listener hears as
    \"sharp\" versus \"dull\"."""
    mono = x.mean(axis=1) if x.ndim > 1 else x
    return spectral_centroid(f_highpass(mono, hp_hz, 2))


def low_energy_ratio(x: np.ndarray, split_hz: float = 200.0) -> float:
    mono = x.mean(axis=1) if x.ndim > 1 else x
    X = np.abs(np.fft.rfft(mono)) ** 2
    f = np.fft.rfftfreq(len(mono), 1.0 / SR)
    total = X.sum()
    if total <= 1e-20:
        return 0.0
    return float(X[f < split_hz].sum() / total)


def attack_ms(x: np.ndarray) -> float:
    mono = np.abs(x.mean(axis=1) if x.ndim > 1 else x)
    win = max(1, n_samples(0.001))
    k = len(mono) // win
    if k < 4:
        return 0.0
    env = mono[: k * win].reshape(k, win).max(axis=1)
    peak = env.max()
    if peak <= 1e-12:
        return 0.0
    i10 = int(np.argmax(env >= 0.10 * peak))
    i90 = int(np.argmax(env >= 0.90 * peak))
    return float((i90 - i10) * win / SR * 1000.0)


def loop_wrap(x: np.ndarray) -> dict:
    mono = x.mean(axis=1) if x.ndim > 1 else x
    d = np.abs(np.diff(mono))
    return {
        "wrap_delta": float(abs(mono[-1] - mono[0])),
        "median_adjacent_delta": float(np.median(d)),
        "p95_adjacent_delta": float(np.percentile(d, 95)),
        "p99_adjacent_delta": float(np.percentile(d, 99)),
        "max_adjacent_delta": float(d.max()),
    }


def peak_dbfs(x: np.ndarray) -> float:
    return lin_to_db(float(np.max(np.abs(x))) if x.size else 0.0)


def rms_dbfs(x: np.ndarray) -> float:
    return lin_to_db(float(np.sqrt(np.mean(x.astype(np.float64) ** 2))) if x.size else 0.0)


def write_wav(path: Path, data: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if data.ndim == 1:
        data = data[:, None]
    peak = np.max(np.abs(data)) if data.size else 0.0
    if peak > db_to_lin(PEAK_DBFS) + 1e-9:
        data = data * (db_to_lin(PEAK_DBFS) / peak)
    ints = np.clip(np.round(data * 32767.0), -32768, 32767).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(ints.shape[1])
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(ints.tobytes())


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--verify", action="store_true", help="also write Tools/Audio/verification.json")
    args = ap.parse_args()

    assets = build_assets()
    script_path = Path(__file__).resolve()
    script_sha = hashlib.sha256(script_path.read_bytes()).hexdigest()

    print(f"generating {len(assets)} files under {OUT_ROOT}")
    records = []
    rendered: dict[str, np.ndarray] = {}

    for a in assets:
        sig = np.asarray(a.builder(), dtype=np.float64)
        write_wav(OUT_ROOT / a.rel, sig)
        rendered[a.rel] = sig
        records.append({
            "path": a.rel,
            "category": a.category,
            "character": a.character,
            "duration_seconds": round(len(sig) / SR, 4),
            "sample_rate": SR,
            "channels": 1 if sig.ndim == 1 else sig.shape[1],
            "peak_dbfs": round(peak_dbfs(sig), 3),
            "rms_dbfs": round(rms_dbfs(sig), 3),
            "spectral_centroid_hz": round(spectral_centroid(sig), 1),
            "synthesis_method": a.method,
            "loops_seamlessly": bool(a.loops),
        })
        print(f"  {a.rel:42s} {len(sig) / SR:6.3f}s  peak {peak_dbfs(sig):7.2f} dBFS")

    provenance = {
        "generator_script": str(script_path),
        "generator_script_sha256": script_sha,
        "generated_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "seed": SEED,
        "sample_rate": SR,
        "bit_depth": 16,
        "peak_ceiling_dbfs": PEAK_DBFS,
        "statement": (
            "All content in this directory is original procedural synthesis made for this "
            "project. It contains no third-party or copyrighted material, no samples were "
            "used, no network resources or sample libraries were consulted, and the project "
            "owner holds full rights to every file."
        ),
        "files": records,
    }
    prov_path = OUT_ROOT / "provenance.json"
    prov_path.write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {prov_path}")

    groups: dict[str, list[dict]] = {}
    for r in records:
        groups.setdefault(r["category"], []).append(r)
    lines = ["# Rocky Kombat - audio manifest", "",
             "Generated by `Tools/Audio/rkc_generate_audio.py` (see `provenance.json` for the",
             "generator SHA-256, seed and per-file measurements). All content is original",
             "procedural synthesis: no samples, no third-party material.", ""]
    titles = {"music": "Music (seamless loops)", "ambience": "Ambience (seamless loop)",
              "stinger": "Stingers", "ui": "UI", "combat": "Combat (attacker-neutral)",
              "voice": "Character voices", "impact": "Character impact identity"}
    for cat in ("music", "ambience", "stinger", "ui", "combat", "voice", "impact"):
        if cat not in groups:
            continue
        lines += [f"## {titles[cat]}", "",
                  "| File | Length | Character | Where it is used |", "| --- | --- | --- | --- |"]
        for r in groups[cat]:
            lines.append(f"| `{r['path']}` | {r['duration_seconds']:.2f} s | "
                         f"{r['character'] or 'neutral'} | TBD |")
        lines.append("")
    lines += ["## Requirement coverage", "",
              "- Stated requirement: music plus punch/combat audio. - `Music/*`, `Combat/*`, `Stingers/*`.",
              "- Stated requirement: whiffs, blocks and clean hits must sound different. -",
              "  `Combat/Whiff_*` is a smooth airy noise swell, `Combat/Block_*` is a dull low-mid smack,",
              "  `Combat/Impact_*` is a bright transient crack with a body thud. Measured spectral",
              "  centroids and attack times are in `Tools/Audio/verification.json`.",
              "- Stated requirement: Rocky and Shadow must be audibly distinct. - Separate voice",
              "  formant profiles (`Rocky/*`, `Shadow/*`) and separate impact timbres",
              "  (`Rocky/GloveImpact_*`, `Shadow/StrikeImpact_*`).",
              "- Stated requirement: audio asset inventory/provenance. - `provenance.json`, this file.", ""]
    man_path = OUT_ROOT / "AUDIO_MANIFEST.md"
    man_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {man_path}")

    if args.verify:
        return verify(records, rendered)
    return 0


def verify(records: list[dict], rendered: dict[str, np.ndarray]) -> int:
    failures: list[str] = []
    checks: dict = {}

    # --- presence / format / duration ----------------------------------------
    for r in records:
        p = OUT_ROOT / r["path"]
        if not p.exists():
            failures.append(f"missing {r['path']}")
            continue
        with wave.open(str(p), "rb") as w:
            fmt = {"channels": w.getnchannels(), "sampwidth": w.getsampwidth() * 8,
                   "framerate": w.getframerate(), "frames": w.getnframes()}
        spec_expect_mono = r["category"] in ("combat", "ui", "stinger", "voice", "impact")
        want_ch = 1 if spec_expect_mono else 2
        if fmt["channels"] != want_ch:
            failures.append(f"{r['path']}: {fmt['channels']} channels, expected {want_ch}")
        if fmt["sampwidth"] != 16:
            failures.append(f"{r['path']}: {fmt['sampwidth']}-bit, expected 16")
        if fmt["framerate"] != SR:
            failures.append(f"{r['path']}: {fmt['framerate']} Hz, expected {SR}")
        flat = np.frombuffer(b"", dtype=np.int16)
        if abs(fmt["frames"] / SR - r["duration_seconds"]) > 0.002:
            failures.append(f"{r['path']}: frame count does not match duration")
        if p.stat().st_size > 10 * 1024 * 1024:
            failures.append(f"{r['path']}: larger than 10 MB")
        if r["peak_dbfs"] > -1.0:
            failures.append(f"{r['path']}: peak {r['peak_dbfs']} dBFS exceeds -1.0")
        _ = flat
    checks["file_count"] = len(records)
    checks["format_failures"] = failures[:]

    # --- byte uniqueness ------------------------------------------------------
    digests: dict[str, list[str]] = {}
    for r in records:
        d = hashlib.sha256((OUT_ROOT / r["path"]).read_bytes()).hexdigest()
        digests.setdefault(d, []).append(r["path"])
    dupes = {k: v for k, v in digests.items() if len(v) > 1}
    checks["duplicate_files"] = dupes
    if dupes:
        failures.append(f"{len(dupes)} duplicate file group(s)")

    # --- loop seams -----------------------------------------------------------
    loops = {}
    for r in records:
        if not r["loops_seamlessly"]:
            continue
        info = loop_wrap(rendered[r["path"]])
        ok = info["wrap_delta"] <= info["p99_adjacent_delta"]
        loops[r["path"]] = {**info, "criterion": "wrap_delta <= p99(|adjacent delta|)",
                            "passes": bool(ok)}
        if not ok:
            failures.append(f"{r['path']}: loop wrap discontinuity {info['wrap_delta']:.6f} "
                            f"> p99 {info['p99_adjacent_delta']:.6f}")
    checks["loops"] = loops

    # --- family separation ----------------------------------------------------
    fam = {}
    for r in records:
        x = rendered[r["path"]]
        fam[r["path"]] = {
            "duration_s": r["duration_seconds"],
            "spectral_centroid_hz": round(spectral_centroid(x), 1),
            "crack_centroid_hz": round(crack_centroid(x), 1),
            "attack_ms": round(attack_ms(x), 2),
            "low_energy_ratio_below_200hz": round(low_energy_ratio(x, 200.0), 4),
            "low_energy_ratio_below_100hz": round(low_energy_ratio(x, 100.0), 4),
            "rms_dbfs": r["rms_dbfs"],
            "peak_dbfs": r["peak_dbfs"],
        }

    def avg(prefix: str, key: str) -> float:
        vals = [v[key] for k, v in fam.items() if k.startswith(prefix)]
        return float(np.mean(vals)) if vals else float("nan")

    sep = {
        "whiff_light_centroid_hz": avg("Combat/Whiff_Light", "spectral_centroid_hz"),
        "whiff_heavy_centroid_hz": avg("Combat/Whiff_Heavy", "spectral_centroid_hz"),
        "impact_light_centroid_hz": avg("Combat/Impact_Light", "spectral_centroid_hz"),
        "impact_heavy_centroid_hz": avg("Combat/Impact_Heavy", "spectral_centroid_hz"),
        "block_centroid_hz": avg("Combat/Block", "spectral_centroid_hz"),
        "whiff_light_attack_ms": avg("Combat/Whiff_Light", "attack_ms"),
        "impact_light_attack_ms": avg("Combat/Impact_Light", "attack_ms"),
        "impact_heavy_low_ratio": avg("Combat/Impact_Heavy", "low_energy_ratio_below_200hz"),
        "impact_light_low_ratio": avg("Combat/Impact_Light", "low_energy_ratio_below_200hz"),
        "rocky_glove_centroid_hz": avg("Rocky/GloveImpact", "spectral_centroid_hz"),
        "shadow_strike_centroid_hz": avg("Shadow/StrikeImpact", "spectral_centroid_hz"),
        "rocky_hurt_centroid_hz": avg("Rocky/VO_Rocky_Hurt", "spectral_centroid_hz"),
        "shadow_hurt_centroid_hz": avg("Shadow/VO_Shadow_Hurt", "spectral_centroid_hz"),
        # Transient-only (above 250 Hz) brightness: how sharp the crack sounds.
        "whiff_light_crack_centroid_hz": avg("Combat/Whiff_Light", "crack_centroid_hz"),
        "whiff_heavy_crack_centroid_hz": avg("Combat/Whiff_Heavy", "crack_centroid_hz"),
        "impact_light_crack_centroid_hz": avg("Combat/Impact_Light", "crack_centroid_hz"),
        "impact_heavy_crack_centroid_hz": avg("Combat/Impact_Heavy", "crack_centroid_hz"),
        "block_crack_centroid_hz": avg("Combat/Block", "crack_centroid_hz"),
        "rocky_glove_crack_centroid_hz": avg("Rocky/GloveImpact", "crack_centroid_hz"),
        "shadow_strike_crack_centroid_hz": avg("Shadow/StrikeImpact", "crack_centroid_hz"),
        # Weight: share of the energy under 100 Hz, where a heavy body/sub lives.
        "impact_heavy_sub_ratio": avg("Combat/Impact_Heavy", "low_energy_ratio_below_100hz"),
        "impact_light_sub_ratio": avg("Combat/Impact_Light", "low_energy_ratio_below_100hz"),
        "block_sub_ratio": avg("Combat/Block", "low_energy_ratio_below_100hz"),
    }
    verdict = {
        "block_transient_duller_than_impact_light":
            sep["block_crack_centroid_hz"] < sep["impact_light_crack_centroid_hz"] * 0.60,
        "impact_heavy_has_more_sub_weight_than_light":
            sep["impact_heavy_sub_ratio"] > sep["impact_light_sub_ratio"] * 1.5,
        "impact_light_transient_brighter_than_heavy":
            sep["impact_light_crack_centroid_hz"] > sep["impact_heavy_crack_centroid_hz"],
        "whiffs_are_slow_attacks_not_impulses":
            sep["whiff_light_attack_ms"] > 5.0 * max(sep["impact_light_attack_ms"], 0.001),
        "whiff_light_brighter_than_whiff_heavy":
            sep["whiff_light_crack_centroid_hz"] > sep["whiff_heavy_crack_centroid_hz"],
        "shadow_strike_sharper_than_rocky_glove":
            sep["shadow_strike_crack_centroid_hz"] > sep["rocky_glove_crack_centroid_hz"],
        "shadow_voice_brighter_than_rocky":
            sep["shadow_hurt_centroid_hz"] > sep["rocky_hurt_centroid_hz"],
    }
    for k, v in verdict.items():
        if not v:
            failures.append(f"separation check failed: {k}")
    checks["family_separation"] = sep
    checks["separation_verdict"] = verdict
    checks["per_file_measurements"] = fam
    checks["failures"] = failures
    checks["result"] = "PASS" if not failures else "FAIL"

    out = REPO / "Tools" / "Audio" / "verification.json"
    out.write_text(json.dumps(checks, indent=2) + "\n", encoding="utf-8")

    print("\n=== family separation ===")
    for k, v in sep.items():
        print(f"  {k:34s} {v:.2f}")
    print("=== verdict ===")
    for k, v in verdict.items():
        print(f"  {'PASS' if v else 'FAIL'}  {k}")
    print(f"=== loop seams ===")
    for k, v in loops.items():
        print(f"  {k:34s} wrap {v['wrap_delta']:.6f}  p99 {v['p99_adjacent_delta']:.6f}  "
              f"{'ok' if v['passes'] else 'FAIL'}")
    print(f"\nwrote {out}")
    print(f"RESULT: {checks['result']} ({len(failures)} failure(s))")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
