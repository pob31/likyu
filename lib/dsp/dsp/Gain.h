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
    GAIN, IN dB AND AS A FACTOR (PRD, Filter core and Band model).

    dB TO FACTOR, EXACT AT 0 dB. The exact bypass requires that 0 dB give
    exactly 1, whatever approximation is used. Here it is std::exp2 of an
    exact product: exp2(0) is exactly 1 in every C library this builds with,
    and tests/dsp/ResponseTests.cpp holds it to that. Phase 2 computes these
    only while a gain moves; if the per-sample offsets of Phase 3 call for a
    faster form, it answers to the same test.

    THE EFFECTIVE GAIN of a band (decided 2026-11-16): static gain plus the
    dynamic offset, the offset clamped to the Range and the sum to +-30 dB.

    Standard library only.
*/

#include <algorithm>
#include <cmath>

namespace likyu::dsp
{
    /** The limit of a band's effective gain, either way. */
    inline constexpr double maxGainDb = 30.0;

    inline constexpr double log2Of10 = 3.32192809488736234787;

    /** The PRD's A = 10^(dB / 40): its square is the band's gain as a factor. */
    inline double bandA (double dB) noexcept
    {
        return std::exp2 (dB * (log2Of10 / 40.0));
    }

    /** sqrt(A) = 10^(dB / 80), which rescales g for the shelves. */
    inline double bandSqrtA (double dB) noexcept
    {
        return std::exp2 (dB * (log2Of10 / 80.0));
    }

    /** 10^(dB / 20): a gain in dB as a factor (the output gain). */
    inline double dbToFactor (double dB) noexcept
    {
        return std::exp2 (dB * (log2Of10 / 20.0));
    }

    /** Static gain + dynamic offset: the offset within +-|range|, the sum
        within +-30 dB. At 0 and 0 it is exactly 0. */
    inline double effectiveGainDb (double staticDb, double offsetDb, double rangeDb) noexcept
    {
        const auto limit = std::abs (rangeDb);
        const auto offset = std::clamp (offsetDb, -limit, limit);
        return std::clamp (staticDb + offset, -maxGainDb, maxGainDb);
    }
}
