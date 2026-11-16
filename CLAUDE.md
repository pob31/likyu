# CLAUDE.md

The two sections below are **PRD *Goals and non-goals* and *Band model: two axes, one
detector*, reproduced verbatim** from the PRD in `docs/`. They are the review criterion for every
pull request.

**If this file and the PRD disagree, the PRD wins and this file is stale. Fix it by copying,
never by editing.** `scripts/check-claude-md.py` compares them in CI.

---

## Goals and non-goals

**Goals**

1. Eight bands in series on one filter structure (TPT state-variable, gain applied as a mix), so that a bell or shelf band at 0 dB is an exact bypass: it nulls against the dry signal.
2. Per band, two independent axes: gain mode (Static, Dynamic, Transient with attack and sustain) and frequency mode (Fixed, Env, Track).
3. Per band, detection decoupled from the band: its own detector filter, and its key taken from the plugin input or the sidechain bus. Detection never reads the signal between stages.
4. Track bands follow one spectral peak each inside a user range, exclude each other's peaks, and keep their identity when peaks trade levels: no frequency swap.
5. Latency constant per instance: zero by default, one global lookahead set once and reported once, never changed by a band's mode or by automation.
6. VST3, AU and LV2 from one core; CLAP later through the project's own shim (decided 2026-11-16).
7. A DSP core with no JUCE dependency, usable outside the plugin and tested headless.
8. Open source under GPLv3, in the pob31 repository (*decided 2026-11-16*).

**Non-goals**

- No linear phase, no FFT or spectral processing in the audio path, no multi-resonance suppression in the manner of Soothe. One tracked peak per band.
- No latency that varies while audio runs.
- No band count that changes inside a session. The count is a build constant, 8 in v1 (decided 2026-11-16).
- No gain dynamics on high-pass, low-pass, notch or band-pass bands: they have no gain to modulate. Their frequency modes do apply.
- No slopes steeper than 12 dB per octave in one band; stack bands for more.
- No build-time channel count on VST3 and AU: they accept any layout whose main output matches its main input, up to 64 channels, chosen by the host. LV2, whose ports are static, ships as mono and stereo variants (decided 2026-11-16).
- No per-band lookahead, no parallel or mid-side routing, no oversampling in v1.

---

## Band model: two axes, one detector

Each instance owns **8 bands**, numbered 1 to 8, processed in series in that order. A band is one audio filter, one detector, and two mode choices.

| Band field | Values | Notes |
| --- | --- | --- |
| Type | Bell, Low shelf, High shelf, High-pass, Low-pass, Notch, Band-pass | One 12 dB/oct TPT SVF |
| Frequency, Q, Gain | 20 Hz to 20 kHz, 0.1 to 40, −30 to +30 dB | Frequency is the rest value in Env and Track |
| Q mode | Proportional (default), Constant | Bell only |
| Gain mode | Static, Dynamic, Transient | Bell and shelves only; other types are Static |
| Frequency mode | Fixed, Env, Track | All types |
| Key source | Self (plugin input), Sidechain | Per band |
| Detector filter | Follow band, Own (type, frequency, Q) | Own types: band-pass, high-pass, low-pass |
| Range | −30 to +30 dB | Limit of the dynamic gain offset, and its sign in Dynamic mode |

**Rules**

- Parameter IDs are strings of the form `b<n>_<param>` (`b3_freq`), frozen for good, never renamed, never positional. Underscore, not dot: LV2 symbols allow `[_a-zA-Z][_a-zA-Z0-9]*` only. Raising the band count later adds IDs and renumbers none.
- The host sees every parameter in real units with its own range (Hz, dB, ms), unlike the OSCQuery plugin's normalised slots.
- The effective gain of a band is `static gain + dynamic offset`, in dB, the offset clamped to the Range and the sum clamped to −30 to +30 dB (decided 2026-11-16). Static mode: offset 0.
- All detection reads the plugin input (Self) or the sidechain bus, both undelayed. No detector reads another band's output.
- Sidechain selected but the sidechain bus absent or disabled: the band falls back to Self and the editor says so. A bus that is enabled but silent cannot be told from a quiet key and is treated as a key.
- Track bands that share a key source exclude each other automatically. There is no user-defined set.
- Mode switches, type switches and band on/off are click-free (short crossfade of the mix terms) and never change latency.
- Lookahead is a per-instance choice, not automatable, and is the only thing that changes reported latency.

---

## Where to look

- **`docs/PRD — Likyū, Dynamic EQ and Transient Designer Plugin (VST3 AU LV2).md`** — the product
  requirements; the source of the two sections above and of every decision, each dated in place.
  Its *Tests and acceptance* table is what "done" means for a feature.
- **`docs/Devplan — Likyū (draft 0.1).md`** — the phase plan: what is built now, in what order,
  what each phase needs, and the facts checked on the way. Read *How to read this* first.
- **`docs/spikes/`**, **`docs/bench/`**, **`docs/handoffs/`** — what each spike concluded, what was
  measured, and where each phase left off.

## Standing rules, decided 2026-11-16

- **C++20 on JUCE 9.0.5**, pinned at the same commit as WFS-DIY, Go.dot and the OSCQuery
  Automation Plugin. VST3, AU and LV2 through JUCE's own wrappers; CLAP later through a shim of
  our own. Neither `clap-juce-extensions` nor `clap-wrapper` enters the build.
- **`lib/dsp` includes the C++ standard library and nothing else.** No JUCE header, no
  allocation after `prepare`. `scripts/check-dsp-includes.py` holds it to that. "The DSP core"
  means `lib/dsp`; `likyu::core` (`src/core/`) is named in full.
- **The audio thread lipogram:** no allocation, lock, exception, syscall or logging between
  `prepare` and `release`. A violation is a number that goes up (`dsp/rt/RtCheck.h`), never a log
  line.
- **GPLv3.** File headers follow go.dot's form and read `Copyright (C) 2026 Pierre-Olivier
  Boulant`. A file copied from a sibling repository keeps its original header and gains an
  `Origin:` line. `scripts/check-headers.py` checks both.
- **Plugin identity, fixed before the first session is saved against it:** company "Pix et Bel",
  manufacturer code `PxBl`, plugin code `Lkyu`, `JUCE_VST3_CAN_REPLACE_VST2=0`, LV2 URIs
  `https://pixetbel.org/lv2/likyu/stereo` and `…/mono`, bundle ID `io.github.pob31.likyu`.
  **Likyu**, in ASCII, for everything a host or a file system reads; **Likyū** in the editor and
  the documentation; `likyu` in identifiers. Changing any of these orphans saved sessions.
- **Parameter IDs** are `b<n>_<suffix>`, declared once in `lib/dsp/dsp/Parameters.h`, and spelled
  nowhere else in `src/`. They are frozen by the author at the end of Phase 5; until then a build
  is not for saving sessions.
- **Anything under the PRD's *Open decisions* is not decided by the implementer.** Surface it,
  propose, wait.
- **Pull requests with CI green for code.** A change that touches only documentation (`docs/`,
  `CLAUDE.md`, READMEs) or only tests (`tests/`) may go straight to `main`. Never rewrite history
  on `main`.

## Code to reuse, and where it is

Sibling repositories on the author's machine, all GPLv3, all on JUCE 9.0.5:

- **The OSCQuery Automation Plugin** (`../OSCQuery-Automation-plugin`): the template for the
  build, CI, check scripts, test harness and realtime instrumentation, copied at `1d8caa8` and
  cut down. Later: `src/shims/clap` for our own CLAP shim, and `SlotPresentation.h` as the
  pattern for the editor's presentation functions (pure functions over state, tested headless).
- **WFS-DIY** (`../WFS_DIY_v1-juce905`): `tools/validation/pipeline-bench` for the shape of the
  bench runner (median, p99, p999, max per block); `Plugin/Installer` and `Plugin/Scripts` for
  the installers and signing (Phase 7).
- **Go.dot** (`../go.dot`): the first host. It feeds silence to a plugin's sidechain bus today
  (`src/wfg/engine/plugin/LaneMapping.h`); routing to it is Go.dot work, devplan Phase 9.

The DSP itself is written here. No code is taken from another EQ.

## The bench, as of 2026-11-16

- **Building on this Windows machine:** `cmake` is not on `PATH`. Visual Studio's is at
  `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`;
  with it on `PATH`, `cmake --preset vs`, `cmake --build --preset vs-debug`, `ctest --preset vs-debug`.
- **Hosts on this Windows machine**, as the OSCQuery plugin's `CLAUDE.md` inventoried them:
  REAPER and Samplitude (VST3), Ableton Live 12, Bitwig Studio 6.
- **The farm**, over SSH when the author is at home: `ssh macmini` (Apple silicon, Xcode, REAPER,
  Live, Bitwig, `auval`; export `/opt/homebrew/bin`) and `ssh nuc` (Ubuntu 24.04, GCC 13, REAPER
  at `/opt/REAPER`). `scripts/farm-check.sh` runs the CI job's checks there on a `git bundle`
  clone.
