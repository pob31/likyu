# Phase 1: the host bench

Started 2026-11-17. Devplan Phase 1. Code: `spikes/hostprobe/`, still in the tree because the
matrix is open; it is deleted when the matrix is filled.

**Status: one partial session, Samplitude at load (part 8); no row is complete.** The probe is built and validated, the procedure is written,
and the validators' rows are filled for Windows. Every host row below is empty and waits for the
author at the hosts. Part 4 says what JUCE's wrappers do as read from their source: it tells what
to expect and is not a measurement.

## 1. The questions

| # | Row of the devplan | Answered by |
| --- | --- | --- |
| 1 | The sidechain bus can be switched on and fed; mono into a stereo main, stereo into a mono main | The probe's *Sidechain* and *SEEN AS* lines |
| 2 | **What the plugin sees with nothing routed**: bus disabled, bus enabled and silent, or channels missing from the buffer | The probe's *SEEN AS* line, before anything is routed |
| 3 | Changing lookahead: the host re-reads latency and realigns, playing and stopped | The probe's *Latency* line and log, and a null test by ear |
| 4 | 284 parameters in the automation chooser and the generic view: usable, names and units shown | The author's eyes, on Likyu itself (it has no editor, so the host shows its generic view) |
| 5 | State save and reload; in Go.dot, recall on cue launch | The probe's fingerprint |
| 6 | Layouts beyond stereo: mono, 5.1, a wide discrete track; whether the host reports an LFE | The probe's *Main bus* line |
| 7 | Multitouch reaches a plugin editor | The probe's pointer pad |
| 8 | pluginval at strictness 10, `auval`, the LV2 validator | Part 3 |

## 2. The procedure, per host and format

Two plugins are installed side by side: **Likyu** (the Phase 0 plugin, no editor) and **Likyu
Probe** (the same processor, watched, with an editor). `spikes\hostprobe\install.ps1` on Windows,
`bash spikes/hostprobe/install.sh` on macOS and Linux. If a host does not list them, add the
per-user VST3 or LV2 folder to its scan paths, or see the script's header.

One session is one host and one format. It takes the steps below in order and ends with **Save
report**, which writes everything the probe saw to `Documents/likyu-hostprobe/`. The log keeps
the time of each change, so the steps need not be saved one by one. What the probe cannot see is
noted by hand in the session's line of the matrix.

**A. Nothing routed (row 2).** A stereo track with audio on it, playing. Insert Likyu Probe, open
its editor, route nothing to it. Read the orange *SEEN AS* line. Then, where the host has a
switch that enables a sidechain input without choosing a source, switch it on and read the line
again.

**B. Sidechain fed (row 1).** Route a second track to the plugin's sidechain by the host's own
means (in REAPER: give the track four channels, send the second track to channels 3 and 4, and
look at the plug-in pin connector). The line should read *BUS ENABLED, SIGNAL* with a level. Try a
mono source and a stereo one. Then put the probe on a mono track and feed it a stereo source.
Note by hand: where the host's control for this is, and whether it offered a mono or a stereo
sidechain.

**C. Sidechain removed again.** Unroute it. Does the line come back to what step A showed, or to
*EXACT ZEROS*?

**D. Lookahead (row 3).** Two tracks with the same click or drum loop; the probe on one, that
track's polarity inverted; the sum should be silent. Stopped, press **3 ms**: the *Latency* line
changes; play: still silent? While playing, press **Lookahead off**, then **1 ms**: does the
silence return by itself, after a stop and start, or not at all? Note it by hand. The log records
whether `prepareToPlay` followed each change.

**E. State (row 5).** Press **Scramble** and note the fingerprint. Save the project, close it,
reopen it: the same fingerprint? Then duplicate the track or copy the plugin: the same again? In
Go.dot, also recall it from a cue.

**F. Layouts (row 6), REAPER and Go.dot.** The probe on a mono track, a 5.1 track and a wide one
(16 channels or more). Read the *Main bus* line: the layout's name, and whether an LFE is
reported.

**G. Pointers (row 7), Windows and Linux, on a touch screen.** Press the pad with one finger, then
several; pinch; turn the wheel. The *Pointers* line counts how many were down at once and of
which kind.

**H. Parameters (row 4), on Likyu, not the probe.** Open the host's generic view and its
automation chooser. Note by hand: are the 284 parameters all there, are names and units shown,
can one be found (search, folders, a flat list), is `lookahead` hidden from automation as it
should be.

Then **Save report**, and go to the next format or host.

## 3. Validators (row 8)

| Check | Where | Result |
| --- | --- | --- |
| pluginval 1.0.4, strictness 10, VST3, Likyu | Windows 11, 2026-11-17, built from the probe's tree at `07f4677` | Pass |
| pluginval 1.0.4, strictness 10, VST3, Likyu Probe, editor tests included | the same | Pass |
| pluginval at strictness 10 on Likyu; `auval`; both LV2 bundles read by lilv | CI on Linux, macOS, Windows | Pass, as recorded in `docs/bench/2026-11-16-phase0.md`; not run again for this phase beyond the CI of its commits |
| `lv2lint` | CI | Not run: not packaged on the runner (Phase 0) |
| `scripts/farm-check.sh` | Mac mini | **Empty.** `ssh macmini` timed out on 2026-11-17 |
| `scripts/farm-check.sh`, and `lv2lint` if the machine has it | NUC | **Empty.** `ssh nuc` timed out on 2026-11-17 |

The probe's LV2 manifests, read after the build: the same audio ports as the product's (stereo:
two main inputs, two sidechain inputs marked `lv2:connectionOptional`, two outputs; mono: one, one
optional, one).

## 4. What JUCE's wrappers do, read from the source at the pin

Not measured. This is what the plugin's own side of each format does, whatever the host; the
hosts' side is the matrix. Paths are under `ThirdParty/JUCE/modules`.

| Format | Host leaves the sidechain alone | Host routes nothing but the bus is on | Latency change |
| --- | --- | --- | --- |
| VST3 | The processor's bus follows the host's `activateBus`: while the host keeps it inactive the bus is **disabled** and the buffer has the main channels only (`juce_audio_plugin_client_VST3.cpp`, `activateBus`, about line 3359) | Channels present and **zero-filled** when the host has the bus inactive but the processor has it on; copied from the host otherwise (`juce_VST3Common.h`, about lines 687 to 714). The host's `silenceFlags` are never read | `restartComponent (kLatencyChanged)`, on the message thread |
| AU | There is no such state: every bus is enabled at construction ("audio units do not have a notion of enabled or un-enabled buses", `juce_audio_plugin_client_AU_1.mm`, about line 122) | An input element with no connection fails its pull and its channels are **cleared** (about line 1532) | `PropertyChanged (kAudioUnitProperty_Latency)`, on the message thread |
| LV2 | There is no such state: every bus is enabled at instantiation (`juce_audio_plugin_client_LV2.cpp`, `createProcessorInstance`, about line 831) | A port the host left unconnected is a null pointer and its channel is **cleared** (about line 639) | A control output port written at the end of every `run`; no notification |

**What this predicts, to be confirmed or contradicted by step A:** only a VST3 host can show the
plugin a disabled sidechain bus. In AU and LV2 a sidechain nobody routed is enabled and reads
exact zeros, which the PRD's rule treats as a key ("a bus that is enabled but silent cannot be
told from a quiet key"). If the hosts bear this out, the fallback to Self never happens in AU and
LV2, and a Sidechain band there is keyed by silence, as it is in Go.dot today. Whether the rule
then stands as written is the author's decision, not this report's.

One more thing read there: if a VST3 host has the bus active but hands over a null pointer or too
few channels, the wrapper gives the plugin a buffer that is silent on **every** channel, the main
input included (`juce_VST3Common.h`, about lines 617 and 664). The probe would show it as the main
peak dropping to exact zero.

## 5. The matrix

One line per session. A cell is empty until a session fills it; "n/a" where the row does not apply
to that host.

| Host, format, system | 1 fed | 2 nothing routed | 3 lookahead | 4 parameters | 5 state | 6 layouts | 7 touch |
| --- | --- | --- | --- | --- | --- | --- | --- |
| REAPER, VST3, Windows |  |  |  |  |  |  |  |
| REAPER, LV2, Windows |  |  |  |  |  |  |  |
| Bitwig, VST3, Windows |  |  |  |  |  | n/a |  |
| Live 12, VST3, Windows |  |  |  |  |  | n/a |  |
| Samplitude, VST3, Windows |  | Partial, 2026-11-17: at load the host leaves the sidechain bus inactive and the plugin has it **disabled**. No block had been processed when the report was saved, so what the buffer holds is not yet seen (part 8) |  | n/a |  | n/a |  |
| Go.dot, VST3, Windows | n/a |  |  |  |  |  |  |
| Go.dot, LV2, Windows | n/a |  |  |  |  |  |  |
| REAPER, VST3, macOS |  |  |  |  |  |  | n/a |
| REAPER, AU, macOS |  |  |  |  |  |  | n/a |
| REAPER, LV2, macOS |  |  |  |  |  |  | n/a |
| Live, AU, macOS |  |  |  |  |  | n/a | n/a |
| Bitwig, VST3, macOS |  |  |  |  |  | n/a | n/a |
| Go.dot, AU, macOS | n/a |  |  |  |  |  | n/a |
| REAPER, VST3, Linux |  |  |  |  |  |  |  |
| REAPER, LV2, Linux |  |  |  |  |  |  |  |

Go.dot's sidechain is not benched (row 1): it feeds that bus silence until devplan Phase 9.

## 6. Empty cells, and who can fill them

- **Every host line**: the author, at the Windows machine for the first seven, at the Mac mini and
  the NUC (or over a remote desktop) for the rest. An agent cannot: each needs a host's window.
- **The Go.dot lines** also need a Go.dot build that hosts the three formats.
- **The farm's validators**: an agent, as soon as `ssh macmini` and `ssh nuc` answer.
- **Row 7 on Linux** needs a touch screen on the NUC; if there is none, the cells stay empty and
  say so.

## 7. Findings

None yet. When the matrix is filled: the answer to row 2 per format, the fallback rule confirmed
or the amendment proposed to the author, and a line in the PRD for each host that cannot do what
it assumes.

## 8. Reports, as saved

### Samplitude, VST3, Windows 11, 2026-11-17 13:44, session 1 (loaded, not played)

Saved eleven seconds after the plugin was inserted, before any audio ran: `Blocks 0`. What it
shows is how Samplitude sets the plugin up, and nothing of steps A to H yet.

- It asks about three layouts: main and sidechain both stereo (accepted, and applied for an
  instant), main in without an output (refused), then main stereo with the sidechain disabled,
  which is the one it keeps. So Samplitude knows the bus is there and leaves it inactive.
- One `prepareToPlay`: 48000 Hz, block 2000, float.
- No process call in those eleven seconds, and no play head seen. Whether Samplitude calls the
  plugin at all while stopped is not known from this: the next report, saved while playing, says.
- `getStateInformation` twice, three seconds after loading.

```text
Host       Magix Samplitude (Sam_x64.exe) - VST3 - Windows 11
Prepared   48000 Hz, block 2000 - prepareToPlay x1, releaseResources x0, reset x0
Blocks     0 - zero-length 0 - float 0, double 0 - bypassed 0
Transport  no play head seen
Main bus   Stereo (L R) - LFE: none reported - peak exact zero
Sidechain  disabled - process buffer 0 channels, buses add up to 0
  SEEN AS  NO AUDIO PROCESSED YET - now exact zero, exact zero
           highest ever exact zero, exact zero
Latency    0 samples (lookahead 0 ms) - changed 0 times - prepareToPlay calls since the last change: 1
State      fingerprint 1CC8BDCE - 0 of 284 off default - get x2, set x0
Pointers   most down at once 0 - kinds seen: none yet
Layout     in [Stereo (L R) | disabled] out [Stereo (L R)]
Layouts    3 distinct layouts asked about

13:44:24.796  constructed as VST3 in Magix Samplitude (Sam_x64.exe), Windows 11
13:44:24.796  default layout: in [Stereo (L R) | disabled] out [Stereo (L R)]
13:44:24.798  host asks: in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]  -> yes
13:44:24.798  host asks: in [Stereo (L R) | disabled] out [disabled]  -> no
13:44:24.798  numChannelsChanged: in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
13:44:24.798  processorLayoutsChanged: in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
13:44:24.798  host asks: in [Stereo (L R) | disabled] out [Stereo (L R)]  -> yes
13:44:24.798  numChannelsChanged: in [Stereo (L R) | disabled] out [Stereo (L R)]
13:44:24.798  processorLayoutsChanged: in [Stereo (L R) | disabled] out [Stereo (L R)]
13:44:24.798  prepareToPlay #1: 48000 Hz, block 2000, float, realtime, latency 0; in [Stereo (L R) | disabled] out [Stereo (L R)]
13:44:27.406  getStateInformation x2 (total 2); fingerprint 1CC8BDCE
```
