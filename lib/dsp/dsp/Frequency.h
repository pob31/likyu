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
    THE FREQUENCY AXIS (PRD, Frequency modes): Fixed, Env, Track, for every
    band type, independent of the gain mode.

    THE RANGE RULES (PRD, decided 2026-11-16), for Env and Track: if Low is
    above High the two are swapped; in Track the rest frequency is the
    band's Frequency clamped into [Low, High]. Env rests at the start of its
    mapping, Low for Up and High for Down, and does not use the band's
    Frequency (decided by the author 2026-11-20).

    Low and High ramp in log2 Hz over the band's smoothing time, like its
    Frequency, one step per control step and interpolated linearly inside
    it: an automated range moves the band smoothly.

    Standard library only.
*/

#include "Math.h"
#include "Parameters.h"
#include "Smoother.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace likyu::dsp
{
    /** The parameter table's choices, in order (dsp/Parameters.cpp). */
    enum class FrequencyMode { fixed, env, track };
    inline constexpr int numFrequencyModes = 3;

    enum class EnvDirection { up, down };
    inline constexpr int numEnvDirections = 2;

    /** Low and High, in log2 Hz, on the grid. */
    class FrequencyRange
    {
    public:
        /** At one octave either side of 1 kHz until told otherwise. */
        FrequencyRange() noexcept
        {
            low.jumpTo (std::log2 (500.0));
            high.jumpTo (std::log2 (2000.0));
            finish();
        }

        void setLength (int smoothingSteps) noexcept
        {
            low.setLength (smoothingSteps);
            high.setLength (smoothingSteps);
        }

        /** In Hz, clamped to the table's range; a value that is not finite is
            ignored. Takes effect at the next step. */
        void setTarget (double lowHz, double highHz) noexcept
        {
            if (std::isfinite (lowHz))
                low.setTarget (std::log2 (std::clamp (lowHz, minFrequency, maxFrequency)));

            if (std::isfinite (highHz))
                high.setTarget (std::log2 (std::clamp (highHz, minFrequency, maxFrequency)));
        }

        /** Every value at its target now, nothing moving. */
        void finish() noexcept
        {
            low.finish();
            high.finish();
            take();
            low0 = low1;
            high0 = high1;
            moving = false;
        }

        /** At a grid point. */
        void step() noexcept
        {
            low0 = low1;
            high0 = high1;
            low.step();
            high.step();
            take();
            moving = ! (exactlyEqual (low0, low1) && exactlyEqual (high0, high1));
        }

        /** step() would change nothing: no ramp moves, the step's start is its end. */
        bool isSettled() const noexcept     { return ! (moving || low.isMoving() || high.isMoving()); }

        /** Low and High in log2 Hz at `t` in (0, 1] of the step, in order. */
        std::pair<double, double> at (double t) const noexcept
        {
            const auto a = moving ? lerpExact (low0, low1, t) : low1;
            const auto b = moving ? lerpExact (high0, high1, t) : high1;
            return a <= b ? std::pair { a, b } : std::pair { b, a };
        }

        /** The same at the step's end. */
        std::pair<double, double> atEnd() const noexcept     { return at (1.0); }

        /** A log2 frequency clamped into the range at `t`: Track's rest. */
        double clampInto (double logHz, double t) const noexcept
        {
            const auto [a, b] = at (t);
            return std::clamp (logHz, a, b);
        }

    private:
        void take() noexcept
        {
            low1 = low.getCurrent();
            high1 = high.getCurrent();
        }

        Ramp low, high;
        double low0 = 0.0, low1 = 0.0, high0 = 0.0, high1 = 0.0;
        bool moving = false;
    };
}
