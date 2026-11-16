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
    The provisional state chunk, version 0 (core/ProvisionalState.h). Runs
    under C and under fr_FR like everything that writes numbers.
*/

#include <doctest/doctest.h>

#include "../TestSupport.h"

#include <core/ProvisionalState.h>
#include <dsp/Parameters.h>

using namespace likyu;

namespace
{
    std::size_t at (int band, dsp::BandParam p)
    {
        return static_cast<std::size_t> (dsp::indexOf (band, p));
    }
}

TEST_CASE ("provisional state: defaults are the table's")
{
    const auto values = core::defaultParameterValues();
    const auto& table = dsp::parameterTable();

    REQUIRE (values.size() == table.size());

    for (std::size_t i = 0; i < values.size(); ++i)
        CHECK (values[i] == table[i].defaultValue);
}

TEST_CASE ("provisional state: a round trip returns the same values")
{
    INFO ("locale in effect: " << std::string (likyutest::appliedLocaleName()));

    auto written = core::defaultParameterValues();
    written[at (0, dsp::BandParam::freq)] = 123.456;
    written[at (2, dsp::BandParam::gain)] = -7.25;
    written[at (7, dsp::BandParam::atk)] = 0.05;
    written[at (4, dsp::BandParam::type)] = 3.0;
    written[at (5, dsp::BandParam::on)] = 1.0;
    written[static_cast<std::size_t> (dsp::indexOf (dsp::GlobalParam::lookahead))] = 2.0;

    const auto text = core::writeProvisionalState (written);

    // A decimal comma anywhere would be a chunk that reopens wrong on another machine.
    CHECK (text.contains ("123.456"));
    CHECK (text.contains ("-7.25"));
    CHECK_FALSE (text.contains ("123,456"));

    auto read = core::defaultParameterValues();
    REQUIRE (core::readProvisionalState (text, read));
    REQUIRE (read.size() == written.size());

    for (std::size_t i = 0; i < read.size(); ++i)
    {
        INFO ("id: " << dsp::parameterTable()[i].id);
        CHECK (read[i] == doctest::Approx (written[i]));
    }
}

TEST_CASE ("provisional state: values are keyed by ID, clamped, and missing ones keep their default")
{
    auto values = core::defaultParameterValues();

    REQUIRE (core::readProvisionalState (
        R"({ "version": 0, "parameters": { "b3_gain": 99.5, "b1_freq": 440, "no_such_parameter": 1 } })", values));

    CHECK (values[at (2, dsp::BandParam::gain)] == doctest::Approx (30.0));     // clamped to the range
    CHECK (values[at (0, dsp::BandParam::freq)] == doctest::Approx (440.0));
    CHECK (values[at (1, dsp::BandParam::freq)] == dsp::parameterTable()[at (1, dsp::BandParam::freq)].defaultValue);
}

TEST_CASE ("provisional state: anything that is not a version 0 chunk is refused and changes nothing")
{
    const auto defaults = core::defaultParameterValues();

    for (const auto* text : { "", "not json", "[]", R"({ "parameters": { "b1_freq": 440 } })",
                              R"({ "version": 1, "parameters": { "b1_freq": 440 } })",
                              R"({ "version": 0 })", R"({ "version": 0, "parameters": 3 })" })
    {
        auto values = defaults;
        INFO ("text: " << std::string (text));
        CHECK_FALSE (core::readProvisionalState (text, values));
        CHECK (values == defaults);
    }
}
