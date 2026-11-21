# Handoff: Phase 7, the arithmetic and the first kernel

2026-11-21, end of the day. The place to start for the optimisation.
[`2026-11-20-phase6.md`](2026-11-20-phase6.md) still holds for everything the author owes the
earlier phases: the listening passes, Band-pass, the freeze of the IDs. None of it depends on this
work. **What this work has changed in the output is declared, three times, and all three are
done**: the last bits of every sample moved today, on every machine, and nothing that could be
heard.

## Where it stands

- **The plan** is the devplan's Phase 7, stages 7a to 7g, approved by the author on 2026-11-21.
  Read it first, then the three bench notes of the day, in order:
  [baseline](../bench/2026-11-21-phase7-baseline.md),
  [foundations](../bench/2026-11-21-phase7-foundations.md),
  [arithmetic](../bench/2026-11-21-phase7-arithmetic.md).
- **The budget case**: 4.00 % of a core on the Windows machine (4.93 % at the baseline), 2.69 % on
  the M4 Pro (3.23 %), 3.86 % on the Ryzen NUC (6.11 %).
- **Built:** 7a whole; 7b whole (contraction off, `LIKYU_SIMD`, AVX2, the fused arithmetic, `pow2`,
  `Vec.h` with its first type); of 7c, the trackers' pair loops and the coefficients an array a
  term; of 7d, the seeding bank.
- **Decided by the author, 2026-11-21**, all in the PRD with the date: AVX2 required on x86, the
  scalar build an option; a remedy that changes the numbers is brought with its figures first;
  fused multiply-adds, written explicitly.

## Next, in the order the profile gives

The last time profile (the M4, before the bank's kernel): the tracker group 31 %, the bank 19 %
(now 4 %), coefficients and filters 17 %, detection 15 %, `exp2` 11 % (now ours, inline).
**Take a new one first** (`sample`, as the foundations note says), then:

1. **Coefficients and levels along the 16 samples of a step** (7d): the coefficient functions of
   `Band.h` as one elementwise kernel on four doubles, `pow2` included (its table read a lane at a
   time), filling `StretchCoefficients` four samples at once; `powerToDb` on eight floats. Needs
   `F64x4` in `Vec.h` (compare, blend, the bit casts of `pow2` and `fastLog2`), each with its lines
   in `SimdTests`. The traps are in the plan's table: `moving ? lerp : end` stays a broadcast; an
   offset of exactly 0 blends the argument of `pow2`, not its result; a stretch under 16 computes
   whole registers from padded inputs.
2. **The tracker group's front half** across its members: `pow2`, tan, the divide and the
   extraction filter of each, eight to a register; `tick` stays scalar.
3. **The detectors**, eight bands of a key slot to a register: the heaviest bookkeeping. Worth it
   on wide layouts keyed by the input (`dynself.64`, `trackself.64`).
4. **The filters across channels** with `work` sample-major: the wide layouts' largest part
   (`static.64` is 9.8 % of a core on Windows and has not moved).
5. Of 7c, still: a settled band skips its ramps; the lookahead in runs.
6. 7e (the Mac's own pass), 7f (the stop: the first-order g of the tracker, which is the only
   thing that shortens its feedback chain, and the x86_64 slice), 7g (the record, the README).

## The rule, and how it is held

An optimisation commit changes no output bit. After every commit, on this machine:

```sh
# the three trees, Release (configured once:
#   cmake --preset vs -B build/vs-reference -DLIKYU_SIMD=reference -DLIKYU_BUILD_PLUGINS=OFF
#   cmake --preset vs -B build/vs-scalar    -DLIKYU_SIMD=scalar    -DLIKYU_BUILD_PLUGINS=OFF)
cmake --build --preset vs-release --target likyu_dsp_tests likyu_bench
cmake --build build/vs-reference --config Release --target likyu_dsp_tests likyu_bench
cmake --build build/vs-scalar --config Release --target likyu_dsp_tests likyu_bench

# the suites pass, and AVX2 gives the bits of the reference
scripts/same-bits.sh build/vs/tests/Release build/vs-reference/tests/Release
build/vs-scalar/tests/Release/likyu_dsp_tests.exe          # its own arithmetic, its own suite

# and they are the bits of the commit before: keep a ledger pair from before the change
build/vs/tests/Release/likyu_bench.exe hash --expect <ledger of the commit before>
LIKYU_LEDGER=<absolute path> build/vs/tests/Release/likyu_dsp_tests.exe    # then cmp
```

Then `ctest --preset vs-debug`, the four `scripts/check-*.py`, and on the farm, **before the
push**, `scripts/farm-bench.sh macmini` and `scripts/farm-bench.sh nuc`, each again with
`reference -DLIKYU_SIMD=reference`: the suite in Release, the two ledgers and the bench's JSON
come back under `build/farm/<host>/<sha>-<variant>/`, and `cmp` says whether NEON and AVX2 give
the reference's bits. NEON cannot be compiled here: the farm is where a kernel first runs on it.
`scripts/bench-compare.py` makes a note's table from two or more of those JSON files.

The ledgers are per machine and are not committed. The current ones are in `build/farm/` and
`build/ledger/` on the author's Windows machine (`pow2-suite.txt`, `pow2-bench.txt`: nothing has
moved since `0c86fad`).

## The shape of it

- **`dsp/simd/Target.h`**: the target (AVX2, NEON, scalar kernels) and the arithmetic
  (`LIKYU_FUSED`). `LIKYU_SIMD` is `auto`, `reference` or `scalar`.
- **`dsp/Math.h`**: `mulAdd`, and `LIKYU_INLINE`. A product and sum is fused only where `mulAdd`
  is written; a plain `a * b + c` is two roundings everywhere, and `lerpExact` and the other
  exact helpers are made of those.
- **`dsp/Svf.h`**: the tick, written once with its six terms as arguments, in the form every
  kernel repeats (its head says it line by line). The detector, the tracker and the bank say the
  same in float.
- **`dsp/Exp2.h`**: `pow2`. Every 2^x of `lib/dsp` goes through it.
- **`dsp/Band.h`**: `StretchCoefficients`, six arrays; the three coefficient functions fill it
  through `set`. They are still three scalar functions: the kernel of item 1 replaces their loops.
- **`dsp/simd/Vec.h`**: `F32x8` on three back-ends, `scalar` always there, `native` the target's.
  A kernel is a template over the type (`SeedBank::run`), and the tests instantiate it on both.
- **`tests/dsp/`**: `ArithmeticTests` (the two canaries), `Exp2Tests`, `SimdTests` (every
  operation, registers against arrays against the scalar expression), `KernelTests` (each kernel
  three ways).
- **`tests/bench/`**: `time`, `suite`, `hash`, `precision`; `BenchThread.cpp` has the platform
  calls. `DspTestMain.cpp` keeps the suite's ledger.

## Things that will bite

- **Only Release shows what the optimiser does.** MSVC's miscompiled conditional store under
  `/arch:AVX2` and five block-size rows on Linux CI were invisible in Debug.
- **MSVC does not inline what it should, and copies structs through memory.** A function that
  returns coefficients by value and is not inlined costs two stalled loads a sample. Per-sample
  values go into arrays a term at a time; what makes them is `LIKYU_INLINE`. When a figure will
  not move on Windows, read the listing: configure a tree with
  `-DCMAKE_CXX_FLAGS="/DWIN32 /D_WINDOWS /EHsc /FAs"` (with `MSYS2_ARG_CONV_EXCL='*'` in Git Bash)
  and look in `Chain.asm`.
- **A conditional store with a narrowing conversion** is not to be written in a loop MSVC can
  vectorise (`Chain::process` walks the set bits).
- **`std::max` is `(a < b) ? b : a`**, and no processor's max instruction is that when a side is
  not a number. `Vec.h` says it to the letter; a new operation gets the same care and its lines in
  `SimdTests`, with the awkward values.
- **A lane of padding runs like the others.** Give it coefficients of 0 and inputs that are
  numbers, and never read it. The bank's lanes 30 and 31 are such.
- **An estimate is not a measurement.** The trackers' pair loops were 13 % of the instructions
  and 1 to 3 % of the time; `exp2` was thought a large cost on Windows and was not; the slow
  `moving` was thought to be `exp2` and was struct copies. Measure, then read the listing.
- **The Windows laptop throttles when warm**: the last fifteen cases of a suite can read 20 % high
  after a build. Run it cold; for the wide cases the farm is the reference.
- **`ssh` splits arguments** (`farm-bench.sh` pipes its values in a script); the bench's first
  line names its target, read it. **A changed compiler wipes a CMake cache**: remove a farm tree
  before giving it another. **The farm scripts must not run two at once.**
- **A script passed through the shell loses its backslashes and trips on apostrophes.** Write an
  edit script to the scratchpad as a file, or edit with the editor.
- **`exp/fused`** is a local branch with the day's experiment. It is not for `main` and can go.
