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
    The channels left out of the link (core/LinkList.h) and their place in
    the state chunk (core/State.h). Acceptance row "link exclusion", its
    state half: a new layout starts with every channel in; the list survives
    a state round trip.
*/

#include <doctest/doctest.h>

#include "../TestSupport.h"

#include <core/LinkList.h>
#include <core/State.h>

using namespace likyu;

namespace
{
    const juce::String fivePointOne { "L R C Lfe Ls Rs" };

    core::LinkList lfeOut()
    {
        return { fivePointOne, 6, std::uint64_t (1) << 3 };
    }
}

TEST_CASE ("link list: applied only to the layout it was made for; in any other, every channel is in")
{
    const auto list = lfeOut();

    CHECK (core::effectiveMask (list, fivePointOne, 6) == 8u);

    // Another arrangement of six channels, another width, no channels: every channel in.
    CHECK (core::effectiveMask (list, "1 2 3 4 5 6", 6) == 0u);
    CHECK (core::effectiveMask (list, "L R C Lfe Ls Rs Lrs Rrs", 8) == 0u);
    CHECK (core::effectiveMask (list, fivePointOne, 0) == 0u);

    // Indices past the layout's channels are not sent.
    auto wide = list;
    wide.excluded |= std::uint64_t (1) << 40;
    CHECK (core::effectiveMask (wide, fivePointOne, 6) == 8u);

    // Sixty-four channels: every bit counts.
    const core::LinkList all { "64", 64, ~std::uint64_t (0) };
    CHECK (core::effectiveMask (all, "64", 64) == ~std::uint64_t (0));
}

TEST_CASE ("link list: to and from its JSON form; anything else is refused and changes nothing")
{
    core::LinkList read;
    REQUIRE (core::fromVar (core::toVar (lfeOut()), read));
    CHECK (read == lfeOut());

    auto untouched = lfeOut();

    for (const auto* text : { "3", "[ 3 ]", "{ \"layout\": 5, \"channels\": 6, \"excluded\": [ 3 ] }",
                              "{ \"layout\": \"L R\", \"channels\": 2.5, \"excluded\": [] }",
                              "{ \"layout\": \"L R\", \"channels\": 2, \"excluded\": 1 }" })
    {
        INFO (std::string (text));
        CHECK_FALSE (core::fromVar (juce::JSON::parse (text), untouched));
        CHECK (untouched == lfeOut());
    }

    // Indices out of 0 to 63, or not whole, are dropped; the rest is read.
    REQUIRE (core::fromVar (juce::JSON::parse ("{ \"layout\": \"L R\", \"channels\": 2, \"excluded\": [ -1, 1, 64, 0.5, \"x\" ] }"), read));
    CHECK (read.excluded == 2u);
}

TEST_CASE ("state: the link list round trips, is not kept as an unknown field, and its fixture reads as it says")
{
    core::StateChunk chunk;
    std::vector<double> values;
    core::LinkList link;

    // The fixture, spelled by hand.
    const auto file = likyutest::stateFixture ("v1-link.json");
    REQUIRE (file.existsAsFile());
    REQUIRE (chunk.read (file.loadFileAsString(), values, link));
    CHECK (link == lfeOut());
    CHECK_FALSE (chunk.keptFields().contains ("link_out"));

    // Written back, read again: the same list, once.
    const auto text = chunk.write (values, link);
    CHECK (text.indexOf ("link_out") == text.lastIndexOf ("link_out"));

    core::StateChunk other;
    core::LinkList again;
    std::vector<double> otherValues;
    REQUIRE (other.read (text, otherValues, again));
    CHECK (again == lfeOut());
    CHECK (otherValues == values);

    // A fresh list after a load is the one written: no stale copy kept from the load.
    core::LinkList changed { fivePointOne, 6, 1u };
    core::LinkList afterChange;
    REQUIRE (other.read (chunk.write (values, changed), otherValues, afterChange));
    CHECK (afterChange == changed);

    // Every channel in: not written at all, and read back as every channel in.
    const auto plain = chunk.write (values, {});
    CHECK_FALSE (plain.contains ("link_out"));
    REQUIRE (other.read (plain, otherValues, afterChange));
    CHECK (afterChange == core::LinkList {});

    // A list that cannot be read: every channel in, and not kept.
    REQUIRE (other.read ("{ \"version\": 1, \"parameters\": {}, \"link_out\": \"nonsense\" }", otherValues, afterChange));
    CHECK (afterChange == core::LinkList {});
    CHECK_FALSE (other.keptFields().contains ("link_out"));
}
