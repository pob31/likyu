# Handoff after Phase 0

2026-11-16. Where the repository stands, what is open, and what to do next.

## What exists

- **The scaffold** (devplan 0a): the OSCQuery plugin's template at `1d8caa8`, cut down. JUCE 9.0.5
  and doctest 2.5.3 as submodules. `CLAUDE.md` holds the PRD's two sections verbatim.
- **`lib/dsp`**, standard library only: `dsp/Parameters.h` (the 284-entry table), `dsp/Lookahead.h`
  (the delay), `dsp/rt/RtCheck.h` (realtime regions). No filter yet.
- **`src/core`**: `ProvisionalState`, a `version` 0 chunk of parameter values. To be deleted when
  chunk version 1 lands in Phase 2.
- **`src/shims/juce`**: `LikyuProcessor`. It passes audio. Main bus 1 to 64 channels, output equal
  to input; sidechain absent, mono or stereo, not enabled by default; 284 parameters in real
  units, inert except `lookahead`; latency reported from the message thread only. No editor.
- **Two plugin targets**: `likyu_plugin` (VST3, AU, stereo LV2) and `likyu_plugin_lv2_mono`.
- **Tests**: `likyu_dsp_tests` (20 cases, no JUCE) and `likyu_tests` (16 cases, under `C` and
  French). Both replace `operator new` and count allocations inside realtime regions.
- **CI**, five jobs, all green on `5101bf6`: `pins`; Linux, macOS, Windows with pluginval 1.0.4 at
  strictness 10 on the VST3; `auval` on macOS; both LV2 bundles listed and read by lilv on Linux;
  `lib/dsp` under Clang's realtime sanitizer with a canary that must die.
- **The tracker spike** (0d): `docs/spikes/phase0-tracker.md`. Synthetic signals only.

## What Phase 0 still owes

- **The spike on recordings.** `spikes/tracker/tracker_spike.py --wav ... --range LOW HIGH
  --plots docs/spikes/phase0-tracker` when the author's files arrive; then the report's part 4,
  the two open decisions it closes in the PRD, and the deletion of `spikes/tracker/`. It needs
  numpy and matplotlib, which are not installed in this machine's Python: use a virtual
  environment.
- **Three answers from the author**: the copyright line (headers currently read
  `Copyright (C) 2026 Pierre-Olivier Boulant`), the mono LV2 bundle's file name (currently
  `Likyu-Mono.lv2`, plugin name "Likyu Mono"), and the two questions the spike raised (Confidence
  threshold, Speed and close low pairs; PRD, Open decisions).

## Things that will bite

- **`cmake` is not on `PATH`** on the Windows machine. `CLAUDE.md`, *The bench*, has the path.
- **The check scripts print the PRD's file name**, which has a macron: they set their own stdout
  to UTF-8, or a Windows console kills them.
- **Git Bash heredocs and backslashes**: a Python patch passed through one lost its escapes
  twice in this phase. Write the script to a file and run it, or use the editor.
- **JUCE is compiled three times** (two plugin targets and `likyu_tests`). About five minutes
  from clean on this machine; CI has ccache.
- **The runner's Ubuntu is 26.04.** `apt.llvm.org` has no repository for it and `lv2lint` is not
  packaged; Clang 22 comes from the image's own packages.
- **`lv2_validate` reports errors that are JUCE's**, not this plugin's: `doap:maintainer` and
  `doap:release` on the plugin subject, and `lv2:portProperty` on patch parameters, which the
  LV2 schema keeps for ports. The step is information only and stays so. lilv, which hosts are
  built on, reads both bundles without complaint.
- **Parameter values pass through `float`** in the JUCE shim (`AudioParameterFloat`). The tests
  compare to 1e-5 relative. Phase 2's dispatch should know it.
- **The lookahead changes its delay between two blocks without a crossfade.** Harmless while the
  plugin is a wire; a click once it is not. Phase 4.

## Next

Phase 1, the host bench, which needs the author at the hosts: the matrix is in the devplan. The
Release artefacts of the last CI run on `main` are what to install. Phase 2 does not depend on
Phase 1 and can start beside it.
