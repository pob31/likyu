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
    THE PROBE'S EDITOR (devplan, Phase 1). Throwaway, and nothing of it is
    the product's editor, which is Phase 6.

    Top: what the host is doing to the plugin, live. Middle: the lookahead
    switch, the state buttons, the report buttons. Bottom left: the log.
    Bottom right: a pad that draws every pointer that reaches it, which is
    the multitouch row of the bench.
*/

#include "ProbeProcessor.h"

#include <map>

namespace likyu::probe
{
    /** Draws every pointer down on it, one disc each, with what JUCE says of it. */
    class TouchPad final : public juce::Component
    {
    public:
        explicit TouchPad (ProbeProcessor&);

        void paint (juce::Graphics&) override;

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

    private:
        struct Pointer
        {
            juce::Point<float> position;
            juce::MouseInputSource::InputSourceType type;
            float pressure;
        };

        void track (const juce::MouseEvent&);

        ProbeProcessor& processor;
        std::map<int, Pointer> pointers;    // by MouseInputSource index
        juce::String lastGesture { "no wheel or pinch yet" };
        int wheelEvents = 0, magnifyEvents = 0;
    };

    //==========================================================================
    class ProbeEditor final : public juce::AudioProcessorEditor,
                              private juce::Timer
    {
    public:
        explicit ProbeEditor (ProbeProcessor&);
        ~ProbeEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void timerCallback() override;
        void saveReport();

        ProbeProcessor& processor;

        juce::StringArray status;
        juce::Rectangle<int> statusArea;

        std::array<juce::TextButton, dsp::numLookaheadChoices> lookaheadButtons;
        juce::TextButton scrambleButton { "Scramble" };
        juce::TextButton defaultsButton { "Defaults" };
        juce::TextButton copyButton { "Copy report" };
        juce::TextButton saveButton { "Save report" };
        juce::TextButton clearButton { "Clear log" };
        juce::Label message;

        juce::TextEditor logView;
        TouchPad touchPad;
        int shownLogRevision = -1;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProbeEditor)
    };
}
