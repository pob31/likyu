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

#include "BandChannels.h"

#include <cmath>

namespace likyu::core
{
    namespace
    {
        const juce::Identifier layoutKey { "layout" };
        const juce::Identifier channelsKey { "channels" };
        const juce::Identifier bandsKey { "bands" };

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

        std::uint64_t allOf (int channels) noexcept
        {
            return channels >= maxIndex ? ~std::uint64_t (0) : (std::uint64_t (1) << channels) - 1u;
        }
    }

    std::uint64_t effectiveMask (const BandChannels& lists, int band, const juce::String& currentLayout,
                                 int currentChannels) noexcept
    {
        if (band < 0 || band >= dsp::numBands || currentChannels <= 0
            || lists.channels != currentChannels || lists.layout != currentLayout)
            return 0;

        return lists.leftOut[static_cast<std::size_t> (band)] & allOf (currentChannels);
    }

    BandChannels withBand (const BandChannels& lists, int band, std::uint64_t leftOut,
                           const juce::String& currentLayout, int currentChannels)
    {
        BandChannels result;
        result.layout = currentLayout;
        result.channels = currentChannels;

        if (currentChannels <= 0)
            return result;

        for (int b = 0; b < dsp::numBands; ++b)
            result.leftOut[static_cast<std::size_t> (b)] = effectiveMask (lists, b, currentLayout, currentChannels);

        if (band >= 0 && band < dsp::numBands)
            result.leftOut[static_cast<std::size_t> (band)] = leftOut & allOf (currentChannels);

        return result;
    }

    juce::var toVar (const BandChannels& lists)
    {
        juce::Array<juce::var> bands;

        for (const auto mask : lists.leftOut)
        {
            juce::Array<juce::var> indices;

            for (int c = 0; c < maxIndex; ++c)
                if (((mask >> c) & 1u) != 0)
                    indices.add (c);

            bands.add (juce::var (indices));
        }

        juce::DynamicObject::Ptr object = new juce::DynamicObject();
        object->setProperty (layoutKey, lists.layout);
        object->setProperty (channelsKey, lists.channels);
        object->setProperty (bandsKey, bands);
        return juce::var (object.get());
    }

    bool fromVar (const juce::var& v, BandChannels& lists)
    {
        if (! v.isObject())
            return false;

        const auto layout = v.getProperty (layoutKey, juce::var());
        const auto channels = v.getProperty (channelsKey, juce::var());
        const auto bands = v.getProperty (bandsKey, juce::var());

        if (! layout.isString() || ! isWhole (channels) || ! bands.isArray())
            return false;

        BandChannels read;
        read.layout = layout.toString();
        read.channels = juce::jlimit (0, maxIndex, static_cast<int> (channels));

        const auto& array = *bands.getArray();

        for (int b = 0; b < dsp::numBands && b < array.size(); ++b)
        {
            const auto& indices = array.getReference (b);

            if (! indices.isArray())
                continue;

            for (const auto& index : *indices.getArray())
            {
                if (! isWhole (index))
                    continue;

                const auto c = static_cast<juce::int64> (index);

                if (c >= 0 && c < maxIndex)
                    read.leftOut[static_cast<std::size_t> (b)] |= std::uint64_t (1) << c;
            }
        }

        lists = read;
        return true;
    }
}
