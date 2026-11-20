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
    Track, the second frequency mode (PRD, Frequency modes; devplan 5b, 5c).
    Acceptance rows: Track lock (a sine in noise at 0 dB SNR, inside the
    range: locks within Speed x 5, error under 20 cents), Track loss (sine
    removed: Hold, then a glide to rest, no jump). Also: confidence, Hold
    freezing the estimate, a band at rest being the Fixed band bit for bit,
    and confidence scaling a boost and not a cut.
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
    /** Uniform noise of this amplitude has the power of a sine of peak 1: 0 dB SNR. */
    const double zeroDbNoise = std::sqrt (1.5);

    /** A chain run one control step at a time, a band's frequency and
        confidence read after each. */
    struct Trace
    {
        std::vector<double> hz, confidence;     // one per control step, at its end
        double rate = 0.0;

        /** The step that ends at or after `seconds`. */
        std::size_t at (double seconds) const
        {
            return std::min (hz.size() - 1, static_cast<std::size_t> (seconds * rate / controlStep));
        }

        double seconds (std::size_t step) const
        {
            return static_cast<double> ((step + 1) * static_cast<std::size_t> (controlStep)) / rate;
        }
    };

    Trace trace (double rate, std::vector<double> key, const std::vector<BandSettings>& settings, int band = 0,
                 std::vector<double>* output = nullptr)
    {
        Chain chain;
        chain.prepare (rate, 1);

        for (std::size_t b = 0; b < settings.size(); ++b)
            chain.setBand (static_cast<int> (b), settings[b]);

        Trace t;
        t.rate = rate;

        for (std::size_t n = 0; n + static_cast<std::size_t> (controlStep) <= key.size(); n += static_cast<std::size_t> (controlStep))
        {
            double* p = key.data() + n;
            chain.process (&p, 1, controlStep);
            t.hz.push_back (chain.frequencyHz (band, 0));
            t.confidence.push_back (chain.bandMeter (band).confidence);
        }

        if (output != nullptr)
            *output = std::move (key);

        return t;
    }

    double cents (double hz, double reference)
    {
        return 1200.0 * std::log2 (hz / reference);
    }

    /** A sine of peak `amplitude` plus uniform noise of peak `noiseAmplitude`. */
    std::vector<double> sineInNoise (double rate, double seconds, double hz, double amplitude, double noiseAmplitude,
                                     std::uint32_t seed = 7)
    {
        const auto length = static_cast<int> (seconds * rate);
        auto out = tone<double> (length, hz, amplitude, rate);
        const auto hiss = noise<double> (length, seed, noiseAmplitude);

        for (std::size_t n = 0; n < out.size(); ++n)
            out[n] += hiss[n];

        return out;
    }

    /** The last time the error exceeds `tolerance` cents (0 if never), and the
        largest error after `from` seconds. */
    std::pair<double, double> lockOf (const Trace& t, double target, double tolerance, double from)
    {
        double last = 0.0, worst = 0.0;

        for (std::size_t s = 0; s < t.hz.size(); ++s)
        {
            const auto e = std::abs (cents (t.hz[s], target));

            if (e > tolerance)
                last = t.seconds (s);

            if (t.seconds (s) >= from)
                worst = std::max (worst, e);
        }

        return { last, worst };
    }
}

//==============================================================================
TEST_CASE ("track, lock: a sine in white noise at 0 dB SNR, inside the range, locks within Speed x 5, under 20 cents")
{
    constexpr double speedMs = 50.0;

    for (const auto rate : { 44100.0, 96000.0 })
    {
        for (const auto hz : { 220.0, 1000.0, 6000.0 })
        {
            INFO ("rate " << rate << ", " << hz << " Hz");

            // The rest half an octave below the sine, the range an octave either side (the spike's case).
            const auto key = sineInNoise (rate, 1.5, hz, 1.0, zeroDbNoise);
            const auto t = trace (rate, key, { trackBand (BandType::bell, hz / std::sqrt (2.0), 6.0, hz / 2.0, hz * 2.0, speedMs) });

            const auto [lock, worst] = lockOf (t, hz, 20.0, 1.0);
            MESSAGE ("rate " << rate << ", " << hz << " Hz: locked within 20 cents after " << 1000.0 * lock
                     << " ms (" << 5.0 * speedMs << " allowed); worst error over the last 0.5 s " << worst
                     << " cents; confidence " << t.confidence.back());

            CHECK (lock < 5.0 * speedMs / 1000.0);
            CHECK (worst < 20.0);
        }
    }
}

TEST_CASE ("track, confidence: about 0.5 on a sine at 0 dB SNR, near 1 clean, near 0 on noise, 0 on silence")
{
    constexpr double rate = 48000.0, hz = 1000.0;
    const auto settings = trackBand (BandType::bell, hz, 6.0, hz / 2.0, hz * 2.0);

    const auto mean = [] (const Trace& t, double from)
    {
        double sum = 0.0;
        int count = 0;

        for (auto s = t.at (from); s < t.hz.size(); ++s, ++count)
            sum += t.confidence[s];

        return sum / count;
    };

    const auto clean = trace (rate, sineInNoise (rate, 2.0, hz, 1.0, 0.0), { settings });
    const auto noisy = trace (rate, sineInNoise (rate, 2.0, hz, 1.0, zeroDbNoise), { settings });
    const auto hiss = trace (rate, noise<double> (static_cast<int> (2.0 * rate), 3, 1.0), { settings });
    const auto silence = trace (rate, std::vector<double> (static_cast<std::size_t> (rate), 0.0), { settings });

    MESSAGE ("confidence: clean " << mean (clean, 1.0) << ", 0 dB SNR " << mean (noisy, 1.0) << ", noise " << mean (hiss, 1.0));
    CHECK (mean (clean, 1.0) > 0.99);
    CHECK (mean (noisy, 1.0) == doctest::Approx (0.5).epsilon (0.1));
    CHECK (mean (hiss, 1.0) < 0.05);
    CHECK (silence.confidence.back() == 0.0);
    CHECK (silence.hz.back() == doctest::Approx (hz).epsilon (1.0e-12));
}

TEST_CASE ("track, loss: the sine removed, the band holds for Hold, then glides to rest, with no jump")
{
    constexpr double rate = 48000.0, hz = 1500.0, rest = 1000.0, holdMs = 200.0;

    auto key = sineInNoise (rate, 2.5, hz, 1.0, 0.01);
    const auto removal = static_cast<std::size_t> (1.0 * rate);

    for (auto n = removal; n < key.size(); ++n)
        key[n] = noise<double> (1, static_cast<std::uint32_t> (n), 0.01)[0];

    auto settings = trackBand (BandType::bell, rest, 6.0, 700.0, 2800.0);
    settings.holdMs = holdMs;
    const auto t = trace (rate, key, { settings });

    // Locked before the removal.
    CHECK (std::abs (cents (t.hz[t.at (0.99)], hz)) < 5.0);

    // Held: the frequency stays on the sine for at least the Hold.
    std::size_t left = t.hz.size();

    for (auto s = t.at (1.0); s < t.hz.size(); ++s)
    {
        if (std::abs (cents (t.hz[s], hz)) > 5.0)
        {
            left = s;
            break;
        }
    }

    const auto heldMs = 1000.0 * (t.seconds (left) - 1.0);
    MESSAGE ("held " << heldMs << " ms after the sine went, for a Hold of " << holdMs << " ms");
    CHECK (heldMs >= holdMs);
    CHECK (heldMs < holdMs + 100.0);

    // Then back to rest, monotonically and without a jump, and at rest exactly.
    double largest = 0.0;
    auto monotonic = true;

    for (auto s = left; s < t.hz.size(); ++s)
    {
        const auto move = cents (t.hz[s], t.hz[s - 1]);
        largest = std::max (largest, std::abs (move));
        monotonic = monotonic && move <= 0.0;
    }

    MESSAGE ("the largest move in one control step on the way back: " << largest << " cents");
    CHECK (monotonic);
    CHECK (largest < 25.0);
    CHECK (t.hz.back() == doctest::Approx (rest).epsilon (1.0e-12));
}

TEST_CASE ("track: while held, the band's frequency stays where it was")
{
    constexpr double rate = 48000.0, hz = 1200.0;

    auto key = sineInNoise (rate, 2.0, hz, 1.0, 0.01);

    for (auto n = static_cast<std::size_t> (rate); n < key.size(); ++n)
        key[n] = noise<double> (1, static_cast<std::uint32_t> (n) * 7u, 0.01)[0];

    auto settings = trackBand (BandType::bell, 1000.0, 6.0, 600.0, 2400.0);
    settings.holdMs = 900.0;
    const auto t = trace (rate, key, { settings });

    // From the moment confidence has fallen to the end of the Hold, the frequency does not drift.
    double drift = 0.0;

    for (auto s = t.at (1.1); s < t.at (1.85); ++s)
        drift = std::max (drift, std::abs (cents (t.hz[s], hz)));

    MESSAGE ("drift while held: " << drift << " cents");
    CHECK (drift < 2.0);
}

TEST_CASE ("track: a band at rest is the Fixed band it would be, bit for bit, static or dynamic")
{
    constexpr double rate = 48000.0;
    const auto input = noise<double> (40000, 9, 0.5);

    for (const auto gmode : { GainMode::staticGain, GainMode::dynamic })
    {
        auto fixed = band (true, BandType::bell, 1000.0, 2.0, 6.0);
        fixed.gmode = gmode;
        fixed.thresholdDb = -30.0;

        auto tracking = trackBand (BandType::bell, 1000.0, 6.0, 500.0, 2000.0);
        tracking.gmode = gmode;
        tracking.thresholdDb = -30.0;

        const auto run = [&input, rate] (const BandSettings& s)
        {
            return render (rate, std::vector<std::vector<double>> { input },
                           { { 0, [s] (Chain& c) { c.setBand (0, s); } } }, fixedBlocks (64));
        };

        const auto a = run (fixed), b = run (tracking);
        CHECK (sameBits (a[0], b[0]));
    }
}

TEST_CASE ("track: confidence scales a boost, so it does not fizz on noise, and leaves a cut alone")
{
    constexpr double rate = 48000.0;
    const auto input = noise<double> (48000, 13, 0.5);

    const auto energy = [] (const std::vector<double>& x, std::size_t from)
    {
        double sum = 0.0;

        for (auto n = from; n < x.size(); ++n)
            sum += x[n] * x[n];

        return sum;
    };

    const auto run = [&input, rate] (BandSettings s)
    {
        return render (rate, std::vector<std::vector<double>> { input },
                       { { 0, [s] (Chain& c) { c.setBand (0, s); } } }, fixedBlocks (64))[0];
    };

    auto boost = trackBand (BandType::bell, 1000.0, 12.0, 500.0, 2000.0);
    const auto unscaled = run (boost);
    boost.confidenceScalesGain = true;
    const auto scaledBoost = run (boost);

    const auto from = static_cast<std::size_t> (0.5 * rate);
    const auto dry = energy (input, from), full = energy (unscaled, from), scaled = energy (scaledBoost, from);
    MESSAGE ("+12 dB on noise: " << 10.0 * std::log10 (full / dry) << " dB more energy unscaled, "
             << 10.0 * std::log10 (scaled / dry) << " dB scaled by confidence");
    CHECK (10.0 * std::log10 (scaled / dry) < 0.1 * 10.0 * std::log10 (full / dry));

    // A cut is not scaled: the same bits with or without.
    auto cut = trackBand (BandType::bell, 1000.0, -12.0, 500.0, 2000.0);
    const auto plain = run (cut);
    cut.confidenceScalesGain = true;
    CHECK (sameBits (run (cut), plain));
}


TEST_CASE ("track: a sine gone for less than the Hold and back finds its band where it left it: the estimate did not drift")
{
    // The spike: with nothing holding it, an estimate drifts 25 to 144 cents
    // in half a second after its sine goes. Here it runs at confidence /
    // threshold of its speed while held, so when the sine comes back the
    // band is on it and does not move.
    constexpr double rate = 48000.0, hz = 900.0;

    for (const auto seed : { 3u, 5u, 11u })
    {
        auto key = sineInNoise (rate, 2.5, hz, 1.0, 0.01, seed);
        const auto from = static_cast<std::size_t> (1.0 * rate), to = static_cast<std::size_t> (1.5 * rate);

        for (auto n = from; n < to; ++n)
            key[n] = noise<double> (1, static_cast<std::uint32_t> (n) * seed, 0.01)[0];

        auto settings = trackBand (BandType::bell, 700.0, 6.0, 450.0, 1800.0);
        settings.holdMs = 800.0;
        const auto t = trace (rate, key, { settings });

        double worst = 0.0;

        for (auto s = t.at (0.9); s < t.hz.size(); ++s)
            worst = std::max (worst, std::abs (cents (t.hz[s], hz)));

        MESSAGE ("seed " << seed << ": the band strays at most " << worst << " cents from the sine, before, during and after the gap");
        CHECK (worst < 5.0);
    }
}
