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
    THE GAIN COMPUTER (PRD, Gain modes: Dynamic). Feed-forward, in the dB
    domain, after Giannoulis, Massberg and Reiss (2012), with the smoothing on
    the offset and not on the level:

        over   = softknee(L - T, W)        Above: reacts when L exceeds T
               = softknee(T - L, W)        Below: reacts when L falls under T
        amount = over (1 - 1/R)
        target = sign(Range) min(amount, |Range|)

    then a one-pole from the offset to its target, per sample.

    ATTACK AND RELEASE (decided by the author 2026-11-20). Attack is the
    offset moving away from 0, the band acting; release is the offset coming
    back to 0, the band letting go. The same rule in Above and Below, so in
    Below the attack is the cut deepening as the key gets quiet. Each is a
    time constant: a step of the target is 63 % done after it.

    RETURN TO ZERO (PRD, decided 2026-11-16). A target of exactly 0 and an
    offset within 0.0001 dB of it: the offset is set to exactly 0, so that an
    idle band is again an exact bypass.

    THE OFFSET IS DOUBLE, although the detection path is float: in float a
    one-pole stops moving once the step it would take is under half a unit
    in the last place, 0.37 dB short of a -30 dB target at 192 kHz with a 2 s
    release, which the dynamic-curve row's 0.25 dB does not allow.

    ON THE GRID. Threshold, knee, slope (1 - 1/R) and Range ramp over 20 ms,
    like the band's own gain, one step per control step, interpolated
    linearly inside it; a jump of any of them would otherwise be a step in
    the gain whenever the attack is short. Attack and release only change a
    rate, and take effect at the next grid point.

    Standard library only. No buffers: the offsets belong to the chain's
    lanes (dsp/Chain.h).
*/

#include "Math.h"
#include "Parameters.h"
#include "Smoother.h"

#include <algorithm>
#include <cmath>

namespace likyu::dsp
{
    /** The parameter table's choices, in order (dsp/Parameters.cpp). */
    enum class GainMode { staticGain, dynamic, transient };
    inline constexpr int numGainModes = 3;

    enum class Direction { above, below };
    inline constexpr int numDirections = 2;

    /** Within this of a target of 0, the offset is 0 (PRD, Return to zero). */
    inline constexpr double returnToZeroDb = 1.0e-4;

    /** How far past the threshold, through the soft knee of width `kneeDb`. */
    inline double softKnee (double overDb, double kneeDb) noexcept
    {
        if (! (kneeDb > 0.0))
            return std::max (0.0, overDb);

        if (2.0 * overDb <= -kneeDb)
            return 0.0;

        if (2.0 * overDb >= kneeDb)
            return overDb;

        const auto y = overDb + 0.5 * kneeDb;
        return y * y / (2.0 * kneeDb);
    }

    /** The offset a level asks for, before smoothing. Exactly 0 below the knee. */
    inline double dynamicTarget (double levelDb, double thresholdDb, double kneeDb, double slope,
                                 double rangeDb, Direction direction) noexcept
    {
        const auto over = direction == Direction::above ? levelDb - thresholdDb : thresholdDb - levelDb;
        const auto amount = softKnee (over, kneeDb) * slope;

        if (! (amount > 0.0))
            return 0.0;

        if (rangeDb < 0.0)
            return -std::min (amount, -rangeDb);

        if (rangeDb > 0.0)
            return std::min (amount, rangeDb);

        return 0.0;
    }

    /** One-pole coefficient for a time constant, per sample. */
    inline double onePole (double milliseconds, double sampleRate) noexcept
    {
        if (! (sampleRate > 0.0) || ! (milliseconds > 0.0))
            return 0.0;

        return std::exp (-1000.0 / (milliseconds * sampleRate));
    }

    class GainComputer
    {
    public:
        /** At the table's defaults until told otherwise. */
        GainComputer() noexcept
        {
            threshold.jumpTo (start.threshold);
            knee.jumpTo (start.knee);
            slope.jumpTo (start.slope);
            range.jumpTo (start.range);
        }

        /** The ramps' length and the rate. */
        void prepare (double sampleRate, int smoothingSteps) noexcept
        {
            rate = sampleRate;
            threshold.setLength (smoothingSteps);
            knee.setLength (smoothingSteps);
            slope.setLength (smoothingSteps);
            range.setLength (smoothingSteps);
            attack = onePole (attackMs, rate);
            release = onePole (releaseMs, rate);
        }

        /** In the table's units, clamped to its ranges; a value that is not
            finite is ignored. Takes effect at the next step. */
        void setTarget (double thresholdDb, double kneeDb, double ratio, double rangeDb,
                        double attackMilliseconds, double releaseMilliseconds) noexcept
        {
            if (std::isfinite (thresholdDb))
                threshold.setTarget (std::clamp (thresholdDb, minThresholdDb, maxThresholdDb));

            if (std::isfinite (kneeDb))
                knee.setTarget (std::clamp (kneeDb, 0.0, maxKneeDb));

            if (std::isfinite (ratio))
                slope.setTarget (1.0 - 1.0 / std::clamp (ratio, minRatio, maxRatio));

            if (std::isfinite (rangeDb))
                range.setTarget (std::clamp (rangeDb, -maxRangeDb, maxRangeDb));

            if (std::isfinite (attackMilliseconds))
                pendingAttackMs = std::clamp (attackMilliseconds, minAttackMs, maxAttackMs);

            if (std::isfinite (releaseMilliseconds))
                pendingReleaseMs = std::clamp (releaseMilliseconds, minReleaseMs, maxReleaseMs);
        }

        /** Every value at its target now, nothing moving. */
        void finish() noexcept
        {
            threshold.finish();
            knee.finish();
            slope.finish();
            range.finish();
            takeCurrent();
            start = end;
            updateRates();
        }

        /** At a grid point: one control step of every ramp. */
        void step() noexcept
        {
            start = end;
            threshold.step();
            knee.step();
            slope.step();
            range.step();
            takeCurrent();
            updateRates();
        }

        /** The target for a level at `t` in (0, 1] of the current step. */
        double targetAt (double levelDb, double t, Direction direction) const noexcept
        {
            if (! moving)
                return dynamicTarget (levelDb, end.threshold, end.knee, end.slope, end.range, direction);

            return dynamicTarget (levelDb, lerpExact (start.threshold, end.threshold, t), lerpExact (start.knee, end.knee, t),
                                  lerpExact (start.slope, end.slope, t), lerpExact (start.range, end.range, t), direction);
        }

        /** The Range at `t` in (0, 1] of the current step: the offset's limit. */
        double rangeAt (double t) const noexcept
        {
            return moving ? lerpExact (start.range, end.range, t) : end.range;
        }

        /** The Range at the current step's start and end, for the transient
            shaper's limit (dsp/Transient.h), which interpolates them itself. */
        double rangeStart() const noexcept  { return start.range; }
        double rangeEnd() const noexcept    { return end.range; }

        /** One sample of the one-pole, from `offset` toward `target`. */
        void smooth (double& offset, double target) const noexcept
        {
            const auto a = std::abs (target) > std::abs (offset) ? attack : release;
            offset = mulAdd (a, offset - target, target);

            if (exactlyEqual (target, 0.0) && std::abs (offset) < returnToZeroDb)
                offset = 0.0;
        }

        double getAttackCoefficient() const noexcept   { return attack; }
        double getReleaseCoefficient() const noexcept  { return release; }

    private:
        struct Values
        {
            double threshold = -24.0, knee = 6.0, slope = 2.0 / 3.0, range = -6.0;
        };

        void takeCurrent() noexcept
        {
            end = { threshold.getCurrent(), knee.getCurrent(), slope.getCurrent(), range.getCurrent() };
            moving = ! (exactlyEqual (start.threshold, end.threshold) && exactlyEqual (start.knee, end.knee)
                        && exactlyEqual (start.slope, end.slope) && exactlyEqual (start.range, end.range));
        }

        void updateRates() noexcept
        {
            if (! exactlyEqual (pendingAttackMs, attackMs))
            {
                attackMs = pendingAttackMs;
                attack = onePole (attackMs, rate);
            }

            if (! exactlyEqual (pendingReleaseMs, releaseMs))
            {
                releaseMs = pendingReleaseMs;
                release = onePole (releaseMs, rate);
            }
        }

        Ramp threshold, knee, slope, range;
        Values start, end;
        bool moving = false;

        double rate = 0.0;
        double attackMs = 5.0, releaseMs = 120.0;
        double pendingAttackMs = 5.0, pendingReleaseMs = 120.0;
        double attack = 0.0, release = 0.0;
    };
}
