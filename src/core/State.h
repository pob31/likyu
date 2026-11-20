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
    THE STATE CHUNK, `version` 1 (PRD, Parameters and state).

        { "version": 1, "parameters": { "bypass": 0, "out_gain": 0.0, "b1_on": 1, "b1_freq": 30.0, ... } }

    - Values in the table's real units (dsp/Parameters.h), keyed by
      parameter ID, never by position. A choice is its index, a toggle 0 or 1.
    - JSON written and read by juce::JSON, whose number text does not follow
      the C locale (tests run under C and fr_FR).
    - UNKNOWN FIELDS ARE KEPT and written back: at the top level, and inside
      "parameters". That is also how a chunk from a build with MORE bands
      keeps its surplus bands (b9_...) untouched here.
    - An ID the chunk does not name takes its default: a chunk from a build
      with FEWER bands loads with the remaining bands off.
    - Anything that is not a version 1 chunk is refused, and the instance
      and the kept fields stay as they were.

    Not shipped until the parameter IDs freeze at the end of Phase 5
    (devplan): until then this format may change without a new version, and
    tests/state/ holds its fixtures. From the freeze on, every version ever
    shipped keeps a reader and a fixture.

    Message thread, or whichever thread the host saves and loads on; never
    the audio thread.
*/

#include <juce_core/juce_core.h>

#include <vector>

namespace likyu::core
{
    /** The table's defaults, in table order. */
    std::vector<double> defaultParameterValues();

    class StateChunk
    {
    public:
        static constexpr int version = 1;

        /** `values` in table order, numParams of them, with the fields kept
            from the last chunk read. */
        juce::String write (const std::vector<double>& values) const;

        /** Fills `values` (resized to the table) from a version 1 chunk:
            IDs it names, clamped to their ranges; the others, their
            defaults. Keeps its unknown fields. False, and nothing changed,
            when the text is not a version 1 chunk. */
        bool read (const juce::String& text, std::vector<double>& values);

        /** The fields kept from the last chunk read: top level, and inside "parameters". */
        const juce::NamedValueSet& keptFields() const noexcept      { return keptTop; }
        const juce::NamedValueSet& keptParameters() const noexcept  { return keptInParameters; }

    private:
        juce::NamedValueSet keptTop, keptInParameters;
    };
}
