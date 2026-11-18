# Phase 1: the host bench

Started 2026-11-17. Devplan Phase 1. Code: `spikes/hostprobe/`, still in the tree because the
matrix is open; it is deleted when the matrix is filled.

**Status, 2026-11-18 morning: the four Windows hosts are measured in VST3, with the gaps listed in
part 6; findings in part 7, two of them waiting on the author. The validators pass on all three
systems, `lv2_validate` reporting only JUCE's own errors as it has since Phase 0 (part 3), and the probe is installed on
the Mac mini and the NUC. REAPER on the Mac is begun (2026-11-18); Go.dot, LV2 in a host, the
other macOS hosts and Linux are not.** Part 4
says what JUCE's wrappers do as read from their source: it tells what to expect and is not a
measurement.

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
| `scripts/farm-check.sh`: Debug build, ctest under C and fr_FR, Release plugins, pluginval at strictness 10 on the VST3 | Mac mini, macOS 15.8.3, arm64, 2026-11-18, `745aff2` | Pass. No warning in our code |
| `auval -v aufx Lkyu PxBl` | Mac mini, in the logged-in session | **AU VALIDATION SUCCEEDED**, with 18 warnings "Parameter did not retain default value when set", all on logarithmic parameters whose default does not survive the float mapping: the sixteen attacks (5 ms on 0.05 to 200), band 4's frequency (500 Hz, read back 499.99997) and band 2's high frequency (160 Hz). Over SSH the component is not found, as the OSCQuery plugin found (that session talks to another registrar), so the script's first run failed there; it now runs auval in the logged-in session when called over SSH, and passes |
| `scripts/farm-check.sh`, as above | NUC, Ubuntu 24.04.5, GCC 13, the same day and commit | Pass. One warning in our code, `-Wfloat-equal` in `tests/plugin/ProcessorTests.cpp`, corrected in `2e0b465` |
| pluginval with its GUI tests | NUC | Pass: first by hand on the logged-in session's Xwayland (`DISPLAY=:0`), then by the script under `xvfb-run` once the author installed `xvfb` |
| Both LV2 bundles read by lilv | NUC | **Pass**, both listed and `lilv_plugin_verify` true; stereo: two main in, two sidechain in, two out; mono: one, one, one; a latency port on each. Read first through `liblilv` from Python, then by the script's `lv2ls` once the author installed `lilv-utils` |
| `lv2_validate` on both bundles | NUC, at `4c75c3a`, the author having installed `lv2-dev` and `sordi` | **112 errors on each, all in what JUCE's wrapper writes**, as in CI since Phase 0: 108 `lv2:portProperty` on `lv2:Parameter` entries (choices and toggles), which the schema keeps for ports; `doap:maintainer` and `doap:release` on the plugin; and two on `…:Program`, the wrapper's state key for the current program (`juce_audio_plugin_client_LV2.cpp`, line 93), undefined and unlabelled. The probe's bundles give the same. Information only, as the Phase 0 record decided; the LV2 gate is lilv, which passes. `lv2lint` is not packaged on Ubuntu 24.04 |
| The probe: pluginval at strictness 10 on `LikyuProbe.vst3`, auval on `aufx Lkyp PxBl`, lilv on both probe LV2 bundles | Mac mini and NUC, as installed by `spikes/hostprobe/install.sh` | Pass. auval gives the same 18 warnings as the product |

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
| REAPER, VST3, Windows | **Not by REAPER's usual way.** With the track at four channels and a send to 3/4, REAPER makes the plugin's **main bus four channels wide** and the send lands in main channels 3 and 4; the sidechain, enabled after them as channels 5 and 6, stays at exact zeros (part 8). **By dragging the key track's routing onto the plugin's window, the sidechain is fed** (14:55, signal while playing). The project file shows what the gesture did: the plugin's pin map re-wired, sidechain inputs fed from track channels 3/4 (finding 2) | **Two states seen.** As inserted: bus disabled, two channels. Later: **bus enabled, exact zeros**, with nothing reaching it | **Realigns at every setting, also on the fly**, with a slip of sound at the change (the author); no `prepareToPlay` after a change | Not yet | Partial: scrambled (`BEF24EA3`) and saved; not reopened | **A four-channel track is given as "L R C Lfe", so an LFE is reported on channel 4** of a track that has none. Mono, 5.1 and wide tracks not yet; on loading REAPER asks for 64 channels and is told yes | **Several fingers, once the editor asks for multitouch** (the author, 2026-11-17). Without the request: the first finger only |
| REAPER, LV2, Windows |  |  |  |  |  |  |  |
| Bitwig, VST3, Windows | **Yes**: a source chosen on the device enables the bus as stereo; four channels, signal on both sidechain channels (-10.1 dB), zeros when the source is silent | **Bus disabled** until a source is chosen; two channels | **Realigns on the fly**, a short burst at the change (the author). Bitwig **restarts the plugin** for every change: `releaseResources` and `prepareToPlay`, twice, within 40 ms | Not yet | State given back at construction (6913 bytes, `BEF24EA3`) in a fresh project: passed from an earlier instance to a new one, whole. Save and reopen not yet done | n/a | **Several fingers, once the editor asks for multitouch** (the author, 2026-11-17). Without the request: the first finger only |
| Live 12, VST3, Windows | **Yes**: a source chosen on the device enables the bus as stereo; four channels, signal on both (-0.2 dB) | **Bus disabled** as inserted. **After a source has been chosen and removed again the bus stays enabled, at exact zeros** | **Realigns on the fly**, a small slip at the change (the author). Live restarts the plugin once for every change | **None shown until configured.** With *Configure* on, one press of Scramble in the probe brings all the parameters into Live's panel (the author). The clip's envelope chooser then lists them by name in table order, **one flat list of some 280 entries**, no groups, no search; `lookahead` and `link` are rightly absent, `bypass` is Live's own "Device On". Units not seen | Saved and reloaded: `setStateInformation` once, after the first `prepareToPlay`; the values were the defaults, so the round trip is shown but not tested | n/a | **Several fingers, once the editor asks for multitouch** (the author, 2026-11-17). Without the request: one at a time |
| Samplitude, VST3, Windows | **Yes.** *Side chain input* menu in the plug-in window; on choosing, the host enables the bus as stereo and the buffer has four channels with the key in the last two (-20.0 and -21.4 dB). Mono main does not exist in this host; a mono source not tried | **Bus disabled** until a sidechain is chosen; the buffer then holds the two main channels only (2584 blocks) | **Realigns at the next play start, not during playback.** A change while playing leaves the null broken until stop and start; the host does not restart the plugin by itself | Generic view of eight slots, each a chooser over the parameters; names shown, choices as text, **no units**, continuous values with seven decimals | **Restored exactly**: one `getStateInformation` per save, one `setStateInformation` on opening (6913 bytes, after the first `prepareToPlay`); the sidechain routing comes back with the project | n/a: no mono tracks | **Several fingers, once the editor asks for multitouch** (the author, 2026-11-17). Without the request: the first finger only |
| Go.dot, VST3, Windows | n/a |  |  |  |  |  |  |
| Go.dot, LV2, Windows | n/a |  |  |  |  |  |  |
| REAPER, VST3, macOS | **Yes, on a four-channel track**: main bus four wide (L R C Lfe), sidechain enabled after it as channels 5 and 6, signal on both (-3.7 dB) while playing, exact zeros when stopped. Routed as on Windows, a send to 3/4, then **the pin connector re-wired by hand**: sidechain inputs fed from track channels 3/4, main inputs 3/4 from nothing (read from the project file; finding 2) | **Bus disabled** as inserted, two channels | **Realigns on the fly**, a small sliver at the change, no restart needed (the author). Ten changes while playing: latency reported within 10 to 70 ms each time, no `prepareToPlay` after any | Not yet in VST3 (in AU, see the next line) | **Restored exactly**: the project reopened at 15:27 gives `BEF24EA3`, 244 off default, the same code Scramble gives on Windows; `setStateInformation` (7008 bytes) just after the first `prepareToPlay`; the four-channel layout and the enabled sidechain come back with it | **A four-channel track is given as "L R C Lfe", LFE on channel 4**, as on Windows; on loading REAPER asks for 64 channels and is told yes, as on Windows. **No mono track:** a mono clip, or a mono master, leaves the plugin's main bus stereo (the author); a REAPER track has two channels at least. **A six-channel track is "5.1 Surround (L R C Lfe Ls Rs)", LFE on channel 4**, a 5.1 main with the sidechain disabled. Wide not yet | n/a; the author: "doesn't work out of the box" |
| REAPER, AU, macOS | **Not run on any four-channel track tried (three)**: 0 blocks every time. The track is silent behind a bypassed plugin and passes its input untouched with the AU alone (the author: "now works"). REAPER's second `AudioUnitInitialize` fails, and every render after it is refused by Apple's AU base class with -10867, `kAudioUnitErr_Uninitialized`: 77,321 times in the system log, all from the probe, none reaching our code. Reproduced outside REAPER: an input and an output of **unequal widths** make the initialise fail exactly so, and REAPER's pin map shows it gives four in and two out (finding 9). **On a two-channel track the AU runs** (16:10: 1150 blocks, signal) |  |  | **Likyu shows all its parameters, and REAPER's automation list has them all** (the author). Generic view: a label, then a slider with the value at its end or a dropdown; **no units** |  |  | n/a |
| REAPER, LV2, macOS |  |  |  |  |  |  | n/a |
| Live, VST3, macOS | **Yes**: a source chosen on the device enables the bus as stereo; four channels, signal on both (-12.6 and -13.8 dB), exact zeros while the source is silent | **Bus disabled** as inserted, two channels | **Realigns on the fly**; the null passes as in REAPER (the author). Live **restarts the plugin** for each of eight changes (`releaseResources`, `prepareToPlay` with the new latency), as on Windows | Scramble brings the parameters into Live's automation (the author), as on Windows | **Restored exactly**: saved after Scramble, the set reopened gives `setStateInformation` (7008 bytes) just after the first `prepareToPlay`, `BEF24EA3`, 244 off default; the sidechain routing comes back with it, enabled a second after loading | n/a | n/a |
| Live, AU, macOS | **Yes**: a source chosen on the device feeds the AU's sidechain, signal -12.6 and -13.8 dB. The AU runs: 1885 blocks, float, 44.1 kHz, 512 | **Enabled, exact zeros** from the first block, through some eight seconds of playback before the source: in AU the bus is never disabled (part 4, now measured). **Source removed again: enabled, exact zeros** while playing (16:50:35) | **Realigns on the fly**, a small chunk at the change, then the null again (the author). **No restart**: three changes during playback, latency reported within 25 to 70 ms, no `prepareToPlay` after any, where Live in VST3 restarts the plugin each time |  | **Restored exactly**: scrambled, saved, reopened; `setStateInformation` (7008 bytes) just after the first `prepareToPlay`, `BEF24EA3`, 244 off default | n/a | n/a |
| Bitwig, VST3, macOS |  |  |  |  |  | n/a | n/a |
| Go.dot, AU, macOS | n/a |  |  |  |  |  | n/a |
| REAPER, VST3, Linux |  |  |  |  |  |  |  |
| REAPER, LV2, Linux |  |  |  |  |  |  |  |

Go.dot's sidechain is not benched (row 1): it feeds that bus silence until devplan Phase 9.

## 6. What is still empty, and who can fill it

On Windows, VST3, small things left in hosts already measured (the author, a few minutes each):

- **REAPER on Windows:** the project reopened after Scramble; mono, 5.1 and wide tracks. (The
  layout and pin map of a working sidechain, and the fixed-stereo control's result, are on file
  from the Mac since 2026-11-18: finding 2.)
- **Bitwig and Live:** save and reopen with scrambled values (Live's reload was made with the
  defaults; Bitwig's state passed between two instances of a fresh project). Live in VST3 on the
  Mac restores a scrambled state exactly (2026-11-18), which makes a fault on Windows unlikely.
- **Live:** *Sidechain On* with no source; what *Input/Sidechain Mix* does to the key.
- **Samplitude:** *Side chain in* ticked with no track chosen; a mono file as the source.
- **Units** in the hosts' own parameter views: none shown in Samplitude (VST3, Windows) nor in
  REAPER (AU, macOS); the other hosts not yet looked at.

Not begun:

- **LV2 in REAPER on Windows** (the bundles are installed in `%APPDATA%\LV2`).
- **Go.dot**, which needs a build that hosts the three formats.
- **macOS and Linux**: the author at the Mac mini and the NUC, or over a remote desktop. An agent
  cannot: each needs a host's window. Row 7 on Linux needs a touch screen there. The probe, its
  mono LV2, the fixed-stereo control and the product are installed in the user's plugin folders
  on both machines since 2026-11-18 (`spikes/hostprobe/install.sh`, built from `745aff2`).

## 7. Findings

From four hosts on Windows, VST3 only, 2026-11-17. Provisional until the other formats and
systems are in. Each says what was measured and what it bears on; **none is a decision**, and
the two marked so wait for the author.

1. **The sidechain fallback covers the first insertion and little else.** All four hosts insert
   the plugin with the sidechain bus **disabled** and a buffer of the main channels only, so the
   PRD's fallback to Self applies to a plugin nobody has routed anything to. But "disabled" is
   not the same as "nothing routed":
   - Live keeps the bus enabled, at exact zeros, after a source has been chosen and removed.
   - REAPER showed the bus enabled at exact zeros with no send at all, and feeds it zeros
     whenever it is stopped.
   - Bitwig and Live feed exact zeros while the chosen source is silent.
   - Samplitude shows a disabled bus between opening a project and the first play although a
     sidechain is routed.

   In every host an enabled bus at exact zeros is an ordinary state. The PRD already treats it as
   a key, so the rule stands on this evidence; what the editor says in that state ("sidechain
   silent"?) and in Samplitude's gap is a question for Phase 6. The prediction of part 4, that
   only AU and LV2 lack a disabled state, was too kind to VST3. **Its AU half is measured**
   (Live, macOS, 2026-11-18): the AU's sidechain is enabled at exact zeros from the first block,
   before any source is chosen, so in AU the fallback to Self never happens and a Sidechain band
   with nothing routed is keyed by silence.
2. **For the author: in REAPER a hand-made send to channels 3/4 feeds the main bus, not the
   sidechain.** The plugin accepts any main layout up to 64 channels with output equal to input
   (PRD, non-goals). On a four-channel track REAPER asks for a four-channel main bus, is told
   yes, and adds the sidechain after it as channels 5 and 6; the send arrives as main channels 3
   and 4 and is processed as audio, and the sidechain reads zeros. Dragging the key track's
   routing onto the plugin's window does feed the sidechain, so REAPER has a way that works.
   **Why, read from the two projects' files (2026-11-18):** REAPER saves each plugin's pin map
   at the head of its state. Its default is one to one, plugin input *k* fed from track channel
   *k*. ReaComp, whose main is fixed at stereo, has four inputs, fed from channels 1 to 4, so a
   send to 3/4 keys it with no further step. The probe, given a four-channel main, has six
   inputs; in both projects, after the author's gesture (on Windows) and re-wiring in the pin
   connector (on the Mac), inputs 3 and 4 (main C and LFE) are fed from nothing and inputs 5 and
   6 (the sidechain) from track channels 3 and 4 (and 5 and 6, which a four-channel track does
   not have: the default's remainder). So the plugin's acceptance of any width costs a REAPER
   user one step in the pin connector, every time, that a fixed-stereo plugin does not.
   **The fixed-stereo control confirms it** (macOS, 2026-11-18, 16:17): on the same four-channel
   track with the same send, pin connector untouched, REAPER wires its inputs one to one, the
   sidechain from track channels 3 and 4, and the sidechain reads the send (-19.3 dB). So the PRD's
   "any layout, chosen by the host" and its reference use case pull against each other in
   REAPER, for a user who routes by hand: any width costs the pin-connector step, a fixed stereo
   main does not. Finding 9 is the same rule in AU.
3. **Decided by the author, 2026-11-18: every channel starts in the link; the LFE is left out by
   hand** (PRD, amended). What led there: **REAPER names a four-channel track "L R C Lfe".** The host thus "reports an
   LFE" on channel 4 of a track that is two stereo pairs or a quad, and the PRD's default (a
   channel reported as LFE is left out of the link) would leave that channel out wrongly. A
   six-channel track is "5.1 Surround (L R C Lfe Ls Rs)", LFE on channel 4 (macOS, 2026-11-18),
   right for the 5.1 file it held. A REAPER track stores a channel count and nothing of its
   content (`NCHAN` in the project), so the name follows the count alone: a six-channel track of
   three stereo pairs would be named 5.1 as well. Inferred from the project format, not measured.
4. **Latency.** REAPER, Bitwig and Live realign on the fly, with a slip of sound at the change;
   Samplitude only at the next play start. Bitwig and Live restart the plugin (`prepareToPlay`)
   for every change in VST3; REAPER and Samplitude do not, nor does Live in AU (macOS,
   2026-11-18), the same host restarting the VST3 and not the AU. So the product can count neither on a
   `prepareToPlay` after a lookahead change nor on its absence: the change of delay must be clean
   by itself (devplan Phase 4). Part of the slip heard today is ours, the delay jumping between
   two blocks.
5. **Touch: the one-finger limit was ours.** JUCE 9 leaves multitouch off on Windows unless the
   editor asks ("Multi-touch is now disabled on Windows by default",
   `ThirdParty/JUCE/BREAKING_CHANGES.md`): a plugin's editor overrides `usesWindowsMultiTouch()`.
   Without the request, all four hosts and a window with no host delivered the first finger only,
   and the system's pinch now and then. With it, **all four hosts pass several fingers to the
   editor**, four at once seen, and no pinch event comes any more: the two exclude each other.
   For Phase 6: the editor asks for multitouch and computes its own pinch from two touches on a
   node. The PRD's multitouch paragraph stands as written.
6. **Parameters in the hosts' own views.**
   - Samplitude shows names, choices as text, continuous values as bare numbers with seven
     decimals and **no unit**: the plugin should give each parameter its own value text
     (Phase 2).
   - Live lists **no parameter until the plugin is configured**, and adds one when it sees it
     move from the plugin's side. Likyu has no editor before Phase 6, so by itself it gives a
     Live user nothing to move; the probe's Scramble brought all of them in at once. Once in,
     they are one flat list of some 280 names in table order, four screens long, with no groups
     and no search; `lookahead` and `link` are rightly absent. Live's closed chooser cuts a name
     after about nineteen characters.
   - Live adds three controls of its own to a plugin with a sidechain input, one of them
     "Input/Sidechain Mix": the key a band reads in Live may be a blend made by the host.
7. **How the hosts run the plugin.** For the tests of Phases 2 to 5 to cover:

   | Host | Blocks at 48 kHz | Precision | While stopped | On play |
   | --- | --- | --- | --- | --- |
   | Samplitude | 2000 | float | not called at all; `releaseResources` on stop | `prepareToPlay` twice, `reset` |
   | REAPER | 128 | **double** | called, then not after about ten seconds of silence | `reset` |
   | Bitwig | 480 | float | not called after about two seconds | none seen |
   | Live | 128, 256 | float | called | none seen |

   REAPER also asks, on loading, for a 64-channel main bus (7th order Ambisonics), is told yes,
   and applies it for an instant before settling on stereo.
8. **State.** Samplitude gave back a scrambled state exactly. Live gave back a state of defaults;
   Bitwig passed a scrambled state between two instances. `setStateInformation` arrives just
   after the first `prepareToPlay` in Samplitude and Live, and before any layout or
   `prepareToPlay` in Bitwig: the product must take a state at either moment.
9. **For the author: in REAPER on macOS the AU is silent on a four-channel track.** REAPER
   initialises the AU, uninitialises it, and its second initialise fails; it then calls render
   on a unit that is not initialised, and Apple's AU base class answers each call with -10867
   (`kAudioUnitErr_Uninitialized`) before our code runs. REAPER says nothing, and what the track gives depends on the chain:
   silence behind a bypassed plugin, the input untouched when the AU is alone, so by ear a
   failed Likyu cannot be told from a working one. The macOS log holds 77,321 such errors from the probe between 15:30 and 15:53 on
   2026-11-18. In a minimal host (part 8) the same failure follows from one cause only: an input
   and an output of different widths. The wrapper accepts each width as it is set, and the
   initialise then refuses the pair with -10868 (`kAudioUnitErr_FormatNotSupported`), because
   the plugin requires output equal to input (PRD, non-goals). Equal widths, 1, 2 or 4, work. **REAPER widens the input and not the output:** its saved pin map for the AU on each
   four-channel track has six inputs (main four, sidechain two) and two outputs, the pair the
   minimal host shows failing. With track 1 set back to two channels (16:10), the map has four
   inputs and two outputs, the initialise succeeds and the AU runs: 1150 blocks, signal at
   -20.9 dB, and the error rate falls by one instance's worth.
   The AU advertises `{-1, -1}` in `SupportedNumChannels`, which Apple defines as any width with
   input equal to output. It is the same rule as finding 2 meeting REAPER in another format, and
   it bears on the same open decision.

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

### Samplitude, VST3, Windows 11, 2026-11-17, touch: only the first finger

The author, on the current build of the probe: "If I place 1 finger I see the red dot #0 touch.
Then other fingers don't register as grey dots anymore since the last version. If I release #0
then I won't see it again unless I lift all fingers off the screen."

- **Only the first finger down is delivered to the editor**, as touch #0. Fingers that land
  after it are not delivered at all, and when the first is lifted while others stay down, none
  of those takes its place: a new touch arrives only after every finger has left the screen.
  This is the system's "primary pointer" and nothing more.
- The grey dots seen earlier under a second finger were not drawn by the probe (the pad draws
  coloured, labelled discs, and its drawing of pointers did not change between builds; only the
  pinch line did). Why they no longer appear is not known.
- No pinch on this try either.

Row 7 for Samplitude is settled as far as it can be: one touch, used as a mouse would be.

### REAPER, Windows 11, 2026-11-17, touch (the author's note)

"In Reaper same for touch as in Samplitude": the first finger down reaches the editor, the others
do not. Two hosts by two makers behaving alike points at what they share, which is how a Windows
host embeds a plugin's window and how JUCE at this pin receives touches inside it, rather than at
either host. Not established here; Bitwig, which is built for touch screens, is the host that can
tell the two apart.

### REAPER, VST3, Windows 11, 2026-11-17 14:33, session 1 (nothing routed)

Project `D:\Desktop\test Likyu1.rpp`: the probe on a two-channel track, three other tracks, no
send between them. The session ran at 48 kHz.

**Measured.**

- **Row 2, and the finding of the day: REAPER can show the plugin an enabled sidechain bus with
  nothing in it.** As inserted (14:23:41) the bus is disabled and the buffer has two channels. At
  14:27:44 REAPER releases the plugin, enables the bus as stereo and prepares it again; from then
  on the buffer has four channels and the sidechain reads **exact zeros**, in every block, with
  no send to the track and the track still at two channels in the saved project. What the author
  did at that moment is to be noted (opening the plug-in pin connector, a routing window, the
  track's channel count put up and back). Whatever it was, a sidechain that nobody feeds is here
  enabled and silent, the case the PRD's rule treats as a key: a Sidechain band would be keyed by
  silence and would **not** fall back to Self.
- **How REAPER sets the plugin up.** It first asks for 64 channels in and out (7th order
  Ambisonics), is told yes, and applies it for an instant; then stereo with the sidechain
  enabled, one `prepareToPlay`; then stereo with the sidechain disabled, a second. Nine layouts
  asked about in all. **Double precision**, blocks of 128.
- **REAPER calls the plugin while stopped**, unlike Samplitude, and stops calling after about
  ten seconds without signal (14:23:44 to 14:23:56, and again later), starting again when
  something plays. `reset` at each start.
- **Row 3, in part.** Five latency changes, four with the transport stopped and one while
  playing. None was followed by `prepareToPlay` or `releaseResources`: REAPER does not restart
  the plugin for a latency change. Whether it realigns needs the null test, not yet reported.
- **Row 5, in part.** `getStateInformation` seven times, the fingerprint that of the defaults
  (`1CC8BDCE`): the project was saved without Scramble, and not reopened.
- **Row 7.** "Most down at once 1 - kinds seen: mouse, touch": a finger arrives as a touch, one
  at a time, as the author said.
- The main input reached -2.7 dB.

**The probe, again.** The 64-channel layouts were logged with all their speaker names, eight
lines of them filling the log; a layout of more than eight channels is now logged by name and
size.

The log, the Ambisonics lines shortened by hand and the `processorLayoutsChanged` lines, which
repeat the line above them, left out:

```text
14:23:41.613  constructed as VST3 in Reaper (REAPER.exe), Windows 11
14:23:41.613  default layout: in [Stereo (L R) | disabled] out [Stereo (L R)]
14:23:41.613  host asks: in [7th Order Ambisonics | 7th Order Ambisonics] out [7th Order Ambisonics]  -> no
14:23:41.613  host asks: in [Stereo (L R) | disabled] out [Stereo (L R)]  -> yes
14:23:41.613  host asks: in [Stereo (L R) | 7th Order Ambisonics] out [Stereo (L R)]  -> no
14:23:41.613  host asks: in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]  -> yes
14:23:41.613  host asks: in [7th Order Ambisonics | Stereo (L R)] out [Stereo (L R)]  -> no
14:23:41.614  host asks: in [7th Order Ambisonics | Stereo (L R)] out [7th Order Ambisonics]  -> yes
14:23:41.614  host asks: in [7th Order Ambisonics | disabled] out [7th Order Ambisonics]  -> yes
14:23:41.614  numChannelsChanged: in [7th Order Ambisonics | disabled] out [7th Order Ambisonics]
14:23:41.614  numChannelsChanged: in [Stereo (L R) | disabled] out [Stereo (L R)]
14:23:41.615  host asks: in [Stereo (L R) | disabled] out [disabled]  -> no
14:23:41.615  numChannelsChanged: in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
14:23:41.615  prepareToPlay #1: 48000 Hz, block 128, double, realtime, latency 0; in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
14:23:41.616  releaseResources #1
14:23:41.616  host asks: in [disabled | disabled] out [disabled]  -> no
14:23:41.616  numChannelsChanged: in [Stereo (L R) | disabled] out [Stereo (L R)]
14:23:41.616  prepareToPlay #2: 48000 Hz, block 128, double, realtime, latency 0; in [Stereo (L R) | disabled] out [Stereo (L R)]
14:23:44.285  process calls: started
14:23:44.285  process buffer: 2 channels; the enabled buses add up to 2
14:23:44.285  sidechain seen as: bus disabled
14:23:44.285  reset x1 (total 1)
14:23:44.285  getStateInformation x3 (total 3); fingerprint 1CC8BDCE
14:23:44.285  transport: stopped
14:23:47.723  a touch reached the editor
14:23:56.562  process calls: stopped
14:24:54.959  process calls: started
14:24:54.959  reset x1 (total 2)
14:25:04.165  process calls: stopped
14:25:25.633  process calls: started
14:25:25.633  reset x1 (total 3)
14:26:06.460  getStateInformation x1 (total 4); fingerprint 1CC8BDCE
14:27:44.010  releaseResources #2
14:27:44.010  numChannelsChanged: in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
14:27:44.010  prepareToPlay #3: 48000 Hz, block 128, double, realtime, latency 0; in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
14:27:44.058  process buffer: 4 channels; the enabled buses add up to 4
14:27:44.058  sidechain seen as: bus enabled, exact zeros
14:27:44.058  reset x1 (total 4)
14:28:03.862  getStateInformation x1 (total 5); fingerprint 1CC8BDCE
14:28:16.864  getStateInformation x1 (total 6); fingerprint 1CC8BDCE
14:29:30.518  reset x1 (total 5)
14:29:30.518  transport: playing
14:29:33.039  transport: stopped
14:29:37.259  lookahead choice 1 (0.25 ms) set from the probe editor
14:29:37.280  latency reported to the host: 0 -> 12 samples, transport not playing
14:29:37.789  reset x1 (total 6)
14:29:37.888  transport: playing
14:29:39.504  transport: stopped
14:29:40.720  lookahead choice 2 (1 ms) set from the probe editor
14:29:40.819  latency reported to the host: 12 -> 48 samples, transport not playing
14:29:41.120  reset x1 (total 7)
14:29:41.120  transport: playing
14:29:42.531  transport: stopped
14:29:44.572  lookahead choice 3 (3 ms) set from the probe editor
14:29:44.653  latency reported to the host: 48 -> 144 samples, transport not playing
14:29:45.463  reset x1 (total 8)
14:29:45.463  transport: playing
14:29:47.257  lookahead choice 2 (1 ms) set from the probe editor
14:29:47.284  latency reported to the host: 144 -> 48 samples, transport playing
14:29:49.002  transport: stopped
14:29:50.081  lookahead choice 2 (1 ms) set from the probe editor
14:29:51.615  lookahead choice 0 (0 ms) set from the probe editor
14:29:51.629  latency reported to the host: 48 -> 0 samples, transport not playing
14:29:57.218  process calls: stopped
14:32:21.890  process calls: started
14:32:21.890  reset x1 (total 9)
14:32:27.128  process calls: stopped
14:32:32.024  reset x1 (total 10)
14:32:32.119  process calls: started
14:33:12.412  getStateInformation x1 (total 7); fingerprint 1CC8BDCE
14:33:15.053  process calls: stopped
14:33:40.300  process calls: started
14:33:40.300  reset x1 (total 11)
```

### REAPER, VST3, Windows 11, 2026-11-17, the null test (the author's note)

"Null test was successful at any lookahead setting." With the log above: REAPER re-reads the
latency after each change and realigns, and does so without `prepareToPlay` or `releaseResources`.
Four of the five changes were made with the transport stopped and one while playing (144 to 48
samples at 14:29:47, playback stopping two seconds later); the note does not say whether the null
held across that one without a stop, which is where Samplitude failed.

### REAPER, VST3, Windows 11, 2026-11-17 14:39 and 14:42, session 2 (scramble, then four channels and a send)

A new instance after REAPER was restarted, on the probe build that logs wide layouts by name and
size. Two reports of the same instance. The author: "With Reaper changing the look ahead on the
fly did adjust with a slip of sound getting through while changing. Scrambled the values." and,
of the second report, "After the Sidechain".

- **Row 3, answered.** REAPER realigns after a lookahead change made during playback, without a
  stop; something is heard at the moment of the change. (The plugin's delay jumps between two
  blocks without a crossfade, as the Phase 0 handoff says, so part of that slip may be ours.)
- **Row 5, in part.** Scramble gives `BEF24EA3`, 244 off default; `getStateInformation` follows
  within half a second and again at each save. No `setStateInformation`: the project has not been
  reopened with the scrambled values.
- **Row 1: the send did not reach the sidechain.** At 14:41:18, when the track was given four
  channels:
  - REAPER asks for `in [L R C Lfe | L R C Lfe] out [L R C Lfe]` (refused: the sidechain is mono
    or stereo), `in [Stereo | L R C Lfe] out [Stereo]` (refused), `in [L R C Lfe | Stereo] out
    [Stereo]` (refused: output must equal input), then `in [L R C Lfe | Stereo] out [L R C Lfe]`,
    which the plugin accepts, as it must by the PRD.
  - It then settles on a **four-channel main bus** with the sidechain disabled (buffer of four
    channels), and at 14:41:56 enables the sidechain: six channels, the sidechain being the last
    two, at **exact zeros** through nine seconds of playback.
  - The main input's highest peak rises to +1.3 dB: the send is in the main bus.
  - It never asked for `in [Stereo | Stereo] out [Stereo]` on the four-channel track, the layout
    that would have put channels 3/4 on the sidechain.
- **Row 6, a first answer.** The four-channel layout REAPER gives has the speakers L, R, C and
  Lfe; JUCE has no name for it ("Unknown"). The probe's *LFE* line reads "channel 4".
- Sixteen distinct layouts asked about by the end.

The second report's status lines, and the log from the change of width:

```text
Host       Reaper (reaper.exe) - VST3 - Windows 11
Prepared   48000 Hz, block 128 - prepareToPlay x4, releaseResources x3, reset x7
Blocks     58198 - sizes 128 to 128, last 128 - zero-length 0 - float 0, double 58198 - bypassed 0
Transport  stopped - process calls: running
Main bus   Unknown (L R C Lfe) - LFE: channel 4 - peak exact zero, highest ever 1.3 dB
Sidechain  Stereo (L R) - process buffer 6 channels, buses add up to 6
  SEEN AS  BUS ENABLED, EXACT ZEROS - now exact zero, exact zero
           highest ever exact zero, exact zero
Latency    0 samples (lookahead 0 ms) - changed 0 times - prepareToPlay calls since the last change: 4
State      fingerprint BEF24EA3 - 244 of 284 off default - get x6, set x0
Pointers   most down at once 0 - kinds seen: none yet
Layout     in [Unknown (L R C Lfe) | Stereo (L R)] out [Unknown (L R C Lfe)]
Layouts    16 distinct layouts asked about

14:38:59.173  scrambled from the probe editor: fingerprint BEF24EA3
14:38:59.644  getStateInformation x1 (total 3); fingerprint BEF24EA3
14:39:18.262  getStateInformation x1 (total 4); fingerprint BEF24EA3
14:41:18.708  releaseResources #2
14:41:18.708  host asks: in [disabled | disabled] out [Stereo (L R)]  -> no
14:41:18.708  numChannelsChanged: in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
14:41:18.708  host asks: in [Unknown (L R C Lfe) | Unknown (L R C Lfe)] out [Unknown (L R C Lfe)]  -> no
14:41:18.708  host asks: in [Stereo (L R) | Unknown (L R C Lfe)] out [Stereo (L R)]  -> no
14:41:18.708  host asks: in [Unknown (L R C Lfe) | Stereo (L R)] out [Stereo (L R)]  -> no
14:41:18.708  host asks: in [Unknown (L R C Lfe) | Stereo (L R)] out [Unknown (L R C Lfe)]  -> yes
14:41:18.708  numChannelsChanged: in [Unknown (L R C Lfe) | Stereo (L R)] out [Unknown (L R C Lfe)]
14:41:18.708  host asks: in [Unknown (L R C Lfe) | disabled] out [disabled]  -> no
14:41:18.708  host asks: in [Unknown (L R C Lfe) | disabled] out [Unknown (L R C Lfe)]  -> yes
14:41:18.708  numChannelsChanged: in [Unknown (L R C Lfe) | disabled] out [Unknown (L R C Lfe)]
14:41:18.708  prepareToPlay #3: 48000 Hz, block 128, double, realtime, latency 0; in [Unknown (L R C Lfe) | disabled] out [Unknown (L R C Lfe)]
14:41:18.774  process buffer: 4 channels; the enabled buses add up to 4
14:41:18.774  reset x1 (total 5)
14:41:48.557  getStateInformation x1 (total 5); fingerprint BEF24EA3
14:41:56.378  releaseResources #3
14:41:56.379  numChannelsChanged: in [Unknown (L R C Lfe) | Stereo (L R)] out [Unknown (L R C Lfe)]
14:41:56.379  prepareToPlay #4: 48000 Hz, block 128, double, realtime, latency 0; in [Unknown (L R C Lfe) | Stereo (L R)] out [Unknown (L R C Lfe)]
14:41:56.437  process buffer: 6 channels; the enabled buses add up to 6
14:41:56.437  sidechain seen as: bus enabled, exact zeros
14:41:56.437  reset x1 (total 6)
14:42:15.409  getStateInformation x1 (total 6); fingerprint BEF24EA3
14:42:17.227  reset x1 (total 7)
14:42:17.227  transport: playing
14:42:26.506  transport: stopped
```

### REAPER, VST3, Windows 11, 2026-11-17 14:55, the sidechain fed (a fragment of the log)

The author: "I figured it out. It was from the hidden routing section to the plugin." That is,
the drag starts from the key track's routing control and ends on the plugin's window. The
fingerprint `BEF24EA3` before the Scramble of 14:55:50 says this is the instance of session 2, the
probe that accepts any main width, not the fixed-stereo control.

```text
14:55:30.960  sidechain seen as: bus enabled, signal
14:55:30.960  transport: playing
14:55:34.496  transport: stopped
14:55:35.502  sidechain seen as: bus enabled, exact zeros
14:55:53.380  sidechain seen as: bus enabled, signal
14:55:53.380  transport: playing
14:55:58.526  transport: stopped
14:55:59.538  sidechain seen as: bus enabled, exact zeros
```

- **Row 1: the sidechain receives signal in REAPER**, while the key track plays.
- The zeros after each stop are real this time: REAPER goes on calling the plugin when stopped,
  with nothing in the sidechain. So a stopped REAPER shows an enabled, silent sidechain, which is
  again the case the PRD treats as a key.
- **Not in the fragment:** the status lines. Whether the main bus is two or four channels wide
  here, how many channels the buffer has, and the levels, need a saved report. That decides
  whether finding 1 of part 7 is a matter of which REAPER gesture is used, or stands.

### Bitwig Studio, VST3, Windows 11, 2026-11-17 18:09, session 1

The author: polarity inverted with a third-party plugin on the second track; the null "works
like in Reaper. Only a short burst of sound gets through when changing"; the sidechain set up
from track 2. The probe is the current build.

- **Bitwig runs the plugin in a process of its own** (`BitwigPluginHost-X64-AVX2.exe`). 48 kHz,
  **blocks of 480**, float. Three layouts asked about, the same three as Samplitude.
- **Row 2.** As inserted: bus disabled, two channels in the buffer, through the whole of the
  lookahead test.
- **Row 1.** At 18:08:21 the author chooses a sidechain source: the bus becomes stereo, the
  plugin is prepared twice, the buffer has four channels. The sidechain then reads signal,
  -10.1 dB at the highest and **the same on both channels** (a mono source, or one sent alike to
  both), and exact zeros in the stretches where the source is silent: three passages from one to
  the other in forty seconds. So here too an enabled bus at exact zeros is an ordinary state, a
  pause in the key.
- **Row 3, answered.** Eleven changes during playback. For each, within 30 to 40 ms of the
  button, Bitwig releases the plugin and prepares it twice with the new latency; the null holds
  again after a short burst. Bitwig is the first host here that answers the latency-changed
  notification by restarting the plugin.
- **Bitwig stops calling the plugin about two seconds after the transport stops** (`reset`, then
  no more blocks) and starts again with it or when the editor is used.
- **Row 5, in part.** `setStateInformation` came first of all, 9 ms after construction and before
  any layout question or `prepareToPlay`: 6913 bytes, fingerprint `BEF24EA3`, the scramble's. So
  this instance was given a state saved earlier (a project reopened, or the plugin process
  restarted by Bitwig): which, the author is to say. Either way the values came back whole.
- **Row 7, not yet:** no pointer reached the pad in this session.

The log, the eleven lookahead changes reduced to the first:

```text
15:07:43.531  constructed as VST3 in Bitwig Studio (BitwigPluginHost-X64-AVX2.exe), Windows 11
15:07:43.531  default layout: in [Stereo (L R) | disabled] out [Stereo (L R)]
15:07:43.540  setStateInformation #1: 6913 bytes; fingerprint now BEF24EA3, 244 parameters off default
15:07:43.540  host asks: in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]  -> yes
15:07:43.540  host asks: in [Stereo (L R) | disabled] out [disabled]  -> no
15:07:43.540  numChannelsChanged: in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
15:07:43.540  host asks: in [Stereo (L R) | disabled] out [Stereo (L R)]  -> yes
15:07:43.540  numChannelsChanged: in [Stereo (L R) | disabled] out [Stereo (L R)]
15:07:43.540  prepareToPlay #1: 48000 Hz, block 480, float, realtime, latency 0; in [Stereo (L R) | disabled] out [Stereo (L R)]
15:07:43.632  process calls: started
15:07:43.632  process buffer: 2 channels; the enabled buses add up to 2
15:07:43.632  sidechain seen as: bus disabled
15:07:43.632  getStateInformation x1 (total 1); fingerprint BEF24EA3
15:07:43.632  transport: stopped
15:07:45.648  reset x1 (total 1)
15:07:45.951  process calls: stopped
15:07:46.557  process calls: started
15:07:46.557  transport: playing
15:07:56.939  transport: stopped
15:07:58.960  reset x1 (total 2)
15:07:59.263  process calls: stopped
18:02:16.170  process calls: started
18:02:18.721  scrambled from the probe editor: fingerprint BEF24EA3
18:02:19.103  getStateInformation x1 (total 2); fingerprint BEF24EA3
18:05:25.446  transport: playing
18:05:32.363  lookahead choice 1 (0.25 ms) set from the probe editor
18:05:32.392  releaseResources #1
18:05:32.392  prepareToPlay #2: 48000 Hz, block 480, float, realtime, latency 12; in [Stereo (L R) | disabled] out [Stereo (L R)]
18:05:32.392  releaseResources #2
18:05:32.392  prepareToPlay #3: 48000 Hz, block 480, float, realtime, latency 12; in [Stereo (L R) | disabled] out [Stereo (L R)]
18:05:32.401  latency reported to the host: 0 -> 12 samples, transport playing
18:05:32.401  reset x1 (total 3)
   (ten more changes, each with the same four lines after it, to 18:07:34)
18:08:21.824  releaseResources #23
18:08:21.824  numChannelsChanged: in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
18:08:21.824  prepareToPlay #24: 48000 Hz, block 480, float, realtime, latency 0; in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
18:08:21.824  releaseResources #24
18:08:21.824  prepareToPlay #25: 48000 Hz, block 480, float, realtime, latency 0; in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
18:08:21.843  process buffer: 4 channels; the enabled buses add up to 4
18:08:21.843  sidechain seen as: bus enabled, exact zeros
18:08:21.843  reset x1 (total 14)
18:08:26.478  sidechain seen as: bus enabled, signal
18:08:35.745  sidechain seen as: bus enabled, exact zeros
18:08:43.911  sidechain seen as: bus enabled, signal
18:08:57.808  sidechain seen as: bus enabled, exact zeros
18:08:58.615  sidechain seen as: bus enabled, signal
```

### Bitwig Studio, VST3, Windows 11, 2026-11-17, the author's answers

- "Fresh project." So the state Bitwig handed the plugin at construction did not come from a
  project reopened. The log begins with that construction, and the fingerprint handed over is the
  scramble's, three hours before the Scramble the log records: there was an earlier instance,
  scrambled, whose log is gone, and Bitwig made this one in its place and passed the state on
  (what it does when a device is moved, duplicated, or its plugin process restarted). The values
  came across whole. A save and reopen is still to do for the row.
- "Same touch behaviour as the other apps": the first finger only.

### Ableton Live 12 Suite, VST3, Windows 11, 2026-11-17 18:23, session 1 (a set saved and reloaded)

The author: "Fresh session. Saved and reloaded. Null test like the previous two: small slip of
sound when changing on the fly. Sidechain, then unassigned the track."

- **Setup.** Three layouts asked about at first, the same as Samplitude and Bitwig; three
  `prepareToPlay` in 60 ms on loading. 48 kHz, float, **blocks of 128 then of 256** after
  18:18:50, when Live released and prepared the plugin again (the audio buffer changed, or the
  device was moved). Live goes on calling the plugin while stopped.
- **Row 5.** `setStateInformation` once, 1 ms after the first `prepareToPlay`: 5448 bytes,
  fingerprint `1CC8BDCE`, none off default. The set was saved with the defaults, so this shows
  that Live hands the state back and when, not that scrambled values survive. (5448 bytes for the
  defaults against 6913 for the scramble: the provisional chunk is text.)
- **Row 2.** As inserted: bus disabled, two channels.
- **Row 1.** At 18:20:57 a source is chosen: the bus becomes stereo, one `prepareToPlay`, four
  channels. Signal on both sidechain channels, -0.2 dB at the highest on each.
- **Row 2 again, after the source was removed:** no layout change follows. The bus stays enabled
  and reads **exact zeros** (18:23:03, and in the report's status). In Live "no sidechain source"
  looks, once a source has been chosen at some time, exactly like a silent key.
- **Row 3.** Four changes during playback; for each, Live releases the plugin and prepares it
  once with the new latency within a few milliseconds; the null returns after a small slip.
- **Row 7.** "Most down at once 1 - kinds seen: touch."
- Not seen: row 4, the 284 parameters in Live's chooser.

```text
18:18:13.795  constructed as VST3 in Ableton Live (Ableton Live 12 Suite.exe), Windows 11
18:18:13.795  default layout: in [Stereo (L R) | disabled] out [Stereo (L R)]
18:18:13.796  host asks: in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]  -> yes
18:18:13.796  host asks: in [Stereo (L R) | disabled] out [disabled]  -> no
18:18:13.796  numChannelsChanged: in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
18:18:13.796  host asks: in [Stereo (L R) | disabled] out [Stereo (L R)]  -> yes
18:18:13.796  numChannelsChanged: in [Stereo (L R) | disabled] out [Stereo (L R)]
18:18:13.796  prepareToPlay #1: 48000 Hz, block 128, float, realtime, latency 0; in [Stereo (L R) | disabled] out [Stereo (L R)]
18:18:13.797  setStateInformation #1: 5448 bytes; fingerprint now 1CC8BDCE, 0 parameters off default
18:18:13.812  releaseResources #1
18:18:13.812  prepareToPlay #2: 48000 Hz, block 128, float, realtime, latency 0; in [Stereo (L R) | disabled] out [Stereo (L R)]
18:18:13.857  releaseResources #2
18:18:13.857  prepareToPlay #3: 48000 Hz, block 128, float, realtime, latency 0; in [Stereo (L R) | disabled] out [Stereo (L R)]
18:18:14.970  process calls: started
18:18:14.970  process buffer: 2 channels; the enabled buses add up to 2
18:18:14.970  sidechain seen as: bus disabled
18:18:14.970  reset x1 (total 1)
18:18:14.970  getStateInformation x1 (total 1); fingerprint 1CC8BDCE
18:18:14.970  transport: stopped
18:18:39.341  transport: playing
18:18:42.365  transport: stopped
18:18:50.718  releaseResources #3
18:18:50.718  host asks: in [disabled | disabled] out [Stereo (L R)]  -> no
18:18:50.718  numChannelsChanged: in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
18:18:50.718  host asks: in [disabled | disabled] out [disabled]  -> no
18:18:50.719  numChannelsChanged: in [Stereo (L R) | disabled] out [Stereo (L R)]
18:18:50.719  prepareToPlay #4: 48000 Hz, block 256, float, realtime, latency 0; in [Stereo (L R) | disabled] out [Stereo (L R)]
18:18:51.045  reset x1 (total 2)
18:18:55.787  transport: playing
18:20:06.323  lookahead choice 1 (0.25 ms) set from the probe editor
18:20:06.329  releaseResources #4
18:20:06.329  prepareToPlay #5: 48000 Hz, block 256, float, realtime, latency 12; in [Stereo (L R) | disabled] out [Stereo (L R)]
18:20:06.413  latency reported to the host: 0 -> 12 samples, transport playing
18:20:06.413  reset x1 (total 3)
   (three more changes, each with the same lines after it, to 18:20:09)
18:20:12.973  a touch reached the editor
18:20:22.184  transport: stopped
18:20:27.286  transport: playing
18:20:57.662  releaseResources #8
18:20:57.662  numChannelsChanged: in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
18:20:57.662  prepareToPlay #9: 48000 Hz, block 256, float, realtime, latency 0; in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
18:20:57.670  process buffer: 4 channels; the enabled buses add up to 4
18:20:57.670  sidechain seen as: bus enabled, exact zeros
18:20:57.670  reset x1 (total 7)
18:21:00.490  sidechain seen as: bus enabled, signal
18:21:13.191  sidechain seen as: bus enabled, exact zeros
18:21:19.481  getStateInformation x1 (total 2); fingerprint 1CC8BDCE
18:22:51.979  sidechain seen as: bus enabled, signal
18:22:57.874  getStateInformation x1 (total 3); fingerprint 1CC8BDCE
18:23:03.117  sidechain seen as: bus enabled, exact zeros
```

### No host, Windows 11, 2026-11-17 18:33: the control for touch

`LikyuProbeNoHost.exe`, the probe's editor in a JUCE window of its own. "Most down at once 1 -
kinds seen: touch": the same one finger as in the four hosts. So no host was the cause.

The cause, found in JUCE's notes at the pin and in its Windows code
(`juce_Windowing_windows.cpp`, `usesMultitouch = false`, and the window registered for touch only
when it is true): multitouch is off by default on Windows since JUCE 9, because a window
registered for touch no longer gets the system's gestures. See finding 6. The probe's editor and
this window now ask for multitouch.

### Ableton Live 12, VST3, Windows 11, 2026-11-17, parameters (the author's notes)

- Live lists none of the 284 parameters by itself: the device's panel is empty until the plugin
  is configured.
- "Forcing the configure and hitting shuffle showed all parameters": with *Configure* on, the
  probe's Scramble, which moves 282 parameters at once, adds them all to Live's panel. No ceiling
  was met.
- **What this means for Likyu before Phase 6:** Live adds a parameter when it sees it move, and a
  move has to come from the plugin's side. Likyu has no editor, so by itself it gives a Live user
  nothing to move. Whether Live offers another way in for a plugin without an editor was not
  tried.
- The automation curves themselves were not seen: the author could not get Live to show any, the
  mixer's included, which is a matter of Live's views and not of the plugin.

### Ableton Live 12, VST3, Windows 11, 2026-11-17, the envelope chooser (a screenshot)

The clip view's *Envelopes* box, device chooser on "Likyu Probe", control chooser open.

- The parameters are listed **by their names, in table order**: "Band 2 Transient Sustain",
  "Band 2 Transient Time", ... "Band 3 On", "Band 3 Type", "Band 3 Frequency", "Band 3 Q", ...
  "Band 4 Transient Sustain". The names read well and say which band.
- It is **one flat list**. About seventy entries fill the height of a 2000-pixel screen, so the
  284 make four screens of scrolling. No grouping by band, no search field. Finding "Band 7
  Ratio" is a matter of scrolling to the seventh block of thirty-five.
- The chooser's closed box cuts the name short ("Band 4 Transient Su"): the band number survives,
  the end of the name does not. A name that puts what differs first would survive better; the
  names are not frozen yet (devplan Phase 5).
- Live's browser files the three plugins under "Pix et Bel": Likyu, Likyu Probe, Likyu Probe
  fixed stereo.
- Not on the screenshot: units, and whether `lookahead` and `link`, which are not automatable,
  are kept out of this list (its top is above the screen).

The author found the curves in Session view's clip envelopes, after "much rummaging".

### Ableton Live 12, VST3, Windows 11, 2026-11-17, the top of the envelope chooser (a screenshot)

The list begins: "Device On", "Sidechain On", "Sidechain Input Gain", "Input/Sidechain Mix",
"Output Gain", "Band 1 On", "Band 1 Type", "Band 1 Frequency", "Band 1 Q". The author: "No
lookahead nor link".

- **`lookahead` and `link` are not offered for automation**, as the table declares them. Live
  honours the flag.
- **`bypass` does not appear under its name.** The first entry is Live's own "Device On", which
  is where a plugin's host bypass parameter goes.
- **Live adds three controls of its own for a plugin with a sidechain input**: "Sidechain On",
  "Sidechain Input Gain" and "Input/Sidechain Mix". They are Live's, ahead of the plugin's, and
  can be automated. "Sidechain On" is a switch separate from the choice of a source, as in
  Samplitude; what the plugin sees with it on and no source was not tried. "Input/Sidechain Mix"
  suggests Live can blend the track's own signal into what it sends to the sidechain bus, so in
  Live the key a Sidechain band reads may be a mix made by the host.
- Of the plugin's own, "Output Gain" comes first, then the bands.

### No host, Windows 11, 2026-11-17 about 19:10: multitouch asked for

`LikyuProbeNoHost.exe` rebuilt with `setUsingWindowsMultiTouch (true)`. The author: "4 fingers >
four coloured discs." JUCE at the pin delivers every finger on this machine once it is asked to.
Whether a host lets them through to a plugin's window is the next thing to try, with the probe
installed at 18:34, which asks through `usesWindowsMultiTouch()`.

### Samplitude and Live, VST3, Windows 11, 2026-11-17: multitouch asked for

The author, with the probe installed at 18:34: "Tested on Ableton Live and Samplitude, they both
have multitouch now." Several fingers reach the plugin's editor in both. No report was saved, so
the count and the pad's pinch line are not on file. Row 7 is answered for these two: a host does
not stand in the way, the editor only has to ask.

### Multitouch asked for, Windows 11, 2026-11-17: the pad on a screenshot

Four discs, "#0 touch" to "#3 touch", and the pad's last line still "no wheel or pinch yet".

- Four fingers arrive as four touch sources, numbered from 0 in the order they land.
- **No pinch event at all with several fingers down**, as JUCE's note says of a window registered
  for touch. The two modes do exclude each other: the editor that wants several fingers computes
  its own pinch.
- The discs are drawn at their largest size, which the pad gives to a pressure of 1 or more: a
  pressure is reported with each touch on this screen. Its range was not looked at.
- Which window this was (a host's or the one without a host) is not on the screenshot.

### REAPER and Bitwig, VST3, Windows 11, 2026-11-17: multitouch asked for

The author: "Reaper and Bitwig too. All four are now showing multitouch response." Row 7 is
answered for the four Windows hosts.

### REAPER, VST3, macOS 15.8.3 (Mac mini), 2026-11-18 15:06 to 15:23, session 1

Four reports of one instance, read from `~/Documents/likyu-hostprobe/` on the Mac over SSH. The
three of 15:06 differ only in their time and block count. The author: "As expected multitouch
doesn't work out of the box. Null test okay. Small sliver of sound gets through when changing look
ahead time, but no need to restart."

- **How REAPER runs it here:** 48 kHz, blocks of 512, **double**, as on Windows (blocks of 128
  there: each machine's audio buffer). `reset` at every play start. Called while stopped, then
  not after about two minutes (15:05:10 to 15:07:13).
- **On loading:** the 64-channel request seen on Windows, accepted and applied for an instant,
  then stereo. One detail not seen there: the first `prepareToPlay` had the sidechain enabled as
  stereo; REAPER released the plugin and prepared it again with the bus disabled, within 3 ms.
- **Row 2:** bus disabled as inserted, two channels.
- **Row 3, answered.** Ten changes of lookahead during playback (15:18:34 to 15:19:46), every
  choice in both directions. Each time the latency reaches the host 10 to 70 ms after the button,
  and no `prepareToPlay` follows. The null holds, with a sliver at each change (the author).
- **Row 6:** at 15:22:03 the track goes to four channels. REAPER asks the same questions as on
  Windows, is refused `[L R C Lfe | L R C Lfe]` and `[L R C Lfe | Stereo] out [Stereo]`, and
  settles on `in [L R C Lfe | disabled] out [L R C Lfe]`: a four-channel main bus, LFE reported on
  channel 4. Back to stereo at 15:22:29, four again at 15:22:56.
- **Row 1: the sidechain fed, beside a four-channel main.** At 15:23:24 REAPER enables the
  sidechain after the four main channels: a buffer of six, the key in 5 and 6, exact zeros until
  play; at 15:23:48 signal, -3.7 dB on both channels, and zeros again on stop. The main input's
  highest peak is -0.7 dB. The author: "It might be my doing. I'm not at ease on Reaper. I tried
  to reproduce the session as setup on Windows." The project, saved at 15:26
  (`~/Desktop/test Likyu1.RPP`), has the send `AUXRECV 1 0 1 0 0 0 0 0 2 …` (track 2's 1/2 to
  this track's 3/4, post-fader) and a pin map of six inputs: 1 and 2 from track channels 1 and 2,
  3 and 4 from none, 5 and 6 from channels 3 and 5, 4 and 6; four outputs, 1 and 2 to track 1 and
  2. The Windows project of 14:56 has the same map. So this is the layout and map of a working
  sidechain beside a four-channel main (finding 2).
- **Row 5:** nothing scrambled yet; the fingerprint is the defaults' (`1CC8BDCE`).
- **Row 7:** one pointer, of kind mouse. macOS has no touch screen of its own; the row is n/a
  there.
- Seventeen distinct layouts asked about by the end.

The last report's status lines, and its log from the change of width (the lines before are the
loading and the lookahead changes summarised above):

```text
Host       Reaper (REAPER) - VST3 - Mac OSX 15.8.3
Prepared   48000 Hz, block 512 - prepareToPlay x6, releaseResources x5, reset x15
Blocks     48134 - sizes 512 to 512, last 512 - zero-length 0 - float 0, double 48134 - bypassed 0
Transport  stopped - process calls: running
Main bus   Unknown (L R C Lfe) - LFE: channel 4 - peak exact zero, highest ever -0.7 dB
Sidechain  Stereo (L R) - process buffer 6 channels, buses add up to 6
  SEEN AS  BUS ENABLED, EXACT ZEROS - now exact zero, exact zero
           highest ever -3.7 dB, -3.7 dB
Latency    0 samples (lookahead 0 ms) - changed 10 times - prepareToPlay calls since the last change: 4
State      fingerprint 1CC8BDCE - 0 of 284 off default - get x7, set x0
Pointers   most down at once 1 - kinds seen: mouse
Layout     in [Unknown (L R C Lfe) | Stereo (L R)] out [Unknown (L R C Lfe)]
Layouts    17 distinct layouts asked about
```

Log:

```text
15:22:03.935  releaseResources #2
15:22:03.935  host asks: in [disabled | disabled] out [Stereo (L R)]  -> no
15:22:03.935  numChannelsChanged: in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
15:22:03.935  processorLayoutsChanged: in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
15:22:03.935  host asks: in [Unknown (L R C Lfe) | Unknown (L R C Lfe)] out [Unknown (L R C Lfe)]  -> no
15:22:03.935  host asks: in [Stereo (L R) | Unknown (L R C Lfe)] out [Stereo (L R)]  -> no
15:22:03.935  host asks: in [Unknown (L R C Lfe) | Stereo (L R)] out [Stereo (L R)]  -> no
15:22:03.935  host asks: in [Unknown (L R C Lfe) | Stereo (L R)] out [Unknown (L R C Lfe)]  -> yes
15:22:03.935  numChannelsChanged: in [Unknown (L R C Lfe) | Stereo (L R)] out [Unknown (L R C Lfe)]
15:22:03.935  processorLayoutsChanged: in [Unknown (L R C Lfe) | Stereo (L R)] out [Unknown (L R C Lfe)]
15:22:03.936  host asks: in [Unknown (L R C Lfe) | disabled] out [disabled]  -> no
15:22:03.936  host asks: in [Unknown (L R C Lfe) | disabled] out [Unknown (L R C Lfe)]  -> yes
15:22:03.936  numChannelsChanged: in [Unknown (L R C Lfe) | disabled] out [Unknown (L R C Lfe)]
15:22:03.936  processorLayoutsChanged: in [Unknown (L R C Lfe) | disabled] out [Unknown (L R C Lfe)]
15:22:03.936  prepareToPlay #3: 48000 Hz, block 512, double, realtime, latency 0; in [Unknown (L R C Lfe) | disabled] out [Unknown (L R C Lfe)]
15:22:03.979  process buffer: 4 channels; the enabled buses add up to 4
15:22:03.979  reset x1 (total 11)
15:22:29.255  releaseResources #3
15:22:29.255  host asks: in [disabled | disabled] out [Unknown (L R C Lfe)]  -> no
15:22:29.255  numChannelsChanged: in [Unknown (L R C Lfe) | Stereo (L R)] out [Unknown (L R C Lfe)]
15:22:29.255  processorLayoutsChanged: in [Unknown (L R C Lfe) | Stereo (L R)] out [Unknown (L R C Lfe)]
15:22:29.255  numChannelsChanged: in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
15:22:29.255  processorLayoutsChanged: in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
15:22:29.255  numChannelsChanged: in [Stereo (L R) | disabled] out [Stereo (L R)]
15:22:29.255  processorLayoutsChanged: in [Stereo (L R) | disabled] out [Stereo (L R)]
15:22:29.255  prepareToPlay #4: 48000 Hz, block 512, double, realtime, latency 0; in [Stereo (L R) | disabled] out [Stereo (L R)]
15:22:29.307  process buffer: 2 channels; the enabled buses add up to 2
15:22:29.307  reset x1 (total 12)
15:22:29.811  getStateInformation x1 (total 5); fingerprint 1CC8BDCE
15:22:56.844  releaseResources #4
15:22:56.845  numChannelsChanged: in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
15:22:56.845  processorLayoutsChanged: in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
15:22:56.845  numChannelsChanged: in [Unknown (L R C Lfe) | Stereo (L R)] out [Unknown (L R C Lfe)]
15:22:56.845  processorLayoutsChanged: in [Unknown (L R C Lfe) | Stereo (L R)] out [Unknown (L R C Lfe)]
15:22:56.845  numChannelsChanged: in [Unknown (L R C Lfe) | disabled] out [Unknown (L R C Lfe)]
15:22:56.845  processorLayoutsChanged: in [Unknown (L R C Lfe) | disabled] out [Unknown (L R C Lfe)]
15:22:56.845  prepareToPlay #5: 48000 Hz, block 512, double, realtime, latency 0; in [Unknown (L R C Lfe) | disabled] out [Unknown (L R C Lfe)]
15:22:56.918  process buffer: 4 channels; the enabled buses add up to 4
15:22:56.918  reset x1 (total 13)
15:23:24.537  releaseResources #5
15:23:24.537  numChannelsChanged: in [Unknown (L R C Lfe) | Stereo (L R)] out [Unknown (L R C Lfe)]
15:23:24.537  processorLayoutsChanged: in [Unknown (L R C Lfe) | Stereo (L R)] out [Unknown (L R C Lfe)]
15:23:24.537  prepareToPlay #6: 48000 Hz, block 512, double, realtime, latency 0; in [Unknown (L R C Lfe) | Stereo (L R)] out [Unknown (L R C Lfe)]
15:23:24.549  process buffer: 6 channels; the enabled buses add up to 6
15:23:24.549  sidechain seen as: bus enabled, exact zeros
15:23:24.549  reset x1 (total 14)
15:23:39.891  getStateInformation x1 (total 6); fingerprint 1CC8BDCE
15:23:45.263  getStateInformation x1 (total 7); fingerprint 1CC8BDCE
15:23:48.010  sidechain seen as: bus enabled, signal
15:23:48.010  reset x1 (total 15)
15:23:48.010  transport: playing
15:23:52.078  transport: stopped
15:23:53.092  sidechain seen as: bus enabled, exact zeros
```

### REAPER, VST3 and AU, macOS 15.8.3 (Mac mini), 2026-11-18 15:27 to 15:35, session 1 continued

Four reports and the project as saved at 15:35 (`~/Desktop/test Likyu1.RPP`), read over SSH. The
author: "Normal Likyu shows all parameters so do the automation curves list. The probe on a 4
channel track lets no sound through. Mono in probe fixed stereo is okay on a stereo track with
mono file." The project then holds five tracks: the VST3 probe on the four-channel track with
the send to 3/4 (as before); **the AU Likyu** on the stereo track that is the send's source; a
plain stereo track; **the AU probe on a new four-channel track** holding a four-channel AmbiX
file; **the fixed-stereo control (VST3)** on a stereo track holding a mono file.

- **Row 5, VST3 (15:27:11):** the project reopened after a Scramble. `setStateInformation` of
  7008 bytes just after the first `prepareToPlay`, fingerprint `BEF24EA3`, 244 off default: the
  same code as Scramble gives on Windows, so restored exactly. (The chunk was 6913 bytes on
  Windows; why it differs is not looked into. The fingerprint hashes the values, and matches.) REAPER
  then applies the four-channel main and the enabled sidechain before the first block.
- **Row 4, AU:** on Likyu itself, "shows all parameters", and REAPER's automation list has them
  all (the author). Units not yet noted.
- **Row 1, AU: no render at all on a four-channel track (15:30:54 and 15:35:46).** Constructed
  at 15:30:14 and prepared twice (float, 512, `in [Stereo | Stereo] out [Stereo]`), reset four
  times at each play start (28 by 15:35), but **0 blocks**: REAPER never called the plugin's
  render. JUCE's AU wrapper calls `processBlock` on every `Render`
  (`juce_audio_plugin_client_AU_1.mm`, lines 1485 to 1549), so the call did not reach the
  wrapper. The author hears no sound through the track. REAPER's saved pin map for it is
  ordinary: six inputs, 1 to 4 from track channels 1 to 4, 5 and 6 from none; two outputs.
  Not known yet: whether the four-channel track is the cause (the AU on a stereo track would
  tell), and whether the product's AU on the stereo track renders (no probe there to say).
  The 3509 "host asks" lines of 15:30:14 are JUCE's own wrapper listing channel configurations
  at construction, within one millisecond, not REAPER.
- **The fixed-stereo control (15:33:26 to 15:35:43)** on a stereo track with a mono file:
  stereo main, sidechain disabled, signal (-21.7 dB), as the author heard. Its layout answers
  show the build is the control: `[Mono | Mono] out [Mono]` refused, `[Stereo | Mono] out
  [Stereo]` accepted. **Not yet the measurement it is for**: on a four-channel track with a send
  to 3/4 (finding 2).

The AU report of 15:35:46, status lines:

```text
Host       Reaper (REAPER) - AU - Mac OSX 15.8.3
Prepared   48000 Hz, block 512 - prepareToPlay x2, releaseResources x1, reset x28
Blocks     0 - zero-length 0 - float 0, double 0 - bypassed 0
Transport  no play head seen - process calls: none now
Main bus   Stereo (L R) - LFE: none reported - peak exact zero, highest ever exact zero
Sidechain  Stereo (L R) - process buffer 0 channels, buses add up to 0
  SEEN AS  NO AUDIO PROCESSED YET - now exact zero, exact zero
           highest ever exact zero, exact zero
Latency    0 samples (lookahead 0 ms) - changed 0 times - prepareToPlay calls since the last change: 2
State      fingerprint 1CC8BDCE - 0 of 284 off default - get x4, set x0
Pointers   most down at once 0 - kinds seen: none yet
Layout     in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
Layouts    3509 distinct layouts asked about
```

Its log after construction:

```text
15:30:14.675  prepareToPlay #1: 48000 Hz, block 512, float, realtime, latency 0; in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
15:30:14.676  releaseResources #1
15:30:14.740  getStateInformation x2 (total 2); fingerprint 1CC8BDCE
15:30:20.516  prepareToPlay #2: 48000 Hz, block 512, float, realtime, latency 0; in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
15:30:20.543  reset x4 (total 4)
15:30:24.910  reset x4 (total 8)
15:30:44.120  reset x4 (total 12)
15:30:52.950  getStateInformation x1 (total 3); fingerprint 1CC8BDCE
15:31:33.780  reset x4 (total 16)
15:31:52.774  reset x4 (total 20)
15:32:51.266  reset x4 (total 24)
15:33:05.152  reset x4 (total 28)
15:35:41.417  getStateInformation x1 (total 4); fingerprint 1CC8BDCE
```

### REAPER, AU, macOS 15.8.3 (Mac mini), 2026-11-18 15:37 to 15:53: the AU's silence, traced

Two more AU instances, each at 0 blocks: at 15:37 on a copy of the AmbiX track, at 15:50 on
*ambiance_voixPapa* (four channels) after the VST3 probe, which the author bypassed in turn. The
author: "I put both on track 1 and disabled in Reaper either one of them to keep only one at a
time." With the VST3 probe the track nulls; "AU never lets sound through". (Scramble changes
nothing audible in either: the Phase 0 plugin passes its input through, whatever its
parameters.)

- **The probe's own log** of each AU instance: `prepareToPlay` (REAPER's first
  `AudioUnitInitialize`), `releaseResources` within 2 ms (`AudioUnitUninitialize`), then a
  second `prepareToPlay` together with the first `reset`. That one comes from JUCE's `Reset`,
  which prepares when it finds itself unprepared (`juce_audio_plugin_client_AU_1.mm`, about line
  254), not from an initialise. A failed initialise returns before `prepareToPlay`, so the
  probe cannot log it.
- **The macOS log** (`log show --predicate 'process == "REAPER" AND messageType == error'`):
  `(LikyuProbe) from <private>, render err: -10867`, 77,321 times from 15:30:14.661 to 15:53,
  thousands a minute while REAPER runs, and from no other plugin. The error is written by
  `AUBase::DoRender` when the unit is not initialised; `Render`, and with it `processBlock`, is
  never reached.
- **A minimal AU host** (a C program on the Mac mini, run in the logged-in session: instantiate
  `aufx Lkyp PxBl` or `aufx Lkyu PxBl`, set stream formats, initialise, render ten blocks of
  512). With the `LIKYU_PROBE_DUMP` report: the probe counts 10 float blocks, a quadraphonic main
  and a stereo sidechain at -12 dB, so its counters work in AU.

  | Sequence | Result |
  | --- | --- |
  | Main 2 or 4 channels in and out, sidechain 2, connected or not | Initialise 0, renders, output as input |
  | Initialise at 2, uninitialise, input and output to 4 (either order), initialise | 0, renders |
  | Initialise at 2, uninitialise, **input alone to 4**, initialise | **-10868**, then every render **-10867**, silence |
  | The same, **output alone to 4**, or **input alone to 1** | **-10868**, then **-10867** |
  | Input and output both to 1 | 0, renders |

  Each `AudioUnitSetProperty` of a width returns 0 even when the pair will not initialise.
  The product's AU behaves as the probe's.

### REAPER, AU, macOS 15.8.3 (Mac mini), 2026-11-18 15:58 to 16:01: "now works", by ear only

The author: "Removing the VST with its sidechain and only having the AU with or without the
sidechain now works." Track 1 (*ambiance_voixPapa*, four channels, the send to 3/4) now holds a
new AU probe alone (constructed 15:58:08, state of 5560 bytes given back at once). The report of
16:01:37: **0 blocks**, the same initialise, uninitialise and `Reset` as before. The macOS log
has the -10867 render errors going on without a gap, 5,000 to 11,000 a minute from 15:58 to
16:01. So the AU is still not run; what changed is what REAPER does with the track around it.
The Phase 0 plugin passes its input through, so sound is no sign that it runs: only the probe's
block count is.

### REAPER, AU, macOS 15.8.3 (Mac mini), 2026-11-18 16:03 to 16:11: the AU runs on two channels

The author: "Removing the sidechain seems to fix things." In the project saved at 16:10, track 1
(*ambiance_voixPapa*) has no send and **two channels instead of four**; its AU probe's pin map
has four inputs (main 1 and 2 from track 1 and 2, sidechain from none) and two outputs.

- **The report of 16:10:35**, from that instance (constructed 15:58:08.505): after 32 minutes of
  resets and no block, a third `prepareToPlay` at 16:10:23, the first initialise that holds;
  process calls start at once, a buffer of four channels (main and sidechain), the sidechain
  enabled at exact zeros; 1150 blocks in float, the main input's highest peak -20.9 dB.
- **The error rate** in the macOS log, at about 94 refused renders a second for each failing
  instance (48000 / 512): one instance's worth from 16:00 to 16:02 (track 1's chain bypassed,
  the red FX button the author saw), two from 16:03 to 16:09 (track 1 on again, still four
  channels), one again from 16:10. The one left is the AU on the four-channel AmbiX track that is
  still active.
- **Before the change:** the 15:30 and 15:50 projects' pin maps for the AU on a four-channel
  track: six inputs, two outputs. The width, not the send, is what matters: from 16:03 to 16:09
  track 1 had no send and four channels, and the AU failed.

### REAPER, VST3, macOS 15.8.3 (Mac mini), 2026-11-18 16:14 to 16:17: the fixed-stereo control on four channels

*Likyu Probe fixed stereo* alone on track 1 (*ambiance_voixPapa*, four channels, the send from
*KaradaKara-last-collage* to 3/4, post-fader), the pin connector not touched. The author:
"Longer play, new report. The beginning is quiet."

- **REAPER's pin map** in the project saved at 16:15: four inputs, fed one to one from track
  channels 1 to 4, so the sidechain (inputs 3 and 4) from track channels 3 and 4, where the send
  lands. The same as ReaComp's on Windows.
- **Layouts:** REAPER asks for three- and four-channel mains (LRS, quadraphonic, first-order
  Ambisonics), each refused by the control, and settles on `in [Stereo | Stereo] out [Stereo]`:
  a buffer of four channels.
- **16:15:33:** exact zeros on the sidechain over a play of 2.4 s. The source track had been
  muted in the 16:10 save and the send is post-fader; the opening of its file is quiet.
- **16:17:46:** a longer play from 16:17:30; **sidechain signal from 16:17:35**, highest
  -19.3 dB on both channels; main input highest -12.5 dB.

Finding 2 has its last measurement.

```text
Host       Reaper (REAPER) - VST3 - Mac OSX 15.8.3
Prepared   48000 Hz, block 512 - prepareToPlay x3, releaseResources x2, reset x4
Blocks     16886 - sizes 512 to 512, last 512 - zero-length 0 - float 0, double 16886 - bypassed 0
Transport  playing - process calls: running
Main bus   Stereo (L R) - LFE: none reported - peak -15.4 dB, highest ever -12.5 dB
Sidechain  Stereo (L R) - process buffer 4 channels, buses add up to 4
  SEEN AS  BUS ENABLED, SIGNAL - now -56.7 dB, -56.7 dB
           highest ever -19.3 dB, -19.3 dB
Latency    0 samples (lookahead 0 ms) - changed 0 times - prepareToPlay calls since the last change: 3
State      fingerprint 1CC8BDCE - 0 of 284 off default - get x3, set x0
Pointers   most down at once 0 - kinds seen: none yet
Layout     in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
Layouts    30 distinct layouts asked about
```

### REAPER, AU, macOS 15.8.3 (Mac mini), 2026-11-18, about 16:20: units

The author, on Likyu itself (not the probe), in REAPER's generic view of the AU: "shows no units
at all, just the label and either a slider and the value at the end, or a dropdown menu."

### REAPER, macOS 15.8.3 (Mac mini), 2026-11-18, about 16:25: no mono track

The author: "Track width doesn't show anywhere. I tried a stereo or mono master and no change,
sound goes through. I put a mono audio clip on a track and the sound would still go through."
No report saved. A REAPER track has two channels at least, so the plugin is never offered a
mono main in ordinary use; row 6's mono case is n/a in REAPER, as in Samplitude. A 5.1 or wide
case needs no such file: a track's channel count is set in its routing window.

### REAPER, VST3, macOS 15.8.3 (Mac mini), 2026-11-18 16:25 to 16:27: a six-channel track

Track 7, "FLAC 5.1", six channels, holding a 5.1 FLAC file; *Likyu Probe* (VST3) alone on it.
REAPER asks for 5.1 on both buses (refused), for a stereo main with a 5.1 sidechain (refused),
accepts `in [5.1 | Stereo] out [5.1]`, then settles on `in [5.1 | disabled] out [5.1]`: a buffer
of six. Signal on the main, -6.0 dB. The pin map in the project of 16:28: eight inputs, 1 to 6
from track channels 1 to 6, 7 and 8 (the sidechain) from channels 7 and 8, which the track does
not have; six outputs.

```text
Host       Reaper (REAPER) - VST3 - Mac OSX 15.8.3
Prepared   48000 Hz, block 512 - prepareToPlay x2, releaseResources x1, reset x3
Blocks     2745 - sizes 512 to 512, last 512 - zero-length 0 - float 0, double 2745 - bypassed 0
Transport  stopped - process calls: running
Main bus   5.1 Surround (L R C Lfe Ls Rs) - LFE: channel 4 - peak exact zero, highest ever -6.0 dB
Sidechain  disabled - process buffer 6 channels, buses add up to 6
  SEEN AS  BUS DISABLED - now exact zero, exact zero
           highest ever exact zero, exact zero
Latency    0 samples (lookahead 0 ms) - changed 0 times - prepareToPlay calls since the last change: 2
State      fingerprint 1CC8BDCE - 0 of 284 off default - get x3, set x0
Pointers   most down at once 0 - kinds seen: none yet
Layout     in [5.1 Surround (L R C Lfe Ls Rs) | disabled] out [5.1 Surround (L R C Lfe Ls Rs)]
Layouts    15 distinct layouts asked about
```

### Ableton Live 12, VST3, macOS 15.8.3 (Mac mini), 2026-11-18 16:35 to 16:47, session 1

Three reports, read over SSH. The author: "There should be a report for the null test. Passed as
with Reaper. Then I tried the sidechain. 2nd report. Used scramble to populate the parameters
for the automations. OK. Saved and reloaded. Saved new report."

- **How Live runs it here:** 44.1 kHz, blocks of 512, float. Called while stopped, as on Windows.
  Two `prepareToPlay` at insertion, within 60 ms.
- **Row 2:** bus disabled as inserted, two channels.
- **Row 3 (16:38:49):** eight changes of lookahead during playback, every choice. Each is
  followed within 10 to 50 ms by `releaseResources` and a `prepareToPlay` that already carries
  the new latency (11, 44, 132 samples at 44.1 kHz), then `reset`: Live restarts the plugin at
  every change, as on Windows. The null passes (the author).
- **Row 1 (16:44:34):** a source chosen on the device: Live releases the plugin, enables the
  sidechain as stereo and prepares it again; four channels, exact zeros until play, then signal,
  highest -12.6 and -13.8 dB.
- **Row 4:** Scramble at 16:41:42 brings the parameters into Live's automation (the author), as
  on Windows.
- **Row 5 (16:47:05):** the set saved after Scramble and reopened. `setStateInformation` of 7008
  bytes just after the first `prepareToPlay`, `BEF24EA3`, 244 off default: restored exactly. Live
  prepares three times while loading and enables the sidechain a second later, so the routing
  comes back with the set. Exact zeros again at 16:47:01 while playing: the source silent there.

The reopened set's report, status lines:

```text
Host       Ableton Live (Live) - VST3 - Mac OSX 15.8.3
Prepared   44100 Hz, block 512 - prepareToPlay x4, releaseResources x3, reset x2
Blocks     2585 - sizes 512 to 512, last 512 - zero-length 0 - float 2585, double 0 - bypassed 0
Transport  playing - process calls: running
Main bus   Stereo (L R) - LFE: none reported - peak exact zero, highest ever -1.8 dB
Sidechain  Stereo (L R) - process buffer 4 channels, buses add up to 4
  SEEN AS  BUS ENABLED, EXACT ZEROS - now exact zero, exact zero
           highest ever -12.8 dB, -13.8 dB
Latency    0 samples (lookahead 0 ms) - changed 0 times - prepareToPlay calls since the last change: 4
State      fingerprint BEF24EA3 - 244 of 284 off default - get x1, set x1
Pointers   most down at once 0 - kinds seen: none yet
Layout     in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
Layouts    3 distinct layouts asked about
```

### Ableton Live 12, AU, macOS 15.8.3 (Mac mini), 2026-11-18 16:50

The author, first: "No AU on Live." Live's plugin database (`Live-plugins-1.db`) then listed 54
plugins and no AU at all, Apple's included. The author: "It's AU2 only. I have AU3 checked and
no AU2." JUCE builds an Audio Unit v2; with only v3 enabled in Live's Plug-In Sources, Live
offers none of ours. Worth a line in the user documentation.

With v2 on, *Likyu Probe* (AU) on a stereo track: one `prepareToPlay` (no initialise and
uninitialise as in REAPER), process calls at once, a buffer of four channels, **the sidechain
enabled at exact zeros from the first block** and through playback from 16:50:05 to 16:50:14,
then signal from 16:50:18 once a source was chosen. 1885 blocks.

```text
Host       Ableton Live (Live) - AU - Mac OSX 15.8.3
Prepared   44100 Hz, block 512 - prepareToPlay x1, releaseResources x0, reset x3
Blocks     1885 - sizes 512 to 512, last 512 - zero-length 0 - float 1885, double 0 - bypassed 0
Transport  playing - process calls: running
Main bus   Stereo (L R) - LFE: none reported - peak -25.3 dB, highest ever -1.6 dB
Sidechain  Stereo (L R) - process buffer 4 channels, buses add up to 4
  SEEN AS  BUS ENABLED, SIGNAL - now -16.2 dB, -16.5 dB
           highest ever -12.6 dB, -13.8 dB
Latency    0 samples (lookahead 0 ms) - changed 0 times - prepareToPlay calls since the last change: 1
State      fingerprint 1CC8BDCE - 0 of 284 off default - get x1, set x0
Pointers   most down at once 0 - kinds seen: none yet
Layout     in [Stereo (L R) | Stereo (L R)] out [Stereo (L R)]
Layouts    3512 distinct layouts asked about
```

The report of 16:51:46, the same instance: the source removed on the device at 16:50:35, the
sidechain reads **enabled, exact zeros** again while playing (main input still at signal). As
with Live in VST3 on Windows, a removed source leaves the bus enabled and silent.

The report of 16:54:44: the set scrambled, saved and reopened. A new instance; one
`prepareToPlay`, two resets, then `setStateInformation` of 7008 bytes, `BEF24EA3`, 244 off
default: restored exactly, as in VST3. The author also notes that Live's sidechain chooser shows
a level meter beside each source, which is Live's own and helps tell a silent source from a
missing one.

The report of 16:56:31, the same instance: lookahead 0.25, 1 and 3 ms during playback; the
latency reaches Live 23 to 72 ms after each button (11, 44, 132 samples at 44.1 kHz) and no
`prepareToPlay` or `releaseResources` follows. The author: "The null test at the various
lookahead buffers is okay. As usual a small audio chunk gets through before things are nulling
each other again." The same Live restarts the VST3 at every change (16:38:49): whether a host
restarts the plugin depends on the format as well as the host.

