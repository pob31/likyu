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
| Frequency, Q, Gain | 20 Hz to 20 kHz, 0.1 to 40, −30 to +30 dB | Frequency is the rest value in Track; Env rests at the start of its mapping (decided 2026-11-20) |
| Q mode | Proportional (default), Constant | Bell only |
| Gain mode | Static, Dynamic, Transient | Bell and shelves only; other types are Static |
| Frequency mode | Fixed, Env, Track | All types |
| Key source | Self (plugin input), Sidechain | Per band |
| Detector filter | Follow band, Own (type, frequency, Q) | Own types: band-pass, high-pass, low-pass |
| Range | −30 to +30 dB | Limit of the dynamic gain offset, and its sign in Dynamic mode |
| Channels | Every channel of the instance (default), or any of them | Per band; state, not a parameter (decided 2026-11-20) |

**Rules**

- Parameter IDs are strings of the form `b<n>_<param>` (`b3_freq`), frozen for good, never renamed, never positional. Underscore, not dot: LV2 symbols allow `[_a-zA-Z][_a-zA-Z0-9]*` only. Raising the band count later adds IDs and renumbers none.
- The host sees every parameter in real units with its own range (Hz, dB, ms), unlike the OSCQuery plugin's normalised slots.
- The effective gain of a band is `static gain + dynamic offset`, in dB, the offset clamped to the Range and the sum clamped to −30 to +30 dB (decided 2026-11-16). Static mode: offset 0.
- All detection reads the plugin input (Self) or the sidechain bus, both undelayed. No detector reads another band's output.
- Sidechain selected but the sidechain bus absent or disabled: the band falls back to Self and the editor says so. A bus that is enabled but silent cannot be told from a quiet key and is treated as a key.
- Track bands that share a key source exclude each other automatically. There is no user-defined set. Bands keyed by the input that apply to different channels read different keys, and do not exclude each other (2026-11-20).
- A band applies to a set of the instance's channels, every channel by default (decided 2026-11-20): a low cut can spare the LFE, and another band be the LFE's alone. A channel a band leaves out is not filtered by it, is no part of its key, and has none of its gain or frequency. The sets are state, not parameters: kept by channel index with the layout they were made for, and applied only in that layout; in any other, every band applies to every channel. Changing a set is click-free, like a type switch.
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

- **The arithmetic of the tick** (*built 2026-11-21, Phase 7; decided the same day by the author: fused, explicitly*). Where the processor has a fused multiply-add, each line above is asked of it, once, in `dsp/Svf.h`, and every kernel repeats that form: `v1 = mulAdd (a1, ic1, a2 v3)`, `v2 = mulAdd (a3, v3, mulAdd (a2, ic1, ic2))`, `ic = mulAdd (2, v, −ic)`, `y = mulAdd (m2, v2, mulAdd (m1, v1, m0 x))`. The one-poles, both forms of tan, the fast log and the coefficients are written the same way, and 2^x is `pow2` (`dsp/Exp2.h`), our own, exactly 1 at 0 and within 2 units in the last place. The values are those of the formulas to the last bit or two; the exactness below is unchanged.
- **Exact bypass.** At A = 1 a bell or shelf gives m0 = 1, m1 = m2 = 0: the output is the input, sample for sample. This is a tested property, not an approximation. It requires that the dB-to-A conversion return exactly 1 at 0 dB, whatever fast approximation is used, and that a dynamic offset come back to exactly 0 (see *Return to zero* under Gain modes).
- **Proportional Q** (default for bells): the bell narrows as gain grows and boost and cut are mirror images. A dynamic cut starts broad and tightens as it digs in. Constant Q is a per-band option.
- **Update rates.** The mix terms and k follow the gain every sample. g is recomputed on a 16-sample control sub-block and interpolated linearly in between, in Env and Track modes and during parameter smoothing; a1 to a3 follow from the interpolated g. The sub-block length is a constant to be confirmed by the modulation tests (*confirmed 2026-11-20: 16 samples*; the modulation-safety tests pass with it, and on the fastest move a band can make, 20 Hz to 20 kHz in one 20 ms ramp, the linear interpolation of g bends the glide by at most 44 cents near 20 kHz at 44.1 kHz, inside steps that each move some 217 cents; on a one-octave move at 1 kHz, under 0.04 cent; `docs/bench/2026-11-20-phase2.md`). The sub-block counter runs across process calls, so the output does not depend on how the host cuts its blocks: bit for bit, measured 2026-11-20. A target set between two blocks takes effect at the next sub-block boundary. *Built 2026-11-20 (Phase 5):* in Env and Track, a lane's frequency at the end of one control step is the g its filter reaches at the end of the next, so the frequency path is one control step late (0.36 ms at 44.1 kHz) and the output still depends on nothing but the sample index; on a glide of three octaves a second, the filter stays within 1.4 cents of its tracker. A sample whose mix is exactly the identity is not written, whether g moves or not, so an Env or Track bell at 0 dB is the input bit for bit (+5 % on the bench with every ramp running).
- **tan by polynomial** (decided 2026-11-16). `g = tan(pi fc / fs)` uses an approximation with no library call. With fc up to 20 kHz the argument reaches 0.45 π at 44.1 kHz, close to the pole, so a plain odd polynomial is only allowed where the argument stays below 0.25 π (fs ≥ 88.2 kHz); below that, a rational form (odd polynomial over even polynomial, one divide). Acceptance: frequency error under 1 cent over 20 Hz to 20 kHz at every supported rate. *Built 2026-11-20:* the rational form is Lambert's continued fraction for tan cut at depth 6 (integer coefficients), under 1e-6 cent below 88.2 kHz; the polynomial is odd, of degree 11, fitted on relative error, under 0.0004 cent from 88.2 kHz. Frequencies are clamped to min(20 kHz, 0.46 fs), which keeps a host's 32 kHz inside the rational form's domain; at 44.1 kHz 20 kHz is 0.4535 π.
- **Shelves under modulation** need g rescaled by √A: a multiply by a square root, no new tan.
- Denormals: flush-to-zero and denormals-are-zero set in the process call, and tested with silence after signal. *Found 2026-11-20:* flush-to-zero alone does not end a decay in this structure: once the products underflow, `ic = 2 v − ic` flips the sign of a state forever at the edge of the normal range, and the output holds there (1.6e-307) instead of reaching 0. A state under 1e-20 is set to 0 at each sub-block boundary.
- Parameter smoothing: user moves arrive at control rate and are smoothed in `lib/dsp` (frequency in log domain, gain in dB), about 20 ms, so that a native client of the library gets the same smoothing as the plugin. *Built 2026-11-20:* linear ramps of 20 ms (Q in log domain too) that land exactly on their target, so a gain ramped back to 0 dB is an exact bypass again.
- Switches (*built 2026-11-20*): on/off, type and Q mode do not ramp. The mix crossfades to the identity (1, 0, 0), the change is made there, and the mix crossfades back, 10 ms each way along a smoothstep; the filter state carries across. A linear crossfade sprayed −52 dB above 8 kHz around a switch, the smoothstep −81 dB at worst (the *Modulation safety* row). The instance bypass crossfades the same way.
- **Precision** (decided 2026-11-16): the audio path keeps its states and coefficients in double, for low frequencies with high Q at high sample rates; the detection path is float, eight lanes to an AVX2 register. The host's buffers may be either precision. *Measured 2026-11-20* (`docs/bench/2026-11-20-phase2.md`): at 20 Hz, Q 40, 192 kHz, a band in float strays from the same band in long double by −48 dB relative to its output (low-pass) and −94 dB (bell +12 dB); in double by −225 dB at worst (low-pass) and −263 dB (bell +12 dB). At that frequency float keeps only 0.7 % of the precision of `1 − a1`, so the float filter's Q is off: the decision stands.

## Detection

Each band owns one detector SVF per channel of its key (the main input's channels for Self, the sidechain's one or two for Sidechain). Each reads its key channel and produces a level in dB; linking decides how those levels are combined for the gain computer and the envelope follower.

- **Detector filter.** *Follow band* copies the band's current frequency and Q (including a moving frequency in Env and Track) as a band-pass. *Own* gives the detector its own type (band-pass, high-pass, low-pass), frequency and Q.
- **Quadrature envelope for band-pass detectors** (corrected 2026-11-16; confirmed the same day by the Phase 0 spike, `docs/spikes/phase0-tracker.md`: within 0.0001 dB of the band-pass magnitude at every tone tried, no ripple). `env = k * sqrt(max(0, v1² − v2 * hp))`, with `hp = x − k * v1 − v2`. In the TPT form the low-pass and high-pass outputs are each one integrator away from the band-pass, so LP · HP = BP² holds exactly: for a single sine the expression is the band-pass magnitude at any frequency, with no ripple and with the band-pass's own selectivity. With several components the cross terms can go negative for an instant, hence the max. The first draft's `k * sqrt(v1² + v2²)` is rejected: v2 is the low-pass output, so everything below fc reached the detector at gain k (unity at Q = 1). This is what allows millisecond time constants on low bands without the envelope ripple becoming distortion. *Found 2026-11-20* (`docs/bench/2026-11-20-phase3.md`): at the first sample of a signal after silence the expression is exactly 0, so the level there is the floor; and on a noise key it dips to 0 for an instant now and then (the envelope of a narrow band of noise has deep troughs, and the cross terms do the rest). An Above band acts a little less for that instant; a Below band acts, by hundredths of a dB at a 5 ms attack, so a Below band is an exact bypass on a steady key over its threshold, not on broadband material. High-pass and low-pass detectors use a peak follower whose shortest time constant is bounded to a few periods of the detector frequency (*decided 2026-11-20 by the author, as written*: instant attack, release τ = 4 periods of the detector frequency, for high-pass and low-pass alike; a low-pass set far above its content ripples between the content's peaks, which the band's attack and release on the offset smooth; to be judged by ear in the listening pass).
- **Level.** `L = 20 log10(env)`, floored at −120 dB. A fast log2 is allowed; error under 0.05 dB.
- **Key sources.** Self is the plugin's main input. Sidechain is the plugin's second input bus (mono or stereo). Per band.
- **Channel linking**, per instance: Linked (default) or Unlinked (decided 2026-11-16). Linked: each key channel is detected on its own and the band's level is the **maximum** of the channel levels (decided 2026-11-16, replacing the mean mixdown of draft 0.1); one gain and one frequency per band apply to every channel, which keeps the image still. A signal present in one channel of 64 drives the band exactly as it would in all of them, and nothing cancels between channels. The cost is one detector per band per key channel, not one per band. Unlinked: each main channel is its own key for Self bands, and so has its own gain and its own coefficients, every sample while the gain moves (*measured 2026-11-20*: 64 channels Unlinked take 1.5 cores in the scalar build, against 11.8 % linked; measured again later the same day, 1.0 core from Phase 3's build and Phase 4's alike, against 12 %: the machine's state moves the figure, not the code; Phase 7's SIMD across channels is the planned remedy). A sidechain band takes the maximum over the sidechain's channels in both cases. A maximum is defined on levels, not on signals, so the Track estimators, which need one signal, and the seeding bank run on the mean of the key's channels when linked: frequency estimation does not depend on level, and the remaining limit is that a component out of phase between channels can cancel for the tracker. **Channels left out of the link** (*retired 2026-11-20 by the author*: a band's own channels, Band model, do what this list was for, an LFE spared a band's cut or its link; what follows is the record) (decided 2026-11-16, amended 2026-11-18): every channel starts in the link, whatever the host's layout calls it; the user leaves a channel out by hand, an LFE or a subwoofer feed alike. Hosts name a layout from its channel count (REAPER calls any four-channel track L R C Lfe and any six-channel one 5.1, Phase 1 bench), and hexaphonic or octophonic material has no LFE, so a reported LFE says nothing certain of what the channel holds. A channel left out does not contribute to the maximum or to the trackers' mean and does not receive the linked gain and frequency: for Self bands it runs on its own key, as in Unlinked. Sidechain bands still apply to it. The set of channels left out is a per-instance list in the state chunk, set in the editor, not a host parameter and not automatable (*decided 2026-11-20 by the author*).
- **Listen**, per band: the band's detector output replaces the plugin output while held, a solo in place. Self: each channel hears its own detector. Sidechain: the mean of the sidechain's detector outputs, on every channel. Not automatable, not saved. In a DAW it acts at once; in a live host it is guarded, because it can put an actor's microphone on the main output (decided 2026-11-16; the mechanism is an open decision). *Decided 2026-11-20 by the author:* the guard is a long press, in every host. The Listen button does nothing on a tap; held for 0.7 s it puts the band's detector on the output, until it is released. No host is recognised and there is no Arm switch. *Amended 2026-11-21 by the author:* Listen latches. Held for 0.7 s it starts, the button lit, and stays on after the release; a short press stops it, and so do selecting another band, the band losing its detector, and the editor closing.
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
- **Attack and release** (*decided 2026-11-20 by the author*): attack is the offset moving away from 0, the band acting; release is the offset coming back to 0, the band letting go. The same rule in Above and Below, so in Below the attack is the cut deepening as the key gets quiet. Each is a one-pole time constant on the offset. *Built 2026-11-20:* the offset is kept in double although the detection path is float: in float the one-pole stops moving once its step is under half a unit in the last place, 0.37 dB short of a −30 dB target at 192 kHz with a 2 s release. Threshold, knee, ratio and Range are smoothed over 20 ms like the band's own gain, so that their automation is not a step in the gain.
- **Return to zero** (decided 2026-11-16): an exponentially smoothed offset never reaches 0 by itself. When its target is 0 and it is within 0.0001 dB of it, the offset is set to exactly 0, in Dynamic and in Transient, so that an idle band is again an exact bypass. The step this makes is about 100 dB under the band's content.

**Transient.** Threshold-free differential envelopes in dB, in the manner of the SPL Transient Designer, per band.

```text
attackSig  = max(0, E_fastAttack - E_slowAttack)     same release on both
sustainSig = max(0, E_slowRelease - E_fastRelease)   same fast attack on both
offset     = attackAmt * attackSig + sustainAmt * sustainSig
offset     = clamp(offset, -|Range|, +|Range|) * floorGate(E_fast)
```

- **The shared constants** (*decided 2026-11-20 by the author*): the attack pair releases at the fast release, the sustain pair attacks at the fast attack. E_fastAttack and E_fastRelease are then one envelope, E_fast (fast attack, fast release), whatever the four constants are, so three envelopes run per lane: E_fast, E_slowAttack (slow attack, fast release), E_slowRelease (fast attack, slow release). Keeping the two fast envelopes apart would take a fifth constant.
- **The gate reads E_fast** (*decided 2026-11-20 by the author*, replacing `floorGate(L)`): on a noise key the band-pass level dips to the floor for an instant now and then (*Detection*), and a gate on L sample by sample would blip the gain there. It is a smoothstep from the floor to the floor + 6 dB (*proposed*), so nothing moves while E_fast is at the floor; once the key is under the floor, the offset reaches exactly 0 within a stated time (*Exact bypass, return*).

- Attack and Sustain amounts run from −100 % to +100 %; 100 % is 1 dB of offset per dB of envelope difference.
- **Sensitivity floor:** a soft gate on the level, so a sustain boost does not lift the noise floor, the reverb tail or the room's ventilation between notes. The four envelopes run on `max(L, floor)`, not on L: measured from −120 dB, the first note after a silence would hold its attack signal far longer than the notes that follow.
- Time constants are four named constants (starting values: attack pair 1 ms and 15 ms, release pair 20 ms and 200 ms) scaled together by one Time parameter, 0.25× to 4×. The starting values are to be tuned by ear on drums, plucked strings and speech before the IDs' ranges are frozen. *Until then* (decided 2026-11-20 by the author) the four are temporary host parameters, `tmp_tfa`, `tmp_tsa`, `tmp_tfr` and `tmp_tsr`, outside the table like `tmp_listen`: not automatable, not saved, never frozen, removed once the values are decided.
- *Built 2026-11-20* (`docs/bench/2026-11-20-phase4.md`): the envelopes are kept in double, like Dynamic's offset (a float one-pole stalls short of its target, and two stalled at different places leave a sustain signal that never returns to 0). A lane's envelopes start at the first level they read, so a switch made with the detector already running (a link change, Dynamic to Transient) fires no false attack. The amounts and the floor ramp over 20 ms; Time and the constants change rates at the next grid point. Each signal's peak is within 0.0002 dB, and its time within 0.5 %, of the one-poles it is made of, at Time 0.5×, 1× and 2×.
- Reducing an attack without lookahead lets the leading edge through before the detector reacts. That is what the global lookahead is for; boosting an attack needs none. *Measured 2026-11-20*: a 3 kHz burst through a Transient bell, attack −100 %, Range 12 dB. With 1 ms of lookahead the band is, to every digit, the static bell at −12 dB over the burst's first 2 ms, the most a lookahead can give (−8.7 dB over the first 0.5 ms; a bell's band-pass takes a few samples to answer a sudden sine). Without, the first 0.125 ms passes at −0.4 dB and the first 0.5 ms at −4.9 dB.

There is no combined Dynamic + Transient mode: two bands at the same frequency do it.

## Frequency modes

Frequency is a second axis, independent of the gain mode, and applies to every band type. Track with a static boost is the auto-wah asked for; Track with a dynamic cut is a de-esser that follows each voice; Env on a low-pass or band-pass is the classic envelope filter.

**Fixed.** The band sits at its Frequency.

**Env.** The key's level, between a Sensitivity threshold and that threshold plus a Span, maps onto the range from Low to High (direction Up) or High to Low (Down), in log frequency. Its attack and release (`fatk`, `frel`) smooth the level before the mapping and are the only smoothing in Env; Speed belongs to Track. Under Sensitivity the band rests at the start of its mapping, Low for Up and High for Down; the band's Frequency is not used in Env (*decided 2026-11-20 by the author*: the Band model's note had read "the rest value in Env and Track", which the mapping contradicted). *Built 2026-11-20* (`docs/bench/2026-11-20-phase5.md`): the follower is a one-pole in dB on clamp(L, Sensitivity, Sensitivity + Span), attack while the level rises: a note after a silence starts at Sensitivity, as the next does, and a level far over the span comes back into it as soon as it falls (Phase 4's lesson for the transient envelopes, at both ends). It starts at the first level it reads, and at Sensitivity it rests exactly, so a quiet key leaves the band exactly at Low or High. Sensitivity and Span ramp over 20 ms, like Low and High. Measured: within 0.2 cents of the mapping at steady state, attack 5.12 ms for 5 and release 120.1 ms for 120, at 44.1 and 96 kHz, Up and Down; with a Follow detector, which follows the moving band, the band settles within 0.3 cents of the frequency its own band-pass response puts it at.

**Range rules** (decided 2026-11-16), for Env and Track: if Low is above High the two are swapped; in Track, the rest frequency is the band's Frequency clamped into \[Low, High\].

**Track.** The band follows the dominant sinusoidal component of its key inside \[Low, High\].

- **Estimator: a frequency-locked notch, zero latency.** Two extra SVFs per Track band, in the detector path: the adapting filter described here and the extraction filter used for exclusion below. The adapting filter's centre frequency adapts until it sits on the component. Two candidates, to be compared in a Phase 0 spike and one chosen on the acceptance tests:
  1. a normalised-gradient adaptive notch (minimise the notch output power with respect to log frequency), in the line of Regalia's lattice ANF;
  2. an energy-ratio discriminator: in this SVF the high-pass and low-pass outputs have equal magnitude exactly at the centre frequency and their ratio grows as (f / fc)², so the smoothed `log(HP² / LP²) / 4` reads the detuning directly in log frequency. Proposed 2026-11-16, unproven here: it must be validated on real material, not only sines. Taken on the raw key, HP² and LP² are each dominated by the whole spectrum above or below fc, so the ratio reads spectral balance and not a peak; the spike must try it behind a band-pass at the current estimate.
- **Chosen by the Phase 0 spike, 2026-11-16, on synthetic signals and provisional until it has run on recordings:** candidate 1, as a frequency-locked loop on one SVF. With `bp = k v1`, `q = k v2` and `e = x − bp`: `log f −= mu · e · q / (bp² + q²)`, `mu = k / (Speed · fs)`. Candidate 2 on the raw key settles 23 to 28 cents off in noise at 0 dB SNR; behind a band-pass it locks well alone but moves 72 to 77 cents beside a neighbour a fifth away at 220 Hz.
- **Speed** sets the adaptation rate; the output frequency also glides in log frequency so tracking speed is the same in every octave. *Built 2026-11-20:* the glide is a one-pole of a quarter of Speed toward the estimate (locked, held) or the rest (released), which snaps to the rest within 0.01 cent: a released band is then the band at its own Frequency, the Fixed band bit for bit. The loop is the spike's, in log2 Hz (its step divided by ln 2), its filter of Q 4 retuned every sample; the estimate, the output and the powers are double, since a float estimate stalls at long Speed and high rates, as Dynamic's offset did.
- **Confidence** = 1 − (notch output power / notch input power), smoothed. Below the Confidence threshold the band holds its frequency for the Hold time, then glides back to its rest Frequency and the tracker is released. Optionally, confidence scales the band's dynamic offset and static boost, so a boost does not fizz on noise. From the spike: the two powers are smoothed over a few periods of the estimate, not over Speed (over Speed, a 40 dB drop takes 461 ms to show; over four periods, 35 ms); and Hold freezes the adaptation itself, not only the band's frequency, or the estimate drifts while held. *Built 2026-11-20*, each from a failing test (`docs/bench/2026-11-20-phase5.md`):
  - the powers are smoothed over four periods or 5 ms, whichever is longer: over 0.7 ms at 6 kHz, the confidence of noise jitters across the threshold, and each crossing locks, holds, freezes and releases;
  - a jump of the input (its power over one period 10 dB from the smoothed power) makes both powers follow at one period until they are within 1 dB again, and a locked loop stops meanwhile: smoothed together, the two powers decay together after a sine stops and hold confidence up for τ ln(SNR), while the loop slides 400 cents on what is left;
  - a released tracker locks once confidence has stayed at the threshold for a quarter of Speed: crossing it for an instant 200 cents short of its sine, then holding there, made a lock take 380 ms instead of 170;
  - on white noise alone, confidence reads more the higher the tracker sits (a notch of Q 4 removes more of it): at most 0.02 at 1 kHz and 0.20 at 16 kHz, under the 30 % default; at 0 dB SNR, 0.51; on a clean sine, 1.0; on silence, 0.
- **Confidence scales the gain** (`fcgain`, built 2026-11-20): the dynamic offset, and the static gain where it is a boost, times 1 − w (1 − confidence), w ramping from 0 to 1 over 20 ms when `fcgain` is turned on, so that turning it on or off is not a step in the gain. A cut is not scaled: bit for bit the band without it.

**Exclusion between Track bands** (decided 2026-11-16). Identity comes from continuity, not from loudness rank, so two peaks trading levels cause no swap.

```text
x_i[n] = key[n] - sum over j != i of lock_j * bp_j[n]
bp_j   = unity-peak band-pass of the raw key at tracker j's frequency
```

- Each tracker adapts on the key with the other locked trackers' components removed (symmetric deflation, detector path only). No hierarchy between bands: a strict cascade would let the first band drift onto the second's peak.
- **No delay in the deflation** (*corrected 2026-11-16*). Each tracker's extraction filter reads the raw key, not a deflated one, so there is no loop to break and the subtraction is in phase. The first draft subtracted the adapting filters' outputs one sample late; the residual is then 2 sin(ωT/2) of the component: −18 dB at 1 kHz and 48 kHz, only −6 dB at 4 kHz, and a gain above fs/6, which is where a de-esser tracks. The Phase 0 spike measured both: the plain delayed form leaves the other sine at −14 dB at 1.5 kHz and +0.1 dB at 9 kHz (44.1 kHz) and swaps; extraction from the raw key, and the delayed form rotated forward by one sample with the filter's quadrature pair, both remove it to the numerical floor and behave alike. The rotated form saves one SVF per Track band and is the fallback if Phase 7 needs it.
- **Onsets** (from the spike): a loud component appearing beside a quiet tracked one captures the quiet one's tracker at low frequency, before the extraction filter has responded. Adaptation is gated on onsets or on the other tracker's lock; the form is Phase 5 work. *Built 2026-11-20:* the jump of the input (under Confidence above) is the gate: the quiet one's loop stops at once, its confidence falls and it holds while another tracker of the key is seeded on the newcomer, locks, and takes it out of the quiet one's input. Measured, a sine appearing 24 dB over a quiet tracked one a fifth below: the quiet one's band strays 6 cents at 220 Hz and 10 at 1 kHz (the spike: 726 to 769 cents at 220 Hz under every deflation).
- **Lock weights and the first lock** (*built 2026-11-20*): `lock_j` is 1 while tracker j is locked, and its confidence over the threshold, at most 1, otherwise: since confidence is measured over the whole key, with only the locked deflated eight equal sines leave every tracker at 1/8 and none ever locks; graded, all eight lock, one each. A lock is refused within the minimum spacing of a tracker that is locked or held, and of two locked or held that close, the one locked more recently is released: the first to lock on a component keeps it. Two trackers acquiring one sine together otherwise both locked and straddled it, 130 cents apart.
- Scope: all Track bands with the same key source. Automatic.
- **Minimum spacing:** two trackers closer than a constant (start at 2 semitones) repel in log frequency. *Built 2026-11-20:* only a released tracker is pushed, at its own Speed; a locked or held one is not (a released one acquiring the same sine pushed a locked one 70 cents off it).
- **Seeding.** The adaptation is local, so a released tracker is re-seeded rather than left to crawl: a shared bank of third-octave band-pass envelopes per key source (detector path only, about 30 SVFs) names the strongest region inside the band's range not already claimed by a locked tracker. *Built 2026-11-20:* the thirty ISO third octaves from 25 Hz to 20 kHz (those under 0.46 fs), Q 4.32, quadrature envelopes over four periods or 5 ms, one bank per key source and lane, read at each grid point. A region is claimed by every tracker of the key whose estimate is in it, the locked and held first, then the released in band order; a released tracker moves to the strongest unclaimed region in its range when that one is 6 dB louder than its own, or when its own is claimed. Measured: 0.72 % of a core per bank at 96 kHz, on the bench machine.
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
- *Built 2026-11-20 (Phase 4):* the delay is the chain's own, in `lib/dsp`, so a native client gets it too. One line per host sample type, so that a sample is moved and never converted: an idle chain outputs the input delayed, bit for bit, −0 and denormals included. It runs in place on the host's buffer one 16-sample control step at a time, after that step's key has been read, so the key may be the buffer itself; a change of the delay lands on the grid. The line runs at every setting, Off included, so that turning the lookahead on reads real history: it costs some 4 % of the static EQ (`docs/bench/2026-11-20-phase4.md`).

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
| `fconf` | 0 to 100 % | 30 | Track |
| `fhold` | 0 to 2000 ms | 200 | Track |
| `fcgain` | off, on (confidence scales gain) | off | Track |

That is 35 parameters per band and 284 in all. Listen is an editor action, not a parameter. The list of channels left out of the link (*retired 2026-11-20 by the author*; a chunk that has its field `link_out` keeps it as an unknown field) was state, not a parameter: it is stored by channel index together with the layout it was made for (amended 2026-11-18), and applies only while the current layout is that one: in any other layout every channel is in the link (*amended 2026-11-20 by the author*, replacing "reset when the layout changes": REAPER sets a 64-channel layout for an instant while loading, and Bitwig loads the state before it sets the layout, so a reset would erase a list just loaded; Phase 1 bench). The list is made again for a new layout when the user edits it there. In the state chunk it is the top-level field `link_out`, written only when a channel is left out. **The channels each band leaves out** (*decided 2026-11-20 by the author*; Band model, Channels) are state in the same form and under the same rule: by channel index, with the layout they were made for, applied only while the current layout is that one, and made again for a new layout when the user edits them there. In the state chunk they are the top-level field `band_out`, a list per band, written only when some band leaves a channel out.

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
- **Layout for speed.** State is structure-of-arrays. The eight detectors of a key channel read the same signal in parallel (one set per key channel, linked or not) and fill one AVX2 register (two NEON registers); they are the SIMD lanes. The audio chain is serial per channel and stays scalar per channel, vectorised across channels where there are several. *Built 2026-11-21 (Phase 7), so far:* the registers are `dsp/simd/Vec.h`, our own, with the same logical width on every target and a plain-array form that the reference and scalar builds run and the tests compare the others with; a kernel is written once over the register type. The first is the seeding bank, its thirty regions the lanes. A stretch's coefficients are six arrays, a term each: beside the two axes above there is a third, the 16 samples of a control step, along which coefficients and levels have no recurrence. *Built 2026-11-22:* the eight detectors of a key channel in one register as written above; the audio chain across channels, eight, four or two consecutive channels to the registers, `work` sample-major; a band's coefficients along the samples, four to a register; the tracker group's coefficients and extraction filters across its members. None changes a bit (`docs/bench/2026-11-22-phase7-kernels.md`). The budget's case is 2.96 % of a core on the bench machine (4.93 % at the start of Phase 7).
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
- **Arithmetic and the processor** (*built 2026-11-21, Phase 7*). `lib/dsp`, and every translation unit that includes its headers, is compiled without floating-point contraction: a multiply and an add are two roundings in the scalar code and in a SIMD kernel alike, which is what lets the two give the same bits; a canary test holds it. On x86 the build requires AVX2 (*decided 2026-11-21 by the author*); `LIKYU_SIMD=scalar` builds the same kernels with no intrinsic, for an older processor, and CI holds it to the bits of the SIMD build on the three platforms. *Found the same day:* Apple clang had been fusing multiply-adds by default, and without them the M4 Pro is 15 % slower on the budget's case and up to 60 % on 64 channels; whether the kernels use fused multiply-adds explicitly was put to the author (`docs/bench/2026-11-21-phase7-foundations.md`). *Decided 2026-11-21 by the author:* they do. `mulAdd` (`dsp/Math.h`) is a b + c in one rounding wherever the processor has the instruction (AVX2 builds, every arm64), and it is written where a recurrence or a polynomial has that shape; a plain `a * b + c` stays two roundings everywhere. `LIKYU_SIMD=reference` is the scalar kernels with the fused operation, the build the SIMD ones are compared with; `LIKYU_SIMD=scalar` on x86 has no FMA to use, keeps two roundings in `mulAdd` too, and so differs from the others in its last bits.

**Budget** (*proposed*, to be replaced by measurements in `docs/bench/`): a stereo instance with all eight bands in Dynamic + Track, stereo sidechain, at 96 kHz with 64-sample blocks, under 1 % of one core of the bench machine, with a worst block under 10 % of the block period. The bench runner prints median, p99, p999 and max per block, in the manner of WFS-DIY's `pipeline-bench`. *Replaced 2026-11-20 by the author:* no fixed figure. Detection on every band takes some computing, so the DSP is optimised as much as possible instead. The bench keeps measuring the same case, and `docs/bench/` keeps the figures. *Baseline of Phase 7, 2026-11-21* (`docs/bench/2026-11-21-phase7-baseline.md`), the bench's thread at audio priority on one performance core: 4.93 % of a core on the Windows machine (Core Ultra 7 255H), 3.23 % on an Apple M4 Pro, 6.11 % on a Ryzen 7 8745H; on 64 channels, linked, 15.8 %, 8.25 % and 20.8 %.

## Editor

One resizable window: the response curve on top, one strip for the selected band below. A plain `juce::Component` root, as in the OSCQuery plugin, so each shim can host it. *Amended 2026-11-20 by the author, on seeing the first curve:* under the curve, a column per band and a foot panel, the layout of WFS-DIY's output EQ (Band columns and foot, below); the instance's four parameters in a row above the curve.

- **Curve.** The static response as a line, and the live response (static plus current offsets and current frequencies) as a second line moving over it. One node per band. Dragging the node's centre moves frequency and gain together; the node carries a crosshair whose horizontal arm drags frequency only and whose vertical arm drags gain only, so one can be moved without disturbing the other. Mouse wheel over a node sets Q.
- **What a node shows of its modes.** Dynamic and Transient: a vertical bracket from the static gain to static gain + Range, with the current offset as a moving mark. Env and Track: a horizontal bracket from Low to High, the node sliding inside it, dimmed while confidence is below threshold.
- **Detector.** With an Own detector filter, a ghost node at the detector's frequency, draggable. A key badge per node: Self or Sidechain, and a warning when Sidechain is chosen but not connected. *Built 2026-11-20, as the author asked:* the ghost is a ring level with its node; the selected band's is tied to its node by a dotted line, is dragged for the detector's frequency and takes the wheel for the detector's Q; the other bands' are drawn faint and take no press. The badge reads IN or SC beside the node, and SC? in the warning colour when the sidechain chosen is not there.
- **Band strip.** Only the controls of the selected band's current gain mode and frequency mode are shown; the others keep their values. *Replaced 2026-11-20 by the author* by the next item; the rule stands for the foot panel.
- **Band columns and foot** (*decided 2026-11-20 by the author*). Eight columns side by side under the curve, one per band and always there, each in its band's colour: On, Type, Frequency, Gain and Q. A type without a gain shows no Gain; an off band's controls are faint and still usable. Under the columns, one foot panel shared by the bands shows the selected band's Q mode, gain mode, frequency mode and key, each with the controls of its current choice and no others. A press on a node or in a column selects the band. Dials, bars and toggles look and handle as WFS-DIY's; a value can be typed with its unit. **The foot is a fixed grid** (*decided 2026-11-20 by the author*): four blocks side by side, Band, Gain, Frequency and Key, each of a fixed width in columns of two rows, so that nothing moves when a mode changes and a block with nothing to show keeps its room. A mode's choice is alone in its block's first column, with nothing beneath it. Then pairs, one above the other. Dynamic: Acts over Threshold, Range over Ratio, Attack over Release, then Knee. Transient: Attack over Sustain, Range over Floor, then Time. In Transient the Range dial is two-sided (*decided 2026-11-21 by the author*): the shaper limits its offset to ±|Range|, so the dial is lit from its top both ways as far as the magnitude, with a dot at each end that drags it, and reads "±6.0 dB"; it writes the magnitude back with the sign the parameter had, which Dynamic uses. The parameter is unchanged. Env: Direction over Sensitivity, Low over High, Attack over Release, then Span. Track: Confidence over Scales gain, Low over High, Speed over Hold. Key: the key, then Detector over its type, then the detector's frequency over its Q. What two modes share is in the same place in both: Range, and Low and High. The foot also holds the selected band's **channels** (*2026-11-20*): a cell per channel of the instance with the host's name for it beneath, a press taking the channel out of the band or back in; not shown on a mono instance.
- **Listen** as a momentary button on the strip. *2026-11-20:* in the foot, on the selected band's title line, behind a long press (Detection, Listen); not offered on a band that is off or has no detector in use; it stops when another band is selected and when the editor closes.
- **The transient's times, temporary** (*decided 2026-11-20 by the author*). While the four time constants of the transient shaper are host parameters for the listening pass (Open decisions), a band in Transient shows a chevron in its Gain block: it slides a panel of the four over the Frequency block, and the panel closes when another band is selected. They are the instance's, not the band's. The panel goes with the parameters.
- **Reset and Flatten** (*decided 2026-11-20 by the author*, as in WFS-DIY's output EQ). Reset band, in the foot beside Listen, puts the selected band back to the table's defaults, every channel in it again. Flatten, at the right of the top row, does that to all eight; the instance's own four parameters stay. Each wants the same long press and acts on release, so neither happens by accident.
- **Analyser** (optional, off by default): spectrum of the input and of the sidechain, computed off the audio thread from a lock-free tap. It is a display aid and never feeds the DSP. *Built 2026-11-20:* an Analyser toggle in the top row, off each time the editor opens and not saved. While it is on, the audio thread writes the input and the sidechain, each the mean of its channels, to a lock-free ring (`core/AnalyserTap.h`); while it is off it writes nothing. The editor's timer takes the latest 8192 samples of each, Hann-windowed, through a radix-2 FFT, and reads them at the curve's frequencies, the largest bin within a twelfth of an octave. The levels are tilted by 3 dB per octave about 1 kHz, so that pink noise lies flat, and fall at 24 dB a second. Drawn behind the curve on a scale of its own, 0 dB at the plot's top and −90 dB at its bottom: the input filled, the sidechain a line, when there is one. Tilt, fall and scale are display choices, to be judged.
- Touch and the Go.dot control surfaces are served by the host parameters; the editor adds nothing a parameter cannot do, except Listen and the channels of each band (*2026-11-20*; was the list of channels left out of the link, retired the same day).
- **Link list** (*retired 2026-11-20 by the author*, with the list; the foot's channel cells replace it). With more than one channel, a row of channel toggles beside the Link switch shows which channels are in the link; all start in it, and a channel the host reports as LFE is marked as such, as a hint, not taken out (amended 2026-11-18).
- What a node and a strip say in each state is written as pure functions over the band's state, so headless tests assert it, as `SlotPresentation.h` does.

* **Multitouch**, on the systems where JUCE delivers it (Windows, Linux; macOS only where the host and OS pass touches through): several nodes dragged at once, one finger each; two fingers on one node pinch its Q, the touch equivalent of the wheel; the crosshair arms take touches as they take the mouse. Arms and nodes have touch-sized hit areas that do not overlap at the default window size. Whether a host forwards touches to a plugin editor at all is checked per host in Phase 1; the mouse path must always be complete without touch.

## Tests and acceptance

A feature is done when its row below passes in CI on the three platforms. Thresholds marked *proposed* are starting points to be confirmed by the first measurements and then written here with their date.

| Property | Test | Pass |
| --- | --- | --- |
| Exact bypass | Noise through 8 bell and shelf bands at 0 dB, every gain and frequency mode idle | Output equals input bit for bit |
| Exact bypass, return | Same, bands in Dynamic and Transient at 0 dB static: a loud passage, then a key under the threshold | Output equals input bit for bit within a stated time of the offset's target reaching 0 (*stated 2026-11-20*: release × ln(\|Range\| / 0.0001 dB), plus one 16-sample control step; 550 ms for −6 dB at a 50 ms release. Dynamic passes. Transient, *stated 2026-11-20*: (fast release × Time / 2) · ln(3 \|Range\| (P − floor)² / (W² · 0.0001 dB)), plus one control step, P the level the key falls from and W the gate's width, because the offset is at most \|Range\| times the gate and the gate under 3 u² for u = (E_fast − floor) / W; 169 ms for −6 dB from +6 dB over a −60 dB floor, measured 159 ms; passes) |
| Static response | Swept sine against the analytic magnitude of each type's bilinear-transformed (warped) response, 44.1 to 192 kHz | Within 0.1 dB up to 0.4 fs |
| Gain limit | Static gain and Range at their extremes, same sign | Effective gain never leaves −30 to +30 dB |
| Block-size invariance | The same input and automation rendered in blocks of 1, 7, 64 and 4096 samples, parameter changes aligned | Outputs equal, bit for bit (*confirmed 2026-11-20*, also at 128, 256, 480, 2000 and random sizes) |
| tan approximation | fc from 20 Hz to 20 kHz at every rate | Under 1 cent |
| Modulation safety | Gain, Q and frequency stepped and swept at audio rate on a sine and on silence; on/off, type and Q-mode switches and the bypass on a 500 Hz sine | No NaN, no growth, no click above −80 dB on silence; a switch puts nothing above −70 dB re the signal above 8 kHz (*proposed 2026-11-20*; measured −81 dB at worst; the gain-mode, key, fallback, detector, direction, link, exclusion and Listen switches of Phase 3, −97 to −99 dB; the Transient switches of Phase 4, and its amounts, floor and Time stepped while it shapes, −88 to −100 dB, and −85 dB from Static into Transient, where the detector starts cold and the note the band enters on reads as an onset under the fade in; the frequency-mode switches of Phase 5, Env's direction, a Track band's key and link, and Low, High, Sensitivity and Span stepped, −93 to −99 dB) |
| Constant latency | Impulse through every combination of band modes, per lookahead setting, bypass on and off | Measured delay = reported latency, unchanged by modes or bypass (*passes 2026-11-20* over every gain mode, type and key, in the DSP and through the processor; and with every frequency mode, Phase 5) |
| Lookahead effect | Burst through a Transient band with negative attack, lookahead Off and 1 ms | With lookahead the leading edge is reduced; without, it passes (*measured 2026-11-20*: with 1 ms, cut exactly as by a static bell at the full cut; without, the first 0.125 ms at −0.4 dB) |
| Dynamic curve | Stepped-level tone against the analytic gain computer | Within 0.25 dB at steady state; attack and release times within 10 % (*measured 2026-11-20*: 0.0011 dB; 5.10 ms for 5 ms, 120.10 ms for 120 ms) |
| Quadrature envelope | Steady tone at a 60 Hz band centre and half an octave either side, 1 ms time constants | Envelope ripple under 0.1 dB (*proposed*); level equals the band-pass magnitude within 0.1 dB (*measured 2026-11-20*, float detector, 44.1 to 192 kHz, Q 1 and 8: ripple 0.0002 dB, level within 0.002 dB; through a dynamic bell at a 1 ms attack and the shortest release, 5 ms, the offset ripples by 0.00003 dB) |
| Detector rejection | Band-pass detector at 3 kHz, Q 1; tone at 100 Hz | Level follows the band-pass response (about −30 dB), not the low-pass |
| Transient | Synthetic burst with known attack and decay | Offset has the sign and timing of the settings; nothing moves below the floor (*measured 2026-11-20*: peaks within 0.0002 dB and 0.5 % of the analytic one-poles; under the floor, exactly 0) |
| Transient, after silence | The same burst, first after 5 s of silence and then repeated | The first burst's offset matches the later ones (*proposed*: within 1 dB and 10 % in duration; *measured 2026-11-20*: within 0.002 dB and the same duration; the same envelopes on L instead of max(L, floor), 5.8 dB apart) |
| Env | Stepped-level tone between Sensitivity and Sensitivity + Span, Up and Down | Frequency follows the log mapping within 20 cents at steady state; attack and release within 10 % (*measured 2026-11-20*, 44.1 and 96 kHz: within 0.2 cents; attack 5.12 ms for 5, release 120.1 ms for 120) |
| Track, lock | Sine in noise at 0 dB SNR, inside the range | Locks within Speed × 5, error under 20 cents (*proposed*; *measured 2026-11-20*, 220 Hz, 1 kHz and 6 kHz at 44.1 and 96 kHz, Speed 50 ms: 52 to 136 ms, from a rest half an octave off or at either end of an octave-wide range; under 2 cents after) |
| Track, no swap | Two sines a fifth apart, at 220 Hz and at 6 kHz, at 44.1 and 96 kHz, two Track bands locked at equal levels and then crossfaded ±12 dB, at the default Speed | Neither band's frequency moves by more than 50 cents (*measured 2026-11-20*: under 0.1 cent, from the moment both are locked) |
| Track, exclusion | Same two sines, both bands released then re-seeded | The bands settle on different sines (*passes 2026-11-20*, from the same rest and again after both were released) |
| Track, loss | Sine removed | Hold, then glide to rest; no jump (*measured 2026-11-20*: held 208 ms for a Hold of 200, then back to rest monotonically, at most 18 cents per 16-sample step, and at rest exactly) |
| Sidechain fallback | Sidechain selected, bus absent | Band behaves as Self; status says so |
| Channel linking | Stereo signal with a transient on one side | Linked: identical gain on both channels |
| Linked maximum | A tone in one channel of 8, then the same tone in all 8; then the tone in two channels in opposite phase | The same offset in the three cases |
| Link exclusion (*retired 2026-11-20* with the list; see A band's channels) | 5.1 with every channel in, a burst on the LFE: every channel's gain moves; the LFE then left out by hand, a burst on the LFE only, then on the left only; a change of layout | With the LFE in, it drives the link like any channel; left out, its burst moves the LFE's gain only and the left burst every channel but the LFE; a new layout starts with every channel in; the list survives a state round trip |
| Layouts | Mono, stereo, 5.1 and 64 channels; sidechain absent, mono and stereo | Processes correctly; no channel reads another's state when unlinked |
| A band's channels | On 5.1, a low cut on five channels and a low-pass on the LFE alone; a dynamic band that leaves the LFE out, a burst on the LFE then on the left; the sets moved while audio runs; a change of layout | Each channel is exactly what it is through its own bands alone, bit for bit; the LFE's burst moves no channel and the LFE is untouched; block-size invariance holds bit for bit and a change puts no step in the signal larger than the signal's own; another layout has every band on every channel; the sets survive a state round trip (*passes 2026-11-20*) |
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
| 7. Performance and packaging | SIMD detector lanes and the rest of the optimisation, measured on the bench in the budget's case (no fixed figure since 2026-11-20), installers, signing and notarisation from WFS-DIY's scripts |  |
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
- [x] **LFE channel:** in the link like every other channel; any channel, the LFE included, left out by hand (decided 2026-11-16; amended 2026-11-18 by the author, after the Phase 1 bench: the host's LFE is not reliable).
- [x] **Link list, form:** *retired 2026-11-20 by the author*, the channels of each band doing its work. Was: a per-instance list in the state chunk, set in the editor, not host parameters (*decided 2026-11-20 by the author*); applied only to the layout it was made for, every channel in the link in any other (amended the same day).
- [x] **Listen guard, mechanism:** *decided 2026-11-20 by the author:* a long press on the Listen button, in every host; no live host is recognised and there is no Arm switch. Was: how the plugin knows it is in a live host and what the guard is. *Proposed:* guarded when the host identifies itself as Go.dot, or when a per-machine preference says so; the guard is an Arm switch on the strip that must be on before Listen acts, off each time the editor opens. Settled with the Go.dot integration at the latest.
- [x] **Quadrature envelope:** confirmed by the Phase 0 spike (2026-11-16).
- [ ] **Deflation form:** extraction from the raw key, confirmed on synthetic signals with two trackers; to hold on recordings and with eight. *Accepted provisionally 2026-11-20 by the author*: Phase 5 builds on it; it stays open until the spike has run on the recordings. *With eight, 2026-11-20 (Phase 5):* eight Track bands over 100 Hz to 10 kHz on eight equal sines take one each, once a tracker's weight follows its confidence (Frequency modes, lock weights).
- [x] **Confidence threshold:** as defined, confidence reads 0.51 for a sine in noise at 0 dB SNR, which was the default `fconf` of 50 % and the level the *Track, lock* row asks a lock at. Either the default comes down (*proposed:* 30 %), or confidence is measured against the key's power inside \[Low, High\] and not over the whole spectrum. *Decided 2026-11-20 by the author:* the default comes down to 30 %; confidence stays measured over the whole key.
- [ ] **Speed and close low pairs:** two sines 110 Hz apart wobble a tracker by 109 cents at Speed 20 ms, 46 at 50 ms, 23 at 100 ms. Whether the low end of `fspeed` (1 ms) stays, with that as documented behaviour, or the range is narrowed. *Measured in the product, 2026-11-20 (Phase 5):* the same pair, 220 and 330 Hz traded by ±12 dB, from the moment both are locked: 0.55 cents at Speed 20 ms, 0.09 at 50, 0.03 at 100, 2.1 at 200. The spike's figures were the maximum from its first sample, its filters empty, so they measured the start, not the trade.
- [ ] **Main bus width in REAPER** (raised by the Phase 1 host bench, 2026-11-17, `docs/spikes/phase1-hosts.md`): because the plugin accepts any main layout, REAPER gives a four-channel track's channels 3 and 4 to the main bus and puts the sidechain after them, so a hand-made send to 3/4 is processed as audio and keys nothing. Dragging the key track's routing onto the plugin's window does feed the sidechain. To decide once two more measurements are in: leave it and document REAPER's gesture, or restrict what the plugin accepts. Both are in since 2026-11-18: REAPER's default pin map is one to one, so a plugin of any width needs the user to re-wire its sidechain in the pin connector, and a control build with a fixed stereo main gets the send to 3/4 with no such step (finding 2 of the bench). Added 2026-11-18, same bench: the AU in REAPER on macOS is silent on a four-channel track; REAPER gives it four inputs and two outputs (its saved pin map), the plugin refuses unequal widths at initialisation, and REAPER then renders nothing and says nothing; on a two-channel track it runs (finding 9 of the bench).
- [x] **LFE reported on a plain four-channel track** (raised by the Phase 1 bench, 2026-11-17): **decided 2026-11-18 by the author**: no channel is left out of the link by default; the user leaves out the LFE, or any channel, by hand. REAPER names a layout from the channel count alone (four channels L R C Lfe, six 5.1), hexaphonic and octophonic files have no LFE, and what the channels hold cannot be known in most cases.
- [ ] **Tracker estimator:** the gradient notch, chosen by the Phase 0 spike on synthetic signals (2026-11-16); confirmed or not when the spike runs on the author's recordings. *Accepted provisionally 2026-11-20 by the author*: Phase 5 builds on it meanwhile.
- [ ] **Transient time constants and the Time range**, settled by the Phase 4 listening pass, before those IDs' ranges freeze. *2026-11-20:* built with the starting values; for the pass, the four are temporary host parameters (`tmp_tfa`, `tmp_tsa`, `tmp_tfr`, `tmp_tsr`) beside Time.
- [x] **Performance budget**: *decided 2026-11-20 by the author*: no fixed figure; optimised as much as possible, since detection on every band takes some computing. The figures below stay as the record. Was: confirm or replace the proposed 1 % figure once Phase 2 gives a first measurement. *First figure, 2026-11-20* (`docs/bench/2026-11-20-phase2.md`): eight static bands, stereo, 96 kHz, 64-sample blocks, 0.36 % of one core at the median (0.89 % with every band's ramps running), Intel Core Ultra 7 255H, the DSP alone. No detection yet: the budget's case is Phase 7's. *Second figure, 2026-11-20* (`docs/bench/2026-11-20-phase3.md`): eight bands in Dynamic on a stereo sidechain, linked, 2.0 % (1.0 % at 48 kHz); keyed by the input, Unlinked, 3.3 %; still before Track. *Third figure, 2026-11-20* (`docs/bench/2026-11-20-phase4.md`): eight bands in Transient on a stereo sidechain of bursts, linked, 2.1 %, about what Dynamic costs; the lookahead's line in the chain adds some 4 % to the static EQ (0.41 %) and nothing measurable to the dynamic paths. *Fourth figure, 2026-11-20* (`docs/bench/2026-11-20-phase5.md`): **the budget's case**, eight bands in Dynamic and Track on a stereo sidechain, linked, eight trackers locked and deflating each other, the seeding bank running: 5.0 % (2.5 % at 48 kHz); Dynamic and Env, 2.7 %. The static and dynamic paths measure 6 to 8 % above Phase 4 (0.435 %, 2.22 %), of the order of the machine's own variation.
- [x] **Fused multiply-adds in the kernels:** *decided 2026-11-21 by the author:* fused, explicitly. Every recurrence is written with an explicit fused multiply-add, the same on AVX2 and NEON; the compiler still never fuses by itself. The scalar build for a processor without FMA keeps the unfused operations and is its own arithmetic, held by its own suite; a second scalar build with the fused operation is the reference CI holds the SIMD builds to, bit for bit. Was (raised by Phase 7, 2026-11-21, `docs/bench/2026-11-21-phase7-foundations.md`): every Apple silicon Mac and every AVX2 processor has the instruction, correctly rounded, and every recurrence of the plugin is a chain of multiply-adds. Written with it explicitly, the kernels gain 13 % of the budget's case and 30 to 37 % of the wide layouts on an M4 Pro (what its build had until contraction was turned off), 10 % and 17 to 23 % on a Ryzen 7 8745H, about 4 % on the Core Ultra 7 255H with MSVC. Against: the output changes once more, and a processor without FMA cannot follow, so the scalar build for an old processor would be a third arithmetic, held by its own suite and not to the bits of the SIMD builds. To decide before the kernels are written: each is written once, in one arithmetic.
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
| 2026-11-18 | Amended: every channel starts in the link, the LFE included; the user leaves any channel out by hand. The host's LFE is shown as a hint only |
| 2026-11-16 | Sidechain routing in Go.dot is Go.dot work, done later, after the app is thoroughly tested |
| 2026-11-16 | No pull requests for now: commits go straight to `main` |
| 2026-11-16 | Listen acts at once in a DAW and is guarded in a live host |
| 2026-11-16 | Effective gain limited to ±30 dB; offsets return to exactly 0; double in the audio path, float in detection |
| 2026-11-16 | Band-pass detector envelope from BP² − LP·HP; deflation without delay, from the raw key |
| 2026-11-16 | LV2 URIs on pixetbel.org (mono, stereo), bundle ID io.github.pob31.likyu |
| 2026-11-16 | Editor: crosshair arms for frequency-only and gain-only drags, wheel or pinch for Q, multitouch where supported |
| 2026-11-20 | Until the editor of Phase 6, JUCE's generic editor stands in, so that Live can reach the parameters (Phase 1 bench, finding 6) |
| 2026-11-20 | The list of channels left out of the link is state, not host parameters; it applies only to the layout it was made for |
| 2026-11-20 | High-pass and low-pass detectors: a peak follower whose release is 4 periods of the detector frequency, as the PRD wrote it |
| 2026-11-20 | Dynamic attack is the offset moving away from 0, release its return to 0, in Above and Below alike |
| 2026-11-20 | Transient: the attack pair releases at the fast release and the sustain pair attacks at the fast attack, so three envelopes run; the floor gate reads the fast envelope, not L |
| 2026-11-20 | The four Transient time constants are temporary host parameters until the listening pass has set them |
| 2026-11-20 | No fixed performance budget: the proposed 1 % becomes "optimised as much as possible"; the bench keeps measuring |
| 2026-11-20 | The spike's estimator (gradient notch) and deflation (from the raw key) accepted provisionally for Phase 5, until the spike has run on recordings |
| 2026-11-20 | Confidence threshold: `fconf` defaults to 30 %; confidence stays measured over the whole key |
| 2026-11-20 | Env rests at the start of its mapping (Low for Up, High for Down); Frequency is the rest value in Track only |
| 2026-11-21 | AVX2 is required of an x86 processor: one build, chosen when it is built; a scalar build without intrinsics stays as an option |
| 2026-11-21 | Phase 7: an optimisation changes no output bit; a remedy that changes the numbers, even inside the tolerances, is brought to the author with its figures first |
| 2026-11-21 | Fused multiply-adds, written explicitly in the kernels; the compiler never fuses by itself; the scalar build for a processor without FMA is its own arithmetic |
