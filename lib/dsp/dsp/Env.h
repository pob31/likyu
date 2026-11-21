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
    ENV (PRD, Frequency modes): the key's level, between a Sensitivity
    threshold and that threshold plus a Span, maps onto Low to High (Up) or
    High to Low (Down), in log frequency. Its attack and release smooth the
    level before the mapping and are the only smoothing in Env.

        x     = clamp(L, Sens, Sens + Span)
        level = one-pole of x in dB: attack while x rises, release while it falls
        u     = (level - Sens) / Span
        log f = log Low + (log High - log Low) * (Up ? u : 1 - u)

    THE FOLLOWER RUNS ON THE SPAN, NOT ON L. Under Sensitivity every level
    maps to the same frequency, and so does every level over the top, so the
    follower is held between the two (built 2026-11-20): a note after a
    silence starts its attack at Sensitivity, as the next one does, and a
    level far over the span comes back into it as soon as it falls. This is
    Phase 4's lesson (the envelopes on max(L, floor)), at both ends.

    AT REST, EXACTLY. With the level at Sensitivity and the follower within
    envRestDb of it, the follower is Sensitivity, so a band whose key is
    quiet sits exactly at the start of its mapping (Low for Up, High for
    Down: decided by the author 2026-11-20), not a hair above it.

    The follower is double, like Dynamic's offset and Transient's envelopes,
    and starts at the first level it reads, so a band switched into Env
    (through the identity) fades in at the frequency its key asks for.

    ON THE GRID. Sensitivity and Span ramp over 20 ms like the band's gain,
    interpolated linearly inside a control step; attack and release only
    change rates, at the next grid point. Low and High are the band's
    (dsp/Frequency.h).

    Standard library only. The followers belong to the chain's lanes
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
    /** Within this of Sensitivity, with the level there, the follower is at rest. */
    inline constexpr double envRestDb = 1.0e-4;

    /** One lane's follower, in dB. */
    struct EnvState
    {
        double level = 0.0;
        bool fresh = true;      // start at the first level read
    };

    class EnvComputer
    {
    public:
        /** At the table's defaults until told otherwise. */
        EnvComputer() noexcept
        {
            sensitivity.jumpTo (start.sensitivity);
            span.jumpTo (start.span);
        }

        void prepare (double sampleRate, int smoothingSteps) noexcept
        {
            rate = sampleRate;
            sensitivity.setLength (smoothingSteps);
            span.setLength (smoothingSteps);
            attack = onePole (attackMs, rate);
            release = onePole (releaseMs, rate);
        }

        /** In the table's units, clamped to its ranges; a value that is not
            finite is ignored. Takes effect at the next step. */
        void setTarget (double sensitivityDb, double spanDb, double attackMilliseconds, double releaseMilliseconds) noexcept
        {
            if (std::isfinite (sensitivityDb))
                sensitivity.setTarget (std::clamp (sensitivityDb, minSensitivityDb, maxSensitivityDb));

            if (std::isfinite (spanDb))
                span.setTarget (std::clamp (spanDb, minSpanDb, maxSpanDb));

            if (std::isfinite (attackMilliseconds))
                pendingAttackMs = std::clamp (attackMilliseconds, minAttackMs, maxAttackMs);

            if (std::isfinite (releaseMilliseconds))
                pendingReleaseMs = std::clamp (releaseMilliseconds, minReleaseMs, maxReleaseMs);
        }

        void finish() noexcept
        {
            sensitivity.finish();
            span.finish();
            takeCurrent();
            start = end;
            moving = false;
            updateRates();
        }

        void step() noexcept
        {
            start = end;
            sensitivity.step();
            span.step();
            takeCurrent();
            updateRates();
        }

        /** step() would change nothing: no ramp moves, start is end, no rate waits. */
        bool isSettled() const noexcept
        {
            return ! (moving || sensitivity.isMoving() || span.isMoving())
                   && exactlyEqual (pendingAttackMs, attackMs) && exactlyEqual (pendingReleaseMs, releaseMs);
        }

        /** `n` samples of one lane's powers, from sample `position` of a step
            of `stepSamples`: the follower, carried in `e`. */
        void run (const float* power, int n, int position, int stepSamples, EnvState& e) const noexcept
        {
            const auto toStep = 1.0 / static_cast<double> (stepSamples);
            auto level = e.level;

            for (int i = 0; i < n; ++i)
            {
                const auto t = static_cast<double> (position + i + 1) * toStep;
                const auto bottom = moving ? lerpExact (start.sensitivity, end.sensitivity, t) : end.sensitivity;
                const auto width = moving ? lerpExact (start.span, end.span, t) : end.span;
                const auto x = std::clamp (static_cast<double> (powerToDb (power[i])), bottom, bottom + width);

                if (e.fresh)
                {
                    e.fresh = false;
                    level = x;
                }

                level = mulAdd (x > level ? attack : release, level - x, x);

                if (exactlyEqual (x, bottom) && std::abs (level - bottom) < envRestDb)
                    level = bottom;
            }

            e.level = level;
        }

        /** Where a follower's level sits in the span at `t` in (0, 1] of the
            step: 0 at Sensitivity, 1 at Sensitivity + Span. */
        double positionAt (double levelDb, double t) const noexcept
        {
            const auto bottom = moving ? lerpExact (start.sensitivity, end.sensitivity, t) : end.sensitivity;
            const auto width = moving ? lerpExact (start.span, end.span, t) : end.span;
            return std::clamp ((levelDb - bottom) / width, 0.0, 1.0);
        }

        double getAttackCoefficient() const noexcept   { return attack; }
        double getReleaseCoefficient() const noexcept  { return release; }

    private:
        struct Values
        {
            double sensitivity = -40.0, span = 30.0;
        };

        void takeCurrent() noexcept
        {
            end = { sensitivity.getCurrent(), span.getCurrent() };
            moving = ! (exactlyEqual (start.sensitivity, end.sensitivity) && exactlyEqual (start.span, end.span));
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

        Ramp sensitivity, span;
        Values start, end;
        bool moving = false;

        double rate = 0.0;
        double attackMs = 5.0, releaseMs = 120.0;
        double pendingAttackMs = 5.0, pendingReleaseMs = 120.0;
        double attack = 0.0, release = 0.0;
    };
}
