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
    The processor without a host: parameters, buses, latency, state (Phase
    0b), and the static EQ as a host drives it (Phase 2; acceptance rows
    constant latency, layouts, realtime and state, their processor halves).
    What a host then does with them is Phase 1's bench.
*/

#include <doctest/doctest.h>

#include "../TestSupport.h"

#include <PluginProcessor.h>

#include <dsp/Parameters.h>
#include <dsp/rt/RtCheck.h>

#include <cmath>
#include <cstring>
#include <vector>

using namespace likyu;

namespace
{
    using Layout = juce::AudioProcessor::BusesLayout;

    Layout layout (const juce::AudioChannelSet& main, const juce::AudioChannelSet& sidechain)
    {
        Layout l;
        l.inputBuses.add (main);
        l.inputBuses.add (sidechain);
        l.outputBuses.add (main);
        return l;
    }

    const auto none = juce::AudioChannelSet::disabled();
    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();

    void setLookahead (LikyuProcessor& processor, int choice)
    {
        processor.setValue (dsp::indexOf (dsp::GlobalParam::lookahead), choice);
    }

    /*  The index of the first non-zero sample of channel 0 after an impulse at
        sample 0, through blocks of 64; -1 if none. */
    int measuredDelay (LikyuProcessor& processor, bool bypassed)
    {
        constexpr int blockSize = 64;
        constexpr int blocks = 16;

        juce::MidiBuffer midi;
        const auto channels = processor.getTotalNumInputChannels();

        for (int block = 0; block < blocks; ++block)
        {
            juce::AudioBuffer<float> buffer (juce::jmax (channels, processor.getTotalNumOutputChannels()), blockSize);
            buffer.clear();

            if (block == 0)
                buffer.setSample (0, 0, 1.0f);

            if (bypassed)
                processor.processBlockBypassed (buffer, midi);
            else
                processor.processBlock (buffer, midi);

            for (int n = 0; n < blockSize; ++n)
                if (std::fpclassify (buffer.getSample (0, n)) != FP_ZERO) // exact silence, not near it
                    return block * blockSize + n;
        }

        return -1;
    }
}

TEST_CASE ("processor: the host sees the table, 284 parameters in real units")
{
    LikyuProcessor processor;
    const auto& table = dsp::parameterTable();

    REQUIRE (processor.getParameters().size() == static_cast<int> (table.size()));

    for (int i = 0; i < static_cast<int> (table.size()); ++i)
    {
        const auto& spec = table[static_cast<std::size_t> (i)];
        auto& parameter = processor.parameterAt (i);

        INFO ("id: " << spec.id);
        CHECK (parameter.getParameterID().toStdString() == spec.id);
        CHECK (parameter.getName (64).toStdString() == spec.name);
        CHECK (parameter.isAutomatable() == spec.automatable);
        CHECK (processor.valueOf (i) == doctest::Approx (spec.defaultValue).epsilon (1.0e-5));

        // The ends of the host's 0 to 1 are the ends of the real range.
        CHECK (static_cast<double> (parameter.convertFrom0to1 (0.0f)) == doctest::Approx (spec.minimum).epsilon (1.0e-5));
        CHECK (static_cast<double> (parameter.convertFrom0to1 (1.0f)) == doctest::Approx (spec.maximum).epsilon (1.0e-5));

        /*  What JUCE's LV2 wrapper asks of an ID (ParameterStorage::getIri):
            unchanged by URL escaping. The Turtle half of its check is covered
            by the symbol test in tests/dsp/ParameterTableTests.cpp. */
        CHECK (juce::URL::addEscapeChars (juce::String (spec.id), true).toStdString() == spec.id);
    }

    CHECK (processor.getBypassParameter() == &processor.parameterAt (dsp::indexOf (dsp::GlobalParam::bypass)));
}

TEST_CASE ("processor: a logarithmic parameter spends as much of the host's range on each octave")
{
    LikyuProcessor processor;
    auto& frequency = processor.parameterAt (dsp::indexOf (0, dsp::BandParam::freq));

    // 20 Hz to 20 kHz is three decades: 200 Hz at a third, 2 kHz at two thirds.
    CHECK (static_cast<double> (frequency.convertFrom0to1 (1.0f / 3.0f)) == doctest::Approx (200.0).epsilon (1.0e-3));
    CHECK (static_cast<double> (frequency.convertFrom0to1 (2.0f / 3.0f)) == doctest::Approx (2000.0).epsilon (1.0e-3));
    CHECK (static_cast<double> (frequency.convertTo0to1 (632.4555f)) == doctest::Approx (0.5).epsilon (1.0e-4));
}

TEST_CASE ("processor: main in equals main out, 1 to 64 channels; sidechain absent, mono or stereo")
{
    LikyuProcessor processor;

    const auto fiveOne = juce::AudioChannelSet::create5point1();
    const auto wide = juce::AudioChannelSet::discreteChannels (64);
    const auto tooWide = juce::AudioChannelSet::discreteChannels (65);

    for (const auto& main : { mono, stereo, fiveOne, juce::AudioChannelSet::create7point1point4(), wide })
    {
        INFO ("main: " << main.getDescription().toStdString());
        CHECK (processor.checkBusesLayoutSupported (layout (main, none)));
        CHECK (processor.checkBusesLayoutSupported (layout (main, mono)));
        CHECK (processor.checkBusesLayoutSupported (layout (main, stereo)));
        CHECK_FALSE (processor.checkBusesLayoutSupported (layout (main, fiveOne)));
    }

    CHECK_FALSE (processor.checkBusesLayoutSupported (layout (tooWide, none)));
    CHECK_FALSE (processor.checkBusesLayoutSupported (layout (none, none)));

    Layout mismatched;
    mismatched.inputBuses.add (mono);
    mismatched.inputBuses.add (none);
    mismatched.outputBuses.add (stereo);
    CHECK_FALSE (processor.checkBusesLayoutSupported (mismatched));
}

TEST_CASE ("processor: the defaults are stereo with the sidechain off, or mono for the mono LV2 variant")
{
    LikyuProcessor stereoDefault (2);
    CHECK (stereoDefault.getBusesLayout().getMainInputChannelSet() == stereo);
    CHECK (stereoDefault.getBusesLayout().getMainOutputChannelSet() == stereo);
    CHECK (stereoDefault.getBusCount (true) == 2);
    CHECK_FALSE (stereoDefault.getBus (true, 1)->isEnabled());

    LikyuProcessor monoDefault (1);
    CHECK (monoDefault.getBusesLayout().getMainInputChannelSet() == mono);
    CHECK (monoDefault.getBusesLayout().getMainOutputChannelSet() == mono);
    CHECK_FALSE (monoDefault.getBus (true, 1)->isEnabled());
}

TEST_CASE ("processor: reported latency equals measured delay, for every lookahead, bypassed or not")
{
    for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        for (int choice = 0; choice < dsp::numLookaheadChoices; ++choice)
        {
            for (const auto bypassed : { false, true })
            {
                LikyuProcessor processor;
                setLookahead (processor, choice);
                processor.setRateAndBufferSizeDetails (rate, 64);
                processor.prepareToPlay (rate, 64);

                const auto expected = dsp::lookaheadSamples (choice, rate);

                INFO ("rate " << rate << ", choice " << choice << ", bypassed " << bypassed);
                CHECK (processor.getLatencySamples() == expected);
                CHECK (measuredDelay (processor, bypassed) == expected);
            }
        }
    }
}

TEST_CASE ("processor: only the lookahead parameter changes the latency, and it does so while prepared")
{
    LikyuProcessor processor;
    processor.setRateAndBufferSizeDetails (48000.0, 64);
    processor.prepareToPlay (48000.0, 64);
    CHECK (processor.getLatencySamples() == 0);

    setLookahead (processor, 2);
    CHECK (processor.getLatencySamples() == 48);

    // Every other parameter, moved end to end: the latency does not notice.
    for (int i = 0; i < dsp::numParams; ++i)
    {
        if (i == dsp::indexOf (dsp::GlobalParam::lookahead))
            continue;

        auto& parameter = processor.parameterAt (i);
        parameter.setValueNotifyingHost (1.0f);
        parameter.setValueNotifyingHost (0.0f);
    }

    CHECK (processor.getLatencySamples() == 48);

    setLookahead (processor, 0);
    CHECK (processor.getLatencySamples() == 0);
}

TEST_CASE ("processor: with lookahead off the output is the input, bit for bit, and the sidechain is left alone")
{
    LikyuProcessor processor;
    REQUIRE (processor.setBusesLayout (layout (stereo, stereo)));
    processor.setRateAndBufferSizeDetails (48000.0, 256);
    processor.prepareToPlay (48000.0, 256);

    REQUIRE (processor.getTotalNumInputChannels() == 4);

    juce::Random random (42);
    juce::AudioBuffer<float> buffer (4, 256);

    for (int c = 0; c < 4; ++c)
        for (int n = 0; n < 256; ++n)
            buffer.setSample (c, n, random.nextFloat() * 2.0f - 1.0f);

    juce::AudioBuffer<float> original;
    original.makeCopyOf (buffer);

    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);

    for (int c = 0; c < 4; ++c)
        CHECK (std::memcmp (buffer.getReadPointer (c), original.getReadPointer (c), 256 * sizeof (float)) == 0);
}

TEST_CASE ("processor: 64 channels in double precision, delayed alike")
{
    LikyuProcessor processor;
    REQUIRE (processor.setBusesLayout (layout (juce::AudioChannelSet::discreteChannels (64), none)));
    setLookahead (processor, 1);
    processor.setProcessingPrecision (juce::AudioProcessor::doublePrecision);
    processor.setRateAndBufferSizeDetails (48000.0, 128);
    processor.prepareToPlay (48000.0, 128);

    juce::AudioBuffer<double> buffer (64, 128);
    buffer.clear();

    for (int c = 0; c < 64; ++c)
        buffer.setSample (c, 0, 1.0 + c);

    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);

    for (int c = 0; c < 64; ++c)
        for (int n = 0; n < 128; ++n)
            REQUIRE (buffer.getSample (c, n) == (n == 12 ? 1.0 + c : 0.0));
}

TEST_CASE ("processor: the process call allocates nothing")
{
    namespace rt = likyu::dsp::rt;
    REQUIRE (rt::isCounting());

    LikyuProcessor processor;
    setLookahead (processor, 3);
    processor.setRateAndBufferSizeDetails (96000.0, 64);
    processor.prepareToPlay (96000.0, 64);

    juce::AudioBuffer<float> buffer (2, 64);
    buffer.clear();
    juce::MidiBuffer midi;

    rt::resetCounts();

    for (int block = 0; block < 500; ++block)
        processor.processBlock (buffer, midi);

    CHECK (rt::regions() == 500);
    CHECK (rt::violations() == 0);
}

TEST_CASE ("processor: state saved by one instance is restored by another")
{
    INFO ("locale in effect: " << std::string (likyutest::appliedLocaleName()));

    LikyuProcessor source;
    source.setValue (dsp::indexOf (2, dsp::BandParam::freq), 3150.0);
    source.setValue (dsp::indexOf (2, dsp::BandParam::gain), -4.5);
    source.setValue (dsp::indexOf (5, dsp::BandParam::gmode), 2.0);
    source.setValue (dsp::indexOf (0, dsp::BandParam::on), 1.0);
    source.setValue (dsp::indexOf (dsp::GlobalParam::lookahead), 3.0);

    juce::MemoryBlock chunk;
    source.getStateInformation (chunk);

    LikyuProcessor restored;
    restored.setRateAndBufferSizeDetails (48000.0, 64);
    restored.prepareToPlay (48000.0, 64);
    restored.setStateInformation (chunk.getData(), static_cast<int> (chunk.getSize()));

    for (int i = 0; i < dsp::numParams; ++i)
    {
        INFO ("id: " << dsp::parameterTable()[static_cast<std::size_t> (i)].id);
        CHECK (restored.valueOf (i) == doctest::Approx (source.valueOf (i)).epsilon (1.0e-5));
    }

    CHECK (restored.valueOf (dsp::indexOf (2, dsp::BandParam::freq)) == doctest::Approx (3150.0).epsilon (1.0e-4));

    // A restored lookahead is a restored latency.
    CHECK (restored.getLatencySamples() == 144);

    // Something that is not a chunk leaves the instance as it was.
    const char garbage[] = "not a chunk";
    restored.setStateInformation (garbage, static_cast<int> (sizeof (garbage) - 1));
    CHECK (restored.valueOf (dsp::indexOf (2, dsp::BandParam::gain)) == doctest::Approx (-4.5));
}

//==============================================================================
// Phase 2: the static EQ as a host drives it.

namespace
{
    void setBand (LikyuProcessor& p, int b, bool on, dsp::BandType type, double hz, double q, double gain,
                  dsp::QMode qmode = dsp::QMode::proportional)
    {
        p.setValue (dsp::indexOf (b, dsp::BandParam::on), on ? 1.0 : 0.0);
        p.setValue (dsp::indexOf (b, dsp::BandParam::type), static_cast<double> (type));
        p.setValue (dsp::indexOf (b, dsp::BandParam::freq), hz);
        p.setValue (dsp::indexOf (b, dsp::BandParam::q), q);
        p.setValue (dsp::indexOf (b, dsp::BandParam::gain), gain);
        p.setValue (dsp::indexOf (b, dsp::BandParam::qmode), static_cast<double> (qmode));
    }

    /** Every band on, each a different type, none at 0 dB. */
    void setEveryType (LikyuProcessor& p)
    {
        for (int b = 0; b < dsp::numBands; ++b)
            setBand (p, b, true, static_cast<dsp::BandType> (b % dsp::numBandTypes), 60.0 * std::pow (2.0, b), 0.5 + b, b % 2 == 0 ? 6.0 : -6.0,
                     b % 2 == 0 ? dsp::QMode::proportional : dsp::QMode::constant);
    }

    juce::AudioBuffer<float> noiseBuffer (int channels, int samples, int seed)
    {
        juce::Random random (seed);
        juce::AudioBuffer<float> buffer (channels, samples);

        for (int c = 0; c < channels; ++c)
            for (int n = 0; n < samples; ++n)
                buffer.setSample (c, n, random.nextFloat() - 0.5f);

        return buffer;
    }

    bool sameChannel (const juce::AudioBuffer<float>& a, int ca, const juce::AudioBuffer<float>& b, int cb)
    {
        return a.getNumSamples() == b.getNumSamples()
            && std::memcmp (a.getReadPointer (ca), b.getReadPointer (cb), static_cast<std::size_t> (a.getNumSamples()) * sizeof (float)) == 0;
    }
}

TEST_CASE ("processor: every default maps to itself through the host's float mapping, and each end to itself")
{
    LikyuProcessor processor;
    const auto& table = dsp::parameterTable();

    for (int i = 0; i < dsp::numParams; ++i)
    {
        const auto& spec = table[static_cast<std::size_t> (i)];
        auto& parameter = processor.parameterAt (i);
        const auto expected = static_cast<float> (spec.defaultValue);

        INFO ("id: " << spec.id);
        CHECK (parameter.convertFrom0to1 (parameter.getDefaultValue()) == expected);
        CHECK (parameter.convertFrom0to1 (parameter.convertTo0to1 (expected)) == expected);

        // What auval does: set each end of the host's 0 to 1, read it back exactly.
        for (const auto end : { 0.0f, 1.0f })
        {
            parameter.setValue (end);
            CHECK (parameter.getValue() == end);
        }

        parameter.setValue (parameter.getDefaultValue());
    }
}

TEST_CASE ("processor: a host reads each value with its unit, and reads typed text back")
{
    LikyuProcessor processor;
    auto& frequency = processor.parameterAt (dsp::indexOf (0, dsp::BandParam::freq));
    auto& gain = processor.parameterAt (dsp::indexOf (0, dsp::BandParam::gain));

    CHECK (frequency.getText (frequency.convertTo0to1 (1200.0f), 64).toStdString() == "1.20 kHz");
    CHECK (gain.getText (gain.convertTo0to1 (-4.5f), 64).toStdString() == "-4.5 dB");
    CHECK (frequency.convertFrom0to1 (frequency.getValueForText ("2k")) == doctest::Approx (2000.0f));
    CHECK (gain.convertFrom0to1 (gain.getValueForText ("+3 dB")) == doctest::Approx (3.0f));

    // Choices and toggles keep the table's names.
    auto& type = processor.parameterAt (dsp::indexOf (0, dsp::BandParam::type));
    CHECK (type.getText (type.convertTo0to1 (3.0f), 64).toStdString() == "High-pass");
}

TEST_CASE ("processor: bells and shelves at 0 dB pass the input bit for bit; a gain away from 0 does not")
{
    LikyuProcessor processor;

    for (int b = 0; b < dsp::numBands; ++b)
        setBand (processor, b, true, static_cast<dsp::BandType> (b % 3), 100.0 * (b + 1), 1.0 + b, 0.0);

    processor.setRateAndBufferSizeDetails (48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    auto buffer = noiseBuffer (2, 512, 5);
    const auto input = buffer;
    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);

    CHECK (sameChannel (buffer, 0, input, 0));
    CHECK (sameChannel (buffer, 1, input, 1));

    processor.setValue (dsp::indexOf (3, dsp::BandParam::gain), 6.0);
    auto next = noiseBuffer (2, 512, 6);
    const auto nextInput = next;
    processor.processBlock (next, midi);
    CHECK_FALSE (sameChannel (next, 0, nextInput, 0));
}

TEST_CASE ("processor: a host that calls processBlockBypassed hears its input, bit for bit")
{
    LikyuProcessor processor;
    setEveryType (processor);
    processor.setRateAndBufferSizeDetails (48000.0, 256);
    processor.prepareToPlay (48000.0, 256);

    auto buffer = noiseBuffer (2, 256, 7);
    const auto input = buffer;
    juce::MidiBuffer midi;
    processor.processBlockBypassed (buffer, midi);

    CHECK (sameChannel (buffer, 0, input, 0));
    CHECK (sameChannel (buffer, 1, input, 1));
}

TEST_CASE ("processor: constant latency, reported equals measured, whatever the bands, bypassed or not")
{
    for (const auto rate : { 44100.0, 96000.0 })
    {
        for (int choice = 0; choice < dsp::numLookaheadChoices; ++choice)
        {
            // Each type alone, then all eight bands at once.
            for (int type = 0; type <= dsp::numBandTypes; ++type)
            {
                for (const auto bypassed : { false, true })
                {
                    LikyuProcessor processor;
                    setLookahead (processor, choice);

                    if (type < dsp::numBandTypes)
                        setBand (processor, 2, true, static_cast<dsp::BandType> (type), 1000.0, 2.0, 9.0);
                    else
                        setEveryType (processor);

                    processor.setRateAndBufferSizeDetails (rate, 64);
                    processor.prepareToPlay (rate, 64);

                    const auto expected = dsp::lookaheadSamples (choice, rate);

                    INFO ("rate " << rate << ", lookahead " << choice << ", type " << type << ", bypassed " << bypassed);
                    CHECK (processor.getLatencySamples() == expected);
                    CHECK (measuredDelay (processor, bypassed) == expected);
                }
            }
        }
    }
}

TEST_CASE ("processor: layouts, every channel as if it ran alone, the sidechain untouched")
{
    const auto fiveOne = juce::AudioChannelSet::create5point1();
    const auto wide = juce::AudioChannelSet::discreteChannels (64);
    constexpr int samples = 1024;

    for (const auto& main : { mono, stereo, fiveOne, wide })
    {
        for (const auto& sidechain : { none, mono, stereo })
        {
            LikyuProcessor processor;
            REQUIRE (processor.setBusesLayout (layout (main, sidechain)));
            setEveryType (processor);
            processor.setRateAndBufferSizeDetails (48000.0, samples);
            processor.prepareToPlay (48000.0, samples);

            const auto mainChannels = main.size();
            const auto total = processor.getTotalNumInputChannels();
            REQUIRE (total == mainChannels + sidechain.size());

            auto buffer = noiseBuffer (total, samples, 11);
            const auto input = buffer;
            juce::MidiBuffer midi;
            processor.processBlock (buffer, midi);

            INFO ("main " << main.getDescription().toStdString() << ", sidechain " << sidechain.size());

            for (int c = mainChannels; c < total; ++c)
                CHECK (sameChannel (buffer, c, input, c));

            // A few channels against a mono instance fed that channel alone.
            for (const auto c : { 0, mainChannels / 2, mainChannels - 1 })
            {
                LikyuProcessor alone (1);
                setEveryType (alone);
                alone.setRateAndBufferSizeDetails (48000.0, samples);
                alone.prepareToPlay (48000.0, samples);

                juce::AudioBuffer<float> one (1, samples);
                one.copyFrom (0, 0, input, c, 0, samples);
                alone.processBlock (one, midi);

                INFO ("channel " << c);
                CHECK (sameChannel (buffer, c, one, 0));
            }
        }
    }
}

TEST_CASE ("processor: 500 blocks with every band on and parameters moving allocate nothing")
{
    namespace rt = likyu::dsp::rt;
    REQUIRE (rt::isCounting());

    LikyuProcessor processor;
    setEveryType (processor);
    processor.setRateAndBufferSizeDetails (96000.0, 64);
    processor.prepareToPlay (96000.0, 64);

    juce::AudioBuffer<float> buffer (2, 64);
    juce::MidiBuffer midi;

    rt::resetCounts();

    for (int block = 0; block < 500; ++block)
    {
        if (block % 10 == 0)
        {
            // Outside the process call, as a host does: a gain, a type, a bypass.
            processor.setValue (dsp::indexOf (block % dsp::numBands, dsp::BandParam::gain), (block % 61) - 30.0);
            processor.setValue (dsp::indexOf ((block / 10) % dsp::numBands, dsp::BandParam::type), (block / 10) % dsp::numBandTypes);
            processor.setValue (dsp::indexOf (dsp::GlobalParam::bypass), (block / 50) % 2);
        }

        buffer.clear();
        buffer.setSample (0, 0, 1.0f);
        processor.processBlock (buffer, midi);
    }

    CHECK (rt::regions() == 500);
    CHECK (rt::violations() == 0);
}

TEST_CASE ("processor: a state loaded before prepareToPlay plays from its first block (Bitwig's order)")
{
    LikyuProcessor source;
    setEveryType (source);
    source.setValue (dsp::indexOf (dsp::GlobalParam::outGain), -3.0);

    juce::MemoryBlock chunk;
    source.getStateInformation (chunk);

    LikyuProcessor loadedFirst;
    loadedFirst.setStateInformation (chunk.getData(), static_cast<int> (chunk.getSize()));
    loadedFirst.setRateAndBufferSizeDetails (48000.0, 256);
    loadedFirst.prepareToPlay (48000.0, 256);

    LikyuProcessor setDirectly;
    setEveryType (setDirectly);
    setDirectly.setValue (dsp::indexOf (dsp::GlobalParam::outGain), -3.0);
    setDirectly.setRateAndBufferSizeDetails (48000.0, 256);
    setDirectly.prepareToPlay (48000.0, 256);

    auto a = noiseBuffer (2, 256, 21);
    auto b = a;
    juce::MidiBuffer midi;
    loadedFirst.processBlock (a, midi);
    setDirectly.processBlock (b, midi);

    CHECK (sameChannel (a, 0, b, 0));
    CHECK (sameChannel (a, 1, b, 1));
}

TEST_CASE ("processor: prepareToPlay again at the same rate keeps the sound going (Samplitude, Live)")
{
    LikyuProcessor once, twice;

    for (auto* p : { &once, &twice })
    {
        setEveryType (*p);
        p->setRateAndBufferSizeDetails (48000.0, 256);
        p->prepareToPlay (48000.0, 256);
    }

    juce::MidiBuffer midi;

    for (int block = 0; block < 4; ++block)
    {
        if (block == 2)
            twice.prepareToPlay (48000.0, 256);

        auto a = noiseBuffer (2, 256, 30 + block);
        auto b = a;
        once.processBlock (a, midi);
        twice.processBlock (b, midi);

        INFO ("block " << block);
        CHECK (sameChannel (a, 0, b, 0));
    }
}
