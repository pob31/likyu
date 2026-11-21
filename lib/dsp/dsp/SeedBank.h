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
    THE SEEDING BANK (PRD, Exclusion between Track bands: seeding). The
    tracker's adaptation is local, so a released tracker is re-seeded
    rather than left to crawl: a bank of third-octave band-pass envelopes
    per key source (detector path only) names the strongest region inside
    the band's range that no other tracker of the key has claimed.

    THIRTY REGIONS, the ISO third octaves from 25 Hz to 20 kHz (1 kHz times
    2^(m/3)), each a band-pass detector at Q 4.32 (a third of an octave
    between its -3 dB points) reading the quadrature envelope
    (dsp/Detector.h), its power smoothed over four periods of its centre or
    5 ms, whichever is longer. Regions above min(20 kHz, 0.46 fs) are left
    out.

    CLAIMS AND SEEDS, at each grid point (dsp/Chain.h, seedTrackers): a
    region is claimed by every tracker of the key whose estimate is in it,
    the locked and held first, then the released ones in band order, each
    claiming the region it ends in. A released tracker moves to the
    strongest unclaimed region in its range when that one is louder than its
    own by seedHysteresisDb, or when its own is claimed by another: so two
    released trackers on two equal components take one each, in band order,
    and a released tracker does not hop between regions of noise.

    Standard library only. Float, the detection path's precision.
*/

#include "Detector.h"
#include "Exp2.h"
#include "FastLog.h"
#include "Tan.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace likyu::dsp
{
    inline constexpr int numSeedRegions = 30;

    /** A released tracker leaves its region for one this much louder (built 2026-11-20). */
    inline constexpr double seedHysteresisDb = 6.0;

    /** A third of an octave between the -3 dB points: Q = 1 / (2^(1/6) - 2^(-1/6)). */
    inline constexpr double seedRegionQ = 4.318473046963146;

    /** The centre of region `r`, in log2 Hz: 1 kHz times 2^((r - 16) / 3), 25 Hz to 20 kHz. */
    constexpr double seedRegionLogHz (int r) noexcept
    {
        return 9.965784284662087 + static_cast<double> (r - 16) / 3.0;   // log2 (1000) = 9.9658
    }

    /** The bank's coefficients, for a rate; shared by every bank. */
    struct SeedBankCoefficients
    {
        std::array<DetectorCoefficients, numSeedRegions> detector {};
        std::array<float, numSeedRegions> smoothing {};
        int count = 0;      // the regions under min(20 kHz, 0.46 fs)

        void prepare (double sampleRate) noexcept
        {
            Warp warp;
            warp.prepare (sampleRate);
            count = 0;

            for (int r = 0; r < numSeedRegions; ++r)
            {
                const auto hz = pow2 (seedRegionLogHz (r));

                if (hz > warp.getHighestFrequency())
                    break;

                const auto index = static_cast<std::size_t> (r);
                detector[index] = makeDetectorCoefficients (DetectorType::bandPass, warp.g (hz), 1.0 / seedRegionQ, hz, sampleRate);
                const auto seconds = std::max (4.0 / hz, 0.005);
                smoothing[index] = sampleRate > 0.0 ? static_cast<float> (1.0 - std::exp (-1.0 / (seconds * sampleRate))) : 1.0f;
                count = r + 1;
            }
        }

        /** The region nearest a log2 frequency, among those there are. */
        int regionOf (double logHz) const noexcept
        {
            const auto r = static_cast<int> (std::lround ((logHz - seedRegionLogHz (16)) * 3.0)) + 16;
            return std::clamp (r, 0, std::max (0, count - 1));
        }
    };

    /** One key's bank. */
    struct SeedBank
    {
        std::array<float, numSeedRegions> s1 {}, s2 {}, power {};

        void clear() noexcept
        {
            s1.fill (0.0f);
            s2.fill (0.0f);
            power.fill (0.0f);
        }

        /** `n` samples of the key through every region. Tiny states are
            snapped to 0 when a control step ends, and only then, so that
            nothing depends on how the host cut its blocks. */
        void run (const float* key, int n, const SeedBankCoefficients& c, bool stepEnds) noexcept
        {
            for (int r = 0; r < c.count; ++r)
            {
                const auto index = static_cast<std::size_t> (r);
                const auto& k = c.detector[index];
                const auto a = c.smoothing[index];
                auto x1 = s1[index], x2 = s2[index], p = power[index], peak = 0.0f, output = 0.0f;

                for (int i = 0; i < n; ++i)
                    p = mulAdd (a, detectorTick (key[i], x1, x2, peak, k, output) - p, p);

                // A state that has decayed to nothing is nothing; one that is not finite is reset.
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
                s1[index] = x1;
                s2[index] = x2;
                power[index] = p;
            }
        }

        /** Region `r`'s level now, in dB. */
        float levelDb (int r) const noexcept
        {
            return powerToDb (power[static_cast<std::size_t> (r)]);
        }
    };
}
