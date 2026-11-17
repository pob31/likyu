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

#include "ProbeEditor.h"

namespace likyu::probe
{
    namespace
    {
        constexpr int lineHeight = 18;
        constexpr int margin = 8;
        constexpr int buttonHeight = 26;

        juce::Font monospaced (float height)
        {
            return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
        }

        const char* typeName (juce::MouseInputSource::InputSourceType type)
        {
            switch (type)
            {
                case juce::MouseInputSource::InputSourceType::mouse:    return "mouse";
                case juce::MouseInputSource::InputSourceType::touch:    return "touch";
                case juce::MouseInputSource::InputSourceType::pen:      return "pen";
            }

            return "?";
        }
    }

    //==========================================================================
    TouchPad::TouchPad (ProbeProcessor& owner) : processor (owner) {}

    void TouchPad::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colour (0xff20242a));
        g.setColour (juce::Colours::grey);
        g.drawRect (getLocalBounds());

        g.setFont (monospaced (13.0f));
        g.setColour (juce::Colours::lightgrey);
        g.drawText ("Pointer pad: several fingers, wheel, pinch",
                    getLocalBounds().reduced (6).removeFromTop (lineHeight), juce::Justification::centredLeft);
        g.drawText (lastGesture, getLocalBounds().reduced (6).removeFromBottom (lineHeight), juce::Justification::centredLeft);

        for (const auto& [index, pointer] : pointers)
        {
            const auto radius = 22.0f + 30.0f * juce::jlimit (0.0f, 1.0f, pointer.pressure);
            g.setColour (juce::Colour::fromHSV (static_cast<float> (index % 8) / 8.0f, 0.7f, 0.95f, 0.8f));
            g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (pointer.position));
            g.setColour (juce::Colours::white);
            g.drawText ("#" + juce::String (index) + " " + typeName (pointer.type),
                        juce::Rectangle<float> (120.0f, static_cast<float> (lineHeight))
                            .withCentre (pointer.position.translated (0.0f, -radius - 10.0f)),
                        juce::Justification::centred);
        }
    }

    void TouchPad::track (const juce::MouseEvent& event)
    {
        pointers[event.source.getIndex()] = { event.position, event.source.getType(), event.pressure };
        processor.noteTouches (static_cast<int> (pointers.size()), event.source.getType());
        repaint();
    }

    void TouchPad::mouseDown (const juce::MouseEvent& event)    { track (event); }
    void TouchPad::mouseDrag (const juce::MouseEvent& event)    { track (event); }

    void TouchPad::mouseUp (const juce::MouseEvent& event)
    {
        pointers.erase (event.source.getIndex());
        repaint();
    }

    void TouchPad::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
    {
        lastGesture = "wheel #" + juce::String (++wheelEvents) + " from " + typeName (event.source.getType())
                      + ": dy " + juce::String (wheel.deltaY, 3) + (wheel.isSmooth ? ", smooth" : ", stepped")
                      + (wheel.isInertial ? ", inertial" : "");
        repaint();
    }

    void TouchPad::mouseMagnify (const juce::MouseEvent& event, float scaleFactor)
    {
        lastGesture = "pinch #" + juce::String (++magnifyEvents) + " from " + typeName (event.source.getType())
                      + ": factor " + juce::String (scaleFactor, 3);
        repaint();
    }

    //==========================================================================
    ProbeEditor::ProbeEditor (ProbeProcessor& owner)
        : juce::AudioProcessorEditor (owner), processor (owner), touchPad (owner)
    {
        for (std::size_t i = 0; i < lookaheadButtons.size(); ++i)
        {
            auto& button = lookaheadButtons[i];
            const auto ms = dsp::lookaheadMilliseconds[i];
            button.setButtonText (i == 0 ? juce::String ("Lookahead off") : juce::String (ms) + " ms");
            button.setClickingTogglesState (false);
            button.setColour (juce::TextButton::buttonOnColourId, juce::Colours::darkorange);
            button.onClick = [this, i] { processor.setLookaheadChoice (static_cast<int> (i)); };
            addAndMakeVisible (button);
        }

        scrambleButton.onClick = [this] { processor.scramble(); };
        defaultsButton.onClick = [this] { processor.restoreDefaults(); };
        clearButton.onClick = [this] { processor.clearLog(); };
        saveButton.onClick = [this] { saveReport(); };

        copyButton.onClick = [this]
        {
            juce::SystemClipboard::copyTextToClipboard (processor.report());
            message.setText ("Report copied to the clipboard.", juce::dontSendNotification);
        };

        for (auto* button : { &scrambleButton, &defaultsButton, &copyButton, &saveButton, &clearButton })
            addAndMakeVisible (*button);

        message.setFont (monospaced (13.0f));
        message.setText ("Throwaway probe: not Likyu, not for saving sessions.", juce::dontSendNotification);
        addAndMakeVisible (message);

        logView.setMultiLine (true);
        logView.setReadOnly (true);
        logView.setScrollbarsShown (true);
        logView.setFont (monospaced (12.0f));
        addAndMakeVisible (logView);

        addAndMakeVisible (touchPad);

        setResizable (true, true);
        setResizeLimits (720, 480, 2400, 1600);
        setSize (980, 640);

        startTimerHz (15);
        timerCallback();
    }

    ProbeEditor::~ProbeEditor()
    {
        stopTimer();
    }

    void ProbeEditor::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colour (0xff15181c));
        g.setFont (monospaced (13.0f));

        auto area = statusArea;

        for (const auto& line : status)
        {
            g.setColour (line.startsWith ("  SEEN AS") ? juce::Colours::orange : juce::Colours::white);
            g.drawText (line, area.removeFromTop (lineHeight), juce::Justification::centredLeft, true);
        }
    }

    void ProbeEditor::resized()
    {
        auto area = getLocalBounds().reduced (margin);

        statusArea = area.removeFromTop (lineHeight * 11 + margin);

        auto buttons = area.removeFromTop (buttonHeight);

        for (auto& button : lookaheadButtons)
            button.setBounds (buttons.removeFromLeft (110).reduced (2, 0));

        buttons.removeFromLeft (margin);

        for (auto* button : { &scrambleButton, &defaultsButton, &copyButton, &saveButton, &clearButton })
            button->setBounds (buttons.removeFromLeft (100).reduced (2, 0));

        message.setBounds (area.removeFromTop (lineHeight + 4));
        area.removeFromTop (margin / 2);

        touchPad.setBounds (area.removeFromRight (juce::jmax (260, area.getWidth() * 2 / 5)));
        area.removeFromRight (margin);
        logView.setBounds (area);
    }

    void ProbeEditor::timerCallback()
    {
        status = processor.statusLines();
        repaint (statusArea);

        const auto choice = processor.lookaheadChoice();

        for (std::size_t i = 0; i < lookaheadButtons.size(); ++i)
            lookaheadButtons[i].setToggleState (static_cast<int> (i) == choice, juce::dontSendNotification);

        if (const auto revision = processor.logRevision(); revision != shownLogRevision)
        {
            shownLogRevision = revision;
            logView.setText (processor.logLines().joinIntoString ("\n"), false);
            logView.moveCaretToEnd();
        }
    }

    void ProbeEditor::saveReport()
    {
        const auto folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                .getChildFile ("likyu-hostprobe");
        const auto file = folder.getChildFile (processor.reportFileName());

        if (folder.createDirectory().wasOk() && file.replaceWithText (processor.report()))
            message.setText ("Saved " + file.getFullPathName(), juce::dontSendNotification);
        else
            message.setText ("Could not write " + file.getFullPathName(), juce::dontSendNotification);
    }
}
