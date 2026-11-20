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
        happens before the same sample in every rendering. With a
        `sidechain` (one vector per sidechain channel, as long as the audio)
        the chain is keyed by the audio itself and that sidechain; without,
        by the audio alone, through the overload a native client uses. */
    template <typename Sample>
    std::vector<std::vector<Sample>> render (double rate, std::vector<std::vector<Sample>> audio,
                                             std::vector<Event> events, const BlockSizes& sizes,
                                             const std::vector<std::vector<Sample>>& sidechain = {})
    {
        std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.at < b.at; });

        likyu::dsp::Chain chain;
        const auto channels = static_cast<int> (audio.size());
        chain.prepare (rate, channels);

        const auto length = static_cast<int> (audio.front().size());
        std::vector<Sample*> pointers (audio.size());
        std::vector<const Sample*> sidePointers (sidechain.size());
        std::size_t next = 0;

        const auto run = [&] (int position, int size)
        {
            for (std::size_t c = 0; c < audio.size(); ++c)
                pointers[c] = audio[c].data() + position;

            if (sidechain.empty())
            {
                chain.process (pointers.data(), channels, size);
                return;
            }

            for (std::size_t s = 0; s < sidechain.size(); ++s)
                sidePointers[s] = sidechain[s].data() + position;

            likyu::dsp::Key<Sample> key;
            key.self = pointers.data();
            key.selfChannels = channels;
            key.sidechain = sidePointers.data();
            key.sidechainChannels = static_cast<int> (sidechain.size());
            chain.process (pointers.data(), channels, key, size);
        };

        for (int position = 0; position < length;)
        {
            while (next < events.size() && events[next].at <= position)
                events[next++].apply (chain);

            auto size = sizes();

            if (size == 0)
            {
                run (position, 0);
                continue;
            }

            size = std::min (size, length - position);

            if (next < events.size())
                size = std::min (size, events[next].at - position);

            run (position, size);
            position += size;
        }

        return audio;
    }

    inline likyu::dsp::BandSettings band (bool on, likyu::dsp::BandType type, double hz, double q, double gainDb,
                                          likyu::dsp::QMode qmode = likyu::dsp::QMode::proportional)
    {
        return { on, type, qmode, hz, q, gainDb };
    }

    /** A band in Dynamic mode, its detector following it, keyed by the input. */
    inline likyu::dsp::BandSettings dynamicBand (likyu::dsp::BandType type, double hz, double q, double gainDb,
                                                 double thresholdDb, double rangeDb, double ratio = 3.0,
                                                 double attackMs = 5.0, double releaseMs = 120.0,
                                                 likyu::dsp::Direction direction = likyu::dsp::Direction::above)
    {
        auto s = band (true, type, hz, q, gainDb);
        s.gmode = likyu::dsp::GainMode::dynamic;
        s.thresholdDb = thresholdDb;
        s.rangeDb = rangeDb;
        s.ratio = ratio;
        s.attackMs = attackMs;
        s.releaseMs = releaseMs;
        s.direction = direction;
        return s;
    }

    /** A band in Transient mode, its detector following it, keyed by the input. */
    inline likyu::dsp::BandSettings transientBand (likyu::dsp::BandType type, double hz, double q, double gainDb,
                                                   double attackPercent, double sustainPercent, double rangeDb,
                                                   double time = 1.0, double floorDb = -60.0)
    {
        auto s = band (true, type, hz, q, gainDb);
        s.gmode = likyu::dsp::GainMode::transient;
        s.transientAttackPercent = attackPercent;
        s.transientSustainPercent = sustainPercent;
        s.rangeDb = rangeDb;
        s.transientTime = time;
        s.transientFloorDb = floorDb;
        return s;
    }

    /** A band in Env mode (Static gain), its detector its own band-pass at
        `detectorHz` so that its level is the key's level there. */
    inline likyu::dsp::BandSettings envBand (likyu::dsp::BandType type, double gainDb, double lowHz, double highHz,
                                             double sensitivityDb, double spanDb, double detectorHz,
                                             likyu::dsp::EnvDirection direction = likyu::dsp::EnvDirection::up,
                                             double attackMs = 5.0, double releaseMs = 120.0)
    {
        auto s = band (true, type, 1000.0, 2.0, gainDb);
        s.fmode = likyu::dsp::FrequencyMode::env;
        s.lowHz = lowHz;
        s.highHz = highHz;
        s.sensitivityDb = sensitivityDb;
        s.spanDb = spanDb;
        s.envDirection = direction;
        s.envAttackMs = attackMs;
        s.envReleaseMs = releaseMs;
        s.dfilt = likyu::dsp::DetectorFilter::own;
        s.dtype = likyu::dsp::DetectorType::bandPass;
        s.detectorFrequency = detectorHz;
        s.detectorQ = 0.5;
        return s;
    }

    /** A sine of peak `amplitude`. */
    template <typename Sample>
    std::vector<Sample> tone (int length, double hz, double amplitude, double rate, double phase = 0.0)
    {
        std::vector<Sample> out (static_cast<std::size_t> (length));

        for (int n = 0; n < length; ++n)
            out[static_cast<std::size_t> (n)] = static_cast<Sample> (amplitude * std::sin (2.0 * 3.14159265358979323846 * hz * n / rate + phase));

        return out;
    }
}
