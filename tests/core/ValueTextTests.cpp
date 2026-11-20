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
    Value text (core/ValueText.h): what a host shows, and what it reads back,
    under C and fr_FR alike.
*/

#include <doctest/doctest.h>

#include "../TestSupport.h"

#include <core/ValueText.h>
#include <dsp/Parameters.h>

#include <cmath>
#include <string>

using namespace likyu;

namespace
{
    const dsp::ParamSpec& spec (const char* id)
    {
        const auto index = dsp::indexOfId (id);
        REQUIRE (index >= 0);
        return dsp::parameterTable()[static_cast<std::size_t> (index)];
    }

    std::string text (const char* id, double value)
    {
        return core::valueText (spec (id), value).toStdString();
    }

    double read (const char* id, const char* t)
    {
        return core::valueFromText (spec (id), t);
    }
}

TEST_CASE ("value text: each unit, as a host shows it")
{
    INFO ("locale in effect: " << std::string (likyutest::appliedLocaleName()));

    CHECK (text ("b1_freq", 20.0) == "20.0 Hz");
    CHECK (text ("b1_freq", 80.0) == "80.0 Hz");
    CHECK (text ("b1_freq", 500.0) == "500 Hz");
    CHECK (text ("b1_freq", 1200.0) == "1.20 kHz");
    CHECK (text ("b1_freq", 14000.0) == "14.0 kHz");
    CHECK (text ("b1_freq", 999.7) == "1.00 kHz");

    CHECK (text ("b1_gain", -4.5) == "-4.5 dB");
    CHECK (text ("b1_gain", 3.0) == "+3.0 dB");
    CHECK (text ("b1_gain", 0.0) == "0.0 dB");
    CHECK (text ("b1_gain", -0.01) == "0.0 dB");
    CHECK (text ("out_gain", -24.0) == "-24.0 dB");

    CHECK (text ("b1_atk", 0.05) == "0.05 ms");
    CHECK (text ("b1_atk", 5.0) == "5.00 ms");
    CHECK (text ("b1_rel", 120.0) == "120 ms");
    CHECK (text ("b1_rel", 45.0) == "45.0 ms");

    CHECK (text ("b1_fconf", 50.0) == "50 %");
    CHECK (text ("b1_ttime", 1.0) == "1.00x");
    CHECK (text ("b1_q", 0.7071) == "0.71");
    CHECK (text ("b1_ratio", 3.0) == "3.00");
}

TEST_CASE ("value text: what a user types is read with or without its unit, a k, or a decimal comma")
{
    INFO ("locale in effect: " << std::string (likyutest::appliedLocaleName()));

    CHECK (read ("b1_freq", "1.2 kHz") == doctest::Approx (1200.0));
    CHECK (read ("b1_freq", "1,2k") == doctest::Approx (1200.0));
    CHECK (read ("b1_freq", "500") == doctest::Approx (500.0));
    CHECK (read ("b1_freq", "500 Hz") == doctest::Approx (500.0));
    CHECK (read ("b1_freq", "99 kHz") == doctest::Approx (20000.0));    // clamped

    CHECK (read ("b1_gain", "-4,5 dB") == doctest::Approx (-4.5));
    CHECK (read ("b1_gain", "+3 dB") == doctest::Approx (3.0));
    CHECK (read ("b1_gain", "-99") == doctest::Approx (-30.0));

    CHECK (read ("b1_rel", "0.2 s") == doctest::Approx (200.0));
    CHECK (read ("b1_rel", "250 ms") == doctest::Approx (250.0));
    CHECK (read ("b1_fconf", "35 %") == doctest::Approx (35.0));
    CHECK (read ("b1_ttime", "1.5x") == doctest::Approx (1.5));
}

TEST_CASE ("value text: every continuous parameter reads back what it shows, to the precision shown")
{
    for (const auto& s : dsp::parameterTable())
    {
        if (s.kind != dsp::ParamKind::continuous)
            continue;

        for (int i = 0; i <= 20; ++i)
        {
            const auto value = dsp::fromNormalised (s, i / 20.0);
            const auto shown = core::valueText (s, value);
            const auto back = core::valueFromText (s, shown);

            INFO ("id " << s.id << ", value " << value << ", shown \"" << shown.toStdString() << "\"");

            // Within half a unit of the last digit shown: the worst is 2 significant digits on 0.05 ms.
            CHECK (std::abs (back - value) <= 0.051 * std::max (1.0, std::abs (value)));
        }
    }
}
