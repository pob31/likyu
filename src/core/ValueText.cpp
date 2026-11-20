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

#include "ValueText.h"

#include <algorithm>
#include <cmath>

namespace likyu::core
{
    namespace
    {
        /** Fixed decimals; 0 decimals as a whole number (JUCE reads 0 as "as many as needed"). */
        juce::String fixed (double value, int decimals)
        {
            if (decimals <= 0)
                return juce::String (static_cast<long long> (std::llround (value)));

            return juce::String (value, decimals);
        }
    }

    juce::String valueText (const dsp::ParamSpec& spec, double value)
    {
        const auto& unit = spec.unit;

        if (unit == "Hz")
        {
            if (value >= 999.5)
                return fixed (value / 1000.0, value >= 9995.0 ? 1 : 2) + " kHz";

            return fixed (value, value >= 99.95 ? 0 : 1) + " Hz";
        }

        if (unit == "dB")
        {
            // Never "-0.0 dB", and a sign on what is above zero.
            if (std::abs (value) < 0.05)
                return "0.0 dB";

            return (value > 0.0 ? "+" : "") + fixed (value, 1) + " dB";
        }

        if (unit == "ms")
            return fixed (value, value < 9.995 ? 2 : (value < 99.95 ? 1 : 0)) + " ms";

        if (unit == "%")
            return fixed (value, 0) + " %";

        if (unit == "x")
            return fixed (value, 2) + "x";

        return fixed (value, 2);
    }

    double valueFromText (const dsp::ParamSpec& spec, const juce::String& text)
    {
        const auto trimmed = text.trim().replaceCharacter (',', '.');
        const auto lower = trimmed.toLowerCase().removeCharacters (" ");

        auto scale = 1.0;

        if (spec.unit == "Hz" && (lower.endsWith ("khz") || lower.endsWith ("k")))
            scale = 1000.0;
        else if (spec.unit == "ms" && lower.endsWith ("s") && ! lower.endsWith ("ms"))
            scale = 1000.0;

        // JUCE reads the leading number and stops at the unit, whatever the C locale says.
        const auto value = trimmed.getDoubleValue() * scale;

        if (! std::isfinite (value))
            return spec.defaultValue;

        return std::clamp (value, spec.minimum, spec.maximum);
    }
}
