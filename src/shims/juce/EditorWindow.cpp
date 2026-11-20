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
            /*  A parameter of the table, or past it one of the temporary
                transient constants, which are host parameters of their own
                (TEMPORARY, EditorHost.h). */
            const auto parameterFor = [&processor] (int index) -> juce::RangedAudioParameter&
            {
                if (index >= dsp::numParams)
                    return processor.temporaryTransientTime (index - dsp::numParams);

                return processor.parameterAt (index);
            };

            host.valueOf = [parameterFor] (int index)
            {
                auto& parameter = parameterFor (index);
                return static_cast<double> (parameter.convertFrom0to1 (parameter.getValue()));
            };

            host.setValue = [parameterFor] (int index, double value)
            {
                auto& parameter = parameterFor (index);
                parameter.setValueNotifyingHost (parameter.convertTo0to1 (static_cast<float> (value)));
            };

            host.beginGesture = [parameterFor] (int index) { parameterFor (index).beginChangeGesture(); };
            host.endGesture = [parameterFor] (int index) { parameterFor (index).endChangeGesture(); };
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

            host.setAnalyser = [&processor] (bool on) { processor.analyser().setEnabled (on); };
            host.readAnalyser = [&processor] (int stream, float* out, int count)
            {
                return processor.analyser().read (static_cast<core::AnalyserTap::Stream> (stream), out, count);
            };
            host.sidechainPresent = [&processor] { return processor.analyser().isPresent (core::AnalyserTap::sidechain); };
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
