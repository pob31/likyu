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

/*
    The registers against the plain arrays (dsp/simd/Vec.h; devplan, Phase 7):
    every operation of every type, on the target's registers and on the
    scalar back-end, the two results compared bit for bit, and the scalar one
    compared with the scalar expression it stands for.

    In the reference and scalar builds the two back-ends are the same type,
    and this is then a test of the arrays against the expressions alone. The
    build that has registers is where it earns its place: AVX2 here and in
    CI on Linux and Windows, NEON on macOS.

    The values are the awkward ones as well as the ordinary: zeros of both
    signs, denormals, infinities, not-a-numbers, and pairs that differ in
    their last bit.
*/

#include <doctest/doctest.h>

#include <dsp/Exp2.h>
#include <dsp/Tan.h>
#include <dsp/simd/Vec.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace likyu::dsp;

namespace
{
    /** A few hundred floats: the awkward ones, then noise over many magnitudes. */
    std::vector<float> awkwardFloats()
    {
        const auto inf = std::numeric_limits<float>::infinity();
        const auto nan = std::numeric_limits<float>::quiet_NaN();
        std::vector<float> values { 0.0f, -0.0f, 1.0f, -1.0f, 2.0f, 0.5f, 1.0f + 0x1p-23f, 1.0f - 0x1p-24f,
                                    std::numeric_limits<float>::min(), -std::numeric_limits<float>::min(),
                                    std::numeric_limits<float>::denorm_min(), -std::numeric_limits<float>::denorm_min(),
                                    0x1p-140f, std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
                                    inf, -inf, nan, -nan, 1.0e-20f, -1.0e-20f, 3.0e-7f, 12345.678f, -0x1p-13f };
        std::uint32_t state = 2026;

        for (int i = 0; i < 360; ++i)
        {
            state = state * 1664525u + 1013904223u;
            const auto mantissa = static_cast<float> (state >> 8) / 8388608.0f - 1.0f;
            state = state * 1664525u + 1013904223u;
            const auto scale = static_cast<int> (state >> 27) - 16;     // 2^-16 to 2^15
            values.push_back (std::ldexp (mantissa, scale));
        }

        return values;
    }

    /** The same bits in every lane; or, in a lane, not a number on both
        sides. Which of two not-a-numbers an addition returns, and with which
        sign, depends on the order a compiler gives the operands, in scalar
        code as in a register: nothing in lib/dsp rests on it. */
    template <std::size_t N>
    bool sameBits (const std::array<float, N>& a, const std::array<float, N>& b)
    {
        for (std::size_t lane = 0; lane < N; ++lane)
        {
            const auto bothNotNumbers = a[lane] != a[lane] && b[lane] != b[lane];

            if (! bothNotNumbers && std::memcmp (&a[lane], &b[lane], sizeof (float)) != 0)
                return false;
        }

        return true;
    }

    /** Eight values from the list, starting at `at` and stepping by `stride`, round. */
    std::array<float, 8> pick (const std::vector<float>& values, std::size_t at, std::size_t stride)
    {
        std::array<float, 8> out {};

        for (std::size_t lane = 0; lane < 8; ++lane)
            out[lane] = values[(at + lane * stride) % values.size()];

        return out;
    }

    template <typename V, typename Operation>
    std::array<float, 8> apply (Operation&& operation, const std::array<float, 8>& a, const std::array<float, 8>& b,
                                const std::array<float, 8>& c)
    {
        std::array<float, 8> out {};
        operation (V::load (a.data()), V::load (b.data()), V::load (c.data())).store (out.data());
        return out;
    }
}

TEST_CASE ("registers, eight floats: every operation gives the bits of the plain arrays, and of the scalar expression")
{
    using Native = simd::native::F32x8;
    using Scalar = simd::scalar::F32x8;

    const auto values = awkwardFloats();
    auto different = 0, notTheExpression = 0, cases = 0;

    // Each operation three ways: on the registers, on the arrays, and a lane at a time as the scalar code writes it.
    const auto check = [&] (auto&& onRegisters, auto&& onArrays, auto&& expression)
    {
        for (std::size_t at = 0; at < values.size(); ++at)
            for (const std::size_t stride : { std::size_t { 1 }, std::size_t { 7 }, std::size_t { 31 } })
            {
                const auto a = pick (values, at, stride), b = pick (values, at * 3 + 5, stride + 2), c = pick (values, at * 5 + 11, stride + 4);
                const auto native = apply<Native> (onRegisters, a, b, c);
                const auto arrays = apply<Scalar> (onArrays, a, b, c);
                std::array<float, 8> expected {};

                for (std::size_t lane = 0; lane < 8; ++lane)
                    expected[lane] = expression (a[lane], b[lane], c[lane]);

                different += sameBits (native, arrays) ? 0 : 1;
                notTheExpression += sameBits (arrays, expected) ? 0 : 1;
                ++cases;
            }
    };

    check ([] (Native a, Native b, Native) { return a + b; }, [] (Scalar a, Scalar b, Scalar) { return a + b; },
           [] (float a, float b, float) { return a + b; });
    check ([] (Native a, Native b, Native) { return a - b; }, [] (Scalar a, Scalar b, Scalar) { return a - b; },
           [] (float a, float b, float) { return a - b; });
    check ([] (Native a, Native b, Native) { return a * b; }, [] (Scalar a, Scalar b, Scalar) { return a * b; },
           [] (float a, float b, float) { return a * b; });
    check ([] (Native a, Native, Native) { return -a; }, [] (Scalar a, Scalar, Scalar) { return -a; },
           [] (float a, float, float) { return -a; });
    check ([] (Native a, Native b, Native c) { return mulAdd (a, b, c); }, [] (Scalar a, Scalar b, Scalar c) { return mulAdd (a, b, c); },
           [] (float a, float b, float c) { return mulAdd (a, b, c); });
    check ([] (Native a, Native b, Native) { return max (a, b); }, [] (Scalar a, Scalar b, Scalar) { return max (a, b); },
           [] (float a, float b, float) { return std::max (a, b); });

    // A value broadcast, and a load stored again, are the value.
    for (const auto x : values)
    {
        std::array<float, 8> native {}, arrays {}, expected {};
        Native::broadcast (x).store (native.data());
        Scalar::broadcast (x).store (arrays.data());
        expected.fill (x);
        different += sameBits (native, arrays) ? 0 : 1;
        notTheExpression += sameBits (arrays, expected) ? 0 : 1;
        ++cases;
    }

    // A division, which the eight-float type gained for the trackers.
    check ([] (Native a, Native b, Native) { return a / b; }, [] (Scalar a, Scalar b, Scalar) { return a / b; },
           [] (float a, float b, float) { return a / b; });

    const std::string target = simd::targetName;
    MESSAGE ("registers: " << target << ", " << cases << " vectors of eight");
    CHECK (different == 0);
    CHECK (notTheExpression == 0);
}

//==============================================================================
namespace
{
    /** The awkward doubles, then values over the ranges pow2 and tan are asked
        for (gains, log2 frequencies, arguments of tan), then wide noise. */
    std::vector<double> awkwardDoubles()
    {
        const auto inf = std::numeric_limits<double>::infinity();
        const auto nan = std::numeric_limits<double>::quiet_NaN();
        std::vector<double> values { 0.0, -0.0, 1.0, -1.0, 0.5, 1.0 + 0x1p-52, 1.0 - 0x1p-53, 1.0 / 128.0, -1.0 / 128.0,
                                     std::numeric_limits<double>::min(), std::numeric_limits<double>::denorm_min(),
                                     -std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::max(),
                                     inf, -inf, nan, -nan, 1000.0, -1000.0, 1000.5, -1000.5, 20.0, 20000.0, 0.7 };
        std::uint32_t state = 4242;

        const auto next = [&state]
        {
            state = state * 1664525u + 1013904223u;
            return static_cast<double> (state >> 8) / 16777216.0;
        };

        for (int i = 0; i < 200; ++i) values.push_back (-3.0 + 6.0 * next());           // gains, as 2^x
        for (int i = 0; i < 200; ++i) values.push_back (4.0 + 11.0 * next());           // log2 Hz
        for (int i = 0; i < 200; ++i) values.push_back (20.0 + 22000.0 * next());       // Hz, for tan
        for (int i = 0; i < 100; ++i) values.push_back (std::ldexp (2.0 * next() - 1.0, static_cast<int> (next() * 80.0) - 40));

        // Both sides of every boundary of pow2's table, around zero and an octave up.
        for (int k = -130; k <= 130; ++k)
        {
            const auto edge = (static_cast<double> (k) + 0.5) / 64.0;
            values.push_back (std::nextafter (edge, -inf));
            values.push_back (edge);
            values.push_back (std::nextafter (edge, inf));
        }

        return values;
    }

    template <std::size_t N>
    bool sameDoubles (const std::array<double, N>& a, const std::array<double, N>& b)
    {
        for (std::size_t lane = 0; lane < N; ++lane)
        {
            const auto bothNotNumbers = a[lane] != a[lane] && b[lane] != b[lane];

            if (! bothNotNumbers && std::memcmp (&a[lane], &b[lane], sizeof (double)) != 0)
                return false;
        }

        return true;
    }

    std::array<double, 4> pick4 (const std::vector<double>& values, std::size_t at, std::size_t stride)
    {
        std::array<double, 4> out {};

        for (std::size_t lane = 0; lane < 4; ++lane)
            out[lane] = values[(at + lane * stride) % values.size()];

        return out;
    }

    template <typename V>
    std::array<double, 4> storeOf (V v)
    {
        std::array<double, 4> out {};
        v.store (out.data());
        return out;
    }
}

TEST_CASE ("registers, four doubles: every operation, pow2 and tan give the bits of the plain arrays and of the scalar code")
{
    using Native = simd::native::F64x4;
    using Scalar = simd::scalar::F64x4;

    const auto values = awkwardDoubles();
    auto different = 0, notTheScalarCode = 0, cases = 0;

    const auto check = [&] (auto&& onRegisters, auto&& onArrays, auto&& scalarCode)
    {
        for (std::size_t at = 0; at < values.size(); ++at)
            for (const std::size_t stride : { std::size_t { 1 }, std::size_t { 13 } })
            {
                const auto a = pick4 (values, at, stride), b = pick4 (values, at * 3 + 5, stride + 2), c = pick4 (values, at * 7 + 1, stride + 4);
                const auto native = storeOf (onRegisters (Native::load (a.data()), Native::load (b.data()), Native::load (c.data())));
                const auto arrays = storeOf (onArrays (Scalar::load (a.data()), Scalar::load (b.data()), Scalar::load (c.data())));
                std::array<double, 4> expected {};

                for (std::size_t lane = 0; lane < 4; ++lane)
                    expected[lane] = scalarCode (a[lane], b[lane], c[lane]);

                different += sameDoubles (native, arrays) ? 0 : 1;
                notTheScalarCode += sameDoubles (arrays, expected) ? 0 : 1;
                ++cases;
            }
    };

    check ([] (Native a, Native b, Native) { return a + b; }, [] (Scalar a, Scalar b, Scalar) { return a + b; },
           [] (double a, double b, double) { return a + b; });
    check ([] (Native a, Native b, Native) { return a - b; }, [] (Scalar a, Scalar b, Scalar) { return a - b; },
           [] (double a, double b, double) { return a - b; });
    check ([] (Native a, Native b, Native) { return a * b; }, [] (Scalar a, Scalar b, Scalar) { return a * b; },
           [] (double a, double b, double) { return a * b; });
    check ([] (Native a, Native b, Native) { return a / b; }, [] (Scalar a, Scalar b, Scalar) { return a / b; },
           [] (double a, double b, double) { return a / b; });
    check ([] (Native a, Native, Native) { return -a; }, [] (Scalar a, Scalar, Scalar) { return -a; },
           [] (double a, double, double) { return -a; });
    check ([] (Native a, Native b, Native c) { return mulAdd (a, b, c); }, [] (Scalar a, Scalar b, Scalar c) { return mulAdd (a, b, c); },
           [] (double a, double b, double c) { return mulAdd (a, b, c); });
    check ([] (Native a, Native b, Native c) { return select (less (a, b), c, a); }, [] (Scalar a, Scalar b, Scalar c) { return select (less (a, b), c, a); },
           [] (double a, double b, double c) { return a < b ? c : a; });
    check ([] (Native a, Native b, Native c) { return clampLanes (a, b, b + c * c); }, [] (Scalar a, Scalar b, Scalar c) { return clampLanes (a, b, b + c * c); },
           [] (double a, double b, double c) { return std::clamp (a, b, b + c * c); });

    // pow2 a lane at a time against pow2 itself, and the warp's tan against Warp::g, at the rates of both forms.
    check ([] (Native a, Native, Native) { return pow2Lanes (a); }, [] (Scalar a, Scalar, Scalar) { return pow2Lanes (a); },
           [] (double a, double, double) { return pow2 (a); });

    for (const auto rate : { 44100.0, 96000.0 })
    {
        Warp warp;
        warp.prepare (rate);
        check ([&warp] (Native a, Native, Native) { return warp.gLanes (a); }, [&warp] (Scalar a, Scalar, Scalar) { return warp.gLanes (a); },
               [&warp] (double a, double, double) { return warp.g (a); });
    }

    // Two times four doubles narrowed to eight floats: each rounded as a static_cast rounds it.
    for (std::size_t at = 0; at < values.size(); ++at)
    {
        const auto a = pick4 (values, at, 1), b = pick4 (values, at + 17, 3);
        std::array<float, 8> native {}, arrays {}, expected {};
        narrow (Native::load (a.data()), Native::load (b.data())).store (native.data());
        narrow (Scalar::load (a.data()), Scalar::load (b.data())).store (arrays.data());

        for (std::size_t lane = 0; lane < 4; ++lane)
        {
            expected[lane] = static_cast<float> (a[lane]);
            expected[lane + 4] = static_cast<float> (b[lane]);
        }

        different += sameBits (native, arrays) ? 0 : 1;
        notTheScalarCode += sameBits (arrays, expected) ? 0 : 1;
        ++cases;
    }

    const std::string target = simd::targetName;
    MESSAGE ("registers: " << target << ", " << cases << " vectors of four doubles");
    CHECK (different == 0);
    CHECK (notTheScalarCode == 0);
}
