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

    Origin: Go.dot src/wfg/client/model/Eq.cpp (pinchedQ, turnedQ, magnifiedQ),
    with Likyu's range of Q.
*/

#pragma once

/*
    Q FROM A WHEEL, A PINCH AND A MAGNIFY GESTURE (PRD, Editor: mouse wheel
    over a node sets Q; two fingers on one node pinch its Q). The same three
    functions as Go.dot's EQ, so the gesture feels the same in both.

      - A wheel step of 0.1 is a factor of 1.1, 1.01 when fine. A Windows
        touchpad sends its pinch as Ctrl + wheel, the other way up: `pinch`
        turns it over.
      - Closing two fingers narrows the band: Q rises as the distance falls.

    No JUCE here.
*/

#include <dsp/Parameters.h>

#include <algorithm>
#include <cmath>

namespace likyu::editor
{
    inline double clampedQ (double q) noexcept
    {
        return std::clamp (std::isfinite (q) ? q : 1.0, dsp::minQ, dsp::maxQ);
    }

    /** Q after two fingers that were `fromDistance` apart are `distance` apart. */
    inline double pinchedQ (double fromQ, double fromDistance, double distance) noexcept
    {
        if (! (fromDistance > 0.0) || ! (distance > 0.0))
            return clampedQ (fromQ);

        return clampedQ (fromQ * fromDistance / distance);
    }

    /** Q after a wheel movement of `wheel` (JUCE's deltaY). */
    inline double turnedQ (double q, double wheel, bool pinch, bool fine) noexcept
    {
        const auto step = fine ? 1.01 : 1.1;
        const auto tenths = (pinch ? -wheel : wheel) * 10.0;

        return clampedQ (q * std::pow (step, tenths));
    }

    /** Q after a magnify gesture of `scale` (above 1: fingers apart, wider). */
    inline double magnifiedQ (double q, double scale) noexcept
    {
        if (! (scale > 0.0))
            return clampedQ (q);

        return clampedQ (q / scale);
    }
}
