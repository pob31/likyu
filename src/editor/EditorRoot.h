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
    THE EDITOR'S ROOT (PRD, Editor: one resizable window; a plain
    juce::Component root, as in the OSCQuery plugin, so each shim can host
    it). From the top:

        the instance's row          GlobalRow
        the response curve          CurveView, which takes what height is left
        a column per band           BandColumn, eight side by side
        the selected band's foot    FootPanel

    The layout under the curve is WFS-DIY's output EQ's (decided by the
    author 2026-11-20). The root owns them, the selection that ties them, and
    a timer at 30 Hz, the rate the PRD gives the meter snapshot's reader. It
    knows its Host and nothing of a shim.
*/

#include "CurveView.h"
#include "EditorHost.h"
#include "Panels.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace likyu::editor
{
    class EditorRoot final : public juce::Component,
                             private juce::Timer
    {
    public:
        /** The window's sizes, for whoever hosts the root. At the default
            size the curve has the size the geometry's tests hold its hit
            areas at (CurveGeometry.h). */
        static constexpr int defaultWidth = static_cast<int> (geometry::defaultCurveWidth);
        static constexpr int defaultHeight = GlobalRow::height + static_cast<int> (geometry::defaultCurveHeight)
                                             + BandColumn::height + FootPanel::height;
        static constexpr int minimumWidth = 880, minimumHeight = 660;
        static constexpr int maximumWidth = 2400, maximumHeight = 1600;

        /** The meter snapshot's reader runs at about this rate (PRD, Detection). */
        static constexpr int refreshHz = 30;

        explicit EditorRoot (Host host);
        ~EditorRoot() override;

        /** What the timer does, at once: for a test or a snapshot, which run
            no message loop. */
        void refreshNow();

        /** The band the foot shows, from 0. */
        void selectBand (int band);

        CurveView& curveView() noexcept                 { return curve; }
        FootPanel& footPanel() noexcept                 { return foot; }
        BandColumn& bandColumn (int band) noexcept      { return *columns[static_cast<std::size_t> (band)]; }

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void timerCallback() override;

        /** The dark look, with combo boxes whose text fits a band's column. */
        struct Look final : juce::LookAndFeel_V4
        {
            Look();
            juce::Font getComboBoxFont (juce::ComboBox&) override;
            void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                               int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
            void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
        };

        Host host;
        Look look;

        GlobalRow global;
        CurveView curve;
        std::array<std::unique_ptr<BandColumn>, dsp::numBands> columns;
        FootPanel foot;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorRoot)
    };
}
