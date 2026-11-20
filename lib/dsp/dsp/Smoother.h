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
    PARAMETER SMOOTHING (PRD, Filter core: user moves are smoothed in lib/dsp,
    frequency in log domain, gain in dB, about 20 ms, so that a native client
    of the library gets the same smoothing as the plugin).

    A Ramp is linear, in whatever domain its caller speaks (log2 Hz, log2 Q,
    dB, a crossfade's 0 to 1), and moves one step per CONTROL STEP, a
    sub-block of the chain's grid (dsp/Chain.h). Two properties matter more
    than its shape:

      - it LANDS EXACTLY on its target, after a fixed number of steps,
        whatever rounding happened on the way: a gain ramped back to 0 dB is
        exactly 0 dB again, so the band is again an exact bypass;
      - its value is a function of the step count alone, never of how a host
        cut its blocks.

    A new target mid-ramp starts a fresh ramp of the full length from where
    the value is. Plain data; standard library only.
*/

#include "Math.h"

#include <algorithm>

namespace likyu::dsp
{
    class Ramp
    {
    public:
        /** How many control steps a ramp takes. At least 1. */
        void setLength (int steps) noexcept
        {
            length = std::max (1, steps);
        }

        /** Value and target together, no ramp. */
        void jumpTo (double value) noexcept
        {
            current = target = value;
            remaining = 0;
        }

        /** Ends any ramp at its target now. */
        void finish() noexcept
        {
            jumpTo (target);
        }

        void setTarget (double value) noexcept
        {
            if (exactlyEqual (value, target))
                return;

            target = value;

            if (exactlyEqual (value, current))
            {
                remaining = 0;
                return;
            }

            remaining = length;
            increment = (target - current) / length;
        }

        /** One control step; returns the new value. The last step of a ramp
            is the target itself, not the sum of the increments. */
        double step() noexcept
        {
            if (remaining > 0)
                current = (--remaining == 0) ? target : current + increment;

            return current;
        }

        double getCurrent() const noexcept  { return current; }
        double getTarget() const noexcept   { return target; }
        bool isMoving() const noexcept      { return remaining > 0; }
        int getLength() const noexcept      { return length; }

    private:
        double current = 0.0;
        double target = 0.0;
        double increment = 0.0;
        int remaining = 0;
        int length = 1;
    };

    /** Control steps for a duration at a rate, on a grid of `stepSamples`. */
    inline int rampSteps (double seconds, double sampleRate, int stepSamples) noexcept
    {
        if (! (sampleRate > 0.0) || stepSamples <= 0)
            return 1;

        const auto steps = seconds * sampleRate / stepSamples;
        return std::max (1, static_cast<int> (steps + 0.5));
    }
}
