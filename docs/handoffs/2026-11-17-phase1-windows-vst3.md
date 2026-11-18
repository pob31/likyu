# Handoff: Phase 1, the four Windows hosts measured in VST3

2026-11-17, evening. Replaces the morning's note
([`2026-11-17-phase1-probe.md`](2026-11-17-phase1-probe.md)) as the place to start; that one
still describes how the probe is built. Phase 1 is **not** done: one format on one system is in.

## Where it stands

- **Measured, by the author with the probe:** Samplitude, REAPER, Bitwig and Live, VST3, Windows
  11. The matrix, the findings (part 7) and every report as saved (part 8) are in
  [`docs/spikes/phase1-hosts.md`](../spikes/phase1-hosts.md). Read part 7 first.
- **Not begun:** LV2 in a host (REAPER on Windows, then Linux), AU and the rest on the Mac mini,
  Go.dot. Part 6 of the report lists them and the small gaps left in the four hosts.
- **The farm**, added 2026-11-18 morning: both machines answered. `scripts/farm-check.sh` passes
  on the Mac mini and the NUC, auval and lilv included (part 3 of the report), and CI is green on
  `745aff2`. The probe, its mono LV2, the fixed-stereo control and the product are installed in
  the user's plugin folders on both, ready for the author's macOS and Linux sessions.

## What the measurements say, in five lines

1. Every host inserts the plugin with the sidechain bus disabled, and every host also shows an
   enabled bus at exact zeros in ordinary use. The PRD's fallback rule stands; it covers the
   first insertion and little else.
2. Three hosts realign on the fly after a lookahead change and one at the next play; two restart
   the plugin for it and two do not.
3. Blocks from 128 to 2000 samples; REAPER runs in double, the others in float; two hosts stop
   calling the plugin when stopped.
4. Multitouch reaches a plugin's editor in all four hosts, once the editor asks JUCE for it.
5. Live shows no parameter of an editor-less plugin until something moves them from inside.

## Waiting for the author

Both are in the PRD's *Open decisions* since today. Neither is the implementer's to settle.

- **REAPER and the main bus's width.** A hand-made send to channels 3/4 lands in the main bus of
  a plugin that accepts any width. Two measurements would size it and are a few minutes each: a
  saved report from the instance whose sidechain works after the drag onto the plugin's window,
  and the fixed-stereo control (`Likyu Probe fixed stereo`, installed) on a four-channel track
  with a send to 3/4.
- **REAPER and the LFE.** A four-channel track arrives as "L R C Lfe".

## For Phase 2, which can start beside this

Nothing here blocks it. What the bench adds to its brief:

- **Block sizes and precision to test with:** 128, 256, 480 and 2000 samples beside the devplan's
  1, 7, 64 and 4096; double as well as float in the processor, since REAPER uses it.
- **`setStateInformation` arrives before any `prepareToPlay` in Bitwig** and just after the first
  in Samplitude and Live. Chunk version 1 must load at either moment.
- **`prepareToPlay` is called often**: twice at every play in Samplitude, once or twice at every
  lookahead change in Live and Bitwig. It must be cheap and must not click.
- **Value text.** Samplitude shows a parameter's value as the plugin's default text: seven
  decimals, no unit. Give each parameter its own text when the table grows a formatter.
- **Parameter names** are cut after about nineteen characters in Live's chooser. They are not
  frozen before the end of Phase 5.
- **A toggle holds the value a host gave it** (0.58) until it is saved as "on". The probe's
  fingerprint tripped on it; the state code already saves the settled value.
- **auval warns on 18 logarithmic defaults** (the sixteen attacks at 5 ms, 500 Hz, 160 Hz): the
  default does not survive the float mapping, 500 reads back as 499.99997. A warning, not a
  failure. When the parameter table gets its formatter and ranges, a test that every default
  maps to itself would settle it.

## For Phase 4 and Phase 6, so it is not found twice

- **Phase 4:** no host can be counted on to restart the plugin at a lookahead change, nor not to.
  The change of delay must be clean by itself. The slip of sound heard at every change today is
  partly the Phase 0 delay jumping between two blocks.
- **Phase 6:** override `AudioProcessorEditor::usesWindowsMultiTouch()` to return true, or one
  finger is all the editor gets on Windows. With it on, `mouseMagnify` is never called: pinch for
  Q is computed from two touches on a node. And the key badge has more states than "connected" and
  "absent": enabled and silent is ordinary, and Samplitude shows "disabled" for a routed
  sidechain until the first play.

## The probe, as it is now

`spikes/hostprobe/`, five targets: the probe (VST3, LV2, AU on macOS), its mono LV2, a
fixed-stereo control (VST3), `likyu_hostprobe_snapshot` (report and picture without a host, and
`--roundtrip` for a save and load), `likyu_hostprobe_touch` (the editor in a window of its own).
The reports of the day found six faults in it, all corrected and each told in part 8 where it
misled a reading: no highest peak for the main input, a stale transport line, zeros reported when
process calls had only stopped, a fingerprint that hashed unsettled toggles, 64 speaker names per
log line, and multitouch not asked for.

Installed on the Windows machine, current as of 18:34: `Likyu.vst3`, `LikyuProbe.vst3`,
`LikyuProbe-fixed.vst3` in `%LOCALAPPDATA%\Programs\Common\VST3`, four LV2 bundles in
`%APPDATA%\LV2`.

## Things that will bite

Those of the two earlier handoffs hold. Added today:

- **`install.ps1` deletes a bundle before copying it.** When a host has the plugin open the
  delete half-succeeds and leaves the installed bundle incomplete until the host is closed and
  the script run again. Close the hosts first.
- **On the farm** (2026-11-18): auval over SSH cannot see a component the logged-in session has
  registered; `farm-check.sh` now runs it in the session through a `.command` file, which leaves
  a Terminal window open on the Mac. On the NUC the author installed `lilv-utils xvfb lv2-dev
  sordi` on 2026-11-18, so the script there runs pluginval's GUI tests, lists both LV2 and prints
  `lv2_validate`'s count as information (112 errors, all JUCE's, as in CI). If a check there is
  ever skipped again, the script still exits 0: read its output, not its exit code.
- **A Python patch passed through a Git Bash heredoc lost its backslashes again**, as the Phase 0
  handoff warned. Write the script to a file with the editor, then run it.
- **The author's hosts are not his daily ones** (his words). Routing a sidechain took a tutorial
  in REAPER; give the steps for the gesture, and expect that a host's obvious way and its right
  way differ.
- **CI does not build `spikes/hostprobe/`.** It compiles `src/shims/juce/PluginProcessor.cpp`; a
  change to `LikyuProcessor` can break it unseen.

## To finish Phase 1

As the morning's note lists, from its step 2: the farm, the remaining formats and systems, the
two measurements above, the author's two decisions, the PRD's host table, a bench note, the
devplan's *Record* completed, then the probe deleted and `final` restored.
