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
    The parameter table against the PRD's two tables (Parameters and state).
*/

#include <doctest/doctest.h>

#include <dsp/Parameters.h>

#include <cctype>
#include <set>
#include <string>

using namespace likyu::dsp;

namespace
{
    /*  An LV2 symbol: [_a-zA-Z][_a-zA-Z0-9]*. Stricter than what JUCE's LV2
        wrapper asks of a parameter ID (that it survive URL escaping and
        Turtle sanitising unchanged), and it implies it: the wrapper's own
        check is in tests/plugin/ProcessorTests.cpp. */
    bool isSymbol (const std::string& text)
    {
        if (text.empty() || ! (std::isalpha (static_cast<unsigned char> (text[0])) || text[0] == '_'))
            return false;

        for (const auto c : text)
            if (! (std::isalnum (static_cast<unsigned char> (c)) || c == '_'))
                return false;

        return true;
    }

    const ParamSpec& band (int index, BandParam p)
    {
        return parameterTable()[static_cast<std::size_t> (indexOf (index, p))];
    }
}

TEST_CASE ("parameters: 284 in all, 35 per band")
{
    CHECK (parameterTable().size() == 284);
    CHECK (numGlobalParams == 4);
    CHECK (numBandParams == 35);
    CHECK (numBands == 8);
}

TEST_CASE ("parameters: every ID is unique and an LV2 symbol")
{
    std::set<std::string> seen;

    for (const auto& spec : parameterTable())
    {
        INFO ("id: " << spec.id);
        CHECK (isSymbol (spec.id));
        CHECK (seen.insert (spec.id).second);
        CHECK_FALSE (spec.name.empty());
    }
}

TEST_CASE ("parameters: the per-instance IDs, in order")
{
    const auto& table = parameterTable();

    CHECK (table[static_cast<std::size_t> (indexOf (GlobalParam::bypass))].id == "bypass");
    CHECK (table[static_cast<std::size_t> (indexOf (GlobalParam::outGain))].id == "out_gain");
    CHECK (table[static_cast<std::size_t> (indexOf (GlobalParam::lookahead))].id == "lookahead");
    CHECK (table[static_cast<std::size_t> (indexOf (GlobalParam::link))].id == "link");
}

TEST_CASE ("parameters: lookahead and link are not automatable, everything else is")
{
    for (const auto& spec : parameterTable())
    {
        INFO ("id: " << spec.id);
        CHECK (spec.automatable == (spec.id != "lookahead" && spec.id != "link"));
    }
}

TEST_CASE ("parameters: band IDs are b<n>_<suffix>, n from 1, in the PRD's order")
{
    static const char* const suffixes[] = {
        "on", "type", "freq", "q", "gain", "qmode", "gmode", "fmode",
        "key", "dfilt", "dtype", "dfreq", "dq",
        "range", "thresh", "ratio", "knee", "atk", "rel", "ddir",
        "tatk", "tsus", "ttime", "tfloor",
        "flo", "fhi", "fspeed", "fdir", "fsens", "fspan", "fatk", "frel",
        "fconf", "fhold", "fcgain"
    };

    REQUIRE (static_cast<int> (std::size (suffixes)) == numBandParams);

    for (int b = 0; b < numBands; ++b)
    {
        for (int p = 0; p < numBandParams; ++p)
        {
            const auto expected = "b" + std::to_string (b + 1) + "_" + suffixes[p];
            CHECK (band (b, static_cast<BandParam> (p)).id == expected);
        }
    }
}

TEST_CASE ("parameters: defaults sit inside their ranges, choices match their bounds")
{
    for (const auto& spec : parameterTable())
    {
        INFO ("id: " << spec.id);
        CHECK (spec.minimum < spec.maximum);
        CHECK (spec.defaultValue >= spec.minimum);
        CHECK (spec.defaultValue <= spec.maximum);

        if (spec.kind == ParamKind::choice)
        {
            CHECK (spec.choices.size() >= 2);
            CHECK (spec.minimum == 0.0);
            CHECK (spec.maximum == static_cast<double> (spec.choices.size()) - 1.0);
        }
        else
        {
            CHECK (spec.choices.empty());
        }

        // A logarithmic range cannot start at or below zero.
        if (spec.skew == ParamSkew::logarithmic)
            CHECK (spec.minimum > 0.0);
    }
}

TEST_CASE ("parameters: the PRD's ranges and defaults, band 3 as the sample")
{
    const auto check = [] (BandParam p, double minimum, double maximum, double value)
    {
        const auto& spec = band (2, p);
        INFO ("id: " << spec.id);
        CHECK (spec.minimum == doctest::Approx (minimum));
        CHECK (spec.maximum == doctest::Approx (maximum));
        CHECK (spec.defaultValue == doctest::Approx (value));
    };

    check (BandParam::on, 0, 1, 0);
    check (BandParam::q, 0.1, 40, 1);
    check (BandParam::gain, -30, 30, 0);
    check (BandParam::dfreq, 20, 20000, 1000);
    check (BandParam::range, -30, 30, -6);
    check (BandParam::thresh, -80, 0, -24);
    check (BandParam::ratio, 1, 20, 3);
    check (BandParam::knee, 0, 24, 6);
    check (BandParam::atk, 0.05, 200, 5);
    check (BandParam::rel, 5, 2000, 120);
    check (BandParam::tatk, -100, 100, 0);
    check (BandParam::tsus, -100, 100, 0);
    check (BandParam::ttime, 0.25, 4, 1);
    check (BandParam::tfloor, -90, -20, -60);
    check (BandParam::fspeed, 1, 1000, 50);
    check (BandParam::fsens, -80, 0, -40);
    check (BandParam::fspan, 6, 60, 30);
    check (BandParam::fconf, 0, 100, 50);
    check (BandParam::fhold, 0, 2000, 200);

    CHECK (band (2, BandParam::type).choices.size() == 7);
    CHECK (band (2, BandParam::gmode).choices.size() == 3);
    CHECK (band (2, BandParam::fmode).choices.size() == 3);
    CHECK (band (2, BandParam::dtype).choices.size() == 3);
    CHECK (band (2, BandParam::freq).unit == "Hz");
    CHECK (band (2, BandParam::gain).unit == "dB");
    CHECK (band (2, BandParam::atk).unit == "ms");
}

TEST_CASE ("parameters: Low and High default to an octave either side of the band's default frequency")
{
    for (int b = 0; b < numBands; ++b)
    {
        const auto frequency = band (b, BandParam::freq).defaultValue;
        const auto low = band (b, BandParam::flo).defaultValue;
        const auto high = band (b, BandParam::fhi).defaultValue;

        INFO ("band " << (b + 1));
        CHECK (low < frequency);
        CHECK (frequency < high);
        CHECK (low == doctest::Approx (frequency * 0.5 < 20.0 ? 20.0 : frequency * 0.5));
        CHECK (high == doctest::Approx (frequency * 2.0 > 20000.0 ? 20000.0 : frequency * 2.0));
    }

    // Spread: each band's default frequency above the last.
    for (int b = 1; b < numBands; ++b)
        CHECK (band (b, BandParam::freq).defaultValue > band (b - 1, BandParam::freq).defaultValue);
}

TEST_CASE ("lookahead: Off, 0.25 ms, 1 ms, 3 ms in whole samples")
{
    CHECK (lookaheadSamples (0, 48000.0) == 0);
    CHECK (lookaheadSamples (1, 48000.0) == 12);
    CHECK (lookaheadSamples (2, 48000.0) == 48);
    CHECK (lookaheadSamples (3, 48000.0) == 144);

    CHECK (lookaheadSamples (1, 44100.0) == 11);    // 11.025
    CHECK (lookaheadSamples (2, 44100.0) == 44);    // 44.1
    CHECK (lookaheadSamples (3, 44100.0) == 132);   // 132.3
    CHECK (lookaheadSamples (3, 192000.0) == 576);

    CHECK (maxLookaheadSamples (192000.0) == 576);
    CHECK (maxLookaheadSamples (44100.0) == 132);

    // Out of range, or no rate yet: no delay.
    CHECK (lookaheadSamples (-1, 48000.0) == 0);
    CHECK (lookaheadSamples (4, 48000.0) == 0);
    CHECK (lookaheadSamples (3, 0.0) == 0);

    const auto& spec = parameterTable()[static_cast<std::size_t> (indexOf (GlobalParam::lookahead))];
    CHECK (static_cast<int> (spec.choices.size()) == numLookaheadChoices);
}
