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

#include <algorithm>
#include <cmath>
#include <utility>

namespace likyu::editor
{
    namespace
    {
        /** The curve's share of the height while something sits below it:
            its height in a window of the default size. */
        constexpr double curveShare = geometry::defaultCurveHeight / EditorRoot::defaultHeight;
    }

    EditorRoot::EditorRoot (Host hostIn)
        : host (std::move (hostIn)),
          curve (host)
    {
        setOpaque (true);
        addAndMakeVisible (curve);

        curve.refresh();
        startTimerHz (refreshHz);
    }

    EditorRoot::~EditorRoot()
    {
        stopTimer();
    }

    void EditorRoot::setStandIn (juce::Component* component)
    {
        if (standIn != nullptr)
            removeChildComponent (standIn);

        standIn = component;

        if (standIn != nullptr)
            addAndMakeVisible (*standIn);

        resized();
    }

    void EditorRoot::refreshNow()
    {
        curve.refresh();
    }

    void EditorRoot::timerCallback()
    {
        curve.refresh();
    }

    void EditorRoot::paint (juce::Graphics& g)
    {
        g.fillAll (palette::background);
    }

    void EditorRoot::resized()
    {
        auto area = getLocalBounds();

        if (standIn == nullptr)
        {
            curve.setBounds (area);
            return;
        }

        curve.setBounds (area.removeFromTop (static_cast<int> (std::lround (area.getHeight() * curveShare))));
        standIn->setBounds (area);
    }
}
