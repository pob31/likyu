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

#include "State.h"

#include <dsp/Parameters.h>

#include <algorithm>
#include <cmath>

namespace likyu::core
{
    namespace
    {
        const juce::Identifier versionKey { "version" };
        const juce::Identifier parametersKey { "parameters" };
        const juce::Identifier linkKey { "link_out" };

        bool isNumber (const juce::var& v)
        {
            return v.isInt() || v.isInt64() || v.isDouble() || v.isBool();
        }

        /** A value as the table allows it: clamped, and a choice or toggle whole. */
        double legal (const dsp::ParamSpec& spec, double value)
        {
            if (! std::isfinite (value))
                return spec.defaultValue;

            const auto clamped = std::clamp (value, spec.minimum, spec.maximum);

            switch (spec.kind)
            {
                case dsp::ParamKind::toggle:        return clamped >= 0.5 ? 1.0 : 0.0;
                case dsp::ParamKind::choice:        return std::round (clamped);
                case dsp::ParamKind::continuous:    return clamped;
            }

            return clamped;
        }
    }

    std::vector<double> defaultParameterValues()
    {
        const auto& table = dsp::parameterTable();

        std::vector<double> values;
        values.reserve (table.size());

        for (const auto& spec : table)
            values.push_back (spec.defaultValue);

        return values;
    }

    juce::String StateChunk::write (const std::vector<double>& values, const LinkList& link) const
    {
        const auto& table = dsp::parameterTable();

        juce::DynamicObject::Ptr parameters = new juce::DynamicObject();

        for (std::size_t i = 0; i < table.size(); ++i)
        {
            const auto& spec = table[i];
            const auto value = legal (spec, i < values.size() ? values[i] : spec.defaultValue);

            if (spec.kind == dsp::ParamKind::continuous)
                parameters->setProperty (juce::Identifier (spec.id), value);
            else
                parameters->setProperty (juce::Identifier (spec.id), static_cast<int> (value));
        }

        for (const auto& kept : keptInParameters)
            parameters->setProperty (kept.name, kept.value);

        juce::DynamicObject::Ptr root = new juce::DynamicObject();
        root->setProperty (versionKey, version);
        root->setProperty (parametersKey, juce::var (parameters.get()));

        if (link.excluded != 0)
            root->setProperty (linkKey, toVar (link));

        for (const auto& kept : keptTop)
            root->setProperty (kept.name, kept.value);

        return juce::JSON::toString (juce::var (root.get()), false);
    }

    bool StateChunk::read (const juce::String& text, std::vector<double>& values)
    {
        LinkList ignored;
        return read (text, values, ignored);
    }

    bool StateChunk::read (const juce::String& text, std::vector<double>& values, LinkList& link)
    {
        juce::var root;

        if (juce::JSON::parse (text, root).failed() || ! root.isObject())
            return false;

        const auto chunkVersion = root.getProperty (versionKey, juce::var());

        if (! (chunkVersion.isInt() || chunkVersion.isInt64() || chunkVersion.isDouble())
            || ! juce::exactlyEqual (static_cast<double> (chunkVersion), static_cast<double> (version)))
            return false;

        const auto parameters = root.getProperty (parametersKey, juce::var());

        if (! parameters.isObject())
            return false;

        const auto& table = dsp::parameterTable();
        auto result = defaultParameterValues();
        juce::NamedValueSet unknownParameters, unknownTop;

        for (const auto& property : parameters.getDynamicObject()->getProperties())
        {
            const auto index = dsp::indexOfId (property.name.toString().toStdString());

            if (index < 0)
            {
                unknownParameters.set (property.name, property.value);
                continue;
            }

            if (isNumber (property.value))
            {
                const auto i = static_cast<std::size_t> (index);
                result[i] = legal (table[i], static_cast<double> (property.value));
            }
        }

        for (const auto& property : root.getDynamicObject()->getProperties())
            if (property.name != versionKey && property.name != parametersKey && property.name != linkKey)
                unknownTop.set (property.name, property.value);

        LinkList list;

        if (! fromVar (root.getProperty (linkKey, juce::var()), list))
            list = {};

        values = std::move (result);
        link = list;
        keptInParameters = std::move (unknownParameters);
        keptTop = std::move (unknownTop);
        return true;
    }
}
