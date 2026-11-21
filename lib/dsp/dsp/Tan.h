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
    THE WARPED FREQUENCY, g = tan(pi fc / fs), WITHOUT A LIBRARY CALL (PRD,
    Filter core: tan by polynomial, decided 2026-11-16; acceptance row "tan
    approximation": under 1 cent from 20 Hz to 20 kHz at every rate).

    Two forms, one per sample rate, chosen in prepare():

      - fs >= 88.2 kHz: an odd polynomial. 20 kHz is then at most 0.227 pi,
        inside the 0.25 pi the PRD allows a polynomial.
      - below: a rational form, odd polynomial over even polynomial, one
        divide, because at 44.1 kHz 20 kHz is 0.4535 pi, near the pole.

    Every frequency is clamped to [20 Hz, min(20 kHz, 0.46 fs)] first. At
    44.1 kHz and above that changes nothing; at a rate a host may choose
    outside the PRD's 44.1 to 192 kHz (22.05, 32 kHz) it keeps the argument
    inside the rational form's domain, [0, 0.46 pi].

    Measured when written (2026-11-20, double): the rational form is within
    1e-6 cent of tan over [0, 0.46 pi]; the polynomial within 4e-4 cent over
    [0, 0.25 pi]. tests/dsp/TanTests.cpp holds both to the PRD's 1 cent at
    every rate.

    Standard library only.
*/

#include "Math.h"
#include "Parameters.h"

#include <algorithm>

namespace likyu::dsp
{
    /** The highest argument of tan the filters use, as a fraction of pi. */
    inline constexpr double maxWarpArgument = 0.46;

    /** From this rate up, 20 kHz stays under 0.25 pi and the polynomial is used. */
    inline constexpr double polynomialMinRate = 88200.0;

    enum class TanForm { rational, polynomial };

    /*  Lambert's continued fraction for tan, cut at depth 6 and expanded:
        x (135135 - 17325 x^2 + 378 x^4 - x^6) / (135135 - 62370 x^2 + 3150 x^4 - 28 x^6).
        Integer coefficients, exact in any precision. */
    template <typename T>
    inline T tanRational (T x) noexcept
    {
        const T y = x * x;
        const T p = mulAdd (y, mulAdd (y, mulAdd (y, T (-1), T (378)), T (-17325)), T (135135));
        const T q = mulAdd (y, mulAdd (y, mulAdd (y, T (-28), T (3150)), T (-62370)), T (135135));
        return x * p / q;
    }

    /*  An odd polynomial of degree 11, fitted on relative error over
        [0, 0.25 pi] (Lawson's iteration, 2026-11-20). Wrong outside it: used
        only where prepare() has checked the rate. */
    template <typename T>
    inline T tanPolynomial (T x) noexcept
    {
        // Horner's rule, the highest power first, each step one mulAdd.
        const T y = x * x;
        T p = T (0.020387002666730214);
        p = mulAdd (y, p, T (0.012532760831065859));
        p = mulAdd (y, p, T (0.057182138867633396));
        p = mulAdd (y, p, T (0.1328495807781988));
        p = mulAdd (y, p, T (0.3333594742443005));
        p = mulAdd (y, p, T (0.9999997745472344));
        return x * p;
    }

    constexpr TanForm tanFormFor (double sampleRate) noexcept
    {
        return sampleRate >= polynomialMinRate ? TanForm::polynomial : TanForm::rational;
    }

    /** g for a frequency, at the rate given to prepare(). Plain data: no allocation. */
    class Warp
    {
    public:
        void prepare (double sampleRate) noexcept
        {
            rate = sampleRate > 0.0 ? sampleRate : 0.0;
            piOverRate = rate > 0.0 ? pi / rate : 0.0;
            highest = rate > 0.0 ? std::min (maxFrequency, maxWarpArgument * rate) : maxFrequency;
            form = tanFormFor (rate);
        }

        /** Into [20 Hz, min(20 kHz, 0.46 fs)]. */
        double clamp (double hz) const noexcept
        {
            return std::clamp (hz, minFrequency, highest);
        }

        /** tan(pi fc / fs) by the form the rate calls for, fc clamped. */
        double g (double hz) const noexcept
        {
            const auto x = piOverRate * clamp (hz);
            return form == TanForm::polynomial ? tanPolynomial (x) : tanRational (x);
        }

        double getSampleRate() const noexcept       { return rate; }
        double getHighestFrequency() const noexcept { return highest; }
        TanForm getForm() const noexcept            { return form; }

    private:
        double rate = 0.0;
        double piOverRate = 0.0;
        double highest = maxFrequency;
        TanForm form = TanForm::rational;
    };
}
