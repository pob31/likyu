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
        outputGain.setBounds (area.removeFromLeft (150));

        area.removeFromLeft (90);                                   // "Lookahead"
        lookahead.setBounds (area.removeFromLeft (92).withSizeKeepingCentre (92, 24));

        area.removeFromLeft (50);                                   // "Link"
        link.setBounds (area.removeFromLeft (104).withSizeKeepingCentre (104, 24));
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
    }

    void BandColumn::mouseDown (const juce::MouseEvent&)
    {
        if (onPress != nullptr)
            onPress();
    }

    void BandColumn::resized()
    {
        auto area = getLocalBounds().reduced (6, 0);
        area.removeFromTop (24);                                    // the name

        auto head = area.removeFromTop (24);
        on.setBounds (head.removeFromLeft (20).withSizeKeepingCentre (18, 18));
        head.removeFromLeft (5);
        type.setBounds (head);

        area.removeFromTop (8);
        frequency.setBounds (area.removeFromTop (36));

        area.removeFromTop (4);
        area.removeFromBottom (4);

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
        g.drawText ("Band " + juce::String (band + 1), juce::Rectangle<int> (8, 4, getWidth() - 16, 18),
                    juce::Justification::centredLeft, false);
    }

    //==========================================================================
    namespace
    {
        constexpr int footTitleHeight = 22;
        constexpr int blockTitleHeight = 16;
        constexpr int cellWidth = 84, cellHeight = 66;
        constexpr int blockGap = 14;
        constexpr int rows = 2;

        int columnsFor (std::size_t count)
        {
            return static_cast<int> ((count + rows - 1) / rows);
        }
    }

    FootPanel::FootPanel (Host& hostIn)
        : host (hostIn)
    {
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
        const auto sections = presentation::footOf (settings);

        std::vector<dsp::BandParam> now;

        for (const auto& section : sections)
            now.insert (now.end(), section.params.begin(), section.params.end());

        if (bandIn == band && now == shown)
            return;

        band = bandIn;
        shown = now;
        rebuild (sections);
    }

    void FootPanel::rebuild (const std::vector<presentation::StripSection>& sections)
    {
        controls.clear();
        blocks.clear();

        const auto accent = palette::band (band);
        const auto& table = dsp::parameterTable();

        for (const auto& section : sections)
        {
            Block block;
            block.title = juce::String (section.title);
            block.first = controls.size();
            block.count = section.params.size();
            blocks.push_back (block);

            for (const auto p : section.params)
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
        }

        resized();
        repaint();
    }

    void FootPanel::refresh()
    {
        for (auto& control : controls)
            control->refresh();
    }

    void FootPanel::resized()
    {
        auto columns = 0;

        for (const auto& block : blocks)
            columns += columnsFor (block.count);

        if (columns == 0)
            return;

        // Cells at their own width where there is room; narrower, all alike, where there is not.
        const auto room = getWidth() - 20 - blockGap * static_cast<int> (blocks.size() - 1);
        const auto cell = std::clamp (room / columns, 48, cellWidth);

        auto x = 10;
        const auto top = footTitleHeight + blockTitleHeight;

        for (auto& block : blocks)
        {
            const auto wide = columnsFor (block.count);
            block.area = { x, footTitleHeight, wide * cell, blockTitleHeight + rows * cellHeight };

            for (std::size_t i = 0; i < block.count; ++i)
            {
                // Down the first column, then the next: a mode's choice sits above its first control.
                const auto column = static_cast<int> (i) / rows, row = static_cast<int> (i) % rows;
                controls[block.first + i]->setBounds (juce::Rectangle<int> (x + column * cell, top + row * cellHeight, cell, cellHeight)
                                                          .reduced (3, 2));
            }

            x += wide * cell + blockGap;
        }
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

        for (const auto& block : blocks)
        {
            const auto title = block.area.withHeight (blockTitleHeight);

            g.setColour (palette::textSecondary);
            g.drawText (block.title.toUpperCase(), title.withTrimmedLeft (4), juce::Justification::centredLeft, false);

            g.setColour (palette::gridLine);
            g.fillRect (title.getX(), title.getBottom() - 1, title.getWidth(), 1);
        }
    }
}
