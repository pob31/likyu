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
    The frequency modes with the rest (devplan 5d): every gain mode with every
    frequency mode through the rows that every mode is held to: constant
    latency, block-size invariance, realtime, exact bypass (idle and its
    return), layouts, channel linking, the gain limit, denormals. Also: a
    Follow detector on a Track band, and how far the 16-sample interpolation
    of g bends a band that tracks.
*/

#include <doctest/doctest.h>

#include "DspTestSupport.h"

#include <dsp/Chain.h>
#include <dsp/rt/RtCheck.h>

#include <cmath>
#include <limits>
#include <string>

using namespace likyu::dsp;
using namespace likyutest::dsp;

namespace
{
    constexpr double rate = 48000.0;

    std::string nameOf (FrequencyMode m)
    {
        return m == FrequencyMode::fixed ? "Fixed" : m == FrequencyMode::env ? "Env" : "Track";
    }

    /** Every band in `gmode` and `fmode`, the types and keys mixed. */
    void everyBandIn (Chain& c, GainMode gmode, FrequencyMode fmode, double gainDb = 6.0)
    {
        for (int b = 0; b < numBands; ++b)
        {
            const auto hz = 60.0 * std::pow (2.0, b);
            auto s = transientBand (static_cast<BandType> (b % numBandTypes), hz, 0.7 + 0.3 * b, b % 2 == 0 ? gainDb : -gainDb,
                                    b % 2 == 0 ? -100.0 : 100.0, 50.0, b % 2 == 0 ? -12.0 : 9.0);
            s.gmode = gmode;
            s.fmode = fmode;
            s.thresholdDb = -60.0;
            s.key = b % 3 == 1 ? KeySource::sidechain : KeySource::self;
            s.lowHz = hz / 2.0;
            s.highHz = hz * 2.0;
            s.sensitivityDb = -60.0;
            s.envDirection = b % 2 == 0 ? EnvDirection::up : EnvDirection::down;
            s.confidenceScalesGain = b % 4 == 0;
            s.speedMs = 10.0 + 20.0 * b;

            if (b == 5)
            {
                s.dfilt = DetectorFilter::own;
                s.dtype = DetectorType::highPass;
                s.detectorFrequency = 2000.0;
            }

            c.setBand (b, s);
        }
    }

    int firstNonZero (const std::vector<std::vector<double>>& audio)
    {
        for (std::size_t n = 0; n < audio[0].size(); ++n)
            if (std::fpclassify (audio[0][n]) != FP_ZERO)
                return static_cast<int> (n);

        return -1;
    }

    /** Noise with -0 and denormals sprinkled through it. */
    template <typename Sample>
    std::vector<std::vector<Sample>> awkwardNoise (int length, double amplitude = 0.5)
    {
        std::vector<std::vector<Sample>> audio { noise<Sample> (length, 41, amplitude), noise<Sample> (length, 42, amplitude) };

        for (auto& channel : audio)
        {
            for (std::size_t n = 0; n < channel.size(); n += 97)
                channel[n] = -Sample (0);

            for (std::size_t n = 31; n < channel.size(); n += 101)
                channel[n] = std::numeric_limits<Sample>::denorm_min() * Sample (n % 7 + 1);
        }

        return audio;
    }

    /** A tone that glides in log frequency from `from` to `to` Hz over the whole length. */
    std::vector<double> glide (int length, double from, double to, double amplitude)
    {
        std::vector<double> out (static_cast<std::size_t> (length));
        double phase = 0.0;

        for (int n = 0; n < length; ++n)
        {
            out[static_cast<std::size_t> (n)] = amplitude * std::sin (phase);
            phase += 2.0 * pi * from * std::pow (to / from, static_cast<double> (n) / length) / rate;
        }

        return out;
    }

    double cents (double hz, double reference)
    {
        return 1200.0 * std::log2 (hz / reference);
    }
}

//==============================================================================
TEST_CASE ("constant latency, frequency modes: an impulse comes out `lookahead` samples late in every gain and frequency mode, bypassed or not")
{
    constexpr int length = 2048;

    for (const auto r : { 44100.0, 96000.0 })
    {
        for (int choice = 0; choice < numLookaheadChoices; ++choice)
        {
            const auto d = lookaheadSamples (choice, r);

            for (const auto bypassed : { false, true })
            {
                for (int g = 0; g < numGainModes; ++g)
                {
                    for (int f = 0; f < numFrequencyModes; ++f)
                    {
                        std::vector<std::vector<double>> impulse (2, std::vector<double> (length, 0.0));
                        impulse[0][0] = 1.0;
                        impulse[1][0] = 0.5;
                        const std::vector<std::vector<double>> sidechain { noise<double> (length, 81, 0.5) };

                        const std::vector<Event> events { { 0, [=] (Chain& c)
                        {
                            everyBandIn (c, static_cast<GainMode> (g), static_cast<FrequencyMode> (f));
                            c.setChain ({ bypassed, -1.0 });
                            c.setLookahead (d);
                        } } };

                        const auto output = render (r, impulse, events, fixedBlocks (64), sidechain);

                        INFO ("rate " << r << ", lookahead " << d << ", gain mode " << g << ", " << nameOf (static_cast<FrequencyMode> (f))
                                      << ", bypassed " << bypassed);
                        CHECK (firstNonZero (output) == d);
                    }
                }
            }
        }
    }
}

//==============================================================================
namespace
{
    /** Everything the frequency modes do, moving: both keys, the link and its
        list, every gain mode, the switches between frequency modes, their
        values, confidence scaling the gain, the lookahead. */
    std::vector<Event> frequencyEverything (int period)
    {
        return {
            { 0, [] (Chain& c)
              {
                  auto env = envBand (BandType::bell, 6.0, 200.0, 2000.0, -50.0, 30.0, 700.0);
                  env.gmode = GainMode::dynamic;
                  env.thresholdDb = -40.0;
                  c.setBand (0, env);

                  auto track = trackBand (BandType::bell, 900.0, 9.0, 300.0, 3000.0, 20.0);
                  track.confidenceScalesGain = true;
                  c.setBand (1, track);

                  auto sideTrack = trackBand (BandType::highShelf, 3000.0, -6.0, 1500.0, 8000.0);
                  sideTrack.key = KeySource::sidechain;
                  sideTrack.gmode = GainMode::transient;
                  sideTrack.transientAttackPercent = -80.0;
                  c.setBand (2, sideTrack);

                  c.setBand (3, trackBand (BandType::lowPass, 6000.0, 0.0, 2000.0, 12000.0, 5.0));
                  c.setBand (4, envBand (BandType::bandPass, 0.0, 400.0, 4000.0, -45.0, 24.0, 1000.0, EnvDirection::down, 1.0, 40.0));
                  c.setBand (5, trackBand (BandType::notch, 500.0, 0.0, 250.0, 1000.0));
                  c.setLookahead (lookaheadSamples (2, rate));
              } },
            { period / 3, [] (Chain& c)
              {
                  auto s = trackBand (BandType::bell, 900.0, 9.0, 300.0, 3000.0, 20.0);
                  s.lowHz = 600.0;
                  s.highHz = 250.0;                       // crossed: swapped
                  s.speedMs = 80.0;
                  s.confidencePercent = 60.0;
                  s.holdMs = 20.0;
                  c.setBand (1, s);
              } },
            { period, [] (Chain& c)
              {
                  c.setChain ({ false, -1.0, false });     // unlinked
                  c.setBand (0, trackBand (BandType::bell, 700.0, 6.0, 200.0, 2000.0));   // Env to Track
              } },
            { period + 7, [] (Chain& c) { c.setBand (5, band (true, BandType::notch, 500.0, 4.0, 0.0)); } },        // Track to Fixed
            { 2 * period, [] (Chain& c)
              {
                  c.setChain ({ false, 0.0, true });
                  c.setBandChannels (4, 1u);                // left channel out of band 5
                  auto s = envBand (BandType::bandPass, 0.0, 400.0, 4000.0, -40.0, 12.0, 1000.0, EnvDirection::up, 0.2, 400.0);
                  c.setBand (4, s);                         // direction, Sensitivity, Span, times
                  c.setLookahead (lookaheadSamples (3, rate));
              } },
            { 2 * period + 300, [] (Chain& c) { c.setBand (5, trackBand (BandType::notch, 500.0, 0.0, 250.0, 1000.0)); } },  // back to Track mid-fade
            { 3 * period, [] (Chain& c)
              {
                  c.setBandChannels (4, 0u);
                  c.setChain ({ true, 0.0, true });
              } },
            { 3 * period + 2000, [] (Chain& c) { c.setChain ({ false, 0.0, true }); } },
        };
    }

    template <typename Sample>
    std::vector<std::vector<Sample>> swungNoise (int length, std::uint32_t seed)
    {
        std::vector<std::vector<Sample>> out;

        for (std::uint32_t c = 0; c < 2; ++c)
        {
            auto channel = noise<Sample> (length, seed + c, 0.3);
            const auto swing = tone<double> (length, 2.0 + c, 1.0, rate);
            const auto partial = tone<double> (length, 800.0 + 300.0 * c, 0.4, rate);

            for (int n = 0; n < length; ++n)
            {
                const auto i = static_cast<std::size_t> (n);
                channel[i] = static_cast<Sample> ((static_cast<double> (channel[i]) + partial[i]) * (0.5 + 0.5 * swing[i]));
            }

            out.push_back (std::move (channel));
        }

        return out;
    }
}

TEST_CASE_TEMPLATE ("block-size invariance, again: Env and Track bands, both keys, the link and their own values moving, bit for bit", Sample, float, double)
{
    constexpr int period = 7 * 4096;
    const auto input = swungNoise<Sample> (4 * period, 51);
    const std::vector<std::vector<Sample>> sidechain { swungNoise<Sample> (4 * period, 61)[0], swungNoise<Sample> (4 * period, 71)[1] };
    const auto events = frequencyEverything (period);

    const auto reference = render (rate, input, events, fixedBlocks (64), sidechain);
    CHECK_FALSE (sameBits (reference[0], input[0]));

    for (const auto& sizes : { fixedBlocks (1), fixedBlocks (7), fixedBlocks (4096), randomBlocks (91) })
    {
        const auto output = render (rate, input, events, sizes, sidechain);

        for (std::size_t c = 0; c < input.size(); ++c)
            CHECK (sameBits (output[c], reference[c]));
    }
}

TEST_CASE ("realtime: 500 blocks of Env and Track bands, both keys, their values, the modes and the link moving, allocate nothing")
{
    namespace rt = likyu::dsp::rt;
    REQUIRE (rt::isCounting());

    Chain chain;
    chain.prepare (96000.0, 2);

    auto left = noise<double> (64, 1, 0.5), right = noise<double> (64, 2, 0.5);
    auto sideLeft = noise<double> (64, 3, 0.5), sideRight = noise<double> (64, 4, 0.5);
    double* channels[] = { left.data(), right.data() };
    const double* side[] = { sideLeft.data(), sideRight.data() };
    Lcg random { 16 };

    rt::resetCounts();

    for (int block = 0; block < 500; ++block)
    {
        if (block % 5 == 0)
        {
            for (int b = 0; b < numBands; ++b)
            {
                auto s = trackBand (static_cast<BandType> (random.next() % numBandTypes), 20.0 * std::pow (1000.0, random.unit()),
                                    60.0 * random.unit() - 30.0, 20.0 * std::pow (1000.0, random.unit()), 20.0 * std::pow (1000.0, random.unit()),
                                    std::pow (1000.0, random.unit()));
                s.on = random.unit() < 0.9;
                s.fmode = static_cast<FrequencyMode> (random.next() % numFrequencyModes);
                s.gmode = static_cast<GainMode> (random.next() % numGainModes);
                s.key = static_cast<KeySource> (random.next() % numKeySources);
                s.dfilt = static_cast<DetectorFilter> (random.next() % numDetectorFilters);
                s.envDirection = static_cast<EnvDirection> (random.next() % numEnvDirections);
                s.sensitivityDb = -80.0 * random.unit();
                s.spanDb = 6.0 + 54.0 * random.unit();
                s.confidencePercent = 100.0 * random.unit();
                s.holdMs = 2000.0 * random.unit() * random.unit();
                s.confidenceScalesGain = random.unit() < 0.5;
                s.thresholdDb = -60.0;
                s.transientAttackPercent = 100.0;
                chain.setBand (b, s);
            }

            chain.setChain ({ random.unit() < 0.2, 48.0 * random.unit() - 24.0, random.unit() < 0.5 });
            chain.setBandChannels (static_cast<int> (random.next() % numBands), random.next() % 4u);
        }

        const auto gain = block % 7 < 3 ? 1.0 : 1.0e-4;

        for (int n = 0; n < 64; ++n)
        {
            const auto t = static_cast<double> (block * 64 + n) / 96000.0;
            left[static_cast<std::size_t> (n)] = gain * (0.3 * random.bipolar() + 0.6 * std::sin (2.0 * pi * 700.0 * t));
            right[static_cast<std::size_t> (n)] = gain * (0.3 * random.bipolar() + 0.6 * std::sin (2.0 * pi * 1900.0 * t));
            sideLeft[static_cast<std::size_t> (n)] = std::sin (2.0 * pi * 3000.0 * t);
        }

        Key<double> key;
        key.self = channels;
        key.selfChannels = 2;
        key.sidechain = side;
        key.sidechainChannels = static_cast<int> (random.next() % 3u);
        chain.process (channels, 2, key, 64);
    }

    CHECK (rt::regions() == 500);
    CHECK (rt::violations() == 0);
}

//==============================================================================
TEST_CASE_TEMPLATE ("exact bypass: bells and shelves at 0 dB in Track, idle or locked, confidence scaling or not, are the input, bit for bit", Sample, float, double)
{
    // A noise key (idle: released, at rest), and a sine in it (locked).
    for (const auto withSine : { false, true })
    {
        auto input = awkwardNoise<Sample> (24000, 0.2);

        if (withSine)
            for (auto& channel : input)
                for (std::size_t n = 0; n < channel.size(); ++n)
                    if (std::fpclassify (channel[n]) == FP_NORMAL)
                        channel[n] = static_cast<Sample> (static_cast<double> (channel[n]) + 0.5 * std::sin (2.0 * pi * 1100.0 * static_cast<double> (n) / rate));

        const std::vector<Event> events { { 0, [] (Chain& c)
        {
            const BandType types[] = { BandType::bell, BandType::lowShelf, BandType::highShelf };

            for (int b = 0; b < numBands; ++b)
            {
                auto s = trackBand (types[b % 3], 800.0 + 50.0 * b, 0.0, 500.0, 2000.0, 20.0);
                s.confidenceScalesGain = b % 2 == 0;
                s.gmode = b % 4 == 3 ? GainMode::dynamic : GainMode::staticGain;
                s.thresholdDb = 0.0;      // never crossed
                c.setBand (b, s);
            }
        } } };

        for (const auto& sizes : { fixedBlocks (64), fixedBlocks (1), randomBlocks (13) })
        {
            const auto output = render (rate, input, events, sizes);

            INFO (std::string (withSine ? "a sine in the key" : "noise"));

            for (std::size_t c = 0; c < input.size(); ++c)
                CHECK (sameBits (output[c], input[c]));
        }
    }
}

TEST_CASE_TEMPLATE ("exact bypass, return: Env and Track bands at 0 dB in Dynamic, a loud passage, then a key under the threshold: the input again, bit for bit", Sample, float, double)
{
    constexpr int loud = 24000, length = 72000;
    auto input = awkwardNoise<Sample> (length, 0.5);

    for (auto& channel : input)
        for (std::size_t n = static_cast<std::size_t> (loud); n < channel.size(); ++n)
            if (std::fpclassify (channel[n]) == FP_NORMAL)
                channel[n] = static_cast<Sample> (static_cast<double> (channel[n]) * 0.001);

    const std::vector<Event> events { { 0, [] (Chain& c)
    {
        for (int b = 0; b < numBands; ++b)
        {
            auto s = b % 2 == 0 ? envBand (BandType::bell, 0.0, 300.0, 3000.0, -50.0, 40.0, 1000.0)
                                : trackBand (BandType::highShelf, 2000.0, 0.0, 1000.0, 6000.0);
            s.gmode = GainMode::dynamic;
            s.thresholdDb = -30.0;
            s.rangeDb = -6.0;
            s.releaseMs = 50.0;
            c.setBand (b, s);
        }
    } } };

    // The Dynamic row's stated time: release x ln(|Range| / 0.0001 dB), plus one control step.
    const auto returned = loud + static_cast<int> (0.050 * std::log (6.0 / 1.0e-4) * rate) + controlStep;

    for (const auto& sizes : { fixedBlocks (64), randomBlocks (17) })
    {
        const auto output = render (rate, input, events, sizes);

        for (std::size_t c = 0; c < input.size(); ++c)
        {
            CHECK_FALSE (sameBits (output[c], input[c]));

            auto exact = true;

            for (auto n = static_cast<std::size_t> (returned); n < input[c].size(); ++n)
                exact = exact && std::memcmp (&output[c][n], &input[c][n], sizeof (Sample)) == 0;

            CHECK (exact);
        }
    }
}

//==============================================================================
TEST_CASE ("layouts, frequency modes: 1, 2, 6 and 64 channels unlinked, every channel exactly as if it ran alone")
{
    constexpr int length = 4000;

    const auto events = [] (bool isLinked)
    {
        return std::vector<Event> { { 0, [isLinked] (Chain& c)
        {
            c.setBand (0, envBand (BandType::bell, 6.0, 300.0, 3000.0, -50.0, 30.0, 1000.0));
            auto track = trackBand (BandType::highShelf, 2000.0, -4.0, 800.0, 6000.0, 10.0);
            track.gmode = GainMode::dynamic;
            track.thresholdDb = -30.0;
            c.setBand (3, track);
            auto scaled = trackBand (BandType::bell, 500.0, 9.0, 200.0, 1200.0, 10.0);
            scaled.confidenceScalesGain = true;
            c.setBand (5, scaled);
            c.setBand (6, trackBand (BandType::bell, 900.0, -6.0, 300.0, 3000.0, 10.0));    // excludes band 5, 3
            c.setChain ({ false, 0.0, isLinked });
        } } };
    };

    for (const auto channels : { 1, 2, 6, 64 })
    {
        std::vector<std::vector<double>> input;

        for (int c = 0; c < channels; ++c)
        {
            auto channel = noise<double> (length, static_cast<std::uint32_t> (300 + c), 0.05);
            const auto partial = tone<double> (length, 300.0 + 37.0 * c, 0.3 + 0.005 * c, rate);

            for (int n = 0; n < length; ++n)
                channel[static_cast<std::size_t> (n)] += partial[static_cast<std::size_t> (n)] * ((n + 61 * c) % 1500 < 700 ? 1.0 : 0.1);

            input.push_back (channel);
        }

        const auto together = render (rate, input, events (false), fixedBlocks (37));

        INFO (channels << " channels");
        CHECK_FALSE (sameBits (together[0], input[0]));

        for (int c = 0; c < channels; ++c)
        {
            const auto alone = render (rate, std::vector<std::vector<double>> { input[static_cast<std::size_t> (c)] },
                                       events (true), fixedBlocks (37));
            CHECK (sameBits (together[static_cast<std::size_t> (c)], alone[0]));
        }
    }
}

TEST_CASE ("channel linking, Track: linked, one frequency for both channels, from their mean; unlinked, each its own")
{
    const auto length = static_cast<int> (0.6 * rate);
    const auto left = tone<double> (length, 700.0, 0.5, rate), right = tone<double> (length, 1100.0, 0.5, rate);

    for (const auto isLinked : { true, false })
    {
        Chain chain;
        chain.prepare (rate, 2);
        chain.setChain ({ false, 0.0, isLinked });
        chain.setBand (0, trackBand (BandType::bell, 900.0, 6.0, 600.0, 1300.0, 20.0));

        auto a = left, b = right;
        double* pointers[] = { a.data(), b.data() };
        chain.process (pointers, 2, length);

        const auto l = chain.frequencyHz (0, 0), r = chain.frequencyHz (0, 1);
        MESSAGE (std::string (isLinked ? "linked: " : "unlinked: ") << l << " Hz and " << r << " Hz");

        if (isLinked)
        {
            CHECK (l == r);
            CHECK ((std::abs (cents (l, 700.0)) < 20.0 || std::abs (cents (l, 1100.0)) < 20.0));
        }
        else
        {
            CHECK (std::abs (cents (l, 700.0)) < 20.0);
            CHECK (std::abs (cents (r, 1100.0)) < 20.0);
        }
    }
}

TEST_CASE ("gain limit: confidence scaling the gain of a Track band at +-30 dB with offsets the same way never takes it past +-30 dB")
{
    const auto length = static_cast<int> (0.8 * rate);
    auto key = tone<double> (length, 1000.0, 0.5, rate);
    const auto hiss = noise<double> (length, 5, 0.3);

    for (std::size_t n = 0; n < key.size(); ++n)
        key[n] += hiss[n];

    for (const auto staticDb : { 30.0, -30.0 })
    {
        const auto sign = staticDb > 0.0 ? 1.0 : -1.0;
        auto s = trackBand (BandType::bell, 1000.0, staticDb, 500.0, 2000.0, 10.0);
        s.gmode = GainMode::dynamic;
        s.thresholdDb = -60.0;
        s.ratio = 20.0;
        s.rangeDb = 30.0 * sign;
        s.confidenceScalesGain = true;

        // The effective gain is read on the band's response at its own frequency, sample by sample.
        Chain chain;
        chain.prepare (rate, 1);
        chain.setBand (0, s);

        auto audio = key;
        double* p = audio.data();
        chain.process (&p, 1, length);

        // Through a band whose gain never passes 30 dB the output never passes the input by more than 30 dB.
        double peakIn = 0.0, peakOut = 0.0;

        for (std::size_t n = static_cast<std::size_t> (0.4 * rate); n < key.size(); ++n)
        {
            peakIn = std::max (peakIn, std::abs (key[n]));
            peakOut = std::max (peakOut, std::abs (audio[n]));
        }

        const auto db = 20.0 * std::log10 (peakOut / peakIn);
        MESSAGE ("static " << staticDb << " dB, Range " << 30.0 * sign << " dB, confidence scaling: the output peaks " << db
                           << " dB against the input");
        CHECK (db < 30.1);
    }
}

TEST_CASE ("denormals, frequency modes: signal, then silence, and never a subnormal sample out")
{
    constexpr int signal = 4800, length = 48000;
    std::vector<std::vector<double>> input { noise<double> (length, 7, 0.5), noise<double> (length, 8, 0.5) };

    for (auto& channel : input)
        std::fill (channel.begin() + signal, channel.end(), 0.0);

    const std::vector<Event> events { { 0, [] (Chain& c)
    {
        c.setBand (0, envBand (BandType::bell, 6.0, 300.0, 3000.0, -60.0, 40.0, 1000.0));
        c.setBand (1, trackBand (BandType::highShelf, 4000.0, -4.0, 2000.0, 8000.0));
        c.setBand (2, trackBand (BandType::lowPass, 8000.0, 0.0, 4000.0, 16000.0));
    } } };

    const auto output = render (rate, input, events, fixedBlocks (64));

    for (const auto& channel : output)
        for (const auto sample : channel)
            REQUIRE (std::fpclassify (sample) != FP_SUBNORMAL);
}

//==============================================================================
TEST_CASE ("follow on a moving band: a Dynamic cut on a Track band keeps its detector on a tone that glides; a Fixed band loses it")
{
    // A de-esser that follows: a tone gliding from 3 to 6 kHz, -6 dB, a band
    // cutting it from -10 dB over. Its detector follows the band: on the tone
    // while it tracks; two octaves off, at -16 dB, for a Fixed band at 3 kHz.
    const auto length = static_cast<int> (2.0 * rate);
    const auto key = glide (length, 3000.0, 6000.0, 0.5);

    const auto offsetAtEnd = [&key] (FrequencyMode fmode)
    {
        auto s = band (true, BandType::bell, 3000.0, 2.0, 0.0);
        s.fmode = fmode;
        s.lowHz = 2000.0;
        s.highHz = 9000.0;
        s.speedMs = 20.0;
        s.gmode = GainMode::dynamic;
        s.thresholdDb = -10.0;
        s.ratio = 20.0;
        s.rangeDb = -12.0;

        Chain chain;
        chain.prepare (rate, 1);
        chain.setBand (0, s);

        auto audio = key;
        double* p = audio.data();
        chain.process (&p, 1, length);
        return chain.offsetDb (0, 0);
    };

    const auto tracking = offsetAtEnd (FrequencyMode::track), fixed = offsetAtEnd (FrequencyMode::fixed);
    MESSAGE ("the tone at 6 kHz: the Track band cuts " << tracking << " dB, the Fixed band at 3 kHz " << fixed << " dB");
    CHECK (tracking < -3.5);
    CHECK (fixed > -0.1);
}

TEST_CASE ("sub-block, frequency modes: how far a band that tracks a fast glide lags the tracker, for information")
{
    // The band's g is the tracker's frequency one control step late, interpolated linearly across the step.
    const auto length = static_cast<int> (1.0 * rate);
    const auto key = glide (length, 500.0, 4000.0, 0.5);

    for (const auto speedMs : { 1.0, 10.0, 50.0 })
    {
        Chain chain;
        chain.prepare (rate, 1);
        chain.setBand (0, trackBand (BandType::bell, 500.0, 6.0, 300.0, 6000.0, speedMs));

        auto audio = key;
        double worst = 0.0, worstLag = 0.0;

        for (int n = 0; n < length; ++n)
        {
            double* p = audio.data() + n;
            chain.process (&p, 1, 1);

            if (n > length / 4)
            {
                const auto target = 500.0 * std::pow (8.0, static_cast<double> (n) / length);
                worst = std::max (worst, std::abs (cents (chain.frequencyHz (0, 0), target)));
                worstLag = std::max (worstLag, std::abs (cents (chain.appliedFrequencyHz (0, 0), chain.frequencyHz (0, 0))));
            }
        }

        MESSAGE ("a glide of three octaves in 1 s at Speed " << speedMs << " ms: the tracker within " << worst
                 << " cents of the tone, the band's filter within " << worstLag << " cents of the tracker");
    }
}
