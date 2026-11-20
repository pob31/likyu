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
    THE COLOURS, in one place, for the author to change.

    They are WFS-DIY's and Go.dot's, so that the three read as one family:
    the dark palette WFS-DIY's plugins hold as constants
    (Plugin/Source/Shared/PluginLookAndFeel.h, wfs::plugin::DarkPalette), and
    the band colours both programs give their EQ nodes
    (spatcore/ui/EQDisplayComponent.h, getBandColour; Go.dot's
    engine/audio/EqColours.h is the first six of them).
*/

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace likyu::editor::palette
{
    inline const juce::Colour background    { 0xff1e1e1e };
    inline const juce::Colour plot          { 0xff1a1a1a };
    inline const juce::Colour gridLine      { 0xff404040 };
    inline const juce::Colour textPrimary   { 0xfffffffe };   // near-white: WFS-DIY avoids a macOS findColour issue with pure white
    inline const juce::Colour textSecondary { 0xffaaaaaa };
    inline const juce::Colour staticLine    { 0xfffffffe };
    inline const juce::Colour staticFill    { 0xff33668c };   // WFS-DIY's accentBlue
    inline const juce::Colour liveLine      { 0xffffb74d };   // the one colour that is not theirs: the live response
    inline const juce::Colour warning       { 0xffe67e22 };

    /** One colour per band, in WFS-DIY's order. */
    inline juce::Colour band (int index) noexcept
    {
        static const std::array<juce::Colour, 8> colours {
            juce::Colour { 0xffe74c3c },    // red
            juce::Colour { 0xffe67e22 },    // orange
            juce::Colour { 0xffffeb3b },    // yellow
            juce::Colour { 0xff2ecc71 },    // green
            juce::Colour { 0xff3498db },    // blue
            juce::Colour { 0xff9b59b6 },    // purple
            juce::Colour { 0xff1abc9c },    // teal
            juce::Colour { 0xffe91e63 }     // pink
        };

        return colours[static_cast<std::size_t> (index >= 0 ? index % 8 : 0)];
    }
}
