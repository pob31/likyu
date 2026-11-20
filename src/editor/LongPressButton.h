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
    A BUTTON THAT WANTS A SUSTAINED PRESS, as WFS-DIY's LongPressButton
    (Source/gui/buttons/LongPressButton.h), written here: a short tap does
    nothing, so nothing that cannot be taken back, or that reaches a live
    output, happens by accident. A bar fills from the left while the press
    is held and turns green when it has been held long enough.

    Two uses:
      - an ACTION, done on release once the press was long enough and the
        pointer is still on the button (Reset, Flatten): onLongPress;
      - a HOLD, on from the moment the press is long enough until the
        release (Listen, the guard decided by the author 2026-11-20):
        onHeld and onLetGo.

    The time is the timer's in use; press(), heldFor() and release() are the
    same steps for a test, which runs no message loop.
*/

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace likyu::editor
{
    class LongPressButton final : public juce::Component,
                                  private juce::Timer
    {
    public:
        /** How long a press must be held, unless told otherwise. */
        static constexpr int defaultHoldMs = 700;

        explicit LongPressButton (juce::String text, int holdMs = defaultHoldMs);
        ~LongPressButton() override;

        /** On release, when the press was long enough and ended on the button. */
        std::function<void()> onLongPress;

        /** The moment the press has been long enough, and the release that follows it. */
        std::function<void()> onHeld;
        std::function<void()> onLetGo;

        /** Shown as on, whatever the pointer does: a Listen that is heard. */
        void setLit (bool shouldBeLit);

        /** The colour of the bar and of the lit state. */
        void setAccent (juce::Colour colour);

        /** Lets go of a press in progress, as a release off the button would. */
        void cancel();

        //======================================================================
        /** The steps of a press, for the mouse and for a test. */
        void press();
        void heldFor (int milliseconds);
        void release (bool onTheButton);

        bool isHeldLongEnough() const noexcept      { return ready; }

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void enablementChanged() override;

    private:
        void timerCallback() override;

        const juce::String text;
        const int holdMs;
        juce::Colour accent { 0xff3498db };

        bool pressed = false, ready = false, lit = false;
        int held = 0;
        juce::uint32 pressedAt = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LongPressButton)
    };
}
