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

#include "EditorRoot.h"

#include "Palette.h"

#include <utility>

namespace likyu::editor
{
    EditorRoot::Look::Look()
        : juce::LookAndFeel_V4 (juce::LookAndFeel_V4::getDarkColourScheme())
    {
        setColour (juce::PopupMenu::backgroundColourId, palette::controlBackground);
        setColour (juce::PopupMenu::textColourId, palette::textPrimary);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, palette::accent);
        setColour (juce::PopupMenu::highlightedTextColourId, palette::textPrimary);
    }

    juce::Font EditorRoot::Look::getComboBoxFont (juce::ComboBox&)
    {
        return juce::Font (juce::FontOptions (12.5f));
    }

    /*  The text up to the arrow, and squeezed before it is cut: "High shelf"
        and "Proportional" are read whole in a column an eighth of the window
        wide. */
    void EditorRoot::Look::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
    {
        label.setBounds (4, 1, box.getWidth() - 28, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
        label.setMinimumHorizontalScale (0.6f);
    }

    EditorRoot::EditorRoot (Host hostIn)
        : host (std::move (hostIn)),
          global (host),
          curve (host),
          foot (host)
    {
        setOpaque (true);

        setLookAndFeel (&look);

        addAndMakeVisible (global);
        addAndMakeVisible (curve);

        for (int band = 0; band < dsp::numBands; ++band)
        {
            auto& column = columns[static_cast<std::size_t> (band)];
            column = std::make_unique<BandColumn> (host, band);
            column->onPress = [this, band] { selectBand (band); };
            addAndMakeVisible (*column);
        }

        addAndMakeVisible (foot);

        curve.onSelect = [this] (int) { refreshNow(); };

        refreshNow();
        startTimerHz (refreshHz);
    }

    EditorRoot::~EditorRoot()
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    void EditorRoot::selectBand (int band)
    {
        if (band != curve.selectedBand())
        {
            curve.selectBand (band);
            refreshNow();
        }
    }

    void EditorRoot::refreshNow()
    {
        global.refresh();
        curve.refresh();

        const auto selected = curve.selectedBand();

        for (int band = 0; band < dsp::numBands; ++band)
        {
            auto& column = *columns[static_cast<std::size_t> (band)];
            column.show (curve.settingsOf (band), band == selected);
            column.refresh();
        }

        foot.show (selected, curve.settingsOf (selected));
        foot.refresh();
    }

    void EditorRoot::timerCallback()
    {
        refreshNow();
    }

    void EditorRoot::paint (juce::Graphics& g)
    {
        g.fillAll (palette::background);
    }

    void EditorRoot::resized()
    {
        auto area = getLocalBounds();

        global.setBounds (area.removeFromTop (GlobalRow::height));
        foot.setBounds (area.removeFromBottom (FootPanel::height));

        auto row = area.removeFromBottom (BandColumn::height);
        const auto width = row.getWidth() / dsp::numBands;

        for (int band = 0; band < dsp::numBands; ++band)
            columns[static_cast<std::size_t> (band)]->setBounds (band + 1 < dsp::numBands ? row.removeFromLeft (width) : row);

        curve.setBounds (area);
    }
}
