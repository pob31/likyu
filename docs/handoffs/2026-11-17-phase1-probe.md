# Handoff: Phase 1, the probe is built and the matrix is open

2026-11-17. For the agent who takes the next step, whichever it is: finishing Phase 1 with the
author's measurements, or starting Phase 2 beside it. Phase 1 is **not** done.

## What exists

- **`spikes/hostprobe/`**, throwaway: `ProbeProcessor` derives from the real `LikyuProcessor`,
  calls it, and records what the host does; `ProbeEditor` shows it live and exports it. Two
  targets, `likyu_hostprobe` (VST3, stereo LV2, AU on macOS) and `likyu_hostprobe_lv2_mono`.
  To a host it is "Likyu Probe" and "Likyu Probe mono": plugin code `Lkyp` under `PxBl`, bundle
  ID `io.github.pob31.likyu.probe`, LV2 URIs `https://pixetbel.org/lv2/likyu-probe/stereo` and
  `…/mono`. The bundles are `LikyuProbe.vst3`, `LikyuProbe.lv2`, `LikyuProbe-mono.lv2`,
  `LikyuProbe.component`.
- **What the probe shows**, per instance: the sidechain as the plugin sees it (bus disabled;
  enabled with its channels missing from the buffer; enabled, exact zeros; enabled, signal, with
  peaks), every layout the host asks about and the one it sets, block sizes, float or double,
  bypassed calls, the transport, each change of reported latency and how many `prepareToPlay`
  calls follow it, a fingerprint of the 284 parameter values, the pointers that reach the editor
  (how many at once, mouse, touch or pen, wheel and pinch). **Copy report** and **Save report**
  give all of it as Markdown; saved reports go to `Documents/likyu-hostprobe/`. With the
  environment variable `LIKYU_PROBE_DUMP` naming a folder, every instance also writes its report
  there when it is destroyed.
- **`docs/spikes/phase1-hosts.md`**: the procedure for each row, the matrix (empty where nothing
  was measured), the validators' rows, what JUCE's wrappers do as read from their source, and the
  list of empty cells.
- **Two changes outside `spikes/`:** the build files say `PROJECT_SOURCE_DIR` where they said
  `CMAKE_SOURCE_DIR` (the probe's project adds the repository as a subdirectory, because the root
  may not build anything in `spikes/`); and `LikyuProcessor` is not `final`
  (`src/shims/juce/PluginProcessor.h`, with a comment).
- **Installed on the Windows machine**, per user: `Likyu.vst3` and `LikyuProbe.vst3` in
  `%LOCALAPPDATA%\Programs\Common\VST3`, the four LV2 bundles in `%APPDATA%\LV2`.
  `spikes\hostprobe\install.ps1 -Uninstall` removes them.

## What was checked, and what was not

- The normal build and its three ctest entries pass on Windows after the build change; the four
  `scripts/check-*.py` pass.
- pluginval 1.0.4 at strictness 10, VST3, Windows: passes on the probe (editor tests included)
  and on the product built from the probe's tree.
- The probe's LV2 manifests list the same audio ports as the product's.
- The probe run without a host (`likyu_hostprobe_snapshot <folder>`, a third target of the spike,
  which writes the report and a picture of the editor): with a stereo sidechain carrying signal
  on its first channel only, it reads "bus enabled, signal", -32.0 dB and exact zero, and the
  editor lays out as intended at its default size.
- **Not checked:** the probe has not been opened in a real host by anyone. The first minute in
  REAPER is its real test. It has not been built on macOS or Linux; `install.sh` has never run.
- **The farm did not answer** (`ssh macmini`, `ssh nuc`: connection timed out). So
  `scripts/farm-check.sh` has still never run, and nothing is installed there.

## To finish Phase 1

1. The author runs the sessions in `docs/spikes/phase1-hosts.md` and saves a report per session.
   On this machine they land in `C:\Users\pierr\Documents\likyu-hostprobe\`; read them from there.
2. On the farm, when it answers: a `git bundle` clone as `scripts/farm-check.sh` describes, that
   script, then `bash spikes/hostprobe/install.sh`. Fill the validators' table.
3. Fill the matrix from the reports and the author's notes. Leave a cell empty rather than infer
   it. The section read from JUCE's source says what to expect; it is not a measurement. Its
   prediction matters: only VST3 can show the plugin a disabled sidechain bus; in AU and LV2 an
   unrouted sidechain would be enabled and read exact zeros, so the fallback to Self would never
   happen there.
4. State the finding on the second row (what the plugin sees with nothing routed) per host and
   format, then confirm the PRD's sidechain fallback rule or propose its amendment **to the
   author**: the rule is a decision of 2026-11-16, and its replacement is not the implementer's.
5. For every host that cannot do what the PRD assumes, a line in the PRD's host table, dated.
6. A bench note in `docs/bench/`, the *Record* paragraph under the devplan's Phase 1 completed,
   a handoff note.
7. Delete `spikes/hostprobe/`, restore `final` on `LikyuProcessor`, have the report name the
   commit where the probe can still be found (`docs/spikes/README.md`). Uninstall the probe from
   each machine first.

## To start Phase 2 beside it

The devplan allows it and nothing here is in its way. Phase 2 touches `lib/dsp`, `src/core` and
the shim; the probe compiles `src/shims/juce/PluginProcessor.cpp` and links `likyu::core`, so:

- **A change to `LikyuProcessor`'s interface can break the probe's build**, which CI does not
  build. While the matrix is open, build it after such a change
  (`cmake --build build/hostprobe --config Release`), or tell the author the probe is frozen at a
  named commit.
- The probe reads `dsp::lookaheadMilliseconds`, `dsp::numParams`, `dsp::indexOf` and the
  processor's `parameterAt`, `valueOf`, `setValue`. Nothing else.
- Phase 3 needs this phase's second row, Phase 2 does not.

## Things that will bite

Those of the Phase 0 handoff still hold. Added here:

- **`const char*` + `juce::String` reads the literal as ASCII**; `juce::String` + `const char*`
  reads it as UTF-8. A `·` in a literal came out as two characters in one place and not in the
  others. The probe's literals are ASCII now; the product's editor will meet this in Phase 6.
- **AU wants a plugin code with one capital, first.** Hence `Lkyp` and not `LkyP`. The product's
  `Lkyu` already conforms.
- **pluginval asks about 44 layouts and toggles the sidechain bus hundreds of times** in one run.
  The probe keeps the first 30 lines of its log and rolls the rest, so a long session still
  shows how it began.
- **The probe's tree builds the product too** (`build/hostprobe/likyu/…`), and JUCE three more
  times: two probe targets and the snapshot program.
- **Hosts and the per-user VST3 folder.** Whether each host scans
  `%LOCALAPPDATA%\Programs\Common\VST3` is not known; `install.ps1 -Vst3Dir` takes another folder.
