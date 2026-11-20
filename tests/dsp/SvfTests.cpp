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
    The bare SVF tick (PRD, Filter core). The types' responses are measured
    in ResponseTests.cpp; here, what the structure itself promises.
*/

#include <doctest/doctest.h>

#include <dsp/Svf.h>
#include <dsp/Tan.h>

#include <algorithm>
#include <cmath>
#include <tuple>

using namespace likyu::dsp;

namespace
{
    SvfCoefficients<double> lowPass (double hz, double q, double rate)
    {
        Warp warp;
        warp.prepare (rate);
        const auto k = 1.0 / q;
        return makeSvfCoefficients (warp.g (hz), k, 0.0, 0.0, 1.0);
    }
}

TEST_CASE ("svf: silence in, exact silence out, whatever the coefficients")
{
    for (const auto q : { 0.1, 0.707, 40.0 })
    {
        const auto c = lowPass (1000.0, q, 48000.0);
        double ic1 = 0.0, ic2 = 0.0;

        for (int n = 0; n < 1000; ++n)
            REQUIRE (svfTick (0.0, ic1, ic2, c) == 0.0);
    }
}

TEST_CASE ("svf: the low-pass output settles at the input's DC level")
{
    const auto c = lowPass (200.0, 0.707, 48000.0);
    double ic1 = 0.0, ic2 = 0.0, y = 0.0;

    for (int n = 0; n < 48000; ++n)
        y = svfTick (1.0, ic1, ic2, c);

    CHECK (y == doctest::Approx (1.0).epsilon (1.0e-12));
}

TEST_CASE ("svf: the band-pass state peaks at k times the input at fc (unity-peak band-pass is k v1)")
{
    constexpr double rate = 48000.0, hz = 1000.0, q = 4.0;
    const auto k = 1.0 / q;

    Warp warp;
    warp.prepare (rate);
    const auto c = makeSvfCoefficients (warp.g (hz), k, 0.0, k, 0.0);

    double ic1 = 0.0, ic2 = 0.0, peak = 0.0;

    for (int n = 0; n < 96000; ++n)
    {
        const auto y = svfTick (std::sin (2.0 * pi * hz * n / rate), ic1, ic2, c);

        if (n > 48000)
            peak = std::max (peak, std::abs (y));
    }

    CHECK (peak == doctest::Approx (1.0).epsilon (1.0e-3));
}

TEST_CASE ("svf: stable at the extremes of the parameter ranges")
{
    // 20 kHz at 44.1 kHz with Q 40, and 20 Hz at 192 kHz with Q 0.1: noise for a while, then silence.
    for (const auto& [hz, q, rate] : { std::tuple { 20000.0, 40.0, 44100.0 }, std::tuple { 20.0, 0.1, 192000.0 },
                                       std::tuple { 20.0, 40.0, 192000.0 }, std::tuple { 20000.0, 0.1, 44100.0 } })
    {
        const auto c = lowPass (hz, q, rate);
        double ic1 = 0.0, ic2 = 0.0;
        unsigned seed = 1;

        for (int n = 0; n < 200000; ++n)
        {
            seed = seed * 1664525u + 1013904223u;
            const auto x = n < 100000 ? static_cast<double> (seed >> 8) / 8388608.0 - 1.0 : 0.0;
            const auto y = svfTick (x, ic1, ic2, c);
            REQUIRE (std::isfinite (y));
            REQUIRE (std::abs (y) < 1.0e3);
        }
    }
}

TEST_CASE ("svf: the identity mix is recognised exactly, and nothing else is")
{
    CHECK (isIdentity (makeSvfCoefficients (0.1, 1.0, 1.0, 0.0, 0.0)));
    CHECK (isIdentity (makeSvfCoefficients (0.1, 1.0, 1.0, -0.0, -0.0)));
    CHECK_FALSE (isIdentity (makeSvfCoefficients (0.1, 1.0, 1.0, 1.0e-300, 0.0)));
    CHECK_FALSE (isIdentity (makeSvfCoefficients (0.1, 1.0, 1.0, 0.0, 1.0e-300)));
    CHECK_FALSE (isIdentity (makeSvfCoefficients (0.1, 1.0, 0.9999999999999999, 0.0, 0.0)));
}
