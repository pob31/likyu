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
    WHAT IS AROUND THE CURVE (decided by the author 2026-11-20, on seeing the
    first curve: the layout of WFS-DIY's output EQ).

      GlobalRow    above: the instance's four parameters
      BandColumn   below, one per band, always there: On, Type, Frequency,
                   Q and Gain, in the band's colour
      FootPanel    under the columns, one for all bands: the selected band's
                   gain mode, frequency mode and key, each with the controls
                   of its current choice and no others

    Which control is where is Presentation.h's (columnOf, footOf, labelOf);
    here they are only made and laid out.
*/

#include "EditorHost.h"
#include "ParamControls.h"
#include "Presentation.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace likyu::editor
{
    class GlobalRow final : public juce::Component
    {
    public:
        static constexpr int height = 44;

        explicit GlobalRow (Host& host);

        void refresh();
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        ParamToggle bypass;
        ParamBar outputGain;
        ParamChoice lookahead, link;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlobalRow)
    };

    //==========================================================================
    class BandColumn final : public juce::Component
    {
    public:
        static constexpr int height = 232;

        BandColumn (Host& host, int band);
        ~BandColumn() override;

        /** What the band is now, and whether it is the selected one. */
        void show (const dsp::BandSettings& settings, bool isSelected);
        void refresh();

        /** A press anywhere in the column, its controls included. */
        std::function<void()> onPress;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        const int band;
        ParamToggle on;
        ParamChoice type;
        ParamBar frequency;
        ParamDial gain, q;

        presentation::Column column;
        bool selected = false;
        bool shownOnce = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandColumn)
    };

    //==========================================================================
    class FootPanel final : public juce::Component
    {
    public:
        static constexpr int height = 204;

        explicit FootPanel (Host& host);

        /** The band whose modes are shown, and what it is now. */
        void show (int band, const dsp::BandSettings& settings);
        void refresh();

        /** The table indices of the controls shown, in order: for a test. */
        std::vector<int> parameters() const;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        struct Block
        {
            juce::String title;
            std::size_t first = 0, count = 0;
            juce::Rectangle<int> area;
        };

        void rebuild (const std::vector<presentation::StripSection>&);

        Host& host;
        int band = -1;
        std::vector<dsp::BandParam> shown;
        std::vector<Block> blocks;
        std::vector<std::unique_ptr<ParamControl>> controls;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FootPanel)
    };
}
