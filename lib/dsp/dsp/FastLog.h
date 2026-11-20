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
    THE LEVEL IN dB, WITHOUT A LIBRARY CALL (PRD, Detection: L = 20 log10(env),
    floored at -120 dB; a fast log2 is allowed, error under 0.05 dB).

    The detectors give a POWER, the envelope squared (dsp/Detector.h), so the
    level is 10 log10(p) = 10 log10(2) log2(p), and no square root is taken.

    log2 of a positive normal float: the exponent read from its bits, plus a
    cubic in the mantissa's fraction u in [0, 1), fitted on the maximum error
    with p(0) = 0 and p(1) = 1 so that octaves join (Lawson's iteration,
    2026-11-20). Measured when written: 0.00088 in log2, 0.0026 dB, in float as
    in double; increasing on [0, 1), so the level never runs backwards.

    Under the floor (zero, negative, subnormal, NaN) the level is the floor;
    above +60 dB it is +60 dB, which also takes care of infinity. Plain
    arithmetic: no allocation, no library call.
*/

#include <algorithm>
#include <bit>
#include <cstdint>

namespace likyu::dsp
{
    /** The PRD's floor for a level, and a ceiling far above any key. */
    inline constexpr float levelFloorDb = -120.0f;
    inline constexpr float levelCeilingDb = 60.0f;

    /** The powers at the floor and at the ceiling: 10^(dB / 10). */
    inline constexpr float powerFloor = 1.0e-12f;
    inline constexpr float powerCeiling = 1.0e6f;

    /** 10 log10(2): dB of power per octave of power. */
    inline constexpr float tenLog10Of2 = 3.0102999566398120f;

    /** log2(x) for a positive normal float; anything else is the caller's to keep out. */
    inline float fastLog2 (float x) noexcept
    {
        constexpr float c1 = 1.4228653419126285f;
        constexpr float c2 = -0.5820854842034611f;
        constexpr float c3 = 0.1592201422908326f;

        const auto bits = std::bit_cast<std::uint32_t> (x);
        const auto exponent = static_cast<int> ((bits >> 23) & 0xffu) - 127;
        const auto u = std::bit_cast<float> ((bits & 0x007fffffu) | 0x3f800000u) - 1.0f;

        return static_cast<float> (exponent) + u * (c1 + u * (c2 + u * c3));
    }

    /** A power as a level in dB, within [-120, +60]. NaN is the floor. */
    inline float powerToDb (float power) noexcept
    {
        if (! (power >= powerFloor))
            return levelFloorDb;

        if (power >= powerCeiling)
            return levelCeilingDb;

        return std::max (levelFloorDb, tenLog10Of2 * fastLog2 (power));
    }
}
