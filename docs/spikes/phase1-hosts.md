# Phase 1: the host bench

Started 2026-11-17. Devplan Phase 1. Code: `spikes/hostprobe/`, still in the tree because the
matrix is open; it is deleted when the matrix is filled.

**Status: Samplitude is measured (part 8); no other host yet.** The probe is built and validated, the procedure is written,
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
| Samplitude, VST3, Windows | **Yes.** *Side chain input* menu in the plug-in window; on choosing, the host enables the bus as stereo and the buffer has four channels with the key in the last two (-20.0 and -21.4 dB). Mono main does not exist in this host; a mono source not tried | **Bus disabled** until a sidechain is chosen; the buffer then holds the two main channels only (2584 blocks) | **Realigns at the next play start, not during playback.** A change while playing leaves the null broken until stop and start; the host does not restart the plugin by itself | Generic view of eight slots, each a chooser over the parameters; names shown, choices as text, **no units**, continuous values with seven decimals | **Restored exactly**: one `getStateInformation` per save, one `setStateInformation` on opening (6913 bytes, after the first `prepareToPlay`); the sidechain routing comes back with the project | n/a: no mono tracks | **One touch pointer; pinch unreliable**: the first finger arrives as touch #0; a second is never a second pointer; pinch events came once (809 of them) and not at all on a later try |
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

### Samplitude, VST3, Windows 11, 2026-11-17 13:46, session 1 continued (played, nothing routed)

The same instance, after about thirty seconds of noise and music, three lookahead changes and
two presses of Scramble. Saved nine seconds after the transport stopped.

**Measured.**

- **Row 2: the sidechain bus stays disabled and the process buffer has two channels**, the main
  pair, in all 743 blocks. Samplitude never activated the bus by itself.
- **Samplitude calls the plugin only while it plays.** 743 blocks of 2000 samples are 31.0 s, and
  the four stretches between a `prepareToPlay` and the next `releaseResources` add up to about
  30 s. On stop it calls `releaseResources`; on play, `prepareToPlay`, `releaseResources`,
  `prepareToPlay` again, then `reset`. Nothing runs in between. Consequence for later phases: in
  this host a meter or a tracker sees nothing while stopped, and every play starts from
  `prepareToPlay`.
- **The block is always 2000 samples** at 48 kHz, float.
- **Row 3, in part.** The first change (0 to 12 samples) came while the plugin was released; the
  `prepareToPlay` pair 350 ms later has the shape of a play start, not of an answer to the
  change. The next two (12 to 48, 48 to 144) came during playback and no `prepareToPlay`
  followed either. So Samplitude does not restart the plugin on a latency change while playing.
  Whether it re-reads the latency and realigns, then or at the next play, needs the null test of
  step D: the probe cannot hear it.
- **Row 5, not yet.** Scramble gave `38B0C07A` both times, as it should; the project was not
  saved and reopened. `getStateInformation` was not called again after loading.

**Not shown by this report, through the probe's fault.** *Main bus peak exact zero* and
*Transport playing* are what the probe last saw, nine seconds after the last block: it kept no
highest-ever peak for the main input and did not say that process calls had stopped. Both are
added to the probe after this session (*highest ever* on the main bus line, *process calls:
running / none now* on the transport line, and "no process calls at the time" in the latency
log). The latency lines below say "transport playing" for the first change, when the plugin was
in fact released.

```text
Host       Magix Samplitude (Sam_x64.exe) - VST3 - Windows 11
Prepared   48000 Hz, block 2000 - prepareToPlay x8, releaseResources x8, reset x8
Blocks     743 - sizes 2000 to 2000, last 2000 - zero-length 0 - float 743, double 0 - bypassed 0
Transport  playing
Main bus   Stereo (L R) - LFE: none reported - peak exact zero
Sidechain  disabled - process buffer 2 channels, buses add up to 2
  SEEN AS  BUS DISABLED - now exact zero, exact zero
           highest ever exact zero, exact zero
Latency    144 samples (lookahead 3 ms) - changed 3 times - prepareToPlay calls since the last change: 0
State      fingerprint 38B0C07A - 252 of 284 off default - get x2, set x0
Pointers   most down at once 0 - kinds seen: none yet
Layout     in [Stereo (L R) | disabled] out [Stereo (L R)]
Layouts    3 distinct layouts asked about

(the first eleven lines are those of the report above)
13:46:08.669  releaseResources #1
13:46:08.669  prepareToPlay #2: 48000 Hz, block 2000, float, realtime, latency 0; in [Stereo (L R) | disabled] out [Stereo (L R)]
13:46:08.889  process buffer: 2 channels; the enabled buses add up to 2
13:46:08.889  sidechain seen as: bus disabled
13:46:08.889  reset x1 (total 1)
13:46:08.889  transport: playing
13:46:19.382  releaseResources #2
13:46:19.440  prepareToPlay #3: 48000 Hz, block 2000, float, realtime, latency 0; in [Stereo (L R) | disabled] out [Stereo (L R)]
13:46:19.440  releaseResources #3
13:46:19.440  prepareToPlay #4: 48000 Hz, block 2000, float, realtime, latency 0; in [Stereo (L R) | disabled] out [Stereo (L R)]
13:46:19.543  reset x2 (total 3)
13:46:20.743  releaseResources #4
13:46:20.786  prepareToPlay #5: 48000 Hz, block 2000, float, realtime, latency 0; in [Stereo (L R) | disabled] out [Stereo (L R)]
13:46:20.786  releaseResources #5
13:46:20.786  prepareToPlay #6: 48000 Hz, block 2000, float, realtime, latency 0; in [Stereo (L R) | disabled] out [Stereo (L R)]
13:46:20.900  reset x2 (total 5)
13:46:27.951  releaseResources #6
13:46:27.968  reset x1 (total 6)
13:46:31.901  lookahead choice 1 (0.25 ms) set from the probe editor
13:46:31.911  latency reported to the host: 0 -> 12 samples, transport playing
13:46:32.265  prepareToPlay #7: 48000 Hz, block 2000, float, realtime, latency 12; in [Stereo (L R) | disabled] out [Stereo (L R)]
13:46:32.265  releaseResources #7
13:46:32.265  prepareToPlay #8: 48000 Hz, block 2000, float, realtime, latency 12; in [Stereo (L R) | disabled] out [Stereo (L R)]
13:46:32.374  reset x1 (total 7)
13:46:33.230  lookahead choice 2 (1 ms) set from the probe editor
13:46:33.280  latency reported to the host: 12 -> 48 samples, transport playing
13:46:33.891  lookahead choice 3 (3 ms) set from the probe editor
13:46:33.986  latency reported to the host: 48 -> 144 samples, transport playing
13:46:34.813  scrambled from the probe editor: fingerprint 38B0C07A
13:46:39.530  scrambled from the probe editor: fingerprint 38B0C07A
13:46:43.992  releaseResources #8
13:46:44.043  reset x1 (total 8)
```

### Samplitude, VST3, Windows 11, 2026-11-17 13:49, session 1 continued (seven more plays)

The same instance and the same build of the probe as above, so the two gaps named there still
apply. Seven more stretches of playback between 13:47:56 and 13:49:32, at 144 samples of latency
throughout. The author's notes on what was done in them are not in yet.

- **Nothing new on the sidechain:** bus disabled, two channels in the buffer, in all 2584 blocks.
- **The play and stop pattern holds without exception** over fourteen more `prepareToPlay` calls:
  on play, `prepareToPlay`, `releaseResources`, `prepareToPlay`, `reset`; on stop,
  `releaseResources`, `reset`. The 1841 new blocks are 76.7 s and the seven stretches add up to
  76.3 s: no process call outside playback.
- **Every `prepareToPlay` after the changes is made with latency 144**, so the plugin's figure is
  there for the host to read at each play start. Whether Samplitude reads it is still the null
  test's to say.
- **No state call:** `getStateInformation` stays at the two calls made on loading and
  `setStateInformation` at none. If the project was saved in this stretch, Samplitude saved it
  without asking the plugin again; if it was not, this says nothing. To be settled by the author's
  note.

The log's new lines are fourteen repetitions of the pattern above and are not copied here.

### Samplitude Suite 2026.5 (build 26215), VST3, 2026-11-17, from a screenshot and the author's notes

The author: the seven plays above were plays and stops, then moves of the faders of Likyu's
generic view (Likyu, not the probe, in the next slot of the same track); then the project was
saved. Seen on the screenshot:

- **Row 1: Samplitude does route a sidechain to a VST3.** The plug-in window's menu bar has
  *Side chain input*, with *Side chain in*, *Side chain solo*, *Side chain filter…* and then the
  project's other tracks (Track 2, 3, 4). It is offered on Likyu and on the probe alike. Nothing
  was chosen yet, so what the plugin then sees is still to measure.
- **Row 4: the generic view.** A list of eight slots; each slot is a chooser over the plugin's
  parameters, numbered in table order ("01: Bypass", "02: Output Gain", "03: Lookahead",
  "04: Link", "05: Band 1 On", "06: Band 1 Type", "07: Band 1 Frequency", "09: Band 1 Gain"),
  with a slider, the value as text and an *Auto.* tick box.
  - Choices and toggles read as their text: "1 ms", "Unlinked", "On", "Bell".
  - Continuous values read as bare numbers with seven decimals and **no unit**:
    "-0.9600010", "863.0382690", "21.9000015". The unit the plugin gives as the parameter's label
    is not shown. For Phase 2: the plugin should give each parameter its own value-to-text
    (decimals and unit), since at least this host shows that string and nothing else.
  - `lookahead` has an *Auto.* box like the others; whether ticking it does anything for a
    parameter declared not automatable was not tried.
- **Row 5, in part:** the probe's count went from "get x2" to "get x3" with the save. One call
  per save. The fingerprint at the save is `38B0C07A`; the reload is to come.
- The probe's editor draws correctly in Samplitude's plug-in window, on a scaled display.

The project is `D:\Desktop6-11-17.VIP` on the author's machine.

### Samplitude, VST3, Windows 11, 2026-11-17 13:56, session 1 continued (null test, then a sidechain)

The same instance, still the first build of the probe. The author: "Null test requires a start
and stop, but works as expected when the playback is restarted." The project was saved again
after this report.

- **Row 3, answered.** Three changes during playback (144 to 48, 48 to 12, 12 to 0, from
  13:55:31). After each the null is broken until the author stops and starts; the log shows a
  `releaseResources` 0.3 to 0.7 s after each change and the play-start pattern after it, which
  are those stops and starts. Samplitude takes the new latency at the next play start and
  realigns correctly from there. It does not act on the latency-changed notification while
  playing.
- **Row 1, answered for a stereo source on a stereo track.** At 13:56:42, with the plugin
  released, the layout becomes `in [Stereo | Stereo]` (`numChannelsChanged`, no new
  `isBusesLayoutSupported` question), then a play start; the buffer has four channels and the
  probe reads signal on both sidechain channels, highest -20.0 and -21.4 dB. Which entry of the
  *Side chain input* menu did it (a track, or *Side chain in*) is to be noted by the author.
- **Row 2, completed by this:** the bus is disabled until the user chooses a sidechain and
  enabled from then on. In Samplitude "sidechain selected in a band, none chosen in the host" is
  therefore visible to the plugin as a disabled bus, and the PRD's fallback to Self applies as
  written.
- **State.** `getStateInformation` once per save (13:50:32, 13:55:30, 13:56:35, 13:56:49). The
  fingerprint went from `38B0C07A` to `D72C0CAF` and the count off default from 252 to 251 when
  the lookahead came back to Off: lookahead is one of the 284 values. The reload should show
  `D72C0CAF`.
- **A fault of the probe, again:** "sidechain seen as: bus enabled, exact zeros" at 13:56:45 and
  on the *SEEN AS* line is not a measurement. Playback stopped at 13:56:44, no block came after,
  and the probe took a second without signal for zeros. Fixed after this session: the
  classification now moves only while blocks arrive. The *Main bus peak* and *Transport* lines
  are stale for the reason given above.

New lines of the log, the play and stop patterns left out:

```text
13:50:32.665  getStateInformation x1 (total 3); fingerprint 38B0C07A
13:55:30.143  getStateInformation x1 (total 4); fingerprint 38B0C07A
13:55:31.667  lookahead choice 2 (1 ms) set from the probe editor
13:55:31.759  latency reported to the host: 144 -> 48 samples, transport playing
13:55:32.386  releaseResources #24
13:55:32.737  prepareToPlay #25: 48000 Hz, block 2000, float, realtime, latency 48; in [Stereo (L R) | disabled] out [Stereo (L R)]
13:55:33.930  lookahead choice 1 (0.25 ms) set from the probe editor
13:55:33.948  latency reported to the host: 48 -> 12 samples, transport playing
13:55:34.238  releaseResources #26
13:55:34.526  prepareToPlay #27: 48000 Hz, block 2000, float, realtime, latency 12; in [Stereo (L R) | disabled] out [Stereo (L R)]
13:55:36.007  lookahead choice 0 (0 ms) set from the probe editor
13:55:36.089  latency reported to the host: 12 -> 0 samples, transport playing
13:55:36.333  releaseResources #28
13:55:36.654  prepareToPlay #29: 48000 Hz, block 2000, float, realtime, latency 0; in [Stereo (L R) | disabled] out [Stereo (L R)]
13:56:35.739  getStateInformation x1 (total 5); fingerprint D72C0CAF
13:56:42.255  numChannelsChanged: in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
13:56:42.255  processorLayoutsChanged: in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
13:56:42.255  prepareToPlay #31: 48000 Hz, block 2000, float, realtime, latency 0; in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
13:56:42.401  process buffer: 4 channels; the enabled buses add up to 4
13:56:42.401  sidechain seen as: bus enabled, signal
13:56:44.223  releaseResources #32
13:56:45.233  sidechain seen as: bus enabled, exact zeros
13:56:49.551  getStateInformation x1 (total 6); fingerprint D72C0CAF
```

### Samplitude, VST3, Windows 11, 2026-11-17 14:01, session 2 (project reopened)

Samplitude closed, the corrected probe installed, the project reopened. A new instance, on the
probe build that keeps the main input's highest peak and says whether process calls are running.

- **Row 5, answered: the state comes back exactly.** On opening: `prepareToPlay`, then
  `setStateInformation` with 6913 bytes, on the message thread, 100 ms later.
  - The fingerprint reads `BEF24EA3`, 244 off default, where the save read `D72C0CAF`, 251. **That
    difference is the probe's, not the host's.** Scramble had set toggles to values such as 0.58,
    which a JUCE toggle holds as given until it is saved as "on"; the fingerprint hashed the 0.58.
    Run without a host (`likyu_hostprobe_snapshot <folder> --roundtrip`), the same scramble, save
    and load give `D72C0CAF` then `BEF24EA3` with 6913 bytes, and every real value equal. So
    Samplitude returned what it was given.
  - The fingerprint now hashes each value as it survives a save, and reads `BEF24EA3` before and
    after.
- **Row 1, more.** On the screenshot the *Side chain input* menu has both *Side chain in* and
  *Track 3* ticked: a switch and a source. The routing is stored in the project. On opening, the
  bus is still disabled at the first `prepareToPlay`; it is enabled at the first play
  (`numChannelsChanged` to `in [Stereo | Stereo]`, then `prepareToPlay` #2), and signal arrives
  from the first block: -19.9 and -18.7 dB at the highest. **Between opening and the first play
  the plugin sees a disabled bus although a sidechain is routed**: an editor that warns "sidechain
  absent" would warn wrongly there until play is pressed.
- The main input reads up to 0.0 dB, the sidechain about 20 dB under: they are different signals,
  and the sidechain does not share memory with the main input.
- The play pattern on a fresh instance: `releaseResources`, layout, `prepareToPlay`, process,
  `reset`.

```text
Host       Magix Samplitude (Sam_x64.exe) - VST3 - Windows 11
Prepared   48000 Hz, block 2000 - prepareToPlay x2, releaseResources x1, reset x1
Blocks     928 - sizes 2000 to 2000, last 2000 - zero-length 0 - float 928, double 0 - bypassed 0
Transport  playing - process calls: running
Main bus   Stereo (L R) - LFE: none reported - peak -4.2 dB, highest ever 0.0 dB
Sidechain  Stereo (L R) - process buffer 4 channels, buses add up to 4
  SEEN AS  BUS ENABLED, SIGNAL - now -22.5 dB, -25.0 dB
           highest ever -19.9 dB, -18.7 dB
Latency    0 samples (lookahead 0 ms) - changed 0 times - prepareToPlay calls since the last change: 2
State      fingerprint BEF24EA3 - 244 of 284 off default - get x2, set x1
Pointers   most down at once 0 - kinds seen: none yet
Layout     in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
Layouts    3 distinct layouts asked about

14:00:23.256  constructed as VST3 in Magix Samplitude (Sam_x64.exe), Windows 11
14:00:23.256  default layout: in [Stereo (L R) | disabled] out [Stereo (L R)]
14:00:23.258  host asks: in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]  -> yes
14:00:23.258  host asks: in [Stereo (L R) | disabled] out [disabled]  -> no
14:00:23.258  numChannelsChanged: in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
14:00:23.258  processorLayoutsChanged: in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
14:00:23.258  host asks: in [Stereo (L R) | disabled] out [Stereo (L R)]  -> yes
14:00:23.258  numChannelsChanged: in [Stereo (L R) | disabled] out [Stereo (L R)]
14:00:23.258  processorLayoutsChanged: in [Stereo (L R) | disabled] out [Stereo (L R)]
14:00:23.258  prepareToPlay #1: 48000 Hz, block 2000, float, realtime, latency 0; in [Stereo (L R) | disabled] out [Stereo (L R)]
14:00:23.359  setStateInformation #1: 6913 bytes; fingerprint now BEF24EA3, 244 parameters off default
14:00:25.978  getStateInformation x2 (total 2); fingerprint BEF24EA3
14:00:29.715  releaseResources #1
14:00:29.715  numChannelsChanged: in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
14:00:29.715  processorLayoutsChanged: in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
14:00:29.715  prepareToPlay #2: 48000 Hz, block 2000, float, realtime, latency 0; in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
14:00:29.896  process calls: started
14:00:29.896  process buffer: 4 channels; the enabled buses add up to 4
14:00:29.896  sidechain seen as: bus enabled, signal
14:00:29.896  reset x1 (total 1)
14:00:29.896  transport: playing
```

### Samplitude, VST3, Windows 11, 2026-11-17, the author's notes on touch and mono

- **Row 7.** "Not sure I see multitouch gestures in the window in Samplitude. I see touch #1 and
  then for the second finger I can see sometimes a grey dot under it." So one touch reaches the
  plugin's editor and is drawn; a second finger is not delivered as a second pointer. The grey dot
  is not the probe's: the pad draws coloured discs with a label, so it is the system's own touch
  mark. No report was saved for this, so the *Pointers* line (how many at once, and whether the
  first finger came as a touch or as a mouse) is not on file; a saved report would settle the
  second point.
- **Mono.** "I'm not sure Samplitude has mono tracks. You can import mono files, but the only way
  to have mono is to collapse the width of the panner to mono. Only export can be mono." So the
  plugin is always given a stereo main bus here, and "stereo into a mono main" does not exist in
  this host. A mono file as the sidechain source remains possible and was not tried.

### Samplitude, VST3, Windows 11, 2026-11-17, the pointer pad on a screenshot

The pad shows one disc labelled "#0 touch" and, on its last line, "pinch #809 from touch: factor
1.000".

- The first finger arrives as a **touch**, source index 0, not as a mouse.
- **The second finger does reach the editor, as a pinch gesture and not as a pointer**: 809
  `mouseMagnify` calls. So in Samplitude two fingers cannot drag two nodes, but a pinch can set a
  Q, which is the gesture the PRD gives it. This corrects the note above, which read the missing
  second disc as no multitouch at all.
- The factor shown is that of the last event alone and reads 1.000 to three decimals: a pinch
  comes as many factors close to 1. Whether their product follows the fingers was not on the
  screen; the pad now shows the product and the range of the factors as well.

### Samplitude, VST3, Windows 11, 2026-11-17, pinch tried again

The author, on a later try: "No pinch feedback. It still says 'No wheel or pinch yet'." So the
809 pinch events of the screenshot above did not come back. **Pinch reaches the editor in
Samplitude sometimes and not always**, and what makes the difference is not known.

What is known of the mechanism, read from JUCE at the pin
(`juce_gui_basics/native/juce_Windowing_windows.cpp`, `doGestureEvent`, about line 2970): on
Windows a pinch is not built from two touch pointers. It comes from the system's own gesture
message (`WM_GESTURE`, zoom), each event carrying the ratio of the fingers' distance to the
previous one, and it is given to whichever JUCE window is under the mouse position at that
moment. So it depends on which window the system sends the gesture to, the host's or the
plugin's, and on where it believes the pointer is: both are the host's business, and both can
differ from one try to the next (which finger lands first, where the mouse cursor was left).

Untried, and worth one minute in the next host as well: both fingers landing together against one
after the other; and the mouse cursor left over the pad against left elsewhere.

**For Phase 6, as it stands:** in Samplitude neither a second pointer nor a pinch can be counted
on. The wheel and the mouse must do everything there, as the PRD already requires.

