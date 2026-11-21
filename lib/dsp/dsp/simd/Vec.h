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
        F64x4   four doubles: one AVX2 register, two NEON registers
        F64x2   two doubles: half an AVX2 register, one NEON register
        M32x8   which of eight floats a choice takes, made from eight flags
        M64x4   which of four doubles a comparison held for

    and comes twice:

        simd::scalar::...   plain arrays and a loop a lane. Always there: it
                            is the kernels of the reference and scalar builds,
                            and what the tests hold the other to.
        simd::native::...   the target's registers (dsp/simd/Target.h); the
                            scalar ones again where the build has none.

    `simd::F32x8` and the others are the native ones. tests/dsp/SimdTests.cpp
    runs every operation on both and compares the bits.

    THE SAME BITS, by construction and not by luck:
      - an operation here is one IEEE operation a lane, the one its name
        says. Nothing is reassociated, and the build never contracts
        (lib/dsp/CMakeLists.txt);
      - `mulAdd` is the build's fused multiply-add (dsp/Math.h): fused in the
        registers exactly where the scalar `mulAdd` is;
      - `max` is std::max, to the letter: (a < b) ? b : a; `clampLanes` is
        std::clamp, to the letter: (v < lo) ? lo : (hi < v) ? hi : v. Both
        matter when a side is not a number or the two are zeros of opposite
        signs, where a processor's own max and min answer differently from
        the standard library and differently between x86 and arm;
      - a conversion from double to float rounds to the nearest, as a scalar
        static_cast does, and flushes to zero where the scalar one would.

    Generic code reaches a constant of either a scalar or a register type
    through `splat<T>` (dsp/Math.h), and a clamp through `clampLanes`.
*/

#include <dsp/Math.h>
#include <dsp/simd/Target.h>

#include <cstdint>

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
        struct M32x8;

        struct F32x8
        {
            using Element = float;
            using Mask = M32x8;
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

            static LIKYU_INLINE F32x8 make (float a, float b, float c, float d, float e, float f, float g, float h) noexcept
            {
                return { { a, b, c, d, e, f, g, h } };
            }

            LIKYU_INLINE void store (float* to) const noexcept
            {
                for (int i = 0; i < lanes; ++i)
                    to[i] = v[i];
            }
        };

        struct F64x4
        {
            using Element = double;
            static constexpr int lanes = 4;

            double v[lanes];

            static LIKYU_INLINE F64x4 load (const double* from) noexcept
            {
                F64x4 r;

                for (int i = 0; i < lanes; ++i)
                    r.v[i] = from[i];

                return r;
            }

            static LIKYU_INLINE F64x4 broadcast (double x) noexcept
            {
                return { { x, x, x, x } };
            }

            static LIKYU_INLINE F64x4 make (double a, double b, double c, double d) noexcept
            {
                return { { a, b, c, d } };
            }

            LIKYU_INLINE void store (double* to) const noexcept
            {
                for (int i = 0; i < lanes; ++i)
                    to[i] = v[i];
            }
        };

        struct M64x4
        {
            bool v[F64x4::lanes];
        };

        struct M32x8
        {
            bool v[F32x8::lanes];

            /** From eight flags, each 0 or -1. */
            static LIKYU_INLINE M32x8 load (const std::int32_t* flags) noexcept
            {
                M32x8 m;

                for (int i = 0; i < F32x8::lanes; ++i)
                    m.v[i] = flags[i] != 0;

                return m;
            }
        };

        /** `yes` where the mask holds, `no` where it does not. */
        LIKYU_INLINE F32x8 select (M32x8 m, F32x8 yes, F32x8 no) noexcept
        {
            for (int i = 0; i < F32x8::lanes; ++i)
                no.v[i] = m.v[i] ? yes.v[i] : no.v[i];

            return no;
        }

        // Eight floats ------------------------------------------------------
        template <typename Operation>
        LIKYU_INLINE F32x8 eachLane (F32x8 a, F32x8 b, Operation&& operation) noexcept
        {
            for (int i = 0; i < F32x8::lanes; ++i)
                a.v[i] = operation (a.v[i], b.v[i]);

            return a;
        }

        LIKYU_INLINE F32x8 operator+ (F32x8 a, F32x8 b) noexcept   { return eachLane (a, b, [] (float x, float y) { return x + y; }); }
        LIKYU_INLINE F32x8 operator- (F32x8 a, F32x8 b) noexcept   { return eachLane (a, b, [] (float x, float y) { return x - y; }); }
        LIKYU_INLINE F32x8 operator* (F32x8 a, F32x8 b) noexcept   { return eachLane (a, b, [] (float x, float y) { return x * y; }); }
        LIKYU_INLINE F32x8 operator/ (F32x8 a, F32x8 b) noexcept   { return eachLane (a, b, [] (float x, float y) { return x / y; }); }

        /** std::max (a, b), a lane at a time: (a < b) ? b : a. */
        LIKYU_INLINE F32x8 max (F32x8 a, F32x8 b) noexcept         { return eachLane (a, b, [] (float x, float y) { return x < y ? y : x; }); }

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

        // Four doubles -------------------------------------------------------
        template <typename Operation>
        LIKYU_INLINE F64x4 eachLane (F64x4 a, F64x4 b, Operation&& operation) noexcept
        {
            for (int i = 0; i < F64x4::lanes; ++i)
                a.v[i] = operation (a.v[i], b.v[i]);

            return a;
        }

        LIKYU_INLINE F64x4 operator+ (F64x4 a, F64x4 b) noexcept   { return eachLane (a, b, [] (double x, double y) { return x + y; }); }
        LIKYU_INLINE F64x4 operator- (F64x4 a, F64x4 b) noexcept   { return eachLane (a, b, [] (double x, double y) { return x - y; }); }
        LIKYU_INLINE F64x4 operator* (F64x4 a, F64x4 b) noexcept   { return eachLane (a, b, [] (double x, double y) { return x * y; }); }
        LIKYU_INLINE F64x4 operator/ (F64x4 a, F64x4 b) noexcept   { return eachLane (a, b, [] (double x, double y) { return x / y; }); }

        LIKYU_INLINE F64x4 operator- (F64x4 a) noexcept
        {
            for (int i = 0; i < F64x4::lanes; ++i)
                a.v[i] = -a.v[i];

            return a;
        }

        LIKYU_INLINE F64x4 mulAdd (F64x4 a, F64x4 b, F64x4 c) noexcept
        {
            for (int i = 0; i < F64x4::lanes; ++i)
                a.v[i] = likyu::dsp::mulAdd (a.v[i], b.v[i], c.v[i]);

            return a;
        }

        /** a < b, a lane at a time: false where either is not a number. */
        LIKYU_INLINE M64x4 less (F64x4 a, F64x4 b) noexcept
        {
            M64x4 m;

            for (int i = 0; i < F64x4::lanes; ++i)
                m.v[i] = a.v[i] < b.v[i];

            return m;
        }

        /** `yes` where the mask holds, `no` where it does not. */
        LIKYU_INLINE F64x4 select (M64x4 m, F64x4 yes, F64x4 no) noexcept
        {
            for (int i = 0; i < F64x4::lanes; ++i)
                no.v[i] = m.v[i] ? yes.v[i] : no.v[i];

            return no;
        }

        /** a == b, a lane at a time: false where either is not a number, true for 0 and -0. */
        LIKYU_INLINE M64x4 equal (F64x4 a, F64x4 b) noexcept
        {
            M64x4 m;

            for (int i = 0; i < F64x4::lanes; ++i)
                m.v[i] = exactlyEqual (a.v[i], b.v[i]);

            return m;
        }

        /** Where both masks hold. */
        LIKYU_INLINE M64x4 both (M64x4 a, M64x4 b) noexcept
        {
            for (int i = 0; i < F64x4::lanes; ++i)
                a.v[i] = a.v[i] && b.v[i];

            return a;
        }

        /** std::abs, a lane at a time: the sign bit cleared. */
        LIKYU_INLINE F64x4 absLanes (F64x4 a) noexcept
        {
            for (int i = 0; i < F64x4::lanes; ++i)
                a.v[i] = std::abs (a.v[i]);

            return a;
        }

        /** Eight floats from two times four doubles, each rounded to the nearest. */
        LIKYU_INLINE F32x8 narrow (F64x4 low, F64x4 high) noexcept
        {
            F32x8 r;

            for (int i = 0; i < F64x4::lanes; ++i)
            {
                r.v[i] = static_cast<float> (low.v[i]);
                r.v[i + F64x4::lanes] = static_cast<float> (high.v[i]);
            }

            return r;
        }

        /** std::clamp (v, lo, hi), to the letter. */
        LIKYU_INLINE F64x4 clampLanes (F64x4 v, F64x4 lo, F64x4 hi) noexcept
        {
            return select (less (v, lo), lo, select (less (hi, v), hi, v));
        }


        // Two doubles ---------------------------------------------------------
        struct M64x2
        {
            bool v[2];
        };

        struct F64x2
        {
            using Element = double;
            static constexpr int lanes = 2;

            double v[lanes];

            static LIKYU_INLINE F64x2 load (const double* from) noexcept    { return { { from[0], from[1] } }; }
            static LIKYU_INLINE F64x2 broadcast (double x) noexcept         { return { { x, x } }; }
            LIKYU_INLINE void store (double* to) const noexcept             { to[0] = v[0]; to[1] = v[1]; }
        };

        template <typename Operation>
        LIKYU_INLINE F64x2 eachLane (F64x2 a, F64x2 b, Operation&& operation) noexcept
        {
            for (int i = 0; i < F64x2::lanes; ++i)
                a.v[i] = operation (a.v[i], b.v[i]);

            return a;
        }

        LIKYU_INLINE F64x2 operator+ (F64x2 a, F64x2 b) noexcept   { return eachLane (a, b, [] (double x, double y) { return x + y; }); }
        LIKYU_INLINE F64x2 operator- (F64x2 a, F64x2 b) noexcept   { return eachLane (a, b, [] (double x, double y) { return x - y; }); }
        LIKYU_INLINE F64x2 operator* (F64x2 a, F64x2 b) noexcept   { return eachLane (a, b, [] (double x, double y) { return x * y; }); }
        LIKYU_INLINE F64x2 operator- (F64x2 a) noexcept            { return { { -a.v[0], -a.v[1] } }; }

        LIKYU_INLINE F64x2 mulAdd (F64x2 a, F64x2 b, F64x2 c) noexcept
        {
            return { { likyu::dsp::mulAdd (a.v[0], b.v[0], c.v[0]), likyu::dsp::mulAdd (a.v[1], b.v[1], c.v[1]) } };
        }

        LIKYU_INLINE M64x2 less (F64x2 a, F64x2 b) noexcept        { return { { a.v[0] < b.v[0], a.v[1] < b.v[1] } }; }

        LIKYU_INLINE F64x2 select (M64x2 m, F64x2 yes, F64x2 no) noexcept
        {
            return { { m.v[0] ? yes.v[0] : no.v[0], m.v[1] ? yes.v[1] : no.v[1] } };
        }

        LIKYU_INLINE F64x2 absLanes (F64x2 a) noexcept             { return { { std::abs (a.v[0]), std::abs (a.v[1]) } }; }

        // pow2Split, a lane at a time, is in dsp/Exp2.h, beside the scalar one it repeats.
    }

    //==========================================================================
   #if defined (LIKYU_SIMD_AVX2)

    namespace native
    {
        struct M32x8;

        struct F32x8
        {
            using Element = float;
            using Mask = M32x8;
            static constexpr int lanes = 8;

            __m256 v;

            static LIKYU_INLINE F32x8 load (const float* from) noexcept     { return { _mm256_loadu_ps (from) }; }
            static LIKYU_INLINE F32x8 broadcast (float x) noexcept          { return { _mm256_set1_ps (x) }; }

            static LIKYU_INLINE F32x8 make (float a, float b, float c, float d, float e, float f, float g, float h) noexcept
            {
                return { _mm256_setr_ps (a, b, c, d, e, f, g, h) };
            }

            LIKYU_INLINE void store (float* to) const noexcept              { _mm256_storeu_ps (to, v); }
        };

        struct F64x4
        {
            using Element = double;
            static constexpr int lanes = 4;

            __m256d v;

            static LIKYU_INLINE F64x4 load (const double* from) noexcept    { return { _mm256_loadu_pd (from) }; }
            static LIKYU_INLINE F64x4 broadcast (double x) noexcept         { return { _mm256_set1_pd (x) }; }
            static LIKYU_INLINE F64x4 make (double a, double b, double c, double d) noexcept { return { _mm256_setr_pd (a, b, c, d) }; }
            LIKYU_INLINE void store (double* to) const noexcept             { _mm256_storeu_pd (to, v); }
        };

        struct M64x4
        {
            __m256d m;
        };

        struct M32x8
        {
            __m256 m;

            static LIKYU_INLINE M32x8 load (const std::int32_t* flags) noexcept
            {
                return { _mm256_castsi256_ps (_mm256_loadu_si256 (reinterpret_cast<const __m256i*> (flags))) };
            }
        };

        // blendv takes `yes` where the mask's sign bit is set: every bit of a flag of -1.
        LIKYU_INLINE F32x8 select (M32x8 m, F32x8 yes, F32x8 no) noexcept   { return { _mm256_blendv_ps (no.v, yes.v, m.m) }; }

        LIKYU_INLINE F32x8 operator+ (F32x8 a, F32x8 b) noexcept            { return { _mm256_add_ps (a.v, b.v) }; }
        LIKYU_INLINE F32x8 operator- (F32x8 a, F32x8 b) noexcept            { return { _mm256_sub_ps (a.v, b.v) }; }
        LIKYU_INLINE F32x8 operator* (F32x8 a, F32x8 b) noexcept            { return { _mm256_mul_ps (a.v, b.v) }; }
        LIKYU_INLINE F32x8 operator/ (F32x8 a, F32x8 b) noexcept            { return { _mm256_div_ps (a.v, b.v) }; }

        // The sign bit turned over, which is what a scalar minus does, whatever the value.
        LIKYU_INLINE F32x8 operator- (F32x8 a) noexcept                     { return { _mm256_xor_ps (a.v, _mm256_set1_ps (-0.0f)) }; }

        LIKYU_INLINE F32x8 mulAdd (F32x8 a, F32x8 b, F32x8 c) noexcept      { return { _mm256_fmadd_ps (a.v, b.v, c.v) }; }

        // maxps (x, y) is x > y ? x : y, and y when either is not a number or
        // both are zeros: with x = b and y = a, that is (a < b) ? b : a.
        LIKYU_INLINE F32x8 max (F32x8 a, F32x8 b) noexcept                  { return { _mm256_max_ps (b.v, a.v) }; }

        LIKYU_INLINE F64x4 operator+ (F64x4 a, F64x4 b) noexcept            { return { _mm256_add_pd (a.v, b.v) }; }
        LIKYU_INLINE F64x4 operator- (F64x4 a, F64x4 b) noexcept            { return { _mm256_sub_pd (a.v, b.v) }; }
        LIKYU_INLINE F64x4 operator* (F64x4 a, F64x4 b) noexcept            { return { _mm256_mul_pd (a.v, b.v) }; }
        LIKYU_INLINE F64x4 operator/ (F64x4 a, F64x4 b) noexcept            { return { _mm256_div_pd (a.v, b.v) }; }
        LIKYU_INLINE F64x4 operator- (F64x4 a) noexcept                     { return { _mm256_xor_pd (a.v, _mm256_set1_pd (-0.0)) }; }
        LIKYU_INLINE F64x4 mulAdd (F64x4 a, F64x4 b, F64x4 c) noexcept      { return { _mm256_fmadd_pd (a.v, b.v, c.v) }; }

        // Ordered and quiet: false where either is not a number, as < is.
        LIKYU_INLINE M64x4 less (F64x4 a, F64x4 b) noexcept                 { return { _mm256_cmp_pd (a.v, b.v, _CMP_LT_OQ) }; }
        LIKYU_INLINE F64x4 select (M64x4 m, F64x4 yes, F64x4 no) noexcept   { return { _mm256_blendv_pd (no.v, yes.v, m.m) }; }
        LIKYU_INLINE M64x4 equal (F64x4 a, F64x4 b) noexcept                { return { _mm256_cmp_pd (a.v, b.v, _CMP_EQ_OQ) }; }
        LIKYU_INLINE M64x4 both (M64x4 a, M64x4 b) noexcept                 { return { _mm256_and_pd (a.m, b.m) }; }
        LIKYU_INLINE F64x4 absLanes (F64x4 a) noexcept                      { return { _mm256_andnot_pd (_mm256_set1_pd (-0.0), a.v) }; }

        /** std::clamp (v, lo, hi), to the letter. */
        LIKYU_INLINE F64x4 clampLanes (F64x4 v, F64x4 lo, F64x4 hi) noexcept
        {
            return select (less (v, lo), lo, select (less (hi, v), hi, v));
        }

        LIKYU_INLINE F32x8 narrow (F64x4 low, F64x4 high) noexcept
        {
            return { _mm256_insertf128_ps (_mm256_castps128_ps256 (_mm256_cvtpd_ps (low.v)), _mm256_cvtpd_ps (high.v), 1) };
        }

        LIKYU_INLINE void pow2Split (F64x4 whole, const double* table, F64x4& fraction, F64x4& power) noexcept
        {
            const auto bits = _mm256_add_epi64 (_mm256_castpd_si256 (whole.v), _mm256_set1_epi64x (std::int64_t { 1023 } << 6));
            const auto index = _mm256_and_si256 (bits, _mm256_set1_epi64x (63));
            fraction = { _mm256_i64gather_pd (table, index, 8) };
            const auto exponent = _mm256_and_si256 (_mm256_srli_epi64 (bits, 6), _mm256_set1_epi64x (0x7ff));
            power = { _mm256_castsi256_pd (_mm256_slli_epi64 (exponent, 52)) };
        }

        // Two doubles: the lower half of an AVX register, the instructions of AVX and FMA.
        struct M64x2
        {
            __m128d m;
        };

        struct F64x2
        {
            using Element = double;
            static constexpr int lanes = 2;

            __m128d v;

            static LIKYU_INLINE F64x2 load (const double* from) noexcept    { return { _mm_loadu_pd (from) }; }
            static LIKYU_INLINE F64x2 broadcast (double x) noexcept         { return { _mm_set1_pd (x) }; }
            LIKYU_INLINE void store (double* to) const noexcept             { _mm_storeu_pd (to, v); }
        };

        LIKYU_INLINE F64x2 operator+ (F64x2 a, F64x2 b) noexcept            { return { _mm_add_pd (a.v, b.v) }; }
        LIKYU_INLINE F64x2 operator- (F64x2 a, F64x2 b) noexcept            { return { _mm_sub_pd (a.v, b.v) }; }
        LIKYU_INLINE F64x2 operator* (F64x2 a, F64x2 b) noexcept            { return { _mm_mul_pd (a.v, b.v) }; }
        LIKYU_INLINE F64x2 operator- (F64x2 a) noexcept                     { return { _mm_xor_pd (a.v, _mm_set1_pd (-0.0)) }; }
        LIKYU_INLINE F64x2 mulAdd (F64x2 a, F64x2 b, F64x2 c) noexcept      { return { _mm_fmadd_pd (a.v, b.v, c.v) }; }
        LIKYU_INLINE M64x2 less (F64x2 a, F64x2 b) noexcept                 { return { _mm_cmp_pd (a.v, b.v, _CMP_LT_OQ) }; }
        LIKYU_INLINE F64x2 select (M64x2 m, F64x2 yes, F64x2 no) noexcept   { return { _mm_blendv_pd (no.v, yes.v, m.m) }; }
        LIKYU_INLINE F64x2 absLanes (F64x2 a) noexcept                      { return { _mm_andnot_pd (_mm_set1_pd (-0.0), a.v) }; }
    }

   #elif defined (LIKYU_SIMD_NEON)

    namespace native
    {
        struct M32x8;

        struct F32x8
        {
            using Element = float;
            using Mask = M32x8;
            static constexpr int lanes = 8;

            float32x4_t lo, hi;

            static LIKYU_INLINE F32x8 load (const float* from) noexcept     { return { vld1q_f32 (from), vld1q_f32 (from + 4) }; }
            static LIKYU_INLINE F32x8 broadcast (float x) noexcept          { return { vdupq_n_f32 (x), vdupq_n_f32 (x) }; }

            static LIKYU_INLINE F32x8 make (float a, float b, float c, float d, float e, float f, float g, float h) noexcept
            {
                const float values[8] = { a, b, c, d, e, f, g, h };
                return load (values);
            }

            LIKYU_INLINE void store (float* to) const noexcept
            {
                vst1q_f32 (to, lo);
                vst1q_f32 (to + 4, hi);
            }
        };

        struct F64x4
        {
            using Element = double;
            static constexpr int lanes = 4;

            float64x2_t lo, hi;

            static LIKYU_INLINE F64x4 load (const double* from) noexcept    { return { vld1q_f64 (from), vld1q_f64 (from + 2) }; }
            static LIKYU_INLINE F64x4 broadcast (double x) noexcept         { return { vdupq_n_f64 (x), vdupq_n_f64 (x) }; }

            static LIKYU_INLINE F64x4 make (double a, double b, double c, double d) noexcept
            {
                const double values[4] = { a, b, c, d };
                return load (values);
            }

            LIKYU_INLINE void store (double* to) const noexcept
            {
                vst1q_f64 (to, lo);
                vst1q_f64 (to + 2, hi);
            }
        };

        struct M64x4
        {
            uint64x2_t lo, hi;
        };

        struct M32x8
        {
            uint32x4_t lo, hi;

            static LIKYU_INLINE M32x8 load (const std::int32_t* flags) noexcept
            {
                return { vreinterpretq_u32_s32 (vld1q_s32 (flags)), vreinterpretq_u32_s32 (vld1q_s32 (flags + 4)) };
            }
        };

        // vbsl takes `yes` bit by bit where the mask is set: every bit of a flag of -1.
        LIKYU_INLINE F32x8 select (M32x8 m, F32x8 yes, F32x8 no) noexcept
        {
            return { vbslq_f32 (m.lo, yes.lo, no.lo), vbslq_f32 (m.hi, yes.hi, no.hi) };
        }

        LIKYU_INLINE F32x8 operator+ (F32x8 a, F32x8 b) noexcept            { return { vaddq_f32 (a.lo, b.lo), vaddq_f32 (a.hi, b.hi) }; }
        LIKYU_INLINE F32x8 operator- (F32x8 a, F32x8 b) noexcept            { return { vsubq_f32 (a.lo, b.lo), vsubq_f32 (a.hi, b.hi) }; }
        LIKYU_INLINE F32x8 operator* (F32x8 a, F32x8 b) noexcept            { return { vmulq_f32 (a.lo, b.lo), vmulq_f32 (a.hi, b.hi) }; }
        LIKYU_INLINE F32x8 operator/ (F32x8 a, F32x8 b) noexcept            { return { vdivq_f32 (a.lo, b.lo), vdivq_f32 (a.hi, b.hi) }; }
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

        LIKYU_INLINE F64x4 operator+ (F64x4 a, F64x4 b) noexcept            { return { vaddq_f64 (a.lo, b.lo), vaddq_f64 (a.hi, b.hi) }; }
        LIKYU_INLINE F64x4 operator- (F64x4 a, F64x4 b) noexcept            { return { vsubq_f64 (a.lo, b.lo), vsubq_f64 (a.hi, b.hi) }; }
        LIKYU_INLINE F64x4 operator* (F64x4 a, F64x4 b) noexcept            { return { vmulq_f64 (a.lo, b.lo), vmulq_f64 (a.hi, b.hi) }; }
        LIKYU_INLINE F64x4 operator/ (F64x4 a, F64x4 b) noexcept            { return { vdivq_f64 (a.lo, b.lo), vdivq_f64 (a.hi, b.hi) }; }
        LIKYU_INLINE F64x4 operator- (F64x4 a) noexcept                     { return { vnegq_f64 (a.lo), vnegq_f64 (a.hi) }; }

        LIKYU_INLINE F64x4 mulAdd (F64x4 a, F64x4 b, F64x4 c) noexcept
        {
            return { vfmaq_f64 (c.lo, a.lo, b.lo), vfmaq_f64 (c.hi, a.hi, b.hi) };
        }

        LIKYU_INLINE M64x4 less (F64x4 a, F64x4 b) noexcept                 { return { vcltq_f64 (a.lo, b.lo), vcltq_f64 (a.hi, b.hi) }; }

        LIKYU_INLINE F64x4 select (M64x4 m, F64x4 yes, F64x4 no) noexcept
        {
            return { vbslq_f64 (m.lo, yes.lo, no.lo), vbslq_f64 (m.hi, yes.hi, no.hi) };
        }

        LIKYU_INLINE M64x4 equal (F64x4 a, F64x4 b) noexcept                { return { vceqq_f64 (a.lo, b.lo), vceqq_f64 (a.hi, b.hi) }; }
        LIKYU_INLINE M64x4 both (M64x4 a, M64x4 b) noexcept                 { return { vandq_u64 (a.lo, b.lo), vandq_u64 (a.hi, b.hi) }; }
        LIKYU_INLINE F64x4 absLanes (F64x4 a) noexcept                      { return { vabsq_f64 (a.lo), vabsq_f64 (a.hi) }; }

        /** std::clamp (v, lo, hi), to the letter. */
        LIKYU_INLINE F64x4 clampLanes (F64x4 v, F64x4 lo, F64x4 hi) noexcept
        {
            return select (less (v, lo), lo, select (less (hi, v), hi, v));
        }

        LIKYU_INLINE F32x8 narrow (F64x4 low, F64x4 high) noexcept
        {
            return { vcombine_f32 (vcvt_f32_f64 (low.lo), vcvt_f32_f64 (low.hi)),
                     vcombine_f32 (vcvt_f32_f64 (high.lo), vcvt_f32_f64 (high.hi)) };
        }

        namespace detail
        {
            LIKYU_INLINE void pow2Split (float64x2_t whole, const double* table, float64x2_t& fraction, float64x2_t& power) noexcept
            {
                const auto bits = vaddq_u64 (vreinterpretq_u64_f64 (whole), vdupq_n_u64 (std::uint64_t { 1023 } << 6));
                const auto index = vandq_u64 (bits, vdupq_n_u64 (63));
                fraction = vsetq_lane_f64 (table[vgetq_lane_u64 (index, 1)], vdupq_n_f64 (table[vgetq_lane_u64 (index, 0)]), 1);
                const auto exponent = vandq_u64 (vshrq_n_u64 (bits, 6), vdupq_n_u64 (0x7ff));
                power = vreinterpretq_f64_u64 (vshlq_n_u64 (exponent, 52));
            }
        }

        LIKYU_INLINE void pow2Split (F64x4 whole, const double* table, F64x4& fraction, F64x4& power) noexcept
        {
            detail::pow2Split (whole.lo, table, fraction.lo, power.lo);
            detail::pow2Split (whole.hi, table, fraction.hi, power.hi);
        }

        // Two doubles: one NEON register.
        struct M64x2
        {
            uint64x2_t m;
        };

        struct F64x2
        {
            using Element = double;
            static constexpr int lanes = 2;

            float64x2_t v;

            static LIKYU_INLINE F64x2 load (const double* from) noexcept    { return { vld1q_f64 (from) }; }
            static LIKYU_INLINE F64x2 broadcast (double x) noexcept         { return { vdupq_n_f64 (x) }; }
            LIKYU_INLINE void store (double* to) const noexcept             { vst1q_f64 (to, v); }
        };

        LIKYU_INLINE F64x2 operator+ (F64x2 a, F64x2 b) noexcept            { return { vaddq_f64 (a.v, b.v) }; }
        LIKYU_INLINE F64x2 operator- (F64x2 a, F64x2 b) noexcept            { return { vsubq_f64 (a.v, b.v) }; }
        LIKYU_INLINE F64x2 operator* (F64x2 a, F64x2 b) noexcept            { return { vmulq_f64 (a.v, b.v) }; }
        LIKYU_INLINE F64x2 operator- (F64x2 a) noexcept                     { return { vnegq_f64 (a.v) }; }
        LIKYU_INLINE F64x2 mulAdd (F64x2 a, F64x2 b, F64x2 c) noexcept      { return { vfmaq_f64 (c.v, a.v, b.v) }; }
        LIKYU_INLINE M64x2 less (F64x2 a, F64x2 b) noexcept                 { return { vcltq_f64 (a.v, b.v) }; }
        LIKYU_INLINE F64x2 select (M64x2 m, F64x2 yes, F64x2 no) noexcept   { return { vbslq_f64 (m.m, yes.v, no.v) }; }
        LIKYU_INLINE F64x2 absLanes (F64x2 a) noexcept                      { return { vabsq_f64 (a.v) }; }
    }

   #else

    namespace native = scalar;

   #endif

    using F32x8 = native::F32x8;
    using F64x4 = native::F64x4;
    using F64x2 = native::F64x2;
    using M32x8 = native::M32x8;
    using M64x4 = native::M64x4;
}
