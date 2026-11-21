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
    Which instructions lib/dsp's kernels are written in, chosen when it is
    built and never while it runs (PRD, Architecture; devplan, Phase 7).

      LIKYU_SIMD_AVX2     x86-64 with AVX2: eight floats or four doubles to a
                          register. Required of an x86 processor since
                          2026-11-21 (decided by the author): the build is one
                          binary, with no dispatch.
      LIKYU_SIMD_NEON     arm64: four floats or two doubles to a register, so
                          the same kernels take registers in pairs.
      LIKYU_SIMD_SCALAR   no intrinsic at all: the same kernels over plain
                          arrays. Asked for with -DLIKYU_SIMD=scalar, for a
                          processor older than AVX2, and built in CI to hold
                          the other two: the three give the same bits.

    lib/dsp/CMakeLists.txt passes the compiler what the first needs, to
    lib/dsp and to everything that includes its headers. A client that
    compiles them some other way gets the #error below, not a slower build.
*/

#if defined (LIKYU_SIMD_SCALAR)
 #define LIKYU_SIMD_NAME "scalar"
#elif defined (__AVX2__)
 #define LIKYU_SIMD_AVX2 1
 #define LIKYU_SIMD_NAME "AVX2"
#elif defined (__aarch64__) || defined (_M_ARM64)
 #define LIKYU_SIMD_NEON 1
 #define LIKYU_SIMD_NAME "NEON"
#else
 #error "lib/dsp: neither AVX2 nor NEON in this translation unit. Compile it with /arch:AVX2 or -mavx2 (lib/dsp/CMakeLists.txt does, for its clients too), or configure with -DLIKYU_SIMD=scalar."
#endif

namespace likyu::dsp::simd
{
    /** "AVX2", "NEON" or "scalar": what this build's kernels are written in. */
    inline constexpr const char* targetName = LIKYU_SIMD_NAME;
}
