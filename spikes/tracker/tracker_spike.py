#!/usr/bin/env python3
# This file is part of Likyu — https://github.com/pob31/likyu
#
# Copyright (C) 2026 Pierre-Olivier Boulant
#
# Likyu is free software: you can redistribute it and/or modify it under the
# terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version. Likyu is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
# (LICENSE, at the repository root) for more details.
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""THROWAWAY: the Phase 0 tracker spike (devplan 0d). Nothing here migrates into lib/.

Three questions, each answered with numbers in docs/spikes/phase0-tracker.md:

  1. Estimator: a normalised-gradient adaptive notch (a frequency-locked loop
     on the SVF) against the energy-ratio discriminator, the latter on the raw
     key and behind a band-pass at the current estimate.
  2. Deflation between Track bands: extraction from the raw key, against the
     delayed subtraction of the trackers' own outputs, with and without a
     one-sample forward rotation.
  3. The band-pass detector envelope: k*sqrt(max(0, v1^2 - v2*hp)) against
     the first draft's k*sqrt(v1^2 + v2^2).

Usage:   python tracker_spike.py [--plots DIR] [--wav FILE ...]
Needs:   numpy; matplotlib for --plots.

Per-sample Python loops: slow and plain on purpose. The filter is the TPT
state-variable filter of the PRD (Filter core), coefficients recomputed every
sample, which the product will not do.
"""

import argparse
import math
import sys
import wave

import numpy as np

EPS = 1.0e-18


def cents(f, ref):
    return 1200.0 * math.log2(f / ref)


# ---------------------------------------------------------------------------
# The filter
# ---------------------------------------------------------------------------
class Svf:
    """Simper's TPT SVF. tick() returns (bp, lp, hp) with bp = v1, lp = v2."""

    __slots__ = ("fs", "k", "ic1", "ic2")

    def __init__(self, fs, q):
        self.fs = fs
        self.k = 1.0 / q
        self.ic1 = 0.0
        self.ic2 = 0.0

    def tick(self, x, fc):
        g = math.tan(math.pi * fc / self.fs)
        k = self.k
        a1 = 1.0 / (1.0 + g * (g + k))
        a2 = g * a1
        a3 = g * a2
        v3 = x - self.ic2
        v1 = a1 * self.ic1 + a2 * v3
        v2 = self.ic2 + a2 * self.ic1 + a3 * v3
        self.ic1 = 2.0 * v1 - self.ic1
        self.ic2 = 2.0 * v2 - self.ic2
        return v1, v2, x - k * v1 - v2


def bandpass_magnitude(f, fc, q, fs):
    """|H| of the unity-peak band-pass k*v1 at frequency f (bilinear, exact)."""
    w = math.tan(math.pi * f / fs) / math.tan(math.pi * fc / fs)
    k = 1.0 / q
    return (k * w) / math.sqrt((1.0 - w * w) ** 2 + (k * w) ** 2)


# ---------------------------------------------------------------------------
# The estimators. Each has tick(x) -> None and exposes f, bp, quad, confidence.
# ---------------------------------------------------------------------------
class TrackerBase:
    confidence_seconds = None   # None: smooth the two powers over `speed`

    def __init__(self, fs, f0, lo, hi, speed, q):
        self.fs = fs
        self.logf = math.log(f0)
        self.f = f0
        self.lo = math.log(lo)
        self.hi = math.log(hi)
        self.speed = speed          # seconds: the loop's time constant
        self.q = q
        self.k = 1.0 / q
        self.bp = 0.0               # unity-peak band-pass of the input at f
        self.quad = 0.0             # its quadrature (lags by 90 degrees at f)
        # Confidence = 1 - notch power / input power, both smoothed over `speed`.
        self.p_in = EPS
        self.p_notch = EPS
        seconds = self.confidence_seconds if self.confidence_seconds is not None else max(speed, 0.005)
        self.c_smooth = 1.0 - math.exp(-1.0 / (seconds * fs))
        self.confidence = 0.0

    def _finish(self, x):
        notch = x - self.bp
        a = self.c_smooth
        self.p_in += a * (x * x - self.p_in)
        self.p_notch += a * (notch * notch - self.p_notch)
        self.confidence = max(0.0, 1.0 - self.p_notch / (self.p_in + EPS))
        self.logf = min(self.hi, max(self.lo, self.logf))
        self.f = math.exp(self.logf)


class GradientNotch(TrackerBase):
    """Candidate 1. Minimise the notch output power with respect to log frequency.

    With e = x - bp (the notch output) and q the quadrature of bp, e*q is the
    gradient of e^2 with respect to the centre frequency up to a positive
    factor: the frequency-locked loop of the SOGI literature, here normalised
    by bp^2 + q^2 so that the rate does not depend on level. mu = k / (speed * fs)
    gives a time constant of `speed`, measured in part 1a (the first run of this
    spike had 2k there and a loop twice as fast as it claimed).
    """

    name = "gradient notch"

    def __init__(self, fs, f0, lo, hi, speed, q=4.0):
        super().__init__(fs, f0, lo, hi, speed, q)
        self.svf = Svf(fs, q)
        self.mu = self.k / (speed * fs)

    def tick(self, x):
        v1, v2, _ = self.svf.tick(x, self.f)
        self.bp = self.k * v1
        self.quad = self.k * v2
        e = x - self.bp
        self.logf -= self.mu * e * self.quad / (self.bp * self.bp + self.quad * self.quad + EPS)
        self._finish(x)


class EnergyRatio(TrackerBase):
    """Candidate 2. log(HP^2 / LP^2) / 4 reads the detuning in log frequency.

    prefilter=False: the discriminator SVF reads the key itself.
    prefilter=True:  it reads the key through a unity band-pass at the estimate.
    """

    def __init__(self, fs, f0, lo, hi, speed, q=4.0, prefilter=False):
        super().__init__(fs, f0, lo, hi, speed, q)
        self.prefilter = prefilter
        self.name = "energy ratio, behind a band-pass" if prefilter else "energy ratio, raw key"
        self.pre = Svf(fs, q)
        self.svf = Svf(fs, q)
        self.p_hp = EPS
        self.p_lp = EPS
        self.p_smooth = 1.0 - math.exp(-1.0 / (0.25 * speed * fs))
        self.rate = 1.0 / (speed * fs)

    def tick(self, x):
        v1, v2, _ = self.pre.tick(x, self.f)
        self.bp = self.k * v1
        self.quad = self.k * v2
        s = self.bp if self.prefilter else x
        _, lp, hp = self.svf.tick(s, self.f)
        a = self.p_smooth
        self.p_hp += a * (hp * hp - self.p_hp)
        self.p_lp += a * (lp * lp - self.p_lp)
        self.logf += self.rate * 0.25 * math.log((self.p_hp + EPS) / (self.p_lp + EPS))
        self._finish(x)


def make(kind, fs, f0, lo, hi, speed, q=4.0):
    if kind == "grad":
        return GradientNotch(fs, f0, lo, hi, speed, q)
    if kind == "ratio_raw":
        return EnergyRatio(fs, f0, lo, hi, speed, q, prefilter=False)
    if kind == "ratio_bp":
        return EnergyRatio(fs, f0, lo, hi, speed, q, prefilter=True)
    raise ValueError(kind)


KINDS = ("grad", "ratio_raw", "ratio_bp")
LABEL = {"grad": "gradient notch", "ratio_raw": "energy ratio, raw key", "ratio_bp": "energy ratio, behind a band-pass"}


# ---------------------------------------------------------------------------
# Signals
# ---------------------------------------------------------------------------
def sine(f, fs, seconds, amp=1.0, phase=0.0):
    t = np.arange(int(seconds * fs)) / fs
    return amp * np.sin(2.0 * np.pi * f * t + phase)


def white(fs, seconds, rms, seed):
    return np.random.default_rng(seed).standard_normal(int(seconds * fs)) * rms


def glide(f_from, f_to, fs, seconds, amp=1.0):
    n = int(seconds * fs)
    f = f_from * (f_to / f_from) ** (np.arange(n) / n)
    return amp * np.sin(2.0 * np.pi * np.cumsum(f) / fs), f


# ---------------------------------------------------------------------------
# Runs
# ---------------------------------------------------------------------------
def run_single(tracker, x):
    f = np.empty(len(x))
    c = np.empty(len(x))
    for n, sample in enumerate(x.tolist()):
        tracker.tick(sample)
        f[n] = tracker.f
        c[n] = tracker.confidence
    return f, c


def run_pair(trackers, x, mode, fs, q_extract=4.0):
    """Two or more trackers on one key, each adapting on the key less the others.

    mode: "none"     no deflation
          "raw"      each tracker's component is extracted from the RAW key by a
                     second SVF at its frequency and subtracted from the others' input
          "delayed"  the trackers' own band-pass outputs, one sample late
          "rotated"  the same, rotated forward by one sample with the quadrature pair
    """
    count = len(trackers)
    extract = [Svf(fs, q_extract) for _ in trackers]
    k_e = 1.0 / q_extract
    f = np.empty((count, len(x)))
    seen = np.empty((count, len(x)))
    comp = [0.0] * count

    for n, key in enumerate(x.tolist()):
        if mode == "raw":
            for j, t in enumerate(trackers):
                comp[j] = k_e * extract[j].tick(key, t.f)[0]
        elif mode == "delayed":
            for j, t in enumerate(trackers):
                comp[j] = t.bp
        elif mode == "rotated":
            for j, t in enumerate(trackers):
                theta = 2.0 * math.pi * t.f / fs
                comp[j] = math.cos(theta) * t.bp - math.sin(theta) * t.quad
        else:
            comp = [0.0] * count

        total = sum(comp)
        inputs = [key - (total - comp[i]) for i in range(count)]

        for i, t in enumerate(trackers):
            t.tick(inputs[i])
            f[i, n] = t.f
            seen[i, n] = inputs[i]

    return f, seen


def lock_metrics(f, target, fs, tolerance_cents=20.0, tail_seconds=0.5):
    """(lock time in s or None, rms error in cents over the tail, max |error| over the tail)."""
    err = 1200.0 * np.log2(f / target)
    outside = np.nonzero(np.abs(err) > tolerance_cents)[0]
    lock = 0.0 if len(outside) == 0 else (outside[-1] + 1) / fs
    if lock >= len(f) / fs - 1.0e-9:
        lock = None
    tail = err[-int(tail_seconds * fs):]
    return lock, float(np.sqrt(np.mean(tail ** 2))), float(np.max(np.abs(tail)))


def component_level_db(x, f, fs):
    """Level of the component at f in x, by projection on a quadrature pair, in dB re amplitude 1."""
    t = np.arange(len(x)) / fs
    c = 2.0 * np.mean(x * np.cos(2.0 * np.pi * f * t))
    s = 2.0 * np.mean(x * np.sin(2.0 * np.pi * f * t))
    return 20.0 * math.log10(math.hypot(c, s) + 1.0e-12)


# ---------------------------------------------------------------------------
# Part 3: the envelope
# ---------------------------------------------------------------------------
def envelope_case(f_tone, fc, q, fs, seconds=1.0):
    svf = Svf(fs, q)
    k = 1.0 / q
    x = sine(f_tone, fs, seconds)
    new = np.empty(len(x))
    old = np.empty(len(x))
    for n, sample in enumerate(x.tolist()):
        v1, v2, hp = svf.tick(sample, fc)
        new[n] = k * math.sqrt(max(0.0, v1 * v1 - v2 * hp))
        old[n] = k * math.sqrt(v1 * v1 + v2 * v2)
    tail = slice(int(0.5 * len(x)), None)
    truth = 20.0 * math.log10(bandpass_magnitude(f_tone, fc, q, fs))

    def describe(env):
        db = 20.0 * np.log10(np.maximum(env[tail], 1.0e-12))
        return float(np.mean(db)), float(np.max(db) - np.min(db))

    return truth, describe(new), describe(old)


def part_envelope(out):
    out("\n## 3. Band-pass detector envelope\n")
    out("Steady tone into one detector SVF. *Truth* is the band-pass magnitude at the tone. Level is the mean")
    out("of the envelope in dB over the second half of one second, ripple its peak-to-peak, both raw (no smoothing).\n")
    out("| fs | Detector | Tone | Truth | New form: level, ripple | First draft: level, ripple |")
    out("| --- | --- | --- | --- | --- | --- |")
    worst_new = 0.0
    for fs in (44100, 96000):
        cases = [(60.0, 60.0, 1.0), (60.0 / math.sqrt(2.0), 60.0, 1.0), (60.0 * math.sqrt(2.0), 60.0, 1.0),
                 (60.0, 60.0, 8.0), (100.0, 3000.0, 1.0), (10000.0, 3000.0, 1.0), (3000.0, 3000.0, 0.3)]
        for f_tone, fc, q in cases:
            truth, new, old = envelope_case(f_tone, fc, q, fs)
            worst_new = max(worst_new, abs(new[0] - truth), new[1])
            out(f"| {fs} | {fc:g} Hz, Q {q:g} | {f_tone:.1f} Hz | {truth:+.2f} dB | "
                f"{new[0]:+.2f} dB, {new[1]:.3f} dB | {old[0]:+.2f} dB, {old[1]:.2f} dB |")
    out(f"\nWorst departure of the new form from the truth, level or ripple: **{worst_new:.4f} dB**.")
    return worst_new


# ---------------------------------------------------------------------------
# Part 1: estimators
# ---------------------------------------------------------------------------
SPEED = 0.050


def part_estimators(out, plots):
    out("\n## 1. Estimator\n")
    out(f"Speed {SPEED * 1000:.0f} ms, tracker Q 4, range one octave either side of the tone, the tracker started")
    out("half an octave below it.\n")

    out("### 1a. Clean sine: does the loop have the time constant it claims?\n")
    out("| Estimator | Time to cover 63 % of the half octave | Residual after 1 s |")
    out("| --- | --- | --- |")
    fs, f0 = 48000, 1000.0
    for kind in KINDS:
        t = make(kind, fs, f0 / math.sqrt(2.0), f0 / 2.0, f0 * 2.0, SPEED)
        f, _ = run_single(t, sine(f0, fs, 1.0))
        err = np.log(f / f0) / math.log(1.0 / math.sqrt(2.0))
        idx = np.nonzero(err < math.exp(-1.0))[0]
        tau = idx[0] / fs if len(idx) else float("nan")
        out(f"| {LABEL[kind]} | {tau * 1000:.0f} ms | {cents(f[-1], f0):+.2f} cents |")

    out("\n### 1b. Sine in white noise at 0 dB SNR (PRD row *Track, lock*: within Speed x 5 = 250 ms, under 20 cents)\n")
    out("Lock time is the last moment the error exceeds 20 cents; *never* means it still does at the end of 3 s.")
    out("Error is over the last half second.\n")
    out("| fs | Tone | Estimator | Lock time | RMS error | Max error | Row |")
    out("| --- | --- | --- | --- | --- | --- | --- |")
    results = {}
    traces = {}
    for fs in (44100, 96000):
        for f0 in (220.0, 1000.0, 6000.0):
            x = sine(f0, fs, 3.0) + white(fs, 3.0, math.sqrt(0.5), seed=int(f0) + fs)
            for kind in KINDS:
                t = make(kind, fs, f0 / math.sqrt(2.0), f0 / 2.0, f0 * 2.0, SPEED)
                f, _ = run_single(t, x)
                lock, rms, peak = lock_metrics(f, f0, fs)
                ok = lock is not None and lock <= 5.0 * SPEED and peak < 20.0
                results[(fs, f0, kind)] = ok
                traces[(fs, f0, kind)] = f
                lock_text = "never" if lock is None else f"{lock * 1000:.0f} ms"
                out(f"| {fs} | {f0:g} Hz | {LABEL[kind]} | {lock_text} | {rms:.1f} cents | {peak:.1f} cents | "
                    f"{'pass' if ok else 'FAIL'} |")

    out("\n### 1c. The same at gentler noise, for scale (1 kHz, 48 kHz)\n")
    out("| SNR | Estimator | Lock time | RMS error | Max error |")
    out("| --- | --- | --- | --- | --- |")
    fs, f0 = 48000, 1000.0
    for snr in (20.0, 10.0, 0.0, -6.0):
        x = sine(f0, fs, 3.0) + white(fs, 3.0, math.sqrt(0.5) * 10.0 ** (-snr / 20.0), seed=77)
        for kind in KINDS:
            t = make(kind, fs, f0 / math.sqrt(2.0), f0 / 2.0, f0 * 2.0, SPEED)
            f, _ = run_single(t, x)
            lock, rms, peak = lock_metrics(f, f0, fs)
            lock_text = "never" if lock is None else f"{lock * 1000:.0f} ms"
            out(f"| {snr:+.0f} dB | {LABEL[kind]} | {lock_text} | {rms:.1f} cents | {peak:.1f} cents |")

    out("\n### 1d. Speed against jitter, gradient notch and band-passed ratio (1 kHz, 48 kHz, 0 dB SNR)\n")
    out("| Speed | Estimator | Lock time | RMS error | Max error |")
    out("| --- | --- | --- | --- | --- |")
    x = sine(f0, fs, 4.0) + white(fs, 4.0, math.sqrt(0.5), seed=78)
    for speed in (0.010, 0.050, 0.200):
        for kind in ("grad", "ratio_bp"):
            t = make(kind, fs, f0 / math.sqrt(2.0), f0 / 2.0, f0 * 2.0, speed)
            f, _ = run_single(t, x)
            lock, rms, peak = lock_metrics(f, f0, fs)
            lock_text = "never" if lock is None else f"{lock * 1000:.0f} ms"
            out(f"| {speed * 1000:.0f} ms | {LABEL[kind]} | {lock_text} | {rms:.1f} cents | {peak:.1f} cents |")

    out("\n### 1e. Tracker Q, gradient notch and band-passed ratio (1 kHz, 48 kHz, 0 dB SNR, Speed 50 ms)\n")
    out("| Q | Estimator | Lock time | RMS error | Max error |")
    out("| --- | --- | --- | --- | --- |")
    for q in (1.0, 2.0, 4.0, 8.0, 16.0):
        for kind in ("grad", "ratio_bp"):
            t = make(kind, fs, f0 / math.sqrt(2.0), f0 / 2.0, f0 * 2.0, SPEED, q)
            f, _ = run_single(t, x)
            lock, rms, peak = lock_metrics(f, f0, fs)
            lock_text = "never" if lock is None else f"{lock * 1000:.0f} ms"
            out(f"| {q:g} | {LABEL[kind]} | {lock_text} | {rms:.1f} cents | {peak:.1f} cents |")

    out("\n### 1f. Sine removed (PRD row *Track, loss*): how fast confidence says so\n")
    out("A 1 kHz sine over noise 40 dB below it, removed at 1 s. Confidence is 1 - notch power / input power,")
    out("the two powers smoothed over the time given. Drift is where the estimate has wandered half a second")
    out("after the removal, with nothing holding it.\n")
    out("| Powers smoothed over | Estimator | Confidence while locked | Time to fall under 0.5 | Drift 0.5 s later |")
    out("| --- | --- | --- | --- | --- |")
    n1 = fs
    x = np.concatenate([sine(f0, fs, 1.0), np.zeros(fs)]) + white(fs, 2.0, 0.01 * math.sqrt(0.5), seed=5)
    for seconds, label in ((None, "Speed (50 ms)"), (4.0 / f0, "4 periods (4 ms)")):
        TrackerBase.confidence_seconds = seconds
        for kind in ("grad", "ratio_bp"):
            t = make(kind, fs, f0, f0 / 2.0, f0 * 2.0, SPEED)
            f, c = run_single(t, x)
            below = np.nonzero(c[n1:] < 0.5)[0]
            fall = "never" if len(below) == 0 else f"{below[0] / fs * 1000:.0f} ms"
            out(f"| {label} | {LABEL[kind]} | {c[n1 - 1]:.3f} | {fall} | {cents(f[n1 + fs // 2], f0):+.0f} cents |")
    TrackerBase.confidence_seconds = None

    out("\n### 1g. Confidence on a sine in noise, and on noise alone (1 kHz, 48 kHz, Q 4)\n")
    out("What the Confidence threshold has to separate. Mean over the last second of three.\n")
    out("| Key | gradient notch | energy ratio, behind a band-pass |")
    out("| --- | --- | --- |")
    for label, x in (("sine, no noise", sine(f0, fs, 3.0)),
                     ("sine in noise at +10 dB SNR", sine(f0, fs, 3.0) + white(fs, 3.0, math.sqrt(0.05), 9)),
                     ("sine in noise at 0 dB SNR", sine(f0, fs, 3.0) + white(fs, 3.0, math.sqrt(0.5), 9)),
                     ("white noise alone", white(fs, 3.0, math.sqrt(0.5), 9))):
        cells = []
        for kind in ("grad", "ratio_bp"):
            _, c = run_single(make(kind, fs, f0, f0 / 2.0, f0 * 2.0, SPEED), x)
            cells.append(f"{float(np.mean(c[-fs:])):.3f}")
        out(f"| {label} | {cells[0]} | {cells[1]} |")

    if plots:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        fig, axes = plt.subplots(2, 3, figsize=(15, 7), sharex=True)
        for row, fs in enumerate((44100, 96000)):
            for col, f0 in enumerate((220.0, 1000.0, 6000.0)):
                ax = axes[row][col]
                for kind in KINDS:
                    f = traces[(fs, f0, kind)]
                    ax.plot(np.arange(len(f)) / fs, 1200.0 * np.log2(f / f0), label=LABEL[kind], linewidth=0.8)
                ax.axhspan(-20, 20, color="0.85", zorder=0)
                ax.axvline(5.0 * SPEED, color="0.4", linestyle=":", linewidth=0.8)
                ax.set_ylim(-700, 700)
                ax.set_title(f"{f0:g} Hz in noise at 0 dB SNR, fs {fs}")
                ax.set_ylabel("error, cents")
                if row == 1:
                    ax.set_xlabel("seconds")
        axes[0][0].legend(loc="lower right", fontsize=8)
        fig.suptitle("Lock from half an octave below (grey band: 20 cents; dotted: Speed x 5)")
        fig.tight_layout()
        fig.savefig(f"{plots}/lock.png", dpi=110)
        plt.close(fig)

    return results


# ---------------------------------------------------------------------------
# Part 2: deflation
# ---------------------------------------------------------------------------
MODES = ("none", "raw", "delayed", "rotated")
MODE_LABEL = {"none": "no deflation", "raw": "from the raw key", "delayed": "own output, one sample late",
              "rotated": "own output, rotated forward"}


def part_deflation(out, plots):
    out("\n## 2. Deflation between two Track bands\n")

    out("### 2a. How much of the other band's sine is left at a tracker's input\n")
    out("Two equal sines a fifth apart, two trackers held on them (Speed 50 ms, Q 4). The level of sine B in")
    out("tracker A's input, relative to its level in the key, over the last half second. 0 dB is no deflation;")
    out("above 0 dB the subtraction adds.\n")
    out("| fs | Pair | " + " | ".join(MODE_LABEL[m] for m in MODES[1:]) + " |")
    out("| --- | --- | --- | --- | --- |")
    for fs in (44100, 96000):
        for fa in (220.0, 1000.0, 6000.0):
            fb = fa * 1.5
            x = sine(fa, fs, 1.5) + sine(fb, fs, 1.5, phase=1.0)
            cells = []
            for mode in MODES[1:]:
                pair = [make("grad", fs, fa, fa / 1.2, fa * 1.2, SPEED), make("grad", fs, fb, fb / 1.2, fb * 1.2, SPEED)]
                _, seen = run_pair(pair, x, mode, fs)
                tail = seen[0][-fs // 2:]
                cells.append(f"{component_level_db(tail, fb, fs):+.1f} dB")
            out(f"| {fs} | {fa:g} and {fb:g} Hz | " + " | ".join(cells) + " |")

    out("\n### 2b. Levels traded by 24 dB (PRD row *Track, no swap*: neither band moves by more than 50 cents)\n")
    out("Both sines at equal level for 1 s, so that both trackers are locked; then over 1 s sine A rises to +12 dB")
    out("and sine B falls to -12 dB; then over 3 s they trade places; then 0.5 s held. Each tracker's range is one")
    out("octave either side of its own sine, so each can reach the other. The figure is the larger of the two")
    out("trackers' worst departures from their own sine. Q is the tracker's and the extraction filter's.\n")
    out("| fs | Pair | Estimator | Q | " + " | ".join(MODE_LABEL[m] for m in MODES) + " |")
    out("| --- | --- | --- | --- | --- | --- | --- | --- |")
    results = {}
    traces = {}

    def trade(fa, fb, fs, cold):
        if cold:
            offset = np.concatenate([np.full(int(0.5 * fs), 12.0), np.linspace(12.0, -12.0, int(3.0 * fs))])
        else:
            offset = np.concatenate([np.zeros(fs), np.linspace(0.0, 12.0, fs),
                                     np.linspace(12.0, -12.0, int(3.0 * fs)), np.full(int(0.5 * fs), -12.0)])
        seconds = len(offset) / fs
        return 0.25 * (10.0 ** (offset / 20.0) * sine(fa, fs, seconds)[: len(offset)]
                       + 10.0 ** (-offset / 20.0) * sine(fb, fs, seconds, phase=1.0)[: len(offset)])

    def worst_departure(kind, fa, fb, fs, mode, q, x):
        pair = [make(kind, fs, fa, fa / 2.0, fa * 2.0, SPEED, q), make(kind, fs, fb, fb / 2.0, fb * 2.0, SPEED, q)]
        f, _ = run_pair(pair, x, mode, fs, q_extract=q)
        return max(float(np.max(np.abs(1200.0 * np.log2(f[0] / fa)))),
                   float(np.max(np.abs(1200.0 * np.log2(f[1] / fb))))), f

    for fs in (44100, 96000):
        for fa in (220.0, 6000.0):
            fb = fa * 1.5
            x = trade(fa, fb, fs, cold=False)
            for kind in ("grad", "ratio_bp"):
                for q in (4.0, 8.0):
                    cells = []
                    for mode in MODES:
                        worst, f = worst_departure(kind, fa, fb, fs, mode, q, x)
                        results[(fs, fa, kind, mode, q)] = worst
                        if q == 4.0:
                            traces[(fs, fa, kind, mode)] = f
                        cells.append(f"{worst:.0f} cents{'' if worst <= 50.0 else ' FAIL'}")
                    out(f"| {fs} | {fa:g} and {fb:g} Hz | {LABEL[kind]} | {q:g} | " + " | ".join(cells) + " |")

    out("\n### 2b'. The same from a cold start at the extreme (information)\n")
    out("Filters empty, sine A already 24 dB above sine B, trackers placed on their sines; 0.5 s, then the trade.")
    out("This is what a loud component appearing beside a quiet tracked one looks like to the quiet one's tracker.\n")
    out("| fs | Pair | Estimator | Q | " + " | ".join(MODE_LABEL[m] for m in MODES) + " |")
    out("| --- | --- | --- | --- | --- | --- | --- | --- |")
    for fs in (44100,):
        for fa in (220.0, 1000.0, 6000.0):
            fb = fa * 1.5
            x = trade(fa, fb, fs, cold=True)
            for kind in ("grad", "ratio_bp"):
                for q in (4.0, 8.0):
                    cells = []
                    for mode in MODES:
                        worst, _ = worst_departure(kind, fa, fb, fs, mode, q, x)
                        cells.append(f"{worst:.0f} cents{'' if worst <= 50.0 else ' FAIL'}")
                    out(f"| {fs} | {fa:g} and {fb:g} Hz | {LABEL[kind]} | {q:g} | " + " | ".join(cells) + " |")

    out("\n### 2b''. Speed against the wobble of a close low pair (220 and 330 Hz, 44.1 kHz, gradient notch, Q 4)\n")
    out("The settled protocol of 2b. The two sines beat at 110 Hz, which a fast loop follows.\n")
    out("| Speed | from the raw key | own output, rotated forward |")
    out("| --- | --- | --- |")
    fs, fa, fb = 44100, 220.0, 330.0
    x = trade(fa, fb, fs, cold=False)
    for speed in (0.020, 0.050, 0.100, 0.200):
        cells = []
        for mode in ("raw", "rotated"):
            pair = [make("grad", fs, fa, fa / 2.0, fa * 2.0, speed), make("grad", fs, fb, fb / 2.0, fb * 2.0, speed)]
            f, _ = run_pair(pair, x, mode, fs)
            worst = max(float(np.max(np.abs(1200.0 * np.log2(f[0] / fa)))),
                        float(np.max(np.abs(1200.0 * np.log2(f[1] / fb)))))
            cells.append(f"{worst:.0f} cents{'' if worst <= 50.0 else ' FAIL'}")
        out(f"| {speed * 1000:.0f} ms | {cells[0]} | {cells[1]} |")

    out("\n### 2d. Two trackers put on the same sine (what exclusion must survive at a crossing or a bad seed)\n")
    out("One 1 kHz sine of amplitude 1 with noise 40 dB below, 48 kHz, both trackers started on it, 2 s.")
    out("The largest sample either tracker was given, and where the two ended.\n")
    out("| Deflation | Largest tracker input | Tracker 1 ends at | Tracker 2 ends at |")
    out("| --- | --- | --- | --- |")
    fs = 48000
    x = sine(1000.0, fs, 2.0) + white(fs, 2.0, 0.01 * math.sqrt(0.5), seed=21)
    for mode in MODES:
        pair = [make("grad", fs, 1000.0, 500.0, 2000.0, SPEED), make("grad", fs, 1000.0, 500.0, 2000.0, SPEED)]
        f, seen = run_pair(pair, x, mode, fs)
        out(f"| {MODE_LABEL[mode]} | {float(np.max(np.abs(seen))):.2f} | {f[0][-1]:.0f} Hz | {f[1][-1]:.0f} Hz |")

    out("\n### 2c. Two glides that cross (information: the PRD accepts an exchange at the crossing)\n")
    out("300 to 600 Hz and 600 to 300 Hz over 3 s, equal levels, 48 kHz, gradient notch, range 200 to 900 Hz.")
    out("Whether each tracker ends on the glide it started on, and its error there.\n")
    out("| Deflation | Tracker 1 ends on | Tracker 2 ends on |")
    out("| --- | --- | --- |")
    fs = 48000
    up, f_up = glide(300.0, 600.0, fs, 3.0)
    down, f_down = glide(600.0, 300.0, fs, 3.0)
    x = 0.5 * (up + down)
    cross = {}
    for mode in MODES:
        pair = [make("grad", fs, 300.0, 200.0, 900.0, SPEED), make("grad", fs, 600.0, 200.0, 900.0, SPEED)]
        f, _ = run_pair(pair, x, mode, fs)
        cross[mode] = f
        ends = []
        for i in range(2):
            e_up, e_down = cents(f[i][-1], f_up[-1]), cents(f[i][-1], f_down[-1])
            ends.append(f"the rising glide ({e_up:+.0f} cents)" if abs(e_up) < abs(e_down)
                        else f"the falling glide ({e_down:+.0f} cents)")
        out(f"| {MODE_LABEL[mode]} | {ends[0]} | {ends[1]} |")

    if plots:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        fig, axes = plt.subplots(2, 4, figsize=(18, 7), sharey="row")
        for row, (fs, fa) in enumerate(((44100, 220.0), (44100, 6000.0))):
            for col, mode in enumerate(MODES):
                ax = axes[row][col]
                f = traces[(fs, fa, "grad", mode)]
                t = np.arange(f.shape[1]) / fs
                ax.plot(t, 1200.0 * np.log2(f[0] / fa), label="tracker A, cents from A", linewidth=0.8)
                ax.plot(t, 1200.0 * np.log2(f[1] / fa), label="tracker B, cents from A", linewidth=0.8)
                ax.axhline(0.0, color="0.6", linewidth=0.5)
                ax.axhline(702.0, color="0.6", linewidth=0.5)
                ax.set_title(f"{fa:g} and {fa * 1.5:g} Hz, fs {fs}: {MODE_LABEL[mode]}", fontsize=9)
                ax.set_xlabel("seconds")
        axes[0][0].set_ylabel("cents above sine A")
        axes[1][0].set_ylabel("cents above sine A")
        axes[0][0].legend(fontsize=8)
        fig.suptitle("Levels traded by 24 dB, gradient notch (a tracker that leaves its line has swapped or drifted)")
        fig.tight_layout()
        fig.savefig(f"{plots}/no-swap.png", dpi=110)
        plt.close(fig)

        fig, axes = plt.subplots(1, 4, figsize=(18, 4), sharey=True)
        t = np.arange(len(x)) / fs
        for col, mode in enumerate(MODES):
            ax = axes[col]
            ax.plot(t, f_up, color="0.7", linewidth=2)
            ax.plot(t, f_down, color="0.7", linewidth=2)
            ax.plot(t, cross[mode][0], linewidth=0.9, label="tracker 1")
            ax.plot(t, cross[mode][1], linewidth=0.9, label="tracker 2")
            ax.set_yscale("log")
            ax.set_title(MODE_LABEL[mode], fontsize=9)
            ax.set_xlabel("seconds")
        axes[0].set_ylabel("Hz")
        axes[0].legend(fontsize=8)
        fig.suptitle("Two glides that cross (grey: the glides)")
        fig.tight_layout()
        fig.savefig(f"{plots}/crossing.png", dpi=110)
        plt.close(fig)

    return results


# ---------------------------------------------------------------------------
# Recordings, when there are some
# ---------------------------------------------------------------------------
def read_wav(path):
    with wave.open(path, "rb") as w:
        fs, channels, width, frames = w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes()
        raw = w.readframes(frames)
    if width == 2:
        data = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    elif width == 3:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        data = ((b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)) << 8 >> 8).astype(np.float64) / 8388608.0
    elif width == 4:
        data = np.frombuffer(raw, dtype="<i4").astype(np.float64) / 2147483648.0
    else:
        raise ValueError(f"{path}: {8 * width}-bit PCM is not read here")
    return data.reshape(-1, channels).mean(axis=1), fs


def part_recordings(out, plots, paths, lo, hi):
    out("\n## 4. Recordings\n")
    if not paths:
        out("None yet. Run again with `--wav file.wav ... --range LOW HIGH` when the author's recordings arrive.")
        return
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    for path in paths:
        x, fs = read_wav(path)
        x = x[: int(10 * fs)]
        start = math.sqrt(lo * hi)
        fig, ax = plt.subplots(figsize=(12, 5))
        ax.specgram(x, NFFT=2048, Fs=fs, noverlap=1536, cmap="Greys")
        for kind in KINDS:
            f, _ = run_single(make(kind, fs, start, lo, hi, SPEED), x)
            ax.plot(np.arange(len(f)) / fs, f, linewidth=0.9, label=LABEL[kind])
        ax.set_ylim(lo / 2.0, hi * 2.0)
        ax.set_yscale("log")
        ax.legend(fontsize=8)
        name = path.replace("\\", "/").split("/")[-1]
        ax.set_title(f"{name}: one tracker in {lo:g} to {hi:g} Hz")
        fig.tight_layout()
        target = f"{plots}/recording-{name.rsplit('.', 1)[0]}.png"
        fig.savefig(target, dpi=110)
        plt.close(fig)
        out(f"- `{name}`: see `{target}`.")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--plots", help="directory to write the PNG plots into")
    parser.add_argument("--wav", nargs="*", default=[], help="recordings to run a tracker over")
    parser.add_argument("--range", nargs=2, type=float, default=[1000.0, 10000.0], metavar=("LOW", "HIGH"))
    parser.add_argument("--only", choices=("envelope", "estimators", "deflation", "recordings"))
    args = parser.parse_args()
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    def out(line=""):
        print(line, flush=True)

    if args.only in (None, "estimators"):
        part_estimators(out, args.plots)
    if args.only in (None, "deflation"):
        part_deflation(out, args.plots)
    if args.only in (None, "envelope"):
        part_envelope(out)
    if args.only in (None, "recordings"):
        part_recordings(out, args.plots, args.wav, args.range[0], args.range[1])


if __name__ == "__main__":
    main()
