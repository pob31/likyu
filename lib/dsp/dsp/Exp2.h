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
    pow2 (x) = 2^x in double, with no library call (devplan, Phase 7).

    WHY OUR OWN. A band's gain becomes its A through 2^x every sample while
    the gain moves, and a tracker's estimate becomes a frequency through it
    every sample (PRD, Filter core and Frequency modes). The C library's
    exp2 is a call per value, differs in its last bit from one platform to
    the next, and cannot be said four values at a time with the same bits.
    This one is plain arithmetic in the build's own (dsp/Math.h, mulAdd): the
    same sequence of operations for one value or for a register of them, so
    a SIMD kernel repeats it bit for bit, and lib/dsp gives the same 2^x on
    every machine.

    HOW. With 64 x = m + r, m the nearest integer and |r| <= 1/2:

        2^x = 2^floor(m / 64) * 2^((m mod 64) / 64) * 2^(r / 64)

    The first factor is an exponent, written into the bits. The second is
    one of 64 constants, each the double nearest its value. The third is
    e^z with |z| = |r| ln 2 / 64 <= 0.0055, as 1 + q with
    q = z + z^2/2 + z^3/6 + z^4/24 + z^5/120; what is left out is under
    4e-17 of the result. The result is T + T q in one rounding, times the
    power of two, which is exact.

    m is taken without a conversion to an integer: 64 x + 1.5 * 2^52 is
    rounded by the addition itself to a whole number, whose low mantissa
    bits are m in two's complement.

    WHAT IT PROMISES, and tests/dsp/Exp2Tests.cpp holds:
      - exactly 1 at 0 and at -0, which the exact bypass rests on (PRD,
        Filter core: "whatever fast approximation is used");
      - exactly 2^n for a whole n;
      - within 2 units in the last place of the true value elsewhere, far
        inside the 1e-14 the gain conversions are held to;
      - the argument is taken within [-1000, 1000]: no band, gain or
        frequency is anywhere near, and a lane of padding in a kernel may
        hold anything. Not a number in, not a number out.

    Standard library only; no allocation.
*/

#include "Math.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>

namespace likyu::dsp
{
    namespace pow2detail
    {
        /** 2^(j / 64) for j from 0 to 63, each the nearest double (computed
            to 80 digits and rounded once, 2026-11-21). */
        inline constexpr std::array<double, 64> table {
            0x1.0000000000000p+0, 0x1.02c9a3e778061p+0, 0x1.059b0d3158574p+0, 0x1.0874518759bc8p+0,
            0x1.0b5586cf9890fp+0, 0x1.0e3ec32d3d1a2p+0, 0x1.11301d0125b51p+0, 0x1.1429aaea92de0p+0,
            0x1.172b83c7d517bp+0, 0x1.1a35beb6fcb75p+0, 0x1.1d4873168b9aap+0, 0x1.2063b88628cd6p+0,
            0x1.2387a6e756238p+0, 0x1.26b4565e27cddp+0, 0x1.29e9df51fdee1p+0, 0x1.2d285a6e4030bp+0,
            0x1.306fe0a31b715p+0, 0x1.33c08b26416ffp+0, 0x1.371a7373aa9cbp+0, 0x1.3a7db34e59ff7p+0,
            0x1.3dea64c123422p+0, 0x1.4160a21f72e2ap+0, 0x1.44e086061892dp+0, 0x1.486a2b5c13cd0p+0,
            0x1.4bfdad5362a27p+0, 0x1.4f9b2769d2ca7p+0, 0x1.5342b569d4f82p+0, 0x1.56f4736b527dap+0,
            0x1.5ab07dd485429p+0, 0x1.5e76f15ad2148p+0, 0x1.6247eb03a5585p+0, 0x1.6623882552225p+0,
            0x1.6a09e667f3bcdp+0, 0x1.6dfb23c651a2fp+0, 0x1.71f75e8ec5f74p+0, 0x1.75feb564267c9p+0,
            0x1.7a11473eb0187p+0, 0x1.7e2f336cf4e62p+0, 0x1.82589994cce13p+0, 0x1.868d99b4492edp+0,
            0x1.8ace5422aa0dbp+0, 0x1.8f1ae99157736p+0, 0x1.93737b0cdc5e5p+0, 0x1.97d829fde4e50p+0,
            0x1.9c49182a3f090p+0, 0x1.a0c667b5de565p+0, 0x1.a5503b23e255dp+0, 0x1.a9e6b5579fdbfp+0,
            0x1.ae89f995ad3adp+0, 0x1.b33a2b84f15fbp+0, 0x1.b7f76f2fb5e47p+0, 0x1.bcc1e904bc1d2p+0,
            0x1.c199bdd85529cp+0, 0x1.c67f12e57d14bp+0, 0x1.cb720dcef9069p+0, 0x1.d072d4a07897cp+0,
            0x1.d5818dcfba487p+0, 0x1.da9e603db3285p+0, 0x1.dfc97337b9b5fp+0, 0x1.e502ee78b3ff6p+0,
            0x1.ea4afa2a490dap+0, 0x1.efa1bee615a27p+0, 0x1.f50765b6e4540p+0, 0x1.fa7c1819e90d8p+0,
        };

        /** 1.5 * 2^52: added to a double under 2^51 in size, the sum is a whole number. */
        inline constexpr double toWhole = 0x1.8p52;

        /** ln 2 / 64. */
        inline constexpr double lnTwoOver64 = 0x1.62e42fefa39efp-7;

        /** The widest argument taken. */
        inline constexpr double limit = 1000.0;
    }

    /** 2^x. See the head of this file for what it promises and how. */
    LIKYU_INLINE double pow2 (double x) noexcept
    {
        using namespace pow2detail;

        const double scaled = std::clamp (x, -limit, limit) * 64.0;
        const double whole = scaled + toWhole;
        const double z = (scaled - (whole - toWhole)) * lnTwoOver64;

        // m in the low bits of the mantissa: m mod 64 picks the constant, and
        // floor (m / 64), biased, is the exponent of the power of two.
        const auto bits = std::bit_cast<std::uint64_t> (whole) + (std::uint64_t { 1023 } << 6);
        const double fraction = table[static_cast<std::size_t> (bits & 63u)];
        const double power = std::bit_cast<double> (((bits >> 6) & 0x7ffu) << 52);

        // q = e^z - 1, as z + z^2 ((1/2 + z/6) + z^2 (1/24 + z/120)).
        const double z2 = z * z;
        const double low = mulAdd (z, 1.0 / 6.0, 0.5);
        const double high = mulAdd (z, 1.0 / 120.0, 1.0 / 24.0);
        const double q = mulAdd (z2, mulAdd (z2, high, low), z);

        return mulAdd (fraction, q, fraction) * power;
    }
}
