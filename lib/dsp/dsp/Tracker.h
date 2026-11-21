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

    SEVERAL TRACKERS ON ONE KEY (PRD, Exclusion between Track bands): the
    Track bands that share a key source run together, sample by sample. Each
    adapts on the key less the components of the others that are locked or
    held, each component extracted from the raw key by a second SVF at its
    tracker's estimate (the same coefficients as its adapting filter): no
    delay, and no loop between trackers. Built 2026-11-20:

      - A TRACKER'S WEIGHT in the others' deflation (the PRD's lock_j) is 1
        while it is locked, and its confidence over the threshold, at most
        1, otherwise, smoothed over trackWeightMs. Confidence is measured
        over the whole key (decided by the author 2026-11-20), so with only
        the locked deflated, eight equal sines leave every tracker at 1/8,
        under any useful threshold, and none ever locks; graded, each
        tracker on its own sine removes part of it from the others, they
        all gain, and all lock.
      - THE FIRST KEEPS ITS COMPONENT. A lock is refused within
        trackSpacingCents of a tracker that is locked or held, and of two
        locked or held trackers that come that close, the one locked the
        more recently is released: the first to lock on a component keeps
        it, and the other, which deflation then leaves without it, is
        seeded elsewhere. Two trackers acquiring one sine together otherwise
        both locked, the second from 235 cents away while the first's
        extraction was still off centre, and the spacing then held them
        straddling it, 130 cents apart.

      - THE SPACING PUSHES ONLY THE RELEASED. A released tracker closer than
        trackSpacingCents to another is pushed away from it, at its own
        Speed; a tie goes to band order. A tracker that is locked or held is
        not pushed: a released one acquiring the same sine pushed the locked
        one 70 cents off it, before any onset.
    Identity comes from continuity, not from loudness: a tracker keeps its
    component when two trade levels. The seeding of released trackers is
    the chain's (dsp/SeedBank.h).

    ON THE GRID. Speed, the threshold and the Hold take effect at the next
    grid point; so does the confidence's smoothing, made from the estimate
    there. Low, High and the rest are the band's at the step's end.

    Standard library only. The trackers' states belong to the chain's lanes
    (dsp/Chain.h); the per-sample loop is here, so that it is inlined
    whatever the caller looks like.
*/

#include "Dynamics.h"
#include "Exp2.h"
#include "FastLog.h"
#include "Frequency.h"
#include "Math.h"
#include "Parameters.h"
#include "Tan.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace likyu::dsp
{
    /** The adapting and extraction filters' Q (Phase 0 spike: unbiased from 1 to 16). */
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

    /** A tracker's lock weight in the others' deflation moves over this (built 2026-11-20). */
    inline constexpr double trackWeightMs = 5.0;

    /** Two trackers closer than this repel (PRD: start at 2 semitones). */
    inline constexpr double trackSpacingCents = 200.0;

    /** Keeps the loop's normalisation finite on silence: far under any key. */
    inline constexpr float trackerEpsilon = 1.0e-20f;

    inline constexpr double lnOf2 = 0.69314718055994530942;

    enum class TrackPhase : std::uint8_t { released, locked, holding };

    /** One lane's tracker. */
    struct TrackerState
    {
        float s1 = 0.0f, s2 = 0.0f;         // the adapting filter
        float x1 = 0.0f, x2 = 0.0f;         // the extraction filter, on the raw key
        double estimate = 0.0;              // log2 Hz: where the notch sits
        double output = 0.0;                // log2 Hz: the band's frequency
        double inputPower = 0.0, notchPower = 0.0;
        double fastPower = 0.0;             // the input's power over one period
        double confidence = 0.0;
        double weight = 0.0;                // 0 to 1: how much the others remove of this one's component
        double smoothing = 0.0;             // the powers' one-pole, from the estimate at the last grid point
        double fastSmoothing = 1.0;         // the same over one period
        bool jumping = false;               // the input jumped, and the powers are following
        int holdLeft = 0;
        int confirmed = 0;                  // samples confident in a row, released
        int lockedFor = 0;                  // samples locked or held, saturating
        TrackPhase phase = TrackPhase::released;
        bool fresh = true;                  // start at rest
        bool atRest = true;                 // the output is the rest, exactly
    };

    /** What a band's trackers run with for one control step. */
    struct TrackerStep
    {
        double mu = 0.0;            // log2 Hz per sample per unit of normalised gradient
        double repel = 0.0;         // the spacing's one-pole, per sample
        double glide = 0.0;         // the output's one-pole coefficient
        double weight = 0.0;        // the lock weight's one-pole coefficient
        double threshold = 0.3;     // confidence, 0 to 1
        int holdSamples = 0;
        int confirmSamples = 0;
        double low = 0.0, high = 0.0, rest = 0.0;   // log2 Hz
    };

    /** A tracker's filter coefficients at its estimate, shared by its
        adapting and its extraction filter. */
    struct TrackerCoefficients
    {
        float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    };

    class TrackerComputer
    {
    public:
        static constexpr float k = static_cast<float> (1.0 / trackerQ);

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
                now.repel = samples > 0.0 ? 1.0 / samples : 0.0;
                now.glide = onePole (trackGlideFraction * speedMs, rate);
                now.weight = 1.0 - onePole (trackWeightMs, rate);
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

            const auto hz = warp.clamp (pow2 (s.estimate));
            const auto seconds = std::max (confidencePeriods / hz, confidenceMinMs * 0.001);
            s.smoothing = rate > 0.0 ? 1.0 - std::exp (-1.0 / (seconds * rate)) : 1.0;
            s.fastSmoothing = rate > 0.0 ? 1.0 - std::exp (-hz / rate) : 1.0;
        }

        /** The filter coefficients at the tracker's estimate now. */
        TrackerCoefficients coefficients (const TrackerState& s) const noexcept
        {
            const auto g = static_cast<float> (warp.g (pow2 (s.estimate)));
            TrackerCoefficients c;
            c.a1 = 1.0f / mulAdd (g, g + k, 1.0f);
            c.a2 = g * c.a1;
            c.a3 = g * c.a2;
            return c;
        }

        /** One sample of the extraction filter on the raw key: the unity-peak
            band-pass at the estimate, which the other trackers remove. */
        static float extract (float key, TrackerState& s, const TrackerCoefficients& c) noexcept
        {
            const auto v3 = key - s.x2;
            const auto v1 = mulAdd (c.a1, s.x1, c.a2 * v3);
            const auto v2 = mulAdd (c.a3, v3, mulAdd (c.a2, s.x1, s.x2));
            s.x1 = mulAdd (2.0f, v1, -s.x1);
            s.x2 = mulAdd (2.0f, v2, -s.x2);
            return k * v1;
        }

        /** One sample of the tracker on its input (the key, less the other
            trackers' components). It locks only when `mayLock()`: no other
            tracker of its key is locked or held within the spacing.

            `mayLock` is asked at one place, when a released tracker has been
            confident for long enough, and nowhere else: among eight trackers
            that is a scan of the seven others, and asked every sample of
            every tracker it was a fifth of the budget case (devplan, Phase
            7). It is asked before this tracker's estimate moves and reads
            nothing this call has written, so the answer is the one it had
            when the caller made it beforehand. */
        template <typename MayLock>
        void tick (float in, TrackerState& s, const TrackerCoefficients& c, MayLock&& mayLock) const noexcept
        {
            const auto v3 = in - s.s2;
            const auto v1 = mulAdd (c.a1, s.s1, c.a2 * v3);
            const auto v2 = mulAdd (c.a3, v3, mulAdd (c.a2, s.s1, s.s2));
            s.s1 = mulAdd (2.0f, v1, -s.s1);
            s.s2 = mulAdd (2.0f, v2, -s.s2);

            const auto bp = k * v1;
            const auto q = k * v2;
            const auto e = in - bp;

            // Confidence: 1 - notch power / input power; 0 on silence. A jump
            // of the input makes the two follow at one period, until they are
            // within trackSettledDb of the input's power over one period.
            const auto inPower = static_cast<double> (in) * static_cast<double> (in);
            const auto ePower = static_cast<double> (e) * static_cast<double> (e);
            s.fastPower = mulAdd (s.fastSmoothing, inPower - s.fastPower, s.fastPower);

            if (s.jumping)
                s.jumping = ! (s.fastPower * settledRatio >= s.inputPower && s.fastPower <= s.inputPower * settledRatio);
            else
                s.jumping = s.fastPower * jumpRatio < s.inputPower || s.fastPower > s.inputPower * jumpRatio;

            const auto smoothing = s.jumping ? s.fastSmoothing : s.smoothing;
            s.inputPower = mulAdd (smoothing, inPower - s.inputPower, s.inputPower);
            s.notchPower = mulAdd (smoothing, ePower - s.notchPower, s.notchPower);
            s.confidence = s.inputPower > static_cast<double> (powerFloor)
                               ? std::max (0.0, 1.0 - s.notchPower / s.inputPower) : 0.0;

            // The states.
            const auto confident = s.confidence >= now.threshold;

            switch (s.phase)
            {
                case TrackPhase::released:
                    s.confirmed = confident ? s.confirmed + 1 : 0;

                    if (s.confirmed > now.confirmSamples && mayLock())
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
                    {
                        s.phase = TrackPhase::locked;
                    }
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
            s.lockedFor = released ? 0 : std::min (s.lockedFor + 1, maxLockedFor);

            if (released || (s.phase == TrackPhase::locked && ! s.jumping))
            {
                const auto gradient = static_cast<double> (e * q) / static_cast<double> (mulAdd (bp, bp, q * q) + trackerEpsilon);
                s.estimate = std::clamp (mulAdd (-now.mu, gradient, s.estimate), now.low, now.high);
            }

            // The others remove this one's component: all of it while it is
            // locked, as much as its confidence says otherwise.
            const auto weight = s.phase == TrackPhase::locked ? 1.0
                                : now.threshold > 0.0 ? std::min (1.0, s.confidence / now.threshold) : 1.0;
            s.weight = mulAdd (now.weight, weight - s.weight, s.weight);

            // The band's frequency glides to the estimate, or back to rest.
            const auto target = released ? now.rest : s.estimate;
            s.output = mulAdd (now.glide, s.output - target, target);

            if (released && std::abs (s.output - now.rest) < restSnap)
                s.output = now.rest;

            s.atRest = released && exactlyEqual (s.output, now.rest);
        }

        /** Whether two estimates are closer than the spacing. */
        static bool tooClose (double a, double b) noexcept
        {
            return std::abs (a - b) < spacing;
        }

        /** Whether two estimates are far enough apart for the spacing to
            leave both alone: exactly when `repel` returns at once, and never
            when `tooClose` (it is false of an estimate that is not a number,
            which `repel` does not leave alone either). */
        static bool apart (double a, double b) noexcept
        {
            return std::abs (a - b) >= spacing;
        }

        /** Released at once, its estimate back at rest, to be seeded again. */
        void release (TrackerState& s) const noexcept
        {
            s.phase = TrackPhase::released;
            s.estimate = now.rest;
            s.confirmed = 0;
            s.lockedFor = 0;
        }

        /** The spacing: `s` pushed away from an estimate closer than
            trackSpacingCents, by its own rate. */
        void repel (TrackerState& s, double otherLogHz, bool below) const noexcept
        {
            const auto gap = std::abs (s.estimate - otherLogHz);

            if (gap >= spacing)
                return;

            const auto push = (spacing - gap) * now.repel;
            s.estimate = std::clamp (s.estimate + (below ? -push : push), now.low, now.high);
        }

        double getSpeedMs() const noexcept      { return speedMs; }

        /** The tan its coefficients are made with. */
        const Warp& getWarp() const noexcept    { return warp; }

        /** A setting waits for the next step to take it. */
        bool isPending() const noexcept         { return pending; }

    private:
        static constexpr double spacing = trackSpacingCents / 1200.0;
        static constexpr int maxLockedFor = 1 << 30;
        static constexpr double restSnap = trackRestCents / 1200.0;

        // 10^(dB / 10), written out: std::pow is not constexpr.
        static constexpr double jumpRatio = 10.0;           // trackJumpDb
        static constexpr double settledRatio = 1.2589254117941673;   // trackSettledDb
        static_assert (exactlyEqual (trackJumpDb, 10.0) && exactlyEqual (trackSettledDb, 1.0), "the ratios above are written for these");

        Warp warp;
        double rate = 0.0;
        double speedMs = 50.0, threshold = 0.3, holdMs = 200.0;
        bool pending = true;
        TrackerStep now;
    };

    /** One tracker of a group: its band's computer, its lane's state, and
        where its confidence goes, sample by sample (or nowhere). */
    struct TrackerMember
    {
        const TrackerComputer* computer = nullptr;
        TrackerState* state = nullptr;
        double* confidence = nullptr;
    };

    inline constexpr int maxTrackerGroup = 8;

    /** From this many members a group's front half runs in registers (runTrackerGroup). */
    inline constexpr int minLaneMembers = 3;

    /** `n` samples of the trackers that share one key (PRD: Track bands with
        the same key source exclude each other), in band order. Each adapts
        on the key less the components the others are locked or held on,
        each of those extracted from the raw key: symmetric deflation, no
        delay, no loop between them (PRD, decided 2026-11-16).

        THE MEMBERS ARE THE LANES of its front half (devplan, Phase 7): each
        sample, every member's coefficients at its estimate (2^x, tan, two
        divides) and its extraction filter on the key, eight members to the
        registers `V32`, four to `V64` (dsp/simd/Vec.h). A lane does what
        TrackerComputer::coefficients and ::extract do, operation for
        operation, so the registers, the plain arrays of the reference build,
        and the member-by-member loop this replaced give the same bits. The
        sum of the components stays in band order, a float at a time; `tick`,
        the state machine, stays a member at a time. A lane past the members
        repeats the last one, has a weight of 0, and is never read back.

        From three members (`minLaneMembers`); under that the registers do
        eight lanes' work for one or two, and measured 6 to 13 % slower than
        the loop a member at a time, which the group then keeps. Up to four,
        2^x and tan take one register of four doubles, not two. */
    template <typename V64 = simd::F64x4, typename V32 = simd::F32x8>
    inline void runTrackerGroup (const float* key, int n, TrackerMember* members, int count) noexcept
    {
        static_assert (V32::lanes == maxTrackerGroup && 2 * V64::lanes == maxTrackerGroup);

        count = std::min (count, maxTrackerGroup);

        if (count <= 0)
            return;

        constexpr auto lanes = static_cast<std::size_t> (maxTrackerGroup);
        std::array<float, lanes> a1 {}, a2 {}, a3 {}, component {}, x1 {}, x2 {}, weight {};
        std::array<double, lanes> estimate {};
        const auto estimateOf = [members, count] (int j) noexcept { return members[std::min (j, count - 1)].state->estimate; };

        // The members' tans are one when they were made for one rate, which a chain's always are.
        const auto& warp = members[0].computer->getWarp();
        auto oneWarp = true;

        for (int j = 1; j < count; ++j)
            oneWarp = oneWarp && members[j].computer->getWarp() == warp;

        const auto inLanes = oneWarp && count >= minLaneMembers;

        for (int j = 0; j < count; ++j)
        {
            x1[static_cast<std::size_t> (j)] = members[j].state->x1;
            x2[static_cast<std::size_t> (j)] = members[j].state->x2;
        }

        auto extractionX1 = V32::load (x1.data()), extractionX2 = V32::load (x2.data());
        const auto two = splat<V32> (2.0), one = splat<V32> (1.0), k = splat<V32> (TrackerComputer::k);

        for (int i = 0; i < n; ++i)
        {
            const auto x = key[i];

            auto removed = 0.0f;

            if (! inLanes)
            {
                // A member at a time: its coefficients, and its component of the key.
                for (int j = 0; j < count; ++j)
                {
                    const auto j0 = static_cast<std::size_t> (j);
                    auto& s = *members[j].state;
                    const auto c = members[j].computer->coefficients (s);
                    a1[j0] = c.a1;
                    a2[j0] = c.a2;
                    a3[j0] = c.a3;

                    if (count > 1)
                    {
                        component[j0] = static_cast<float> (s.weight) * TrackerComputer::extract (x, s, c);
                        removed += component[j0];
                    }
                }
            }
            else
            {
                // Every member's coefficients at its estimate (TrackerComputer::coefficients).
                const auto low = V64::make (estimateOf (0), estimateOf (1), estimateOf (2), estimateOf (3));
                const auto gLow = warp.gLanes (pow2Lanes (low));
                auto gHigh = gLow;

                if (count > V64::lanes)
                    gHigh = warp.gLanes (pow2Lanes (V64::make (estimateOf (4), estimateOf (5), estimateOf (6), estimateOf (7))));

                const auto g = narrow (gLow, gHigh);
                const auto c1 = one / mulAdd (g, g + k, one);
                const auto c2 = g * c1;
                const auto c3 = g * c2;
                c1.store (a1.data());
                c2.store (a2.data());
                c3.store (a3.data());

                // The extraction filters (TrackerComputer::extract), weighted, and their sum in band order.
                const auto in = splat<V32> (x);
                const auto v3 = in - extractionX2;
                const auto v1 = mulAdd (c1, extractionX1, c2 * v3);
                const auto v2 = mulAdd (c3, v3, mulAdd (c2, extractionX1, extractionX2));
                extractionX1 = mulAdd (two, v1, -extractionX1);
                extractionX2 = mulAdd (two, v2, -extractionX2);

                for (int j = 0; j < count; ++j)
                    weight[static_cast<std::size_t> (j)] = static_cast<float> (members[j].state->weight);

                (V32::load (weight.data()) * (k * v1)).store (component.data());

                for (int j = 0; j < count; ++j)
                    removed += component[static_cast<std::size_t> (j)];
            }

            for (int j = 0; j < count; ++j)
            {
                auto& m = members[j];
                const auto j0 = static_cast<std::size_t> (j);
                const auto input = count > 1 ? x - (removed - component[j0]) : x;

                // No lock beside one that is locked or held.
                const auto mayLock = [members, count, j]() noexcept
                {
                    const auto here = members[j].state->estimate;

                    for (int o = 0; o < count; ++o)
                    {
                        const auto& other = *members[o].state;

                        if (o != j && other.phase != TrackPhase::released && TrackerComputer::tooClose (here, other.estimate))
                            return false;
                    }

                    return true;
                };

                TrackerCoefficients c;
                c.a1 = a1[j0];
                c.a2 = a2[j0];
                c.a3 = a3[j0];
                m.computer->tick (input, *m.state, c, mayLock);

                if (m.confidence != nullptr)
                    m.confidence[i] = m.state->confidence;
            }

            /*  The spacing, between every pair; a tie goes to band order. Of
                two locked or held this close, the more recent lets go.

                With every pair apart, which is nearly always, the loop below
                does nothing: it releases only two that are too close, and
                `repel` returns at once from two that are apart. One pass
                over the estimates says so, and the loop is passed over. */
            auto allApart = true;

            for (int j = 0; j < count; ++j)
                estimate[static_cast<std::size_t> (j)] = members[j].state->estimate;

            for (int a = 0; a < count; ++a)
                for (int b = a + 1; b < count; ++b)
                    allApart = allApart && TrackerComputer::apart (estimate[static_cast<std::size_t> (a)], estimate[static_cast<std::size_t> (b)]);

            if (allApart)
                continue;

            for (int a = 0; a < count; ++a)
            {
                for (int b = a + 1; b < count; ++b)
                {
                    auto& sa = *members[a].state;
                    auto& sb = *members[b].state;

                    if (sa.phase != TrackPhase::released && sb.phase != TrackPhase::released
                        && TrackerComputer::tooClose (sa.estimate, sb.estimate))
                    {
                        if (sb.lockedFor <= sa.lockedFor)
                            members[b].computer->release (sb);
                        else
                            members[a].computer->release (sa);
                    }

                    // Only the released are pushed.
                    const auto aBelow = sa.estimate <= sb.estimate;
                    const auto ea = sa.estimate, eb = sb.estimate;

                    if (sa.phase == TrackPhase::released)
                        members[a].computer->repel (sa, eb, aBelow);

                    if (sb.phase == TrackPhase::released)
                        members[b].computer->repel (sb, ea, ! aBelow);
                }
            }
        }

        if (! inLanes)
            return;

        extractionX1.store (x1.data());
        extractionX2.store (x2.data());

        for (int j = 0; j < count; ++j)
        {
            members[j].state->x1 = x1[static_cast<std::size_t> (j)];
            members[j].state->x2 = x2[static_cast<std::size_t> (j)];
        }
    }
}
