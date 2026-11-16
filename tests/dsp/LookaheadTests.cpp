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
    The lookahead delay (PRD, Latency; acceptance rows "Constant latency" and
    "Block-size invariance", as far as Phase 0 has anything to delay).
*/

#include <doctest/doctest.h>

#include <dsp/Lookahead.h>
#include <dsp/Parameters.h>
#include <dsp/rt/RtCheck.h>

#include <cstdint>
#include <cstring>
#include <vector>

using namespace likyu::dsp;

namespace
{
    /*  Deterministic noise in [-1, 1): a linear congruential generator, so the
        same bits on every platform. */
    template <typename Sample>
    std::vector<Sample> noise (int length, std::uint32_t seed)
    {
        std::vector<Sample> out (static_cast<std::size_t> (length));

        for (auto& sample : out)
        {
            seed = seed * 1664525u + 1013904223u;
            sample = static_cast<Sample> (static_cast<double> (seed >> 8) / 8388608.0 - 1.0);
        }

        return out;
    }

    /*  Runs `signal` through the line in blocks of `blockSize`, one channel. */
    template <typename Sample>
    std::vector<Sample> run (Lookahead<Sample>& line, std::vector<Sample> signal, int blockSize)
    {
        const auto total = static_cast<int> (signal.size());

        for (int start = 0; start < total; start += blockSize)
        {
            Sample* channel = signal.data() + start;
            line.process (&channel, 1, std::min (blockSize, total - start));
        }

        return signal;
    }

    template <typename Sample>
    bool sameBits (const std::vector<Sample>& a, const std::vector<Sample>& b)
    {
        return a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (Sample)) == 0;
    }
}

TEST_CASE_TEMPLATE ("lookahead: a delay of 0 is the input, bit for bit", Sample, float, double)
{
    Lookahead<Sample> line;
    line.prepare (1, maxLookaheadSamples (192000.0));
    line.setDelay (0);

    const auto input = noise<Sample> (5000, 1);
    CHECK (sameBits (run (line, input, 64), input));
}

TEST_CASE_TEMPLATE ("lookahead: an impulse comes out exactly `delay` samples late", Sample, float, double)
{
    for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        for (int choice = 0; choice < numLookaheadChoices; ++choice)
        {
            const auto delay = lookaheadSamples (choice, rate);

            Lookahead<Sample> line;
            line.prepare (1, maxLookaheadSamples (rate));
            line.setDelay (delay);
            REQUIRE (line.getDelay() == delay);

            std::vector<Sample> input (2000, Sample {});
            input[10] = Sample (1);

            const auto output = run (line, input, 64);

            INFO ("rate " << rate << ", choice " << choice << ", delay " << delay);

            for (int n = 0; n < static_cast<int> (output.size()); ++n)
                REQUIRE (output[static_cast<std::size_t> (n)] == (n == 10 + delay ? Sample (1) : Sample (0)));
        }
    }
}

TEST_CASE_TEMPLATE ("lookahead: the output does not depend on how the blocks are cut", Sample, float, double)
{
    const auto input = noise<Sample> (10000, 7);
    std::vector<Sample> reference;

    for (const auto blockSize : { 1, 7, 64, 4096 })
    {
        Lookahead<Sample> line;
        line.prepare (1, 576);
        line.setDelay (144);

        const auto output = run (line, input, blockSize);

        if (reference.empty())
            reference = output;

        INFO ("block size " << blockSize);
        CHECK (sameBits (output, reference));
    }

    // And it is the input, 144 samples late, behind 144 samples of silence.
    for (std::size_t n = 0; n < reference.size(); ++n)
        REQUIRE (reference[n] == (n < 144 ? Sample (0) : input[n - 144]));
}

TEST_CASE ("lookahead: every channel is delayed alike and none reads another")
{
    constexpr int channels = 64;
    constexpr int length = 1024;
    constexpr int delay = 48;

    Lookahead<float> line;
    line.prepare (channels, 144);
    line.setDelay (delay);

    std::vector<std::vector<float>> signal;
    std::vector<float*> pointers;

    for (int c = 0; c < channels; ++c)
        signal.push_back (noise<float> (length, static_cast<std::uint32_t> (100 + c)));

    const auto input = signal;

    for (auto& channel : signal)
        pointers.push_back (channel.data());

    for (int start = 0; start < length; start += 100)
    {
        std::vector<float*> block;

        for (auto* p : pointers)
            block.push_back (p + start);

        line.process (block.data(), channels, std::min (100, length - start));
    }

    for (int c = 0; c < channels; ++c)
        for (int n = 0; n < length; ++n)
            REQUIRE (signal[static_cast<std::size_t> (c)][static_cast<std::size_t> (n)]
                     == (n < delay ? 0.0f : input[static_cast<std::size_t> (c)][static_cast<std::size_t> (n - delay)]));
}

TEST_CASE ("lookahead: the delay is clamped to what prepare allocated, and survives a zero-length block")
{
    Lookahead<float> line;
    line.prepare (1, 100);

    line.setDelay (5000);
    CHECK (line.getDelay() == 100);

    line.setDelay (-3);
    CHECK (line.getDelay() == 0);

    line.setDelay (10);
    float sample = 1.0f;
    float* channel = &sample;
    line.process (&channel, 1, 0);      // process with zero samples (PRD, Realtime rules)
    CHECK (sample == 1.0f);

    // Unprepared: leaves the audio alone.
    Lookahead<float> unprepared;
    unprepared.process (&channel, 1, 1);
    CHECK (sample == 1.0f);
}

TEST_CASE ("lookahead: turning it on mid-stream reads real history")
{
    Lookahead<float> line;
    line.prepare (1, 144);
    line.setDelay (0);

    const auto input = noise<float> (2000, 3);
    auto signal = input;

    float* first = signal.data();
    line.process (&first, 1, 1000);

    line.setDelay (48);
    float* second = signal.data() + 1000;
    line.process (&second, 1, 1000);

    for (std::size_t n = 1000; n < 2000; ++n)
        REQUIRE (signal[n] == input[n - 48]);
}

TEST_CASE ("lookahead: process allocates nothing")
{
    namespace rt = likyu::dsp::rt;

    // Zero means zero only when this executable's operator new is the counting one.
    REQUIRE (rt::isCounting());

    Lookahead<double> line;
    line.prepare (2, 576);
    line.setDelay (576);

    auto left = noise<double> (4096, 11);
    auto right = noise<double> (4096, 12);
    double* channels[] = { left.data(), right.data() };

    rt::resetCounts();

    for (int block = 0; block < 500; ++block)
        line.process (channels, 2, 64);

    CHECK (rt::regions() == 500);
    CHECK (rt::violations() == 0);
}

TEST_CASE ("realtime check: an allocation inside a region is counted")
{
    namespace rt = likyu::dsp::rt;
    REQUIRE (rt::isCounting());

    rt::resetCounts();

    {
        // Through a volatile pointer, so no optimiser elides the pair.
        static int* volatile escape = nullptr;

        const rt::ScopedRealtimeCheck region;
        escape = new int (1);               // the violation this test is about
        delete escape;
    }

    CHECK (rt::violations() >= 1);
    rt::resetCounts();
}
