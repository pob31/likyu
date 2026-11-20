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

#include "EditorWindow.h"

namespace likyu
{
    namespace
    {
        /** The editor's host, from the processor. Every function runs on the
            message thread; the processor outlives its editor. */
        editor::Host hostFor (LikyuProcessor& processor)
        {
            editor::Host host;
            host.valueOf = [&processor] (int index) { return processor.valueOf (index); };
            host.setValue = [&processor] (int index, double value) { processor.setValue (index, value); };
            host.beginGesture = [&processor] (int index) { processor.beginGesture (index); };
            host.endGesture = [&processor] (int index) { processor.endGesture (index); };
            host.readMeters = [&processor] (core::Meters& meters) { return processor.meters().read (meters); };
            host.sampleRate = [&processor] { return processor.getSampleRate(); };

            host.channelNames = [&processor]
            {
                std::vector<std::string> names;

                for (const auto& name : processor.channelNames())
                    names.push_back (name.toStdString());

                return names;
            };

            host.bandLeftOut = [&processor] (int band) { return processor.bandChannelsLeftOut (band); };
            host.setBandLeftOut = [&processor] (int band, std::uint64_t leftOut) { processor.setBandChannelsLeftOut (band, leftOut); };
            host.setListen = [&processor] (int band) { processor.setListen (band); };
            return host;
        }
    }

    EditorWindow::EditorWindow (LikyuProcessor& processor)
        : juce::AudioProcessorEditor (processor),
          root (std::make_unique<editor::EditorRoot> (hostFor (processor)))
    {
        addAndMakeVisible (*root);

        setResizable (true, true);
        setResizeLimits (editor::EditorRoot::minimumWidth, editor::EditorRoot::minimumHeight,
                         editor::EditorRoot::maximumWidth, editor::EditorRoot::maximumHeight);
        setSize (editor::EditorRoot::defaultWidth, editor::EditorRoot::defaultHeight);
    }

    void EditorWindow::resized()
    {
        root->setBounds (getLocalBounds());
    }
}
