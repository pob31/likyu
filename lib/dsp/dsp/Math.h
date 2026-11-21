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

#include <functional>

// A multiply and an add are two roundings, in every translation unit that includes
// lib/dsp (lib/dsp/CMakeLists.txt says why). MSVC defines this under /fp:contract and
// /fp:fast; GCC and Clang have no such macro, and tests/dsp/ArithmeticTests.cpp is
// what holds them.
#if defined (_M_FP_CONTRACT)
 #error "lib/dsp must be compiled without floating-point contraction: /fp:precise, and no /fp:contract or /fp:fast"
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

    /** 3u^2 - 2u^3: from 0 to 1 with a flat start and a flat end, exactly 0
        at 0 and exactly 1 at 1. A crossfade shaped by it has no corner, and a
        corner is what a linear crossfade sprays above the signal. */
    template <typename T>
    constexpr T smoothstep (T u) noexcept
    {
        return u * u * (T (3) - T (2) * u);
    }
}
