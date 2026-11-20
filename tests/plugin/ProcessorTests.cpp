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
#include <utility>
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

TEST_CASE ("processor: the host sees the table, 284 parameters in real units, then the temporary transient constants")
{
    LikyuProcessor processor;
    const auto& table = dsp::parameterTable();
    const auto& times = dsp::temporaryTransientSpecs();

    // The 284, then the four constants until the listening pass (Phase 4). Listen is no parameter.
    REQUIRE (processor.getParameters().size() == static_cast<int> (table.size()) + dsp::numTemporaryTransientTimes);

    for (int i = 0; i < dsp::numTemporaryTransientTimes; ++i)
    {
        auto& time = processor.temporaryTransientTime (i);
        const auto& spec = times[static_cast<std::size_t> (i)];

        INFO ("id: " << spec.id);
        CHECK (processor.getParameters()[static_cast<int> (table.size()) + i] == &time);
        CHECK (time.getParameterID().toStdString() == spec.id);
        CHECK_FALSE (time.isAutomatable());
        CHECK (static_cast<double> (time.get()) == doctest::Approx (spec.defaultValue).epsilon (1.0e-6));
        CHECK (static_cast<double> (time.convertFrom0to1 (0.0f)) == doctest::Approx (spec.minimum).epsilon (1.0e-5));
        CHECK (static_cast<double> (time.convertFrom0to1 (1.0f)) == doctest::Approx (spec.maximum).epsilon (1.0e-5));
        CHECK (juce::URL::addEscapeChars (juce::String (spec.id), true).toStdString() == spec.id);
    }
    CHECK (processor.listen() == -1);

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

    /** A band in Dynamic mode at 0 dB static, its detector following it. */
    void setDynamic (LikyuProcessor& p, int b, dsp::BandType type, double hz, double thresholdDb, double rangeDb,
                     dsp::KeySource key = dsp::KeySource::self, double attackMs = 1.0, double releaseMs = 50.0)
    {
        setBand (p, b, true, type, hz, 1.0, 0.0);
        p.setValue (dsp::indexOf (b, dsp::BandParam::gmode), static_cast<double> (dsp::GainMode::dynamic));
        p.setValue (dsp::indexOf (b, dsp::BandParam::key), static_cast<double> (key));
        p.setValue (dsp::indexOf (b, dsp::BandParam::thresh), thresholdDb);
        p.setValue (dsp::indexOf (b, dsp::BandParam::range), rangeDb);
        p.setValue (dsp::indexOf (b, dsp::BandParam::ratio), 20.0);
        p.setValue (dsp::indexOf (b, dsp::BandParam::atk), attackMs);
        p.setValue (dsp::indexOf (b, dsp::BandParam::rel), releaseMs);
    }

    /** A band in Transient mode at 0 dB static, its detector following it. */
    void setTransient (LikyuProcessor& p, int b, dsp::BandType type, double hz, double attackPercent, double sustainPercent,
                       double rangeDb, dsp::KeySource key = dsp::KeySource::self)
    {
        setBand (p, b, true, type, hz, 1.0, 0.0);
        p.setValue (dsp::indexOf (b, dsp::BandParam::gmode), static_cast<double> (dsp::GainMode::transient));
        p.setValue (dsp::indexOf (b, dsp::BandParam::key), static_cast<double> (key));
        p.setValue (dsp::indexOf (b, dsp::BandParam::tatk), attackPercent);
        p.setValue (dsp::indexOf (b, dsp::BandParam::tsus), sustainPercent);
        p.setValue (dsp::indexOf (b, dsp::BandParam::range), rangeDb);
    }

    void addTone (juce::AudioBuffer<float>& buffer, int channel, double hz, double amplitude, int from, int to)
    {
        for (int n = from; n < to; ++n)
            buffer.addSample (channel, n, static_cast<float> (amplitude * std::sin (2.0 * 3.14159265358979323846 * hz * n / 48000.0)));
    }

    /** `buffer` through `p` in blocks of `block`. */
    void processInBlocks (LikyuProcessor& p, juce::AudioBuffer<float>& buffer, int block)
    {
        juce::MidiBuffer midi;

        for (int start = 0; start < buffer.getNumSamples(); start += block)
        {
            const auto n = juce::jmin (block, buffer.getNumSamples() - start);
            juce::AudioBuffer<float> part (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, n);
            p.processBlock (part, midi);
        }
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

//==============================================================================
TEST_CASE ("processor: detectors read the input undelayed: with a lookahead, a dynamic band's cut is in place when the delayed burst arrives")
{
    constexpr int onset = 2400, length = 9600;

    const auto firstOfBurst = [] (int lookahead)
    {
        LikyuProcessor processor;
        setLookahead (processor, lookahead);
        setDynamic (processor, 0, dsp::BandType::bell, 3000.0, -40.0, -20.0, dsp::KeySource::self, 1.0, 100.0);
        processor.setRateAndBufferSizeDetails (48000.0, 64);
        processor.prepareToPlay (48000.0, 64);

        juce::AudioBuffer<float> buffer (2, length);
        buffer.clear();
        addTone (buffer, 0, 3000.0, 0.5, onset, length);
        addTone (buffer, 1, 3000.0, 0.5, onset, length);
        const auto input = buffer;
        processInBlocks (processor, buffer, 64);

        /*  The energy of the burst's first 2 ms as it comes out, against the
            same samples going in. Energy, not the peak: a filter cut to -19 dB
            still lets a sudden sine through for the few samples its band-pass
            takes to answer. */
        const auto delay = processor.getLatencySamples();
        double in = 0.0, out = 0.0;

        for (int n = 0; n < 96; ++n)
        {
            in += std::pow (static_cast<double> (input.getSample (0, onset + n)), 2.0);
            out += std::pow (static_cast<double> (buffer.getSample (0, onset + delay + n)), 2.0);
        }

        return 10.0 * std::log10 (out / in);
    };

    const auto without = firstOfBurst (0), with = firstOfBurst (3);

    INFO ("the burst's first 2 ms: " << without << " dB without lookahead, " << with << " dB with 3 ms");
    CHECK (with < without - 6.0);
    CHECK (with < -12.0);
}

TEST_CASE ("processor: a Sidechain band is keyed by the bus; disabled, it falls back to Self; enabled at exact zeros, it is keyed by silence")
{
    constexpr int length = 4800;

    const auto run = [] (const juce::AudioChannelSet& sidechain, dsp::KeySource key, bool loudMain, bool loudSidechain)
    {
        LikyuProcessor processor;
        REQUIRE (processor.setBusesLayout (layout (stereo, sidechain)));
        setDynamic (processor, 0, dsp::BandType::bell, 2000.0, -40.0, -12.0, key);
        processor.setRateAndBufferSizeDetails (48000.0, 256);
        processor.prepareToPlay (48000.0, 256);

        juce::AudioBuffer<float> buffer (processor.getTotalNumInputChannels(), length);
        buffer.clear();

        for (int c = 0; c < 2; ++c)
            if (loudMain)
                addTone (buffer, c, 2000.0, 0.5, 0, length);
            else
                for (int n = 0; n < length; ++n)
                    buffer.setSample (c, n, 1.0e-4f * static_cast<float> ((n * 7919 + c * 31) % 200 - 100) / 100.0f);

        if (loudSidechain)
            for (int c = 2; c < buffer.getNumChannels(); ++c)
                addTone (buffer, c, 2000.0, 0.5, 0, length);

        const auto input = buffer;
        processInBlocks (processor, buffer, 256);
        return std::pair { input, buffer };
    };

    // Enabled and fed: the quiet main is cut by the sidechain's tone.
    {
        const auto [in, out] = run (stereo, dsp::KeySource::sidechain, false, true);
        CHECK_FALSE (sameChannel (out, 0, in, 0));

        // The sidechain never reaches the output.
        CHECK (sameChannel (out, 2, in, 2));
    }

    // Enabled at exact zeros: a key of silence, so the loud main passes untouched.
    {
        const auto [in, out] = run (stereo, dsp::KeySource::sidechain, true, false);
        CHECK (sameChannel (out, 0, in, 0));
        CHECK (sameChannel (out, 1, in, 1));
    }

    // Disabled: the band runs on Self, exactly as a Self band does.
    {
        const auto [in, fellBack] = run (none, dsp::KeySource::sidechain, true, false);
        const auto [unused, self] = run (none, dsp::KeySource::self, true, false);
        CHECK_FALSE (sameChannel (fellBack, 0, in, 0));
        CHECK (sameChannel (fellBack, 0, self, 0));
        CHECK (sameChannel (fellBack, 1, self, 1));
    }
}

TEST_CASE ("processor: link exclusion on 5.1: the list applies to its layout, survives state, and any other layout has every channel in")
{
    constexpr int lfe = 3, length = 4800;
    const auto fiveOne = juce::AudioChannelSet::create5point1();
    const auto sevenOne = juce::AudioChannelSet::create7point1();
    const auto wide = juce::AudioChannelSet::discreteChannels (64);

    const auto makeProcessor = [] (LikyuProcessor& p, const juce::AudioChannelSet& main)
    {
        REQUIRE (p.setBusesLayout (layout (main, none)));
        setDynamic (p, 0, dsp::BandType::lowShelf, 80.0, -30.0, -9.0);
        p.setRateAndBufferSizeDetails (48000.0, 256);
        p.prepareToPlay (48000.0, 256);
    };

    /*  Quiet noise everywhere, the key under the threshold, and a burst at
        80 Hz on the LFE: a channel whose gain moves is not the input any more,
        and one whose gain does not is the input, bit for bit. A second of the
        quiet noise first, for what came before to return to zero. */
    const auto moved = [] (LikyuProcessor& p)
    {
        const auto channels = p.getTotalNumInputChannels();

        auto settle = noiseBuffer (channels, 48000, 40);
        settle.applyGain (1.0e-4f);
        processInBlocks (p, settle, 256);

        auto buffer = noiseBuffer (channels, length, 41);
        buffer.applyGain (1.0e-4f);
        addTone (buffer, lfe, 80.0, 0.5, 480, length);
        const auto input = buffer;
        processInBlocks (p, buffer, 256);

        std::vector<bool> result;

        for (int c = 0; c < channels; ++c)
            result.push_back (! sameChannel (buffer, c, input, c));

        return result;
    };

    LikyuProcessor first;
    makeProcessor (first, fiveOne);
    CHECK (first.currentLayoutName() == "L R C Lfe Ls Rs");

    // Every channel in, the default: the LFE's burst moves all six.
    CHECK (moved (first) == std::vector<bool> (6, true));

    // The LFE left out: its burst moves it alone.
    const core::LinkList lfeOut { first.currentLayoutName(), 6, std::uint64_t (1) << lfe };
    first.setLinkList (lfeOut);
    CHECK (moved (first) == std::vector<bool> { false, false, false, true, false, false });

    juce::MemoryBlock chunk;
    first.getStateInformation (chunk);

    // A second instance on 5.1, loaded from the first: the same list, the same behaviour.
    {
        LikyuProcessor second;
        makeProcessor (second, fiveOne);
        second.setStateInformation (chunk.getData(), static_cast<int> (chunk.getSize()));
        CHECK (second.getLinkList() == lfeOut);
        CHECK (moved (second) == std::vector<bool> { false, false, false, true, false, false });
    }

    // On 7.1 the list is kept but not applied: every channel in.
    {
        LikyuProcessor other;
        makeProcessor (other, sevenOne);
        other.setStateInformation (chunk.getData(), static_cast<int> (chunk.getSize()));
        CHECK (other.getLinkList() == lfeOut);
        CHECK (moved (other) == std::vector<bool> (8, true));
    }

    // Bitwig's order, state before the layout: the list applies once the layout is its own.
    {
        LikyuProcessor bitwig;
        bitwig.setStateInformation (chunk.getData(), static_cast<int> (chunk.getSize()));
        makeProcessor (bitwig, fiveOne);
        CHECK (moved (bitwig) == std::vector<bool> { false, false, false, true, false, false });
    }

    // REAPER's 64 channels for an instant while loading erase nothing.
    {
        LikyuProcessor reaper;
        makeProcessor (reaper, fiveOne);
        reaper.setStateInformation (chunk.getData(), static_cast<int> (chunk.getSize()));
        reaper.releaseResources();
        makeProcessor (reaper, wide);
        reaper.releaseResources();
        makeProcessor (reaper, fiveOne);
        CHECK (reaper.getLinkList() == lfeOut);
        CHECK (moved (reaper) == std::vector<bool> { false, false, false, true, false, false });
    }
}

TEST_CASE ("processor: a band's channels on 5.1: a low cut the LFE is spared; the lists apply to their layout, survive state, and the editor's calls make them")
{
    constexpr int lfe = 3, length = 4800;
    const auto fiveOne = juce::AudioChannelSet::create5point1();
    const auto sevenOne = juce::AudioChannelSet::create7point1();

    const auto makeProcessor = [] (LikyuProcessor& p, const juce::AudioChannelSet& main)
    {
        REQUIRE (p.setBusesLayout (layout (main, none)));
        setBand (p, 0, true, dsp::BandType::highPass, 120.0, 0.7, 0.0);
        p.setRateAndBufferSizeDetails (48000.0, 256);
        p.prepareToPlay (48000.0, 256);
    };

    // Noise on every channel: a channel the low cut applies to is not the input any more.
    const auto cut = [] (LikyuProcessor& p)
    {
        const auto channels = p.getTotalNumInputChannels();
        auto buffer = noiseBuffer (channels, length, 60);
        const auto input = buffer;
        processInBlocks (p, buffer, 256);

        std::vector<bool> result;

        for (int c = 0; c < channels; ++c)
            result.push_back (! sameChannel (buffer, c, input, c));

        return result;
    };

    LikyuProcessor first;
    makeProcessor (first, fiveOne);

    // The channels as the host names them, for the editor's cells.
    CHECK (first.channelNames() == juce::StringArray { "L", "R", "C", "Lfe", "Ls", "Rs" });

    // Every channel, the default.
    CHECK (first.bandChannelsLeftOut (0) == 0);
    CHECK (cut (first) == std::vector<bool> (6, true));

    // The editor takes the LFE out of band 1: the lists become this layout's.
    first.setBandChannelsLeftOut (0, std::uint64_t (1) << lfe);
    CHECK (first.bandChannelsLeftOut (0) == std::uint64_t (1) << lfe);
    CHECK (first.bandChannelsLeftOut (1) == 0);
    CHECK (first.getBandChannels().layout == "L R C Lfe Ls Rs");
    CHECK (first.getBandChannels().channels == 6);

    // A second of noise for the band's fade through the identity, then: the LFE untouched, bit for bit.
    cut (first);
    CHECK (cut (first) == std::vector<bool> { true, true, true, false, true, true });

    const auto lists = first.getBandChannels();
    juce::MemoryBlock chunk;
    first.getStateInformation (chunk);
    CHECK (chunk.toString().contains ("band_out"));

    // A second instance on 5.1, loaded from the first.
    {
        LikyuProcessor second;
        makeProcessor (second, fiveOne);
        second.setStateInformation (chunk.getData(), static_cast<int> (chunk.getSize()));
        CHECK (second.getBandChannels() == lists);
        cut (second);
        CHECK (cut (second) == std::vector<bool> { true, true, true, false, true, true });
    }

    // On 7.1 the lists are kept but not applied: every channel.
    {
        LikyuProcessor other;
        makeProcessor (other, sevenOne);
        other.setStateInformation (chunk.getData(), static_cast<int> (chunk.getSize()));
        CHECK (other.getBandChannels() == lists);
        CHECK (other.bandChannelsLeftOut (0) == 0);
        cut (other);
        CHECK (cut (other) == std::vector<bool> (8, true));

        // Edited there, they are made again for 7.1.
        other.setBandChannelsLeftOut (2, 1u);
        CHECK (other.getBandChannels().channels == 8);
        CHECK (other.bandChannelsLeftOut (0) == 0);
        CHECK (other.bandChannelsLeftOut (2) == 1u);
    }

    // Bitwig's order, state before the layout: the lists apply once the layout is their own.
    {
        LikyuProcessor bitwig;
        bitwig.setStateInformation (chunk.getData(), static_cast<int> (chunk.getSize()));
        makeProcessor (bitwig, fiveOne);
        cut (bitwig);
        CHECK (cut (bitwig) == std::vector<bool> { true, true, true, false, true, true });
    }

    // A stereo instance names its two channels, and a discrete one counts them.
    {
        LikyuProcessor stereoOne;
        CHECK (stereoOne.channelNames() == juce::StringArray { "L", "R" });

        LikyuProcessor discrete;
        REQUIRE (discrete.setBusesLayout (layout (juce::AudioChannelSet::discreteChannels (4), none)));
        CHECK (discrete.channelNames() == juce::StringArray { "1", "2", "3", "4" });
    }
}

TEST_CASE ("processor: one block far longer than maximumBlockSize gives what blocks of 64 give, with a lookahead")
{
    constexpr int length = 6000;

    const auto make = [] (LikyuProcessor& p)
    {
        setLookahead (p, 2);
        setDynamic (p, 0, dsp::BandType::bell, 1000.0, -40.0, -12.0);
        setDynamic (p, 4, dsp::BandType::highShelf, 6000.0, -50.0, 6.0);
        p.setRateAndBufferSizeDetails (48000.0, 64);
        p.prepareToPlay (48000.0, 64);
    };

    LikyuProcessor small, large;
    make (small);
    make (large);

    auto a = noiseBuffer (2, length, 51);
    auto b = a;
    const auto input = a;
    processInBlocks (small, a, 64);
    processInBlocks (large, b, length);     // one block, 94 times the announced size

    CHECK_FALSE (sameChannel (a, 0, input, 0));
    CHECK (sameChannel (a, 0, b, 0));
    CHECK (sameChannel (a, 1, b, 1));
}

TEST_CASE ("processor: constant latency with Dynamic and Transient bands on both keys, reported equals measured, bypassed or not")
{
    for (const auto gmode : { dsp::GainMode::dynamic, dsp::GainMode::transient })
    {
        for (int choice = 0; choice < dsp::numLookaheadChoices; ++choice)
        {
            for (const auto bypassed : { false, true })
            {
                LikyuProcessor processor;
                REQUIRE (processor.setBusesLayout (layout (stereo, stereo)));
                setLookahead (processor, choice);

                for (int b = 0; b < dsp::numBands; ++b)
                {
                    const auto type = static_cast<dsp::BandType> (b % 3);
                    const auto hz = 60.0 * std::pow (2.0, b);
                    const auto key = b % 2 == 0 ? dsp::KeySource::self : dsp::KeySource::sidechain;

                    if (gmode == dsp::GainMode::dynamic)
                        setDynamic (processor, b, type, hz, -60.0, b % 2 == 0 ? -12.0 : 6.0, key);
                    else
                        setTransient (processor, b, type, hz, b % 2 == 0 ? -100.0 : 100.0, 60.0, b % 2 == 0 ? -12.0 : 6.0, key);
                }

                processor.setRateAndBufferSizeDetails (48000.0, 64);
                processor.prepareToPlay (48000.0, 64);

                const auto expected = dsp::lookaheadSamples (choice, 48000.0);

                INFO ((gmode == dsp::GainMode::dynamic ? "Dynamic" : "Transient") << ", lookahead " << choice << ", bypassed " << bypassed);
                CHECK (processor.getLatencySamples() == expected);
                CHECK (measuredDelay (processor, bypassed) == expected);
            }
        }
    }
}

TEST_CASE ("processor: the temporary transient constants reach every band, leave the latency alone and are not saved")
{
    constexpr int onset = 2400, length = 9600;

    // A 3 kHz burst through a Transient bell, attack -100 %: how much of its first 30 ms is cut.
    const auto cutDb = [] (LikyuProcessor& processor)
    {
        juce::AudioBuffer<float> buffer (2, length);
        buffer.clear();

        for (int n = onset; n < length; ++n)
            for (int c = 0; c < 2; ++c)
                buffer.setSample (c, n, static_cast<float> (0.5 * std::sin (2.0 * 3.14159265358979323846 * 3000.0 * (n - onset) / 48000.0)));

        const auto input = buffer;
        processInBlocks (processor, buffer, 64);

        double in = 0.0, out = 0.0;

        for (int n = onset; n < onset + 1440; ++n)
        {
            in += std::pow (static_cast<double> (input.getSample (0, n)), 2.0);
            out += std::pow (static_cast<double> (buffer.getSample (0, n)), 2.0);
        }

        return 10.0 * std::log10 (out / in);
    };

    const auto make = [] (LikyuProcessor& p)
    {
        setTransient (p, 5, dsp::BandType::bell, 3000.0, -100.0, 0.0, -30.0);   // a Range that does not clamp it
        p.setRateAndBufferSizeDetails (48000.0, 64);
        p.prepareToPlay (48000.0, 64);
    };

    LikyuProcessor starting, slower;
    make (starting);
    make (slower);

    // The slow attack four times longer: the attack signal lasts longer, so more of the burst is cut.
    auto& slowAttack = slower.temporaryTransientTime (1);
    slowAttack.setValueNotifyingHost (slowAttack.convertTo0to1 (60.0f));
    CHECK (static_cast<double> (slowAttack.get()) == doctest::Approx (60.0).epsilon (1.0e-5));

    const auto atStart = cutDb (starting), atSlower = cutDb (slower);

    INFO ("the burst's first 30 ms: " << atStart << " dB with the starting values, " << atSlower << " dB with a 60 ms slow attack");
    CHECK (atSlower < atStart - 1.0);
    CHECK (slower.getLatencySamples() == 0);

    // Not saved: the chunk does not name them, and an instance loaded from it keeps its own.
    juce::MemoryBlock chunk;
    slower.getStateInformation (chunk);
    const auto text = juce::String::fromUTF8 (static_cast<const char*> (chunk.getData()), static_cast<int> (chunk.getSize()));
    CHECK_FALSE (text.contains ("tmp_"));

    LikyuProcessor loaded;
    loaded.setStateInformation (chunk.getData(), static_cast<int> (chunk.getSize()));
    CHECK (static_cast<double> (loaded.temporaryTransientTime (1).get()) == doctest::Approx (dsp::transientSlowAttackMs).epsilon (1.0e-6));
}

TEST_CASE ("processor: lookahead effect: a burst through a Transient bell with a negative attack: with 1 ms its leading edge is cut as a static cut would; without, it passes")
{
    constexpr int onset = 2400, length = 9600;

    // `lookahead` -1: a static bell already at the full cut, the most a lookahead can give.
    const auto leadingEdgeDb = [] (int lookahead, int samples)
    {
        LikyuProcessor processor;
        setLookahead (processor, std::max (0, lookahead));

        if (lookahead < 0)
            setBand (processor, 0, true, dsp::BandType::bell, 3000.0, 1.0, -12.0);
        else
            setTransient (processor, 0, dsp::BandType::bell, 3000.0, -100.0, 0.0, -12.0);

        processor.setRateAndBufferSizeDetails (48000.0, 64);
        processor.prepareToPlay (48000.0, 64);

        juce::AudioBuffer<float> buffer (2, length);
        buffer.clear();

        for (int n = onset; n < length; ++n)
            for (int c = 0; c < 2; ++c)
                buffer.setSample (c, n, static_cast<float> (0.5 * std::sin (2.0 * 3.14159265358979323846 * 3000.0 * (n - onset) / 48000.0)));

        const auto input = buffer;
        processInBlocks (processor, buffer, 64);

        const auto delay = processor.getLatencySamples();
        double in = 0.0, out = 0.0;

        for (int n = 0; n < samples; ++n)
        {
            in += std::pow (static_cast<double> (input.getSample (0, onset + n)), 2.0);
            out += std::pow (static_cast<double> (buffer.getSample (0, onset + delay + n)), 2.0);
        }

        return 10.0 * std::log10 (out / in);
    };

    for (const auto samples : { 6, 24, 48 })   // 0.125, 0.5 and 1 ms
    {
        const auto without = leadingEdgeDb (0, samples), with = leadingEdgeDb (2, samples), best = leadingEdgeDb (-1, samples);

        INFO ("the burst's first " << samples << " samples: " << without << " dB without lookahead, " << with
                                   << " dB with 1 ms, " << best << " dB through a static bell at -12 dB");
        CHECK (std::abs (with - best) < 0.01);
        CHECK (with < without - 1.5);

        if (samples < 10)
            CHECK (without > -1.0);
    }
}

TEST_CASE ("processor: 500 blocks of dynamic bands, a sidechain, the link and its list moving allocate nothing")
{
    namespace rt = likyu::dsp::rt;
    REQUIRE (rt::isCounting());

    LikyuProcessor processor;
    REQUIRE (processor.setBusesLayout (layout (stereo, stereo)));

    for (int b = 0; b < dsp::numBands; ++b)
        setDynamic (processor, b, static_cast<dsp::BandType> (b % 3), 60.0 * std::pow (2.0, b), -50.0, -9.0,
                    b % 2 == 0 ? dsp::KeySource::self : dsp::KeySource::sidechain);

    setLookahead (processor, 2);
    processor.setRateAndBufferSizeDetails (96000.0, 64);
    processor.prepareToPlay (96000.0, 64);

    auto buffer = noiseBuffer (4, 64, 61);
    juce::MidiBuffer midi;

    rt::resetCounts();

    for (int block = 0; block < 500; ++block)
    {
        if (block % 10 == 0)
        {
            // Outside the process call, as a host or the editor does.
            processor.setValue (dsp::indexOf (dsp::GlobalParam::link), (block / 10) % 2);
            processor.setValue (dsp::indexOf (block % dsp::numBands, dsp::BandParam::thresh), -60.0 + block % 50);
            processor.setLinkList ({ processor.currentLayoutName(), 2, static_cast<std::uint64_t> ((block / 20) % 4) });
        }

        processor.processBlock (buffer, midi);
    }

    CHECK (rt::regions() == 500);
    CHECK (rt::violations() == 0);
}

//==============================================================================
TEST_CASE ("processor: the temporary Listen puts a band's detector on the output and is not saved")
{
    constexpr int length = 4800;

    LikyuProcessor processor;
    setBand (processor, 1, true, dsp::BandType::bell, 1000.0, 2.0, 6.0);
    processor.setValue (dsp::indexOf (1, dsp::BandParam::dfilt), static_cast<double> (dsp::DetectorFilter::own));
    processor.setValue (dsp::indexOf (1, dsp::BandParam::dtype), static_cast<double> (dsp::DetectorType::highPass));
    processor.setValue (dsp::indexOf (1, dsp::BandParam::dfreq), 4000.0);
    processor.setRateAndBufferSizeDetails (48000.0, 256);
    processor.prepareToPlay (48000.0, 256);

    // Band 2's high-pass detector at 4 kHz: a 200 Hz tone almost gone, a 9 kHz one through.
    const auto levelOf = [&processor] (double hz)
    {
        juce::AudioBuffer<float> buffer (2, length);
        buffer.clear();
        addTone (buffer, 0, hz, 0.5, 0, length);
        addTone (buffer, 1, hz, 0.5, 0, length);
        processInBlocks (processor, buffer, 256);
        return 20.0 * std::log10 (static_cast<double> (buffer.getMagnitude (0, length / 2, length / 2)) / 0.5);
    };

    processor.setListen (1);
    const auto low = levelOf (200.0), high = levelOf (9000.0);

    INFO ("Listen on band 2: 200 Hz at " << low << " dB, 9 kHz at " << high << " dB");
    CHECK (low < -40.0);
    CHECK (std::abs (high) < 1.0);

    core::Meters meters;
    processor.meters().read (meters);
    CHECK (meters.listening == 1);

    // Not saved: the chunk has no such field, and a new instance starts with Listen off.
    juce::MemoryBlock chunk;
    processor.getStateInformation (chunk);
    CHECK_FALSE (chunk.toString().containsIgnoreCase ("listen"));

    LikyuProcessor restored;
    restored.setStateInformation (chunk.getData(), static_cast<int> (chunk.getSize()));
    CHECK (restored.listen() == -1);

    // Off again: the bell, no longer the detector.
    processor.setListen (-1);
    CHECK (std::abs (levelOf (1000.0) - 6.0) < 0.1);
}

TEST_CASE ("processor: the meters say each band's offset and frequency, and that a Sidechain band fell back to Self")
{
    LikyuProcessor processor;
    setDynamic (processor, 0, dsp::BandType::bell, 2000.0, -40.0, -12.0, dsp::KeySource::sidechain);
    setDynamic (processor, 3, dsp::BandType::highShelf, 5000.0, -40.0, 6.0);
    processor.setRateAndBufferSizeDetails (48000.0, 256);
    processor.prepareToPlay (48000.0, 256);

    juce::AudioBuffer<float> buffer (2, 4800);
    buffer.clear();
    addTone (buffer, 0, 2000.0, 0.5, 0, 4800);
    addTone (buffer, 1, 2000.0, 0.5, 0, 4800);
    processInBlocks (processor, buffer, 256);

    core::Meters meters;
    REQUIRE (processor.meters().read (meters));
    CHECK (meters.block == 4800 / 256 + 1);
    CHECK (meters.listening == -1);

    // The sidechain bus is disabled: band 1 runs on Self, is cut by the tone, and says it fell back.
    CHECK (meters.bands[0].keyFellBack);
    CHECK (meters.bands[0].offsetDb < -6.0f);
    CHECK (meters.bands[0].frequencyHz == doctest::Approx (2000.0).epsilon (1.0e-5));

    // Band 4, keyed by Self, at rest: the tone is far under its high shelf's band-pass.
    CHECK_FALSE (meters.bands[3].keyFellBack);
    CHECK (meters.bands[3].confidence == 0.0f);
}

//==============================================================================
namespace
{
    /** A band's frequency mode and range, through the host's parameters. */
    void setFrequencyMode (LikyuProcessor& p, int b, dsp::FrequencyMode fmode, double lowHz, double highHz)
    {
        p.setValue (dsp::indexOf (b, dsp::BandParam::fmode), static_cast<double> (fmode));
        p.setValue (dsp::indexOf (b, dsp::BandParam::flo), lowHz);
        p.setValue (dsp::indexOf (b, dsp::BandParam::fhi), highHz);
    }
}

TEST_CASE ("processor: constant latency with Env and Track bands in every gain mode, on both keys, reported equals measured, bypassed or not")
{
    for (const auto fmode : { dsp::FrequencyMode::env, dsp::FrequencyMode::track })
    {
        for (const auto gmode : { dsp::GainMode::staticGain, dsp::GainMode::dynamic, dsp::GainMode::transient })
        {
            for (int choice = 0; choice < dsp::numLookaheadChoices; ++choice)
            {
                for (const auto bypassed : { false, true })
                {
                    LikyuProcessor processor;
                    REQUIRE (processor.setBusesLayout (layout (stereo, stereo)));
                    setLookahead (processor, choice);

                    for (int b = 0; b < dsp::numBands; ++b)
                    {
                        const auto type = static_cast<dsp::BandType> (b % dsp::numBandTypes);
                        const auto hz = 60.0 * std::pow (2.0, b);
                        const auto key = b % 2 == 0 ? dsp::KeySource::self : dsp::KeySource::sidechain;

                        if (gmode == dsp::GainMode::transient)
                            setTransient (processor, b, type, hz, b % 2 == 0 ? -100.0 : 100.0, 60.0, b % 2 == 0 ? -12.0 : 6.0, key);
                        else
                            setDynamic (processor, b, type, hz, -60.0, b % 2 == 0 ? -12.0 : 6.0, key);

                        if (gmode == dsp::GainMode::staticGain)
                            processor.setValue (dsp::indexOf (b, dsp::BandParam::gmode), 0.0);

                        processor.setValue (dsp::indexOf (b, dsp::BandParam::gain), b % 2 == 0 ? 6.0 : -6.0);
                        setFrequencyMode (processor, b, fmode, hz / 2.0, hz * 2.0);
                    }

                    processor.setRateAndBufferSizeDetails (48000.0, 64);
                    processor.prepareToPlay (48000.0, 64);

                    const auto expected = dsp::lookaheadSamples (choice, 48000.0);

                    INFO ((fmode == dsp::FrequencyMode::env ? "Env" : "Track") << ", gain mode " << static_cast<int> (gmode)
                          << ", lookahead " << choice << ", bypassed " << bypassed);
                    CHECK (processor.getLatencySamples() == expected);
                    CHECK (measuredDelay (processor, bypassed) == expected);
                }
            }
        }
    }
}

TEST_CASE ("processor: the meters say a Track band's frequency and confidence, and an Env band's frequency")
{
    LikyuProcessor processor;

    // Band 1 tracks between 500 Hz and 2 kHz from a rest at 700 Hz; band 3,
    // in Env, maps the tone's level onto 300 Hz to 3 kHz.
    setBand (processor, 0, true, dsp::BandType::bell, 700.0, 2.0, 6.0);
    setFrequencyMode (processor, 0, dsp::FrequencyMode::track, 500.0, 2000.0);
    setBand (processor, 2, true, dsp::BandType::bell, 1000.0, 1.0, 3.0);
    setFrequencyMode (processor, 2, dsp::FrequencyMode::env, 300.0, 3000.0);
    processor.setValue (dsp::indexOf (2, dsp::BandParam::fsens), -60.0);
    processor.setValue (dsp::indexOf (2, dsp::BandParam::fspan), 60.0);

    processor.setRateAndBufferSizeDetails (48000.0, 256);
    processor.prepareToPlay (48000.0, 256);

    juce::AudioBuffer<float> buffer (2, 24000);
    buffer.clear();
    addTone (buffer, 0, 1100.0, 0.5, 0, 24000);
    addTone (buffer, 1, 1100.0, 0.5, 0, 24000);
    processInBlocks (processor, buffer, 256);

    core::Meters meters;
    REQUIRE (processor.meters().read (meters));

    CHECK (meters.bands[0].frequencyHz == doctest::Approx (1100.0).epsilon (0.005));
    CHECK (meters.bands[0].confidence > 0.95f);

    // Env reads the tone at -6 dB through its band-pass at 1 kHz: 54 dB into a 60 dB span, near High.
    CHECK (meters.bands[2].frequencyHz > 1500.0f);
    CHECK (meters.bands[2].frequencyHz < 3000.0f);
    CHECK (meters.bands[2].confidence == 0.0f);
}

TEST_CASE ("processor: 500 blocks of Env and Track bands, a sidechain, their modes and values moving allocate nothing")
{
    namespace rt = likyu::dsp::rt;
    REQUIRE (rt::isCounting());

    LikyuProcessor processor;
    REQUIRE (processor.setBusesLayout (layout (stereo, stereo)));

    for (int b = 0; b < dsp::numBands; ++b)
    {
        const auto hz = 60.0 * std::pow (2.0, b);
        setDynamic (processor, b, static_cast<dsp::BandType> (b % 3), hz, -50.0, -9.0,
                    b % 2 == 0 ? dsp::KeySource::self : dsp::KeySource::sidechain);
        setFrequencyMode (processor, b, b % 2 == 0 ? dsp::FrequencyMode::track : dsp::FrequencyMode::env, hz / 2.0, hz * 2.0);
    }

    setLookahead (processor, 2);
    processor.setRateAndBufferSizeDetails (96000.0, 64);
    processor.prepareToPlay (96000.0, 64);

    auto buffer = noiseBuffer (4, 64, 71);
    juce::MidiBuffer midi;

    rt::resetCounts();

    for (int block = 0; block < 500; ++block)
    {
        if (block % 10 == 0)
        {
            // Outside the process call, as a host or the editor does.
            const auto b = block % dsp::numBands;
            processor.setValue (dsp::indexOf (dsp::GlobalParam::link), (block / 10) % 2);
            processor.setValue (dsp::indexOf (b, dsp::BandParam::fmode), (block / 10) % dsp::numFrequencyModes);
            processor.setValue (dsp::indexOf (b, dsp::BandParam::fhi), 1000.0 + 50.0 * (block % 40));
            processor.setValue (dsp::indexOf (b, dsp::BandParam::fcgain), (block / 20) % 2);
            processor.setLinkList ({ processor.currentLayoutName(), 2, static_cast<std::uint64_t> ((block / 20) % 4) });
        }

        processor.processBlock (buffer, midi);
    }

    CHECK (rt::regions() == 500);
    CHECK (rt::violations() == 0);
}
