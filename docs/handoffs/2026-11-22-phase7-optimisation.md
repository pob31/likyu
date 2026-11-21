# Handoff: Phase 7, the optimisation

2026-11-22. Replaces [`2026-11-21-phase7-foundations.md`](2026-11-21-phase7-foundations.md) as the
place to start; that note still holds for how the rule is held, step by step, and what bit on the
way. [`2026-11-20-phase6.md`](2026-11-20-phase6.md) still holds for what the author owes the earlier
phases: the listening passes, Band-pass, the freeze of the IDs.

## Where it stands

- **The optimisation half of Phase 7 is built**: the plan's stages 7a to 7d, the quiet band of 7c,
  the stop of 7f measured, the record of 7g. 7e, the Mac's own pass, may wait (decided by the
  author). The installers are the other half of the phase, not begun.
- **The budget case: 2.96 % of a core on the Windows machine, 2.28 % on the M4 Pro, 2.85 % on the
  Ryzen NUC**, against 4.93, 3.23 and 6.11 % at the start of the phase. 64 channels linked: 6.1,
  5.0 and 5.8 % (15.8, 8.3, 20.8). Every case of the suite is faster on every machine. The five
  bench notes of the phase are in `docs/bench/2026-11-2*-phase7-*.md`, in the order baseline,
  foundations, arithmetic, kernels, stop.
- **The output changed three times, each declared in its commit, and not since `0c86fad`**:
  contraction off (the Mac's only), the fused arithmetic, our own 2^x. Nothing the listening passes
  could hear; the passes still owed will hear the plugin that ships.
- **Not installed in the hosts.** The Windows build installed for the author's sessions is from
  before the phase.

## Needs the author

From the stop note, [`2026-11-22-phase7-stop.md`](../bench/2026-11-22-phase7-stop.md), each also in
the PRD's open decisions:

1. **The remedies that change the numbers.** *Proposed:* neither now. The first-order update of a
   tracker's g holds every threshold and gains 0 to 5 % of the budget case (13 to 14 % for one
   Track band alone); the bank only while a tracker is unlocked gains nothing measurable. Both are
   on the local branch `exp/first-order-g`, not pushed.
2. **The macOS installer's x86_64 slice.** *Proposed:* the scalar build. AVX2 there would not load
   under the Rosetta of macOS 11 to 14 nor on a pre-Haswell Intel Mac, and under the Rosetta of
   macOS 15 it is half the speed of the scalar build (8.4 % against 4.4 % of a core, measured).
3. **The Mac's own pass (7e)**, when the author wants it: the detectors in registers cost the M4
   2 to 4 % in stereo, the first thing to look at there.

## What was built after the foundations note

- **Kernels** (`lib/dsp`), each a template over the register type, each held to the code it
  replaced in `tests/dsp/KernelTests.cpp`:
  - `SeedBank::run`: the bank's thirty regions the lanes;
  - `runTrackerGroup`: its members' coefficients and extraction filters the lanes, from three
    members (`minLaneMembers`);
  - `Band::coefficientsAlong`: a band's coefficients four samples at a time, for the four places
    that make them;
  - `runDetectorLanes` (Detector.h) and `Chain::runDetectors`: a key channel's eight detectors in
    one register, before the bands of a stretch; `detect` takes the powers from `slotPower`;
  - `tickAcross` and `runChannels` (Chain.cpp): the filters across channels, `work` sample-major.
- **`dsp/simd/Vec.h`**: F32x8, F64x4, F64x2, and the masks M32x8, M64x4, M64x2, on three
  back-ends. `pow2Lanes`, `Warp::gLanes`, `bandALanes`, `lerpLanes`, `smoothstepLanes`,
  `designBand` and `makeSvfCoefficients` are written once for a double or a register.
- **The quiet band** (`Band::beginStep`): a band in which nothing moves or waits skips its step;
  each computer says when its step would change nothing (`isSettled`, `isPending`).

## Things that will bite

Those of the foundations note stand. Added since:

- **A new operation of `Vec.h` gets its lines in `SimdTests`**, with the awkward values, against
  both the plain arrays and the scalar expression. A test lambda must not read a variable in the
  expression that modifies it (the two-double tick did, once).
- **A new kernel gets its test in `KernelTests`** against the scalar code it stands for, written
  out there; `same-bits.sh` then holds the build on the three platforms.
- **The quiet band rests on every setter being seen.** A new mutator of `Band` or of one of its
  computers must either break `quiet` or be visible to `settledInside()`; `setContext`,
  `setTarget`, `snap`, `setTransientTimes` and `prepare` are today's.
- **`work` is sample-major** (`[sample * channels + channel]`); `listened`, `keys` and the lanes'
  buffers are still channel-major.
- **Small groups and stereo are measured cases**: the tracker group keeps its loop under three
  members, the constant band its registers under two channels, because the registers lost there.
  Measure both before changing a threshold.
- **Rosetta translates AVX2 and FMA on macOS 15.8**, slowly: an x86_64 build can be tested on the
  Mac with `-DCMAKE_OSX_ARCHITECTURES=x86_64` and `arch -x86_64`.

## Next

1. The author's answers to the stop note.
2. The installers (the devplan's Phase 7, second half): Windows, macOS (universal, the x86_64 slice
   as decided), Linux; signing and notarisation.
3. The listening passes on a build from `main`, the plugin installed again in the hosts.
