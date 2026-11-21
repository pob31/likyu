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
    The kernels of lib/dsp (devplan, Phase 7), each held three ways:

      - on the target's registers and on the plain arrays of the scalar
        back-end, in this one executable: the same bits, after every
        stretch, whatever the stretches' lengths;
      - against the scalar code the kernel stands for, written out here a
        lane at a time from the functions it is made of;
      - and, outside this file, by the bits ledger, which a kernel that
        replaces a loop leaves as it was.
*/

#include <doctest/doctest.h>

#include <dsp/Band.h>
#include <dsp/Denormals.h>
#include <dsp/Detector.h>
#include <dsp/SeedBank.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

using namespace likyu::dsp;

namespace
{
    /** A key with everything in it: noise at several levels, silence, -0,
        denormals, a burst far over full scale, and partials for the regions
        to ring on. */
    std::vector<float> awkwardKey (int length, double rate)
    {
        std::vector<float> key (static_cast<std::size_t> (length));
        std::uint32_t state = 77;

        for (int n = 0; n < length; ++n)
        {
            state = state * 1664525u + 1013904223u;
            const auto noise = static_cast<double> (state >> 8) / 8388608.0 - 1.0;
            const auto section = (n * 16) / length;
            auto x = 0.3 * noise + 0.4 * std::sin (2.0 * pi * 440.0 * n / rate) + 0.2 * std::sin (2.0 * pi * 5300.0 * n / rate);

            if (section == 3)       x = 0.0;
            else if (section == 4)  x = -0.0;
            else if (section == 5)  x = 1.0e-41 * noise;        // denormal as a float
            else if (section == 9)  x *= 1.0e-6;
            else if (section == 12) x *= 300.0;

            key[static_cast<std::size_t> (n)] = static_cast<float> (x);
        }

        return key;
    }

    bool sameBank (const SeedBank& a, const SeedBank& b)
    {
        return std::memcmp (a.s1.data(), b.s1.data(), sizeof (a.s1)) == 0
               && std::memcmp (a.s2.data(), b.s2.data(), sizeof (a.s2)) == 0
               && std::memcmp (a.power.data(), b.power.data(), sizeof (a.power)) == 0;
    }

    /** The bank as dsp/Detector.h says it, a region at a time: what the kernel stands for. */
    void runBankRegionByRegion (SeedBank& bank, const float* key, int n, const SeedBankCoefficients& c, bool stepEnds)
    {
        for (int r = 0; r < c.count; ++r)
        {
            const auto index = static_cast<std::size_t> (r);
            DetectorCoefficients k;
            k.type = DetectorType::bandPass;
            k.a1 = c.a1[index];
            k.a2 = c.a2[index];
            k.a3 = c.a3[index];
            k.k = c.k[index];
            auto x1 = bank.s1[index], x2 = bank.s2[index], p = bank.power[index], peak = 0.0f, output = 0.0f;

            for (int i = 0; i < n; ++i)
                p = mulAdd (c.smoothing[index], detectorTick (key[i], x1, x2, peak, k, output) - p, p);

            const auto clean = [] (float& v)
            {
                if (! std::isfinite (v) || std::abs (v) < 1.0e-20f)
                    v = 0.0f;
            };

            if (stepEnds)
            {
                clean (x1);
                clean (x2);
                clean (p);
            }

            bank.s1[index] = x1;
            bank.s2[index] = x2;
            bank.power[index] = p;
        }
    }
}

TEST_CASE ("kernel, the seeding bank: registers, plain arrays and a region at a time give the same bits, after every stretch")
{
    for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0, 22050.0 })
    {
        SeedBankCoefficients coefficients;
        coefficients.prepare (rate);

        const auto length = 9000;
        const auto key = awkwardKey (length, rate);

        // As the chain runs it: flush-to-zero on, stretches that end on the 16-sample grid, of any length up to it.
        const ScopedFlushToZero flushToZero;
        SeedBank onRegisters, onArrays, byRegion;
        std::uint32_t state = 5;
        auto different = 0, notTheLoop = 0, stretches = 0;

        for (int done = 0, position = 0; done < length;)
        {
            state = state * 1664525u + 1013904223u;
            const auto n = std::min ({ 1 + static_cast<int> (state >> 28), controlStep - position, length - done });
            const auto stepEnds = position + n == controlStep;

            onRegisters.run<simd::native::F32x8> (key.data() + done, n, coefficients, stepEnds);
            onArrays.run<simd::scalar::F32x8> (key.data() + done, n, coefficients, stepEnds);
            runBankRegionByRegion (byRegion, key.data() + done, n, coefficients, stepEnds);

            different += sameBank (onRegisters, onArrays) ? 0 : 1;
            notTheLoop += sameBank (onArrays, byRegion) ? 0 : 1;
            ++stretches;

            position = (position + n) % controlStep;
            done += n;
        }

        INFO ("at " << rate << " Hz, " << coefficients.count << " regions, " << stretches << " stretches");
        CHECK (different == 0);
        CHECK (notTheLoop == 0);

        // And something came of it: the bank is not at rest, nor anything but numbers.
        auto loudest = 0.0f;
        auto finite = true;

        for (int r = 0; r < coefficients.count; ++r)
        {
            loudest = std::max (loudest, onRegisters.power[static_cast<std::size_t> (r)]);
            finite = finite && std::isfinite (onRegisters.power[static_cast<std::size_t> (r)]);
        }

        CHECK (loudest > 0.0f);
        CHECK (finite);
    }
}
