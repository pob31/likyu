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
    The detector and the level (PRD, Detection). Acceptance rows: quadrature
    envelope, detector rejection; the fast log's 0.05 dB.

    THE TRUTH for a band-pass detector is the unity-peak band-pass magnitude
    of the TPT filter at the tone: the analog k s / (s^2 + k s + 1) at
    s = j tan(pi f / fs) / g, the bilinear transform prewarped at the
    detector's frequency, with the library's g. A level is read raw, sample by
    sample, before any gain computer: its mean in dB over the second half of
    a second, and its ripple, peak to peak.
*/

#include <doctest/doctest.h>

#include <dsp/Detector.h>
#include <dsp/FastLog.h>
#include <dsp/Math.h>
#include <dsp/Tan.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace likyu::dsp;

namespace
{
    /** The unity-peak band-pass magnitude of the TPT SVF at `hz`, in dB. */
    double bandPassDb (double hz, double centre, double q, double rate)
    {
        Warp warp;
        warp.prepare (rate);
        const auto omega = std::tan (pi * hz / rate) / warp.g (centre);
        const auto k = 1.0 / q;
        const auto magnitude = k * omega / std::sqrt ((1.0 - omega * omega) * (1.0 - omega * omega) + k * k * omega * omega);
        return 20.0 * std::log10 (magnitude);
    }

    struct Reading
    {
        double meanDb = 0.0;
        double rippleDb = 0.0;
    };

    /** A steady tone through one detector for `seconds`; the level over the second half. */
    Reading readTone (DetectorType type, double centre, double q, double toneHz, double amplitude,
                      double rate, double seconds = 1.0)
    {
        Warp warp;
        warp.prepare (rate);
        const auto c = makeDetectorCoefficients (type, warp.g (centre), 1.0 / q, centre, rate);

        float s1 = 0.0f, s2 = 0.0f, peak = 0.0f, out = 0.0f;
        const auto length = static_cast<int> (seconds * rate);
        double sum = 0.0, lowest = 1.0e9, highest = -1.0e9;
        int counted = 0;

        for (int n = 0; n < length; ++n)
        {
            const auto x = static_cast<float> (amplitude * std::sin (2.0 * pi * toneHz * n / rate));
            const auto level = static_cast<double> (powerToDb (detectorTick (x, s1, s2, peak, c, out)));

            if (n >= length / 2)
            {
                sum += level;
                lowest = std::min (lowest, level);
                highest = std::max (highest, level);
                ++counted;
            }
        }

        return { sum / counted, highest - lowest };
    }
}

//==============================================================================
TEST_CASE ("fast log: within 0.05 dB of 10 log10 from -120 to +60 dB, and never decreasing")
{
    double worst = 0.0;
    auto previous = -std::numeric_limits<float>::infinity();
    auto monotone = true;

    for (int step = 0; step <= 180000; ++step)
    {
        const auto db = -120.0 + 0.001 * step;
        const auto power = static_cast<float> (std::pow (10.0, db / 10.0));
        const auto fast = powerToDb (power);
        const auto exact = 10.0 * std::log10 (static_cast<double> (power));

        worst = std::max (worst, std::abs (static_cast<double> (fast) - exact));
        monotone = monotone && fast >= previous;
        previous = fast;
    }

    MESSAGE ("fast log: worst error " << worst << " dB");
    CHECK (worst < 0.05);
    CHECK (monotone);

    // At an exact power of two the cubic adds nothing: 1 is exactly 0 dB.
    CHECK (powerToDb (1.0f) == 0.0f);
}

TEST_CASE ("fast log: under the floor, NaN, infinity and the ceiling")
{
    constexpr auto nan = std::numeric_limits<float>::quiet_NaN();
    constexpr auto inf = std::numeric_limits<float>::infinity();

    for (const auto p : { 0.0f, -0.0f, -1.0f, 1.0e-13f, std::numeric_limits<float>::denorm_min(), nan, -inf })
        CHECK (powerToDb (p) == levelFloorDb);

    for (const auto p : { 1.0e6f, 1.0e30f, inf })
        CHECK (powerToDb (p) == levelCeilingDb);

    CHECK (powerToDb (powerFloor) >= levelFloorDb);
    CHECK (powerToDb (powerFloor) < levelFloorDb + 0.01f);
}

//==============================================================================
TEST_CASE ("quadrature envelope: a tone at a 60 Hz centre and half an octave either side reads the band-pass magnitude, without ripple")
{
    double worstLevel = 0.0, worstRipple = 0.0;

    for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        for (const auto q : { 1.0, 8.0 })
        {
            for (const auto tone : { 60.0, 60.0 / std::sqrt (2.0), 60.0 * std::sqrt (2.0) })
            {
                const auto truth = bandPassDb (tone, 60.0, q, rate) + 20.0 * std::log10 (0.5);
                const auto r = readTone (DetectorType::bandPass, 60.0, q, tone, 0.5, rate, q > 4.0 ? 2.0 : 1.0);

                INFO (rate << " Hz, Q " << q << ", tone " << tone << " Hz: truth " << truth
                            << " dB, level " << r.meanDb << " dB, ripple " << r.rippleDb << " dB");
                CHECK (std::abs (r.meanDb - truth) < 0.1);
                CHECK (r.rippleDb < 0.1);

                worstLevel = std::max (worstLevel, std::abs (r.meanDb - truth));
                worstRipple = std::max (worstRipple, r.rippleDb);
            }
        }
    }

    MESSAGE ("quadrature envelope, float: worst level error " << worstLevel << " dB, worst ripple " << worstRipple << " dB");
}

TEST_CASE ("quadrature envelope: 20 Hz, Q 40 at 192 kHz in float, for information")
{
    const auto truth = bandPassDb (20.0, 20.0, 40.0, 192000.0) + 20.0 * std::log10 (0.5);
    const auto r = readTone (DetectorType::bandPass, 20.0, 40.0, 20.0, 0.5, 192000.0, 8.0);

    MESSAGE ("20 Hz, Q 40, 192 kHz, float: level " << r.meanDb << " dB against " << truth
                                                   << " dB, ripple " << r.rippleDb << " dB");
    CHECK (std::isfinite (r.meanDb));
}

TEST_CASE ("detector rejection: a band-pass at 3 kHz, Q 1, reads a 100 Hz tone at the band-pass response, not the low-pass")
{
    for (const auto rate : { 44100.0, 96000.0 })
    {
        const auto truth = bandPassDb (100.0, 3000.0, 1.0, rate);
        const auto r = readTone (DetectorType::bandPass, 3000.0, 1.0, 100.0, 1.0, rate);

        INFO (rate << " Hz: truth " << truth << " dB, level " << r.meanDb << " dB, ripple " << r.rippleDb << " dB");
        CHECK (truth == doctest::Approx (-29.6).epsilon (0.01));
        CHECK (std::abs (r.meanDb - truth) < 0.1);
        CHECK (r.rippleDb < 0.1);

        // The first draft's k sqrt(v1^2 + v2^2) let the low-pass through: about -5.7 dB here.
        CHECK (r.meanDb < -25.0);
    }
}

//==============================================================================
TEST_CASE ("peak follower: a high-pass or low-pass detector reads a tone's peak, and falls at four periods of its frequency")
{
    constexpr double rate = 48000.0;

    struct Case { DetectorType type; double centre, tone; std::string name; };

    for (const auto& c : { Case { DetectorType::highPass, 1000.0, 6000.0, "high-pass" },
                           Case { DetectorType::lowPass, 1000.0, 150.0, "low-pass" } })
    {
        Warp warp;
        warp.prepare (rate);
        const auto k = makeDetectorCoefficients (c.type, warp.g (c.centre), 1.0, c.centre, rate);

        float s1 = 0.0f, s2 = 0.0f, peak = 0.0f, out = 0.0f;
        const auto stop = static_cast<int> (0.5 * rate);
        std::vector<double> level;

        for (int n = 0; n < stop + static_cast<int> (0.05 * rate); ++n)
        {
            const auto x = n < stop ? static_cast<float> (0.5 * std::sin (2.0 * pi * c.tone * n / rate)) : 0.0f;
            level.push_back (static_cast<double> (powerToDb (detectorTick (x, s1, s2, peak, k, out))));
        }

        // In the pass band, the peak of a 0.5 sine is about -6 dB.
        double lowest = 1.0e9, highest = -1.0e9;

        for (int n = stop / 2; n < stop; ++n)
        {
            lowest = std::min (lowest, level[static_cast<std::size_t> (n)]);
            highest = std::max (highest, level[static_cast<std::size_t> (n)]);
        }

        INFO (c.name << ": " << lowest << " to " << highest << " dB while the tone plays");
        CHECK (highest == doctest::Approx (-6.0).epsilon (0.1));
        MESSAGE (c.name << " detector, tone at " << c.tone << " Hz: ripple " << highest - lowest << " dB");

        // After the tone, once the filter's own ringing is under the follower:
        // 10 log10(e) 2 / tau dB per second, tau = 4 periods.
        const auto from = stop + static_cast<int> (0.01 * rate), to = stop + static_cast<int> (0.03 * rate);
        const auto fell = level[static_cast<std::size_t> (from)] - level[static_cast<std::size_t> (to)];
        const auto expected = 10.0 * std::log10 (std::exp (1.0)) * 2.0 * (c.centre / followerPeriods) * (to - from) / rate;

        INFO (c.name << ": fell " << fell << " dB in 20 ms, expected " << expected);
        CHECK (fell == doctest::Approx (expected).epsilon (0.02));
    }
}
