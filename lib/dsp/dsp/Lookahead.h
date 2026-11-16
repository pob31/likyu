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
    THE LOOKAHEAD DELAY (PRD, Latency): one delay for the whole instance,
    ahead of band 1, the only thing that gives the plugin latency. Detection
    reads the undelayed signals, so every detector sees the audio that much
    early.

    A whole number of samples, every channel the same. At a delay of 0 the
    output is the input, bit for bit: each sample is written to the line and
    read straight back.

    The line is written on every call whatever the delay, so that turning
    the lookahead on mid-stream reads real history and not what was left
    from the last time it was on.

    prepare() allocates for the longest delay; nothing after it does.
    Standard library only.
*/

#include "rt/RtCheck.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace likyu::dsp
{
    template <typename Sample>
    class Lookahead
    {
    public:
        /** Allocates. Not for the audio thread. */
        void prepare (int numChannelsToAllocate, int maxDelaySamples)
        {
            channels = std::max (0, numChannelsToAllocate);
            length = std::max (0, maxDelaySamples) + 1;
            line.assign (static_cast<std::size_t> (channels) * static_cast<std::size_t> (length), Sample {});
            writeIndex = 0;
            delay = std::min (delay, length - 1);
        }

        void reset() noexcept
        {
            std::fill (line.begin(), line.end(), Sample {});
            writeIndex = 0;
        }

        /** Clamped to what prepare() allocated. Takes effect on the next process(). */
        void setDelay (int samples) noexcept
        {
            delay = std::clamp (samples, 0, std::max (0, length - 1));
        }

        int getDelay() const noexcept           { return delay; }
        int getMaxDelay() const noexcept        { return std::max (0, length - 1); }
        int getNumChannels() const noexcept     { return channels; }

        /** In place. Channels beyond those prepared are left as they are. */
        void process (Sample* const* audio, int numChannels, int numSamples) noexcept LIKYU_AUDIO_THREAD
        {
            const rt::ScopedRealtimeCheck realtime;

            const auto used = std::min (numChannels, channels);

            if (used <= 0 || numSamples <= 0 || length <= 0)
                return;

            for (int channel = 0; channel < used; ++channel)
            {
                auto* const history = line.data() + static_cast<std::size_t> (channel) * static_cast<std::size_t> (length);
                auto* const samples = audio[channel];
                auto write = writeIndex;

                for (int n = 0; n < numSamples; ++n)
                {
                    history[write] = samples[n];

                    auto read = write - delay;

                    if (read < 0)
                        read += length;

                    samples[n] = history[read];

                    if (++write == length)
                        write = 0;
                }
            }

            writeIndex = (writeIndex + numSamples) % length;
        }

    private:
        std::vector<Sample> line;   // channel after channel, `length` samples each
        int channels = 0;
        int length = 0;             // max delay + 1
        int writeIndex = 0;
        int delay = 0;
    };
}
