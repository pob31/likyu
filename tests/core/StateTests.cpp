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
    The state chunk, version 1 (core/State.h; acceptance row "state": round
    trip, every version's fixture, unknown fields, under C and fr_FR).

    The fixtures in tests/state/ were written once, from the PRD's table
    spelled by hand, not by this writer: they say what version 1 is.
*/

#include <doctest/doctest.h>

#include "../TestSupport.h"

#include <core/State.h>
#include <dsp/Parameters.h>

#include <map>
#include <string>

using namespace likyu;

namespace
{
    std::size_t at (const char* id)
    {
        const auto index = dsp::indexOfId (id);
        REQUIRE (index >= 0);
        return static_cast<std::size_t> (index);
    }

    juce::String load (const char* name)
    {
        const auto file = likyutest::stateFixture (name);
        REQUIRE (file.existsAsFile());
        return file.loadFileAsString();
    }

    /** What v1-basic.json changes from the defaults. */
    const std::map<std::string, double>& basicChanges()
    {
        static const std::map<std::string, double> changes {
            { "out_gain", 1.5 }, { "lookahead", 2 }, { "link", 1 },
            { "b1_on", 1 }, { "b1_type", 3 }, { "b1_freq", 35.5 },
            { "b3_on", 1 }, { "b3_freq", 3150 }, { "b3_gain", -4.5 }, { "b3_q", 2.5 },
            { "b4_on", 1 }, { "b4_qmode", 1 }, { "b4_gain", 9 },
            { "b5_atk", 0.5 }, { "b6_gmode", 2 }, { "b2_fspeed", 12 },
            { "b8_on", 1 }, { "b8_type", 4 }, { "b8_freq", 16000 },

            // The fixtures were written while fconf defaulted to 50 %; it is
            // 30 % since 2026-11-20 (PRD). A chunk keeps what it says.
            { "b1_fconf", 50 }, { "b2_fconf", 50 }, { "b3_fconf", 50 }, { "b4_fconf", 50 },
            { "b5_fconf", 50 }, { "b6_fconf", 50 }, { "b7_fconf", 50 }, { "b8_fconf", 50 },
        };

        return changes;
    }

    /** Every value as the defaults with `changes`, bands from `firstDefaultBand` on left at their defaults. */
    void checkValues (const std::vector<double>& values, const std::map<std::string, double>& changes, int firstDefaultBand = 8)
    {
        const auto& table = dsp::parameterTable();
        REQUIRE (values.size() == table.size());

        for (std::size_t i = 0; i < table.size(); ++i)
        {
            const auto& id = table[i].id;
            const auto found = changes.find (id);
            const auto bandNumber = id[0] == 'b' ? std::atoi (id.c_str() + 1) : 0;
            const auto expected = found != changes.end() && bandNumber <= firstDefaultBand ? found->second : table[i].defaultValue;

            INFO ("id: " << id);
            CHECK (values[i] == doctest::Approx (expected));
        }
    }
}

TEST_CASE ("state: the defaults are the table's")
{
    const auto values = core::defaultParameterValues();
    const auto& table = dsp::parameterTable();

    REQUIRE (values.size() == table.size());

    for (std::size_t i = 0; i < values.size(); ++i)
        CHECK (values[i] == table[i].defaultValue);
}

TEST_CASE ("state: the version 1 fixture reads as it says, and keeps nothing")
{
    INFO ("locale in effect: " << std::string (likyutest::appliedLocaleName()));

    core::StateChunk chunk;
    std::vector<double> values;

    REQUIRE (chunk.read (load ("v1-basic.json"), values));
    checkValues (values, basicChanges());
    CHECK (chunk.keptFields().isEmpty());
    CHECK (chunk.keptParameters().isEmpty());
}

TEST_CASE ("state: a round trip gives the same values, in locale-independent text")
{
    INFO ("locale in effect: " << std::string (likyutest::appliedLocaleName()));

    core::StateChunk chunk;
    std::vector<double> values;
    REQUIRE (chunk.read (load ("v1-basic.json"), values));

    const auto text = chunk.write (values);

    // A decimal comma anywhere would reopen wrong on another machine.
    CHECK (text.contains ("3150"));
    CHECK (text.contains ("-4.5"));
    CHECK_FALSE (text.contains ("-4,5"));
    CHECK (text.contains ("\"version\": 1"));

    core::StateChunk again;
    std::vector<double> reread;
    REQUIRE (again.read (text, reread));
    REQUIRE (reread.size() == values.size());

    for (std::size_t i = 0; i < values.size(); ++i)
    {
        INFO ("id: " << dsp::parameterTable()[i].id);
        CHECK (reread[i] == values[i]);
    }
}

TEST_CASE ("state: a chunk with fewer bands loads with the others at their defaults, off")
{
    core::StateChunk chunk;
    std::vector<double> values;

    REQUIRE (chunk.read (load ("v1-six-bands.json"), values));
    checkValues (values, basicChanges(), 6);
    CHECK (values[at ("b7_on")] == 0.0);
    CHECK (values[at ("b8_on")] == 0.0);
}

TEST_CASE ("state: a chunk with more bands keeps the surplus untouched and writes it back")
{
    core::StateChunk chunk;
    std::vector<double> values;

    REQUIRE (chunk.read (load ("v1-ten-bands.json"), values));
    checkValues (values, basicChanges());

    const auto& kept = chunk.keptParameters();
    CHECK (kept.size() == 2 * dsp::numBandParams);
    CHECK (static_cast<int> (kept["b9_on"]) == 1);
    CHECK (static_cast<double> (kept["b9_gain"]) == doctest::Approx (-3.0));
    CHECK (static_cast<int> (kept["b10_type"]) == 2);

    // Written back as they came, beside the build's own eight bands.
    const auto written = juce::JSON::parse (chunk.write (values));
    const auto parameters = written["parameters"];
    CHECK (static_cast<int> (parameters["b9_on"]) == 1);
    CHECK (static_cast<double> (parameters["b9_gain"]) == doctest::Approx (-3.0));
    CHECK (static_cast<int> (parameters["b10_type"]) == 2);
    CHECK (static_cast<double> (parameters["b8_freq"]) == doctest::Approx (16000.0));
}

TEST_CASE ("state: unknown fields, at the top and among the parameters, are kept and written back")
{
    core::StateChunk chunk;
    std::vector<double> values;

    REQUIRE (chunk.read (load ("v1-unknown-fields.json"), values));
    checkValues (values, basicChanges());

    const auto written = juce::JSON::parse (chunk.write (values));
    const auto original = juce::JSON::parse (load ("v1-unknown-fields.json"));

    for (const auto* field : { "editor", "linkLeftOut" })
    {
        INFO ("field " << field);
        CHECK (juce::JSON::toString (written[field]) == juce::JSON::toString (original[field]));
    }

    CHECK (static_cast<int> (written["parameters"]["b1_future"]) == 7);
    CHECK (written["parameters"]["global_future"].toString() == "text");
}

TEST_CASE ("state: values are clamped, choices and toggles made whole, and non-numbers take their default")
{
    core::StateChunk chunk;
    std::vector<double> values;

    REQUIRE (chunk.read (R"({ "version": 1, "parameters": { "b3_gain": 99.5, "b1_type": 2.6, "b1_on": 0.7,
                                                             "b2_freq": "loud", "lookahead": 9 } })", values));

    CHECK (values[at ("b3_gain")] == 30.0);
    CHECK (values[at ("b1_type")] == 3.0);
    CHECK (values[at ("b1_on")] == 1.0);
    CHECK (values[at ("b2_freq")] == dsp::parameterTable()[at ("b2_freq")].defaultValue);
    CHECK (values[at ("lookahead")] == 3.0);
}

TEST_CASE ("state: anything but a version 1 chunk is refused, and changes neither the values nor the kept fields")
{
    core::StateChunk chunk;
    std::vector<double> values;
    REQUIRE (chunk.read (load ("v1-unknown-fields.json"), values));

    const auto before = values;
    const auto keptBefore = chunk.keptFields().size();

    for (const auto* text : { "", "not json", "[]", R"({ "parameters": { "b1_freq": 440 } })",
                              R"({ "version": 0, "parameters": { "b1_freq": 440 } })",
                              R"({ "version": 2, "parameters": { "b1_freq": 440 } })",
                              R"({ "version": 1.5, "parameters": { "b1_freq": 440 } })",
                              R"({ "version": "1", "parameters": { "b1_freq": 440 } })",
                              R"({ "version": 1 })", R"({ "version": 1, "parameters": 3 })" })
    {
        INFO ("text: " << std::string (text));
        CHECK_FALSE (chunk.read (text, values));
        CHECK (values == before);
        CHECK (chunk.keptFields().size() == keptBefore);
    }
}
