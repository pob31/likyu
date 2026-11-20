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

        const auto run = [&input] (const BandSettings& s)
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

    const auto run = [&input] (BandSettings s)
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

//==============================================================================
// Several trackers on one key (devplan 5c).

namespace
{
    /** A chain run one control step at a time, every band's frequency read after each. */
    std::vector<std::vector<double>> traceBands (double rate, std::vector<double> key, const std::vector<BandSettings>& settings)
    {
        Chain chain;
        chain.prepare (rate, 1);

        for (std::size_t b = 0; b < settings.size(); ++b)
            chain.setBand (static_cast<int> (b), settings[b]);

        std::vector<std::vector<double>> hz (settings.size());

        for (std::size_t n = 0; n + static_cast<std::size_t> (controlStep) <= key.size(); n += static_cast<std::size_t> (controlStep))
        {
            double* p = key.data() + n;
            chain.process (&p, 1, controlStep);

            for (std::size_t b = 0; b < settings.size(); ++b)
                hz[b].push_back (chain.frequencyHz (static_cast<int> (b), 0));
        }

        return hz;
    }

    std::size_t stepAt (double seconds, double rate)
    {
        return static_cast<std::size_t> (seconds * rate / controlStep);
    }

    /** Sines whose levels in dB move linearly between breakpoints: one
        (time, level of each sine) per breakpoint. */
    std::vector<double> sines (double rate, const std::vector<double>& hz,
                               const std::vector<std::pair<double, std::vector<double>>>& breakpoints)
    {
        const auto length = static_cast<std::size_t> (breakpoints.back().first * rate);
        std::vector<double> out (length, 0.0);

        for (std::size_t n = 0; n < length; ++n)
        {
            const auto t = static_cast<double> (n) / rate;
            std::size_t k = 0;

            while (k + 2 < breakpoints.size() && t >= breakpoints[k + 1].first)
                ++k;

            const auto& [t0, l0] = breakpoints[k];
            const auto& [t1, l1] = breakpoints[k + 1];
            const auto u = std::clamp ((t - t0) / (t1 - t0), 0.0, 1.0);

            for (std::size_t s = 0; s < hz.size(); ++s)
            {
                const auto db = l0[s] + u * (l1[s] - l0[s]);
                out[n] += std::pow (10.0, db / 20.0) * std::sin (2.0 * pi * hz[s] * t + 0.7 * static_cast<double> (s));
            }
        }

        return out;
    }

    /** Which of `targets` a frequency is on, within `tolerance` cents; -1 for none. */
    int onWhich (double hz, const std::vector<double>& targets, double tolerance)
    {
        for (std::size_t i = 0; i < targets.size(); ++i)
            if (std::abs (cents (hz, targets[i])) < tolerance)
                return static_cast<int> (i);

        return -1;
    }
}

TEST_CASE ("track, no swap: two sines a fifth apart, locked at equal levels, then traded by +-12 dB: neither band moves by more than 50 cents")
{
    for (const auto rate : { 44100.0, 96000.0 })
    {
        for (const auto low : { 220.0, 6000.0 })
        {
            const auto high = low * 1.5;
            INFO ("rate " << rate << ", " << low << " and " << high << " Hz");

            // Equal for 1 s, then over 1 s A to +12 and B to -12, over 3 s the other way, held 0.5 s (the spike's protocol).
            const auto key = sines (rate, { low, high }, { { 0.0, { -12.0, -12.0 } }, { 1.0, { -12.0, -12.0 } },
                                                           { 2.0, { 0.0, -24.0 } }, { 5.0, { -24.0, 0.0 } }, { 5.5, { -24.0, 0.0 } } });

            // Each band's range an octave either side of one sine; their rests a little off.
            const auto hz = traceBands (rate, key, { trackBand (BandType::bell, low * 0.9, 3.0, low / 2.0, low * 2.0),
                                                     trackBand (BandType::bell, high * 1.1, 3.0, high / 2.0, high * 2.0) });

            const auto locked = stepAt (0.99, rate);
            const auto a = onWhich (hz[0][locked], { low, high }, 20.0);
            const auto b = onWhich (hz[1][locked], { low, high }, 20.0);
            REQUIRE (a >= 0);
            REQUIRE (b >= 0);
            CHECK (a != b);

            double worst = 0.0;

            for (auto s = locked; s < hz[0].size(); ++s)
            {
                worst = std::max (worst, std::abs (cents (hz[0][s], hz[0][locked])));
                worst = std::max (worst, std::abs (cents (hz[1][s], hz[1][locked])));
            }

            MESSAGE ("rate " << rate << ", " << low << " and " << high << " Hz: the bands settle on sines " << a << " and " << b
                     << "; through the trade, the larger departure is " << worst << " cents");
            CHECK (worst < 50.0);
        }
    }
}

TEST_CASE ("track, exclusion: two bands released, then re-seeded, settle on different sines")
{
    constexpr double rate = 48000.0, a = 300.0, b = 450.0;

    // The sines, then nothing for longer than the Hold, then the sines again.
    auto key = sines (rate, { a, b }, { { 0.0, { -12.0, -12.0 } }, { 3.0, { -12.0, -12.0 } } });
    const auto gapFrom = static_cast<std::size_t> (1.0 * rate), gapTo = static_cast<std::size_t> (1.8 * rate);

    for (auto n = gapFrom; n < gapTo; ++n)
        key[n] = 0.0;

    // Both bands rest at the same frequency and cover both sines.
    const auto hz = traceBands (rate, key, { trackBand (BandType::bell, 370.0, 3.0, 200.0, 700.0),
                                             trackBand (BandType::bell, 370.0, 3.0, 200.0, 700.0) });

    for (const auto at : { 0.95, 2.95 })
    {
        const auto s = stepAt (at, rate);
        const auto first = onWhich (hz[0][s], { a, b }, 20.0), second = onWhich (hz[1][s], { a, b }, 20.0);
        MESSAGE ("at " << at << " s: band 1 on sine " << first << " (" << hz[0][s] << " Hz), band 2 on sine " << second
                 << " (" << hz[1][s] << " Hz)");
        CHECK (first >= 0);
        CHECK (second >= 0);
        CHECK (first != second);
    }

    // Released during the gap: both back at rest.
    const auto released = stepAt (1.75, rate);
    CHECK (hz[0][released] == doctest::Approx (370.0).epsilon (1.0e-12));
    CHECK (hz[1][released] == doctest::Approx (370.0).epsilon (1.0e-12));
}

TEST_CASE ("track, lock, seeded: from a rest at either end of the range, a sine at 0 dB SNR is found within Speed x 5")
{
    constexpr double rate = 48000.0, speedMs = 50.0;

    for (const auto hz : { 220.0, 1000.0, 6000.0 })
    {
        for (const auto rest : { hz / 2.0, hz * 2.0 })
        {
            const auto key = sineInNoise (rate, 1.5, hz, 1.0, zeroDbNoise, 17);
            const auto t = trace (rate, key, { trackBand (BandType::bell, rest, 6.0, hz / 2.0, hz * 2.0, speedMs) });
            const auto [lock, worst] = lockOf (t, hz, 20.0, 1.0);
            MESSAGE (hz << " Hz from a rest at " << rest << " Hz: within 20 cents after " << 1000.0 * lock << " ms, worst after "
                     << worst << " cents");
            CHECK (lock < 5.0 * speedMs / 1000.0);
            CHECK (worst < 20.0);
        }
    }
}

TEST_CASE ("track, eight bands on one key: eight sines, eight bands over the whole range, one each")
{
    constexpr double rate = 48000.0;
    const std::vector<double> targets { 150.0, 260.0, 450.0, 780.0, 1350.0, 2350.0, 4100.0, 7100.0 };
    const auto key = sines (rate, targets, { { 0.0, std::vector<double> (8, -20.0) }, { 2.0, std::vector<double> (8, -20.0) } });

    std::vector<BandSettings> settings;

    for (int b = 0; b < numBands; ++b)
        settings.push_back (trackBand (BandType::bell, 1000.0, 2.0, 100.0, 10000.0));

    const auto hz = traceBands (rate, key, settings);
    const auto s = stepAt (1.95, rate);
    std::vector<int> on;
    std::string list;

    for (std::size_t b = 0; b < hz.size(); ++b)
    {
        on.push_back (onWhich (hz[b][s], targets, 20.0));
        list += " " + std::to_string (static_cast<int> (std::lround (hz[b][s])));
    }

    auto distinct = 0;

    for (std::size_t i = 0; i < targets.size(); ++i)
        distinct += std::count (on.begin(), on.end(), static_cast<int> (i)) > 0 ? 1 : 0;

    MESSAGE ("eight bands, eight sines: the bands end at" << list << " Hz; " << distinct << " of the 8 sines have a band");
    CHECK (distinct == 8);
}

TEST_CASE ("track, onset: a loud sine appearing beside a quiet tracked one does not take its band (spike, finding 5)")
{
    for (const auto rate : { 44100.0, 48000.0 })
    {
        for (const auto low : { 220.0, 1000.0 })
        {
            const auto high = low * 1.5;

            // The quiet one alone for 1 s; then the loud one, 24 dB over it, at once.
            const auto key = sines (rate, { low, high }, { { 0.0, { -30.0, -200.0 } }, { 1.0, { -30.0, -200.0 } },
                                                           { 1.0001, { -30.0, -6.0 } }, { 2.5, { -30.0, -6.0 } } });

            const auto hz = traceBands (rate, key, { trackBand (BandType::bell, low, 3.0, low / 2.0, low * 2.0),
                                                     trackBand (BandType::bell, high * 1.2, 3.0, high / 2.0, high * 2.0) });

            double worst = 0.0;

            for (auto s = stepAt (0.9, rate); s < hz[0].size(); ++s)
                worst = std::max (worst, std::abs (cents (hz[0][s], low)));

            const auto second = hz[1].back();
            MESSAGE ("rate " << rate << ": " << low << " Hz quiet, " << high << " Hz appearing 24 dB over it: the quiet one's band strays "
                     << worst << " cents; the other band ends at " << second << " Hz");
            CHECK (worst < 50.0);
        }
    }
}

TEST_CASE ("track, crossing glides, for information: the PRD accepts an exchange at the crossing")
{
    constexpr double rate = 48000.0;
    const auto length = static_cast<int> (3.5 * rate);
    std::vector<double> key (static_cast<std::size_t> (length));
    double up = 0.0, down = 0.0;

    for (int n = 0; n < length; ++n)
    {
        const auto u = std::clamp ((static_cast<double> (n) / rate - 0.5) / 3.0, 0.0, 1.0);
        up += 2.0 * pi * 300.0 * std::pow (2.0, u) / rate;
        down += 2.0 * pi * 600.0 * std::pow (0.5, u) / rate;
        key[static_cast<std::size_t> (n)] = 0.25 * (std::sin (up) + std::sin (down));
    }

    const auto hz = traceBands (rate, key, { trackBand (BandType::bell, 300.0, 3.0, 200.0, 900.0),
                                             trackBand (BandType::bell, 600.0, 3.0, 200.0, 900.0) });

    MESSAGE ("two glides crossing, 300 to 600 Hz and 600 to 300 Hz over 3 s: band 1 starts at " << hz[0][stepAt (0.45, rate)]
             << " Hz and ends at " << hz[0].back() << " Hz; band 2 starts at " << hz[1][stepAt (0.45, rate)] << " and ends at "
             << hz[1].back() << " Hz");
    CHECK (std::abs (cents (hz[0].back(), hz[1].back())) > 600.0);
}

TEST_CASE ("track, Speed against a close low pair, for information (PRD, open decision): 220 and 330 Hz traded by +-12 dB")
{
    constexpr double rate = 44100.0;
    const auto key = sines (rate, { 220.0, 330.0 }, { { 0.0, { -12.0, -12.0 } }, { 1.0, { -12.0, -12.0 } },
                                                      { 2.0, { 0.0, -24.0 } }, { 5.0, { -24.0, 0.0 } }, { 5.5, { -24.0, 0.0 } } });

    for (const auto speed : { 20.0, 50.0, 100.0, 200.0 })
    {
        const auto hz = traceBands (rate, key, { trackBand (BandType::bell, 200.0, 3.0, 110.0, 440.0, speed),
                                                 trackBand (BandType::bell, 360.0, 3.0, 165.0, 660.0, speed) });

        const auto locked = stepAt (1.0, rate);
        double worst = 0.0;

        for (auto s = locked; s < hz[0].size(); ++s)
        {
            worst = std::max (worst, std::abs (cents (hz[0][s], hz[0][locked])));
            worst = std::max (worst, std::abs (cents (hz[1][s], hz[1][locked])));
        }

        MESSAGE ("Speed " << speed << " ms: the larger departure through the trade is " << worst << " cents");
    }
}


TEST_CASE ("track, confidence of white noise alone across the spectrum, for information (PRD: over the whole key)")
{
    // A notch of Q 4 at f removes (pi / 2) (f / Q) of white noise's fs / 2:
    // noise alone reads more confidence the higher the band tracks.
    for (const auto rate : { 44100.0, 96000.0 })
    {
        std::string line;

        for (const auto hz : { 250.0, 1000.0, 4000.0, 8000.0, 16000.0 })
        {
            const auto settings = trackBand (BandType::bell, hz, 6.0, hz / std::sqrt (2.0), std::min (hz * std::sqrt (2.0), 20000.0));
            const auto t = trace (rate, noise<double> (static_cast<int> (2.0 * rate), 23, 1.0), { settings });

            double sum = 0.0, peak = 0.0;
            int count = 0;

            for (auto s = t.at (0.5); s < t.hz.size(); ++s, ++count)
            {
                sum += t.confidence[s];
                peak = std::max (peak, t.confidence[s]);
            }

            const auto expected = (pi / 2.0) * (hz / trackerQ) / (rate / 2.0);
            line += "  " + std::to_string (static_cast<int> (hz)) + " Hz: mean " + std::to_string (sum / count) + " (expected "
                    + std::to_string (expected) + "), peak " + std::to_string (peak) + ";";
        }

        MESSAGE ("white noise, rate " << rate << ":" << line);
    }
}
