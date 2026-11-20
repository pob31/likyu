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

#include "ParamControls.h"

#include "Palette.h"

#include <core/ValueText.h>
#include <dsp/Math.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace likyu::editor
{
    namespace
    {
        constexpr float labelHeight = 13.0f;
        constexpr float textHeight = 15.0f;
        constexpr float labelFont = 11.0f;

        /** The dial's arc: 315 degrees, open at the bottom, as WFS-DIY's. */
        constexpr float arcRange = 315.0f * juce::MathConstants<float>::pi / 180.0f;
        constexpr float arcStart = -0.5f * arcRange;

        /** How far a wheel's step and a fine drag go. */
        constexpr double wheelStep = 0.1;
        constexpr double fineFactor = 0.1;

        juce::Font fontOf (float height)
        {
            return juce::Font (juce::FontOptions (height));
        }

        /** Its value 0 is inside its range and not at an end: the arc grows from there. */
        bool isBipolar (const dsp::ParamSpec& spec) noexcept
        {
            return spec.skew == dsp::ParamSkew::linear && spec.minimum < 0.0 && spec.maximum > 0.0;
        }
    }

    //==========================================================================
    ParamControl::ParamControl (Host& hostIn, int indexIn, juce::String labelIn, juce::Colour accentIn)
        : host (hostIn), index (indexIn), label (std::move (labelIn)), accent (accentIn)
    {
    }

    const dsp::ParamSpec& ParamControl::spec() const
    {
        return dsp::parameterTable()[static_cast<std::size_t> (index)];
    }

    double ParamControl::value() const
    {
        return host.valueOf (index);
    }

    double ParamControl::normalised() const
    {
        return dsp::toNormalised (spec(), value());
    }

    void ParamControl::setOnce (double newValue)
    {
        host.beginGesture (index);
        host.setValue (index, newValue);
        host.endGesture (index);
        refresh();
    }

    void ParamControl::setDimmed (bool shouldBeDimmed)
    {
        if (dimmed != shouldBeDimmed)
        {
            dimmed = shouldBeDimmed;
            setAlpha (dimmed ? 0.4f : 1.0f);
        }
    }

    //==========================================================================
    ContinuousControl::ContinuousControl (Host& hostIn, int indexIn, juce::String labelIn, juce::Colour accentIn)
        : ParamControl (hostIn, indexIn, std::move (labelIn), accentIn)
    {
        text.setFont (fontOf (labelFont + 1.0f));
        text.setJustificationType (juce::Justification::centred);
        text.setColour (juce::Label::textColourId, palette::textPrimary);
        text.setColour (juce::Label::backgroundWhenEditingColourId, palette::plot);
        text.setColour (juce::Label::textWhenEditingColourId, palette::textPrimary);
        text.setColour (juce::Label::outlineWhenEditingColourId, accent);
        text.setMinimumHorizontalScale (0.7f);
        text.setEditable (true, false, false);

        // Typed, with or without its unit; clamped to the range by the reader.
        text.onTextChange = [this]
        {
            setOnce (core::valueFromText (spec(), text.getText()));
            text.setText (core::valueText (spec(), value()), juce::dontSendNotification);
        };

        addAndMakeVisible (text);
    }

    ContinuousControl::~ContinuousControl()
    {
        endDrag();
    }

    void ContinuousControl::refresh()
    {
        const auto now = normalised();

        if (read && dsp::exactlyEqual (now, shown))
            return;

        shown = now;
        read = true;

        if (! text.isBeingEdited())
            text.setText (core::valueText (spec(), value()), juce::dontSendNotification);

        repaint();
    }

    void ContinuousControl::beginDrag()
    {
        if (! dragging)
        {
            dragging = true;
            host.beginGesture (index);
        }
    }

    void ContinuousControl::dragTo (double normalisedValue)
    {
        host.setValue (index, dsp::fromNormalised (spec(), std::clamp (normalisedValue, 0.0, 1.0)));
        refresh();
    }

    void ContinuousControl::endDrag()
    {
        if (dragging)
        {
            dragging = false;
            host.endGesture (index);
        }
    }

    void ContinuousControl::mouseUp (const juce::MouseEvent&)
    {
        endDrag();
    }

    void ContinuousControl::mouseDoubleClick (const juce::MouseEvent&)
    {
        endDrag();
        setOnce (spec().defaultValue);
    }

    void ContinuousControl::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        if (! (std::abs (wheel.deltaY) > 0.0f) || dragging)
            return;

        const auto step = static_cast<double> (wheel.deltaY) * wheelStep * (e.mods.isShiftDown() ? fineFactor : 1.0);
        setOnce (dsp::fromNormalised (spec(), std::clamp (normalised() + step, 0.0, 1.0)));
    }

    //==========================================================================
    juce::Rectangle<float> ParamDial::dialArea() const
    {
        return getLocalBounds().toFloat().withTrimmedTop (labelHeight).withTrimmedBottom (textHeight).reduced (1.0f);
    }

    void ParamDial::resized()
    {
        text.setBounds (getLocalBounds().removeFromBottom (static_cast<int> (textHeight)));
    }

    void ParamDial::paint (juce::Graphics& g)
    {
        g.setColour (palette::textSecondary);
        g.setFont (fontOf (labelFont));
        g.drawText (label, getLocalBounds().toFloat().removeFromTop (labelHeight), juce::Justification::centred, false);

        const auto area = dialArea();
        const auto radius = 0.5f * std::min (area.getWidth(), area.getHeight());

        if (radius < 4.0f)
            return;

        const auto centre = area.getCentre();
        const auto track = 0.8f * radius;
        const auto width = std::max (2.5f, 0.15f * radius);
        const auto stroke = juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        const auto angleOf = [] (double n) { return arcStart + arcRange * static_cast<float> (n); };

        juce::Path whole;
        whole.addCentredArc (centre.x, centre.y, track, track, 0.0f, arcStart, arcStart + arcRange, true);
        g.setColour (palette::controlTrack);
        g.strokePath (whole, stroke);

        // From the range's start, or from 0 where 0 is inside the range.
        const auto from = isBipolar (spec()) ? dsp::toNormalised (spec(), 0.0) : 0.0;
        const auto now = angleOf (shown);

        if (std::abs (shown - from) > 1.0e-4)
        {
            juce::Path active;
            active.addCentredArc (centre.x, centre.y, track, track, 0.0f,
                                  std::min (angleOf (from), now), std::max (angleOf (from), now), true);
            g.setColour (accent);
            g.strokePath (active, stroke);
        }

        const auto dot = 0.8f * width;
        g.setColour (palette::textPrimary);
        g.fillEllipse (centre.x + track * std::sin (now) - dot, centre.y - track * std::cos (now) - dot, 2.0f * dot, 2.0f * dot);
    }

    void ParamDial::mouseDown (const juce::MouseEvent& e)
    {
        const auto fromCentre = e.position - dialArea().getCentre();

        beginDrag();
        fromNormalised = shown;
        lastAngle = std::atan2 (fromCentre.y, fromCentre.x);
        turned = 0.0;
    }

    void ParamDial::mouseDrag (const juce::MouseEvent& e)
    {
        if (! dragging)
            return;

        // Round the centre, as WFS-DIY's dial: the angle travelled is the value's travel.
        const auto fromCentre = e.position - dialArea().getCentre();
        const auto angle = std::atan2 (fromCentre.y, fromCentre.x);
        auto delta = angle - lastAngle;

        if (delta > juce::MathConstants<float>::pi)
            delta -= juce::MathConstants<float>::twoPi;
        else if (delta < -juce::MathConstants<float>::pi)
            delta += juce::MathConstants<float>::twoPi;

        lastAngle = angle;
        turned += static_cast<double> (delta / arcRange) * (e.mods.isShiftDown() ? fineFactor : 1.0);
        dragTo (fromNormalised + turned);
    }

    //==========================================================================
    namespace
    {
        constexpr float besideTextWidth = 62.0f;
        constexpr float besideBarHeight = 16.0f;
    }

    void ParamBar::setTextBeside (bool beside)
    {
        textBeside = beside;
        resized();
    }

    juce::Rectangle<float> ParamBar::barArea() const
    {
        const auto area = getLocalBounds().toFloat();

        if (textBeside)
            return area.withTrimmedRight (besideTextWidth).withSizeKeepingCentre (area.getWidth() - besideTextWidth, besideBarHeight)
                       .withX (area.getX());

        return area.withTrimmedBottom (textHeight + 4.0f);
    }

    void ParamBar::resized()
    {
        text.setJustificationType (juce::Justification::centredRight);

        if (textBeside)
            text.setBounds (getLocalBounds().removeFromRight (static_cast<int> (besideTextWidth)));
        else
            text.setBounds (getLocalBounds().removeFromBottom (static_cast<int> (textHeight)));
    }

    void ParamBar::paint (juce::Graphics& g)
    {
        const auto area = barArea();

        g.setColour (palette::controlTrack);
        g.fillRect (area);

        const auto filled = area.withWidth (area.getWidth() * static_cast<float> (shown));
        g.setColour (accent);
        g.fillRect (filled);

        g.setColour (palette::textPrimary);
        g.fillRect (std::min (filled.getRight(), area.getRight() - 2.0f), area.getY(), 2.0f, area.getHeight());
    }

    void ParamBar::mouseDown (const juce::MouseEvent& e)
    {
        const auto area = barArea();

        if (! (area.getWidth() > 0.0f))
            return;

        beginDrag();
        fine = e.mods.isShiftDown();
        pressX = e.position.x;

        // A press jumps to the pointer, as WFS-DIY's slider; with Shift it moves from where it is.
        if (! fine)
            dragTo (static_cast<double> ((e.position.x - area.getX()) / area.getWidth()));

        fromNormalised = shown;
    }

    void ParamBar::mouseDrag (const juce::MouseEvent& e)
    {
        const auto area = barArea();

        if (! dragging || ! (area.getWidth() > 0.0f))
            return;

        if (e.mods.isShiftDown() != fine)
        {
            fine = e.mods.isShiftDown();
            pressX = e.position.x;
            fromNormalised = shown;
        }

        const auto moved = static_cast<double> ((e.position.x - pressX) / area.getWidth());
        dragTo (fromNormalised + moved * (fine ? fineFactor : 1.0));
    }

    //==========================================================================
    ParamChoice::ParamChoice (Host& hostIn, int indexIn, juce::String labelIn, juce::Colour accentIn, bool showLabelIn)
        : ParamControl (hostIn, indexIn, std::move (labelIn), accentIn),
          showLabel (showLabelIn)
    {
        auto id = 1;

        for (const auto& choice : spec().choices)
            box.addItem (juce::String (choice), id++);

        box.setColour (juce::ComboBox::backgroundColourId, palette::controlBackground);
        box.setColour (juce::ComboBox::outlineColourId, palette::controlBorder);
        box.setColour (juce::ComboBox::textColourId, palette::textPrimary);
        box.setColour (juce::ComboBox::arrowColourId, palette::textSecondary);

        box.onChange = [this] { setOnce (static_cast<double> (box.getSelectedItemIndex())); };
        addAndMakeVisible (box);
    }

    void ParamChoice::refresh()
    {
        // A menu the user has open is theirs until they have chosen (Go.dot's MenuHold).
        if (box.isPopupActive())
            return;

        const auto now = static_cast<int> (std::lround (value()));

        if (box.getSelectedItemIndex() != now)
            box.setSelectedItemIndex (now, juce::dontSendNotification);
    }

    void ParamChoice::paint (juce::Graphics& g)
    {
        if (! showLabel)
            return;

        g.setColour (palette::textSecondary);
        g.setFont (fontOf (labelFont));
        g.drawText (label, getLocalBounds().toFloat().removeFromTop (labelHeight), juce::Justification::centred, false);
    }

    void ParamChoice::resized()
    {
        auto area = getLocalBounds();

        if (showLabel)
            area.removeFromTop (static_cast<int> (labelHeight) + 3);

        box.setBounds (area.withHeight (std::min (area.getHeight(), 24)));
    }

    //==========================================================================
    ParamToggle::ParamToggle (Host& hostIn, int indexIn, juce::String labelIn, juce::Colour accentIn, bool showLabelIn)
        : ParamControl (hostIn, indexIn, std::move (labelIn), accentIn),
          showLabel (showLabelIn)
    {
    }

    juce::Rectangle<float> ParamToggle::square() const
    {
        const auto area = getLocalBounds().toFloat();
        const auto side = std::min (18.0f, std::min (area.getWidth(), area.getHeight()));

        if (! showLabel)
            return area.withSizeKeepingCentre (side, side);

        // Under its name, like a dial under its own.
        return juce::Rectangle<float> (side, side).withCentre ({ area.getCentreX(), labelHeight + 3.0f + 12.0f });
    }

    void ParamToggle::refresh()
    {
        const auto now = value() >= 0.5;

        if (read && now == on)
            return;

        on = now;
        read = true;
        repaint();
    }

    void ParamToggle::paint (juce::Graphics& g)
    {
        const auto box = square().reduced (1.0f);

        g.setColour (on ? accent : palette::controlTrack);
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (palette::controlBorder);
        g.drawRoundedRectangle (box, 3.0f, 1.0f);

        if (showLabel)
        {
            g.setColour (palette::textSecondary);
            g.setFont (fontOf (labelFont));
            g.drawText (label, getLocalBounds().toFloat().removeFromTop (labelHeight), juce::Justification::centred, false);
        }
    }

    void ParamToggle::mouseDown (const juce::MouseEvent&)
    {
        setOnce (on ? 0.0 : 1.0);
    }
}
