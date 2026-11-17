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

/*
    The probe's editor in a window of its own, with no host around it. The
    control for the touch row: three hosts deliver the first finger only
    (docs/spikes/phase1-hosts.md). If several fingers draw several discs
    here, the limit is in how a host holds a plugin's window; if not, it is
    in JUCE at this pin or in the system, and no host is to blame.

    No audio runs: only the pointer pad means anything here.
*/

#include "ProbeEditor.h"

namespace
{
    class TouchWindow final : public juce::DocumentWindow
    {
    public:
        TouchWindow()
            : juce::DocumentWindow ("Likyu Probe, no host", juce::Colours::black, juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setUsingWindowsMultiTouch (true);
            setContentOwned (processor.createEditor(), true);
            setResizable (true, false);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplicationBase::quit();
        }

    private:
        likyu::probe::ProbeProcessor processor { 2 };
    };

    class TouchApp final : public juce::JUCEApplication
    {
    public:
        const juce::String getApplicationName() override        { return "Likyu Probe, no host"; }
        const juce::String getApplicationVersion() override     { return LIKYU_VERSION_STRING; }

        void initialise (const juce::String&) override          { window = std::make_unique<TouchWindow>(); }
        void shutdown() override                                { window.reset(); }

    private:
        std::unique_ptr<TouchWindow> window;
    };
}

START_JUCE_APPLICATION (TouchApp)
