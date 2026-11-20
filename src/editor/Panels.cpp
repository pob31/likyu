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

#include "Panels.h"

#include "Palette.h"

#include <algorithm>
#include <cmath>

namespace likyu::editor
{
    namespace
    {
        using P = dsp::BandParam;

        juce::Font fontOf (float height, bool bold = false)
        {
            return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
        }

        juce::String labelFor (P p)
        {
            return juce::String (presentation::labelOf (p));
        }
    }

    //==========================================================================
    GlobalRow::GlobalRow (Host& host)
        : bypass (host, dsp::indexOf (dsp::GlobalParam::bypass), "Bypass", palette::warning, false),
          outputGain (host, dsp::indexOf (dsp::GlobalParam::outGain), "Output", palette::accent),
          lookahead (host, dsp::indexOf (dsp::GlobalParam::lookahead), "Lookahead", palette::accent, false),
          link (host, dsp::indexOf (dsp::GlobalParam::link), "Link", palette::accent, false)
    {
        outputGain.setTextBeside (true);

        flatten.onLongPress = [this]
        {
            if (onFlatten != nullptr)
                onFlatten();
        };

        addAndMakeVisible (flatten);

        addAndMakeVisible (bypass);
        addAndMakeVisible (outputGain);
        addAndMakeVisible (lookahead);
        addAndMakeVisible (link);
    }

    void GlobalRow::refresh()
    {
        bypass.refresh();
        outputGain.refresh();
        lookahead.refresh();
        link.refresh();
    }

    void GlobalRow::resized()
    {
        // Each control follows its caption, which paint() draws: see captionWidth there.
        auto area = getLocalBounds().reduced (10, 4);
        area.removeFromLeft (70);                                   // the name

        area.removeFromLeft (56);                                   // "Bypass"
        bypass.setBounds (area.removeFromLeft (22).withSizeKeepingCentre (20, 20));

        area.removeFromLeft (70);                                   // "Output"
        outputGain.setBounds (area.removeFromLeft (212));

        area.removeFromLeft (90);                                   // "Lookahead"
        lookahead.setBounds (area.removeFromLeft (92).withSizeKeepingCentre (92, 24));

        area.removeFromLeft (50);                                   // "Link"
        link.setBounds (area.removeFromLeft (104).withSizeKeepingCentre (104, 24));

        flatten.setBounds (area.removeFromRight (84).withSizeKeepingCentre (84, 24));
    }

    void GlobalRow::paint (juce::Graphics& g)
    {
        g.fillAll (palette::background);

        const auto caption = [this, &g] (const juce::Component& control, const juce::String& text)
        {
            g.drawText (text, juce::Rectangle<int> (control.getX() - 96, 0, 90, getHeight()),
                        juce::Justification::centredRight, false);
        };

        g.setColour (palette::textPrimary);
        g.setFont (fontOf (17.0f, true));
        g.drawText (juce::String::fromUTF8 ("Liky\xc5\xab"), juce::Rectangle<int> (12, 0, 70, getHeight()),
                    juce::Justification::centredLeft, false);

        g.setColour (palette::textSecondary);
        g.setFont (fontOf (12.0f));
        caption (bypass, "Bypass");
        caption (outputGain, "Output");
        caption (lookahead, "Lookahead");
        caption (link, "Link");
    }

    //==========================================================================
    BandColumn::BandColumn (Host& host, int bandIn)
        : band (bandIn),
          on (host, dsp::indexOf (bandIn, P::on), labelFor (P::on), palette::band (bandIn), false),
          type (host, dsp::indexOf (bandIn, P::type), labelFor (P::type), palette::band (bandIn), false),
          frequency (host, dsp::indexOf (bandIn, P::freq), labelFor (P::freq), palette::band (bandIn)),
          gain (host, dsp::indexOf (bandIn, P::gain), labelFor (P::gain), palette::band (bandIn)),
          q (host, dsp::indexOf (bandIn, P::q), labelFor (P::q), palette::band (bandIn))
    {
        for (auto* control : std::initializer_list<juce::Component*> { &on, &type, &frequency, &gain, &q })
            addAndMakeVisible (control);

        frequency.setMarker (true);
        q.setShowsWidth (true);

        // A press on any of its controls is a press in the column.
        addMouseListener (this, true);
    }

    BandColumn::~BandColumn()
    {
        removeMouseListener (this);
    }

    void BandColumn::show (const dsp::BandSettings& settings, bool isSelected)
    {
        const auto now = presentation::columnOf (settings);

        if (shownOnce && now.on == column.on && now.showsGain == column.showsGain
            && now.frequencyInUse == column.frequencyInUse && isSelected == selected)
            return;

        const auto layoutChanged = ! shownOnce || now.showsGain != column.showsGain;

        column = now;
        selected = isSelected;
        shownOnce = true;

        // An off band keeps its controls, faint; so does a Frequency its mode does not use.
        type.setDimmed (! column.on);
        gain.setDimmed (! column.on);
        q.setDimmed (! column.on);
        frequency.setDimmed (! column.on || ! column.frequencyInUse);
        gain.setVisible (column.showsGain);

        if (layoutChanged)
            resized();

        repaint();
    }

    void BandColumn::refresh()
    {
        on.refresh();
        type.refresh();
        frequency.refresh();
        gain.refresh();
        q.refresh();

        // A sharp band is a narrow mark on its Frequency bar, a broad band a wide one.
        frequency.setSpread (1.0 - q.shownNormalised());
    }

    void BandColumn::mouseDown (const juce::MouseEvent&)
    {
        if (onPress != nullptr)
            onPress();
    }

    void BandColumn::resized()
    {
        auto area = getLocalBounds().reduced (12, 0);
        area.removeFromTop (30);                                    // the name

        auto head = area.removeFromTop (24);
        on.setBounds (head.removeFromLeft (20).withSizeKeepingCentre (18, 18));
        head.removeFromLeft (6);
        type.setBounds (head);

        area.removeFromTop (18);
        frequency.setBounds (area.removeFromTop (42));

        area.removeFromTop (18);
        area.removeFromBottom (10);

        // Gain and Q side by side; Q alone in the middle for a type without a gain.
        if (column.showsGain)
        {
            gain.setBounds (area.removeFromLeft (area.getWidth() / 2));
            q.setBounds (area);
        }
        else
        {
            q.setBounds (area.withSizeKeepingCentre (area.getWidth() / 2, area.getHeight()));
        }
    }

    void BandColumn::paint (juce::Graphics& g)
    {
        const auto colour = palette::band (band);

        g.fillAll (selected ? palette::selectedColumn : palette::background);

        if (selected)
        {
            g.setColour (colour);
            g.fillRect (0, 0, getWidth(), 2);
        }

        g.setColour (column.on ? colour : colour.withMultipliedAlpha (0.55f));
        g.setFont (fontOf (13.0f, selected));
        g.drawText ("Band " + juce::String (band + 1), juce::Rectangle<int> (12, 7, getWidth() - 24, 18),
                    juce::Justification::centredLeft, false);
    }

    //==========================================================================
    namespace
    {
        constexpr int footTitleHeight = 26;
        constexpr int blockTitleHeight = 20;
        constexpr int cellWidth = 78, cellHeight = 76;
        constexpr int minimumCellWidth = 52;
        constexpr int blockGap = 22;
        constexpr int footMargin = 12;

        /** A mode's choice has more room than a dial needs: "Proportional" is read whole. */
        constexpr int choiceExtra = 22;

        /** The channels' cells keep the right of the panel, at the least this much of it. */
        constexpr int channelsWidth = 190;

        int totalColumns()
        {
            auto columns = 0;

            for (const auto count : presentation::footColumns)
                columns += count;

            return columns;
        }
    }

    //==========================================================================
    void ChannelCells::show (const std::vector<std::string>& namesIn, std::uint64_t leftOutIn, juce::Colour colourIn)
    {
        if (namesIn == names && leftOutIn == leftOut && colourIn == colour)
            return;

        names = namesIn;
        leftOut = leftOutIn;
        colour = colourIn;
        repaint();
    }

    /*  Cells of 26 pixels with their names beneath where they fit; smaller,
        and without names, where an instance has more channels than that
        leaves room for (64 at the most). */
    ChannelCells::Grid ChannelCells::grid() const
    {
        Grid g;
        const auto channels = std::max (1, count());

        for (const auto cell : { 26, 20, 16, 12, 9 })
        {
            g.cell = cell;
            g.named = cell >= 20;
            g.row = cell + (g.named ? 16 : 3);
            g.columns = std::max (1, getWidth() / cell);

            if (((channels + g.columns - 1) / g.columns) * g.row <= getHeight())
                break;
        }

        return g;
    }

    juce::Rectangle<int> ChannelCells::cellBounds (int channel) const
    {
        const auto g = grid();
        return { (channel % g.columns) * g.cell, (channel / g.columns) * g.row, g.cell, g.row };
    }

    void ChannelCells::paint (juce::Graphics& g)
    {
        const auto layout = grid();
        g.setFont (fontOf (10.0f));

        for (int c = 0; c < count(); ++c)
        {
            const auto area = cellBounds (c);
            const auto box = area.withHeight (layout.cell).toFloat().reduced (layout.cell >= 16 ? 3.0f : 1.0f);
            const auto in = ((leftOut >> c) & 1u) == 0;

            g.setColour (in ? colour : palette::controlTrack);
            g.fillRoundedRectangle (box, 3.0f);
            g.setColour (palette::controlBorder);
            g.drawRoundedRectangle (box, 3.0f, 1.0f);

            if (layout.named)
            {
                g.setColour (in ? palette::textPrimary : palette::textSecondary);
                g.drawText (juce::String (names[static_cast<std::size_t> (c)]),
                            area.withTrimmedTop (layout.cell - 1), juce::Justification::centredTop, false);
            }
        }
    }

    void ChannelCells::mouseDown (const juce::MouseEvent& e)
    {
        for (int c = 0; c < count(); ++c)
        {
            if (! cellBounds (c).contains (e.getPosition()))
                continue;

            leftOut ^= std::uint64_t (1) << c;
            repaint();

            if (onChange != nullptr)
                onChange (leftOut);

            return;
        }
    }

    //==========================================================================
    FootPanel::FootPanel (Host& hostIn)
        : host (hostIn)
    {
        addChildComponent (cells);

        cells.onChange = [this] (std::uint64_t leftOut)
        {
            if (band >= 0)
                host.setBandLeftOut (band, leftOut);
        };

        // Listen: the band's detector on the output, from a long press until the release.
        listen.onHeld = [this]
        {
            if (band >= 0)
            {
                listening = true;
                host.setListen (band);
            }
        };

        listen.onLetGo = [this] { stopListening(); };

        reset.onLongPress = [this]
        {
            if (band >= 0 && onReset != nullptr)
                onReset (band);
        };

        addAndMakeVisible (listen);
        addAndMakeVisible (reset);
    }

    FootPanel::~FootPanel()
    {
        stopListening();
    }

    void FootPanel::stopListening()
    {
        if (listening)
        {
            listening = false;
            host.setListen (-1);
        }
    }

    std::vector<int> FootPanel::parameters() const
    {
        std::vector<int> indices;

        for (const auto& control : controls)
            indices.push_back (control->parameterIndex());

        return indices;
    }

    void FootPanel::show (int bandIn, const dsp::BandSettings& settings)
    {
        std::vector<dsp::BandParam> now;

        for (const auto& section : presentation::footOf (settings))
            now.insert (now.end(), section.params.begin(), section.params.end());

        // Another band: a Listen in progress was the other band's.
        if (bandIn != band)
        {
            listen.cancel();
            stopListening();
        }

        // Nothing to hear on a band that is off or has no detector in use.
        listen.setEnabled (settings.on && presentation::usesDetector (settings));
        listen.setAccent (palette::band (bandIn));
        reset.setAccent (palette::warning);

        if (bandIn == band && now == shown)
            return;

        band = bandIn;
        shown = now;
        rebuild();
    }

    void FootPanel::rebuild()
    {
        controls.clear();

        const auto accent = palette::band (band);
        const auto& table = dsp::parameterTable();

        for (const auto p : shown)
        {
            const auto index = dsp::indexOf (band, p);
            const auto name = labelFor (p);
            std::unique_ptr<ParamControl> control;

            switch (table[static_cast<std::size_t> (index)].kind)
            {
                case dsp::ParamKind::toggle:
                    control = std::make_unique<ParamToggle> (host, index, name, accent, true);
                    break;

                case dsp::ParamKind::choice:
                    control = std::make_unique<ParamChoice> (host, index, name, accent, true);
                    break;

                case dsp::ParamKind::continuous:
                    control = std::make_unique<ParamDial> (host, index, name, accent);
                    break;
            }

            addAndMakeVisible (*control);
            control->refresh();
            controls.push_back (std::move (control));
        }

        resized();
        repaint();
    }

    juce::Rectangle<int> FootPanel::boundsOf (dsp::BandParam p) const
    {
        for (std::size_t i = 0; i < shown.size() && i < controls.size(); ++i)
            if (shown[i] == p)
                return controls[i]->getBounds();

        return {};
    }

    void FootPanel::refresh()
    {
        for (auto& control : controls)
            control->refresh();

        if (band < 0)
            return;

        // The band's channels: nothing to choose on a mono instance.
        const auto names = host.channelNames();
        const auto wanted = names.size() > 1;

        cells.show (names, host.bandLeftOut (band), palette::band (band));

        if (wanted != cells.isVisible())
        {
            cells.setVisible (wanted);
            repaint();
        }
    }

    /*  A column's width: its own where the panel has room for every block
        and the channels, narrower and all alike where it has not. It depends
        on the panel's width alone, never on the band's modes. */
    int FootPanel::cellWidthNow() const
    {
        const auto room = getWidth() - 2 * footMargin - channelsWidth
                          - (blockGap + choiceExtra) * presentation::numFootBlocks;
        return std::clamp (room / totalColumns(), minimumCellWidth, cellWidth);
    }

    juce::Rectangle<int> FootPanel::blockArea (presentation::FootBlock block) const
    {
        const auto cell = cellWidthNow();
        auto x = footMargin;

        for (int b = 0; b < static_cast<int> (block); ++b)
            x += presentation::footColumns[b] * cell + choiceExtra + blockGap;

        return { x, footTitleHeight, presentation::footColumns[static_cast<int> (block)] * cell + choiceExtra,
                 blockTitleHeight + presentation::footRows * cellHeight };
    }

    void FootPanel::resized()
    {
        const auto cell = cellWidthNow();
        const auto top = footTitleHeight + blockTitleHeight;

        // Every control at its own place (Presentation.h, footPlaceOf), whatever else is shown.
        for (std::size_t i = 0; i < shown.size() && i < controls.size(); ++i)
        {
            const auto place = presentation::footPlaceOf (shown[i]);
            const auto block = blockArea (place.block);

            // The block's first column, the mode's choice, is the wider one.
            const auto x = block.getX() + place.column * cell + (place.column > 0 ? choiceExtra : 0);
            const auto width = cell + (place.column == 0 ? choiceExtra : 0);

            controls[i]->setBounds (juce::Rectangle<int> (x, top + place.row * cellHeight, width, cellHeight).reduced (4, 4));
        }

        // On the title's line, at the right: Listen, then Reset.
        auto titleRow = juce::Rectangle<int> (0, 3, getWidth() - footMargin, footTitleHeight - 5);
        reset.setBounds (titleRow.removeFromRight (92));
        titleRow.removeFromRight (8);
        listen.setBounds (titleRow.removeFromRight (72));

        // What is left, to the right: the band's channels under their own title.
        const auto last = blockArea (presentation::FootBlock::key);
        const auto x = last.getRight() + blockGap;

        cellsTitle = { x, footTitleHeight, std::max (0, getWidth() - footMargin - x), blockTitleHeight };
        cells.setBounds (cellsTitle.getX(), top + 6, cellsTitle.getWidth(),
                         std::max (0, getHeight() - top - 12));
    }

    void FootPanel::paint (juce::Graphics& g)
    {
        g.fillAll (palette::foot);

        if (band < 0)
            return;

        const auto colour = palette::band (band);

        g.setColour (colour);
        g.fillRect (0, 0, getWidth(), 2);

        g.setFont (fontOf (13.0f, true));
        g.drawText ("Band " + juce::String (band + 1), juce::Rectangle<int> (12, 4, 120, footTitleHeight - 4),
                    juce::Justification::centredLeft, false);

        g.setFont (fontOf (11.0f));

        // Every block's title, always: a block with nothing to show keeps its room.
        for (int b = 0; b < presentation::numFootBlocks; ++b)
        {
            const auto block = static_cast<presentation::FootBlock> (b);
            const auto title = blockArea (block).withHeight (blockTitleHeight);

            g.setColour (palette::textSecondary);
            g.drawText (juce::String (presentation::footTitle (block)).toUpperCase(), title.withTrimmedLeft (4),
                        juce::Justification::centredLeft, false);

            g.setColour (palette::gridLine);
            g.fillRect (title.getX(), title.getBottom() - 1, title.getWidth(), 1);
        }

        if (cells.isVisible())
        {
            g.setColour (palette::textSecondary);
            g.drawText ("CHANNELS", cellsTitle.withTrimmedLeft (4), juce::Justification::centredLeft, false);

            g.setColour (palette::gridLine);
            g.fillRect (cellsTitle.getX(), cellsTitle.getBottom() - 1, cellsTitle.getWidth(), 1);
        }
    }
}
