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
    THE METER SNAPSHOT (PRD, Detection: per band, the current gain offset,
    frequency and tracker confidence are published for the editor at about
    30 Hz; Realtime rules: the snapshot leaves the audio thread through a
    lock-free single-writer slot).

    A TRIPLE BUFFER. Three copies of the meters: the writer's, the reader's
    and one in the middle, whose index and a "fresh" bit sit in one atomic
    byte. The writer fills its copy and exchanges it with the middle one; the
    reader, when the middle is fresh, exchanges its copy for it. Each side
    only ever touches the copy it holds, so the payload is plain data and no
    read is torn; both sides are wait-free: one exchange each, no loop. A
    seqlock over plain fields would be a data race in C++, hence not that.

    ONE WRITER, the audio thread, once per block. ONE READER, the editor's
    timer (Phase 6) or a test. Standard library only.
*/

#include <dsp/Parameters.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace likyu::core
{
    struct BandMeters
    {
        float offsetDb = 0.0f;
        float frequencyHz = 0.0f;
        float confidence = 0.0f;
        bool keyFellBack = false;
    };

    struct Meters
    {
        std::uint64_t block = 0;        // counts the blocks published
        int listening = -1;             // the band Listen plays, from 0, or -1
        std::array<BandMeters, dsp::numBands> bands {};
    };

    class MeterSnapshot
    {
    public:
        /** The writer: the audio thread, after a block. */
        void publish (const Meters& meters) noexcept
        {
            slots[writing] = meters;
            const auto previous = middle.exchange (static_cast<std::uint8_t> (writing | fresh), std::memory_order_acq_rel);
            writing = static_cast<std::uint8_t> (previous & indexBits);
        }

        /** The reader: the latest meters published (the defaults before the
            first). True when they are newer than those of the last read. */
        bool read (Meters& out) noexcept
        {
            auto newer = false;

            if ((middle.load (std::memory_order_acquire) & fresh) != 0)
            {
                const auto previous = middle.exchange (reading, std::memory_order_acq_rel);
                reading = static_cast<std::uint8_t> (previous & indexBits);
                newer = true;
            }

            out = slots[reading];
            return newer;
        }

    private:
        static constexpr std::uint8_t indexBits = 3, fresh = 4;

        std::array<Meters, 3> slots {};
        std::uint8_t writing = 0;                   // the writer's own
        std::uint8_t reading = 1;                   // the reader's own
        std::atomic<std::uint8_t> middle { 2 };     // the third, and whether it is fresh
    };
}
