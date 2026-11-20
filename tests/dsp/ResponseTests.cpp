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
    The band types against their analog transfer functions (PRD, Filter core;
    acceptance rows "static response" and "gain limit", and the half of
    "exact bypass" that is the coefficients' own).

    HOW THE RESPONSE IS MEASURED. A static band is linear and time-invariant,
    so its magnitude at a frequency is that of its impulse response's Fourier
    transform there: the answer a swept sine gives once settled, without the
    settling time, which keeps the Debug suite quick. The impulse is run
    through the band as the chain will build it (Warp, Gain.h, designBand,
    svfTick), until its state is below 1e-18. A stepped sine at three
    frequencies confirms that the two methods agree.

    THE REFERENCE is each type's analog transfer function, written out here
    on its own (they are RBJ's analog prototypes wherever Simper's table and
    those coincide: the proportional bell is RBJ's peaking EQ, the shelves
    are RBJ's shelves), evaluated in double at s = j tan(pi f / fs) /
    tan(pi fc / fs): the bilinear transform prewarped at fc, with the
    library's tan. Within 0.1 dB up to 0.4 fs, at six rates.
*/

#include <doctest/doctest.h>

#include <dsp/BandCoefficients.h>
#include <dsp/Gain.h>
#include <dsp/Svf.h>
#include <dsp/Tan.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <string>
#include <vector>

using namespace likyu::dsp;

namespace
{
    constexpr double ratesInThePrd[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };

    struct Setting
    {
        BandType type;
        QMode qmode;
        double hz, q, gainDb;
    };

    /*  std::string: doctest prints a bare char pointer as an address. */
    std::string nameOf (BandType type)
    {
        switch (type)
        {
            case BandType::bell:        return "bell";
            case BandType::lowShelf:    return "low shelf";
            case BandType::highShelf:   return "high shelf";
            case BandType::highPass:    return "high-pass";
            case BandType::lowPass:     return "low-pass";
            case BandType::notch:       return "notch";
            case BandType::bandPass:    return "band-pass";
        }

        return "?";
    }

    /** The coefficients as the chain builds them. */
    SvfCoefficients<double> coefficientsFor (const Setting& s, const Warp& warp)
    {
        const auto a = hasGain (s.type) ? bandA (s.gainDb) : 1.0;
        const auto sqrtA = hasGain (s.type) ? bandSqrtA (s.gainDb) : 1.0;
        return designBand (s.type, s.qmode, warp.g (s.hz), 1.0 / s.q, a, sqrtA);
    }

    /** The impulse response, run until the state has died away. */
    std::vector<double> impulseResponse (const SvfCoefficients<double>& c)
    {
        std::vector<double> h;
        double ic1 = 0.0, ic2 = 0.0;

        for (int n = 0; n < (1 << 22); ++n)
        {
            h.push_back (svfTick (n == 0 ? 1.0 : 0.0, ic1, ic2, c));

            if (n > 64 && std::abs (ic1) < 1.0e-18 && std::abs (ic2) < 1.0e-18)
                break;
        }

        return h;
    }

    /** |H| at hz, from the impulse response. The phasor is recomputed every
        1024 samples so that its rounding cannot accumulate. */
    double magnitudeAt (const std::vector<double>& h, double hz, double rate)
    {
        const auto w = 2.0 * pi * hz / rate;
        const auto stepC = std::cos (w), stepS = std::sin (w);
        double re = 0.0, im = 0.0, c = 1.0, s = 0.0;

        for (std::size_t n = 0; n < h.size(); ++n)
        {
            if ((n & 1023u) == 0)
            {
                c = std::cos (w * static_cast<double> (n));
                s = std::sin (w * static_cast<double> (n));
            }

            re += h[n] * c;
            im -= h[n] * s;

            const auto nextC = c * stepC - s * stepS;
            s = s * stepC + c * stepS;
            c = nextC;
        }

        return std::hypot (re, im);
    }

    /** The analog prototype, prewarped at fc. */
    double referenceMagnitude (const Setting& set, double hz, double rate)
    {
        const auto fc = std::clamp (set.hz, minFrequency, std::min (maxFrequency, maxWarpArgument * rate));
        const std::complex<double> s (0.0, std::tan (pi * hz / rate) / std::tan (pi * fc / rate));
        const auto q = set.q;
        const auto a = std::pow (10.0, set.gainDb / 40.0);
        const auto sqrtA = std::sqrt (a);
        const auto s2 = s * s;
        const auto denominator = s2 + s / q + 1.0;

        std::complex<double> h;

        switch (set.type)
        {
            case BandType::bell:
                h = set.qmode == QMode::proportional ? (s2 + s * (a / q) + 1.0) / (s2 + s / (a * q) + 1.0)
                                                     : (s2 + s * (a * a / q) + 1.0) / denominator;
                break;

            case BandType::lowShelf:
                h = a * (s2 + s * (sqrtA / q) + a) / (a * s2 + s * (sqrtA / q) + 1.0);
                break;

            case BandType::highShelf:
                h = a * (a * s2 + s * (sqrtA / q) + 1.0) / (s2 + s * (sqrtA / q) + a);
                break;

            case BandType::highPass:    h = s2 / denominator; break;
            case BandType::lowPass:     h = 1.0 / denominator; break;
            case BandType::notch:       h = (s2 + 1.0) / denominator; break;
            case BandType::bandPass:    h = (s / q) / denominator; break;
        }

        return std::abs (h);
    }

    double dB (double magnitude)
    {
        return 20.0 * std::log10 (magnitude);
    }

    /** Worst error in dB from 20 Hz to 0.4 fs, where the reference is above -100 dB. */
    double worstErrorDb (const Setting& set, double rate)
    {
        Warp warp;
        warp.prepare (rate);

        const auto h = impulseResponse (coefficientsFor (set, warp));
        const auto top = 0.4 * rate;

        double worst = 0.0;
        constexpr int points = 48;

        for (int i = 0; i <= points; ++i)
        {
            const auto hz = 20.0 * std::pow (top / 20.0, i / double (points));
            const auto expected = referenceMagnitude (set, hz, rate);

            if (expected < 1.0e-5)
                continue;

            worst = std::max (worst, std::abs (dB (magnitudeAt (h, hz, rate)) - dB (expected)));
        }

        // And at the band's own frequency, where a bell or a notch is at its extreme.
        const auto atCentre = referenceMagnitude (set, set.hz, rate);

        if (set.hz < top && atCentre >= 1.0e-5)
            worst = std::max (worst, std::abs (dB (magnitudeAt (h, set.hz, rate)) - dB (atCentre)));

        return worst;
    }

    std::vector<Setting> settingsFor (double rate)
    {
        std::vector<Setting> settings;
        const auto high = std::min (15000.0, 0.3 * rate);

        for (int t = 0; t < numBandTypes; ++t)
        {
            const auto type = static_cast<BandType> (t);

            for (int m = 0; m < (type == BandType::bell ? numQModes : 1); ++m)
            {
                const auto qmode = static_cast<QMode> (m);
                settings.push_back ({ type, qmode, 60.0, 0.7, 9.0 });
                settings.push_back ({ type, qmode, 1000.0, 4.0, -12.0 });
                settings.push_back ({ type, qmode, high, 1.5, 6.0 });
            }
        }

        return settings;
    }
}

//==============================================================================
TEST_CASE ("gain: 0 dB is exactly a factor of 1, and the conversions agree with pow elsewhere")
{
    CHECK (bandA (0.0) == 1.0);
    CHECK (bandA (-0.0) == 1.0);
    CHECK (bandSqrtA (0.0) == 1.0);
    CHECK (dbToFactor (0.0) == 1.0);

    for (int i = -300; i <= 300; ++i)
    {
        const auto db = i / 10.0;
        INFO ("dB " << db);
        CHECK (bandA (db) == doctest::Approx (std::pow (10.0, db / 40.0)).epsilon (1.0e-14));
        CHECK (bandSqrtA (db) == doctest::Approx (std::pow (10.0, db / 80.0)).epsilon (1.0e-14));
        CHECK (dbToFactor (db) == doctest::Approx (std::pow (10.0, db / 20.0)).epsilon (1.0e-14));
    }
}

TEST_CASE ("gain limit: the effective gain never leaves -30 to +30 dB")
{
    // The row: static gain and Range at their extremes, same sign.
    CHECK (effectiveGainDb (30.0, 30.0, 30.0) == 30.0);
    CHECK (effectiveGainDb (-30.0, -30.0, -30.0) == -30.0);

    // Every combination on a grid, offsets beyond the Range included.
    for (int s = -30; s <= 30; s += 5)
        for (int o = -60; o <= 60; o += 5)
            for (int r = -30; r <= 30; r += 5)
            {
                const auto e = effectiveGainDb (s, o, r);
                REQUIRE (e >= -maxGainDb);
                REQUIRE (e <= maxGainDb);

                // The offset never exceeds the Range's magnitude.
                REQUIRE (std::abs (e - std::clamp (static_cast<double> (s), -30.0, 30.0)) <= std::abs (r) + 1.0e-12);
            }

    // No offset, no change: the static gain itself, exactly; and 0 stays exactly 0.
    CHECK (effectiveGainDb (-4.5, 0.0, -6.0) == -4.5);
    CHECK (effectiveGainDb (0.0, 0.0, 0.0) == 0.0);
}

TEST_CASE ("exact bypass, coefficients: a bell or a shelf at 0 dB is exactly the identity mix")
{
    for (const auto type : { BandType::bell, BandType::lowShelf, BandType::highShelf })
        for (const auto qmode : { QMode::proportional, QMode::constant })
            for (const auto g : { 1.0e-4, 0.01, 0.3, 1.0, 6.3 })
                for (const auto q : { 0.1, 0.707, 1.0, 13.0, 40.0 })
                {
                    INFO (nameOf (type) << ", g " << g << ", Q " << q);
                    CHECK (isIdentity (designBand (type, qmode, g, 1.0 / q, bandA (0.0), bandSqrtA (0.0))));
                }

    // The other types are never the identity, whatever their (ignored) gain.
    for (const auto type : { BandType::highPass, BandType::lowPass, BandType::notch, BandType::bandPass })
        CHECK_FALSE (isIdentity (designBand (type, QMode::proportional, 0.1, 1.0, 1.0, 1.0)));
}

TEST_CASE ("static response: every type within 0.1 dB of its warped analog response up to 0.4 fs, at six rates")
{
    for (const auto rate : ratesInThePrd)
    {
        for (const auto& set : settingsFor (rate))
        {
            const auto worst = worstErrorDb (set, rate);

            INFO ("rate " << rate << ", " << nameOf (set.type)
                  << std::string (set.type == BandType::bell ? (set.qmode == QMode::proportional ? " (proportional)" : " (constant Q)") : "")
                  << ", " << set.hz << " Hz, Q " << set.q << ", " << set.gainDb << " dB: worst " << worst << " dB");
            CHECK (worst < 0.1);

            // Far inside the row's threshold: only the tan approximation and rounding remain.
            CHECK (worst < 0.001);
        }
    }
}

TEST_CASE ("static response: Q 40 and the extremes of gain, at 48 kHz")
{
    for (const auto type : { BandType::bell, BandType::lowShelf, BandType::highShelf, BandType::notch, BandType::bandPass })
        for (const auto qmode : { QMode::proportional, QMode::constant })
            for (const auto gain : { -30.0, 30.0 })
            {
                const Setting set { type, qmode, 2000.0, 40.0, gain };
                const auto worst = worstErrorDb (set, 48000.0);

                INFO (nameOf (type) << ", " << gain << " dB: worst " << worst << " dB");
                CHECK (worst < 0.1);
            }
}

TEST_CASE ("static response: a stepped sine reads what the impulse response says")
{
    constexpr double rate = 48000.0;
    const Setting set { BandType::bell, QMode::proportional, 1000.0, 4.0, -12.0 };

    Warp warp;
    warp.prepare (rate);
    const auto c = coefficientsFor (set, warp);
    const auto h = impulseResponse (c);

    // Whole periods: fs / 120, fs / 48, fs / 16.
    for (const int period : { 120, 48, 16 })
    {
        const auto hz = rate / period;
        double ic1 = 0.0, ic2 = 0.0, re = 0.0, im = 0.0;
        const int settle = 48000, measured = period * 200;

        for (int n = 0; n < settle + measured; ++n)
        {
            const auto phase = 2.0 * pi * (n % period) / period;
            const auto y = svfTick (std::sin (phase), ic1, ic2, c);

            if (n >= settle)
            {
                re += y * std::sin (phase);
                im += y * std::cos (phase);
            }
        }

        const auto stepped = 2.0 * std::hypot (re, im) / measured;

        INFO (hz << " Hz");
        CHECK (std::abs (dB (stepped) - dB (magnitudeAt (h, hz, rate))) < 0.001);
    }
}
