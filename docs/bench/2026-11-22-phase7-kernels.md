# Bench note, Phase 7: the kernels and the frame

2026-11-21 and 22, from `11d5e08` (the seeding bank, the first kernel;
[`2026-11-21-phase7-arithmetic.md`](2026-11-21-phase7-arithmetic.md)) to `8946036`. Stage 7d's
other four kernels and 7c's quiet band, every one of them changing no bit. Against the baseline
([`2026-11-21-phase7-baseline.md`](2026-11-21-phase7-baseline.md)).

96 kHz, blocks of 64, stereo and eight bands unless the case says so; the trimmed mean; the share
of one core; the bench's thread at audio priority. The wide layouts on Windows are from a cold run
(the laptop reads them 30 to 50 % high once warm); the farm's are the reference for them.

## Where it stands

| | Baseline | Now | |
| --- | --- | --- | --- |
| **The budget case**, Windows, Core Ultra 7 255H, MSVC | 32.9 µs (4.93 %) | **19.8 µs (2.96 %)** | −40 % |
| Mac mini, M4 Pro, Apple clang | 21.5 µs (3.23 %) | **15.2 µs (2.28 %)** | −29 % |
| NUC, Ryzen 7 8745H, GCC 13 | 40.7 µs (6.11 %) | **19.0 µs (2.85 %)** | −53 % |
| **The devplan's second case**, 64 channels linked, Windows | 105.0 µs (15.8 %) | **40.5 µs (6.08 %)** | −61 % |
| Mac | 55.0 µs (8.25 %) | **33.6 µs (5.04 %)** | −39 % |
| NUC | 138.5 µs (20.8 %) | **38.4 µs (5.76 %)** | −72 % |

| Case | Windows: baseline | now | Mac: baseline | now | NUC: baseline | now |
| --- | --- | --- | --- | --- | --- | --- |
| `static` | 2.9 µs | 1.7 | 2.4 µs | 1.4 | 4.0 µs | 1.9 |
| `moving` | 8.4 | 4.3 | 6.1 | 3.6 | 8.6 | 4.4 |
| `dynamic` | 14.2 | 7.5 | 9.6 | 6.6 | 20.5 | 7.4 |
| `unlinked` | 23.5 | 13.3 | 14.9 | 11.1 | 29.6 | 13.7 |
| `transient` | 15.1 | 7.9 | 8.7 | 5.8 | 22.8 | 9.6 |
| `env` | 17.1 | 9.7 | 10.4 | 8.0 | 25.2 | 11.7 |
| **`track`** | **32.9** | **19.8** | **21.5** | **15.2** | **40.7** | **19.0** |
| `trackself` | 31.6 | 16.8 | 20.6 | 13.0 | 39.4 | 16.9 |
| `seeding` | 4.8 | 0.9 | 2.9 | 0.7 | 11.1 | 0.7 |
| `static.64` | 71.6 | 19.3 | 35.1 | 18.0 | 97.3 | 16.9 |
| `dynamic.64` | 86.1 | 26.5 | 42.6 | 25.0 | 118.0 | 26.9 |
| `dynself.64` | 166.5 | 58.4 | 88.6 | 49.2 | 342.2 | 58.4 |
| `unlinked.64` | 695.7 | 412 | 419.9 | 326.9 | 856.8 | 402.7 |
| **`track.64`** | **105.0** | **40.5** | **55.0** | **33.6** | **138.5** | **38.4** |
| `trackself.64` | 184.7 | 69.8 | 100.8 | 57.3 | 266.9 | 77.6 |

Every case is faster on every machine. Unlinked on 64 channels, which ran only on the M4 at the
baseline, takes 49 to 62 % of a core.

## What was built

| Commit | What |
| --- | --- |
| `0bddb9b`, `511797d` | **The tracker group's front half**, its members the lanes: each member's 2^x, tan, divides and extraction filter, eight to the registers; `tick` and the ordered sum stay a member at a time. From three members; under that, the loop a member at a time, which measured 6 to 13 % faster for one or two |
| `662f9ed` | **A band's coefficients along the samples**, four to a register of doubles: one kernel for the four places that made them a sample at a time. An offset of exactly 0, a scale of exactly 1 and a fade at exactly 1 are choices of lanes, of the argument of 2^x and of the mix |
| `12cdf6a` | **A key channel's detectors, the eight bands its lanes**: the PRD's own layout for speed. A pass before the bands runs every key channel some band reads; each band takes its powers from there |
| `13f3bff` | **The filters across channels**: `work` sample-major, runs of 8, 4 or 2 consecutive channels in registers (F64x2 is new, for stereo), a lone channel as before; the constant set broadcast once |
| `8946036` | **The quiet band**: a band whose last step moved nothing and in which nothing moves or waits does not make its step. With the filters fast, that bookkeeping was a third of the static EQ's time |

Each kernel is a template over the register type and is held to the code it replaced in
`KernelTests` (the tracker group: every field of 1 to 8 trackers at two rates; the coefficients:
120,960 stretches; the detectors: 56,633 stretches), and the filters across channels by the
suite's layout rows, which compare each channel of 6 and 64 with the same channel alone. The
registers' operations are held to the plain arrays and the scalar expressions in `SimdTests`, the
awkward values included. On each of the three machines the SIMD build and the reference give the
same ledgers at every commit, and they are the ledgers of `0c86fad`.

## Step by step

| Windows, the budget case | µs | Its biggest gains elsewhere |
| --- | --- | --- |
| `11d5e08`, the bank | 26.7 | |
| the tracker group | 25.0 | `trackself` 23.3 → 21.8 |
| the coefficients | 22.6 | `moving` 6.5 → 5.2, `unlinked` 20.7 → 15.3, `transient` 13.0 → 10.6 |
| the detectors | 21.6 | `dynself.64` 165 → 112, `trackself.64` 177 → 120 |
| the filters across channels | 20.9 | `static.64` 65 → 19.3, `track.64` 91 → 40.5, `static` 2.9 → 2.2 |
| the quiet band | 19.8 | `static` 2.2 → 1.7; no band on 0.9 → 0.5 |

On the farm (the budget case, M4 and NUC): the tracker group 2 % and 5 %; the coefficients 5 % and
8 %; the detectors −1 % and 5 %; the filters 6 % and 6 %; the quiet band 4 % and 6 %.

## Where the time goes now

Sampled on the M4 Pro at `13f3bff` (`sample`, six seconds):

| The budget case | Share | | The static EQ | Share |
| --- | --- | --- | --- | --- |
| The tracker group, its front half | 26 % | | The filters | 40 % |
| `tick`, a member at a time | 19 % | | `Chain::process`: conversions, lookahead | 9 % |
| The coefficients along the samples | 22 % | | The bands' steps (before the quiet band) | 35 % |
| Detection and the gain computers | 18 % | | Detection's bookkeeping, nothing to detect | 4 % |
| The bank, the mean key, the seeding | 9 % | | | |
| The filters | 6 % | | | |

**The trackers are 45 % of the budget case** and the part that registers help least: a tracker's
estimate feeds its next sample's coefficients through 2^x, tan, a divide, the filter and the
gradient, and the samples come one after another. What shortens that chain changes the numbers:
the first-order update of g, which the plan keeps for the author (7f).

## Found on the way

- **The M4 does not gain from the detectors in registers, in stereo** (2 to 4 % slower): its
  out-of-order window already overlapped the sixteen scalar chains. It gains 20 to 26 % on the wide
  layouts keyed by the input. For the Mac's own pass (7e): the width per kernel on NEON.
- **Small tracker groups lose in registers**; the threshold of three members is measured, on the
  three machines.
- **Filling a stretch's coefficients for a constant band** cost as much as it saved, eight bands
  times sixteen samples: the constant set is broadcast from its first entry.
- **A test that reads a variable in the same expression that modifies it** (the tick's states)
  tests the compiler's order, not the code: the two-double test failed that way, once.
- **The ledger held through all of it**: nothing has moved since `0c86fad`.
