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
    THE CONTROLS OF THE COLUMNS AND THE FOOT: one parameter of the table
    each, through the editor's Host. They look and handle as the controls of
    WFS-DIY's output EQ do, written here on Likyu's own parameters:

      ParamDial    an arc of 315 degrees with a dot, turned by dragging round
                   its centre (WfsBasicDial); its name above, its value below
      ParamBar     a bar; a press jumps to the pointer (WfsStandardSlider);
                   its value below. As a marker (a band's Frequency) the
                   whole bar is tinted in the band's colour and a block of
                   that colour marks the value, so that a band at 30 Hz
                   shows as much colour as one at 14 kHz; otherwise it is
                   filled to the value, from 0 where 0 is inside the range
      ParamChoice  a combo box of the table's choices
      ParamToggle  a square in the band's colour when on (EQBandToggle)

    A value is shown with the table's own text and unit (core/ValueText.h)
    and can be typed, with or without its unit. Shift is a fine drag, a
    double click the table's default, the wheel a hundredth of the range.
    Every change is inside a gesture.

    refresh() is the editor's timer: each control reads its parameter and
    repaints only if it moved.
*/

#include "EditorHost.h"

#include <dsp/Parameters.h>

#include <juce_gui_basics/juce_gui_basics.h>

namespace likyu::editor
{
    class ParamControl : public juce::Component
    {
    public:
        ParamControl (Host& host, int index, juce::String label, juce::Colour accent);

        virtual void refresh() = 0;

        int parameterIndex() const noexcept     { return index; }

        /** Drawn faint, still usable: a control of something not in use now. */
        void setDimmed (bool shouldBeDimmed);

    protected:
        const dsp::ParamSpec& spec() const;
        double value() const;
        double normalised() const;

        /** One change, in a gesture of its own. */
        void setOnce (double value);

        Host& host;
        const int index;
        const juce::String label;
        const juce::Colour accent;
        bool dimmed = false;
    };

    //==========================================================================
    /** What the dial and the bar share: a value text that can be typed, and a
        drag inside one gesture. */
    class ContinuousControl : public ParamControl
    {
    public:
        ContinuousControl (Host& host, int index, juce::String label, juce::Colour accent);
        ~ContinuousControl() override;

        void refresh() override;

        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    protected:
        void beginDrag();
        void dragTo (double normalisedValue);
        void endDrag();

        juce::Label text;
        double shown = 0.0;         // the normalised value last drawn
        bool dragging = false;

    private:
        bool read = false;
    };

    class ParamDial final : public ContinuousControl
    {
    public:
        using ContinuousControl::ContinuousControl;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;

    private:
        juce::Rectangle<float> dialArea() const;

        double fromNormalised = 0.0;
        float lastAngle = 0.0f;
        double turned = 0.0;
    };

    class ParamBar final : public ContinuousControl
    {
    public:
        using ContinuousControl::ContinuousControl;

        /** The value to the right of the bar, on its line, instead of under
            it: for a row of controls. */
        void setTextBeside (bool beside);

        /** A tinted bar with a block at the value, in place of a fill. */
        void setMarker (bool marker);

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;

    private:
        juce::Rectangle<float> barArea() const;

        bool textBeside = false;
        bool marker = false;

        double fromNormalised = 0.0;
        float pressX = 0.0f;
        bool fine = false;
    };

    //==========================================================================
    class ParamChoice final : public ParamControl
    {
    public:
        /** `showLabel`: the control's name above the box. */
        ParamChoice (Host& host, int index, juce::String label, juce::Colour accent, bool showLabel);

        void refresh() override;
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        juce::ComboBox box;
        const bool showLabel;
    };

    class ParamToggle final : public ParamControl
    {
    public:
        /** `showLabel`: the control's name beside the square. */
        ParamToggle (Host& host, int index, juce::String label, juce::Colour accent, bool showLabel);

        void refresh() override;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        juce::Rectangle<float> square() const;

        const bool showLabel;
        bool on = false;
        bool read = false;
    };
}
