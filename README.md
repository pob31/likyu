# Likyū

A dynamic EQ and transient designer plugin: eight parametric bands, each of which chooses
independently how its **gain** behaves (Static, Dynamic, Transient) and how its **frequency**
behaves (Fixed, Env, Track). VST3, AU and LV2 from one codebase.

- **Dynamic:** a band cuts or boosts as its key gets louder, or quieter. A de-esser, a de-boomer,
  or a band of the music that opens room while someone speaks.
- **Transient:** a band shapes attack and sustain without a threshold. Soften a snare's attack in
  its own band and leave the rest alone.
- **Env:** a band's frequency follows the key's level. An envelope filter.
- **Track:** a band follows one spectral peak inside a range you set. Several Track bands keep to
  different peaks and do not swap when the peaks trade levels.
- **Each band has its own detector**, with its own filter, keyed from the plugin's input or from
  a sidechain.
- **A band at 0 dB is an exact bypass**: it nulls against the dry signal.
- **Latency is zero by default** and constant: one optional lookahead for the whole instance,
  never changed by what the bands do.

The name is a play on *l'EQ*.

**Status:** Phase 0 of the plan. The repository builds a plugin that passes audio and does no EQ
yet: VST3 and LV2 on Windows, macOS and Linux, AU on macOS. It already shows a host what the
finished plugin will (the 284 parameters, a sidechain input, a lookahead that reports latency), so
that host behaviour can be measured before the DSP is written. Do not save sessions with it:
parameter IDs are not frozen.

- [PRD](docs/PRD%20—%20Likyū,%20Dynamic%20EQ%20and%20Transient%20Designer%20Plugin%20(VST3%20AU%20LV2).md):
  what the plugin does, how the DSP works, and how each feature is accepted.
- [Development plan, draft 0.1](docs/Devplan%20—%20Likyū%20(draft%200.1).md): the phases, what each
  delivers and what it needs.
- [`CLAUDE.md`](CLAUDE.md): the review criterion for every change, and where to look.
- [Tracker spike](docs/spikes/phase0-tracker.md): how a band follows a frequency, measured on
  synthetic signals.

## What it is not

No linear phase, no FFT in the audio path, no multi-resonance suppression: one tracked peak per
band. No slopes steeper than 12 dB per octave in one band; stack bands for more. No per-band
lookahead, no mid-side, no oversampling in the first version.

## What it needs

- **On Windows and Linux, an x86-64 processor with AVX2 and FMA**: Intel from Haswell (2013), AMD
  from Excavator and Zen. Some Pentium, Celeron and Atom models of later years have neither. The
  plugin built for such a processor is `-DLIKYU_SIMD=scalar` (below); it computes the same things
  with no fused multiply-add, so its output differs from the others' in its last bits.
- **On macOS, Apple silicon or Intel**, macOS 11 or later.
- One instance of eight bands in Dynamic and Track, stereo, at 96 kHz, takes about 3 % of one core
  of a recent laptop processor; the figures per case and per machine are in `docs/bench/`.

## Building

CMake 3.22 or later, a C++20 compiler, and the submodules:

```
git clone --recurse-submodules https://github.com/pob31/likyu.git
```

| Where | Configure, build, test |
| --- | --- |
| Windows, any shell | `cmake --preset vs`, `cmake --build --preset vs-debug`, `ctest --preset vs-debug` |
| macOS, Linux (Ninja) | `cmake --preset dev`, `cmake --build --preset dev-debug`, `ctest --preset dev-debug` |

The plugins land in `build/<preset>/src/shims/juce/likyu_plugin_artefacts/<config>/` (VST3, AU and
the stereo LV2) and `likyu_plugin_lv2_mono_artefacts/<config>/` (the mono LV2). On Linux,
`.github/workflows/ci.yml` lists the packages JUCE needs.

Part of the suite runs twice, under `C` and under the French locale, and fails if the French
locale is missing (`sudo locale-gen fr_FR.UTF-8` on Debian and Ubuntu).

The DSP is a library of its own, `lib/dsp`, with no JUCE dependency, so that it can be tested
headless and linked outside the plugin. Its suite is the `likyu_dsp_tests` target and runs in
well under a second.

`LIKYU_SIMD` chooses what its kernels are written in: `auto` (the default: AVX2 with FMA on x86-64,
NEON on arm64), `reference` (the same kernels on plain arrays, in the same arithmetic, which CI
holds the others to bit for bit with `scripts/same-bits.sh`) or `scalar` (for an x86-64 without
AVX2). The `reference` and `scalar` presets configure them without the plugins.

Before pushing: `python3 scripts/check-claude-md.py`, `check-pins.py`, `check-headers.py` and
`check-dsp-includes.py`, as CI's first job runs them.

## Licence

GPLv3. Copyright (C) 2026 Pierre-Olivier Boulant. See [LICENSE](LICENSE).
