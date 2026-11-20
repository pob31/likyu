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
    constexpr SvfCoefficients<T> makeSvfCoefficients (T g, T k, T m0, T m1, T m2) noexcept
    {
        const T a1 = T (1) / (T (1) + g * (g + k));
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

    /** One sample through one channel's state. */
    template <typename T>
    inline T svfTick (T x, T& ic1, T& ic2, const SvfCoefficients<T>& c) noexcept
    {
        const T v3 = x - ic2;
        const T v1 = c.a1 * ic1 + c.a2 * v3;
        const T v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = T (2) * v1 - ic1;
        ic2 = T (2) * v2 - ic2;
        return c.m0 * x + c.m1 * v1 + c.m2 * v2;
    }

    /** The state only, for a band whose mix is the identity: it stays warm
        for when the band's gain moves away from 0 dB. */
    template <typename T>
    inline void svfAdvance (T x, T& ic1, T& ic2, const SvfCoefficients<T>& c) noexcept
    {
        const T v3 = x - ic2;
        const T v1 = c.a1 * ic1 + c.a2 * v3;
        const T v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = T (2) * v1 - ic1;
        ic2 = T (2) * v2 - ic2;
    }
}
