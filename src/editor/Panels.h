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
                   of its current choice and no others; and its channels
      TimesPanel   TEMPORARY, in the foot: the transient shaper's four time
                   constants, which slide over the Frequency block from a
                   chevron in the Gain block, while the band is in Transient
      ChannelCells in the foot: a cell per channel of the instance, its name
                   beneath; a press takes the channel out of the selected
                   band or back in (PRD, Band model, Channels)

    Which control is where is Presentation.h's (columnOf, footOf, labelOf);
    here they are only made and laid out.
*/

#include "EditorHost.h"
#include "LongPressButton.h"
#include "ParamControls.h"
#include "Presentation.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace likyu::editor
{
    /** A switch of the editor's own, not a parameter: lit while on. */
    class TextToggle final : public juce::Component
    {
    public:
        explicit TextToggle (juce::String text);

        bool isOn() const noexcept      { return on; }
        void setOn (bool shouldBeOn);

        std::function<void (bool on)> onChange;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        const juce::String text;
        bool on = false;
    };

    class GlobalRow final : public juce::Component
    {
    public:
        static constexpr int height = 44;

        explicit GlobalRow (Host& host);

        void refresh();
        void paint (juce::Graphics&) override;
        void resized() override;

        /** Every band back to its defaults, on a long press. */
        std::function<void()> onFlatten;

        LongPressButton& flattenButton() noexcept       { return flatten; }

        /** The analyser, off each time the editor opens. */
        TextToggle& analyserToggle() noexcept           { return analyser; }

    private:
        ParamToggle bypass;
        ParamBar outputGain;
        ParamChoice lookahead, link;
        LongPressButton flatten { "Flatten" };
        TextToggle analyser { "Analyser" };

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
    class ChannelCells final : public juce::Component
    {
    public:
        /** The channels' names, which of them the band leaves out, and the band's colour. */
        void show (const std::vector<std::string>& names, std::uint64_t leftOut, juce::Colour colour);

        int count() const noexcept      { return static_cast<int> (names.size()); }

        /** Where a channel's cell is, name included: for a press, and for a test. */
        juce::Rectangle<int> cellBounds (int channel) const;

        /** Called with the new set when a press changes it. */
        std::function<void (std::uint64_t leftOut)> onChange;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        struct Grid
        {
            int columns = 1, cell = 26, row = 36;
            bool named = true;
        };

        Grid grid() const;

        std::vector<std::string> names;
        std::uint64_t leftOut = 0;
        juce::Colour colour;
    };

    //==========================================================================
    /*  TEMPORARY, with the four host parameters it shows (decided by the
        author 2026-11-20: a panel that slides over the frequency controls,
        from a chevron, and closes when another band is selected). The
        constants are the instance's, not the band's. */
    class TimesPanel final : public juce::Component
    {
    public:
        explicit TimesPanel (Host& host);

        void refresh();
        void setCell (int width, int height);

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        ParamDial fastAttack, slowAttack, fastRelease, slowRelease;
        int cellWidth = 78, cellHeight = 76;
    };

    /** A chevron that opens what is to its right, and closes it. */
    class ChevronButton final : public juce::Component
    {
    public:
        std::function<void()> onClick;

        void setOpen (bool shouldBeOpen);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        bool open = false;
    };

    //==========================================================================
    class FootPanel final : public juce::Component
    {
    public:
        static constexpr int height = 204;

        explicit FootPanel (Host& host);
        ~FootPanel() override;

        /** The band whose modes are shown, and what it is now. */
        void show (int band, const dsp::BandSettings& settings);

        /** The shown band back to its defaults, on a long press. */
        std::function<void (int band)> onReset;

        LongPressButton& listenButton() noexcept        { return listen; }

        /** TEMPORARY: the transient's times. The chevron shows in Transient
            only; the panel slides open over a few refreshes. */
        ChevronButton& timesChevron() noexcept          { return chevron; }
        TimesPanel& timesPanel() noexcept               { return times; }
        bool timesAreOpen() const noexcept              { return timesWanted; }
        void setTimesOpen (bool shouldBeOpen);
        LongPressButton& resetButton() noexcept         { return reset; }
        void refresh();

        /** The table indices of the controls shown, in order: for a test. */
        std::vector<int> parameters() const;

        /** Where the control of a band parameter is, or an empty rectangle
            when it is not shown: for a test. */
        juce::Rectangle<int> boundsOf (dsp::BandParam) const;

        /** The selected band's channels; not shown on a mono instance. */
        ChannelCells& channelCells() noexcept       { return cells; }

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void rebuild();

        /** Where a block starts and how wide one of its columns is, for the width of the moment. */
        juce::Rectangle<int> blockArea (presentation::FootBlock) const;
        int cellWidthNow() const;

        Host& host;
        int band = -1;
        std::vector<dsp::BandParam> shown;                      // in order, one per control
        std::vector<std::unique_ptr<ParamControl>> controls;

        ChannelCells cells;
        juce::Rectangle<int> cellsTitle;

        // Listen: heard from the moment its press is long enough until the release.
        void stopListening();

        void layoutTimes();

        TimesPanel times;
        ChevronButton chevron;
        bool timesWanted = false;
        double openness = 0.0;

        LongPressButton listen { "Listen" };
        LongPressButton reset { "Reset band" };
        bool listening = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FootPanel)
    };
}
