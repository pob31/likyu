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

#include "ProvisionalState.h"

#include <dsp/Parameters.h>

#include <algorithm>

namespace likyu::core
{
    std::vector<double> defaultParameterValues()
    {
        const auto& table = dsp::parameterTable();

        std::vector<double> values;
        values.reserve (table.size());

        for (const auto& spec : table)
            values.push_back (spec.defaultValue);

        return values;
    }

    juce::String writeProvisionalState (const std::vector<double>& values)
    {
        const auto& table = dsp::parameterTable();

        juce::DynamicObject::Ptr parameters = new juce::DynamicObject();

        for (std::size_t i = 0; i < table.size() && i < values.size(); ++i)
            parameters->setProperty (juce::Identifier (table[i].id), values[i]);

        juce::DynamicObject::Ptr root = new juce::DynamicObject();
        root->setProperty ("version", 0);
        root->setProperty ("parameters", juce::var (parameters.get()));

        return juce::JSON::toString (juce::var (root.get()), true);
    }

    bool readProvisionalState (const juce::String& text, std::vector<double>& values)
    {
        juce::var root;

        if (juce::JSON::parse (text, root).failed() || ! root.isObject())
            return false;

        const auto version = root.getProperty ("version", juce::var());

        if (! (version.isInt() || version.isInt64() || version.isDouble()) || static_cast<int> (version) != 0)
            return false;

        const auto parameters = root.getProperty ("parameters", juce::var());

        if (! parameters.isObject())
            return false;

        const auto& table = dsp::parameterTable();
        values.resize (table.size());

        for (std::size_t i = 0; i < table.size(); ++i)
        {
            const auto& spec = table[i];
            const auto value = parameters.getProperty (juce::Identifier (spec.id), juce::var());

            if (value.isInt() || value.isInt64() || value.isDouble() || value.isBool())
                values[i] = std::clamp (static_cast<double> (value), spec.minimum, spec.maximum);
        }

        return true;
    }
}
