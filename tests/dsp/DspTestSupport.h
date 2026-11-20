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
    Helpers for likyu_dsp_tests: signals and a renderer that drives a chain
    the way a host does, block by block, with settings changed between
    blocks. Standard library only, like the suite.
*/

#include <dsp/Chain.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

namespace likyutest::dsp
{
    /** A linear congruential generator: the same bits on every platform. */
    struct Lcg
    {
        std::uint32_t state = 1;

        std::uint32_t next() noexcept
        {
            state = state * 1664525u + 1013904223u;
            return state;
        }

        /** In [-1, 1). */
        double bipolar() noexcept
        {
            return static_cast<double> (next() >> 8) / 8388608.0 - 1.0;
        }

        /** In [0, 1). */
        double unit() noexcept
        {
            return static_cast<double> (next() >> 8) / 16777216.0;
        }
    };

    template <typename Sample>
    std::vector<Sample> noise (int length, std::uint32_t seed, double amplitude = 1.0)
    {
        Lcg random { seed };
        std::vector<Sample> out (static_cast<std::size_t> (length));

        for (auto& sample : out)
            sample = static_cast<Sample> (amplitude * random.bipolar());

        return out;
    }

    template <typename Sample>
    bool sameBits (const std::vector<Sample>& a, const std::vector<Sample>& b)
    {
        return a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (Sample)) == 0;
    }

    /** Something done to the chain before the sample `at`. */
    struct Event
    {
        int at;
        std::function<void (likyu::dsp::Chain&)> apply;
    };

    /** The next block's length: a fixed size, or anything a host might send. */
    using BlockSizes = std::function<int()>;

    inline BlockSizes fixedBlocks (int size)
    {
        return [size] { return size; };
    }

    /** Lengths from 0 to 600, zeros included. */
    inline BlockSizes randomBlocks (std::uint32_t seed)
    {
        return [random = Lcg { seed }]() mutable { return static_cast<int> (random.next() % 601u); };
    }

    /** Runs `input` (one vector per channel) through a fresh chain, cutting
        blocks with `sizes` and also at every event, so that each event
        happens before the same sample in every rendering. */
    template <typename Sample>
    std::vector<std::vector<Sample>> render (double rate, std::vector<std::vector<Sample>> audio,
                                             std::vector<Event> events, const BlockSizes& sizes)
    {
        std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.at < b.at; });

        likyu::dsp::Chain chain;
        const auto channels = static_cast<int> (audio.size());
        chain.prepare (rate, channels);

        const auto length = static_cast<int> (audio.front().size());
        std::vector<Sample*> pointers (audio.size());
        std::size_t next = 0;

        for (int position = 0; position < length;)
        {
            while (next < events.size() && events[next].at <= position)
                events[next++].apply (chain);

            auto size = sizes();

            if (size == 0)
            {
                for (std::size_t c = 0; c < audio.size(); ++c)
                    pointers[c] = audio[c].data() + position;

                chain.process (pointers.data(), channels, 0);
                continue;
            }

            size = std::min (size, length - position);

            if (next < events.size())
                size = std::min (size, events[next].at - position);

            for (std::size_t c = 0; c < audio.size(); ++c)
                pointers[c] = audio[c].data() + position;

            chain.process (pointers.data(), channels, size);
            position += size;
        }

        return audio;
    }

    inline likyu::dsp::BandSettings band (bool on, likyu::dsp::BandType type, double hz, double q, double gainDb,
                                          likyu::dsp::QMode qmode = likyu::dsp::QMode::proportional)
    {
        return { on, type, qmode, hz, q, gainDb };
    }
}
