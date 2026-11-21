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

    THE REGIONS ARE THE LANES (devplan, Phase 7): thirty filters that do not
    know of each other, on one signal. They are kept an array a term, a lane a
    region, padded to 32, and run eight to a register (dsp/simd/Vec.h), the
    sample loop inside. `run` is the kernel, written once over the register
    type; a lane does what dsp/Detector.h's band-pass tick does, operation
    for operation, so the registers, the plain arrays of the reference build
    and the loop a region this replaced give the same bits. A lane past the
    regions has coefficients of 0 and is never read.

    Standard library only. Float, the detection path's precision.
*/

#include "Detector.h"
#include "Exp2.h"
#include "FastLog.h"
#include "Tan.h"
#include "simd/Vec.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace likyu::dsp
{
    inline constexpr int numSeedRegions = 30;

    /** The regions as lanes: whole registers of eight. */
    inline constexpr int numSeedLanes = 32;

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
        // The band-pass detector of each region (dsp/Detector.h), an array a
        // term: a1 to a3, k, k squared, and the smoothing of its power.
        std::array<float, numSeedLanes> a1 {}, a2 {}, a3 {}, k {}, kSquared {}, smoothing {};
        int count = 0;      // the regions under min(20 kHz, 0.46 fs)

        void prepare (double sampleRate) noexcept
        {
            Warp warp;
            warp.prepare (sampleRate);
            count = 0;

            for (auto* term : { &a1, &a2, &a3, &k, &kSquared, &smoothing })
                term->fill (0.0f);

            for (int r = 0; r < numSeedRegions; ++r)
            {
                const auto hz = pow2 (seedRegionLogHz (r));

                if (hz > warp.getHighestFrequency())
                    break;

                const auto index = static_cast<std::size_t> (r);
                const auto detector = makeDetectorCoefficients (DetectorType::bandPass, warp.g (hz), 1.0 / seedRegionQ, hz, sampleRate);
                a1[index] = detector.a1;
                a2[index] = detector.a2;
                a3[index] = detector.a3;
                k[index] = detector.k;
                kSquared[index] = detector.k * detector.k;
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
        std::array<float, numSeedLanes> s1 {}, s2 {}, power {};

        void clear() noexcept
        {
            s1.fill (0.0f);
            s2.fill (0.0f);
            power.fill (0.0f);
        }

        /** `n` samples of the key through every region: the kernel, over the
            register type `V` (the build's own unless a test says otherwise).
            Tiny states are snapped to 0 when a control step ends, and only
            then, so that nothing depends on how the host cut its blocks. */
        template <typename V = simd::F32x8>
        void run (const float* key, int n, const SeedBankCoefficients& c, bool stepEnds) noexcept
        {
            const auto two = V::broadcast (2.0f), zero = V::broadcast (0.0f);

            for (int lane = 0; lane < c.count; lane += V::lanes)
            {
                const auto a1 = V::load (c.a1.data() + lane), a2 = V::load (c.a2.data() + lane), a3 = V::load (c.a3.data() + lane);
                const auto k = V::load (c.k.data() + lane), kSquared = V::load (c.kSquared.data() + lane);
                const auto smoothing = V::load (c.smoothing.data() + lane);
                auto x1 = V::load (s1.data() + lane), x2 = V::load (s2.data() + lane), p = V::load (power.data() + lane);

                for (int i = 0; i < n; ++i)
                {
                    // dsp/Detector.h's tick and its band-pass power, then the one-pole.
                    const auto x = V::broadcast (key[i]);
                    const auto v3 = x - x2;
                    const auto v1 = mulAdd (a1, x1, a2 * v3);
                    const auto v2 = mulAdd (a3, v3, mulAdd (a2, x1, x2));
                    x1 = mulAdd (two, v1, -x1);
                    x2 = mulAdd (two, v2, -x2);

                    const auto hp = mulAdd (-k, v1, x) - v2;
                    const auto tick = kSquared * max (zero, mulAdd (v1, v1, -(v2 * hp)));
                    p = mulAdd (smoothing, tick - p, p);
                }

                x1.store (s1.data() + lane);
                x2.store (s2.data() + lane);
                p.store (power.data() + lane);
            }

            if (! stepEnds)
                return;

            // A state that has decayed to nothing is nothing; one that is not finite is reset.
            const auto clean = [] (float& v)
            {
                if (! std::isfinite (v) || std::abs (v) < 1.0e-20f)
                    v = 0.0f;
            };

            for (int lane = 0; lane < numSeedLanes; ++lane)
            {
                const auto index = static_cast<std::size_t> (lane);
                clean (s1[index]);
                clean (s2[index]);
                clean (power[index]);
            }
        }

        /** Region `r`'s level now, in dB. */
        float levelDb (int r) const noexcept
        {
            return powerToDb (power[static_cast<std::size_t> (r)]);
        }
    };
}
