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
    The channels each band leaves out, in the core (core/BandChannels.h; PRD,
    Band model, Channels): which layout the lists apply to, their JSON form,
    and their place in the state chunk beside the link list.
*/

#include <doctest/doctest.h>

#include "../TestSupport.h"

#include <core/BandChannels.h>
#include <core/State.h>

using namespace likyu;

namespace
{
    const juce::String fivePointOne { "L R C Lfe Ls Rs" };
    constexpr std::uint64_t lfe = std::uint64_t (1) << 3;
    constexpr std::uint64_t allButLfe = 0x3f & ~lfe;

    /** A low cut the LFE is spared on band 1, a band for the LFE alone on band 8. */
    core::BandChannels lfeApart()
    {
        core::BandChannels lists;
        lists.layout = fivePointOne;
        lists.channels = 6;
        lists.leftOut[0] = lfe;
        lists.leftOut[7] = allButLfe;
        return lists;
    }
}

TEST_CASE ("band channels: applied only to the layout they were made for; in any other, every band is on every channel")
{
    const auto lists = lfeApart();
    CHECK (lists.any());
    CHECK_FALSE (core::BandChannels {}.any());

    CHECK (core::effectiveMask (lists, 0, fivePointOne, 6) == lfe);
    CHECK (core::effectiveMask (lists, 7, fivePointOne, 6) == allButLfe);
    CHECK (core::effectiveMask (lists, 3, fivePointOne, 6) == 0);

    // Another arrangement, another width, no channels at all, no such band.
    CHECK (core::effectiveMask (lists, 0, "L R C Lfe Ls Rs Lrs Rrs", 8) == 0);
    CHECK (core::effectiveMask (lists, 0, "1 2 3 4 5 6", 6) == 0);
    CHECK (core::effectiveMask (lists, 0, fivePointOne, 0) == 0);
    CHECK (core::effectiveMask (lists, -1, fivePointOne, 6) == 0);
    CHECK (core::effectiveMask (lists, dsp::numBands, fivePointOne, 6) == 0);

    // Bits beyond the layout's channels are not the layout's.
    auto wide = lists;
    wide.leftOut[2] = ~std::uint64_t (0);
    CHECK (core::effectiveMask (wide, 2, fivePointOne, 6) == 0x3f);
}

TEST_CASE ("band channels: one band edited in the layout of the moment")
{
    // In their own layout: that band changes, the others stay.
    auto edited = core::withBand (lfeApart(), 3, 0x30, fivePointOne, 6);
    CHECK (edited.layout == fivePointOne);
    CHECK (edited.channels == 6);
    CHECK (edited.leftOut[0] == lfe);
    CHECK (edited.leftOut[3] == 0x30);
    CHECK (edited.leftOut[7] == allButLfe);

    // In another layout the old lists did not apply: they are replaced, the other bands on every channel.
    edited = core::withBand (lfeApart(), 3, 0x2, "L R", 2);
    CHECK (edited.layout == "L R");
    CHECK (edited.channels == 2);
    CHECK (edited.leftOut[0] == 0);
    CHECK (edited.leftOut[3] == 0x2);
    CHECK (edited.leftOut[7] == 0);

    // Bits beyond the layout are dropped; a band that does not exist changes nothing.
    CHECK (core::withBand ({}, 0, ~std::uint64_t (0), "L R", 2).leftOut[0] == 0x3);
    CHECK (core::withBand (lfeApart(), 99, 0x1, fivePointOne, 6) == lfeApart());
}

TEST_CASE ("band channels: to and from their JSON form; anything else is refused and changes nothing")
{
    const auto lists = lfeApart();
    core::BandChannels read;
    REQUIRE (core::fromVar (core::toVar (lists), read));
    CHECK (read == lists);

    const auto text = juce::JSON::toString (core::toVar (lists), true);
    CHECK (text.contains ("\"bands\": [[3], [], [], [], [], [], [], [0, 1, 2, 4, 5]]"));

    // Fewer bands listed: the rest leave nothing out. More, a list that is no list, an index out of range: passed over.
    REQUIRE (core::fromVar (juce::JSON::parse ("{ \"layout\": \"L R\", \"channels\": 2, \"bands\": [ [ 1 ], \"x\", [ 0, 64, -1, 1.5 ] ] }"), read));
    CHECK (read.layout == "L R");
    CHECK (read.leftOut[0] == 0x2);
    CHECK (read.leftOut[1] == 0);
    CHECK (read.leftOut[2] == 0x1);
    CHECK (read.leftOut[3] == 0);

    REQUIRE (core::fromVar (juce::JSON::parse ("{ \"layout\": \"L R\", \"channels\": 2, \"bands\": [ [], [], [], [], [], [], [], [], [ 0 ], [ 1 ] ] }"), read));
    CHECK_FALSE (read.any());

    const auto before = lfeApart();
    read = before;
    CHECK_FALSE (core::fromVar (juce::var ("nonsense"), read));
    CHECK_FALSE (core::fromVar (juce::JSON::parse ("{ \"layout\": \"L R\", \"channels\": 2 }"), read));
    CHECK_FALSE (core::fromVar (juce::JSON::parse ("{ \"layout\": 3, \"channels\": 2, \"bands\": [] }"), read));
    CHECK (read == before);
}

TEST_CASE ("state: the band channels round trip, are not kept as an unknown field, and their fixture reads as it says; a retired link list is kept as unknown")
{
    core::StateChunk chunk;
    std::vector<double> values;
    core::BandChannels lists;

    // The fixture, spelled by hand.
    const auto file = likyutest::stateFixture ("v1-band-out.json");
    REQUIRE (file.existsAsFile());
    REQUIRE (chunk.read (file.loadFileAsString(), values, lists));
    CHECK (lists == lfeApart());
    CHECK_FALSE (chunk.keptFields().contains ("band_out"));

    // Written back, read again: the same lists, once.
    const auto text = chunk.write (values, lists);
    CHECK (text.indexOf ("band_out") == text.lastIndexOf ("band_out"));

    core::StateChunk other;
    core::BandChannels again;
    std::vector<double> otherValues;
    REQUIRE (other.read (text, otherValues, again));
    CHECK (again == lfeApart());
    CHECK (otherValues == values);

    // Every band on every channel: not written at all, and read back so.
    const auto plain = chunk.write (values, {});
    CHECK_FALSE (plain.contains ("band_out"));
    REQUIRE (other.read (plain, otherValues, again));
    CHECK_FALSE (again.any());

    // The reader that does not ask for them still reads the chunk.
    REQUIRE (other.read (text, otherValues));
    CHECK_FALSE (other.keptFields().contains ("band_out"));

    // Lists that cannot be read: every channel, and not kept.
    REQUIRE (other.read ("{ \"version\": 1, \"parameters\": {}, \"band_out\": \"nonsense\" }", otherValues, again));
    CHECK_FALSE (again.any());
    CHECK_FALSE (other.keptFields().contains ("band_out"));

    // The list of channels left out of the link, retired 2026-11-20: a chunk of an earlier build reads,
    // and its field is kept and written back like any field this build does not know.
    const auto earlier = "{ \"version\": 1, \"parameters\": { \"b1_on\": 1 }, "
                         "\"link_out\": { \"layout\": \"L R C Lfe Ls Rs\", \"channels\": 6, \"excluded\": [ 3 ] } }";
    REQUIRE (other.read (earlier, otherValues, again));
    CHECK_FALSE (again.any());
    CHECK (other.keptFields().contains ("link_out"));
    CHECK (other.write (otherValues).contains ("link_out"));
}
