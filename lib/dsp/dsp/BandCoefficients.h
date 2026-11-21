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
    THE BAND TYPES (PRD, Filter core): one SVF, the type is in how g and k
    are scaled and in the mix. With A = 10^(gain / 40):

        Type                    g          k          m0, m1, m2
        Bell, proportional Q    g          1 / (Q A)  1, k (A^2 - 1), 0
        Bell, constant Q        g          1 / Q      1, k (A^2 - 1), 0
        Low shelf               g / sqrtA  1 / Q      1, k (A - 1), A^2 - 1
        High shelf              g sqrtA    1 / Q      A^2, k (1 - A) A, 1 - A^2
        High-pass               g          1 / Q      1, -k, -1
        Low-pass                g          1 / Q      0, 0, 1
        Band-pass (unity peak)  g          1 / Q      0, k, 0
        Notch                   g          1 / Q      1, -k, 0

    EXACT BYPASS. At A = 1 a bell or a shelf gives exactly (1, 0, 0): the
    products above are exact when A is exactly 1, which Gain.h guarantees at
    0 dB.

    The enumerations follow the parameter table's choices, in order
    (dsp/Parameters.cpp): their values are the parameters' values.

    Standard library only.
*/

#include "Svf.h"

namespace likyu::dsp
{
    enum class BandType { bell, lowShelf, highShelf, highPass, lowPass, notch, bandPass };
    inline constexpr int numBandTypes = 7;

    enum class QMode { proportional, constant };
    inline constexpr int numQModes = 2;

    /** Bells and shelves have a gain; the other types ignore it (PRD, non-goals). */
    constexpr bool hasGain (BandType type) noexcept
    {
        return type == BandType::bell || type == BandType::lowShelf || type == BandType::highShelf;
    }

    /** One row of the table. g = tan(pi fc / fs), unscaled; kq = 1 / Q;
        a = A and sqrtA = sqrt(A), both 1 for a type without gain. */
    template <typename T>
    inline SvfCoefficients<T> designBand (BandType type, QMode qmode, T g, T kq, T a, T sqrtA) noexcept
    {
        // A^2 - 1 and 1 - A^2 in one rounding (mulAdd), and exactly 0 at A = 1 either way.
        const T aboveUnity = mulAdd (a, a, T (-1));
        const T belowUnity = mulAdd (-a, a, T (1));

        switch (type)
        {
            case BandType::bell:
            {
                const T k = qmode == QMode::proportional ? kq / a : kq;
                return makeSvfCoefficients (g, k, T (1), k * aboveUnity, T (0));
            }

            case BandType::lowShelf:
                return makeSvfCoefficients (g / sqrtA, kq, T (1), kq * (a - T (1)), aboveUnity);

            case BandType::highShelf:
                return makeSvfCoefficients (g * sqrtA, kq, a * a, kq * (T (1) - a) * a, belowUnity);

            case BandType::highPass:
                return makeSvfCoefficients (g, kq, T (1), -kq, T (-1));

            case BandType::lowPass:
                return makeSvfCoefficients (g, kq, T (0), T (0), T (1));

            case BandType::notch:
                return makeSvfCoefficients (g, kq, T (1), -kq, T (0));

            case BandType::bandPass:
                return makeSvfCoefficients (g, kq, T (0), kq, T (0));
        }

        return makeSvfCoefficients (g, kq, T (1), T (0), T (0));
    }
}
