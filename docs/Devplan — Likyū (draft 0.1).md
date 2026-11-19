# Likyū — Development Plan (suggestion for Claude Code)

**Draft 0.1**, 2026-11-16, derived from PRD draft 0.2 of the same date. A proposed phase order, not
a schedule. Each phase ends with something that runs in a host and a test that stays in CI. Phases
will be broken into subphases as they go.

**Decided 2026-11-16, by the author** (all recorded in the PRD's *Decided so far*): eight bands
with two independent axes; detection from the plugin input or the sidechain only; JUCE 9.0.5 for
the shell and no JUCE in `lib/dsp`; VST3, AU and LV2 first, CLAP later through our own shim;
GPLv3; a linked level is the maximum over the key's channels; Likyu for hosts and files, Likyū in
the editor and the documentation; sidechain routing in Go.dot is Go.dot work, done after the rest
of that app is thoroughly tested.

---

## How to read this

- **Read the PRD first.** Its *Goals and non-goals* and *Band model: two axes, one detector*
  sections are the review criterion for every change. A `CLAUDE.md` holding those two sections
  verbatim is Phase 0 work.
- **DSP before core, core before shim, editor last.** Nothing reaches the plugin before `lib/dsp`
  has it and a headless test drives it. `lib/dsp` includes the C++ standard library and nothing
  else. Phases 2 to 5 are listened to through a host's generic parameter view.
- **A feature is done when its row in the PRD's *Tests and acceptance* table passes in CI on three
  platforms.** Each phase below names its rows. A threshold the PRD marks *proposed* is confirmed
  or replaced by the first measurement, and the PRD is amended with the date.
- **Anything the PRD lists under *Open decisions* is not decided by the implementer.** Surface it,
  propose, wait. Those that block a phase are repeated under that phase's *Needs from the author*.
- **Spikes are throwaway.** They live in `spikes/`, never migrate into `lib/` or `src/`, and each
  ends with a written pass or fail in `docs/spikes/`.
- **Listening is part of acceptance** for Phases 3 to 5. The automated rows say the maths is
  right; only the author's ears say the time constants are. Notes go in `docs/bench/`.
- **Small commits per subphase, straight to `main`** (decided 2026-11-16, by the author: no pull
  requests for now). One concern per commit; the test is part of it; CI is watched after the push.

Sizes are relative: **S** days, **M** weeks, **L** several weeks. No dates.

---

## Facts checked on 2026-11-16 that the PRD left open

These change the build, not the design. Each is re-checked at the phase that depends on it.

| Fact | Where checked | Consequence |
| --- | --- | --- |
| The template pins JUCE at `d60b7a78`, the 9.0.5 tag, and doctest as its own submodule | `ThirdParty/` in the OSCQuery plugin's checkout | Phase 0 pins the same commits. `clap` and `clap-helpers` are not added until the CLAP shim is. |
| The template's CI has four jobs (`pins`, Linux, macOS, Windows), runs pluginval 1.0.4 at **strictness 5**, builds VST3 and AU only, and has **no RTSan job**; `rt/RtCheck.h` mentions `[[clang::nonblocking]]` but nothing compiles with the realtime sanitizer | `.github/workflows/ci.yml`, `src/core/rt/RtCheck.h` there | Three things the PRD calls "copied" are new work in Phase 0: strictness 10, an LV2 build with its validation, and the RTSan job. |
| JUCE 9.0.5's LV2 wrapper writes the audio ports once, from the processor's default bus layout, and enables every bus at instantiation. A bus that is not enabled by default gets `lv2:connectionOptional`. It writes no `lv2:isSideChain` | `juce_audio_plugin_client_LV2.cpp` at the pin | One LV2 plugin per target, as the PRD expected: mono and stereo are two targets. An LV2 host has no property telling it which input is the sidechain; whether an unconnected optional port can be told from silence is a Phase 1 row. |
| The same wrapper exposes parameters as patch properties, not control ports. Each IRI is the plugin URI plus the parameter ID, and the ID must survive URL escaping and Turtle sanitising unchanged | same file, `ParameterStorage::getIri` | `b3_freq` passes. The PRD's reason for the underscore (LV2 symbols) is not the mechanism JUCE uses, but the rule stands and is now tested: Phase 0 asserts every ID in the table is unchanged by both sanitisers. |
| Go.dot hosts a plugin's main bus and **feeds silence to every other input**, a sidechain above all; its own engine plugins answer `canSidechain()` false | `src/wfg/engine/plugin/LaneMapping.h` and the schema text, in the `go.dot-juce9` checkout of 2026-11-02 | The sidechain bus is enabled and silent in Go.dot, so a Sidechain band there is keyed by silence and does **not** fall back to Self. The PRD's Phase 9 row said otherwise and is amended. Until Phase 9 the editor must say so in Go.dot (Phase 8). |
| WFS-DIY has the bench runner the PRD names and the installer scripts | `tools/validation/pipeline-bench/`, `Plugin/Installer/` (Inno Setup script, `build-pkg.sh`, `build-tarball-linux.sh`), `Plugin/Scripts/build-all.*` in the `WFS_DIY_v1-juce905` checkout | Phase 2's bench runner starts from the first, Phase 7's packaging from the second. |
| GitHub's `ubuntu-latest` is Ubuntu 26.04 ("resolute"): `apt.llvm.org` has no repository for it, `lv2lint` is not packaged, and its own packages carry Clang 22 | The first two CI runs of this repository, 2026-11-16 | The RTSan job installs the newest Clang the image offers. `lv2lint` is dropped until it is packaged again. |
| The template pins line endings to LF in `.gitattributes`, because the author's Windows machine has `autocrlf=true` | `.gitattributes` there | Copied in Phase 0; it also ends the warning Git prints on every commit of this repository. |

---

## Where code comes from

| Piece | Origin | What changes |
| --- | --- | --- |
| Build system | OSCQuery plugin: `CMakePresets.json`, `cmake/OqaOptions.cmake`, `cmake/OqaThirdParty.cmake`, `cmake/JuceHeaderStub` | Renamed (`Likyu*`, `LIKYU_*`), cut down: no `juce_simpleweb`, no shared core library, no CLAP |
| CI and checks | OSCQuery plugin: `.github/workflows/ci.yml`, `scripts/check-claude-md.py`, `check-pins.py`, `check-headers.py`, `farm-check.sh` | Section names for the `CLAUDE.md` check; LV2 build and validation; strictness 10; an RTSan job; no validators' settings file (this plugin opens no socket) |
| Realtime instrumentation | OSCQuery plugin: `src/core/rt/RtCheck.h`, `tests/support/RtAllocCounter.cpp` | `RtCheck.h` moves to where `lib/dsp` can include it without JUCE |
| Test harness | OSCQuery plugin: `tests/TestMain.cpp`, `TestSupport.h`, `LocaleTests.cpp` | The mock device and its helpers are not copied |
| Plugin shell | OSCQuery plugin: `src/shims/juce/CMakeLists.txt` and the processor's skeleton | One product, not slot-count variants; a sidechain bus; real-unit parameters |
| Editor root | OSCQuery plugin: the plain `juce::Component` root and the `SlotPresentation.h` pattern (pure functions over state) | Everything inside it is new |
| Bench runner | WFS-DIY `tools/validation/pipeline-bench` | Its shape only: median, p99, p999 and max per block |
| Installers, signing | WFS-DIY `Plugin/Installer`, `Plugin/Scripts` | Renamed; an LV2 install path |
| The DSP | Written here | The SVF is Simper's published form; the gain computer follows Giannoulis, Massberg and Reiss (2012). No code is taken from another EQ |

Files copied keep their header and gain a one-line origin note. Both repositories are GPLv3.

---

## Phase 0 — Scaffold and spikes · M

**Goal:** a repository that builds a pass-through plugin in three formats on three platforms,
already shaped like the product as far as a host can see, and a written answer on how to track a
frequency.

**0a. Repository.**

- From the template, as listed above. Submodules: JUCE at `d60b7a78`, doctest at the template's
  pin. `THIRD_PARTY_NOTICES.md` from the first dependency, held to the gitlinks by `check-pins.py`.
- `.gitattributes`, `.gitignore`, GPL headers in the template's form.
- `CLAUDE.md` = the PRD's *Goals and non-goals* and *Band model: two axes, one detector*, verbatim,
  under the template's preamble; `check-claude-md.py` compares them on every push.
- Layout, fixed now:

  ```
  lib/dsp/          likyu::dsp, standard library only: filters, detectors, the chain, the parameter table
  src/core/         likyu::core: dispatch, state chunk, meter snapshot
  src/editor/       the editor (Phase 6)
  src/shims/juce/   one AudioProcessor: VST3, AU, LV2
  tests/            doctest suites, fixtures, reference curves, the bench runner
  spikes/           throwaway
  docs/             PRD, this plan, bench/, handoffs/, spikes/
  ```

**0b. The plugin a host can already measure.**

- Two targets. `likyu_plugin`: VST3 everywhere, AU on macOS, LV2 with a stereo default layout and
  the `…/likyu/stereo` URI. `likyu_plugin_lv2_mono`: LV2 only, mono default layout, the
  `…/likyu/mono` URI. Product name `Likyu`; company, codes and bundle ID as the PRD fixes them.
- Buses: main in and out, any layout from 1 to 64 channels with output equal to input; one
  sidechain input, absent, mono or stereo. Declared **not enabled by default** so that JUCE's LV2
  marks its ports optional; Phase 1 checks that every host can still switch it on.
- **The parameter table**, written once in `lib/dsp` from the PRD's two tables: 284 entries with
  ID, name, unit, range, default, skew, automatable. The shim builds the host's parameters from
  it. They do nothing yet. A test asserts the count, the uniqueness of IDs, and that each ID
  survives JUCE's LV2 sanitising unchanged.
- **A lookahead switch that is real**: a delay line sized in `prepare`, the four settings, the
  host's latency-changed notification. It is the product's own lookahead, written early.
- A provisional state chunk (`version` 0: the parameter values, nothing else) so the bench can
  save and reload. It is never shipped and gets no fixture; chunk version 1 is Phase 2 work.
- Bench builds are not for saving sessions: no parameter ID is frozen yet.

**0c. CI.**

- `pins`, Linux, macOS, Windows as the template. pluginval at strictness 10 on the VST3; `auval`
  on macOS; on Linux the two LV2 bundles through `lv2lint` or `lv2_validate`, whichever proves
  usable in a runner (the report says which and why).
- **RTSan job**, new: Linux, a Clang that has `-fsanitize=realtime`, the test executables only,
  `[[clang::nonblocking]]` on `process`. Until Phase 2 it has one function to check.
- `scripts/farm-check.sh` for the Mac mini and the NUC.

**0d. Spike: tracking.** In `spikes/tracker/`, its own small executable and scripts, any language
that plots. It answers three questions and is then deleted.

1. *Estimator.* The gradient adaptive notch against the energy-ratio discriminator, the second
   both on the raw key and behind a band-pass at the current estimate.
2. *Deflation.* Extraction from the raw key (the PRD's corrected form) against the delayed
   subtraction with a one-sample forward rotation. Cost and behaviour, at 220 Hz and at 6 kHz, at
   44.1 and 96 kHz.
3. *Envelope, information only.* `k * sqrt(max(0, v1² − v2·hp))` against the band-pass magnitude,
   on a centred tone, tones half an octave off, and a 100 Hz tone into a 3 kHz detector at Q 1.
   This brings forward the check the PRD leaves to Phase 3, because a wrong formula there would
   change Phase 3's shape.

   Material: synthetic signals taken from the PRD's Track rows (a sine in noise at 0 dB SNR, two
   sines a fifth apart trading ±12 dB, a sine removed, two glides that cross), and recordings.
   Scored on the PRD's thresholds: lock within Speed × 5, error under 20 cents, no move over
   50 cents on a level trade.

**Done when:** CI is green on three platforms with the three formats validated; the plugin passes
audio bit for bit with lookahead Off; `docs/spikes/phase0-tracker.md` names one estimator and one
deflation form, with the plots; the PRD's two open items on them are closed with the date.

**Needs from the author:**

- **Recordings for the spike**: speech with sibilance, a note with a wandering resonance (a bass,
  a tom, or a room mode excited by a voice), a full mix. A few seconds each.
- **Copyright line** for the file headers. *Decided 2026-11-16:* `Copyright (C) 2026 Pierre-Olivier Boulant`; no other contributor for now.
- **Name of the mono LV2.** *Decided 2026-11-16:* "Likyu mono" to a host; the bundles are `Likyu.lv2` and `Likyu-mono.lv2`.

**Record, 2026-11-16.** Built the same day as this plan, on a branch merged to `main` (pull
request 1, the only one: the author decided that day that commits go straight to `main` for now).
Handoff: [`docs/handoffs/2026-11-16-after-phase0.md`](handoffs/2026-11-16-after-phase0.md); bench
note: [`docs/bench/2026-11-16-phase0.md`](bench/2026-11-16-phase0.md); spike:
[`docs/spikes/phase0-tracker.md`](spikes/phase0-tracker.md).

- *0a to 0c, done.* The scaffold, the pass-through plugin with its buses, 284 parameters, real
  lookahead and provisional state, and five CI jobs green: pluginval at strictness 10 on three
  platforms, `auval`, both LV2 bundles read by lilv, `lib/dsp` under the realtime sanitizer.
- *Not as planned:* `lv2lint` is not packaged on the runner's Ubuntu 26.04 and was not run;
  `lv2_validate` runs as information and reports only what JUCE's wrapper writes. The LV2 gate is
  therefore lilv listing and reading both plugins. The suite is two executables, not one: the DSP
  suite has no JUCE in it at all, which is what lets the RTSan job skip JUCE entirely.
- *Added beyond the plan:* a canary in the RTSan job, a program that allocates inside a
  nonblocking function and must die, so that a sanitizer that is not looking cannot pass for a
  clean suite; `scripts/check-dsp-includes.py`.
- *0d, the spike, on synthetic signals only.* Envelope form confirmed, to 0.0001 dB. Estimator:
  the gradient notch, as a frequency-locked loop on one SVF. Deflation: from the raw key; the
  rotated delayed form measures the same and is the fallback. Six findings beyond the questions
  are in the report, two of them new open decisions in the PRD (the Confidence threshold; Speed
  and close low pairs).
- *Done-when, not yet met in full:* the spike has not run on recordings, so the estimator and
  the deflation form stay provisional and `spikes/tracker/` is not deleted. The plugin passes
  audio bit for bit with lookahead Off (tested in the processor, not yet heard in a host).
- *Decided by the author the same day:* headers read `Copyright (C) 2026 Pierre-Olivier Boulant`,
  no other contributor for now; the mono LV2 is "Likyu mono" to a host, `Likyu-mono.lv2` on disk.

---

## Phase 1 — Host bench · S

**Goal:** what each host does with a sidechain bus, a latency change and 284 parameters, measured
before any code assumes it.

The Phase 0 plugin, installed in each host. The matrix, written to `docs/spikes/phase1-hosts.md`:

| Check | Where |
| --- | --- |
| The sidechain bus can be switched on and fed; mono into a stereo main, stereo into a mono main | REAPER (VST3, AU, LV2), Bitwig, Live, Samplitude |
| **What the plugin sees with nothing routed**: bus disabled, bus enabled and silent, or channels missing from the buffer. This decides what "absent or disabled" can mean in the fallback rule | every host and format, Go.dot included |
| Changing lookahead: the host re-reads latency and realigns, during playback and stopped | every host |
| 284 parameters in the automation chooser and the generic view: usable, names and units shown | Live, REAPER, Bitwig, Go.dot |
| State save and reload; in Go.dot, recall on cue launch | every host |
| Layouts beyond stereo: mono, 5.1, a wide discrete track; whether the host reports an LFE as one | REAPER, Go.dot |
| Multitouch reaches a plugin editor (a throwaway component that draws its touches) | every host, Windows and Linux |
| pluginval at strictness 10, `auval`, the LV2 validator | the farm and CI |

REAPER, Samplitude and Live are on the author's Windows machine and Bitwig on both; the Mac mini
covers AU; the NUC covers LV2 on Linux. A row that cannot be filled is left empty and listed.

**Done when:** the report exists; the PRD's sidechain fallback rule is confirmed or amended from
the second row; any host that cannot do what the PRD assumes has a line in the PRD saying so.

**Needs from the author:** an hour per host at the Windows machine; access to the Mac mini and
the NUC; a Go.dot build that hosts the three formats.

**Record, 2026-11-17: the probe is built, the matrix is open.** Handoff:
[`docs/handoffs/2026-11-17-phase1-probe.md`](handoffs/2026-11-17-phase1-probe.md); report, with
the procedure for each row: [`docs/spikes/phase1-hosts.md`](spikes/phase1-hosts.md).

- *Done.* `spikes/hostprobe/`: the Phase 0 processor with an observer derived from it and an
  editor, as a plugin of its own ("Likyu Probe", plugin code `Lkyp`). It shows and exports what
  each row of the matrix asks: the sidechain as the plugin sees it, layouts, latency changes and
  what the host does after them, a fingerprint of the 284 values, the pointers that reach the
  editor. pluginval at strictness 10 passes on it and on the product, on Windows. Both are
  installed on the Windows machine.
- *Changed outside `spikes/`:* the build names its paths from `PROJECT_SOURCE_DIR`, so the probe's
  project can add the repository as a subdirectory; `LikyuProcessor` is not `final` while the
  probe exists.
- *Not done:* no host row is measured. They need the author at the hosts. The farm did not answer
  over SSH on the day, so `scripts/farm-check.sh` has still never run there and the probe is not
  installed on the Mac mini or the NUC.
- *Done-when, not met:* the report exists as a procedure and an empty matrix; the fallback rule is
  neither confirmed nor amended.

**Record, 2026-11-17, evening: the four Windows hosts are measured in VST3.** Handoff:
[`docs/handoffs/2026-11-17-phase1-windows-vst3.md`](handoffs/2026-11-17-phase1-windows-vst3.md).

- *Done.* Samplitude, REAPER, Bitwig and Live, by the author with the probe: sidechain fed,
  nothing routed, lookahead and the null test, touch; state and parameters in part. The findings
  are part 7 of the report.
- *The fallback rule stands on this evidence*, and covers less than it seems to: every host
  inserts the plugin with the bus disabled, and every host also shows an enabled bus at exact
  zeros in ordinary use.
- *Two findings wait for the author* (PRD, Open decisions): REAPER gives a track's extra channels
  to the main bus of a plugin that accepts any width; REAPER names a four-channel track
  "L R C Lfe".
- *Found on the way:* JUCE 9 leaves multitouch off on Windows unless the editor asks. With the
  request all four hosts pass several fingers.
- *Not done:* LV2, Go.dot, macOS, Linux, the farm's validators. Done-when is still not met.
- *Added 2026-11-18, morning:* the farm answered. `scripts/farm-check.sh` passes on the Mac mini
  and the NUC, auval and lilv included; `lv2_validate` reports only what JUCE's wrapper writes,
  as in CI. The probe is installed on both for the author's sessions there.

**Record, 2026-11-18: the macOS hosts are measured.** Handoff:
[`docs/handoffs/2026-11-18-phase1-macos.md`](handoffs/2026-11-18-phase1-macos.md).

- *Done.* REAPER (VST3, AU), Live (VST3, AU), Bitwig (VST3) on the Mac mini, by the author with
  the probe; findings 1 to 10 of the report.
- *Decided by the author:* every channel starts in the link, the LFE is left out by hand (PRD
  amended). Go.dot is on hold until its plugin system changes.
- *Found and corrected:* the plugin exported JUCE's Objective-C++ symbols on macOS, which a
  JUCE host merged with its own (finding 10; the build now hides them).
- *Waiting for the author:* the main bus width in REAPER, with its evidence complete (findings 2
  and 9).
- *Not done:* LV2 in a host, Linux, a wide REAPER track. Done-when is still not met: the
  fallback rule is not yet confirmed or amended in the PRD, and REAPER's limits have no line
  there yet. Phase 2 starts beside it, by the author's plan.

---

## Phase 2 — Static EQ · M

**Goal:** `lib/dsp` is an eight-band parametric EQ that nulls at 0 dB, headless and under test,
and the plugin plays it.

- **2a. The filter.** The TPT SVF as the PRD writes it; `g` by the rational approximation below
  0.25 π and the polynomial above; the 16-sample sub-block with a counter that runs across calls.
  *Rows:* tan approximation.
- **2b. Types and gain.** The eight rows of the coefficient table, proportional and constant Q,
  the dB-to-A conversion exact at 0 dB, the effective gain limit. Reference curves are computed in
  the test, in double, from each type's transfer function on the unit circle. *Rows:* exact
  bypass, static response, gain limit.
- **2c. Movement.** Smoothing in `lib/dsp` (log frequency, dB gain); click-free type, mode and
  on/off switches; flush-to-zero set by `lib/dsp` itself, not by a JUCE helper. *Rows:* modulation
  safety, block-size invariance, denormals.
- **2d. Precision, measured.** Noise floor of a band at 20 Hz, Q 40, 192 kHz, with the audio path
  in float and in double. The PRD decided double; this either confirms it with a number in
  `docs/bench/` or brings the decision back to the author.
- **2e. Chain, table, state.** The series chain over 1 to 64 channels; `likyu::core` dispatching
  host values (atomics, read once per block); state chunk `version` 1 with its fixture, unknown
  fields kept, fewer or more bands than the build, under `C` and `fr_FR`. *Rows:* state, layouts,
  realtime, constant latency (static modes only, so far).
- **2f. The bench runner** and a first figure for eight static bands, stereo, 96 kHz, 64-sample
  blocks, on the bench machine.

**Done when:** the rows above are green; the plugin is a usable static EQ from a host's generic
view; `docs/bench/` has the precision note and the first timing.

**Needs from the author:** confirm or replace the **1 % budget** once 2f gives a number (PRD open
decision). Nothing blocking.

---

## Phase 3 — Dynamic · M

**Goal:** a band's gain follows a key, from the input or the sidechain, linked or not.

- **3a. Detector.** The detector SVF, Follow and Own; the band-pass envelope in the form the
  Phase 0 spike confirmed; the peak follower for high-pass and low-pass detectors, its shortest
  time constant bounded; the level in dB with the fast log. *Rows:* quadrature envelope, detector
  rejection.
- **3b. Gain computer.** Above and Below, soft knee, ratio, Range as limit and sign, attack and
  release on the offset, return to zero. *Rows:* dynamic curve, exact bypass return.
- **3c. Keys and linking.** One set of detectors per key channel; Linked takes the maximum of the
  levels; Unlinked per channel; the sidechain's maximum; the fallback as Phase 1 left it; channels
  left out of the link by hand, all in by default (PRD, amended 2026-11-18). *Rows:* sidechain fallback, channel
  linking, linked maximum, link exclusion.
- **3d. Out of the audio thread.** The meter snapshot (offset, frequency, confidence per band)
  through a single-writer slot. Listen as a flag on the core, driven by a test and by a temporary
  parameter until the editor exists; that parameter is removed in Phase 6 and never frozen.
- **Listening pass:** speech with sibilance, a boomy voice, a mix ducked by a voice in a host
  that routes a sidechain.

**Done when:** the rows are green; the reference use case's first half works in REAPER: a 2 to
4 kHz band of a music track, keyed from a voice on the sidechain, opens room only while the voice
speaks.

**Needs from the author:**

- Phase 1's findings (the fallback row).
- **Form of the link list** (PRD open decision): state set in the editor, as proposed, or host
  parameters. It decides 3c's interface and whether the parameter count stays at 284.

---

## Phase 4 — Transient and lookahead · M

**Goal:** attack and sustain shaped per band without a threshold, and a lookahead that lets a band
catch a leading edge.

- Four envelopes on `max(L, floor)`, the two differences, attack and sustain amounts, the soft
  gate, the Time parameter scaling the four constants together. *Rows:* transient, transient
  after silence.
- The Phase 0 delay line becomes part of the chain: detection reads undelayed signals, bypass
  crossfades to the delayed dry signal. *Rows:* constant latency over every combination of modes
  and bypass, lookahead effect.
- **Listening pass, which sets numbers:** drums, plucked strings, speech. It fixes the four time
  constants and the range of Time.

**Done when:** the rows are green; the listening notes are in `docs/bench/`; the PRD's time
constants and `ttime` range are amended and no longer marked *tune*.

**Needs from the author:** the listening pass itself; the decision on the constants (PRD open
decision).

---

## Phase 5 — Frequency modes · L

**Goal:** a band's frequency follows the key's level, or one spectral peak, and two Track bands
never swap.

- **5a. Env.** Level between Sensitivity and Sensitivity + Span mapped onto log frequency, Up and
  Down, `fatk` and `frel`; the range rules. *Row:* Env.
- **5b. One tracker.** The estimator the spike chose; Speed; the glide in log frequency;
  confidence, Hold, the glide back to the clamped rest frequency; confidence scaling gain.
  *Rows:* Track lock, Track loss.
- **5c. Several.** Deflation in the form the spike chose; minimum spacing; the third-octave
  seeding bank per key source; trackers on the mean of the key's channels when linked. *Rows:*
  Track no swap (220 Hz and 6 kHz, 44.1 and 96 kHz), Track exclusion.
- **5d. With the rest.** Follow-band detectors on a moving band; `g` interpolation under Track;
  every combination of gain mode and frequency mode through the modulation and latency rows
  again.
- **Listening pass:** a de-esser that follows two voices; a boost that follows a bass line; an
  envelope filter on a guitar.

**Done when:** the rows are green; the reference use case runs whole on one instance in REAPER
(voice-keyed band, self-keyed band taming boom, Transient band on a snare, Track band on a
resonance).

**Needs from the author:**

- The spike's result accepted (Phase 0).
- **Parameter IDs frozen here.** *Proposed:* with every parameter now implemented, the author
  declares the 284 IDs frozen at the end of this phase; from then on sessions may be saved and
  chunk version 1 is a shipped version. Ranges marked *tune* stay movable until the beta, as the
  PRD says.
- **Band-pass as a band type** (PRD open decision): kept or dropped, now that Env has been heard
  on it. Dropping it after the freeze would cost an enum value, so it is decided before.

---

## Phase 6 — Editor · M

**Goal:** the curve and the strip, by mouse and by touch, adding nothing a parameter cannot do
except Listen and the link list.

- **6a. Presentation first.** What a node, a bracket, a badge and a strip say in each state, as
  pure functions over the band's state and the snapshot, with headless tests. No pixel before
  this passes.
- **6b. Curve.** Static line and live line from the snapshot at about 30 Hz; nodes; the crosshair
  with its frequency-only and gain-only arms; wheel for Q; vertical and horizontal brackets;
  dimming under the confidence threshold.
- **6c. Detector and keys.** The ghost node for an Own detector; the key badge and its warnings
  (sidechain absent; in Go.dot before Phase 9, sidechain silent by design).
- **6d. Strip.** Only the current modes' controls; Listen, with the guard in the form decided; the
  link list beside Link.
- **6e. Touch.** Several nodes at once, pinch for Q, hit areas that do not overlap at the default
  size, where Phase 1 found that touches arrive. The mouse path complete without it.
- **6f. Analyser**, off by default, from a lock-free tap, last.

**Done when:** every parameter is reachable from the editor; the presentation tests are in CI;
pluginval passes with the editor open; the temporary Listen parameter is gone.

**Needs from the author:**

- **Listen guard, mechanism** (PRD open decision): how a live host is recognised, and the Arm
  switch as proposed or another form.
- A look at the first curve before the strip is built: sizes, colours, what is too much.

---

## Phase 7 — Performance and packaging · M

**Goal:** inside the budget, and installable.

- Detector lanes in SIMD: eight bands of a key channel to an AVX2 register, two NEON registers;
  the audio chain vectorised across channels. The scalar path stays, selected at build time, and
  both are held to the same outputs by the existing tests.
- The seeding bank costed on its own: thirty filters per key source are the first suspect, and
  running them only while a tracker is unlocked is the first remedy to measure.
- The bench against the budget: stereo, eight bands in Dynamic and Track, stereo sidechain,
  96 kHz, 64 samples; then 64 channels linked, as information.
- Installers from WFS-DIY's scripts: Windows (Inno Setup), macOS (pkg, universal binary, signed
  and notarised), Linux (tarball), each with VST3 and LV2, AU on macOS.

**Done when:** the budget row holds on the bench machine and the figure is in `docs/bench/`; the
three installers install plugins that pass the validators on a clean machine.

**Needs from the author:**

- **Oldest x86 processor supported.** AVX2 as a requirement excludes machines before 2013;
  *proposed:* require it, say so in the README, keep the scalar build as an option.
- Signing identities, as for WFS-DIY.

---

## Phase 8 — Go.dot integration, public beta · M

**Goal:** Likyū in a show file, and in other people's hands.

- State recall on cue launch, without a click and without a latency change.
- Control-surface mapping of the 284 parameters; the band strip on a surface.
- The Listen guard working in Go.dot. The editor saying plainly, on a Sidechain band in Go.dot,
  that Go.dot feeds that bus silence until Go.dot can route to it.
- Public beta: tagged build, installers, a README section on what is and is not finished.

**Done when:** a Go.dot show with several instances recalls correctly through a cue list; the
beta is published.

**Needs from the author:** whether the **beta waits for Phase 9** (PRD open decision; proposed:
it does not).

---

## Phase 9 — Sidechain in Go.dot · M, in Go.dot's repository

**Goal:** a cue routes a second source to the plugin's sidechain bus.

Work in Go.dot, not here, taken up once the rest of Go.dot is thoroughly tested (decided
2026-11-16). On this side: the warning added in Phase 8 is removed, the Go.dot row of the host
bench is filled, and the reference use case is run on an array with an actor's microphone.

---

## Later

The CLAP shim, ported from the OSCQuery plugin's `src/shims/clap`; `lib/dsp` linked natively in
Go.dot; frequency momentum to carry trackers through crossings; a wide LV2 variant if one is asked
for.

---

## Cross-cutting, every phase

- **Three-platform CI** green before merge; pluginval at strictness 10, `auval` and the LV2
  validator from Phase 0 on.
- **`lib/dsp` includes no JUCE header.** A check in the `pins` job greps for it.
- **No allocation, lock, exception, syscall or log between `prepare` and `release`.** The
  allocation counter and the RTSan job run on every test that calls `process`.
- **Block-size invariance** is re-run on every new mode: 1, 7, 64 and 4096 samples.
- **Constant latency** is re-run on every new mode: reported equals measured, whatever the bands
  do.
- **Exact bypass** is re-run on every new mode: idle means bit for bit.
- **Locale**: everything that writes numbers runs under `fr_FR` as well as `C`.
- **State chunk fixtures**: every version ever shipped stays loadable, a fixture per version in
  `tests/state/`.
- **Each phase ends** with a bench note in `docs/bench/` and a handoff note in `docs/handoffs/`,
  dated, and a *Record* paragraph under its heading here.
- **The PRD is amended, with the date, whenever a measurement contradicts it.** This plan never
  overrides it.

---

## Needs from the author, collected

| Phase | Need | Blocking |
| --- | --- | --- |
| 0 | Recordings for the tracker spike | The spike, not the scaffold |
| 0 | Copyright line; mono LV2 name | Decided 2026-11-16 |
| 1 | Time at each host; the farm; a Go.dot build | Yes |
| 2 | The performance budget, after the first figure | No |
| 3 | Form of the link list | Yes, for 3c |
| 4 | Listening pass; the transient time constants | Yes |
| 5 | Freeze of the parameter IDs; band-pass as a band type | Yes, at the end |
| 6 | Listen guard mechanism; a look at the first curve | Yes, for 6d |
| 7 | Oldest processor supported; signing identities | Yes |
| 8 | Whether the beta waits for Phase 9 | Yes, at the end |

Nothing blocks the start of Phase 0.
