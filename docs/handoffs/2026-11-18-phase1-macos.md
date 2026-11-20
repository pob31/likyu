# Handoff: Phase 1, the macOS hosts measured; Phase 2 can start

> **Superseded as the place to start** by [`2026-11-20-phase2.md`](2026-11-20-phase2.md).
> Its Phase 1 notes, and the things that bite, still hold.

2026-11-18, evening. Replaces [`2026-11-17-phase1-windows-vst3.md`](2026-11-17-phase1-windows-vst3.md)
as the place to start; that one still holds the notes for Phases 2, 4 and 6 found on Windows,
and the things that bite. The probe's build is described in
[`2026-11-17-phase1-probe.md`](2026-11-17-phase1-probe.md).

**The author's plan (2026-11-18):** start Phase 2 in the next session; the author checks LV2
meanwhile. Phase 2 needs nothing that is still open in Phase 1.

## Where Phase 1 stands

- **Measured:** Windows 11, VST3: Samplitude, REAPER, Bitwig, Live. macOS 15.8 (Mac mini):
  REAPER (VST3 and AU), Live (VST3 and AU), Bitwig (VST3). The validators on all three systems.
  Everything is in [`docs/spikes/phase1-hosts.md`](../spikes/phase1-hosts.md): part 5 the
  matrix, part 6 what is empty, **part 7 the ten findings (read these)**, part 8 every report.
- **Not done:** LV2 in a host (REAPER on Windows, REAPER on Linux), Linux at all, a wide REAPER
  track, the small Windows gaps of part 6. **Go.dot is on hold** by the author's decision until
  its plugin system changes (finding 10).
- **Waiting for the author:** one open decision, *Main bus width in REAPER* (PRD, Open
  decisions; findings 2 and 9). Its evidence is complete: a plugin that accepts any main width
  needs the user to re-wire its sidechain in REAPER's pin connector, a fixed-stereo control does
  not; and the AU of such a plugin is silently not run on any REAPER track wider than stereo.
- **Decided today by the author:** every channel starts in the link; the LFE is left out by hand
  (PRD amended, decision log 2026-11-18).
- **To close the phase** once LV2 and Linux are in and the decision is made: the PRD's host
  lines, a bench note in `docs/bench/`, the devplan's *Record*, then `spikes/hostprobe/` deleted
  and `final` restored on `LikyuProcessor`, and the probe uninstalled from the three machines.

## What the Mac added, in six lines

1. **REAPER's default pin map is one to one** (finding 2). Any width costs a re-wiring step; a
   fixed stereo main does not.
2. **REAPER renders no AU of ours on a track wider than stereo** (finding 9): it gives four in,
   two out; the plugin refuses unequal widths at initialisation; REAPER then calls render on an
   uninitialised unit, and the track is silent or passes untouched, with no message.
3. **An AU's sidechain is never disabled** (finding 1): enabled at exact zeros from the first
   block, so the fallback to Self cannot happen in AU.
4. **Whether a host restarts the plugin at a latency change depends on host and format**
   (finding 4): Live restarts the VST3 and not the AU; Bitwig restarts twice.
5. **State came back exactly** in every host and format tried on the Mac, sidechain routing
   included.
6. **The plugin exported JUCE on macOS** (finding 10), and a JUCE host merged it with its own;
   corrected in the build.

## For Phase 2

The 2026-11-17 handoff's list holds (block sizes 128, 256, 480, 512, 2000; double as well as
float; state at any moment relative to `prepareToPlay`; `prepareToPlay` often and cheap; each
parameter its own value text; names cut at about nineteen characters in Live). Added since:

- **Value text with units.** Neither Samplitude nor REAPER's generic view of the AU shows a unit;
  both print the plugin's own text. A formatter with units is what users will see there.
- **auval warns on 18 logarithmic defaults** that do not survive the float mapping (5 ms reads
  back 5.0000024). A test that every default maps to itself would settle it with the table's
  formatter.
- **Sample rates seen:** 44.1 and 48 kHz on the Mac, 48 kHz on Windows; blocks of 512 in all
  three Mac hosts.
- **Layouts:** the chain is to work over 1 to 64 channels whatever the author decides for
  REAPER; that decision changes what the shim accepts, not the chain.
- **Check after any build change on macOS:** `nm -gU <binary> | c++filt | grep -c juce::` must
  give 0 (finding 10). CI does not check it.

## For Phases 3, 4, 6 and 7

- **Phase 3:** the fallback to Self covers a VST3 bus the host leaves disabled and nothing in
  AU; a Sidechain band with nothing routed in AU is keyed by silence. What the editor says then
  is Phase 6's.
- **Phase 4:** the change of delay must be clean by itself; a restart may or may not follow.
- **Phase 6:** an editor in Go.dot runs in its editing helper, a JUCE program: finding 10 is
  what keeps it alive there.
- **Phase 7 (documentation):** Live shows our AU only with Audio Units v2 enabled in its
  Plug-In Sources; REAPER users on multichannel tracks need the VST3 (finding 9) unless the
  decision changes that.

## The probe, as it is now

- **Installed:** Windows (as of 2026-11-17, before the visibility fix, which concerns macOS
  only); Mac mini (rebuilt 2026-11-18 with the fix, under `~/likyu-farm-20261118/`); NUC (built
  from `745aff2`, without the fix; on Linux the C++ preset already hid JUCE, so nothing to
  redo for the measurements).
- **New in the tree:** `spikes/hostprobe/auhost.c`, a minimal AU host for the Mac, built by
  hand; its header says how. It reproduced finding 9.

## Things that will bite, added today

- **On the Mac over SSH:** `auval` and any AU host must run in the logged-in session (a
  `.command` file opened with `open`); `log` is a zsh builtin, use `/usr/bin/log`; a glob that
  matches nothing aborts the whole zsh command. The Mac stopped answering SSH again on the
  morning of 2026-11-19.
- **Reading REAPER's projects** answers more than the reports: a plugin's pin map is the second
  line of its block in the `.RPP` (base64; an ID, `0xfeed5eee`, the input count, one 64-bit mask
  of track channels per input, then the outputs).
- **Counting refused AU renders** in the macOS log tells how many instances fail: about 94 a
  second each at 48 kHz and 512 frames.
- **A pass-through plugin sounds the same whether it runs or not.** Only the probe's block
  count tells (finding 9). This stops being true when Phase 2 makes the plugin do something.
- **The author's hosts are not his daily ones**: give the gesture step by step, and read the
  project file before concluding what was routed.
