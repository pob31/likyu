# Bench note, Phase 7: the foundations, and a question

2026-11-21, from `1545073` (the baseline, [`2026-11-21-phase7-baseline.md`](2026-11-21-phase7-baseline.md))
to `fbef822`. What Stage 1 of the plan and the first step of Stage 2 measured, and the one decision
they bring back to the author before the kernels are written: **fused multiply-adds**.

Same cases as the baseline: 96 kHz, blocks of 64, stereo and eight bands unless the case says so,
the trimmed mean, the share of one core.

## What was built

| Commit | What | Bits |
| --- | --- | --- |
| `4425c0b` | No fused multiply-add anywhere: `-ffp-contract=off`, `/fp:precise`, PUBLIC on `likyu_dsp`, with a canary test | **The Mac's move, once** (declared). Windows and Linux: unchanged |
| `3ed3c36` | A partly touched step is written back bit by bit (a miscompile of MSVC's, below) | Unchanged |
| `7549a9e` | `LIKYU_SIMD`: AVX2 on x86 (decided 2026-11-21), NEON on arm64, or `scalar`; the scalar build and `scripts/same-bits.sh` in CI | Unchanged |
| `fbef822` | The trackers' pair loops only when they have something to do | Unchanged |

On each of the three machines the suite's ledger (66 lines) and the bench's (19) are the same in
the SIMD build and in the scalar build, and on Windows and the NUC they are the baseline's. No
kernel is written in intrinsics yet: so far the three builds differ by their compiler options only.

## What turning contraction off cost the Mac

Apple clang fused multiply-adds by default; MSVC and GCC at their defaults on x86-64 did not. With
the option, the M4 Pro:

| Case | Fused, by the compiler (baseline) | Not fused (`4425c0b`) | |
| --- | --- | --- | --- |
| `static` | 2.4 µs (0.358 %) | 2.5 µs (0.374 %) | +5 % |
| `dynamic` | 9.6 µs (1.44 %) | 10.9 µs (1.63 %) | +14 % |
| `transient` | 8.7 µs (1.30 %) | 9.9 µs (1.49 %) | +14 % |
| **`track`**, the budget case | **21.5 µs (3.23 %)** | **24.8 µs (3.71 %)** | **+15 %** |
| `seeding` | 2.9 µs (0.431 %) | 4.1 µs (0.618 %) | +43 % |
| `static.64` | 35.1 µs (5.26 %) | 56.0 µs (8.40 %) | +60 % |
| `dynself.64` | 88.6 µs (13.3 %) | 129.7 µs (19.5 %) | +46 % |
| `track.64` | 55.0 µs (8.25 %) | 78.6 µs (11.8 %) | +43 % |
| `unlinked.64` | 419.9 µs (63.0 %) | 459.5 µs (68.9 %) | +9 % |

Every recurrence of the plugin (the filters, the detectors, the bank) is a chain of multiply-adds,
and its speed is the chain's latency. This is more than the plan expected, and it is not only the
Mac's.

## What fused arithmetic would give on x86

An experiment on a throwaway branch (`exp/fused`, local, not for `main`): the compiler left to
contract, with FMA available. Not a design: MSVC breaks twelve bit-exact rows that way, which is
why the real thing would be written with explicit fused operations. It measures what there is to
gain.

**NUC, Ryzen 7 8745H, GCC 13.3:**

| Case | Baseline (SSE2) | AVX2, not fused (`main`) | AVX2, fused by GCC |
| --- | --- | --- | --- |
| `static` | 4.0 µs (0.607 %) | 3.8 µs (0.565 %) | 3.1 µs (0.466 %) |
| `dynamic` | 20.5 µs (3.08 %) | 16.3 µs (2.45 %) | 15.2 µs (2.27 %) |
| `transient` | 22.8 µs (3.42 %) | 18.8 µs (2.82 %) | 17.1 µs (2.57 %) |
| **`track`** | **40.7 µs (6.11 %)** | **36.7 µs (5.50 %)** | **33.1 µs (4.96 %)** |
| `seeding` | 11.1 µs (1.67 %) | 6.4 µs (0.958 %) | 4.9 µs (0.735 %) |
| `static.64` | 97.3 µs (14.6 %) | 95.0 µs (14.3 %) | 72.9 µs (10.9 %) |
| `dynself.64` | 342.2 µs (51.3 %) | 227.6 µs (34.1 %) | 189.5 µs (28.4 %) |
| `track.64` | 138.5 µs (20.8 %) | 147.9 µs (22.2 %) | 123.5 µs (18.5 %) |

- `-mavx2` alone, with GCC, is worth 10 % on the budget case and 40 % on the bank: its code for
  the detector was the slow one of the baseline.
- Fused on top: 10 % more on the budget case, 17 % on the static EQ, 23 % on 64 static channels.

**Windows, Core Ultra 7 255H, MSVC:** fused by the compiler, the stereo cases gain 0 to 7 % (the
budget case 4 %). The figures past the first minute of that run are not usable (below).

## The question for the author

Every Apple silicon Mac and every processor with AVX2 has a fused multiply-add, correctly rounded
on both. The kernels of Stages 2 and 3 can be written with it explicitly, the same operations on
AVX2 and NEON:

- **For:** on the M4, 13 % of the budget case and 30 to 37 % of the wide layouts, which is what
  its baseline had; on the NUC, 10 % and 17 to 23 %; on the Windows machine about 4 %. And NEON
  and AVX2 would give the same bits as each other wherever the C library is not called.
- **Against:** the output changes once more (declared, like the Mac's today). And a processor
  without FMA cannot follow: its scalar build would either be very slow (a software `fma`) or
  give different bits from the SIMD builds. The scalar kernels would still be held to the SIMD
  ones bit for bit in CI, on machines that have FMA; the build for an old processor would be a
  third arithmetic, held by its own suite.
- **It is decided before the kernels**, not after: `exp2`, the coefficient routine, the filters
  and the detectors are each written once, in one arithmetic.

## What the AVX2 option alone does with MSVC

No bit changes. Most cases move by under 3 %. Two do not:

| Case | Baseline | `/arch:AVX2` |
| --- | --- | --- |
| `moving` | 8.4 µs (1.26 %) | 12.4 µs (1.87 %) |
| `env` | 17.1 µs (2.57 %) | 20.3 µs (3.04 %) |
| `track` | 32.9 µs (4.93 %) | 34.4 µs (5.17 %) |

Both are the band's coefficients made per sample, around a call to the library's `exp2`. Not yet
explained; the coefficient routine and our own `exp2` replace exactly that code, and the figure
is taken again then. Until then the Windows build is slower than the baseline on those cases.

## The trackers' pair loops

By instruction count (callgrind on the NUC, the budget case) the lock check and the spacing were
44 % of the tracker group and 13 % of everything. In time they were little: the budget case gains
1 % on Windows, 0 % on the NUC, 3 % on the M4; keyed by the input (`trackself`), 3, 6 and 10 %.
The tracker's time is in its feedback chain (estimate, `exp2`, tan, a divide, the filter, the
gradient, the estimate), which no count of instructions shows. The plan's estimate of 6 to 8 µs for
these loops came from a fit to the band sweeps, and the sweeps' bands do not cost the same.

The same profile, by instructions, for what it is worth as a map:

| Part | Share of the instructions |
| --- | --- |
| The tracker group (before this change) | 29 %, and 2 % in tan |
| Detectors, levels and gain computers (`detect`) | 15 % |
| The bands' coefficients and their filters, per lane | 17 % |
| The library's `exp2` (trackers and coefficients) | 10 % |
| The seeding bank | 10 % |
| `beginStep`, seeding decisions, the rest | 17 % |

## Where the time goes, sampled

The first profile by time: macOS's `sample` on the M4 Pro, eight seconds of the budget case at
`fbef822` (not fused, the pair loops gone), 6218 samples at the top of the stack.

| Part | Share of the time |
| --- | --- |
| The tracker group: coefficients, extraction, `tick` | 31 % |
| `Chain::track` itself: the seeding bank, the mean key | 19 % |
| The bands' coefficients and their filters, per lane | 17 % |
| Detectors, levels and gain computers | 15 % |
| The library's `exp2` | 11 % |
| Seeding decisions, `updateFrequencies`, `beginStep`, the rest | 8 % |

- **The bank is a fifth of the budget case** on its own, and it is thirty independent filters on
  one signal: the first kernel to write, as the plan has it.
- **The library's `exp2` is a tenth**, on this machine: more than MSVC's showed. Our own, written
  once for one value or four, is 7b's next step.
- **The tracker group is a third**, and is the part SIMD helps least: its time is the feedback
  chain. The first-order update of g (7f, the author's) is what shortens it.

How it was taken, for the next one: the bench runs far faster than real time, so
`likyu_bench time --scenario track --seconds 500` gives it some fifteen seconds to be sampled in;
then `sample <pid> 8 -file out.txt`, and the section "Sort by top of stack".

## Found on the way

- **MSVC 19.52 miscompiles a conditional store under `/arch:AVX2`.** `for i: if (bit i of mask)
  out[i] = float (w[i])`, the write-back of a step in which only some samples were touched, is
  vectorised into a store that writes zeros where the bit is clear. Ten lines reproduce it, with
  the option and not without. Found by the block-size row of `ChannelsTests` the first time the
  suite ran in Release with AVX2: fifteen samples of silence in one channel. The loop now walks
  the set bits. **No build before today had the option; every build after it needs the Release
  suite.**
- **The Release suite failed on Linux CI at the baseline commit**, with GCC 15.2 on the
  `ubuntu-26.04` image: five block-size rows, in `lib/dsp` as Phase 6 left it. GCC 15.2 on the NUC
  passes the same commit, so it is the image's compiler defaults. It passes since `4425c0b` and
  `3ed3c36`; which of the two cured it was not isolated. Debug never showed it.
- **The scalar and SIMD builds are compared in CI on the three platforms** from now on; on macOS
  that job is the only place NEON will be held to the scalar kernels at every push.
- **The Windows laptop slows by about 20 % after a minute of the suite once it is warm**: the
  last fifteen cases of a suite read high when builds ran just before. The first runs of the day
  did not show it. For the wide cases the farm's figures are the ones to trust; a pause between
  cases is to be tried.
- **`scripts/farm-bench.sh` passed its arguments through `ssh` unquoted** at first: the "scalar"
  runs on the farm were the default build under another name. The bench's first line names its
  target, which is how it was seen; fixed in `a349fad`, and the runs made again.
