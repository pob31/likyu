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
    The registers lib/dsp's kernels are written in (PRD, Architecture: layout
    for speed; devplan, Phase 7). Our own, thin, and no library: lib/dsp
    includes the standard library and the intrinsics' headers, nothing else.

    A KERNEL IS WRITTEN ONCE, as a template over a vector type, and says the
    operations of the scalar code it stands for, in the same order. Every
    type here has the same logical width on every target:

        F32x8   eight floats: one AVX2 register, two NEON registers

    and comes twice:

        simd::scalar::F32x8   a plain array and a loop a lane. Always
                              there: it is the kernels of the reference and
                              scalar builds, and what the tests hold the
                              other to.
        simd::native::F32x8   the target's registers (dsp/simd/Target.h); the
                              scalar one again where the build has none.

    `simd::F32x8` is the native one. tests/dsp/SimdTests.cpp runs every
    operation on both and compares the bits.

    THE SAME BITS, by construction and not by luck:
      - an operation here is one IEEE operation a lane, the one its name
        says. Nothing is reassociated, and the build never contracts
        (lib/dsp/CMakeLists.txt);
      - `mulAdd` is the build's fused multiply-add (dsp/Math.h): fused in the
        registers exactly where the scalar `mulAdd` is;
      - `max` is std::max, to the letter: (a < b) ? b : a. It matters when
        one side is not a number or the two are zeros of opposite signs,
        where a processor's own max instruction answers differently from
        std::max and differently between x86 and arm.

    More types (four doubles, two doubles) and more operations arrive with
    the kernels that need them, each with its lines in SimdTests.cpp.
*/

#include <dsp/Math.h>
#include <dsp/simd/Target.h>

#if defined (LIKYU_SIMD_AVX2)
 #include <immintrin.h>
#elif defined (LIKYU_SIMD_NEON)
 #include <arm_neon.h>
#endif

namespace likyu::dsp::simd
{
    //==========================================================================
    namespace scalar
    {
        struct F32x8
        {
            static constexpr int lanes = 8;

            float v[lanes];

            static LIKYU_INLINE F32x8 load (const float* from) noexcept
            {
                F32x8 r;

                for (int i = 0; i < lanes; ++i)
                    r.v[i] = from[i];

                return r;
            }

            static LIKYU_INLINE F32x8 broadcast (float x) noexcept
            {
                F32x8 r;

                for (int i = 0; i < lanes; ++i)
                    r.v[i] = x;

                return r;
            }

            LIKYU_INLINE void store (float* to) const noexcept
            {
                for (int i = 0; i < lanes; ++i)
                    to[i] = v[i];
            }
        };

        LIKYU_INLINE F32x8 operator+ (F32x8 a, F32x8 b) noexcept
        {
            for (int i = 0; i < F32x8::lanes; ++i)
                a.v[i] = a.v[i] + b.v[i];

            return a;
        }

        LIKYU_INLINE F32x8 operator- (F32x8 a, F32x8 b) noexcept
        {
            for (int i = 0; i < F32x8::lanes; ++i)
                a.v[i] = a.v[i] - b.v[i];

            return a;
        }

        LIKYU_INLINE F32x8 operator* (F32x8 a, F32x8 b) noexcept
        {
            for (int i = 0; i < F32x8::lanes; ++i)
                a.v[i] = a.v[i] * b.v[i];

            return a;
        }

        LIKYU_INLINE F32x8 operator- (F32x8 a) noexcept
        {
            for (int i = 0; i < F32x8::lanes; ++i)
                a.v[i] = -a.v[i];

            return a;
        }

        /** a b + c, a lane at a time, in the build's arithmetic. */
        LIKYU_INLINE F32x8 mulAdd (F32x8 a, F32x8 b, F32x8 c) noexcept
        {
            for (int i = 0; i < F32x8::lanes; ++i)
                a.v[i] = likyu::dsp::mulAdd (a.v[i], b.v[i], c.v[i]);

            return a;
        }

        /** std::max (a, b), a lane at a time: (a < b) ? b : a. */
        LIKYU_INLINE F32x8 max (F32x8 a, F32x8 b) noexcept
        {
            for (int i = 0; i < F32x8::lanes; ++i)
                a.v[i] = a.v[i] < b.v[i] ? b.v[i] : a.v[i];

            return a;
        }
    }

    //==========================================================================
   #if defined (LIKYU_SIMD_AVX2)

    namespace native
    {
        struct F32x8
        {
            static constexpr int lanes = 8;

            __m256 v;

            static LIKYU_INLINE F32x8 load (const float* from) noexcept     { return { _mm256_loadu_ps (from) }; }
            static LIKYU_INLINE F32x8 broadcast (float x) noexcept          { return { _mm256_set1_ps (x) }; }
            LIKYU_INLINE void store (float* to) const noexcept              { _mm256_storeu_ps (to, v); }
        };

        LIKYU_INLINE F32x8 operator+ (F32x8 a, F32x8 b) noexcept            { return { _mm256_add_ps (a.v, b.v) }; }
        LIKYU_INLINE F32x8 operator- (F32x8 a, F32x8 b) noexcept            { return { _mm256_sub_ps (a.v, b.v) }; }
        LIKYU_INLINE F32x8 operator* (F32x8 a, F32x8 b) noexcept            { return { _mm256_mul_ps (a.v, b.v) }; }

        // The sign bit turned over, which is what a scalar minus does, whatever the value.
        LIKYU_INLINE F32x8 operator- (F32x8 a) noexcept                     { return { _mm256_xor_ps (a.v, _mm256_set1_ps (-0.0f)) }; }

        LIKYU_INLINE F32x8 mulAdd (F32x8 a, F32x8 b, F32x8 c) noexcept      { return { _mm256_fmadd_ps (a.v, b.v, c.v) }; }

        // maxps (x, y) is x > y ? x : y, and y when either is not a number or
        // both are zeros: with x = b and y = a, that is (a < b) ? b : a.
        LIKYU_INLINE F32x8 max (F32x8 a, F32x8 b) noexcept                  { return { _mm256_max_ps (b.v, a.v) }; }
    }

   #elif defined (LIKYU_SIMD_NEON)

    namespace native
    {
        struct F32x8
        {
            static constexpr int lanes = 8;

            float32x4_t lo, hi;

            static LIKYU_INLINE F32x8 load (const float* from) noexcept     { return { vld1q_f32 (from), vld1q_f32 (from + 4) }; }
            static LIKYU_INLINE F32x8 broadcast (float x) noexcept          { return { vdupq_n_f32 (x), vdupq_n_f32 (x) }; }

            LIKYU_INLINE void store (float* to) const noexcept
            {
                vst1q_f32 (to, lo);
                vst1q_f32 (to + 4, hi);
            }
        };

        LIKYU_INLINE F32x8 operator+ (F32x8 a, F32x8 b) noexcept            { return { vaddq_f32 (a.lo, b.lo), vaddq_f32 (a.hi, b.hi) }; }
        LIKYU_INLINE F32x8 operator- (F32x8 a, F32x8 b) noexcept            { return { vsubq_f32 (a.lo, b.lo), vsubq_f32 (a.hi, b.hi) }; }
        LIKYU_INLINE F32x8 operator* (F32x8 a, F32x8 b) noexcept            { return { vmulq_f32 (a.lo, b.lo), vmulq_f32 (a.hi, b.hi) }; }
        LIKYU_INLINE F32x8 operator- (F32x8 a) noexcept                     { return { vnegq_f32 (a.lo), vnegq_f32 (a.hi) }; }

        // vfmaq (c, a, b) is c + a b, fused.
        LIKYU_INLINE F32x8 mulAdd (F32x8 a, F32x8 b, F32x8 c) noexcept
        {
            return { vfmaq_f32 (c.lo, a.lo, b.lo), vfmaq_f32 (c.hi, a.hi, b.hi) };
        }

        // Not vmaxq, which answers a number where std::max keeps what is not
        // one: the comparison and a choice, (a < b) ? b : a.
        LIKYU_INLINE F32x8 max (F32x8 a, F32x8 b) noexcept
        {
            return { vbslq_f32 (vcltq_f32 (a.lo, b.lo), b.lo, a.lo), vbslq_f32 (vcltq_f32 (a.hi, b.hi), b.hi, a.hi) };
        }
    }

   #else

    namespace native = scalar;

   #endif

    using F32x8 = native::F32x8;
}
