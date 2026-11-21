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

#include "LongPressButton.h"

#include "Palette.h"

#include <algorithm>
#include <utility>

namespace likyu::editor
{
    namespace
    {
        const juce::Colour readyColour { 0xff2ecc71 };
    }

    LongPressButton::LongPressButton (juce::String textIn, int holdMsIn)
        : text (std::move (textIn)), holdMs (std::max (1, holdMsIn))
    {
    }

    LongPressButton::~LongPressButton()
    {
        stopTimer();
    }

    void LongPressButton::setLit (bool shouldBeLit)
    {
        if (lit != shouldBeLit)
        {
            lit = shouldBeLit;
            repaint();
        }
    }

    void LongPressButton::setAccent (juce::Colour colour)
    {
        if (accent != colour)
        {
            accent = colour;
            repaint();
        }
    }

    //==========================================================================
    void LongPressButton::press()
    {
        if (! isEnabled() || pressed)
            return;

        // Lit and latched: a short press is enough to let go.
        if (lit && onPressWhileLit != nullptr)
        {
            onPressWhileLit();
            return;
        }

        pressed = true;
        ready = false;
        held = 0;
        repaint();
    }

    void LongPressButton::heldFor (int milliseconds)
    {
        if (! pressed)
            return;

        held = std::max (held, milliseconds);

        if (! ready && held >= holdMs)
        {
            ready = true;

            if (onHeld != nullptr)
                onHeld();
        }

        repaint();
    }

    void LongPressButton::release (bool onTheButton)
    {
        if (! pressed)
            return;

        const auto wasReady = ready;
        pressed = false;
        ready = false;
        held = 0;
        repaint();

        if (! wasReady)
            return;

        // The hold ends wherever the pointer is; the action wants the pointer still on the button.
        if (onLetGo != nullptr)
            onLetGo();

        if (onTheButton && onLongPress != nullptr)
            onLongPress();
    }

    void LongPressButton::cancel()
    {
        stopTimer();
        release (false);
    }

    void LongPressButton::enablementChanged()
    {
        if (! isEnabled())
            cancel();

        repaint();
    }

    //==========================================================================
    void LongPressButton::mouseDown (const juce::MouseEvent& e)
    {
        if (! e.mods.isLeftButtonDown() && ! e.source.isTouch())
            return;

        press();

        if (pressed)
        {
            pressedAt = juce::Time::getMillisecondCounter();
            startTimer (30);
        }
    }

    void LongPressButton::mouseUp (const juce::MouseEvent& e)
    {
        stopTimer();
        release (contains (e.getPosition()));
    }

    void LongPressButton::timerCallback()
    {
        heldFor (static_cast<int> (juce::Time::getMillisecondCounter() - pressedAt));
    }

    void LongPressButton::paint (juce::Graphics& g)
    {
        const auto area = getLocalBounds().toFloat().reduced (0.5f);
        const auto alpha = isEnabled() ? 1.0f : 0.4f;

        g.setColour ((lit ? accent : palette::controlBackground).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (area, 3.0f);

        // The bar: how much of the hold is done, green once it is.
        if (pressed)
        {
            const auto done = std::clamp (static_cast<float> (held) / static_cast<float> (holdMs), 0.0f, 1.0f);
            const juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (area.withWidth (area.getWidth() * done).getSmallestIntegerContainer());
            g.setColour (ready ? readyColour : accent.withMultipliedAlpha (0.8f));
            g.fillRoundedRectangle (area, 3.0f);
        }

        g.setColour (palette::controlBorder.withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (area, 3.0f, 1.0f);

        g.setColour ((lit || ready ? juce::Colours::black : palette::textPrimary).withMultipliedAlpha (alpha));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText (text, getLocalBounds(), juce::Justification::centred, false);
    }
}
