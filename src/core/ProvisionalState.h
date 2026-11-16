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
    THE PROVISIONAL STATE CHUNK, `version` 0 (devplan, Phase 0b).

    The parameter values and nothing else, so that the Phase 1 host bench can
    save and reload a session. It is NEVER SHIPPED and has no fixture: state
    chunk version 1, with unknown fields kept and a reader for every version
    ever shipped, is Phase 2 work, and this file is deleted when it lands.

        { "version": 0, "parameters": { "bypass": 0, "out_gain": 0, "b1_on": 0, ... } }

    Values are in the table's real units (dsp/Parameters.h), keyed by
    parameter ID, never by position. Number text is juce::JSON's, which is
    locale-independent (tests/LocaleTests.cpp).
*/

#include <juce_core/juce_core.h>

#include <vector>

namespace likyu::core
{
    /** `values` in table order, numParams of them. */
    juce::String writeProvisionalState (const std::vector<double>& values);

    /** Overwrites the entries of `values` whose ID the text names, each clamped
        to its range; the others keep what they held (the caller passes
        defaults). False, and `values` untouched, when the text is not a
        version 0 chunk. */
    bool readProvisionalState (const juce::String& text, std::vector<double>& values);

    /** The table's defaults, in table order. */
    std::vector<double> defaultParameterValues();
}
