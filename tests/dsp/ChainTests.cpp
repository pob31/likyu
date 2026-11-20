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
    The chain (PRD, Signal flow). Acceptance rows: exact bypass, block-size
    invariance, layouts and realtime (their DSP halves; the processor's are
    in tests/plugin/ProcessorTests.cpp), denormals (the deterministic half;
    the timing half is the bench runner's).
*/

#include <doctest/doctest.h>

#include "DspTestSupport.h"

#include <dsp/Chain.h>
#include <dsp/Denormals.h>
#include <dsp/rt/RtCheck.h>

#include <cmath>
#include <limits>
#include <string>

using namespace likyu::dsp;
using namespace likyutest::dsp;

namespace
{
    constexpr double rate = 48000.0;

    /** 7 x 4096: a block boundary for 1, 7, 64, 128, 256 and 4096 alike. */
    constexpr int period = 28672;

    std::vector<std::vector<double>> stereoNoise (int length, double amplitude = 0.5)
    {
        return { noise<double> (length, 11, amplitude), noise<double> (length, 12, amplitude) };
    }

    template <typename Sample>
    std::vector<std::vector<Sample>> as (const std::vector<std::vector<double>>& audio)
    {
        std::vector<std::vector<Sample>> out;

        for (const auto& channel : audio)
        {
            auto& converted = out.emplace_back();

            for (const auto sample : channel)
                converted.push_back (static_cast<Sample> (sample));
        }

        return out;
    }

    /** A scenario that touches everything the chain does: smoothing, every
        kind of switch, the output gain and the bypass, a change every period. */
    std::vector<Event> everything()
    {
        return {
            { 0, [] (Chain& c)
              {
                  c.setBand (0, band (true, BandType::highPass, 40.0, 0.707, 0.0));
                  c.setBand (1, band (true, BandType::lowShelf, 120.0, 0.8, 4.0));
                  c.setBand (2, band (true, BandType::bell, 400.0, 2.0, -6.0));
                  c.setBand (3, band (true, BandType::bell, 1500.0, 8.0, 9.0, QMode::constant));
                  c.setBand (4, band (true, BandType::notch, 3000.0, 4.0, 0.0));
                  c.setBand (5, band (true, BandType::highShelf, 8000.0, 0.7, -3.0));
                  c.setBand (6, band (false, BandType::bandPass, 5000.0, 1.0, 0.0));
                  c.setBand (7, band (true, BandType::lowPass, 18000.0, 0.707, 0.0));
                  c.setChain ({ false, -2.0 });
              } },
            { period, [] (Chain& c)
              {
                  c.setBand (2, band (true, BandType::bell, 2500.0, 0.5, 12.0));      // every ramp at once
                  c.setBand (5, band (true, BandType::highShelf, 4000.0, 2.0, 6.0));
                  c.setChain ({ false, 3.0 });
              } },
            { 2 * period, [] (Chain& c)
              {
                  c.setBand (1, band (true, BandType::highShelf, 120.0, 0.8, 4.0));   // type
                  c.setBand (3, band (true, BandType::bell, 1500.0, 8.0, 9.0));       // Q mode
                  c.setBand (4, band (false, BandType::notch, 3000.0, 4.0, 0.0));     // off
                  c.setBand (6, band (true, BandType::bandPass, 5000.0, 1.0, 0.0));   // on
              } },
            { 2 * period + 100, [] (Chain& c)
              {
                  c.setBand (1, band (true, BandType::lowPass, 120.0, 0.8, 4.0));     // turned round mid-fade
              } },
            { 3 * period, [] (Chain& c) { c.setChain ({ true, 3.0 }); } },
            { 4 * period, [] (Chain& c)
              {
                  c.setChain ({ false, 0.0 });
                  c.setBand (0, band (false, BandType::highPass, 40.0, 0.707, 0.0));
              } },
        };
    }

    /** Eight bells and shelves at 0 dB, both Q modes, various frequencies. */
    std::vector<Event> idleAtZero()
    {
        return { { 0, [] (Chain& c)
                   {
                       const BandType types[] = { BandType::bell, BandType::lowShelf, BandType::highShelf };

                       for (int b = 0; b < numBands; ++b)
                           c.setBand (b, band (true, types[b % 3], 50.0 * std::pow (2.0, b), 0.3 + b,
                                               0.0, b % 2 == 0 ? QMode::proportional : QMode::constant));
                   } } };
    }

    /** Noise with -0 and denormals sprinkled through it. */
    template <typename Sample>
    std::vector<std::vector<Sample>> awkwardNoise (int length)
    {
        auto audio = as<Sample> (stereoNoise (length));

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
TEST_CASE_TEMPLATE ("exact bypass: noise through 8 bell and shelf bands at 0 dB is the input, bit for bit", Sample, float, double)
{
    const auto input = awkwardNoise<Sample> (20000);

    for (const auto& sizes : { fixedBlocks (64), fixedBlocks (1), randomBlocks (5) })
    {
        const auto output = render (rate, input, idleAtZero(), sizes);

        for (std::size_t c = 0; c < input.size(); ++c)
            CHECK (sameBits (output[c], input[c]));
    }
}

TEST_CASE_TEMPLATE ("exact bypass: bells and shelves at 0 dB whose frequency and Q keep moving are the input, bit for bit", Sample, float, double)
{
    /*  Their mix is the identity whatever g and k are, so nothing of them is
        written, although the step is not constant: an Env or Track band at
        0 dB moves its g all the time (Phase 5). Written, 1 x + 0 v1 + 0 v2
        turns -0 into +0, and a denormal into 0 under denormals-are-zero. */
    const auto input = awkwardNoise<Sample> (20000);
    std::vector<Event> events = idleAtZero();

    for (int at = 1000; at < 20000; at += 1500)
        events.push_back ({ at, [at] (Chain& c)
                            {
                                const BandType types[] = { BandType::bell, BandType::lowShelf, BandType::highShelf };

                                for (int b = 0; b < numBands; ++b)
                                    c.setBand (b, band (true, types[b % 3], 60.0 * std::pow (2.0, b) * (1.0 + 0.0001 * at),
                                                        0.5 + 0.0003 * at, 0.0));
                            } });

    for (const auto& sizes : { fixedBlocks (64), fixedBlocks (1), randomBlocks (6) })
    {
        const auto output = render (rate, input, events, sizes);

        for (std::size_t c = 0; c < input.size(); ++c)
            CHECK (sameBits (output[c], input[c]));
    }
}

TEST_CASE ("exact bypass: a band moved away from 0 dB and back is the input again once its ramp ends")
{
    const auto input = stereoNoise (24000);
    const std::vector<Event> events {
        { 0, [] (Chain& c) { c.setBand (3, band (true, BandType::bell, 1000.0, 2.0, 0.0)); } },
        { 4800, [] (Chain& c) { c.setBand (3, band (true, BandType::bell, 1000.0, 2.0, 6.0)); } },
        { 9600, [] (Chain& c) { c.setBand (3, band (true, BandType::bell, 1000.0, 2.0, 0.0)); } },
    };

    const auto output = render (rate, input, events, fixedBlocks (100));

    // The ramp is 60 control steps at 48 kHz (20 ms); it lands at 9600 + 960.
    const auto landed = 9600 + rampSteps (smoothingSeconds, rate, controlStep) * controlStep;
    CHECK (landed == 10560);

    for (std::size_t c = 0; c < input.size(); ++c)
    {
        auto movedAtAll = false;

        for (int n = 4800; n < 9600; ++n)
            movedAtAll = movedAtAll || ! exactlyEqual (output[c][static_cast<std::size_t> (n)], input[c][static_cast<std::size_t> (n)]);

        CHECK (movedAtAll);

        for (int n = 0; n < 4800; ++n)
            REQUIRE (output[c][static_cast<std::size_t> (n)] == input[c][static_cast<std::size_t> (n)]);

        for (std::size_t n = static_cast<std::size_t> (landed); n < input[c].size(); ++n)
            REQUIRE (output[c][n] == input[c][n]);
    }
}

TEST_CASE ("exact bypass: an instance fully bypassed is the input, bit for bit, whatever its bands do")
{
    const auto input = awkwardNoise<float> (12000);
    auto events = everything();
    events.push_back ({ 0, [] (Chain& c) { c.setChain ({ true, 6.0 }); } });

    const auto output = render (rate, input, events, fixedBlocks (64));

    for (std::size_t c = 0; c < input.size(); ++c)
        CHECK (sameBits (output[c], input[c]));
}

//==============================================================================
TEST_CASE_TEMPLATE ("block-size invariance: the same output, bit for bit, however the host cuts its blocks", Sample, float, double)
{
    const auto input = as<Sample> (stereoNoise (5 * period));
    const auto reference = render (rate, input, everything(), fixedBlocks (64));

    // The reference did something: it is not the input.
    CHECK_FALSE (sameBits (reference[0], input[0]));

    struct Cut { std::string name; BlockSizes sizes; };
    const std::vector<Cut> cuts {
        { "1", fixedBlocks (1) },       { "7", fixedBlocks (7) },       { "128", fixedBlocks (128) },
        { "256", fixedBlocks (256) },   { "480", fixedBlocks (480) },   { "2000", fixedBlocks (2000) },
        { "4096", fixedBlocks (4096) }, { "random", randomBlocks (99) },
    };

    for (const auto& cut : cuts)
    {
        const auto output = render (rate, input, everything(), cut.sizes);

        INFO ("blocks of " << cut.name);

        for (std::size_t c = 0; c < input.size(); ++c)
            CHECK (sameBits (output[c], reference[c]));
    }
}

TEST_CASE ("block-size invariance: a change arriving anywhere inside a control step lands at its end")
{
    const auto input = stereoNoise (8000);

    const auto renderWithChangeAt = [&input] (int at)
    {
        const std::vector<Event> events {
            { 0, [] (Chain& c) { c.setBand (0, band (true, BandType::bell, 300.0, 1.0, 0.0)); } },
            { at, [] (Chain& c) { c.setBand (0, band (true, BandType::bell, 300.0, 1.0, 9.0)); } },
        };

        return render (rate, input, events, fixedBlocks (64));
    };

    // 4096 is a grid point: a change set before it lands there; one set at
    // 4097 to 4111 waits for 4112, as does one set at 4112.
    const auto atStepStart = renderWithChangeAt (4096);
    const auto justAfter = renderWithChangeAt (4097);
    const auto lateInStep = renderWithChangeAt (4111);
    const auto nextStep = renderWithChangeAt (4112);

    for (std::size_t c = 0; c < input.size(); ++c)
    {
        CHECK (sameBits (justAfter[c], lateInStep[c]));
        CHECK (sameBits (lateInStep[c], nextStep[c]));
        CHECK_FALSE (sameBits (atStepStart[c], justAfter[c]));
    }
}

//==============================================================================
TEST_CASE ("layouts: 1, 2, 6 and 64 channels, every channel exactly as if it ran alone")
{
    constexpr int length = 3000;
    auto events = everything();

    // The scenario compressed into the length: every switch and ramp still happens.
    for (auto& event : events)
        event.at = event.at / 64;

    for (const auto channels : { 1, 2, 6, 64 })
    {
        std::vector<std::vector<double>> input;

        for (int c = 0; c < channels; ++c)
            input.push_back (noise<double> (length, static_cast<std::uint32_t> (500 + c), 0.5));

        const auto together = render (rate, input, events, fixedBlocks (37));

        INFO (channels << " channels");

        for (int c = 0; c < channels; ++c)
        {
            const auto alone = render (rate, std::vector<std::vector<double>> { input[static_cast<std::size_t> (c)] },
                                       events, fixedBlocks (37));
            CHECK (sameBits (together[static_cast<std::size_t> (c)], alone[0]));
        }
    }
}

TEST_CASE ("layouts: channels beyond those prepared are left alone; zero samples, no channels and no preparation are harmless")
{
    Chain chain;
    chain.prepare (rate, 2);
    chain.setBand (0, band (true, BandType::bell, 1000.0, 1.0, 12.0));

    auto audio = stereoNoise (256);
    audio.push_back (noise<double> (256, 77));
    const auto input = audio;

    double* pointers[] = { audio[0].data(), audio[1].data(), audio[2].data() };
    chain.process (pointers, 3, 256);

    CHECK_FALSE (sameBits (audio[0], input[0]));
    CHECK (sameBits (audio[2], input[2]));

    chain.process (pointers, 3, 0);
    chain.process (pointers, 0, 256);
    chain.process (static_cast<double* const*> (nullptr), 2, 256);

    Chain unprepared;
    unprepared.setBand (0, band (true, BandType::bell, 1000.0, 1.0, 12.0));
    auto untouched = input;
    double* more[] = { untouched[0].data(), untouched[1].data() };
    unprepared.process (more, 2, 256);
    CHECK (sameBits (untouched[0], input[0]));
}

//==============================================================================
TEST_CASE ("prepare: the same rate and width again changes nothing; another rate starts over")
{
    const auto input = stereoNoise (6000);
    const std::vector<Event> settings { { 0, [] (Chain& c) { c.setBand (0, band (true, BandType::bell, 200.0, 4.0, 9.0)); } } };
    const auto uninterrupted = render (rate, input, settings, fixedBlocks (100));

    // The same, with a prepare at the same rate and width in the middle (Samplitude, Live, Bitwig).
    auto withPrepare = settings;
    withPrepare.push_back ({ 3000, [] (Chain& c) { c.prepare (rate, 2); } });
    const auto prepared = render (rate, input, withPrepare, fixedBlocks (100));

    for (std::size_t c = 0; c < input.size(); ++c)
        CHECK (sameBits (prepared[c], uninterrupted[c]));

    // A new rate clears the state: what follows is what a fresh chain at that rate would do.
    auto withNewRate = settings;
    withNewRate.push_back ({ 3000, [] (Chain& c) { c.prepare (96000.0, 2); } });
    const auto restarted = render (rate, input, withNewRate, fixedBlocks (100));

    std::vector<std::vector<double>> tail;
    for (const auto& channel : input)
        tail.emplace_back (channel.begin() + 3000, channel.end());

    const auto fresh = render (96000.0, tail, settings, fixedBlocks (100));

    for (std::size_t c = 0; c < input.size(); ++c)
        CHECK (std::equal (restarted[c].begin() + 3000, restarted[c].end(), fresh[c].begin(), fresh[c].end()));
}

TEST_CASE ("reset: the states clear and the targets are reached at once, as in a fresh chain")
{
    const auto input = stereoNoise (6000);
    std::vector<Event> events { { 0, [] (Chain& c) { c.setBand (2, band (true, BandType::lowShelf, 300.0, 0.7, -9.0)); } },
                                { 2000, [] (Chain& c) { c.setBand (2, band (true, BandType::lowShelf, 900.0, 0.7, 6.0)); } },
                                { 2003, [] (Chain& c) { c.reset(); } } };

    const auto withReset = render (rate, input, events, fixedBlocks (100));

    std::vector<std::vector<double>> tail;
    for (const auto& channel : input)
        tail.emplace_back (channel.begin() + 2003, channel.end());

    const auto fresh = render (rate, tail, { { 0, [] (Chain& c) { c.setBand (2, band (true, BandType::lowShelf, 900.0, 0.7, 6.0)); } } },
                               fixedBlocks (100));

    for (std::size_t c = 0; c < input.size(); ++c)
        CHECK (std::equal (withReset[c].begin() + 2003, withReset[c].end(), fresh[c].begin(), fresh[c].end()));
}

TEST_CASE ("prepare: settings given before the first block apply from its first sample, with no ramp from the defaults")
{
    Chain chain;
    chain.prepare (rate, 1);
    chain.setBand (0, band (true, BandType::bell, 1000.0, 2.0, 12.0));
    chain.setChain ({ false, -6.0 });

    std::vector<double> audio (64, 0.0);
    audio[0] = 1.0;
    double* pointer = audio.data();
    chain.process (&pointer, 1, 64);

    // The first sample of the impulse response: m0 + m1 a2 + m2 a3 for a bell is 1 + k (A^2 - 1) a2.
    Warp warp;
    warp.prepare (rate);
    const auto k = designBand (BandType::bell, QMode::proportional, warp.g (1000.0), 0.5, bandA (12.0), bandSqrtA (12.0));
    CHECK (audio[0] == doctest::Approx ((1.0 + k.m1 * k.a2) * dbToFactor (-6.0)).epsilon (1.0e-12));
}

//==============================================================================
TEST_CASE ("realtime: 500 blocks of every type and every switch allocate nothing")
{
    namespace rt = likyu::dsp::rt;
    REQUIRE (rt::isCounting());

    Chain chain;
    chain.prepare (96000.0, 2);

    auto left = noise<double> (64, 1), right = noise<double> (64, 2);
    double* channels[] = { left.data(), right.data() };
    Lcg random { 3 };

    rt::resetCounts();

    for (int block = 0; block < 500; ++block)
    {
        if (block % 5 == 0)
        {
            for (int b = 0; b < numBands; ++b)
                chain.setBand (b, band (random.unit() < 0.8, static_cast<BandType> (random.next() % numBandTypes),
                                        20.0 * std::pow (1000.0, random.unit()), 0.1 * std::pow (400.0, random.unit()),
                                        60.0 * random.unit() - 30.0, static_cast<QMode> (random.next() % numQModes)));

            chain.setChain ({ random.unit() < 0.2, 48.0 * random.unit() - 24.0 });
        }

        chain.process (channels, 2, 64);
    }

    CHECK (rt::regions() == 500);
    CHECK (rt::violations() == 0);
}

//==============================================================================
TEST_CASE ("denormals: flush-to-zero is on inside process and off again after it")
{
    const auto supported = []
    {
        const ScopedFlushToZero probe;
        return ScopedFlushToZero::isActive();
    }();

    if (! supported)
    {
        MESSAGE ("no flush-to-zero on this architecture");
        return;
    }

    REQUIRE_FALSE (ScopedFlushToZero::isActive());

    // 1e-307 is a normal double; at -24 dB it is 6.3e-309, a subnormal, unless flushed.
    Chain chain;
    chain.prepare (rate, 1);
    chain.setChain ({ false, -24.0 });

    std::vector<double> audio (64, 1.0e-307);
    double* pointer = audio.data();
    chain.process (&pointer, 1, 64);

    for (const auto sample : audio)
        REQUIRE (sample == 0.0);

    // Restored: the same product outside is a subnormal again.
    CHECK_FALSE (ScopedFlushToZero::isActive());
    volatile double tiny = 1.0e-307, factor = 0.063;
    CHECK (std::fpclassify (tiny * factor) == FP_SUBNORMAL);
}

TEST_CASE ("denormals: signal, then silence, and never a subnormal sample out")
{
    constexpr int signal = 4800, length = 48000;
    auto input = stereoNoise (length);

    for (auto& channel : input)
        std::fill (channel.begin() + signal, channel.end(), 0.0);

    const std::vector<Event> events { { 0, [] (Chain& c)
    {
        c.setBand (0, band (true, BandType::bell, 2000.0, 0.7, 6.0));
        c.setBand (1, band (true, BandType::highShelf, 5000.0, 0.7, -4.0));
        c.setBand (2, band (true, BandType::lowPass, 8000.0, 0.707, 0.0));
    } } };

    const auto output = render (rate, input, events, fixedBlocks (64));

    for (const auto& channel : output)
    {
        for (const auto sample : channel)
            REQUIRE (std::fpclassify (sample) != FP_SUBNORMAL);

        // Decayed through the subnormal range to exact silence.
        for (std::size_t n = length - 14400; n < channel.size(); ++n)
            REQUIRE (channel[n] == 0.0);
    }
}

//==============================================================================
/*  THE LOOKAHEAD IN THE CHAIN (Phase 4; PRD, Latency). Acceptance rows:
    constant latency (its DSP half: every combination of gain modes, types
    and keys, per lookahead, bypassed or not), exact bypass and block-size
    invariance again with a delay, and the lookahead effect. */
namespace
{
    /** `audio` delayed by `samples`, +0 before. */
    template <typename Sample>
    std::vector<std::vector<Sample>> delayed (const std::vector<std::vector<Sample>>& audio, int samples)
    {
        auto out = audio;

        for (std::size_t c = 0; c < audio.size(); ++c)
            for (std::size_t n = 0; n < audio[c].size(); ++n)
                out[c][n] = static_cast<int> (n) >= samples ? audio[c][n - static_cast<std::size_t> (samples)] : Sample {};

        return out;
    }

    /** The index of the first sample of channel 0 that is not exactly 0; -1 if none. */
    template <typename Sample>
    int firstNonZero (const std::vector<std::vector<Sample>>& audio)
    {
        for (std::size_t n = 0; n < audio[0].size(); ++n)
            if (std::fpclassify (audio[0][n]) != FP_ZERO)
                return static_cast<int> (n);

        return -1;
    }

    /** Every band in `gmode`, the types and keys mixed, one detector of its own. */
    void everyBandIn (Chain& c, GainMode gmode)
    {
        const BandType types[] = { BandType::bell, BandType::lowShelf, BandType::highShelf };

        for (int b = 0; b < numBands; ++b)
        {
            auto s = transientBand (types[b % 3], 60.0 * std::pow (2.0, b), 0.7 + 0.3 * b, b % 2 == 0 ? 6.0 : -6.0,
                                    b % 2 == 0 ? -100.0 : 100.0, 50.0, b % 2 == 0 ? -12.0 : 9.0);
            s.gmode = gmode;
            s.thresholdDb = -60.0;
            s.key = b % 3 == 1 ? KeySource::sidechain : KeySource::self;

            if (b == 5)
            {
                s.dfilt = DetectorFilter::own;
                s.dtype = DetectorType::highPass;
                s.detectorFrequency = 2000.0;
            }

            c.setBand (b, s);
        }
    }
}

TEST_CASE_TEMPLATE ("exact bypass with a lookahead: idle bands, or the instance bypassed, give the input delayed, bit for bit", Sample, float, double)
{
    const auto input = awkwardNoise<Sample> (20000);

    for (const auto r : { 48000.0, 96000.0 })
    {
        for (int choice = 0; choice < numLookaheadChoices; ++choice)
        {
            const auto d = lookaheadSamples (choice, r);
            const auto expected = delayed (input, d);

            auto idle = idleAtZero();
            idle.push_back ({ 0, [d] (Chain& c) { c.setLookahead (d); } });

            const std::vector<Event> bypassed { { 0, [d] (Chain& c)
            {
                c.setBand (0, band (true, BandType::bell, 1000.0, 2.0, 9.0));
                c.setBand (1, transientBand (BandType::lowShelf, 200.0, 0.7, -3.0, -100.0, 100.0, -12.0));
                c.setChain ({ true, 6.0 });
                c.setLookahead (d);
            } } };

            for (const auto& sizes : { fixedBlocks (64), randomBlocks (5) })
            {
                INFO ("rate " << r << ", lookahead " << d << " samples");
                const auto a = render (r, input, idle, sizes);
                const auto b = render (r, input, bypassed, sizes);

                for (std::size_t c = 0; c < input.size(); ++c)
                {
                    CHECK (sameBits (a[c], expected[c]));
                    CHECK (sameBits (b[c], expected[c]));
                }
            }
        }
    }
}

TEST_CASE ("constant latency, in the DSP: an impulse comes out `lookahead` samples late whatever the bands do, bypassed or not")
{
    constexpr int length = 2048;

    for (const auto r : { 44100.0, 96000.0 })
    {
        for (int choice = 0; choice < numLookaheadChoices; ++choice)
        {
            const auto d = lookaheadSamples (choice, r);

            for (const auto bypassed : { false, true })
            {
                // Each type alone, then all eight bands in each gain mode, both keys.
                for (int config = 0; config < numBandTypes + numGainModes; ++config)
                {
                    std::vector<std::vector<double>> impulse (2, std::vector<double> (length, 0.0));
                    impulse[0][0] = 1.0;
                    impulse[1][0] = 0.5;
                    const std::vector<std::vector<double>> sidechain { noise<double> (length, 81, 0.5) };

                    const std::vector<Event> events { { 0, [=] (Chain& c)
                    {
                        if (config < numBandTypes)
                            c.setBand (2, band (true, static_cast<BandType> (config), 1000.0, 2.0, 9.0));
                        else
                            everyBandIn (c, static_cast<GainMode> (config - numBandTypes));

                        c.setChain ({ bypassed, -1.0 });
                        c.setLookahead (d);
                    } } };

                    const auto output = render (r, impulse, events, fixedBlocks (64), sidechain);

                    INFO ("rate " << r << ", lookahead " << d << ", configuration " << config << ", bypassed " << bypassed);
                    CHECK (firstNonZero (output) == d);
                }
            }
        }
    }
}

TEST_CASE_TEMPLATE ("block-size invariance with a lookahead: the same output, bit for bit, and a change of the delay lands on the grid", Sample, float, double)
{
    auto events = everything();
    events.push_back ({ 0, [] (Chain& c)
    {
        everyBandIn (c, GainMode::transient);
        c.setBand (6, band (true, BandType::notch, 5000.0, 2.0, 0.0));
        c.setLookahead (lookaheadSamples (3, rate));
    } });
    events.push_back ({ period + 5, [] (Chain& c) { c.setLookahead (lookaheadSamples (1, rate)); } });    // mid-step
    events.push_back ({ 2 * period + 11, [] (Chain& c) { c.setLookahead (0); } });
    events.push_back ({ 3 * period + 3, [] (Chain& c) { c.setLookahead (lookaheadSamples (2, rate)); } });

    const auto input = awkwardNoise<Sample> (5 * period);
    const std::vector<std::vector<Sample>> sidechain { as<Sample> (stereoNoise (5 * period, 0.3))[0] };
    const auto reference = render (rate, input, events, fixedBlocks (64), sidechain);
    CHECK_FALSE (sameBits (reference[0], input[0]));

    struct Cut { std::string name; BlockSizes sizes; };
    const std::vector<Cut> cuts {
        { "1", fixedBlocks (1) }, { "7", fixedBlocks (7) }, { "128", fixedBlocks (128) },
        { "4096", fixedBlocks (4096) }, { "random", randomBlocks (77) },
    };

    for (const auto& cut : cuts)
    {
        const auto output = render (rate, input, events, cut.sizes, sidechain);

        INFO ("blocks of " << cut.name);

        for (std::size_t c = 0; c < input.size(); ++c)
            CHECK (sameBits (output[c], reference[c]));
    }
}

TEST_CASE ("lookahead effect: a burst through a Transient band with a negative attack: with 1 ms of lookahead its leading edge is reduced; without, it passes")
{
    constexpr double hz = 3000.0;
    constexpr int onset = 4800, length = 14400;

    // `choice` -1: a static bell already at the full cut, the most a lookahead can give.
    const auto leadingEdgeDb = [] (int choice, int samples)
    {
        const auto d = choice < 0 ? 0 : lookaheadSamples (choice, rate);
        std::vector<double> burst (static_cast<std::size_t> (length), 0.0);

        for (int n = onset; n < length; ++n)
            burst[static_cast<std::size_t> (n)] = 0.5 * std::sin (2.0 * pi * hz * (n - onset) / rate);

        const std::vector<Event> events { { 0, [d, choice] (Chain& c)
        {
            if (choice < 0)
                c.setBand (0, band (true, BandType::bell, hz, 1.0, -12.0));
            else
                c.setBand (0, transientBand (BandType::bell, hz, 1.0, 0.0, -100.0, 0.0, -12.0));

            c.setLookahead (d);
        } } };

        const auto output = render (rate, std::vector<std::vector<double>> { burst }, events, fixedBlocks (64))[0];

        // Energy, not the peak: a bell cut by 12 dB still lets a sudden sine
        // through for the few samples its band-pass takes to answer.
        double in = 0.0, out = 0.0;

        for (int n = 0; n < samples; ++n)
        {
            in += std::pow (burst[static_cast<std::size_t> (onset + n)], 2.0);
            out += std::pow (output[static_cast<std::size_t> (onset + d + n)], 2.0);
        }

        return 10.0 * std::log10 (out / in);
    };

    for (const auto ms : { 0.125, 0.25, 0.5, 1.0, 2.0 })
    {
        const auto samples = static_cast<int> (std::lround (ms * 0.001 * rate));
        const auto without = leadingEdgeDb (0, samples), with = leadingEdgeDb (2, samples), best = leadingEdgeDb (-1, samples);

        MESSAGE ("lookahead effect, the burst's first " << ms << " ms: " << without << " dB without lookahead, " << with
                 << " dB with 1 ms; a static bell at -12 dB, " << best << " dB");

        /*  With 1 ms the cut is in place when the burst arrives: the band is
            then the static bell at the full cut, which is the most any
            lookahead can give. Without, the edge gets through first. */
        CHECK (std::abs (with - best) < 0.01);
        CHECK (with < without - 1.5);

        if (ms < 0.2)
            CHECK (without > -1.0);
    }
}
