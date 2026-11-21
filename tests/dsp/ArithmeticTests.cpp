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

/*
    What the build promises of arithmetic itself (devplan, Phase 7): the
    canary of lib/dsp/CMakeLists.txt's "no fused multiply-add". Every
    bit-exact row of the suite, and the equality of the scalar and the SIMD
    kernels, rests on a multiply and an add being two roundings wherever
    lib/dsp is compiled, this translation unit included.
*/

#include <doctest/doctest.h>

#include <dsp/Math.h>

using namespace likyu::dsp;

namespace
{
    /** a b + c, written as one expression, which is what a compiler contracts. */
    template <typename T>
    T multiplyThenAdd (T a, T b, T c) noexcept
    {
        return a * b + c;
    }
}

TEST_CASE ("arithmetic: a multiply and an add are two roundings, never a fused one")
{
    /*  (1 + e)(1 - e) = 1 - e^2. With e^2 half a unit in the last place under
        1, the product rounds to 1 exactly (a tie, to the even mantissa), and
        less 1 it is 0. Fused, the product is not rounded, and the sum is
        -e^2. Through volatiles, so that nothing is folded when compiling. */
    SUBCASE ("double")
    {
        volatile double e = 0x1p-27, one = 1.0;
        const double a = one + e, b = one - e, c = -one;
        CHECK (exactlyEqual (multiplyThenAdd (a, b, c), 0.0));
    }

    SUBCASE ("float")
    {
        volatile float e = 0x1p-13f, one = 1.0f;
        const float a = one + e, b = one - e, c = -one;
        CHECK (exactlyEqual (multiplyThenAdd (a, b, c), 0.0f));
    }
}
