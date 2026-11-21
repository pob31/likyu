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
    Small numeric helpers shared by the filters. Standard library only.

    EXACTNESS IS A FEATURE HERE. The exact bypass (PRD, Filter core) rests on
    a few comparisons and interpolations that must be exact, not close:
    `exactlyEqual` says so where it is meant, and the build's -Wfloat-equal
    stays on for every comparison that is not.
*/

#include <dsp/simd/Target.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <type_traits>

// A multiply and an add are two roundings, in every translation unit that includes
// lib/dsp (lib/dsp/CMakeLists.txt says why). MSVC defines this under /fp:contract and
// /fp:fast; GCC and Clang have no such macro, and tests/dsp/ArithmeticTests.cpp is
// what holds them.
#if defined (_M_FP_CONTRACT)
 #error "lib/dsp must be compiled without floating-point contraction: /fp:precise, and no /fp:contract or /fp:fast"
#endif

/*  LIKYU_INLINE: inline, and not left to the compiler's judgement. For the few
    small functions that a sample's coefficients are made of (2^x, a band's row of
    the table, the fade). Found 2026-11-21 in MSVC's listing: left as calls, each
    returned its 48 bytes of coefficients through memory, written a field at a time
    and copied with 32-byte loads, which waits on every one of those stores; it was
    half again the cost of a moving band under /arch:AVX2. */
#if defined (_MSC_VER) && ! defined (__clang__)
 #define LIKYU_INLINE __forceinline
#elif defined (__GNUC__) || defined (__clang__)
 #define LIKYU_INLINE inline __attribute__ ((always_inline))
#else
 #define LIKYU_INLINE inline
#endif

namespace likyu::dsp
{
    inline constexpr double pi = 3.14159265358979323846;

    /** a == b, on purpose. */
    template <typename T>
    constexpr bool exactlyEqual (T a, T b) noexcept
    {
        return std::equal_to<T> {} (a, b);
    }

    /** (1 - t) a + t b. Exactly a at t = 0 and exactly b at t = 1, for finite a
        and b: a ramp that is interpolated with it lands on its end value. */
    template <typename T>
    constexpr T lerpExact (T a, T b, T t) noexcept
    {
        return (T (1) - t) * a + t * b;
    }

    /** a b + c, the fused multiply-add of the build's arithmetic (PRD,
        Realtime rules; decided 2026-11-21 by the author): ONE rounding
        wherever the processor has the instruction, which is every build but
        the scalar one for an x86 older than AVX2 (dsp/simd/Target.h).

        It is written where a recurrence or a polynomial has this shape, by
        hand, and nowhere else: the compiler is told never to fuse by itself,
        so that the scalar code and a SIMD kernel, which says the same thing
        with an intrinsic, give the same bits. A plain a * b + c beside it
        stays two roundings, and is still what `lerpExact` and the other
        exact helpers here are made of.

        Every compiler of the build makes one instruction of it (checked for
        MSVC under /arch:AVX2, 2026-11-21); in a Debug build it is the C
        library's, with the same result. */
    template <typename T>
    inline T mulAdd (T a, T b, T c) noexcept
    {
       #if LIKYU_FUSED
        return std::fma (a, b, c);
       #else
        return a * b + c;
       #endif
    }

    /** A constant as a T: the value itself for a float or a double, the
        value in every lane for a register (dsp/simd/Vec.h). So that code
        written once over T says its constants once. */
    template <typename T>
    LIKYU_INLINE T splat (double x) noexcept
    {
        if constexpr (std::is_floating_point_v<T>)
            return static_cast<T> (x);
        else
            return T::broadcast (static_cast<typename T::Element> (x));
    }

    /** std::clamp, under the name the registers' own has (dsp/simd/Vec.h),
        so that code written once over a scalar or a register type says it once. */
    template <typename T>
    LIKYU_INLINE T clampLanes (T v, T lo, T hi) noexcept requires std::is_floating_point_v<T>
    {
        return std::clamp (v, lo, hi);
    }

    /** std::abs, under the registers' name. */
    template <typename T>
    LIKYU_INLINE T absLanes (T v) noexcept requires std::is_floating_point_v<T>
    {
        return std::abs (v);
    }

    /** lerpExact's expression, for a scalar or a register: the same operations. */
    template <typename T>
    LIKYU_INLINE T lerpLanes (T a, T b, T t) noexcept
    {
        return (splat<T> (1.0) - t) * a + t * b;
    }

    /** smoothstep's expression, for a scalar or a register: the same operations. */
    template <typename T>
    LIKYU_INLINE T smoothstepLanes (T u) noexcept
    {
        return u * u * (splat<T> (3.0) - splat<T> (2.0) * u);
    }

    /** 3u^2 - 2u^3: from 0 to 1 with a flat start and a flat end, exactly 0
        at 0 and exactly 1 at 1. A crossfade shaped by it has no corner, and a
        corner is what a linear crossfade sprays above the signal. */
    template <typename T>
    constexpr T smoothstep (T u) noexcept
    {
        return u * u * (T (3) - T (2) * u);
    }
}
