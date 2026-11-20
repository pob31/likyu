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
    The warped frequency without a library call (PRD, Filter core; acceptance
    row "tan approximation": fc from 20 Hz to 20 kHz at every rate, under
    1 cent). The error is the one a listener hears: the frequency the filter
    actually sits at, fs / pi * atan(g), against the one asked for.
*/

#include <doctest/doctest.h>

#include <dsp/Tan.h>

#include <algorithm>
#include <cmath>

using namespace likyu::dsp;

namespace
{
    constexpr double ratesInThePrd[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };

    /** The frequency g really gives, in cents from the one asked for. */
    double centsOff (double g, double hz, double rate)
    {
        const auto actual = rate / pi * std::atan (g);
        return 1200.0 * std::log2 (actual / hz);
    }
}

TEST_CASE ("tan: under 1 cent from 20 Hz to 20 kHz at every rate the PRD supports")
{
    constexpr int steps = 4000;

    for (const auto rate : ratesInThePrd)
    {
        Warp warp;
        warp.prepare (rate);

        double worst = 0.0;

        for (int i = 0; i <= steps; ++i)
        {
            const auto hz = minFrequency * std::pow (maxFrequency / minFrequency, i / double (steps));
            worst = std::max (worst, std::abs (centsOff (warp.g (hz), hz, rate)));
        }

        INFO ("rate " << rate << ", worst " << worst << " cent");
        CHECK (worst < 1.0);

        // Far inside the threshold: a regression in a coefficient shows here first.
        CHECK (worst < 0.01);
    }
}

TEST_CASE ("tan: the polynomial from 88.2 kHz up, the rational form below")
{
    CHECK (tanFormFor (44100.0) == TanForm::rational);
    CHECK (tanFormFor (48000.0) == TanForm::rational);
    CHECK (tanFormFor (88200.0) == TanForm::polynomial);
    CHECK (tanFormFor (96000.0) == TanForm::polynomial);
    CHECK (tanFormFor (192000.0) == TanForm::polynomial);

    // The polynomial is only ever asked for arguments under 0.25 pi.
    for (const auto rate : ratesInThePrd)
        if (tanFormFor (rate) == TanForm::polynomial)
            CHECK (pi * maxFrequency / rate < 0.25 * pi);
}

TEST_CASE ("tan: each form against the library over its own domain")
{
    for (int i = 1; i <= 2000; ++i)
    {
        const auto x = maxWarpArgument * pi * i / 2000.0;
        INFO ("x = " << x);
        CHECK (std::abs (tanRational (x) / std::tan (x) - 1.0) < 1.0e-8);
    }

    for (int i = 1; i <= 2000; ++i)
    {
        const auto x = 0.25 * pi * i / 2000.0;
        INFO ("x = " << x);
        CHECK (std::abs (tanPolynomial (x) / std::tan (x) - 1.0) < 1.0e-6);
    }

    // Odd functions, exact at 0.
    CHECK (tanRational (0.0) == 0.0);
    CHECK (tanPolynomial (0.0) == 0.0);
}

TEST_CASE ("tan: frequencies are clamped to 20 Hz and to min(20 kHz, 0.46 fs), at any rate a host may choose")
{
    for (const auto rate : { 22050.0, 32000.0, 44100.0, 384000.0 })
    {
        Warp warp;
        warp.prepare (rate);

        INFO ("rate " << rate);
        CHECK (warp.getHighestFrequency() == doctest::Approx (std::min (maxFrequency, maxWarpArgument * rate)));
        CHECK (warp.clamp (5.0) == minFrequency);
        CHECK (warp.clamp (1.0e6) == warp.getHighestFrequency());

        // Finite, positive and rising over the whole parameter range and beyond it.
        double last = 0.0;

        for (int i = 0; i <= 400; ++i)
        {
            const auto hz = 10.0 * std::pow (3000.0, i / 400.0);   // 10 Hz to 30 kHz
            const auto g = warp.g (hz);
            REQUIRE (std::isfinite (g));
            REQUIRE (g > 0.0);
            REQUIRE (g >= last);
            last = g;
        }
    }

    // Unprepared: no rate, no movement, nothing undefined.
    Warp unprepared;
    CHECK (unprepared.g (1000.0) == 0.0);
}
