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

    namespace
    {
        /** The room a combo box keeps at its right for its chevron. */
        constexpr int chevronZone = 20;
    }

    /*  The box with a small chevron in a zone of its own, which the text
        never enters (JUCE's own arrow is wider than the room it leaves the
        text in a narrow box). */
    void EditorRoot::Look::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                         int, int, int, int, juce::ComboBox& box)
    {
        const auto area = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height)).reduced (0.5f);

        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (area, 3.0f);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (area, 3.0f, 1.0f);

        const auto x = static_cast<float> (width) - 0.5f * static_cast<float> (chevronZone);
        const auto y = 0.5f * static_cast<float> (height);

        juce::Path chevron;
        chevron.startNewSubPath (x - 4.0f, y - 2.0f);
        chevron.lineTo (x, y + 2.5f);
        chevron.lineTo (x + 4.0f, y - 2.0f);

        g.setColour (box.findColour (juce::ComboBox::arrowColourId).withAlpha (box.isEnabled() ? 0.9f : 0.3f));
        g.strokePath (chevron, juce::PathStrokeType (1.6f));
    }

    /*  The text up to the chevron's zone, and squeezed before it is cut:
        "High shelf" and "Proportional" are read whole in a narrow box. */
    void EditorRoot::Look::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
    {
        label.setBounds (3, 1, box.getWidth() - 3 - chevronZone, box.getHeight() - 2);
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
        foot.onReset = [this] (int band) { resetBand (band); };
        global.onFlatten = [this] { flatten(); };

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

    void EditorRoot::resetBand (int band)
    {
        if (band < 0 || band >= dsp::numBands)
            return;

        const auto& table = dsp::parameterTable();

        for (int p = 0; p < dsp::numBandParams; ++p)
        {
            const auto index = dsp::indexOf (band, static_cast<dsp::BandParam> (p));
            host.beginGesture (index);
            host.setValue (index, table[static_cast<std::size_t> (index)].defaultValue);
            host.endGesture (index);
        }

        host.setBandLeftOut (band, 0);
        refreshNow();
    }

    void EditorRoot::flatten()
    {
        for (int band = 0; band < dsp::numBands; ++band)
            resetBand (band);
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
