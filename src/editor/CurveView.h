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
    THE CURVE (PRD, Editor; devplan 6b): the static response as a line, the
    live response as a second line over it, one node per band with its
    crosshair, the brackets of the band's modes.

    IT PAINTS WHAT THE PURE FUNCTIONS SAY: Presentation.h for what a node and
    its brackets are, CurveGeometry.h for where they are and what a press
    lands on, ResponseCurve.h for the two lines. What is decided here is
    pixels and colours only.

    GESTURES, as WFS-DIY's and Go.dot's EQs have taught them:
      - a node's centre drags frequency and gain; the horizontal arm
        frequency only, the vertical arm gain only (PRD);
      - every drag is relative to the value at the press; Shift is a fine
        drag, re-anchored when it changes mid-drag;
      - the wheel sets Q of the band under the pointer, else of the selected
        band; Ctrl turns it over (a Windows touchpad's pinch), Shift is fine;
      - a double click on a bell's or a shelf's node puts its gain at 0 dB;
      - the selected band's ghost node, where its Own detector listens,
        drags the detector's frequency, and the wheel over it sets the
        detector's Q. The other bands' ghosts are drawn faint and take no
        press (decided by the author 2026-11-20);
      - each pointer has its own drag, keyed by kind and index (JUCE numbers
        the mouse 0 and the first finger 0), so several nodes move at once;
      - a second finger on a node a finger holds, or on nothing while one
        finger holds a node, pinches that band's Q: closing narrows the
        band. The node's own drag stops there; a third finger is ignored.
        (Phase 1, finding 5: with multitouch on, Windows sends no pinch of
        its own.)

    refresh() is the editor's timer: it reads the values and the meters and
    repaints only when something it draws has changed.
*/

#include "CurveGeometry.h"
#include "EditorHost.h"
#include "Presentation.h"

#include <core/Dispatch.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace likyu::editor
{
    class CurveView final : public juce::Component
    {
    public:
        explicit CurveView (Host& host);
        ~CurveView() override;

        /** Reads the host's values and meters; repaints if the picture changed. */
        void refresh();

        /** The band the strip shows, from 0. */
        int selectedBand() const noexcept       { return selected; }
        void selectBand (int band);

        /** A band as the last refresh read it. */
        const dsp::BandSettings& settingsOf (int band) const noexcept
        {
            return settings[static_cast<std::size_t> (band)];
        }

        /** Called when a press selects another band. */
        std::function<void (int band)> onSelect;

        void paint (juce::Graphics&) override;
        void resized() override;

        /** A pointer: its kind (true for a touch), then its index within the kind. */
        using PointerKey = std::pair<bool, int>;

        /** What the mouse handlers do, by pointer: for them, and for a test,
            which has no touch screen. */
        void pointerDown (PointerKey key, juce::Point<float> at, juce::ModifierKeys mods);
        void pointerMoved (PointerKey key, juce::Point<float> at, juce::ModifierKeys mods);
        void pointerUp (PointerKey key);

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

    private:
        struct Drag
        {
            int band = -1;
            geometry::Part part = geometry::Part::none;
            juce::Point<float> press, last;
            bool fine = false;

            // The values at the press, or at the last change of Shift.
            double fromHz = 0.0, fromDb = 0.0, fromLow = 0.0, fromHigh = 0.0, fromDetectorHz = 0.0;

            bool movesFrequency = false, movesRange = false, movesGain = false, movesDetector = false;
        };

        /** Two fingers on one band: its Q from how far apart they are. */
        struct Pinch
        {
            int band = -1;
            PointerKey first, second;
            juce::Point<float> a, b;
            double fromQ = 1.0, fromDistance = 1.0;
        };

        static PointerKey keyOf (const juce::MouseEvent&) noexcept;

        void rebuild();
        void anchor (Drag&, juce::Point<float> at, bool fine) const;
        void forEachParameter (const Drag&, const std::function<void (int index)>&) const;
        void endDrag (const Drag&);
        void endAllDrags();
        bool isDragged (int band) const noexcept;
        geometry::Hit hitAt (const juce::MouseEvent&) const noexcept;
        geometry::Hit hitAt (juce::Point<float> at, bool touch) const noexcept;
        void endPinch();
        void setQ (int band, dsp::BandParam which, double q);

        void paintGrid (juce::Graphics&) const;
        void paintLines (juce::Graphics&) const;
        void paintBand (juce::Graphics&, int band) const;

        Host& host;

        core::Dispatch::Values values {};
        std::array<dsp::BandSettings, dsp::numBands> settings {};
        core::Meters meters;
        double rate = 0.0;
        bool read = false;

        geometry::Plot plot;
        geometry::Places places {};

        std::vector<double> hz, staticDb, liveDb;
        bool liveDiffers = false;

        int selected = 0;
        geometry::Hit hovered;
        std::map<PointerKey, Drag> drags;
        std::optional<Pinch> pinch;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CurveView)
    };
}
