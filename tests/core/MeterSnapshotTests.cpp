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
    The meter snapshot (core/MeterSnapshot.h): one writer, one reader, never
    a torn read, never one going backwards.
*/

#include <doctest/doctest.h>

#include <core/MeterSnapshot.h>
#include <dsp/Math.h>

#include <atomic>
#include <thread>

using namespace likyu;

namespace
{
    /** Every field says the same number, so a mixture of two publications shows. */
    core::Meters stamped (std::uint64_t k)
    {
        core::Meters m;
        m.block = k;
        m.listening = static_cast<int> (k % 9) - 1;

        for (auto& band : m.bands)
        {
            band.offsetDb = static_cast<float> (k % 4096);
            band.frequencyHz = static_cast<float> (k % 4096) + 0.5f;
            band.confidence = static_cast<float> (k % 4096) + 0.25f;
            band.keyFellBack = k % 2 == 1;
        }

        return m;
    }

    bool consistent (const core::Meters& m)
    {
        const auto k = m.block;

        // Before the first publication, the defaults.
        if (k == 0)
        {
            for (const auto& band : m.bands)
                if (! (dsp::exactlyEqual (band.offsetDb, 0.0f) && dsp::exactlyEqual (band.frequencyHz, 0.0f)
                       && dsp::exactlyEqual (band.confidence, 0.0f) && ! band.keyFellBack))
                    return false;

            return m.listening == -1;
        }

        if (m.listening != static_cast<int> (k % 9) - 1)
            return false;

        for (const auto& band : m.bands)
            if (! (dsp::exactlyEqual (band.offsetDb, static_cast<float> (k % 4096))
                   && dsp::exactlyEqual (band.frequencyHz, static_cast<float> (k % 4096) + 0.5f)
                   && dsp::exactlyEqual (band.confidence, static_cast<float> (k % 4096) + 0.25f)
                   && band.keyFellBack == (k % 2 == 1)))
                return false;

        return true;
    }
}

TEST_CASE ("meter snapshot: the latest publication, once; the defaults before any")
{
    core::MeterSnapshot snapshot;
    core::Meters m;

    CHECK_FALSE (snapshot.read (m));
    CHECK (m.block == 0);
    CHECK (m.listening == -1);

    snapshot.publish (stamped (1));
    snapshot.publish (stamped (2));
    REQUIRE (snapshot.read (m));
    CHECK (m.block == 2);
    CHECK (consistent (m));

    // Nothing new: the same meters again, and false.
    CHECK_FALSE (snapshot.read (m));
    CHECK (m.block == 2);

    snapshot.publish (stamped (3));
    REQUIRE (snapshot.read (m));
    CHECK (m.block == 3);
}

TEST_CASE ("meter snapshot: a writer thread and a reader thread, never a torn read, never backwards")
{
    core::MeterSnapshot snapshot;
    std::atomic<bool> done { false };
    constexpr std::uint64_t publications = 200000;

    std::thread writer ([&]
    {
        for (std::uint64_t k = 1; k <= publications; ++k)
            snapshot.publish (stamped (k));

        done.store (true, std::memory_order_release);
    });

    std::uint64_t reads = 0, fresh = 0, last = 0;
    auto torn = false, backwards = false;
    core::Meters m;

    while (! done.load (std::memory_order_acquire) || reads < 1000)
    {
        if (snapshot.read (m))
            ++fresh;

        ++reads;
        torn = torn || ! consistent (m);
        backwards = backwards || m.block < last;
        last = m.block;
    }

    writer.join();

    // After the writer has finished, the last publication is what is read.
    snapshot.read (m);

    MESSAGE ("meter snapshot: " << reads << " reads, " << fresh << " of them fresh");
    CHECK_FALSE (torn);
    CHECK_FALSE (backwards);
    CHECK (m.block == publications);
}
