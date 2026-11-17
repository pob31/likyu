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
    The probe without a host: it is given a stereo main bus and a stereo
    sidechain with signal on its first channel only, runs a few blocks,
    then writes the report as text and the editor as a picture. A way to
    look at the probe before a host does.

        likyu_hostprobe_snapshot <folder>
*/

#include "ProbeEditor.h"

#include <cmath>
#include <iostream>

int main (int argc, char* argv[])
{
    if (argc < 2 || ! juce::File::isAbsolutePath (juce::String (argv[1])))
    {
        std::cerr << "usage: likyu_hostprobe_snapshot <absolute folder>\n";
        return 2;
    }

    const juce::File folder { juce::String (argv[1]) };
    const juce::ScopedJuceInitialiser_GUI gui;

    likyu::probe::ProbeProcessor processor (2);

    auto layout = processor.getBusesLayout();
    layout.inputBuses.set (1, juce::AudioChannelSet::stereo());

    if (! processor.setBusesLayout (layout))
    {
        std::cerr << "the sidechain could not be enabled\n";
        return 1;
    }

    constexpr auto rate = 48000.0;
    constexpr auto blockSize = 256;

    processor.setRateAndBufferSizeDetails (rate, blockSize);
    processor.prepareToPlay (rate, blockSize);
    processor.setLookaheadChoice (2);

    juce::AudioBuffer<float> buffer (4, blockSize);
    juce::MidiBuffer midi;
    auto phase = 0.0;

    for (int block = 0; block < 100; ++block)
    {
        buffer.clear();

        for (int i = 0; i < blockSize; ++i)
        {
            const auto sample = static_cast<float> (0.25 * std::sin (phase));
            phase += juce::MathConstants<double>::twoPi * 440.0 / rate;
            buffer.setSample (0, i, sample);
            buffer.setSample (1, i, sample);
            buffer.setSample (2, i, 0.1f * sample);    // the sidechain's first channel; the second stays at zero
        }

        processor.processBlock (buffer, midi);
    }

    processor.refreshNow();

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* probeEditor = dynamic_cast<likyu::probe::ProbeEditor*> (editor.get());

    if (probeEditor == nullptr)
        return 1;

    probeEditor->refreshNow();

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
    const auto picture = folder.getChildFile ("hostprobe.png");
    picture.deleteFile();

    if (juce::FileOutputStream stream (picture); stream.openedOk())
        juce::PNGImageFormat().writeImageToStream (image, stream);

    folder.getChildFile ("hostprobe.md").replaceWithText (processor.report());

    editor.reset();
    processor.releaseResources();

    std::cout << "wrote " << picture.getFullPathName() << "\n";
    return 0;
}
