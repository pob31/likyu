# Bench note, Phase 7: the arithmetic, the coefficients, the first kernel

2026-11-21, later the same day, from `fbef822` to `11d5e08`. The author decided for fused
multiply-adds, written explicitly (the question of
[`2026-11-21-phase7-foundations.md`](2026-11-21-phase7-foundations.md)); this is what was built on
it, and what it measures against the baseline
([`2026-11-21-phase7-baseline.md`](2026-11-21-phase7-baseline.md)).

96 kHz, blocks of 64, stereo and eight bands unless the case says so; the trimmed mean; the share
of one core; the bench's thread at audio priority.

## Where the budget case stands

| | Baseline | Now | |
| --- | --- | --- | --- |
| Windows, Core Ultra 7 255H, MSVC | 32.9 µs (4.93 %) | **26.7 µs (4.00 %)** | −19 % |
| Mac mini, M4 Pro, Apple clang | 21.5 µs (3.23 %) | **17.9 µs (2.69 %)** | −17 % |
| NUC, Ryzen 7 8745H, GCC 13 | 40.7 µs (6.11 %) | **25.8 µs (3.86 %)** | −37 % |

## What was built

| Commit | What | Bits |
| --- | --- | --- |
| `3208519` | `mulAdd` (a b + c in one rounding where the processor has it), and `LIKYU_SIMD=reference`: the scalar kernels in the same fused arithmetic, which CI now holds the SIMD builds to | Unchanged |
| `a453b38` | The recurrences and the polynomials asked of `mulAdd`: the filter's tick and its copies in the detector and the tracker, every one-pole, both forms of tan, the fast log, the coefficients | **Declared: every build, every machine** |
| `0c86fad` | `pow2`, our own 2^x, in place of the library's `exp2` everywhere in `lib/dsp` | **Declared: every build, every machine** |
| `559faab` | A stretch's coefficients as six arrays, and the small functions they are made of inline whatever the compiler judges | Unchanged |
| `11d5e08` | `dsp/simd/Vec.h` and the first kernel: the seeding bank, eight regions to a register | Unchanged |

The phase's three declared changes of bits are now behind it: contraction off (the Mac's only),
the fused arithmetic, `pow2`. On each machine the SIMD build and the reference give the same
ledgers at every commit; the scalar build for an x86 without FMA is its own arithmetic since
`a453b38`, as decided, and passes its own suite. 174 cases on the three machines.

## The three machines

| Case | Windows: baseline | now | Mac: baseline | now | NUC: baseline | now |
| --- | --- | --- | --- | --- | --- | --- |
| `static` | 2.9 µs | 2.9 | 2.4 µs | 2.4 | 4.0 µs | 3.0 |
| `moving` | 8.4 | 6.5 | 6.1 | 4.0 | 8.6 | 6.7 |
| `dynamic` | 14.2 | 12.5 | 9.6 | 8.5 | 20.5 | 13.1 |
| `unlinked` | 23.5 | 20.7 | 14.9 | 13.4 | 29.6 | 21.1 |
| `transient` | 15.1 | 13.0 | 8.7 | 7.8 | 22.8 | 15.1 |
| `env` | 17.1 | 15.2 | 10.4 | 10.0 | 25.2 | 17.4 |
| **`track`** | **32.9** | **26.7** | **21.5** | **17.9** | **40.7** | **25.8** |
| `trackself` | 31.6 | 23.3 | 20.6 | 15.8 | 39.4 | 23.8 |
| `seeding` | 4.8 | 0.9 | 2.9 | 0.7 | 11.1 | 0.7 |
| `static.64` | 71.6 | 65.2 | 35.1 | 33.9 | 97.3 | 69.7 |
| `dynamic.64` | 86.1 | 80.7 | 42.6 | 41.4 | 118.0 | 83.9 |
| `dynself.64` | 166.5 | 165.3 | 88.6 | 87.2 | 342.2 | 166.8 |
| `unlinked.64` | 695.7 | 620.8 | 419.9 | 394.0 | 856.8 | 607.5 |
| **`track.64`** | **105.0** | **92.8** | **55.0** | **51.0** | **138.5** | **96.5** |
| `trackself.64` | 184.7 | 177.3 | 100.8 | 95.7 | 266.9 | 178.2 |

- **Unlinked on 64 channels is under one core on the three machines** (93 %, 59 %, 91 %), with no
  kernel written for it yet.
- **The wide layouts have barely moved on Windows and the Mac**: they are the filters and the
  detectors across channels, which are 7d's last two kernels.

## Step by step

**The fused arithmetic** (`a453b38`). Written by hand at each product and sum that has the shape,
in the form a kernel will repeat: a state's path from one sample to the next is two fused
operations. What must be exact still is: A² − 1 and 1 − A² are 0 at A = 1, the fade is the
identity at 0 and the band's mix at 1, a one-pole at its target stays there. The suite passed
unchanged the first time.

- The M4 is back to its baseline, which was fused by its compiler, and a little under: the budget
  case 21.3 µs against 21.5, `trackself` 18.1 against 20.6.
- The NUC: the budget case 36.7 to 32.2 µs, the static EQ 3.7 to 3.0, 64 static channels 94.9 to
  69.8, `dynself.64` 227.7 to 169.1.
- Windows, MSVC: little, as measured beforehand: the static EQ 4 %, 64 static channels 9 %, the
  budget case 1.5 %.

**`pow2`** (`0c86fad`): 64 x rounded to a whole number by an addition, a table of 64 constants,
five terms of e^z, the result T + T q in one rounding, times a power of two written into the bits.
Exactly 1 at 0 and exactly 2^n at every whole n; within 1.27 units in the last place of the true
value, measured on the NUC against `long double` over a million points. The NUC gains 7 % on the budget case. On Windows
it changed no figure, and that is how the next item was found.

**The coefficients, an array a term** (`559faab`). `/arch:AVX2` had slowed MSVC's per-sample
coefficients by half (`moving` 12.3 µs against 8.4 at the baseline), and neither the fused
arithmetic nor `pow2` moved it. MSVC's listing showed why: `pow2` and the band's row of the table
were calls, each returning its 48 bytes of coefficients through memory, written a field at a time
and copied with two 32-byte loads, which wait on every one of those stores, twice a sample. With
six arrays, each term stored as it is made and read by the filter one by one, and the small
functions inline whatever the compiler judges:

| Windows | Before | After |
| --- | --- | --- |
| `moving` | 12.3 µs | 6.5 |
| `dynamic` | 14.2 | 12.5 |
| `env` | 20.3 | 15.2 |
| `track` | 33.9 | 30.8 |

The Mac gained as well (`moving` 5.2 to 4.0 µs, `dynamic` 9.7 to 8.4): the copies cost there too.

**The seeding bank, the first kernel** (`11d5e08`). Thirty filters that know nothing of each
other, on one signal: its regions are the lanes, eight to a register, the sample loop inside. The
same template runs on an AVX2 register, on two NEON registers and on a plain array; a lane does
what the detector's band-pass tick does, operation for operation, and the bank's line of the
ledger did not move.

| The bank alone | Before | After |
| --- | --- | --- |
| Windows | 4.6 µs | 0.87 |
| Mac | 2.9 | 0.7 |
| NUC | 4.9 | 0.7 |

It was a fifth of the budget case on the M4, by the sampled profile; it is now a twentieth.

## Where the time goes now

The budget case sampled again on the M4 Pro, at `11d5e08` (`sample`, eight seconds, 6170 samples
at the top of the stack), beside the profile of the foundations note:

| Part | Before the day's second half | Now |
| --- | --- | --- |
| The tracker group: coefficients, extraction, `tick` | 31 % | **42 %** |
| The bands' coefficients and their filters, per lane | 17 % | 26 % |
| Detectors, levels and gain computers | 15 % | 17 % |
| `Chain::track` itself: the seeding bank, the mean key | 19 % | 5 % |
| The library's `exp2` | 11 % | none: `pow2` is inline in the two rows above |
| Seeding decisions, `updateFrequencies`, `beginStep`, the rest | 8 % | 10 % |

**The trackers are now the first cost by far**, and the order of what is left follows: the tracker
group's front half across its members; then the coefficients and levels along the samples; then
the detectors. What shortens the tracker's own chain, the first-order update of g, is the
author's to decide at the stop.

## Found on the way

- **NEON ran right the first time it ran**, on the farm, before the push: `SimdTests` (7296
  vectors of awkward values, each operation on the registers and on the arrays) and `KernelTests`
  (the bank on registers, on arrays and a region at a time, at five rates, compared after every
  stretch). It cannot be compiled on the Windows machine; the farm and the macOS job are where it
  is held.
- **`std::max` is not a processor's max.** `(a < b) ? b : a` keeps the first operand when either
  is not a number; `maxps` keeps the second, and NEON's returns a number. The wrapper says
  `std::max` to the letter on each, and the tests include the not-a-numbers.
- **Which of two not-a-numbers an addition returns depends on the order a compiler gives the
  operands**, in scalar code as in a register: the register tests accept any not-a-number for
  another. Nothing in `lib/dsp` rests on it, and the ledgers' signals have none.
- **A sanity check written after a burst 300 times full scale failed at 22.05 kHz**: the powers
  remember such a burst for many times their smoothing. The test's fault, not the bank's.
