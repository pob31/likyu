# Bench note, Phase 7: the baseline

2026-11-21, at `9b045ec`: `lib/dsp` as Phase 6 left it, measured with Phase 7's instruments before
any optimisation. This is the "before" of every figure the phase will give. The plan is in the
devplan's Phase 7; the after is `docs/bench/` at the end of it.

## The instruments

- **`likyu_bench`'s thread** is now at the priority a host gives its audio thread and on one
  performance core, as the notes of Phases 2 and 3 asked: MMCSS Pro Audio and an affinity on
  Windows, the user-interactive QoS on macOS (which has no affinity), an affinity and `SCHED_FIFO`
  on Linux. `--thread plain --core any` is the thread of the earlier notes.
- **A trimmed mean** beside the median: the mean of the middle 80 % of the blocks. A block of the
  static EQ is under 3 µs and the Windows clock steps by 0.1 µs, so the median moves in steps of
  3 %. The tables below are in it; the medians are in the JSON, and in the eight scenarios of the
  earlier notes they are within 1 % of it on Windows.
- **`likyu_bench suite`**: 27 cases, three runs of 3 s each, the median of the three, in one JSON.
  `scripts/bench-compare.py` makes the tables; `scripts/farm-bench.sh` runs it all on a farm
  machine from a bundle.
- **The bits ledger**: `likyu_bench hash` (19 lines: every scenario with awkward blocks and
  something of everything changed on the way) and the suite's own (`LIKYU_LEDGER`: 66 of the 166
  test cases render through it). An optimisation that changes no bit leaves both files as they
  were; a ledger is compared with one made on the same machine.
- **The suite in Release**, in CI on the three platforms and in a `*-release` test preset: 166
  cases, 2,664,961 assertions, green on the three machines below.

## The three machines

96 kHz, blocks of 64 samples (666.7 µs), float buffers, 8 bands and stereo unless the case says
otherwise. Microseconds per block, and the share of one core.

- **Windows**: Intel Core Ultra 7 255H, MSVC (Visual Studio 18), pinned to a performance core,
  MMCSS Pro Audio.
- **Mac mini**: Apple M4 Pro, Apple clang 17, user-interactive QoS.
- **NUC**: AMD Ryzen 7 8745H, Ubuntu 24.04, GCC 13.3, pinned, `SCHED_FIFO`.

| Case | Windows, 255H | Mac mini, M4 Pro | NUC, 8745H |
| --- | --- | --- | --- |
| `static` | 2.9 µs (0.436 %) | 2.4 µs (0.358 %) | 4.0 µs (0.607 %) |
| `moving` | 8.4 µs (1.26 %) | 6.1 µs (0.915 %) | 8.6 µs (1.29 %) |
| `dynamic` | 14.2 µs (2.13 %) | 9.6 µs (1.44 %) | 20.5 µs (3.08 %) |
| `dynself` | 14.1 µs (2.12 %) | 9.4 µs (1.42 %) | 20.5 µs (3.08 %) |
| `unlinked` | 23.5 µs (3.53 %) | 14.9 µs (2.23 %) | 29.6 µs (4.44 %) |
| `transient` | 15.1 µs (2.27 %) | 8.7 µs (1.30 %) | 22.8 µs (3.42 %) |
| `env` | 17.1 µs (2.57 %) | 10.4 µs (1.56 %) | 25.2 µs (3.78 %) |
| **`track`**, the PRD's budget case | **32.9 µs (4.93 %)** | **21.5 µs (3.23 %)** | **40.7 µs (6.11 %)** |
| `trackself` | 31.6 µs (4.74 %) | 20.6 µs (3.08 %) | 39.4 µs (5.90 %) |
| `seeding`, the bank alone | 4.8 µs (0.721 %) | 2.9 µs (0.431 %) | 11.1 µs (1.67 %) |
| `track.48k` | 32.6 µs (2.44 %) | 21.1 µs (1.58 %) | 41.0 µs (3.07 %) |
| `static.b0`, no band | 1.1 µs (0.158 %) | 0.8 µs (0.122 %) | 1.2 µs (0.178 %) |
| `static.b1` | 1.2 µs (0.180 %) | 1.0 µs (0.151 %) | 1.6 µs (0.238 %) |
| `static.b2` | 1.4 µs (0.217 %) | 1.2 µs (0.183 %) | 1.9 µs (0.290 %) |
| `static.b4` | 2.0 µs (0.294 %) | 1.6 µs (0.236 %) | 2.6 µs (0.395 %) |
| `dynamic.b1` | 2.7 µs (0.402 %) | 1.9 µs (0.291 %) | 3.9 µs (0.582 %) |
| `dynamic.b2` | 4.4 µs (0.654 %) | 3.0 µs (0.457 %) | 6.5 µs (0.969 %) |
| `dynamic.b4` | 7.6 µs (1.14 %) | 5.2 µs (0.784 %) | 11.4 µs (1.70 %) |
| `track.b1` | 10.3 µs (1.55 %) | 7.2 µs (1.08 %) | 16.0 µs (2.41 %) |
| `track.b2` | 12.8 µs (1.92 %) | 9.1 µs (1.37 %) | 19.4 µs (2.91 %) |
| `track.b4` | 18.6 µs (2.79 %) | 11.7 µs (1.75 %) | 25.8 µs (3.86 %) |
| `static.64` | 71.6 µs (10.7 %) | 35.1 µs (5.26 %) | 97.3 µs (14.6 %) |
| `dynamic.64`, on the stereo sidechain | 86.1 µs (12.9 %) | 42.6 µs (6.39 %) | 118.0 µs (17.7 %) |
| `dynself.64`, keyed by the input, linked | 166.5 µs (25.0 %) | 88.6 µs (13.3 %) | 342.2 µs (51.3 %) |
| `unlinked.64` | 695.7 µs (104.4 %) | 419.9 µs (63.0 %) | 856.8 µs (128.5 %) |
| **`track.64`**, the devplan's second case | **105.0 µs (15.8 %)** | **55.0 µs (8.25 %)** | **138.5 µs (20.8 %)** |
| `trackself.64` | 184.7 µs (27.7 %) | 100.8 µs (15.1 %) | 266.9 µs (40.0 %) |

- **The audio priority changes the worst block, not the figure.** The Windows medians are those
  of the Phase 6 note within 2 % (the budget case: 4.93 % against 5.03 %). The worst block of the
  budget case was 22 % of its period on a run alone and 92 % once in the suite: better than the
  337 % of Phase 3, and still the machine's, not the code's. On the NUC under `SCHED_FIFO` the
  budget case's worst block is 9 % of the period and its p99 is within 6 % of its median.
- **Unlinked on 64 channels** runs in real time on the M4 only.

## Where the time goes, on the Windows machine

From the sweeps above (`.b0` to `.b4`, then eight bands):

| Part of the budget case's 32.9 µs | µs a block | How it is read |
| --- | --- | --- |
| The frame: eight `beginStep`, the lookahead, the conversions | 1.1 | `static.b0` |
| The eight audio filters | 1.9 | `static` less the frame |
| What Dynamic adds: detectors, levels, the one-pole, coefficients per sample | 11.3 | `dynamic` less `static`; 1.4 to 1.5 a band |
| The seeding bank | 4.8 | `seeding`; `track.b1` less `dynamic.b1` is 7.6, the bank and one tracker |
| The trackers and what Track adds in the band path | 13.9 | the rest |

- **The trackers do not grow in proportion to their number.** From one Track band to two costs
  2.5 µs, from two to four 2.9 a band, from four to eight 3.6 a band: the lock checks and the
  spacing run over every pair of trackers, every sample. The first of the plan's remedies, and one
  that changes no bit.
- **Dynamic costs six times the filter it moves** (1.4 to 1.5 µs a band against 0.23): the
  band's coefficients made per sample.
- **64 channels:** static is filters (10.7 %); keyed by the input and linked, the detectors are
  another 12 % of a core (`dynself.64` less `dynamic.64`); unlinked is the coefficients, once per
  channel per sample.

## Between the machines

- **The NUC is slowest where the detector runs**: the seeding bank costs 2.3 times what it does on
  Windows, and `dynself.64` twice, where the rest is 1.0 to 1.5 times. GCC's code for the
  detector's sample loop (a `switch` on the type per sample) is the first place to look; taking the
  `switch` out of the loop is in the plan for every compiler.
- **The M4 is the fastest of the three** on every case, with the build it has today: Apple clang
  fuses multiply-adds by default. The first change of Stage 1 turns that off, so that the SIMD and
  scalar kernels give the same bits; what it costs the M4 is the next figure to take.
- **The ledgers differ between machines, as expected.** All 19 lines of the bench's differ between
  Windows and the Mac; 11 differ between Windows and the NUC, the 8 others being the same bits from
  MSVC and from GCC. Nothing is compared across machines.
