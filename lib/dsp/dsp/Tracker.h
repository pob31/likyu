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
    TRACK (PRD, Frequency modes): the band follows the dominant sinusoidal
    component of its key inside [Low, High].

    THE ESTIMATOR, as the Phase 0 spike chose it (accepted provisionally by
    the author 2026-11-20): a frequency-locked loop on one TPT SVF, the
    gradient notch. With bp = k v1 (the unity-peak band-pass), q = k v2 (its
    quadrature) and e = x - bp (the notch output),

        log f  -=  mu e q / (bp^2 + q^2 + eps)        mu = k / (Speed fs)

    The spike wrote it in natural log; here the estimate is log2 Hz, so the
    step is divided by ln 2. Its measured time constant is Speed. The filter
    is retuned every sample at the estimate, and the estimate is clamped to
    [Low, High] every sample. Tracker Q is a constant, the spike's 4.

    CONFIDENCE = 1 - notch power / input power, the two smoothed over four
    periods of the estimate (spike: over Speed, a 40 dB drop takes 461 ms to
    show; over four periods, 35 ms), or over confidenceMinMs where that is
    longer. Under the floor of a level (-120 dB) it is 0: digital silence is
    not a sine. Built 2026-11-20:

      - THE FLOOR IN ms. Over four periods of 6 kHz (0.7 ms) the confidence
        of noise jitters across a 30 % threshold, and every crossing is a
        lock, a Hold that freezes the loop, and a release that starts it
        again from rest: a sine at 0 dB SNR took 0.8 to 1 s to lock at
        6 kHz, against 0.16 s at 220 Hz and 1 kHz.
      - A JUMP OF THE INPUT. Smoothed together, the two powers decay
        together after a sine stops, and confidence stays where it was for
        tau ln(SNR) (35 ms for 40 dB at 1 kHz, as the spike measured), while
        the loop slides on what is left: 400 cents in 40 ms. So when the
        input's power over one period moves more than trackJumpDb from the
        smoothed input power, both powers follow at one period, and a locked
        tracker's loop stops for as long, until the two are within
        trackSettledDb again: a sine that stops is seen within a few
        periods, and the loop does not move meanwhile.

    THE STATES. Locked at the Confidence threshold or above. Under it, the
    band HOLDS its frequency for the Hold time, and the adaptation is frozen,
    not only the band's frequency (spike: with nothing holding it, an
    estimate drifts after its sine goes; here, on noise, a free loop slides
    hundreds of cents in half a second); confidence coming back resumes the
    lock. After the Hold the tracker is RELEASED: its estimate starts again
    from the rest frequency, the band's Frequency clamped into [Low, High]
    (PRD, range rules), and the band glides back there. Released, the loop
    adapts again.

    A LOCK IS CONFIRMED (built 2026-11-20): released, a tracker locks once
    confidence has stayed at the threshold or above for trackConfirmFraction
    of Speed. At 0 dB SNR confidence crosses 30 % for an instant some 200
    cents short of the sine; locked there, the first dip held and froze the
    loop on its way, and a lock at 1 kHz took 380 ms instead of 170.

    THE OUTPUT GLIDES in log2 Hz toward the estimate (locked, holding), or
    the rest (released): a one-pole whose time constant is
    trackGlideFraction of Speed (built 2026-11-20), so a lock gained or lost
    is a glide, not a jump, and tracking speed is the same in every octave
    (PRD). Within trackRestCents of the rest it is the rest, exactly: a
    released band is then the band at its own frequency, Phase 2's band,
    bit for bit.

    PRECISION. The two SVFs are float, the detection path's precision (PRD,
    decided 2026-11-16); the estimate, the output and the powers are double,
    the lesson of Dynamic's offset: a float log2 near 10 has a step of about
    1e-6, and a Speed of 1 s at 192 kHz moves the estimate by about that
    much per sample, so a float estimate would stall.

    ON THE GRID. Speed, the threshold and the Hold take effect at the next
    grid point; so does the confidence's smoothing, made from the estimate
    there. Low, High and the rest are the band's at the step's end.

    Standard library only. The trackers' states belong to the chain's lanes
    (dsp/Chain.h); the per-sample loop is here, so that it is inlined
    whatever the caller looks like.
*/

#include "Dynamics.h"
#include "FastLog.h"
#include "Frequency.h"
#include "Math.h"
#include "Parameters.h"
#include "Tan.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace likyu::dsp
{
    /** The adapting filter's Q (Phase 0 spike: unbiased from 1 to 16). */
    inline constexpr double trackerQ = 4.0;

    /** Confidence's powers are smoothed over this many periods of the estimate (spike)... */
    inline constexpr double confidencePeriods = 4.0;

    /** ...or over this long, where that is longer (built 2026-11-20). */
    inline constexpr double confidenceMinMs = 5.0;

    /** The input's power over one period this far from its smoothed power is
        a jump: the powers follow at one period, a locked loop stops. */
    inline constexpr double trackJumpDb = 10.0;

    /** A jump ends once the two are this close again. */
    inline constexpr double trackSettledDb = 1.0;

    /** The output's time constant, as a fraction of Speed (built 2026-11-20, to be judged by ear). */
    inline constexpr double trackGlideFraction = 0.25;

    /** A released tracker locks once confident for this fraction of Speed (built 2026-11-20). */
    inline constexpr double trackConfirmFraction = 0.25;

    /** Within this of the rest, a released output is the rest. */
    inline constexpr double trackRestCents = 0.01;

    /** Keeps the loop's normalisation finite on silence: far under any key. */
    inline constexpr float trackerEpsilon = 1.0e-20f;

    inline constexpr double lnOf2 = 0.69314718055994530942;

    enum class TrackPhase : std::uint8_t { released, locked, holding };

    /** One lane's tracker. */
    struct TrackerState
    {
        float s1 = 0.0f, s2 = 0.0f;         // the adapting filter
        double estimate = 0.0;              // log2 Hz: where the notch sits
        double output = 0.0;                // log2 Hz: the band's frequency
        double inputPower = 0.0, notchPower = 0.0;
        double fastPower = 0.0;             // the input's power over one period
        double confidence = 0.0;
        double smoothing = 0.0;             // the powers' one-pole, from the estimate at the last grid point
        double fastSmoothing = 1.0;         // the same over one period
        bool jumping = false;               // the input jumped, and the powers are following
        int holdLeft = 0;
        int confirmed = 0;                  // samples confident in a row, released
        TrackPhase phase = TrackPhase::released;
        bool fresh = true;                  // start at rest
        bool atRest = true;                 // the output is the rest, exactly
    };

    /** What a band's trackers run with for one control step. */
    struct TrackerStep
    {
        double mu = 0.0;            // log2 Hz per sample per unit of normalised gradient
        double glide = 0.0;         // the output's one-pole coefficient
        double threshold = 0.3;     // confidence, 0 to 1
        int holdSamples = 0;
        int confirmSamples = 0;
        double low = 0.0, high = 0.0, rest = 0.0;   // log2 Hz
    };

    class TrackerComputer
    {
    public:
        void prepare (double sampleRate) noexcept
        {
            rate = sampleRate;
            warp.prepare (sampleRate);
            pending = true;
        }

        /** In the table's units, clamped to its ranges; a value that is not
            finite is ignored. Takes effect at the next grid point. */
        void setTarget (double speedMilliseconds, double confidencePercent, double holdMilliseconds) noexcept
        {
            if (std::isfinite (speedMilliseconds))
                speedMs = std::clamp (speedMilliseconds, minSpeedMs, maxSpeedMs);

            if (std::isfinite (confidencePercent))
                threshold = std::clamp (confidencePercent, 0.0, maxConfidencePercent) / 100.0;

            if (std::isfinite (holdMilliseconds))
                holdMs = std::clamp (holdMilliseconds, 0.0, maxHoldMs);

            pending = true;
        }

        /** At a grid point: the step's values, from the band's range and its
            own Frequency (log2 Hz) at the step's end. */
        void step (const FrequencyRange& range, double ownLogHz) noexcept
        {
            if (pending)
            {
                pending = false;
                const auto samples = speedMs * 0.001 * rate;
                now.mu = rate > 0.0 ? (1.0 / trackerQ) / (samples * lnOf2) : 0.0;
                now.glide = onePole (trackGlideFraction * speedMs, rate);
                now.threshold = threshold;
                now.holdSamples = static_cast<int> (std::lround (holdMs * 0.001 * rate));
                now.confirmSamples = static_cast<int> (std::lround (trackConfirmFraction * samples));
            }

            const auto [low, high] = range.atEnd();
            now.low = low;
            now.high = high;
            now.rest = std::clamp (ownLogHz, low, high);
        }

        const TrackerStep& current() const noexcept     { return now; }

        /** At a grid point, for one lane: the confidence's smoothing from the
            estimate there, and a fresh tracker put at rest. */
        void beginStep (TrackerState& s) const noexcept
        {
            if (s.fresh)
            {
                s.fresh = false;
                s.estimate = s.output = now.rest;
                s.atRest = true;
            }

            const auto hz = warp.clamp (std::exp2 (s.estimate));
            const auto seconds = std::max (confidencePeriods / hz, confidenceMinMs * 0.001);
            s.smoothing = rate > 0.0 ? 1.0 - std::exp (-1.0 / (seconds * rate)) : 1.0;
            s.fastSmoothing = rate > 0.0 ? 1.0 - std::exp (-hz / rate) : 1.0;
        }

        /** `n` samples of one lane's tracker on its input `x`: the state
            carried in `s`. When `confidence` is given, each sample's
            confidence is written there. */
        void run (const float* x, int n, TrackerState& s, double* confidence) const noexcept
        {
            constexpr auto k = static_cast<float> (1.0 / trackerQ);
            const auto snap = trackRestCents / 1200.0;
            const auto jumpRatio = std::pow (10.0, trackJumpDb / 10.0);
            const auto settledRatio = std::pow (10.0, trackSettledDb / 10.0);

            for (int i = 0; i < n; ++i)
            {
                const auto g = static_cast<float> (warp.g (std::exp2 (s.estimate)));
                const auto a1 = 1.0f / (1.0f + g * (g + k));
                const auto a2 = g * a1;
                const auto a3 = g * a2;

                const auto in = x[i];
                const auto v3 = in - s.s2;
                const auto v1 = a1 * s.s1 + a2 * v3;
                const auto v2 = s.s2 + a2 * s.s1 + a3 * v3;
                s.s1 = 2.0f * v1 - s.s1;
                s.s2 = 2.0f * v2 - s.s2;

                const auto bp = k * v1;
                const auto q = k * v2;
                const auto e = in - bp;

                // Confidence: 1 - notch power / input power; 0 on silence. A
                // jump of the input makes the two follow at one period.
                const auto inPower = static_cast<double> (in) * static_cast<double> (in);
                const auto ePower = static_cast<double> (e) * static_cast<double> (e);
                s.fastPower += s.fastSmoothing * (inPower - s.fastPower);

                // A jump starts at trackJumpDb apart and lasts until the two
                // are within trackSettledDb: the smoothed powers follow all
                // the way, not only back to within the jump.
                if (s.jumping)
                    s.jumping = ! (s.fastPower * settledRatio >= s.inputPower && s.fastPower <= s.inputPower * settledRatio);
                else
                    s.jumping = s.fastPower * jumpRatio < s.inputPower || s.fastPower > s.inputPower * jumpRatio;

                const auto jumped = s.jumping;
                const auto smoothing = jumped ? s.fastSmoothing : s.smoothing;
                s.inputPower += smoothing * (inPower - s.inputPower);
                s.notchPower += smoothing * (ePower - s.notchPower);
                s.confidence = s.inputPower > static_cast<double> (powerFloor)
                                   ? std::max (0.0, 1.0 - s.notchPower / s.inputPower) : 0.0;

                // The states.
                const auto confident = s.confidence >= now.threshold;

                switch (s.phase)
                {
                    case TrackPhase::released:
                        s.confirmed = confident ? s.confirmed + 1 : 0;

                        if (s.confirmed > now.confirmSamples)
                        {
                            s.phase = TrackPhase::locked;
                            s.confirmed = 0;
                        }
                        break;

                    case TrackPhase::locked:
                        if (! confident)
                        {
                            s.phase = TrackPhase::holding;
                            s.holdLeft = now.holdSamples;
                        }
                        break;

                    case TrackPhase::holding:
                        if (confident)
                            s.phase = TrackPhase::locked;
                        else if (--s.holdLeft < 0)
                        {
                            // Released: the estimate starts again from the rest.
                            s.phase = TrackPhase::released;
                            s.estimate = now.rest;
                        }
                        break;
                }

                // The loop: released, or locked while the input does not jump.
                const auto released = s.phase == TrackPhase::released;

                if (released || (s.phase == TrackPhase::locked && ! jumped))
                {
                    const auto gradient = static_cast<double> (e * q) / static_cast<double> (bp * bp + q * q + trackerEpsilon);
                    s.estimate = std::clamp (s.estimate - now.mu * gradient, now.low, now.high);
                }

                // The band's frequency glides to the estimate, or back to rest.
                const auto target = released ? now.rest : s.estimate;
                s.output = target + now.glide * (s.output - target);

                if (released && std::abs (s.output - now.rest) < snap)
                    s.output = now.rest;

                s.atRest = released && exactlyEqual (s.output, now.rest);

                if (confidence != nullptr)
                    confidence[i] = s.confidence;
            }
        }

        double getSpeedMs() const noexcept      { return speedMs; }

    private:
        Warp warp;
        double rate = 0.0;
        double speedMs = 50.0, threshold = 0.3, holdMs = 200.0;
        bool pending = true;
        TrackerStep now;
    };
}
