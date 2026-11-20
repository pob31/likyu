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
    THE ANALYSER'S TAP (PRD, Editor: the spectrum of the input and of the
    sidechain, computed off the audio thread from a lock-free tap; a display
    aid that never feeds the DSP; devplan 6f).

    TWO STREAMS, the input and the sidechain, each the mean of its channels,
    each a ring of floats that the audio thread writes and the editor reads
    the latest samples of. Nothing is written while the analyser is off,
    which is the default: the tap costs the audio thread one flag then.

    LOCK-FREE AND WITHOUT A DATA RACE. Every slot is an atomic float, written
    and read relaxed, and each stream's count of samples written is
    published with release and read with acquire. The reader takes the
    latest samples, then checks that the writer has not gone round the ring
    over them while it read; if it has, the read fails and the editor keeps
    its last picture. With a ring four times the longest read, that needs
    the editor to stall for most of a second.

    Allocated once, at construction; nothing after. Standard library only.
*/

#include <atomic>
#include <cstdint>
#include <memory>

namespace likyu::core
{
    class AnalyserTap
    {
    public:
        enum Stream { input, sidechain, numStreams };

        /** Samples kept per stream: a power of two, four times the longest read. */
        static constexpr int capacity = 32768;

        AnalyserTap()
            : slots (new std::atomic<float>[static_cast<std::size_t> (capacity * numStreams)])
        {
            for (int i = 0; i < capacity * numStreams; ++i)
                slots[static_cast<std::size_t> (i)].store (0.0f, std::memory_order_relaxed);
        }

        /** The editor's: on while the analyser is shown. */
        void setEnabled (bool shouldBeEnabled) noexcept     { enabled.store (shouldBeEnabled, std::memory_order_relaxed); }
        bool isEnabled() const noexcept                     { return enabled.load (std::memory_order_relaxed); }

        /** The audio thread's: `n` samples of the mean of `numChannels`
            channels; none (a sidechain absent or disabled) marks the stream
            silent. Nothing while the tap is off. */
        template <typename Sample>
        void write (Stream stream, const Sample* const* channels, int numChannels, int n) noexcept
        {
            if (! isEnabled() || n <= 0)
                return;

            auto& state = streams[stream];
            state.present.store (numChannels > 0 && channels != nullptr, std::memory_order_relaxed);

            if (numChannels <= 0 || channels == nullptr)
                return;

            const auto start = state.written.load (std::memory_order_relaxed);
            auto* ring = slots.get() + stream * capacity;
            const auto scale = 1.0f / static_cast<float> (numChannels);

            for (int i = 0; i < n; ++i)
            {
                auto sum = 0.0f;

                for (int c = 0; c < numChannels; ++c)
                    sum += static_cast<float> (channels[c][i]);

                ring[(start + static_cast<std::uint64_t> (i)) & mask].store (sum * scale, std::memory_order_relaxed);
            }

            state.written.store (start + static_cast<std::uint64_t> (n), std::memory_order_release);
        }

        /** The editor's: whether the stream had channels at its last write. */
        bool isPresent (Stream stream) const noexcept
        {
            return streams[stream].present.load (std::memory_order_relaxed);
        }

        /** The editor's: the latest `count` samples, oldest first. False, and
            `out` not to be used, when fewer have been written or the writer
            went round the ring over them during the read. */
        bool read (Stream stream, float* out, int count) const noexcept
        {
            if (count <= 0 || count > capacity)
                return false;

            const auto& state = streams[stream];
            const auto end = state.written.load (std::memory_order_acquire);

            if (end < static_cast<std::uint64_t> (count))
                return false;

            const auto start = end - static_cast<std::uint64_t> (count);
            const auto* ring = slots.get() + stream * capacity;

            for (int i = 0; i < count; ++i)
                out[i] = ring[(start + static_cast<std::uint64_t> (i)) & mask].load (std::memory_order_relaxed);

            // Overwritten while read: the writer is past the ring's length from the first sample read.
            return state.written.load (std::memory_order_acquire) - start <= static_cast<std::uint64_t> (capacity);
        }

    private:
        static constexpr std::uint64_t mask = capacity - 1;
        static_assert ((capacity & (capacity - 1)) == 0, "the ring's length is a power of two");

        struct State
        {
            std::atomic<std::uint64_t> written { 0 };
            std::atomic<bool> present { false };
        };

        std::unique_ptr<std::atomic<float>[]> slots;
        State streams[numStreams];
        std::atomic<bool> enabled { false };
    };
}
