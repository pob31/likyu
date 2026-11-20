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
    THE DETECTOR (PRD, Detection): one TPT SVF per band per key channel,
    reading the key and never the audio between stages, and giving a POWER
    per sample, the square of the envelope, which dsp/FastLog.h turns into a
    level in dB.

    THE FILTER. Follow band: a band-pass at the band's current frequency and
    1/Q, unscaled: not the k / A of a proportional bell nor the g / sqrt(A) of
    a shelf, so the detector's bandwidth never depends on the offset it
    produces. Own: the band's dtype (band-pass, high-pass, low-pass) at its
    dfreq and dq. dsp/Band.h works out which, on the control grid.

    BAND-PASS: THE QUADRATURE ENVELOPE (PRD, corrected 2026-11-16, confirmed
    by the Phase 0 spike to 0.0001 dB). With hp = x - k v1 - v2, the high-pass
    output,

        env^2 = k^2 max(0, v1^2 - v2 hp)

    is the unity-peak band-pass magnitude of a sine at any frequency, with no
    ripple: in the TPT form LP HP = BP^2 holds exactly. With several
    components the cross terms can go negative for an instant, hence the max.

    HIGH-PASS AND LOW-PASS: A PEAK FOLLOWER on the output squared, instant
    attack, its release bounded to a few periods of the detector frequency
    (PRD): tau = followerPeriods / f, so the power falls as exp(-2 t / tau).
    Decided by the author 2026-11-20: as the PRD writes it, 4 periods, for
    high-pass and low-pass alike, to be judged by ear. A low-pass set far
    above its content ripples between the content's peaks; the band's own
    attack and release, on the offset, smooth what is left.

    PRECISION. Float, as the PRD decided for the detection path (eight lanes
    to an AVX2 register, Phase 7). States are kept by the caller,
    structure-of-arrays (dsp/Chain.h).

    Standard library only; plain arithmetic in the tick.
*/

#include "Svf.h"

#include <algorithm>
#include <cmath>

namespace likyu::dsp
{
    /** The parameter table's choices, in order (dsp/Parameters.cpp). */
    enum class KeySource { self, sidechain };
    inline constexpr int numKeySources = 2;

    enum class DetectorFilter { follow, own };
    inline constexpr int numDetectorFilters = 2;

    enum class DetectorType { bandPass, highPass, lowPass };
    inline constexpr int numDetectorTypes = 3;

    /** The peak follower's release, in periods of the detector frequency. */
    inline constexpr double followerPeriods = 4.0;

    /** One detector's coefficients, shared by every key channel of a band. */
    struct DetectorCoefficients
    {
        DetectorType type = DetectorType::bandPass;
        float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
        float k = 1.0f;
        float release = 0.0f;   // the peak follower's power factor per sample

        bool operator== (const DetectorCoefficients&) const noexcept = default;
    };

    /** For a warped frequency g = tan(pi f / fs), a damping k = 1 / Q, and
        the frequency itself in Hz for the follower's release. Computed in
        double and rounded once. */
    inline DetectorCoefficients makeDetectorCoefficients (DetectorType type, double g, double k,
                                                          double hz, double sampleRate) noexcept
    {
        const auto c = makeSvfCoefficients (g, k, 0.0, 0.0, 0.0);

        DetectorCoefficients d;
        d.type = type;
        d.a1 = static_cast<float> (c.a1);
        d.a2 = static_cast<float> (c.a2);
        d.a3 = static_cast<float> (c.a3);
        d.k = static_cast<float> (k);
        d.release = sampleRate > 0.0 && hz > 0.0
                        ? static_cast<float> (std::exp (-2.0 * hz / (followerPeriods * sampleRate)))
                        : 0.0f;
        return d;
    }

    /** One key sample through one detector: returns its power. `output` is
        the detector's own signal (band-pass at unity peak, high-pass or
        low-pass), which Listen plays. `peak` is the follower's state, unused
        by a band-pass. */
    inline float detectorTick (float x, float& s1, float& s2, float& peak,
                               const DetectorCoefficients& c, float& output) noexcept
    {
        const float v3 = x - s2;
        const float v1 = c.a1 * s1 + c.a2 * v3;
        const float v2 = s2 + c.a2 * s1 + c.a3 * v3;
        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;

        const float hp = x - c.k * v1 - v2;

        switch (c.type)
        {
            case DetectorType::bandPass:
                output = c.k * v1;
                return c.k * c.k * std::max (0.0f, v1 * v1 - v2 * hp);

            case DetectorType::highPass:
                output = hp;
                break;

            case DetectorType::lowPass:
                output = v2;
                break;
        }

        peak = std::max (output * output, peak * c.release);
        return peak;
    }
}
