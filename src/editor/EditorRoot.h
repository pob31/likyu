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
    THE EDITOR'S ROOT (PRD, Editor: one resizable window, the response curve
    on top, one strip for the selected band below; a plain juce::Component
    root, as in the OSCQuery plugin, so each shim can host it).

    It owns the curve and a timer at 30 Hz, the rate the PRD gives the meter
    snapshot's reader. It knows its Host and nothing of a shim.

    devplan 6b: the curve only. The strip is 6d; until then whoever hosts
    the root may put a stand-in below the curve (setStandIn), which the JUCE
    shim fills with JUCE's generic editor so that no build loses a parameter.
*/

#include "CurveView.h"
#include "EditorHost.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace likyu::editor
{
    class EditorRoot final : public juce::Component,
                             private juce::Timer
    {
    public:
        /** The window's sizes, for whoever hosts the root. */
        static constexpr int defaultWidth = 960, defaultHeight = 640;
        static constexpr int minimumWidth = 720, minimumHeight = 460;
        static constexpr int maximumWidth = 2400, maximumHeight = 1600;

        /** The meter snapshot's reader runs at about this rate (PRD, Detection). */
        static constexpr int refreshHz = 30;

        explicit EditorRoot (Host host);
        ~EditorRoot() override;

        /** TEMPORARY until the strip of devplan 6d: a component laid out below
            the curve, not owned. Null takes it away. */
        void setStandIn (juce::Component* component);

        /** What the timer does, at once: for a test or a snapshot, which run
            no message loop. */
        void refreshNow();

        CurveView& curveView() noexcept     { return curve; }

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void timerCallback() override;

        Host host;
        CurveView curve;
        juce::Component* standIn = nullptr;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorRoot)
    };
}
