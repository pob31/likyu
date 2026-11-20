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
    VALUE TEXT: what a host shows of a continuous parameter, and what it
    understands when a user types one (Phase 1, finding 6: Samplitude and
    REAPER's generic view of the AU show the plugin's own text, and showed
    seven decimals and no unit).

        Hz    "80.0 Hz", "500 Hz", "1.20 kHz", "14.0 kHz"
        dB    "-4.5 dB", "0.0 dB", "+3.0 dB"
        ms    "0.05 ms", "5.00 ms", "120 ms"
        %     "50 %"
        x     "1.00x"
        none  "0.71" (Q, ratio)

    Reading accepts the number with or without its unit, "k" or "kHz" for a
    frequency, "s" for a time in ms, and a decimal comma; the result is
    clamped to the range. Number text is JUCE's, which does not follow the
    C locale: tests/core/ValueTextTests.cpp runs under C and fr_FR.

    Choices and toggles carry their own text (the table's choice names).
*/

#include <dsp/Parameters.h>

#include <juce_core/juce_core.h>

namespace likyu::core
{
    /** A continuous parameter's value as text, unit included. */
    juce::String valueText (const dsp::ParamSpec& spec, double value);

    /** The value a text names, in the parameter's units, clamped to its range. */
    double valueFromText (const dsp::ParamSpec& spec, const juce::String& text);
}
