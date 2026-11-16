# Phase 0 spike: tracking

2026-11-16. Devplan Phase 0d. Code: `spikes/tracker/tracker_spike.py`, still in the tree because
part 4 (recordings) has not been run; it is deleted when it has.

**Status: synthetic signals only.** Every figure below comes from sines, glides and white noise.
The PRD asks that the estimator be validated on real material, and that part waits for the
author's recordings. The choices below are therefore provisional on that run, and are stated as
such in the PRD.

## The three questions, and the answers

| Question | Answer | Standing |
| --- | --- | --- |
| 1. Which estimator | **The gradient adaptive notch**, a frequency-locked loop on one SVF | Passes every row tried; provisional until the recordings |
| 2. Which deflation | **Extraction from the raw key**, as the PRD's corrected form has it. The delayed form rotated forward by one sample measures the same and costs one SVF less per Track band; it is the fallback if Phase 7 needs the saving | The plain delayed form is rejected, as computed |
| 3. The envelope `k * sqrt(max(0, v1² − v2·hp))` | **Confirmed.** It equals the band-pass magnitude to 0.0001 dB at every tone tried, with no ripple before any smoothing | Closed |

## 1. Estimator

Three candidates, each at Speed 50 ms and tracker Q 4 unless said.

- **Energy ratio on the raw key: rejected.** On a clean sine it is exact. In white noise at 0 dB
  SNR it settles 23 to 28 cents off at five of the six tone and rate pairs and never enters the
  20 cent band; at −6 dB it is 164 cents off. HP² and LP² each carry the whole spectrum on their
  side of the estimate, as the PRD's note expected.
- **Energy ratio behind a band-pass: works alone, fails beside a neighbour.** It locks in 114 to
  179 ms with the least jitter of the three (0.2 to 0.6 cents RMS at 0 dB SNR). But it is biased
  at low Q (12 cents at Q 1), it needs a second SVF, and with two sines a fifth apart at 220 and
  330 Hz it moves by 72 to 77 cents when their levels trade, with any deflation. That fails the
  *Track, no swap* row.
- **Gradient notch: chosen.** One SVF. It locks in 148 to 161 ms at every tone and rate (the row
  allows 250 ms), with 0.1 to 1.1 cents RMS and at most 3.8 cents of error at 0 dB SNR; it is
  unbiased at every Q from 1 to 16; and it holds the 220 and 330 Hz pair within 46 cents and the
  6 and 9 kHz pair within 3 cents through the level trade.

The loop, as it should be written in Phase 5: with `bp = k·v1`, `q = k·v2` and `e = x − bp`,

```text
log f  -=  mu * e * q / (bp² + q² + eps)        mu = k / (Speed * fs)
```

Measured time constant with that `mu`: 46 ms for a Speed of 50 ms. (The first run had `2k` and a
loop twice as fast as it claimed.)

## 2. Deflation

- **Without deflation two trackers always swap or collapse**: about 700 cents, a fifth, in every
  case of the level trade. Exclusion is not optional.
- **The plain one-sample delay does what the PRD review computed.** The other band's sine is left
  at −26.5 dB at 330 Hz, −14.1 dB at 1.5 kHz and +0.1 dB at 9 kHz (44.1 kHz), and the 6 and 9 kHz
  pair swaps.
- **From the raw key, and delayed-then-rotated, are indistinguishable here.** Both remove the
  other sine to the numerical floor (below −220 dB) at every frequency and rate, give the same
  figures through the level trade to within 2 cents, and neither misbehaves when both trackers
  are put on the same sine.
- Raw-key extraction is kept because it has no feedback path between trackers, which is easier
  to reason about with eight of them. This spike only ever ran two.

## What the spike found beyond its questions

These change Phase 5 or the PRD. The first two are for the author.

1. **The default Confidence threshold sits exactly on the lock row.** Confidence, defined as
   1 − notch power / input power over the whole key, measures 0.51 for a sine at 0 dB SNR, 0.91 at
   +10 dB and 0.01 on noise. The PRD's default `fconf` is 50 %, and its *Track, lock* row asks for
   a lock at 0 dB SNR. With both as written, that band hovers on the threshold. Either the default
   comes down, or confidence is measured against the key's power inside the band's \[Low, High\]
   range and not over the whole spectrum.
2. **Close pairs at low frequency limit how fast Speed can be.** Two sines 110 Hz apart beat at
   110 Hz and a fast loop follows the beat: 109 cents of wobble at Speed 20 ms, 46 at 50 ms, 23 at
   100 ms, 12 at 200 ms. The *Track, no swap* row passes at the default Speed with 4 cents to
   spare at 220 Hz, and fails below it. The row should name its Speed.
3. **Confidence must be smoothed over periods, not over Speed.** Smoothed over Speed (50 ms), it
   takes 461 ms to fall under 0.5 after a sine drops by 40 dB, because both powers decay together.
   Smoothed over four periods it takes 35 ms.
4. **Hold must freeze the adaptation, not only the output.** With nothing holding it, an estimate
   drifts 25 to 144 cents in the half second after its sine is removed.
5. **A loud component appearing beside a quiet tracked one captures the quiet one's tracker at low
   frequency.** From a cold start with a 24 dB disadvantage, the 220 and 330 Hz pair swaps under
   every deflation: the extraction filter needs a few milliseconds to respond and the loop moves
   in that time. At 1 kHz and above the same start holds. Adaptation needs gating on onsets, or on
   the other tracker's lock, in Phase 5.
6. **Crossing glides bounce.** With deflation, two glides that cross exchange trackers at the
   crossing, as the PRD expected; without it both trackers end on the same glide.

## Limits

- No real material yet (part 4).
- Two trackers at most; eight sharing a key were not tried.
- One noise seed per case; the figures are single runs, not statistics.
- Coefficients were recomputed every sample, which the product will not do: the 16-sample
  sub-block of the PRD may add its own error under fast tracking.
- Seeding, minimum spacing, Hold and the glide to rest are Phase 5 logic and were not built.

## Plots

- `phase0-tracker/lock.png`: the three estimators locking from half an octave below, in noise.
- `phase0-tracker/no-swap.png`: the level trade under the four deflation forms.
- `phase0-tracker/crossing.png`: the crossing glides.

---

# Measurements

The spike's own output, unedited.

## 1. Estimator

Speed 50 ms, tracker Q 4, range one octave either side of the tone, the tracker started
half an octave below it.

### 1a. Clean sine: does the loop have the time constant it claims?

| Estimator | Time to cover 63 % of the half octave | Residual after 1 s |
| --- | --- | --- |
| gradient notch | 46 ms | -0.00 cents |
| energy ratio, raw key | 44 ms | -0.00 cents |
| energy ratio, behind a band-pass | 48 ms | -0.00 cents |

### 1b. Sine in white noise at 0 dB SNR (PRD row *Track, lock*: within Speed x 5 = 250 ms, under 20 cents)

Lock time is the last moment the error exceeds 20 cents; *never* means it still does at the end of 3 s.
Error is over the last half second.

| fs | Tone | Estimator | Lock time | RMS error | Max error | Row |
| --- | --- | --- | --- | --- | --- | --- |
| 44100 | 220 Hz | gradient notch | 148 ms | 1.1 cents | 3.8 cents | pass |
| 44100 | 220 Hz | energy ratio, raw key | never | 27.5 cents | 30.0 cents | FAIL |
| 44100 | 220 Hz | energy ratio, behind a band-pass | 114 ms | 0.6 cents | 1.4 cents | pass |
| 44100 | 1000 Hz | gradient notch | 156 ms | 0.5 cents | 2.1 cents | pass |
| 44100 | 1000 Hz | energy ratio, raw key | never | 27.6 cents | 29.0 cents | FAIL |
| 44100 | 1000 Hz | energy ratio, behind a band-pass | 144 ms | 0.3 cents | 0.7 cents | pass |
| 44100 | 6000 Hz | gradient notch | 158 ms | 0.3 cents | 1.4 cents | pass |
| 44100 | 6000 Hz | energy ratio, raw key | 119 ms | 14.6 cents | 16.0 cents | pass |
| 44100 | 6000 Hz | energy ratio, behind a band-pass | 179 ms | 0.6 cents | 1.0 cents | pass |
| 96000 | 220 Hz | gradient notch | 150 ms | 0.8 cents | 2.6 cents | pass |
| 96000 | 220 Hz | energy ratio, raw key | never | 27.7 cents | 29.3 cents | FAIL |
| 96000 | 220 Hz | energy ratio, behind a band-pass | 114 ms | 0.4 cents | 1.2 cents | pass |
| 96000 | 1000 Hz | gradient notch | 161 ms | 0.3 cents | 1.2 cents | pass |
| 96000 | 1000 Hz | energy ratio, raw key | never | 27.1 cents | 28.2 cents | FAIL |
| 96000 | 1000 Hz | energy ratio, behind a band-pass | 136 ms | 0.2 cents | 0.4 cents | pass |
| 96000 | 6000 Hz | gradient notch | 161 ms | 0.1 cents | 0.6 cents | pass |
| 96000 | 6000 Hz | energy ratio, raw key | never | 23.3 cents | 25.7 cents | FAIL |
| 96000 | 6000 Hz | energy ratio, behind a band-pass | 155 ms | 0.5 cents | 0.7 cents | pass |

### 1c. The same at gentler noise, for scale (1 kHz, 48 kHz)

| SNR | Estimator | Lock time | RMS error | Max error |
| --- | --- | --- | --- | --- |
| +20 dB | gradient notch | 160 ms | 0.1 cents | 0.2 cents |
| +20 dB | energy ratio, raw key | 126 ms | 0.3 cents | 0.3 cents |
| +20 dB | energy ratio, behind a band-pass | 132 ms | 0.0 cents | 0.0 cents |
| +10 dB | gradient notch | 161 ms | 0.2 cents | 0.6 cents |
| +10 dB | energy ratio, raw key | 123 ms | 2.6 cents | 2.8 cents |
| +10 dB | energy ratio, behind a band-pass | 133 ms | 0.0 cents | 0.1 cents |
| +0 dB | gradient notch | 162 ms | 0.5 cents | 2.0 cents |
| +0 dB | energy ratio, raw key | never | 26.6 cents | 29.0 cents |
| +0 dB | energy ratio, behind a band-pass | 142 ms | 0.2 cents | 0.6 cents |
| -6 dB | gradient notch | 163 ms | 1.0 cents | 3.9 cents |
| -6 dB | energy ratio, raw key | never | 164.1 cents | 184.7 cents |
| -6 dB | energy ratio, behind a band-pass | 166 ms | 0.8 cents | 1.8 cents |

### 1d. Speed against jitter, gradient notch and band-passed ratio (1 kHz, 48 kHz, 0 dB SNR)

| Speed | Estimator | Lock time | RMS error | Max error |
| --- | --- | --- | --- | --- |
| 10 ms | gradient notch | 24 ms | 2.4 cents | 8.8 cents |
| 10 ms | energy ratio, behind a band-pass | 23 ms | 1.3 cents | 3.9 cents |
| 50 ms | gradient notch | 155 ms | 0.5 cents | 1.7 cents |
| 50 ms | energy ratio, behind a band-pass | 141 ms | 0.2 cents | 0.5 cents |
| 200 ms | gradient notch | 642 ms | 0.1 cents | 0.4 cents |
| 200 ms | energy ratio, behind a band-pass | 578 ms | 0.1 cents | 0.2 cents |

### 1e. Tracker Q, gradient notch and band-passed ratio (1 kHz, 48 kHz, 0 dB SNR, Speed 50 ms)

| Q | Estimator | Lock time | RMS error | Max error |
| --- | --- | --- | --- | --- |
| 1 | gradient notch | 162 ms | 1.0 cents | 4.4 cents |
| 1 | energy ratio, behind a band-pass | 113 ms | 12.1 cents | 14.5 cents |
| 2 | gradient notch | 159 ms | 0.7 cents | 2.7 cents |
| 2 | energy ratio, behind a band-pass | 126 ms | 1.4 cents | 2.1 cents |
| 4 | gradient notch | 155 ms | 0.5 cents | 1.7 cents |
| 4 | energy ratio, behind a band-pass | 141 ms | 0.2 cents | 0.5 cents |
| 8 | gradient notch | 153 ms | 0.3 cents | 1.1 cents |
| 8 | energy ratio, behind a band-pass | 189 ms | 0.1 cents | 0.3 cents |
| 16 | gradient notch | 153 ms | 0.2 cents | 0.7 cents |
| 16 | energy ratio, behind a band-pass | 277 ms | 0.1 cents | 0.3 cents |

### 1f. Sine removed (PRD row *Track, loss*): how fast confidence says so

A 1 kHz sine over noise 40 dB below it, removed at 1 s. Confidence is 1 - notch power / input power,
the two powers smoothed over the time given. Drift is where the estimate has wandered half a second
after the removal, with nothing holding it.

| Powers smoothed over | Estimator | Confidence while locked | Time to fall under 0.5 | Drift 0.5 s later |
| --- | --- | --- | --- | --- |
| Speed (50 ms) | gradient notch | 1.000 | 461 ms | -25 cents |
| Speed (50 ms) | energy ratio, behind a band-pass | 1.000 | 461 ms | +144 cents |
| 4 periods (4 ms) | gradient notch | 1.000 | 35 ms | -25 cents |
| 4 periods (4 ms) | energy ratio, behind a band-pass | 1.000 | 35 ms | +144 cents |

### 1g. Confidence on a sine in noise, and on noise alone (1 kHz, 48 kHz, Q 4)

What the Confidence threshold has to separate. Mean over the last second of three.

| Key | gradient notch | energy ratio, behind a band-pass |
| --- | --- | --- |
| sine, no noise | 1.000 | 1.000 |
| sine in noise at +10 dB SNR | 0.911 | 0.911 |
| sine in noise at 0 dB SNR | 0.508 | 0.508 |
| white noise alone | 0.011 | 0.030 |

## 2. Deflation between two Track bands

### 2a. How much of the other band's sine is left at a tracker's input

Two equal sines a fifth apart, two trackers held on them (Speed 50 ms, Q 4). The level of sine B in
tracker A's input, relative to its level in the key, over the last half second. 0 dB is no deflation;
above 0 dB the subtraction adds.

| fs | Pair | from the raw key | own output, one sample late | own output, rotated forward |
| --- | --- | --- | --- | --- |
| 44100 | 220 and 330 Hz | -238.1 dB | -26.5 dB | -228.3 dB |
| 44100 | 1000 and 1500 Hz | -235.0 dB | -14.1 dB | -231.3 dB |
| 44100 | 6000 and 9000 Hz | -234.5 dB | +0.1 dB | -234.1 dB |
| 96000 | 220 and 330 Hz | -236.6 dB | -33.1 dB | -226.2 dB |
| 96000 | 1000 and 1500 Hz | -236.2 dB | -20.3 dB | -230.3 dB |
| 96000 | 6000 and 9000 Hz | -232.8 dB | -6.5 dB | -232.4 dB |

### 2b. Levels traded by 24 dB (PRD row *Track, no swap*: neither band moves by more than 50 cents)

Both sines at equal level for 1 s, so that both trackers are locked; then over 1 s sine A rises to +12 dB
and sine B falls to -12 dB; then over 3 s they trade places; then 0.5 s held. Each tracker's range is one
octave either side of its own sine, so each can reach the other. The figure is the larger of the two
trackers' worst departures from their own sine. Q is the tracker's and the extraction filter's.

| fs | Pair | Estimator | Q | no deflation | from the raw key | own output, one sample late | own output, rotated forward |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 44100 | 220 and 330 Hz | gradient notch | 4 | 758 cents FAIL | 46 cents | 44 cents | 44 cents |
| 44100 | 220 and 330 Hz | gradient notch | 8 | 759 cents FAIL | 44 cents | 43 cents | 43 cents |
| 44100 | 220 and 330 Hz | energy ratio, behind a band-pass | 4 | 705 cents FAIL | 72 cents FAIL | 72 cents FAIL | 72 cents FAIL |
| 44100 | 220 and 330 Hz | energy ratio, behind a band-pass | 8 | 712 cents FAIL | 76 cents FAIL | 76 cents FAIL | 76 cents FAIL |
| 44100 | 6000 and 9000 Hz | gradient notch | 4 | 702 cents FAIL | 3 cents | 747 cents FAIL | 3 cents |
| 44100 | 6000 and 9000 Hz | gradient notch | 8 | 705 cents FAIL | 3 cents | 804 cents FAIL | 3 cents |
| 44100 | 6000 and 9000 Hz | energy ratio, behind a band-pass | 4 | 702 cents FAIL | 1 cents | 702 cents FAIL | 1 cents |
| 44100 | 6000 and 9000 Hz | energy ratio, behind a band-pass | 8 | 702 cents FAIL | 1 cents | 702 cents FAIL | 1 cents |
| 96000 | 220 and 330 Hz | gradient notch | 4 | 756 cents FAIL | 46 cents | 44 cents | 44 cents |
| 96000 | 220 and 330 Hz | gradient notch | 8 | 760 cents FAIL | 44 cents | 43 cents | 43 cents |
| 96000 | 220 and 330 Hz | energy ratio, behind a band-pass | 4 | 705 cents FAIL | 73 cents FAIL | 73 cents FAIL | 73 cents FAIL |
| 96000 | 220 and 330 Hz | energy ratio, behind a band-pass | 8 | 712 cents FAIL | 77 cents FAIL | 77 cents FAIL | 77 cents FAIL |
| 96000 | 6000 and 9000 Hz | gradient notch | 4 | 721 cents FAIL | 2 cents | 702 cents FAIL | 2 cents |
| 96000 | 6000 and 9000 Hz | gradient notch | 8 | 703 cents FAIL | 2 cents | 706 cents FAIL | 2 cents |
| 96000 | 6000 and 9000 Hz | energy ratio, behind a band-pass | 4 | 702 cents FAIL | 2 cents | 702 cents FAIL | 2 cents |
| 96000 | 6000 and 9000 Hz | energy ratio, behind a band-pass | 8 | 702 cents FAIL | 2 cents | 27 cents | 2 cents |

### 2b'. The same from a cold start at the extreme (information)

Filters empty, sine A already 24 dB above sine B, trackers placed on their sines; 0.5 s, then the trade.
This is what a loud component appearing beside a quiet tracked one looks like to the quiet one's tracker.

| fs | Pair | Estimator | Q | no deflation | from the raw key | own output, one sample late | own output, rotated forward |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 44100 | 220 and 330 Hz | gradient notch | 4 | 758 cents FAIL | 726 cents FAIL | 807 cents FAIL | 757 cents FAIL |
| 44100 | 220 and 330 Hz | gradient notch | 8 | 759 cents FAIL | 769 cents FAIL | 776 cents FAIL | 766 cents FAIL |
| 44100 | 220 and 330 Hz | energy ratio, behind a band-pass | 4 | 706 cents FAIL | 77 cents FAIL | 80 cents FAIL | 89 cents FAIL |
| 44100 | 220 and 330 Hz | energy ratio, behind a band-pass | 8 | 712 cents FAIL | 80 cents FAIL | 80 cents FAIL | 80 cents FAIL |
| 44100 | 1000 and 1500 Hz | gradient notch | 4 | 1002 cents FAIL | 30 cents | 789 cents FAIL | 29 cents |
| 44100 | 1000 and 1500 Hz | gradient notch | 8 | 772 cents FAIL | 51 cents FAIL | 753 cents FAIL | 51 cents FAIL |
| 44100 | 1000 and 1500 Hz | energy ratio, behind a band-pass | 4 | 702 cents FAIL | 20 cents | 77 cents FAIL | 31 cents |
| 44100 | 1000 and 1500 Hz | energy ratio, behind a band-pass | 8 | 702 cents FAIL | 17 cents | 17 cents | 17 cents |
| 44100 | 6000 and 9000 Hz | gradient notch | 4 | 702 cents FAIL | 7 cents | 747 cents FAIL | 6 cents |
| 44100 | 6000 and 9000 Hz | gradient notch | 8 | 710 cents FAIL | 11 cents | 804 cents FAIL | 11 cents |
| 44100 | 6000 and 9000 Hz | energy ratio, behind a band-pass | 4 | 702 cents FAIL | 6 cents | 702 cents FAIL | 10 cents |
| 44100 | 6000 and 9000 Hz | energy ratio, behind a band-pass | 8 | 702 cents FAIL | 2 cents | 702 cents FAIL | 3 cents |

### 2b''. Speed against the wobble of a close low pair (220 and 330 Hz, 44.1 kHz, gradient notch, Q 4)

The settled protocol of 2b. The two sines beat at 110 Hz, which a fast loop follows.

| Speed | from the raw key | own output, rotated forward |
| --- | --- | --- |
| 20 ms | 109 cents FAIL | 105 cents FAIL |
| 50 ms | 46 cents | 44 cents |
| 100 ms | 23 cents | 23 cents |
| 200 ms | 12 cents | 11 cents |

### 2d. Two trackers put on the same sine (what exclusion must survive at a crossing or a bad seed)

One 1 kHz sine of amplitude 1 with noise 40 dB below, 48 kHz, both trackers started on it, 2 s.
The largest sample either tracker was given, and where the two ended.

| Deflation | Largest tracker input | Tracker 1 ends at | Tracker 2 ends at |
| --- | --- | --- | --- |
| no deflation | 1.03 | 1000 Hz | 1000 Hz |
| from the raw key | 0.84 | 992 Hz | 992 Hz |
| own output, one sample late | 0.87 | 1000 Hz | 1000 Hz |
| own output, rotated forward | 0.88 | 1000 Hz | 1000 Hz |

### 2c. Two glides that cross (information: the PRD accepts an exchange at the crossing)

300 to 600 Hz and 600 to 300 Hz over 3 s, equal levels, 48 kHz, gradient notch, range 200 to 900 Hz.
Whether each tracker ends on the glide it started on, and its error there.

| Deflation | Tracker 1 ends on | Tracker 2 ends on |
| --- | --- | --- |
| no deflation | the falling glide (+17 cents) | the falling glide (+17 cents) |
| from the raw key | the falling glide (+20 cents) | the rising glide (-20 cents) |
| own output, one sample late | the rising glide (-20 cents) | the falling glide (+20 cents) |
| own output, rotated forward | the falling glide (+20 cents) | the rising glide (-20 cents) |

## 3. Band-pass detector envelope

Steady tone into one detector SVF. *Truth* is the band-pass magnitude at the tone. Level is the mean
of the envelope in dB over the second half of one second, ripple its peak-to-peak, both raw (no smoothing).

| fs | Detector | Tone | Truth | New form: level, ripple | First draft: level, ripple |
| --- | --- | --- | --- | --- | --- |
| 44100 | 60 Hz, Q 1 | 60.0 Hz | +0.00 dB | -0.00 dB, 0.000 dB | -0.00 dB, 0.00 dB |
| 44100 | 60 Hz, Q 1 | 42.4 Hz | -1.76 dB | -1.76 dB, 0.000 dB | -0.12 dB, 3.01 dB |
| 44100 | 60 Hz, Q 1 | 84.9 Hz | -1.76 dB | -1.76 dB, 0.000 dB | -3.14 dB, 3.01 dB |
| 44100 | 60 Hz, Q 8 | 60.0 Hz | +0.00 dB | -0.00 dB, 0.000 dB | -0.00 dB, 0.00 dB |
| 44100 | 3000 Hz, Q 1 | 100.0 Hz | -29.67 dB | -29.67 dB, 0.000 dB | -5.74 dB, 29.65 dB |
| 44100 | 3000 Hz, Q 1 | 10000.0 Hz | -11.73 dB | -11.73 dB, 0.000 dB | -15.81 dB, 12.00 dB |
| 44100 | 3000 Hz, Q 0.3 | 3000.0 Hz | +0.00 dB | -0.00 dB, 0.000 dB | -0.00 dB, 0.00 dB |
| 96000 | 60 Hz, Q 1 | 60.0 Hz | +0.00 dB | -0.00 dB, 0.000 dB | -0.00 dB, 0.00 dB |
| 96000 | 60 Hz, Q 1 | 42.4 Hz | -1.76 dB | -1.76 dB, 0.000 dB | -0.12 dB, 3.01 dB |
| 96000 | 60 Hz, Q 1 | 84.9 Hz | -1.76 dB | -1.76 dB, 0.000 dB | -3.14 dB, 3.01 dB |
| 96000 | 60 Hz, Q 8 | 60.0 Hz | +0.00 dB | -0.00 dB, 0.000 dB | -0.00 dB, 0.00 dB |
| 96000 | 3000 Hz, Q 1 | 100.0 Hz | -29.57 dB | -29.57 dB, 0.000 dB | -5.73 dB, 29.57 dB |
| 96000 | 3000 Hz, Q 1 | 10000.0 Hz | -10.40 dB | -10.40 dB, 0.000 dB | -14.21 dB, 10.64 dB |
| 96000 | 3000 Hz, Q 0.3 | 3000.0 Hz | +0.00 dB | +0.00 dB, 0.000 dB | +0.00 dB, 0.00 dB |

Worst departure of the new form from the truth, level or ripple: **0.0001 dB**.

## 4. Recordings

None yet. Run again with `--wav file.wav ... --range LOW HIGH` when the author's recordings arrive.
