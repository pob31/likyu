# PRD — Likyū, Dynamic EQ and Transient Designer Plugin (VST3 AU LV2)

Nov 16, 2026 · @Pierre-Olivier Boulant

## Summary

An eight-band parametric EQ plugin in which every band chooses, independently, how its **gain** behaves (Static, Dynamic, Transient) and how its **frequency** behaves (Fixed, Env, Track). Each band has its own detector, decoupled from the band's filter, keyed from the plugin input or from a sidechain bus. Latency is zero by default and constant per instance.

First host is Go.dot (VST3, AU, LV2 through Tracktion Engine). The DSP core is a standalone library so that Go.dot can later link it natively and the apps' own EQs can adopt it. WFS-DIY's per-output EQs are **not** a target: signal-dependent gain per loudspeaker modulates the wavefront, and HF distance attenuation already covers that use case deterministically.

**Status:** draft 0.2 (0.1 reviewed and corrected on 2026-11-16), nothing built. The repository, build, CI and review discipline are those of the OSCQuery Automation Plugin (`pob31/OSCQuery-Automation-plugin`), copied and cut down. The two sections that follow, *Goals and non-goals* and *Band model*, are to be reproduced verbatim in `CLAUDE.md` as the review criterion for every change. Every decision in this document carries its date; items marked *proposed* are defaults awaiting the author (see Open decisions).

Reference use case, to design around: music on an array with a 2 to 4 kHz band keyed from an actor's microphone, carving room only while they speak; on the same instance, a self-keyed band taming boom, a Transient band softening a snare attack, and a Track band following a wandering resonance.

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

## Signal flow

The audio path is a series chain of eight filters behind one optional delay. Everything that decides a band's gain or frequency sits in a side path that reads the undelayed input or the undelayed sidechain, in parallel for all bands.

```text
                 +--------------------- detection (per band, in parallel) ----------------------+
main in --+----->| key select -> [deflation] -> detector SVF -> envelope -> gain computer  -> G_n |
          |      |   ^                       -> tracker / env follower              -> f_n     |
sidechain-|------+---+                                                                          |
          |      +------------------------------------------------------------------------------+
          |                          | G_1 f_1      | G_2 f_2              | G_8 f_8
          v                          v              v                      v
      lookahead delay --------->  band 1  ------->  band 2  --- ... --->  band 8  ---> out
      (0 by default)
```

- **Why detection never reads between stages** (decided 2026-11-16): a band reacting to what an earlier band just did makes overlapping dynamic bands interact and hunt. Reading the input makes every band's behaviour a function of the input alone.
- **Why series and not a crossover multiband:** a crossover leaves all-pass phase behind even at unity. Here an idle band is transparent.
- The sidechain is never delayed and never reaches the output, except through the per-band Listen button, which routes that band's detector signal to the output in place of the chain.

## Filter core

Every band, audio and detector, is the trapezoidal (TPT, zero-delay-feedback) state-variable filter in Simper's form. It stays stable under per-sample modulation of gain, Q and frequency, which is the whole reason for choosing it.

```text
g  = tan(pi * fc / fs)        k = 1 / Q
a1 = 1 / (1 + g * (g + k))    a2 = g * a1    a3 = g * a2

v3  = x - ic2
v1  = a1 * ic1 + a2 * v3            (band-pass state)
v2  = ic2 + a2 * ic1 + a3 * v3      (low-pass)
ic1 = 2 * v1 - ic1
ic2 = 2 * v2 - ic2

y = m0 * x + m1 * v1 + m2 * v2
```

With A = 10^(gain\_dB / 40):

| Type | g | k | m0, m1, m2 |
| --- | --- | --- | --- |
| Bell, proportional Q | g | 1 / (Q A) | 1, k (A² − 1), 0 |
| Bell, constant Q | g | 1 / Q | 1, k (A² − 1), 0 |
| Low shelf | g / √A | 1 / Q | 1, k (A − 1), A² − 1 |
| High shelf | g √A | 1 / Q | A², k (1 − A) A, 1 − A² |
| High-pass | g | 1 / Q | 1, −k, −1 |
| Low-pass | g | 1 / Q | 0, 0, 1 |
| Band-pass (unity peak) | g | 1 / Q | 0, k, 0 |
| Notch | g | 1 / Q | 1, −k, 0 |

- **Exact bypass.** At A = 1 a bell or shelf gives m0 = 1, m1 = m2 = 0: the output is the input, sample for sample. This is a tested property, not an approximation. It requires that the dB-to-A conversion return exactly 1 at 0 dB, whatever fast approximation is used, and that a dynamic offset come back to exactly 0 (see *Return to zero* under Gain modes).
- **Proportional Q** (default for bells): the bell narrows as gain grows and boost and cut are mirror images. A dynamic cut starts broad and tightens as it digs in. Constant Q is a per-band option.
- **Update rates.** The mix terms and k follow the gain every sample. g is recomputed on a 16-sample control sub-block and interpolated linearly in between, in Env and Track modes and during parameter smoothing; a1 to a3 follow from the interpolated g. The sub-block length is a constant to be confirmed by the modulation tests. The sub-block counter runs across process calls, so the output does not depend on how the host cuts its blocks.
- **tan by polynomial** (decided 2026-11-16). `g = tan(pi fc / fs)` uses an approximation with no library call. With fc up to 20 kHz the argument reaches 0.45 π at 44.1 kHz, close to the pole, so a plain odd polynomial is only allowed where the argument stays below 0.25 π (fs ≥ 88.2 kHz); below that, a rational form (odd polynomial over even polynomial, one divide). Acceptance: frequency error under 1 cent over 20 Hz to 20 kHz at every supported rate.
- **Shelves under modulation** need g rescaled by √A: a multiply by a square root, no new tan.
- Denormals: flush-to-zero and denormals-are-zero set in the process call, and tested with silence after signal.
- Parameter smoothing: user moves arrive at control rate and are smoothed in `lib/dsp` (frequency in log domain, gain in dB), about 20 ms, so that a native client of the library gets the same smoothing as the plugin.
- **Precision** (decided 2026-11-16): the audio path keeps its states and coefficients in double, for low frequencies with high Q at high sample rates; the detection path is float, eight lanes to an AVX2 register. The host's buffers may be either precision.

## Detection

Each band owns one detector SVF per channel of its key (the main input's channels for Self, the sidechain's one or two for Sidechain). Each reads its key channel and produces a level in dB; linking decides how those levels are combined for the gain computer and the envelope follower.

- **Detector filter.** *Follow band* copies the band's current frequency and Q (including a moving frequency in Env and Track) as a band-pass. *Own* gives the detector its own type (band-pass, high-pass, low-pass), frequency and Q.
- **Quadrature envelope for band-pass detectors** (corrected 2026-11-16; confirmed the same day by the Phase 0 spike, `docs/spikes/phase0-tracker.md`: within 0.0001 dB of the band-pass magnitude at every tone tried, no ripple). `env = k * sqrt(max(0, v1² − v2 * hp))`, with `hp = x − k * v1 − v2`. In the TPT form the low-pass and high-pass outputs are each one integrator away from the band-pass, so LP · HP = BP² holds exactly: for a single sine the expression is the band-pass magnitude at any frequency, with no ripple and with the band-pass's own selectivity. With several components the cross terms can go negative for an instant, hence the max. The first draft's `k * sqrt(v1² + v2²)` is rejected: v2 is the low-pass output, so everything below fc reached the detector at gain k (unity at Q = 1). This is what allows millisecond time constants on low bands without the envelope ripple becoming distortion. High-pass and low-pass detectors use a peak follower whose shortest time constant is bounded to a few periods of the detector frequency.
- **Level.** `L = 20 log10(env)`, floored at −120 dB. A fast log2 is allowed; error under 0.05 dB.
- **Key sources.** Self is the plugin's main input. Sidechain is the plugin's second input bus (mono or stereo). Per band.
- **Channel linking**, per instance: Linked (default) or Unlinked (decided 2026-11-16). Linked: each key channel is detected on its own and the band's level is the **maximum** of the channel levels (decided 2026-11-16, replacing the mean mixdown of draft 0.1); one gain and one frequency per band apply to every channel, which keeps the image still. A signal present in one channel of 64 drives the band exactly as it would in all of them, and nothing cancels between channels. The cost is one detector per band per key channel, not one per band. Unlinked: each main channel is its own key for Self bands. A sidechain band takes the maximum over the sidechain's channels in both cases. A maximum is defined on levels, not on signals, so the Track estimators, which need one signal, and the seeding bank run on the mean of the key's channels when linked: frequency estimation does not depend on level, and the remaining limit is that a component out of phase between channels can cancel for the tracker. **Channels left out of the link** (decided 2026-11-16): a channel the host's layout reports as LFE is left out by default; any other channel, such as a subwoofer feed in a discrete layout that names nothing, is left out by hand. A channel left out does not contribute to the maximum or to the trackers' mean and does not receive the linked gain and frequency: for Self bands it runs on its own key, as in Unlinked. Sidechain bands still apply to it. The set of channels left out is a per-instance list in the state chunk, set in the editor, not a host parameter and not automatable (*proposed*).
- **Listen**, per band: the band's detector output replaces the plugin output while held, a solo in place. Self: each channel hears its own detector. Sidechain: the mean of the sidechain's detector outputs, on every channel. Not automatable, not saved. In a DAW it acts at once; in a live host it is guarded, because it can put an actor's microphone on the main output (decided 2026-11-16; the mechanism is an open decision).
- **Metering out of the audio thread:** per band, the current gain offset, current frequency and tracker confidence are published through a lock-free snapshot for the editor at about 30 Hz.

## Gain modes

All three modes produce one number per sample, the dynamic offset in dB, added to the band's static gain and clamped to the Range. One code path; only the source of the offset differs.

**Static.** Offset 0.

**Dynamic.** A feed-forward gain computer in the dB domain (Giannoulis, Massberg and Reiss, 2012), with smoothing applied to the offset, not to the level.

```text
over   = softknee(L - T, W)            Above: reacts when L exceeds T
       = softknee(T - L, W)            Below: reacts when L falls under T
amount = over * (1 - 1/R)
offset = sign(Range) * min(amount, |Range|)     then attack / release smoothing
```

- Range negative: the band cuts as the key gets louder (de-ess, de-boom, ducking from the sidechain). Range positive: it boosts (upward expansion, or a band that opens when the actor speaks).
- The band travels between its static gain and static gain + Range, which is what the editor draws.
- **Return to zero** (decided 2026-11-16): an exponentially smoothed offset never reaches 0 by itself. When its target is 0 and it is within 0.0001 dB of it, the offset is set to exactly 0, in Dynamic and in Transient, so that an idle band is again an exact bypass. The step this makes is about 100 dB under the band's content.

**Transient.** Threshold-free differential envelopes in dB, in the manner of the SPL Transient Designer, per band.

```text
attackSig  = max(0, E_fastAttack - E_slowAttack)     same release on both
sustainSig = max(0, E_slowRelease - E_fastRelease)   same fast attack on both
offset     = attackAmt * attackSig + sustainAmt * sustainSig
offset     = clamp(offset, -|Range|, +|Range|) * floorGate(L)
```

- Attack and Sustain amounts run from −100 % to +100 %; 100 % is 1 dB of offset per dB of envelope difference.
- **Sensitivity floor:** a soft gate on the level, so a sustain boost does not lift the noise floor, the reverb tail or the room's ventilation between notes. The four envelopes run on `max(L, floor)`, not on L: measured from −120 dB, the first note after a silence would hold its attack signal far longer than the notes that follow.
- Time constants are four named constants (starting values: attack pair 1 ms and 15 ms, release pair 20 ms and 200 ms) scaled together by one Time parameter, 0.25× to 4×. The starting values are to be tuned by ear on drums, plucked strings and speech before the IDs' ranges are frozen.
- Reducing an attack without lookahead lets the leading edge through before the detector reacts. That is what the global lookahead is for; boosting an attack needs none.

There is no combined Dynamic + Transient mode: two bands at the same frequency do it.

## Frequency modes

Frequency is a second axis, independent of the gain mode, and applies to every band type. Track with a static boost is the auto-wah asked for; Track with a dynamic cut is a de-esser that follows each voice; Env on a low-pass or band-pass is the classic envelope filter.

**Fixed.** The band sits at its Frequency.

**Env.** The key's level, between a Sensitivity threshold and that threshold plus a Span, maps onto the range from Low to High (direction Up) or High to Low (Down), in log frequency. Its attack and release (`fatk`, `frel`) smooth the level before the mapping and are the only smoothing in Env; Speed belongs to Track.

**Range rules** (decided 2026-11-16), for Env and Track: if Low is above High the two are swapped; in Track, the rest frequency is the band's Frequency clamped into \[Low, High\].

**Track.** The band follows the dominant sinusoidal component of its key inside \[Low, High\].

- **Estimator: a frequency-locked notch, zero latency.** Two extra SVFs per Track band, in the detector path: the adapting filter described here and the extraction filter used for exclusion below. The adapting filter's centre frequency adapts until it sits on the component. Two candidates, to be compared in a Phase 0 spike and one chosen on the acceptance tests:
  1. a normalised-gradient adaptive notch (minimise the notch output power with respect to log frequency), in the line of Regalia's lattice ANF;
  2. an energy-ratio discriminator: in this SVF the high-pass and low-pass outputs have equal magnitude exactly at the centre frequency and their ratio grows as (f / fc)², so the smoothed `log(HP² / LP²) / 4` reads the detuning directly in log frequency. Proposed 2026-11-16, unproven here: it must be validated on real material, not only sines. Taken on the raw key, HP² and LP² are each dominated by the whole spectrum above or below fc, so the ratio reads spectral balance and not a peak; the spike must try it behind a band-pass at the current estimate.
- **Chosen by the Phase 0 spike, 2026-11-16, on synthetic signals and provisional until it has run on recordings:** candidate 1, as a frequency-locked loop on one SVF. With `bp = k v1`, `q = k v2` and `e = x − bp`: `log f −= mu · e · q / (bp² + q²)`, `mu = k / (Speed · fs)`. Candidate 2 on the raw key settles 23 to 28 cents off in noise at 0 dB SNR; behind a band-pass it locks well alone but moves 72 to 77 cents beside a neighbour a fifth away at 220 Hz.
- **Speed** sets the adaptation rate; the output frequency also glides in log frequency so tracking speed is the same in every octave.
- **Confidence** = 1 − (notch output power / notch input power), smoothed. Below the Confidence threshold the band holds its frequency for the Hold time, then glides back to its rest Frequency and the tracker is released. Optionally, confidence scales the band's dynamic offset and static boost, so a boost does not fizz on noise. From the spike: the two powers are smoothed over a few periods of the estimate, not over Speed (over Speed, a 40 dB drop takes 461 ms to show; over four periods, 35 ms); and Hold freezes the adaptation itself, not only the band's frequency, or the estimate drifts while held.

**Exclusion between Track bands** (decided 2026-11-16). Identity comes from continuity, not from loudness rank, so two peaks trading levels cause no swap.

```text
x_i[n] = key[n] - sum over j != i of lock_j * bp_j[n]
bp_j   = unity-peak band-pass of the raw key at tracker j's frequency
```

- Each tracker adapts on the key with the other locked trackers' components removed (symmetric deflation, detector path only). No hierarchy between bands: a strict cascade would let the first band drift onto the second's peak.
- **No delay in the deflation** (*corrected 2026-11-16*). Each tracker's extraction filter reads the raw key, not a deflated one, so there is no loop to break and the subtraction is in phase. The first draft subtracted the adapting filters' outputs one sample late; the residual is then 2 sin(ωT/2) of the component: −18 dB at 1 kHz and 48 kHz, only −6 dB at 4 kHz, and a gain above fs/6, which is where a de-esser tracks. The Phase 0 spike measured both: the plain delayed form leaves the other sine at −14 dB at 1.5 kHz and +0.1 dB at 9 kHz (44.1 kHz) and swaps; extraction from the raw key, and the delayed form rotated forward by one sample with the filter's quadrature pair, both remove it to the numerical floor and behave alike. The rotated form saves one SVF per Track band and is the fallback if Phase 7 needs it.
- **Onsets** (from the spike): a loud component appearing beside a quiet tracked one captures the quiet one's tracker at low frequency, before the extraction filter has responded. Adaptation is gated on onsets or on the other tracker's lock; the form is Phase 5 work.
- Scope: all Track bands with the same key source. Automatic.
- **Minimum spacing:** two trackers closer than a constant (start at 2 semitones) repel in log frequency.
- **Seeding.** The adaptation is local, so a released tracker is re-seeded rather than left to crawl: a shared bank of third-octave band-pass envelopes per key source (detector path only, about 30 SVFs) names the strongest region inside the band's range not already claimed by a locked tracker.
- **Crossings.** Two components gliding through each other may exchange trackers at the crossing; both bands are at the same frequency at that instant. A frequency-momentum term (predict from df/dt) to carry trackers through is a later refinement, not v1.
- The audio bands themselves are not deflated: exclusion exists only between detectors.

Tracking many simultaneous resonances is spectral processing and out of scope.

## Latency

Reported latency is 0 samples unless the user sets a lookahead, and is the same number whatever the bands do.

- Every filter is minimum-phase IIR; Static, Dynamic, Transient, Env and Track all run with no added delay.
- **Lookahead** is one per-instance choice: Off, 0.25 ms, 1 ms, 3 ms, converted to whole samples at the current rate. It delays the whole audio path once, ahead of band 1; detection reads the undelayed signals, so every detector sees the audio that much early.
- Lookahead is not automatable and is saved with the state. Changing it is the only event that calls the host's latency-changed notification. The delay line is allocated for the maximum at prepare time.
- No per-band lookahead: it would break the series sum.
- Bypass keeps the lookahead delay in the path and crossfades to the delayed dry signal, so bypassing changes neither the latency nor the timing.
- A test asserts that the reported latency and the measured impulse delay stay equal and unchanged across every combination of band modes.

## Parameters and state

The parameter list is declared in one table in `lib/dsp` (ID, name, unit, range, default, skew, automatable), from which the shims build the host's parameters. IDs are frozen once a build has been used to save a session; ranges and defaults marked *tune* may still move until the first public beta.

**Per instance**

| ID | Values | Default | Automatable |
| --- | --- | --- | --- |
| `bypass` | off, on | off | yes (host bypass) |
| `out_gain` | −24 to +24 dB | 0 | yes |
| `lookahead` | Off, 0.25 ms, 1 ms, 3 ms | Off | no |
| `link` | Linked (maximum over key channels), Unlinked | Linked | no |

**Per band** (`b1_` to `b8_`)

| ID suffix | Values | Default | Used by |
| --- | --- | --- | --- |
| `on` | off, on | off | all |
| `type` | Bell, Low shelf, High shelf, High-pass, Low-pass, Notch, Band-pass | Bell | all |
| `freq` | 20 to 20000 Hz, log | spread per band | all |
| `q` | 0.1 to 40, log | 1 | all |
| `gain` | −30 to +30 dB | 0 | bell, shelves |
| `qmode` | Proportional, Constant | Proportional | bell |
| `gmode` | Static, Dynamic, Transient | Static | bell, shelves |
| `fmode` | Fixed, Env, Track | Fixed | all |
| `key` | Self, Sidechain | Self | detector |
| `dfilt` | Follow, Own | Follow | detector |
| `dtype` | Band-pass, High-pass, Low-pass | Band-pass | detector, Own |
| `dfreq`, `dq` | as `freq`, `q` | 1000 Hz, 1 | detector, Own |
| `range` | −30 to +30 dB | −6 | Dynamic, Transient |
| `thresh` | −80 to 0 dB | −24 | Dynamic |
| `ratio` | 1 to 20 | 3 | Dynamic |
| `knee` | 0 to 24 dB | 6 | Dynamic |
| `atk`, `rel` | 0.05 to 200 ms, 5 to 2000 ms, log | 5, 120 | Dynamic |
| `ddir` | Above, Below | Above | Dynamic |
| `tatk`, `tsus` | −100 to +100 % | 0, 0 | Transient |
| `ttime` | 0.25× to 4× (*tune*) | 1× | Transient |
| `tfloor` | −90 to −20 dB | −60 | Transient |
| `flo`, `fhi` | as `freq` | one octave either side of the band's default `freq` (fixed values, not linked to `freq`) | Env, Track |
| `fspeed` | 1 to 1000 ms, log (*tune*) | 50 | Track |
| `fdir` | Up, Down | Up | Env |
| `fsens`, `fspan` | −80 to 0 dB, 6 to 60 dB | −40, 30 | Env |
| `fatk`, `frel` | as `atk`, `rel` | 5, 120 | Env |
| `fconf` | 0 to 100 % | 50 | Track |
| `fhold` | 0 to 2000 ms | 200 | Track |
| `fcgain` | off, on (confidence scales gain) | off | Track |

That is 35 parameters per band and 284 in all. Listen is an editor action, not a parameter. The list of channels left out of the link is state, not a parameter: it is stored by channel index together with the layout it was made for, and is reset to the host-reported LFE channels when the layout changes.

**State chunk.** JSON, `version` 1, written and read by `likyu::core` with locale-independent number text. Unknown fields are kept and written back; a reader exists for every version ever shipped, each with a fixture in `tests/state/`. A chunk with fewer bands than the build loads with the remaining bands off; one with more keeps the surplus bands in the chunk untouched. The suite runs under `C` and `fr_FR`, as in the OSCQuery plugin.

## Formats, identity, hosts

VST3 on Windows, macOS and Linux, AU on macOS, LV2 on all three, all through JUCE's own wrappers. CLAP comes later through a shim of our own, ported from the OSCQuery plugin's `src/shims/clap`; neither `clap-juce-extensions` nor `clap-wrapper` enters the build (decided 2026-11-16, consistent with 2026-11-04 there).

- **JUCE 9.0.5**, pinned at the same commit as WFS-DIY, Go.dot and the OSCQuery plugin. C++20. CMake 3.22.
- **Identity, fixed before the first session is saved against it:** name **Likyū** (a play on *l'EQ*) in the editor, the documentation and the website; ASCII **Likyu** for everything a host or a file system reads (product name, the plugin name reported to hosts, bundle and installer file names) and `likyu` in every identifier (decided 2026-11-16), company "Pix et Bel", manufacturer code `PxBl`, plugin code `Lkyu`, `JUCE_VST3_CAN_REPLACE_VST2=0` (decided 2026-11-16). *Decided 2026-11-16:* LV2 URIs `https://pixetbel.org/lv2/likyu/mono` and `https://pixetbel.org/lv2/likyu/stereo`, bundle ID `io.github.pob31.likyu`. Changing any of these later orphans saved sessions.
- **Buses:** main in and out with the same layout both sides. VST3 and AU negotiate it with the host at run time, any layout up to 64 channels; per-channel state is allocated in prepare. LV2 ports are fixed in the bundle's manifest, so LV2 ships two plugins, "Likyu" (stereo) and "Likyu mono", each with its own URI (to confirm against JUCE's LV2 wrapper in Phase 0, where LV2 first ships). One optional sidechain input bus, mono or stereo whatever the main width; a stereo sidechain is detected per channel and the maximum taken. The plugin must process correctly with the sidechain bus disabled or absent.
- **No MIDI**, not a synth.

**Host bench, Phase 1.** Sidechain routing is where hosts differ most, so it is measured before the editor assumes anything, and the findings are written to `docs/spikes/`:

| Host | Formats | To establish |
| --- | --- | --- |
| Go.dot (Tracktion Engine) | VST3, AU, LV2 | Loading, Self-keyed operation, state recall on cue launch. Sidechain routing is not benched here: Go.dot does not offer it yet (see Phase 8) |
| REAPER (Windows, macOS, Linux) | VST3, AU, LV2 | Channels 3 and 4 as sidechain; bus disabled |
| Bitwig Studio | VST3 | Sidechain selector; latency change on lookahead |
| Ableton Live | VST3, AU | Sidechain routing; 284 parameters in the automation chooser |
| Samplitude | VST3 | Sidechain support at all |

For each: sidechain connected and not, mono into stereo, the latency-change notification, state save and reload, whether multitouch reaches the plugin editor, pluginval at strictness 10, `auval` on macOS.

## Architecture and repository

JUCE for the shell, plain C++ for the DSP (decided 2026-11-16). The layout is the OSCQuery plugin's, with a stricter core.

| Directory | Target | Holds | May include |
| --- | --- | --- | --- |
| `lib/dsp/` | `likyu::dsp`, static | SVF, tan approximation, detectors, gain computers, trackers, seeding bank, lookahead, the band and the chain, the parameter table, parameter smoothing | The C++ standard library only. No JUCE header, no allocation after `prepare` |
| `src/core/` | `likyu::core`, static | Dispatch of host parameter values to the DSP, the state chunk, the meter snapshot, `rt/RtCheck` | `likyu::dsp`, `juce_core` |
| `src/editor/` | interface | The editor, a client of `likyu::core` | `juce_gui_basics` |
| `src/shims/juce/` | plugin | One `juce::AudioProcessor`: VST3, AU, LV2 | everything above |
| `src/shims/clap/` | later | Our own CLAP shim | CLAP C API, `clap-helpers` |
| `tests/` | executables | doctest suites, fixtures, reference curves, a bench runner |  |

Namespace and target prefix: `likyu`. Display name: Likyū. In this document "the DSP core" always means `lib/dsp`; `likyu::core` is named in full.

- **Why the DSP has no JUCE:** Go.dot or an app can link `lib/dsp` directly without a second copy of JUCE in the process, and the DSP suite builds and runs in seconds.
- **Layout for speed.** State is structure-of-arrays. The eight detectors of a key channel read the same signal in parallel (one set per key channel, linked or not) and fill one AVX2 register (two NEON registers); they are the SIMD lanes. The audio chain is serial per channel and stays scalar per channel, vectorised across channels where there are several.
- **Copied from the OSCQuery plugin, as is:** `CMakePresets.json`, `cmake/*ThirdParty.cmake` and `*Options.cmake` cut down, the `pins` CI job and the three `scripts/check-*.py`, the three-platform CI with ccache, pluginval and the validators' settings discipline, `rt/RtCheck` and `tests/support/RtAllocCounter.cpp`, the GPL file header, `THIRD_PARTY_NOTICES.md`, the `docs/` structure (PRD, devplan, bench, handoffs, spikes) and the rule that `CLAUDE.md` reproduces two PRD sections verbatim.
- **Not copied:** the shared core library and registry (no cross-instance state here), `lib/oscquery`, `juce_simpleweb`, the plugin's own server.
- Commits go straight to `main` for now, code included, with CI watched after each push (decided 2026-11-16, by the author, replacing the pull-request rule copied from the OSCQuery plugin). Never rewrite history on `main`.

## Realtime rules and performance

Everything in this plugin runs on the audio thread, so the lipogram is the product: no allocation, lock, exception, syscall or logging between `prepare` and `release`.

- `ScopedRealtimeCheck` regions around the process call; the test executables' replaced `operator new` counts violations; `[[clang::nonblocking]]` on the entry points under the RTSan CI job. A violation is a number that goes up, never a log line.
- All buffers, including the lookahead line at its maximum and at the highest supported rate, are sized in `prepare`.
- Parameters reach the audio thread as atomics read once per block; the meter snapshot leaves it through a lock-free single-writer slot.
- Sample rates 44.1 to 192 kHz; block sizes from 1 sample; variable block sizes; `process` with zero samples.
- No oversampling. The TPT warping near Nyquist at 44.1 and 48 kHz is accepted and documented, not corrected, in v1.

**Budget** (*proposed*, to be replaced by measurements in `docs/bench/`): a stereo instance with all eight bands in Dynamic + Track, stereo sidechain, at 96 kHz with 64-sample blocks, under 1 % of one core of the bench machine, with a worst block under 10 % of the block period. The bench runner prints median, p99, p999 and max per block, in the manner of WFS-DIY's `pipeline-bench`.

## Editor

One resizable window: the response curve on top, one strip for the selected band below. A plain `juce::Component` root, as in the OSCQuery plugin, so each shim can host it.

- **Curve.** The static response as a line, and the live response (static plus current offsets and current frequencies) as a second line moving over it. One node per band. Dragging the node's centre moves frequency and gain together; the node carries a crosshair whose horizontal arm drags frequency only and whose vertical arm drags gain only, so one can be moved without disturbing the other. Mouse wheel over a node sets Q.
- **What a node shows of its modes.** Dynamic and Transient: a vertical bracket from the static gain to static gain + Range, with the current offset as a moving mark. Env and Track: a horizontal bracket from Low to High, the node sliding inside it, dimmed while confidence is below threshold.
- **Detector.** With an Own detector filter, a ghost node at the detector's frequency, draggable. A key badge per node: Self or Sidechain, and a warning when Sidechain is chosen but not connected.
- **Band strip.** Only the controls of the selected band's current gain mode and frequency mode are shown; the others keep their values.
- **Listen** as a momentary button on the strip.
- **Analyser** (optional, off by default): spectrum of the input and of the sidechain, computed off the audio thread from a lock-free tap. It is a display aid and never feeds the DSP.
- Touch and the Go.dot control surfaces are served by the host parameters; the editor adds nothing a parameter cannot do, except Listen and the list of channels left out of the link.
- **Link list.** With more than one channel, a row of channel toggles beside the Link switch shows which channels are in the link; those the host reports as LFE start out of it and are marked as such.
- What a node and a strip say in each state is written as pure functions over the band's state, so headless tests assert it, as `SlotPresentation.h` does.

* **Multitouch**, on the systems where JUCE delivers it (Windows, Linux; macOS only where the host and OS pass touches through): several nodes dragged at once, one finger each; two fingers on one node pinch its Q, the touch equivalent of the wheel; the crosshair arms take touches as they take the mouse. Arms and nodes have touch-sized hit areas that do not overlap at the default window size. Whether a host forwards touches to a plugin editor at all is checked per host in Phase 1; the mouse path must always be complete without touch.

## Tests and acceptance

A feature is done when its row below passes in CI on the three platforms. Thresholds marked *proposed* are starting points to be confirmed by the first measurements and then written here with their date.

| Property | Test | Pass |
| --- | --- | --- |
| Exact bypass | Noise through 8 bell and shelf bands at 0 dB, every gain and frequency mode idle | Output equals input bit for bit |
| Exact bypass, return | Same, bands in Dynamic and Transient at 0 dB static: a loud passage, then a key under the threshold | Output equals input bit for bit within a stated time of the offset's target reaching 0 |
| Static response | Swept sine against the analytic magnitude of each type's bilinear-transformed (warped) response, 44.1 to 192 kHz | Within 0.1 dB up to 0.4 fs |
| Gain limit | Static gain and Range at their extremes, same sign | Effective gain never leaves −30 to +30 dB |
| Block-size invariance | The same input and automation rendered in blocks of 1, 7, 64 and 4096 samples, parameter changes aligned | Outputs equal (*proposed*: bit for bit) |
| tan approximation | fc from 20 Hz to 20 kHz at every rate | Under 1 cent |
| Modulation safety | Gain, Q and frequency stepped and swept at audio rate on a sine and on silence | No NaN, no growth, no click above −80 dB on silence |
| Constant latency | Impulse through every combination of band modes, per lookahead setting, bypass on and off | Measured delay = reported latency, unchanged by modes or bypass |
| Lookahead effect | Burst through a Transient band with negative attack, lookahead Off and 1 ms | With lookahead the leading edge is reduced; without, it passes |
| Dynamic curve | Stepped-level tone against the analytic gain computer | Within 0.25 dB at steady state; attack and release times within 10 % |
| Quadrature envelope | Steady tone at a 60 Hz band centre and half an octave either side, 1 ms time constants | Envelope ripple under 0.1 dB (*proposed*); level equals the band-pass magnitude within 0.1 dB |
| Detector rejection | Band-pass detector at 3 kHz, Q 1; tone at 100 Hz | Level follows the band-pass response (about −30 dB), not the low-pass |
| Transient | Synthetic burst with known attack and decay | Offset has the sign and timing of the settings; nothing moves below the floor |
| Transient, after silence | The same burst, first after 5 s of silence and then repeated | The first burst's offset matches the later ones (*proposed*: within 1 dB and 10 % in duration) |
| Env | Stepped-level tone between Sensitivity and Sensitivity + Span, Up and Down | Frequency follows the log mapping within 20 cents at steady state; attack and release within 10 % |
| Track, lock | Sine in noise at 0 dB SNR, inside the range | Locks within Speed × 5, error under 20 cents (*proposed*) |
| Track, no swap | Two sines a fifth apart, at 220 Hz and at 6 kHz, at 44.1 and 96 kHz, two Track bands locked at equal levels and then crossfaded ±12 dB, at the default Speed | Neither band's frequency moves by more than 50 cents |
| Track, exclusion | Same two sines, both bands released then re-seeded | The bands settle on different sines |
| Track, loss | Sine removed | Hold, then glide to rest; no jump |
| Sidechain fallback | Sidechain selected, bus absent | Band behaves as Self; status says so |
| Channel linking | Stereo signal with a transient on one side | Linked: identical gain on both channels |
| Linked maximum | A tone in one channel of 8, then the same tone in all 8; then the tone in two channels in opposite phase | The same offset in the three cases |
| Link exclusion | 5.1 with a burst on the LFE only, then on the left only; then a discrete 6-channel layout with channel 4 left out by hand | LFE burst moves the LFE's gain only; left burst moves every channel but the LFE; the same with the hand-set channel; the list survives a state round trip |
| Layouts | Mono, stereo, 5.1 and 64 channels; sidechain absent, mono and stereo | Processes correctly; no channel reads another's state when unlinked |
| State | Round trip; every shipped version's fixture; unknown fields; under `C` and `fr_FR` | Identical parameters; unknown fields written back |
| Realtime | 500 blocks of every mode under the allocation counter; RTSan job | Zero violations |
| Denormals | Signal then 10 s of silence | Block time does not rise |
| Formats | pluginval strictness 10 on VST3; `auval` on AU; LV2 validation (`lv2lint` or equivalent) | Clean |

Beside the automated suite, a listening pass on real material (drums, plucked strings, speech with sibilance, a full mix keyed by a voice) closes each of Phases 3 to 5, with notes in `docs/bench/`.

## Phases

Each phase ends with CI green, a bench note and a handoff note, as in the OSCQuery plugin. The detail of each phase is in the devplan, `docs/Devplan — Likyū (draft 0.1).md`.

| Phase | Delivers | Needs |
| --- | --- | --- |
| 0. Scaffold and spikes | Repository from the OSCQuery template; a plugin that passes audio, with its identity fixed, and already carrying what the host bench measures: the sidechain bus, the full parameter set of this PRD (inert, and not yet frozen: bench builds are not for saving sessions) and a lookahead switch that changes reported latency; VST3, AU and the two LV2 variants validated in CI. Spike: the two tracker candidates on recorded material, written up | Nothing outstanding |
| 1. Host bench | Sidechain bus behaviour per host, in `docs/spikes/` | Phase 0 build |
| 2. Static EQ | `lib/dsp`: SVF, tan, all types, Q modes, smoothing, the chain, exact-bypass and response tests; the parameter table; state chunk v1 |  |
| 3. Dynamic | Detector SVF, quadrature envelope, gain computer, key select, linking, sidechain fallback, Listen | Phase 1 findings |
| 4. Transient and lookahead | Differential envelopes, floor, global lookahead and its latency tests | Listening pass to set the time constants |
| 5. Frequency modes | Env; Track with the chosen estimator, confidence, hold; deflation, spacing, seeding bank | Phase 0 spike result |
| 6. Editor | Curve, nodes, brackets, strip, ghost detector node, analyser |  |
| 7. Performance and packaging | SIMD detector lanes, bench against the budget, installers, signing and notarisation from WFS-DIY's scripts |  |
| 8. Go.dot integration | Recall, control-surface mapping, the Listen guard; public beta (*proposed:* the beta does not wait for the sidechain in Go.dot) |  |
| 9. Sidechain in Go.dot | Routing a second source from a cue to the plugin's sidechain bus: work in Go.dot, not in this plugin, taken up once the rest of Go.dot is thoroughly tested (decided 2026-11-16). Until then Go.dot feeds the sidechain bus silence (checked in its source 2026-11-16), so a Sidechain band there is keyed by silence and does not fall back to Self; the editor says so, and the reference use case is exercised in the other bench hosts | Go.dot |
| Later | CLAP shim; native linking of `lib/dsp` in Go.dot; frequency momentum for crossings; a wide LV2 variant if one is asked for |  |

Phases 2 to 5 are testable headless before any editor exists: a host's generic parameter view is enough to listen.

## Open decisions

Nothing blocks Phase 0 any more. The three identity items are decided; they are part of an identity that cannot change once a session is saved.

- [x] **Name:** Likyū, `likyu` in identifiers (decided 2026-11-16).
- [x] **Plugin code:** `Lkyu`, under manufacturer `PxBl` (decided 2026-11-16).
- [x] **LV2 URIs and bundle ID:** `https://pixetbel.org/lv2/likyu/mono`, `https://pixetbel.org/lv2/likyu/stereo`, `io.github.pob31.likyu` (decided 2026-11-16).
- [x] **Host-visible name:** `Likyu` for hosts and files, Likyū in the editor and the documentation (decided 2026-11-16).
- [x] **Linked key:** maximum over the key's channel levels (decided 2026-11-16).
- [x] **Sidechain fallback:** Self (decided 2026-11-16).
- [x] **Precision, effective gain limit, return to zero, range rules, Speed for Track only:** as written (decided 2026-11-16).
- [x] **LFE channel:** left out of the link when the host reports it as LFE, otherwise left out by hand (decided 2026-11-16).
- [ ] **Link list, form:** a per-instance list in the state chunk, set in the editor and reset when the layout changes, as proposed; or host parameters, if Go.dot's control surfaces must reach it.
- [ ] **Listen guard, mechanism:** how the plugin knows it is in a live host and what the guard is. *Proposed:* guarded when the host identifies itself as Go.dot, or when a per-machine preference says so; the guard is an Arm switch on the strip that must be on before Listen acts, off each time the editor opens. Settled with the Go.dot integration at the latest.
- [x] **Quadrature envelope:** confirmed by the Phase 0 spike (2026-11-16).
- [ ] **Deflation form:** extraction from the raw key, confirmed on synthetic signals with two trackers; to hold on recordings and with eight.
- [ ] **Confidence threshold:** as defined, confidence reads 0.51 for a sine in noise at 0 dB SNR, which is the default `fconf` of 50 % and the level the *Track, lock* row asks a lock at. Either the default comes down (*proposed:* 30 %), or confidence is measured against the key's power inside \[Low, High\] and not over the whole spectrum.
- [ ] **Speed and close low pairs:** two sines 110 Hz apart wobble a tracker by 109 cents at Speed 20 ms, 46 at 50 ms, 23 at 100 ms. Whether the low end of `fspeed` (1 ms) stays, with that as documented behaviour, or the range is narrowed.
- [ ] **Tracker estimator:** the gradient notch, chosen by the Phase 0 spike on synthetic signals (2026-11-16); confirmed or not when the spike runs on the author's recordings.
- [ ] **Transient time constants and the Time range**, settled by the Phase 4 listening pass, before those IDs' ranges freeze.
- [ ] **Performance budget**: confirm or replace the proposed 1 % figure once Phase 2 gives a first measurement.
- [ ] **Band-pass as a band type** in a series EQ: kept for envelope-filter use; drop it if it proves a trap in practice.
- [x] **Sidechain in Go.dot:** Go.dot work, implemented later, once the rest of the app is thoroughly tested (decided 2026-11-16). It is its own phase, after the integration.
- [ ] **Public beta and the Go.dot sidechain:** whether the beta goes out at the end of Phase 8, without the sidechain in Go.dot, as proposed, or waits for Phase 9.

**Decided so far**

| Date | Decision |
| --- | --- |
| 2026-11-16 | Detection from the plugin input or the sidechain, never between stages |
| 2026-11-16 | Eight bands, a build constant; string IDs so the count can grow |
| 2026-11-16 | Polynomial (rational near Nyquist) tan for g |
| 2026-11-16 | Frequency mode is a second axis: Fixed, Env, Track |
| 2026-11-16 | Track bands exclude each other by symmetric deflation; identity by continuity |
| 2026-11-16 | JUCE 9.0.5 for the shell, no JUCE in the DSP library |
| 2026-11-16 | VST3, AU and LV2 first; CLAP later through our own shim |
| 2026-11-16 | GPLv3 |
| 2026-11-16 | Not a target for WFS-DIY's per-output EQs |
| 2026-11-16 | Channel count set by the host on VST3 and AU, up to 64; mono and stereo variants on LV2 |
| 2026-11-16 | Channels linked by default; a linked level is the maximum over the key's channels (trackers on their mean) |
| 2026-11-16 | Name Likyū in the editor and documentation, Likyu for hosts and files, likyu in identifiers; plugin code Lkyu |
| 2026-11-16 | Sidechain absent or disabled: fall back to Self |
| 2026-11-16 | LFE left out of the link when the host reports it, any channel by hand otherwise |
| 2026-11-16 | Sidechain routing in Go.dot is Go.dot work, done later, after the app is thoroughly tested |
| 2026-11-16 | No pull requests for now: commits go straight to `main` |
| 2026-11-16 | Listen acts at once in a DAW and is guarded in a live host |
| 2026-11-16 | Effective gain limited to ±30 dB; offsets return to exactly 0; double in the audio path, float in detection |
| 2026-11-16 | Band-pass detector envelope from BP² − LP·HP; deflation without delay, from the raw key |
| 2026-11-16 | LV2 URIs on pixetbel.org (mono, stereo), bundle ID io.github.pob31.likyu |
| 2026-11-16 | Editor: crosshair arms for frequency-only and gain-only drags, wheel or pinch for Q, multitouch where supported |
