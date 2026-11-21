# Handoff: Phase 7, the instruments and the foundations

2026-11-21. The place to start for the optimisation. [`2026-11-20-phase6.md`](2026-11-20-phase6.md)
still holds for everything the author owes the earlier phases: the listening passes, Band-pass, the
freeze of the IDs. None of it depends on this work, and this work changes nothing they will hear,
with the exceptions it declares.

## Where it stands

- **The plan** is in the devplan's Phase 7, in stages 7a to 7g, approved by the author on
  2026-11-21. Read it first, then the two bench notes of the day:
  [`2026-11-21-phase7-baseline.md`](../bench/2026-11-21-phase7-baseline.md) and
  [`2026-11-21-phase7-foundations.md`](../bench/2026-11-21-phase7-foundations.md).
- **Built:** 7a (the instruments), the build half of 7b (contraction off, `LIKYU_SIMD`, AVX2, the
  scalar build in CI), and the first step of 7c (the trackers' pair loops).
- **Nothing is faster yet where it counts.** The budget case on Windows is 5.10 % against 4.93 %
  at the baseline (the AVX2 option alone slows MSVC's per-sample coefficients, to be measured again
  after the coefficient routine); on the NUC 5.51 % against 6.11 %; on the M4 3.60 % against
  3.23 %, because its baseline was fused.
- **Decided by the author, 2026-11-21**, both in the PRD with the date: AVX2 is required on x86,
  the scalar build stays as an option; a remedy that changes the numbers is brought with its
  figures first.

## Needs the author, before anything else is written

**Fused multiply-adds in the kernels** (PRD, open decision; the foundations note has the figures).
Without them the M4 lost 15 % on the budget case and up to 60 % on 64 channels; with them the NUC
would gain 10 to 23 %; Windows about 4 %. The cost is a second change of the output and a scalar
build for old processors that is its own arithmetic. `exp2`, `Vec.h`, the coefficient routine, the
filters and the detectors are each written once in the arithmetic chosen, so this comes first.

Two more, later (7f): the remedies that change the numbers, and the universal binary's x86_64
slice.

## The rule, and how it is held

An optimisation commit changes no output bit. After every commit, on one machine:

```sh
# both builds, Release (the scalar tree is configured once:
#   cmake --preset vs -B build/vs-scalar -DLIKYU_SIMD=scalar -DLIKYU_BUILD_PLUGINS=OFF)
cmake --build --preset vs-release --target likyu_dsp_tests likyu_bench
cmake --build build/vs-scalar --config Release --target likyu_dsp_tests likyu_bench

# the suites pass, and the two builds give the same bits
scripts/same-bits.sh build/vs/tests/Release build/vs-scalar/tests/Release

# and they are the bits of the commit before: keep a ledger pair from before the change
build/vs/tests/Release/likyu_bench.exe hash --expect <ledger of the commit before>
LIKYU_LEDGER=<absolute path> build/vs/tests/Release/likyu_dsp_tests.exe    # then cmp
```

Then `ctest --preset vs-debug`, the four `scripts/check-*.py`, and on the farm
`scripts/farm-bench.sh macmini` and `scripts/farm-bench.sh nuc` (and each again with
`scalar -DLIKYU_SIMD=scalar`): the suite in Release, the two ledgers and the bench's JSON come
back under `build/farm/<host>/<sha>-<variant>/`. `scripts/bench-compare.py` makes a note's table
from two or more of those JSON files.

The ledgers are per machine and are not committed. Today's are in `build/farm/` on the author's
Windows machine; on Windows and the NUC they have not changed since the baseline.

## The shape of it

- **`tests/bench/BenchMain.cpp`**: `time`, `suite`, `hash`, `precision`. `BenchThread.cpp` is the
  one file with platform calls. The ledger's signals and settings are arithmetic alone, so that it
  does not depend on how a compiler treats the bench.
- **`tests/dsp/DspTestMain.cpp`**: a doctest listener keeps a hash per test case; `render`
  (`DspTestSupport.h`) feeds it. 66 of the 167 cases render through it.
- **`lib/dsp/dsp/simd/Target.h`**: names the target and refuses a translation unit compiled
  otherwise. `Math.h` includes it and refuses `/fp:contract`. Both options are PUBLIC on
  `likyu_dsp`, so the plugin, the editor's headers and the tests are compiled alike.
- **`tests/dsp/ArithmeticTests.cpp`**: the contraction canary. Seen to fail on the Mac without the
  option (−5.55e-17) and to pass with it.
- **`Tracker.h`**: `tick` takes the lock check as a function and asks it at one place;
  `runTrackerGroup` skips the spacing when `apart` holds for every pair.

## Things that will bite

- **Only Release shows what the optimiser does.** Two failures today were invisible in Debug:
  MSVC's miscompiled conditional store under `/arch:AVX2`, and five block-size rows on Linux CI
  with the image's GCC 15.2. Run the Release suite before every push.
- **A conditional store with a narrowing conversion** (`if (bit) out[i] = float (w[i])`) is not to
  be written in a loop MSVC can vectorise. `Chain::process` walks the set bits instead.
- **The Windows laptop throttles.** Warm, it reads the last fifteen cases of the suite about 20 %
  high. Run the suite cold, or trust its first ten cases and the farm for the wide ones. Do not
  time anything locally while a build runs.
- **`ssh` splits arguments.** `farm-bench.sh` puts its values in the script it pipes. The bench's
  first line names its target: read it before believing a "scalar" figure.
- **A changed compiler wipes a CMake cache and its `-D` options with it**: remove a farm build
  tree before giving it another compiler.
- **The farm scripts must not run two at once**: both make and delete the branch `farm-bench`.
- **Inline Python in a heredoc turns `\\n` into a newline** on its way through the tool. Edit
  source that has escapes with the editor, not with a script.
- **`exp/fused`** is a local branch with the experiment's one commit. It is not for `main`.
- **An estimate is not a measurement.** The plan put 6 to 8 µs on the trackers' pair loops, from a
  fit to the band sweeps; they were 1 to 3 % of the time. `callgrind` on the NUC counts
  instructions (`perf` is closed to users there); nothing on the three machines samples time yet.
  On the Mac, `sample` and `xctrace` are installed and untried.

## Next

1. The author's answer on fused multiply-adds.
2. 7b's second half in that arithmetic: `exp2` (the second declared change of bits), `Vec.h` with
   its three back-ends and their tests. Then the Windows figures for `moving` and `env` again.
3. 7c: one coefficient routine; the sample loop outside in the bank, the detectors and the
   filters; then 7d's kernels, the seeding bank first.
4. A time profile, on the Mac or by opening `perf` on the NUC, before choosing between 7d's
   kernels: the tracker's feedback chain is the largest single cost and no instruction count
   shows it.
