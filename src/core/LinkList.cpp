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

#include "LinkList.h"

#include <cmath>

namespace likyu::core
{
    namespace
    {
        const juce::Identifier layoutKey { "layout" };
        const juce::Identifier channelsKey { "channels" };
        const juce::Identifier excludedKey { "excluded" };

        constexpr int maxIndex = 64;

        bool isWhole (const juce::var& v)
        {
            if (v.isInt() || v.isInt64())
                return true;

            if (! v.isDouble())
                return false;

            const auto d = static_cast<double> (v);
            return std::isfinite (d) && juce::exactlyEqual (d, std::round (d));
        }
    }

    std::uint64_t effectiveMask (const LinkList& list, const juce::String& currentLayout, int currentChannels) noexcept
    {
        if (currentChannels <= 0 || list.channels != currentChannels || list.layout != currentLayout)
            return 0;

        const auto all = currentChannels >= maxIndex ? ~std::uint64_t (0) : (std::uint64_t (1) << currentChannels) - 1u;
        return list.excluded & all;
    }

    juce::var toVar (const LinkList& list)
    {
        juce::Array<juce::var> indices;

        for (int c = 0; c < maxIndex; ++c)
            if (((list.excluded >> c) & 1u) != 0)
                indices.add (c);

        juce::DynamicObject::Ptr object = new juce::DynamicObject();
        object->setProperty (layoutKey, list.layout);
        object->setProperty (channelsKey, list.channels);
        object->setProperty (excludedKey, indices);
        return juce::var (object.get());
    }

    bool fromVar (const juce::var& v, LinkList& list)
    {
        if (! v.isObject())
            return false;

        const auto layout = v.getProperty (layoutKey, juce::var());
        const auto channels = v.getProperty (channelsKey, juce::var());
        const auto excluded = v.getProperty (excludedKey, juce::var());

        if (! layout.isString() || ! isWhole (channels) || ! excluded.isArray())
            return false;

        LinkList read;
        read.layout = layout.toString();
        read.channels = juce::jlimit (0, maxIndex, static_cast<int> (channels));

        for (const auto& index : *excluded.getArray())
        {
            if (! isWhole (index))
                continue;

            const auto c = static_cast<juce::int64> (index);

            if (c >= 0 && c < maxIndex)
                read.excluded |= std::uint64_t (1) << c;
        }

        list = read;
        return true;
    }
}
