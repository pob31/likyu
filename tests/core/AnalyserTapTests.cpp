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
    The analyser's tap (core/AnalyserTap.h): nothing while off, the mean of
    the channels while on, the latest samples in order, and never a read
    that mixes two passes of the ring.
*/

#include <doctest/doctest.h>

#include <core/AnalyserTap.h>

#include <atomic>
#include <thread>
#include <vector>

using namespace likyu;

TEST_CASE ("analyser tap: off writes nothing; on, the mean of the channels, the latest samples first to last")
{
    core::AnalyserTap tap;
    std::vector<float> left (100), right (100), out (100);

    for (int i = 0; i < 100; ++i)
    {
        left[static_cast<std::size_t> (i)] = static_cast<float> (i);
        right[static_cast<std::size_t> (i)] = static_cast<float> (3 * i);
    }

    const float* channels[] = { left.data(), right.data() };

    // Off, the default: nothing to read.
    CHECK_FALSE (tap.isEnabled());
    tap.write (core::AnalyserTap::input, channels, 2, 100);
    CHECK_FALSE (tap.read (core::AnalyserTap::input, out.data(), 1));

    tap.setEnabled (true);
    tap.write (core::AnalyserTap::input, channels, 2, 100);

    // Fewer written than asked for: no read.
    CHECK_FALSE (tap.read (core::AnalyserTap::input, out.data(), 101));

    REQUIRE (tap.read (core::AnalyserTap::input, out.data(), 10));
    for (int i = 0; i < 10; ++i)
        CHECK (out[static_cast<std::size_t> (i)] == static_cast<float> (2 * (90 + i)));

    // Doubles, and a stream of its own.
    std::vector<double> side (50, 0.25);
    const double* sideChannels[] = { side.data() };
    tap.write (core::AnalyserTap::sidechain, sideChannels, 1, 50);
    CHECK (tap.isPresent (core::AnalyserTap::sidechain));
    REQUIRE (tap.read (core::AnalyserTap::sidechain, out.data(), 50));
    CHECK (out[49] == 0.25f);

    // No channels: the stream is marked absent, and its samples stay as they were.
    tap.write<double> (core::AnalyserTap::sidechain, nullptr, 0, 50);
    CHECK_FALSE (tap.isPresent (core::AnalyserTap::sidechain));

    // Round the ring several times: still the latest, in order.
    std::vector<float> ramp (1000);
    const float* one[] = { ramp.data() };
    auto next = 0.0f;

    for (int block = 0; block < 100; ++block)
    {
        for (auto& sample : ramp)
            sample = next++;

        tap.write (core::AnalyserTap::input, one, 1, 1000);
    }

    std::vector<float> latest (static_cast<std::size_t> (core::AnalyserTap::capacity));
    REQUIRE (tap.read (core::AnalyserTap::input, latest.data(), core::AnalyserTap::capacity));
    CHECK (latest.back() == next - 1.0f);
    CHECK (latest.front() == next - static_cast<float> (core::AnalyserTap::capacity));

    // More than the ring holds is never read.
    CHECK_FALSE (tap.read (core::AnalyserTap::input, latest.data(), core::AnalyserTap::capacity + 1));
}

TEST_CASE ("analyser tap: a reader beside a writer gets the latest samples in order, or nothing")
{
    core::AnalyserTap tap;
    tap.setEnabled (true);

    std::atomic<bool> done { false };
    constexpr int block = 64, count = 8192;

    // A ramp, exact in float up to 2^24: any read is a run of consecutive values.
    std::thread writer ([&tap, &done]
    {
        std::vector<float> ramp (block);
        const float* one[] = { ramp.data() };
        auto next = 0.0f;

        while (next < 1.6e7f)
        {
            for (auto& sample : ramp)
                sample = next++;

            tap.write (core::AnalyserTap::input, one, 1, block);
        }

        done = true;
    });

    std::vector<float> out (count);
    auto reads = 0, broken = 0;

    while (! done)
    {
        if (! tap.read (core::AnalyserTap::input, out.data(), count))
            continue;

        ++reads;

        for (int i = 1; i < count; ++i)
            if (out[static_cast<std::size_t> (i)] != out[static_cast<std::size_t> (i - 1)] + 1.0f)
            {
                ++broken;
                break;
            }
    }

    writer.join();

    INFO (reads << " reads");
    CHECK (reads > 0);
    CHECK (broken == 0);
}
