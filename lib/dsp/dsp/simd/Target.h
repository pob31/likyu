/*
    This file is part of Likyu — https://github.com/pob31/likyu

    Copyright (C) 2026 Pierre-Olivier Boulant

    Likyu is free software: you can redistribute it and/or modify it under the
    terms of the GNU General Public License as published by the Free Software
    Foundation, either version 3 of the License, or (at your option) any later
    version. Likyu is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
    or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
    (LICENSE, at the repository root) for more details.

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

/*
    Which instructions lib/dsp's kernels are written in, and which arithmetic
    they compute in, chosen when it is built and never while it runs (PRD,
    Realtime rules: arithmetic and the processor; devplan, Phase 7).

    THE KERNELS

      LIKYU_SIMD_AVX2     x86-64 with AVX2 and FMA: eight floats or four
                          doubles to a register. Required of an x86 processor
                          since 2026-11-21 (decided by the author): the build
                          is one binary, with no dispatch.
      LIKYU_SIMD_NEON     arm64: four floats or two doubles to a register, so
                          the same kernels take registers in pairs.
      LIKYU_SIMD_SCALAR   no intrinsic at all: the same kernels over plain
                          arrays (-DLIKYU_SIMD=reference or scalar).

    THE ARITHMETIC (decided 2026-11-21 by the author: fused, explicitly)

      LIKYU_FUSED 1       `mulAdd` (dsp/Math.h) is a b + c in one rounding.
                          Every build but one: AVX2, NEON, the scalar kernels
                          on arm64, and -DLIKYU_SIMD=reference, which is the
                          scalar kernels with the fused operation on x86. All
                          of these give the same bits on one machine, and CI
                          holds the SIMD builds to the reference.
      LIKYU_FUSED 0       -DLIKYU_SIMD=scalar on x86: the build for a
                          processor older than AVX2, which has no such
                          instruction. `mulAdd` is two roundings there, so its
                          output differs from the others' in its last bits. It
                          is held by its own suite, not to their ledger.

    A compiler never fuses by itself in any of them (lib/dsp/CMakeLists.txt):
    a plain a * b + c is two roundings everywhere.

    lib/dsp/CMakeLists.txt passes the compiler what each needs, to lib/dsp
    and to everything that includes its headers. A client that compiles them
    some other way gets the #error below, not a slower build or other bits.
*/

#if defined (LIKYU_SIMD_SCALAR)
 #if defined (LIKYU_SIMD_REFERENCE) || defined (__aarch64__) || defined (_M_ARM64)
  #define LIKYU_FUSED 1
  #define LIKYU_SIMD_NAME "scalar kernels, fused"
 #else
  #define LIKYU_FUSED 0
  #define LIKYU_SIMD_NAME "scalar kernels, not fused"
 #endif
#elif defined (__AVX2__) && (defined (__FMA__) || defined (_MSC_VER))
 #define LIKYU_SIMD_AVX2 1
 #define LIKYU_FUSED 1
 #define LIKYU_SIMD_NAME "AVX2"
#elif defined (__aarch64__) || defined (_M_ARM64)
 #define LIKYU_SIMD_NEON 1
 #define LIKYU_FUSED 1
 #define LIKYU_SIMD_NAME "NEON"
#else
 #error "lib/dsp: neither AVX2 with FMA nor NEON in this translation unit. Compile it with /arch:AVX2 or -mavx2 -mfma (lib/dsp/CMakeLists.txt does, for its clients too), or configure with -DLIKYU_SIMD=scalar."
#endif

namespace likyu::dsp::simd
{
    /** What this build's kernels are written in, for a report's first line. */
    inline constexpr const char* targetName = LIKYU_SIMD_NAME;

    /** Whether `mulAdd` is one rounding in this build. */
    inline constexpr bool fused = LIKYU_FUSED != 0;
}
