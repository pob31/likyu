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
    THE TRANSIENT SHAPER (PRD, Gain modes: Transient). Threshold-free
    differential envelopes in dB, per lane, on the lane's level L:

        x          = max(L, floor)
        attackSig  = max(0, E_fast - E_slowAttack)
        sustainSig = max(0, E_slowRelease - E_fast)
        offset     = clamp(attack attackSig + sustain sustainSig, -|Range|, +|Range|)
                     * gate(E_fast)

    THREE ENVELOPES, NOT FOUR (decided by the author 2026-11-20). The PRD's
    attack pair shares "the same release" and its sustain pair "the same
    fast attack"; the author read the first as the fast release. The attack
    pair's fast envelope (fast attack, fast release) is then the sustain
    pair's fast one too, whatever the four constants are, so three one-poles
    run, each a time constant in dB (63 % of a step after it):

        E_fast         fast attack, fast release
        E_slowAttack   slow attack, fast release
        E_slowRelease  fast attack, slow release

    Time scales the four constants together. Keeping the two fast envelopes
    apart would take a fifth constant.

    THE FLOOR. The envelopes run on max(L, floor), not on L: measured from
    -120 dB, the first note after a silence would hold its attack signal far
    longer than the notes that follow (PRD). The GATE reads E_fast, not L
    sample by sample (decided by the author 2026-11-20): on a noise key the
    band-pass level dips to the floor for an instant now and then (Phase 3),
    and a gate on L would blip the gain there. It is a smoothstep from the
    floor to the floor + transientGateWidthDb, so nothing moves while E_fast
    is at the floor.

    RETURN TO ZERO (PRD, "in Dynamic and in Transient"): an offset within
    0.0001 dB of 0 is exactly 0, so an idle band is again the exact bypass.

    THE ENVELOPES ARE DOUBLE, like Dynamic's offset: in float a one-pole
    stops moving once its step is under half a unit in the last place, and
    two envelopes stalled at different places leave a sustain signal that
    never returns to 0. A lane's envelopes start at the first level they
    read, so a switch that rebuilds the lanes with the detector already warm
    (a link change, Dynamic to Transient) does not fire a false attack.

    ON THE GRID. The two amounts and the floor ramp over 20 ms, like the
    band's own gain, interpolated linearly inside a control step; Time and
    the constants only change rates and take effect at the next grid point.
    Range is the band's, from its gain computer's ramp (dsp/Dynamics.h).

    Standard library only. The envelopes belong to the chain's lanes
    (dsp/Chain.h); the per-sample loop is here, so that it is inlined
    whatever the caller looks like.
*/

#include "Dynamics.h"
#include "FastLog.h"
#include "Math.h"
#include "Parameters.h"
#include "Smoother.h"

#include <algorithm>
#include <cmath>

namespace likyu::dsp
{
    /** The PRD's starting values, in ms, scaled by Time (*tune*: the Phase 4
        listening pass sets them). */
    inline constexpr double transientFastAttackMs = 1.0;
    inline constexpr double transientSlowAttackMs = 15.0;
    inline constexpr double transientFastReleaseMs = 20.0;
    inline constexpr double transientSlowReleaseMs = 200.0;

    /** The gate opens over this much above the floor (proposed). */
    inline constexpr double transientGateWidthDb = 6.0;

    /** The four constants, instance-wide. A native client keeps the
        defaults; the plugin's temporary parameters move them until the
        listening pass has set them. */
    struct TransientTimes
    {
        double fastAttackMs = transientFastAttackMs;
        double slowAttackMs = transientSlowAttackMs;
        double fastReleaseMs = transientFastReleaseMs;
        double slowReleaseMs = transientSlowReleaseMs;

        bool operator== (const TransientTimes& o) const noexcept
        {
            return exactlyEqual (fastAttackMs, o.fastAttackMs) && exactlyEqual (slowAttackMs, o.slowAttackMs)
                && exactlyEqual (fastReleaseMs, o.fastReleaseMs) && exactlyEqual (slowReleaseMs, o.slowReleaseMs);
        }
    };

    /** One lane's envelopes, in dB. */
    struct TransientEnvelopes
    {
        double fast = 0.0, slowAttack = 0.0, slowRelease = 0.0;
        bool fresh = true;      // start at the first level read
    };

    class TransientComputer
    {
    public:
        /** At the table's defaults until told otherwise. */
        TransientComputer() noexcept
        {
            attack.jumpTo (start.attack);
            sustain.jumpTo (start.sustain);
            floor.jumpTo (start.floor);
        }

        /** The ramps' length and the rate. */
        void prepare (double sampleRate, int smoothingSteps) noexcept
        {
            rate = sampleRate;
            attack.setLength (smoothingSteps);
            sustain.setLength (smoothingSteps);
            floor.setLength (smoothingSteps);
            makeRates();
        }

        /** In the table's units (%, x, dB), clamped to its ranges; a value
            that is not finite is ignored. Takes effect at the next step. */
        void setTarget (double attackPercent, double sustainPercent, double timeScale, double floorDb) noexcept
        {
            if (std::isfinite (attackPercent))
                attack.setTarget (std::clamp (attackPercent, -maxTransientAmountPercent, maxTransientAmountPercent) / 100.0);

            if (std::isfinite (sustainPercent))
                sustain.setTarget (std::clamp (sustainPercent, -maxTransientAmountPercent, maxTransientAmountPercent) / 100.0);

            if (std::isfinite (timeScale))
                pendingTime = std::clamp (timeScale, minTransientTime, maxTransientTime);

            if (std::isfinite (floorDb))
                floor.setTarget (std::clamp (floorDb, minTransientFloorDb, maxTransientFloorDb));
        }

        /** The four constants; each that is not a positive finite number is
            ignored. Takes effect at the next step. */
        void setTimes (const TransientTimes& t) noexcept
        {
            const auto take = [] (double value, double& into)
            {
                if (std::isfinite (value) && value > 0.0)
                    into = std::clamp (value, 0.01, 10000.0);
            };

            take (t.fastAttackMs, pendingTimes.fastAttackMs);
            take (t.slowAttackMs, pendingTimes.slowAttackMs);
            take (t.fastReleaseMs, pendingTimes.fastReleaseMs);
            take (t.slowReleaseMs, pendingTimes.slowReleaseMs);
        }

        /** Every value at its target now, nothing moving. */
        void finish() noexcept
        {
            attack.finish();
            sustain.finish();
            floor.finish();
            takeCurrent();
            start = end;
            updateRates();
        }

        /** At a grid point: one control step of every ramp. */
        void step() noexcept
        {
            start = end;
            attack.step();
            sustain.step();
            floor.step();
            takeCurrent();
            updateRates();
        }

        /** `n` samples of one lane, from sample `position` of a step of
            `stepSamples`: the lane's powers in, its offsets out, the
            envelopes carried in `e`. Range is the band's at the step's start
            and end. True when any offset of the stretch is not 0. */
        bool run (const float* power, int n, int position, int stepSamples, double rangeStart, double rangeEnd,
                  TransientEnvelopes& e, double* offsets) const noexcept
        {
            const auto rangeMoving = ! exactlyEqual (rangeStart, rangeEnd);
            const auto toStep = 1.0 / static_cast<double> (stepSamples);
            auto fast = e.fast, slowAttack = e.slowAttack, slowRelease = e.slowRelease;
            auto any = false;

            for (int i = 0; i < n; ++i)
            {
                const auto t = static_cast<double> (position + i + 1) * toStep;
                const auto floorDb = moving ? lerpExact (start.floor, end.floor, t) : end.floor;
                const auto x = std::max (static_cast<double> (powerToDb (power[i])), floorDb);

                if (e.fresh)
                {
                    e.fresh = false;
                    fast = slowAttack = slowRelease = x;
                }

                fast = mulAdd (x > fast ? fastAttack : fastRelease, fast - x, x);
                slowAttack = mulAdd (x > slowAttack ? slowAttackRate : fastRelease, slowAttack - x, x);
                slowRelease = mulAdd (x > slowRelease ? fastAttack : slowReleaseRate, slowRelease - x, x);

                const auto attackSignal = std::max (0.0, fast - slowAttack);
                const auto sustainSignal = std::max (0.0, slowRelease - fast);
                const auto a = moving ? lerpExact (start.attack, end.attack, t) : end.attack;
                const auto s = moving ? lerpExact (start.sustain, end.sustain, t) : end.sustain;
                const auto limit = std::abs (rangeMoving ? lerpExact (rangeStart, rangeEnd, t) : rangeEnd);

                auto offset = std::clamp (mulAdd (a, attackSignal, s * sustainSignal), -limit, limit) * gate (fast - floorDb);

                if (! (std::abs (offset) >= returnToZeroDb))
                    offset = 0.0;

                offsets[i] = offset;
                any = any || ! exactlyEqual (offset, 0.0);
            }

            e.fast = fast;
            e.slowAttack = slowAttack;
            e.slowRelease = slowRelease;
            return any;
        }

        /** The gate, for `aboveFloorDb` = E_fast - floor: 0 at and under the
            floor, 1 from the floor + transientGateWidthDb. */
        static double gate (double aboveFloorDb) noexcept
        {
            if (! (aboveFloorDb > 0.0))
                return 0.0;

            if (aboveFloorDb >= transientGateWidthDb)
                return 1.0;

            return smoothstep (aboveFloorDb / transientGateWidthDb);
        }

        double getTime() const noexcept                  { return time; }
        const TransientTimes& getTimes() const noexcept  { return times; }

    private:
        struct Values
        {
            double attack = 0.0, sustain = 0.0, floor = -60.0;
        };

        void takeCurrent() noexcept
        {
            end = { attack.getCurrent(), sustain.getCurrent(), floor.getCurrent() };
            moving = ! (exactlyEqual (start.attack, end.attack) && exactlyEqual (start.sustain, end.sustain)
                        && exactlyEqual (start.floor, end.floor));
        }

        void updateRates() noexcept
        {
            if (exactlyEqual (pendingTime, time) && pendingTimes == times)
                return;

            time = pendingTime;
            times = pendingTimes;
            makeRates();
        }

        void makeRates() noexcept
        {
            fastAttack = onePole (times.fastAttackMs * time, rate);
            slowAttackRate = onePole (times.slowAttackMs * time, rate);
            fastRelease = onePole (times.fastReleaseMs * time, rate);
            slowReleaseRate = onePole (times.slowReleaseMs * time, rate);
        }

        Ramp attack, sustain, floor;
        Values start, end;
        bool moving = false;

        double rate = 0.0;
        double time = 1.0, pendingTime = 1.0;
        TransientTimes times, pendingTimes;
        double fastAttack = 0.0, slowAttackRate = 0.0, fastRelease = 0.0, slowReleaseRate = 0.0;
    };
}
