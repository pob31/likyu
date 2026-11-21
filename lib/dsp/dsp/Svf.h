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
    THE STATE-VARIABLE FILTER (PRD, Filter core): the trapezoidal
    (zero-delay-feedback) SVF in Simper's published form, as the PRD writes it,
    every band and every detector.

        g  = tan(pi fc / fs)          k = 1 / Q
        a1 = 1 / (1 + g (g + k))      a2 = g a1      a3 = g a2

        v3  = x - ic2
        v1  = a1 ic1 + a2 v3          (band-pass state)
        v2  = ic2 + a2 ic1 + a3 v3    (low-pass)
        ic1 = 2 v1 - ic1
        ic2 = 2 v2 - ic2

        y = m0 x + m1 v1 + m2 v2

    The state is two numbers per channel; the coefficients are shared by every
    channel of a band. T is the precision of state and coefficients: double in
    the audio path (PRD, decided 2026-11-16), any floating type for the
    precision measurement (devplan 2d).

    Standard library only.
*/

#include "Math.h"

namespace likyu::dsp
{
    template <typename T>
    struct SvfCoefficients
    {
        T a1 {}, a2 {}, a3 {};
        T m0 { 1 }, m1 {}, m2 {};
    };

    /** The coefficients for a warped frequency g, a damping k and a mix. */
    template <typename T>
    LIKYU_INLINE SvfCoefficients<T> makeSvfCoefficients (T g, T k, T m0, T m1, T m2) noexcept
    {
        const T a1 = T (1) / mulAdd (g, g + k, T (1));
        const T a2 = g * a1;
        const T a3 = g * a2;
        return { a1, a2, a3, m0, m1, m2 };
    }

    /** True when the mix is exactly (1, 0, 0): the output is the input,
        whatever the state. The caller then copies rather than computes, which
        keeps -0 and, under denormals-are-zero, a denormal input as they are. */
    template <typename T>
    constexpr bool isIdentity (const SvfCoefficients<T>& c) noexcept
    {
        return exactlyEqual (c.m0, T (1)) && exactlyEqual (c.m1, T (0)) && exactlyEqual (c.m2, T (0));
    }

    /*  THE TICK, in the build's arithmetic (dsp/Math.h, mulAdd; decided
        2026-11-21). Each line is written once, here, and a SIMD kernel says
        the same operations in the same order:

            v3  = x - ic2
            v1  = mulAdd (a1, ic1, a2 * v3)
            v2  = mulAdd (a3, v3, mulAdd (a2, ic1, ic2))
            ic1 = mulAdd (2, v1, -ic1)
            ic2 = mulAdd (2, v2, -ic2)
            y   = mulAdd (m2, v2, mulAdd (m1, v1, m0 * x))

        The state updates are 2 v - ic either way, since doubling is exact;
        said with mulAdd, the path of a state from one sample to the next is
        two fused operations where the processor has them, and that path is
        what the speed of the filter is. */

    /** One sample through one channel's state, the six terms given one by
        one: for a caller that keeps them an array a term. */
    template <typename T>
    LIKYU_INLINE T svfTick (T x, T& ic1, T& ic2, T a1, T a2, T a3, T m0, T m1, T m2) noexcept
    {
        const T v3 = x - ic2;
        const T v1 = mulAdd (a1, ic1, a2 * v3);
        const T v2 = mulAdd (a3, v3, mulAdd (a2, ic1, ic2));
        ic1 = mulAdd (T (2), v1, -ic1);
        ic2 = mulAdd (T (2), v2, -ic2);
        return mulAdd (m2, v2, mulAdd (m1, v1, m0 * x));
    }

    /** The state only, for a band whose mix is the identity: it stays warm
        for when the band's gain moves away from 0 dB. */
    template <typename T>
    LIKYU_INLINE void svfAdvance (T x, T& ic1, T& ic2, T a1, T a2, T a3) noexcept
    {
        const T v3 = x - ic2;
        const T v1 = mulAdd (a1, ic1, a2 * v3);
        const T v2 = mulAdd (a3, v3, mulAdd (a2, ic1, ic2));
        ic1 = mulAdd (T (2), v1, -ic1);
        ic2 = mulAdd (T (2), v2, -ic2);
    }

    /** One sample through one channel's state. */
    template <typename T>
    inline T svfTick (T x, T& ic1, T& ic2, const SvfCoefficients<T>& c) noexcept
    {
        return svfTick (x, ic1, ic2, c.a1, c.a2, c.a3, c.m0, c.m1, c.m2);
    }

    /** The state only, from a set of coefficients. */
    template <typename T>
    inline void svfAdvance (T x, T& ic1, T& ic2, const SvfCoefficients<T>& c) noexcept
    {
        svfAdvance (x, ic1, ic2, c.a1, c.a2, c.a3);
    }
}
