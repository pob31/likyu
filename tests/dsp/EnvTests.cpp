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
    Env, the first frequency mode (PRD, Frequency modes; devplan 5a).
    Acceptance row: Env (a stepped-level tone between Sensitivity and
    Sensitivity + Span, Up and Down: the frequency follows the log mapping
    within 20 cents at steady state, attack and release within 10 %). Also:
    the one control step by which the band's filter follows its lane, the
    rest at the start of the mapping, a Follow detector on the moving band,
    the types without a gain, exact bypass at 0 dB, block-size invariance.
*/

#include <doctest/doctest.h>

#include "DspTestSupport.h"

#include <dsp/Chain.h>

#include <cmath>
#include <limits>
#include <string>

using namespace likyu::dsp;
using namespace likyutest::dsp;

namespace
{
    /** A tone at 1 kHz whose level steps: (level in dB of its peak, seconds). */
    std::vector<double> steppedTone (double rate, const std::vector<std::pair<double, double>>& segments, double hz = 1000.0)
    {
        std::vector<double> out;
        double phase = 0.0;

        for (const auto& [db, seconds] : segments)
        {
            const auto amplitude = std::pow (10.0, db / 20.0);
            const auto count = static_cast<int> (seconds * rate);

            for (int n = 0; n < count; ++n)
            {
                out.push_back (amplitude * std::sin (phase));
                phase += 2.0 * pi * hz / rate;
            }
        }

        return out;
    }

    /** A chain run one sample at a time, the band's frequency read after each. */
    struct Trace
    {
        std::vector<double> hz;         // what band `band`'s lane asks for, channel 0
        std::vector<double> applied;    // what its filter reaches at the step's end
        std::vector<std::vector<double>> out;
    };

    Trace trace (double rate, std::vector<std::vector<double>> audio, const std::vector<BandSettings>& settings, int band = 0,
                 int channel = 0)
    {
        Chain chain;
        const auto channels = static_cast<int> (audio.size());
        chain.prepare (rate, channels);

        for (std::size_t b = 0; b < settings.size(); ++b)
            chain.setBand (static_cast<int> (b), settings[b]);

        const auto length = audio.front().size();
        Trace t;
        t.hz.resize (length);
        t.applied.resize (length);
        std::vector<double*> pointers (audio.size());

        for (std::size_t n = 0; n < length; ++n)
        {
            for (std::size_t c = 0; c < audio.size(); ++c)
                pointers[c] = audio[c].data() + n;

            chain.process (pointers.data(), channels, 1);
            t.hz[n] = chain.frequencyHz (band, channel);
            t.applied[n] = chain.appliedFrequencyHz (band, channel);
        }

        t.out = std::move (audio);
        return t;
    }

    double cents (double hz, double reference)
    {
        return 1200.0 * std::log2 (hz / reference);
    }

    /** Where the log mapping puts a level: Low to High (Up) or High to Low. */
    double mapped (double levelDb, double lowHz, double highHz, double sensitivityDb, double spanDb, EnvDirection direction)
    {
        const auto u = std::clamp ((levelDb - sensitivityDb) / spanDb, 0.0, 1.0);
        const auto at = direction == EnvDirection::up ? u : 1.0 - u;
        return std::exp2 (std::log2 (lowHz) + at * (std::log2 (highHz) - std::log2 (lowHz)));
    }

    /** The first sample from `from` at which the log frequency has covered
        63 % of the way from `start` to `end`. */
    double timeConstantMs (const std::vector<double>& hz, std::size_t from, double start, double end, double rate)
    {
        const auto target = std::log2 (start) + (1.0 - std::exp (-1.0)) * (std::log2 (end) - std::log2 (start));
        const auto rising = end > start;

        for (auto n = from; n < hz.size(); ++n)
            if (rising ? std::log2 (hz[n]) >= target : std::log2 (hz[n]) <= target)
                return 1000.0 * static_cast<double> (n - from) / rate;

        return std::numeric_limits<double>::infinity();
    }

    /** Noise with -0 and denormals sprinkled through it. */
    template <typename Sample>
    std::vector<std::vector<Sample>> awkwardNoise (int length)
    {
        std::vector<std::vector<Sample>> audio { noise<Sample> (length, 21, 0.5), noise<Sample> (length, 22, 0.5) };

        for (auto& channel : audio)
        {
            for (std::size_t n = 0; n < channel.size(); n += 97)
                channel[n] = -Sample (0);

            for (std::size_t n = 31; n < channel.size(); n += 101)
                channel[n] = std::numeric_limits<Sample>::denorm_min() * Sample (n % 7 + 1);
        }

        return audio;
    }
}

//==============================================================================
TEST_CASE ("env: a stepped-level tone maps onto Low to High in log frequency, Up and Down: within 20 cents at steady state, attack and release within 10 %")
{
    constexpr double low = 250.0, high = 4000.0, sensitivity = -40.0, span = 30.0;
    constexpr double attackMs = 5.0, releaseMs = 120.0;

    for (const auto rate : { 44100.0, 96000.0 })
    {
        for (const auto direction : { EnvDirection::up, EnvDirection::down })
        {
            INFO ("rate " << rate << ", " << std::string (direction == EnvDirection::up ? "Up" : "Down"));

            const auto input = steppedTone (rate, { { -30.0, 1.2 }, { -15.0, 1.2 }, { -30.0, 1.2 } });
            const auto s = envBand (BandType::bell, 6.0, low, high, sensitivity, span, 1000.0, direction, attackMs, releaseMs);
            const auto t = trace (rate, { input }, { s });

            const auto at = [&t, rate] (double seconds) { return t.hz[static_cast<std::size_t> (seconds * rate)]; };
            const auto quiet = mapped (-30.0, low, high, sensitivity, span, direction);
            const auto loud = mapped (-15.0, low, high, sensitivity, span, direction);

            // Steady state, at the end of each segment.
            const auto e1 = cents (at (1.19), quiet), e2 = cents (at (2.39), loud), e3 = cents (at (3.59), quiet);
            MESSAGE ("rate " << rate << std::string (direction == EnvDirection::up ? " Up" : " Down") << ": steady error "
                     << e1 << ", " << e2 << ", " << e3 << " cents");
            CHECK (std::abs (e1) < 20.0);
            CHECK (std::abs (e2) < 20.0);
            CHECK (std::abs (e3) < 20.0);

            // Attack on the rise, release on the fall, read on the lane's frequency.
            const auto rise = static_cast<std::size_t> (1.2 * rate), fall = static_cast<std::size_t> (2.4 * rate);
            const auto attack = timeConstantMs (t.hz, rise, quiet, loud, rate);
            const auto release = timeConstantMs (t.hz, fall, loud, quiet, rate);
            MESSAGE ("  attack " << attack << " ms for " << attackMs << ", release " << release << " ms for " << releaseMs);
            CHECK (attack == doctest::Approx (attackMs).epsilon (0.10));
            CHECK (release == doctest::Approx (releaseMs).epsilon (0.10));
        }
    }
}

TEST_CASE ("env: the band's filter follows its lane one control step late, exactly")
{
    constexpr double rate = 48000.0;
    const auto input = steppedTone (rate, { { -60.0, 0.05 }, { -12.0, 0.1 }, { -30.0, 0.1 } });
    const auto t = trace (rate, { input }, { envBand (BandType::bell, 6.0, 200.0, 3000.0, -40.0, 30.0, 1000.0) });

    auto moved = false;

    constexpr auto step = static_cast<std::size_t> (controlStep);

    for (auto n = step; n < input.size(); ++n)
    {
        // What the filter reaches in step k is the lane's frequency at the end of step k - 1.
        const auto stepStart = n - n % step;
        REQUIRE (t.applied[n] == t.hz[stepStart - 1]);
        moved = moved || ! exactlyEqual (t.hz[n], t.hz[n - 1]);
    }

    CHECK (moved);
}

TEST_CASE ("env: under Sensitivity a band rests exactly at the start of its mapping, Low for Up and High for Down, and comes back there")
{
    constexpr double rate = 48000.0, low = 300.0, high = 2400.0;

    for (const auto direction : { EnvDirection::up, EnvDirection::down })
    {
        const auto rest = direction == EnvDirection::up ? low : high;
        const auto input = steppedTone (rate, { { -70.0, 0.3 }, { -20.0, 0.3 }, { -70.0, 1.5 } });
        const auto t = trace (rate, { input }, { envBand (BandType::bell, 6.0, low, high, -40.0, 30.0, 1000.0, direction) });

        const auto before = t.hz[static_cast<std::size_t> (0.29 * rate)];
        const auto during = t.hz[static_cast<std::size_t> (0.59 * rate)];
        const auto after = t.hz.back();

        CHECK (before == doctest::Approx (rest).epsilon (1.0e-12));
        CHECK (std::abs (cents (during, rest)) > 600.0);

        // Back to rest exactly, not a hair off: the same bits as before the note.
        CHECK (after == before);
    }
}

TEST_CASE ("env: with a Follow detector the band settles where the level it reads through itself maps it")
{
    /*  The detector follows the band (PRD, Detection), so the level is the
        tone through a unity band-pass at the band's own frequency: the band
        settles where mapping (L0 + |H(f)| in dB) = f. */
    constexpr double rate = 48000.0, low = 250.0, high = 4000.0, sensitivity = -40.0, span = 30.0, tone = 1000.0, q = 1.0;
    constexpr double levelDb = -10.0;

    auto s = envBand (BandType::bell, 3.0, low, high, sensitivity, span, 1000.0);
    s.dfilt = DetectorFilter::follow;
    s.q = q;

    const auto input = steppedTone (rate, { { levelDb, 2.0 } }, tone);
    const auto t = trace (rate, { input }, { s });

    // The unity band-pass of the TPT SVF on the unit circle (bilinear, exact).
    const auto magnitudeDb = [] (double hz, double centre)
    {
        const auto w = std::tan (pi * hz / rate) / std::tan (pi * centre / rate);
        const auto k = 1.0 / q;
        return 20.0 * std::log10 ((k * w) / std::sqrt ((1.0 - w * w) * (1.0 - w * w) + (k * w) * (k * w)));
    };

    // Bisection on log2 f for mapping (L(f)) - f = 0.
    auto a = std::log2 (low), b = std::log2 (high);
    const auto residual = [&] (double lf)
    {
        const auto f = std::exp2 (lf);
        return std::log2 (mapped (levelDb + magnitudeDb (tone, f), low, high, sensitivity, span, EnvDirection::up)) - lf;
    };

    for (int i = 0; i < 100; ++i)
    {
        const auto m = 0.5 * (a + b);
        (residual (a) * residual (m) <= 0.0 ? b : a) = m;
    }

    const auto expected = std::exp2 (0.5 * (a + b));
    const auto settled = t.hz.back();
    MESSAGE ("Follow on Env: settles at " << settled << " Hz, the fixed point is " << expected << " Hz");
    CHECK (std::abs (cents (settled, expected)) < 20.0);
}

TEST_CASE ("env: a high-pass, low-pass, band-pass and notch in Env move with the key; their gain mode stays Static")
{
    constexpr double rate = 48000.0;
    const auto input = steppedTone (rate, { { -70.0, 0.2 }, { -12.0, 0.3 } });

    for (const auto type : { BandType::highPass, BandType::lowPass, BandType::bandPass, BandType::notch })
    {
        auto s = envBand (type, 0.0, 200.0, 3200.0, -40.0, 30.0, 1000.0);
        s.gmode = GainMode::dynamic;   // ignored: a type without a gain is Static

        const auto t = trace (rate, { input }, { s });
        const auto fixed = render (rate, std::vector<std::vector<double>> { input },
                                   { { 0, [] (Chain& c) { c.setBand (0, band (true, BandType::highPass, 200.0, 2.0, 0.0)); } } },
                                   fixedBlocks (64));

        CHECK (t.hz[static_cast<std::size_t> (0.19 * rate)] == doctest::Approx (200.0).epsilon (1.0e-12));
        CHECK (cents (t.hz.back(), 200.0) > 2000.0);

        auto finite = true;

        for (const auto y : t.out[0])
            finite = finite && std::isfinite (y);

        CHECK (finite);

        if (type == BandType::highPass)
        {
            auto differs = false;

            for (auto n = static_cast<std::size_t> (0.3 * rate); n < input.size(); ++n)
                differs = differs || ! exactlyEqual (t.out[0][n], fixed[0][n]);

            CHECK (differs);
        }
    }
}

TEST_CASE_TEMPLATE ("exact bypass: bells and shelves at 0 dB in Env, their frequency moving with the key, are the input, bit for bit", Sample, float, double)
{
    const auto input = awkwardNoise<Sample> (24000);
    const std::vector<Event> events { { 0, [] (Chain& c)
                                        {
                                            const BandType types[] = { BandType::bell, BandType::lowShelf, BandType::highShelf };

                                            for (int b = 0; b < numBands; ++b)
                                                c.setBand (b, envBand (types[b % 3], 0.0, 100.0 * (b + 1), 800.0 * (b + 1), -50.0, 30.0,
                                                                       500.0 * (b + 1), b % 2 == 0 ? EnvDirection::up : EnvDirection::down,
                                                                       0.5, 20.0));
                                        } } };

    for (const auto& sizes : { fixedBlocks (64), fixedBlocks (1), randomBlocks (7) })
    {
        const auto output = render (48000.0, input, events, sizes);

        for (std::size_t c = 0; c < input.size(); ++c)
            CHECK (sameBits (output[c], input[c]));
    }
}

TEST_CASE_TEMPLATE ("block-size invariance: Env bands, both directions, their values and the mode moving, bit for bit", Sample, float, double)
{
    constexpr double rate = 48000.0;
    constexpr int length = 7 * 4096 * 2;

    std::vector<std::vector<Sample>> input;

    for (std::uint32_t seed : { 31u, 32u })
    {
        auto channel = noise<Sample> (length, seed, 0.3);
        const auto swing = tone<double> (length, 3.0, 1.0, rate);

        for (int n = 0; n < length; ++n)
            channel[static_cast<std::size_t> (n)] *= static_cast<Sample> (0.5 + 0.5 * swing[static_cast<std::size_t> (n)]);

        input.push_back (std::move (channel));
    }

    const auto events = []
    {
        std::vector<Event> e;
        e.push_back ({ 0, [] (Chain& c)
                       {
                           c.setBand (0, envBand (BandType::bell, 9.0, 300.0, 3000.0, -40.0, 30.0, 1000.0));
                           c.setBand (1, envBand (BandType::lowPass, 0.0, 800.0, 12000.0, -45.0, 24.0, 2000.0, EnvDirection::down));
                           auto follow = envBand (BandType::highShelf, -6.0, 2000.0, 9000.0, -50.0, 40.0, 1000.0);
                           follow.dfilt = DetectorFilter::follow;
                           c.setBand (2, follow);
                           auto dynamic = envBand (BandType::bell, 0.0, 150.0, 900.0, -40.0, 30.0, 400.0);
                           dynamic.gmode = GainMode::dynamic;
                           dynamic.thresholdDb = -30.0;
                           dynamic.rangeDb = -9.0;
                           c.setBand (3, dynamic);
                       } });
        e.push_back ({ 7001, [] (Chain& c)
                       {
                           auto s = envBand (BandType::bell, 9.0, 3000.0, 300.0, -30.0, 12.0, 1000.0);   // Low above High: swapped
                           s.envAttackMs = 1.0;
                           s.envReleaseMs = 40.0;
                           c.setBand (0, s);
                       } });
        e.push_back ({ 13003, [] (Chain& c) { c.setBand (1, envBand (BandType::lowPass, 0.0, 800.0, 12000.0, -45.0, 24.0, 2000.0)); } });   // direction
        e.push_back ({ 19005, [] (Chain& c) { c.setBand (2, band (true, BandType::highShelf, 3000.0, 2.0, -6.0)); } });                     // to Fixed
        e.push_back ({ 26007, [] (Chain& c) { c.setChain ({ false, 0.0, false }); } });                                                    // unlinked
        e.push_back ({ 33009, [] (Chain& c) { c.setBand (2, envBand (BandType::highShelf, -6.0, 2000.0, 9000.0, -50.0, 40.0, 1000.0)); } }); // back to Env
        return e;
    }();

    const auto reference = render (rate, input, events, fixedBlocks (64));

    for (const auto& sizes : { fixedBlocks (1), fixedBlocks (7), fixedBlocks (4096), randomBlocks (8) })
    {
        const auto output = render (rate, input, events, sizes);

        for (std::size_t c = 0; c < input.size(); ++c)
            CHECK (sameBits (output[c], reference[c]));
    }
}

TEST_CASE ("env, unlinked: a tone in the left channel moves the left channel's frequency only")
{
    constexpr double rate = 48000.0;
    const auto left = steppedTone (rate, { { -12.0, 0.4 } });
    const std::vector<double> right (left.size(), 0.0);

    Chain chain;
    chain.prepare (rate, 2);
    chain.setChain ({ false, 0.0, false });
    chain.setBand (0, envBand (BandType::bell, 6.0, 200.0, 3200.0, -40.0, 30.0, 1000.0));

    auto audio = std::vector<std::vector<double>> { left, right };
    double* pointers[] = { audio[0].data(), audio[1].data() };
    chain.process (pointers, 2, static_cast<int> (left.size()));

    CHECK (cents (chain.frequencyHz (0, 0), 200.0) > 2000.0);
    CHECK (chain.frequencyHz (0, 1) == doctest::Approx (200.0).epsilon (1.0e-12));

    // Linked, one frequency for both.
    Chain linked;
    linked.prepare (rate, 2);
    linked.setBand (0, envBand (BandType::bell, 6.0, 200.0, 3200.0, -40.0, 30.0, 1000.0));
    audio = { left, right };
    double* again[] = { audio[0].data(), audio[1].data() };
    linked.process (again, 2, static_cast<int> (left.size()));

    CHECK (linked.frequencyHz (0, 0) == linked.frequencyHz (0, 1));
    CHECK (cents (linked.frequencyHz (0, 1), 200.0) > 2000.0);
    CHECK (linked.bandMeter (0).frequencyHz == linked.frequencyHz (0, 0));
}
