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
    pow2, lib/dsp's own 2^x (dsp/Exp2.h; devplan, Phase 7): what its head
    promises. The reference is long double's exp2 where long double is wider
    than double, and the C library's double exp2 where it is not (MSVC), whose
    own error is then part of what is measured.
*/

#include <doctest/doctest.h>

#include <dsp/Exp2.h>
#include <dsp/Gain.h>

#include <cmath>
#include <cstdint>
#include <limits>

using namespace likyu::dsp;

namespace
{
    /** |pow2 (x) - 2^x| in units of the last place of the result. */
    double errorInUlps (double x)
    {
        const auto got = pow2 (x);

        if (std::numeric_limits<long double>::digits > std::numeric_limits<double>::digits)
        {
            const auto truth = std::exp2 (static_cast<long double> (x));
            const auto ulp = static_cast<long double> (std::nextafter (got, std::numeric_limits<double>::infinity()) - got);
            return static_cast<double> (std::abs (static_cast<long double> (got) - truth) / ulp);
        }

        const auto truth = std::exp2 (x);
        const auto ulp = std::nextafter (truth, std::numeric_limits<double>::infinity()) - truth;
        return std::abs (got - truth) / ulp;
    }
}

TEST_CASE ("pow2: exactly 1 at 0 and at -0, and exactly 2^n for every whole n it takes")
{
    CHECK (exactlyEqual (pow2 (0.0), 1.0));
    CHECK (exactlyEqual (pow2 (-0.0), 1.0));

    auto wrong = 0;

    for (int n = -1000; n <= 1000; ++n)
        wrong += exactlyEqual (pow2 (static_cast<double> (n)), std::ldexp (1.0, n)) ? 0 : 1;

    CHECK (wrong == 0);
}

TEST_CASE ("pow2: within 2 units in the last place, over the gains, the frequencies and beyond")
{
    // A band's A is 2^x for x within +-2.5, a frequency for x from 4.3 to 14.3.
    auto worst = 0.0, worstAt = 0.0;
    std::uint32_t state = 12345;

    const auto sweep = [&] (double from, double to, int points)
    {
        for (int i = 0; i < points; ++i)
        {
            state = state * 1664525u + 1013904223u;
            const auto x = from + (to - from) * (static_cast<double> (state >> 8) / 16777216.0);
            const auto error = errorInUlps (x);

            if (error > worst)
            {
                worst = error;
                worstAt = x;
            }
        }
    };

    sweep (-2.5, 2.5, 400000);
    sweep (4.0, 15.0, 400000);
    sweep (-60.0, 60.0, 200000);

    // Around every boundary between two constants of the table.
    for (int k = -64 * 3; k <= 64 * 3; ++k)
        for (int step = -8; step <= 8; ++step)
        {
            const auto x = std::nextafter ((static_cast<double> (k) + 0.5) / 64.0, step < 0 ? -1.0e9 : 1.0e9)
                           + static_cast<double> (step) * 1.0e-15;
            worst = std::max (worst, errorInUlps (x));
        }

    MESSAGE ("pow2: worst error " << worst << " units in the last place, at x = " << worstAt);
    CHECK (worst < 2.0);
}

TEST_CASE ("pow2: outside what it takes it holds at the limit; not a number in, not a number out")
{
    CHECK (exactlyEqual (pow2 (5000.0), std::ldexp (1.0, 1000)));
    CHECK (exactlyEqual (pow2 (-5000.0), std::ldexp (1.0, -1000)));
    CHECK (exactlyEqual (pow2 (std::numeric_limits<double>::infinity()), std::ldexp (1.0, 1000)));
    CHECK (std::isnan (pow2 (std::numeric_limits<double>::quiet_NaN())));
}

TEST_CASE ("pow2: the gain conversions are made of it, and are exactly 1 at 0 dB")
{
    CHECK (exactlyEqual (bandA (0.0), 1.0));
    CHECK (exactlyEqual (bandSqrtA (0.0), 1.0));
    CHECK (exactlyEqual (dbToFactor (0.0), 1.0));
    CHECK (bandA (40.0) == doctest::Approx (10.0).epsilon (1.0e-15));
    CHECK (bandA (-40.0) == doctest::Approx (0.1).epsilon (1.0e-15));
    CHECK (dbToFactor (-6.0) == doctest::Approx (std::pow (10.0, -6.0 / 20.0)).epsilon (1.0e-15));
}
