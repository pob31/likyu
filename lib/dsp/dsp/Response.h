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
    THE RESPONSE OF A BAND, FOR A PICTURE (PRD, Editor: the static response
    as a line, the live response as a second line). Not used by the audio
    path, and never on the audio thread's account.

    EXACT FOR THE FILTER AS BUILT. The trapezoidal SVF (dsp/Svf.h) is the
    analog one with every integrator replaced by its bilinear transform, so
    its response at f is the analog response at

        s = j tan(pi f / fs) / g

    where g is the filter's own warped frequency. With D = s^2 + k s + 1,
    the band-pass state v1 is s / D and the low-pass v2 is 1 / D, and

        H = m0 + m1 s / D + m2 / D.

    g and k are read back from the coefficients (a2 = g a1,
    a1 = 1 / (1 + g (g + k))), so whatever designBand did to them, a shelf's
    rescaled g included, is in the picture, and so is the library's own tan
    for the band's frequency. tests/dsp/ResponseTests.cpp holds it to the
    Fourier transform of the impulse response.

    Standard library only.
*/

#include "BandCoefficients.h"
#include "Gain.h"
#include "Math.h"
#include "Parameters.h"
#include "Svf.h"
#include "Tan.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace likyu::dsp
{
    /** The lowest magnitude the picture's functions return, in dB: a notch's
        centre is -infinity otherwise. */
    inline constexpr double responseFloorDb = -200.0;

    /** H at `hz` of a filter with coefficients `c` running at `rate`. A
        frequency at or above Nyquist is read just under it. */
    inline std::complex<double> svfResponse (const SvfCoefficients<double>& c, double hz, double rate) noexcept
    {
        if (! (rate > 0.0) || ! (c.a1 > 0.0) || ! (c.a2 > 0.0))
            return { c.m0, 0.0 };

        const auto g = c.a2 / c.a1;
        const auto k = (1.0 / c.a1 - 1.0) / g - g;

        const auto x = pi * std::clamp (hz / rate, 0.0, 0.4999);
        const std::complex<double> s (0.0, std::tan (x) / g);
        const auto d = s * s + k * s + 1.0;

        return c.m0 + (c.m1 * s + c.m2) / d;
    }

    /** |H| in dB, not below responseFloorDb. */
    inline double svfMagnitudeDb (const SvfCoefficients<double>& c, double hz, double rate) noexcept
    {
        const auto magnitude = std::abs (svfResponse (c, hz, rate));
        const auto floor = std::exp2 (responseFloorDb * (log2Of10 / 20.0));

        return magnitude > floor ? 20.0 * std::log10 (magnitude) : responseFloorDb;
    }

    /** A static band's coefficients as the chain builds them at rest: the
        frequency through `warp`, Q and gain clamped to the table's ranges, the
        gain ignored for a type that has none. */
    inline SvfCoefficients<double> staticBandCoefficients (BandType type, QMode qmode, double hz, double q,
                                                           double gainDb, const Warp& warp) noexcept
    {
        const auto db = hasGain (type) ? std::clamp (gainDb, -maxGainDb, maxGainDb) : 0.0;
        const auto shelf = type == BandType::lowShelf || type == BandType::highShelf;

        return designBand (type, qmode, warp.g (hz), 1.0 / std::clamp (q, minQ, maxQ),
                           hasGain (type) ? bandA (db) : 1.0,
                           shelf ? bandSqrtA (db) : 1.0);
    }
}
