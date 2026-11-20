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

#include "PluginProcessor.h"

#include <core/ValueText.h>
#include <dsp/rt/RtCheck.h>

#include <algorithm>
#include <cmath>

namespace likyu
{
    namespace
    {
        /*  Bumped only if a parameter is ever ADDED after the IDs freeze (AU
            orders parameters by this hint, then by ID). */
        constexpr int parameterVersionHint = 1;

        juce::AudioChannelSet setForChannels (int channels)
        {
            return channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        }

        /*  A continuous parameter's 0 to 1: the table's own mapping
            (dsp/Parameters.h) between the range's FLOAT bounds, which JUCE
            passes, so that the ends map to exactly 0 and 1 (0.1 as a float is
            a little above 0.1, and auval sets and reads back each end); and a
            snap that gives back a value within a millionth of a round number
            as that number, so that every default maps to itself through the
            float arithmetic (auval warned on 18 that did not, Phase 1). */
        juce::NormalisableRange<float> rangeFor (const dsp::ParamSpec& spec)
        {
            const auto skew = spec.skew;

            return { static_cast<float> (spec.minimum), static_cast<float> (spec.maximum),
                     [skew] (float lo, float hi, float proportion)
                     {
                         return static_cast<float> (dsp::fromNormalised (skew, lo, hi, static_cast<double> (proportion)));
                     },
                     [skew] (float lo, float hi, float value)
                     {
                         return static_cast<float> (dsp::toNormalised (skew, lo, hi, static_cast<double> (value)));
                     },
                     [] (float lo, float hi, float value)
                     {
                         const auto clamped = std::clamp (static_cast<double> (value), static_cast<double> (lo), static_cast<double> (hi));
                         return std::clamp (static_cast<float> (dsp::snapToRound (clamped)), lo, hi);
                     } };
        }

        std::unique_ptr<juce::RangedAudioParameter> makeParameter (const dsp::ParamSpec& spec)
        {
            const juce::ParameterID id { juce::String (spec.id), parameterVersionHint };
            const juce::String name (spec.name);

            switch (spec.kind)
            {
                case dsp::ParamKind::toggle:
                    return std::make_unique<juce::AudioParameterBool> (
                        id, name, spec.defaultValue > 0.5,
                        juce::AudioParameterBoolAttributes().withAutomatable (spec.automatable));

                case dsp::ParamKind::choice:
                {
                    juce::StringArray entries;

                    for (const auto& entry : spec.choices)
                        entries.add (juce::String (entry));

                    return std::make_unique<juce::AudioParameterChoice> (
                        id, name, entries, static_cast<int> (spec.defaultValue),
                        juce::AudioParameterChoiceAttributes().withAutomatable (spec.automatable));
                }

                case dsp::ParamKind::continuous:
                    break;
            }

            /*  The unit is in the value text (core/ValueText.h), where the
                hosts of Phase 1 show it; not also as a label, which some hosts
                print after the text. */
            const auto* s = &spec;

            return std::make_unique<juce::AudioParameterFloat> (
                id, name, rangeFor (spec), static_cast<float> (spec.defaultValue),
                juce::AudioParameterFloatAttributes()
                    .withAutomatable (spec.automatable)
                    .withStringFromValueFunction ([s] (float value, int maximumLength)
                    {
                        const auto text = core::valueText (*s, static_cast<double> (value));
                        return maximumLength > 0 ? text.substring (0, maximumLength) : text;
                    })
                    .withValueFromStringFunction ([s] (const juce::String& text)
                    {
                        return static_cast<float> (core::valueFromText (*s, text));
                    }));
        }
    }

    //==========================================================================
    LikyuProcessor::LikyuProcessor (int defaultChannels)
        : juce::AudioProcessor (BusesProperties()
                                    .withInput ("Input", setForChannels (defaultChannels), true)
                                    .withOutput ("Output", setForChannels (defaultChannels), true)
                                    /*  Not enabled by default: JUCE's LV2 wrapper then marks its
                                        ports lv2:connectionOptional, and a host that never routes a
                                        sidechain never sees the plugin ask for one. */
                                    .withInput ("Sidechain", setForChannels (defaultChannels), false))
    {
        const auto& table = dsp::parameterTable();
        parameters.reserve (table.size());
        readers.reserve (table.size());

        for (const auto& spec : table)
        {
            auto parameter = makeParameter (spec);
            parameters.push_back (parameter.get());

            Reader reader;
            reader.continuous = dynamic_cast<juce::AudioParameterFloat*> (parameter.get());
            reader.choice = dynamic_cast<juce::AudioParameterChoice*> (parameter.get());
            reader.toggle = dynamic_cast<juce::AudioParameterBool*> (parameter.get());
            readers.push_back (reader);

            addParameter (parameter.release());
        }

        parameterAt (dsp::indexOf (dsp::GlobalParam::lookahead)).addListener (this);
    }

    LikyuProcessor::~LikyuProcessor()
    {
        parameterAt (dsp::indexOf (dsp::GlobalParam::lookahead)).removeListener (this);
        cancelPendingUpdate();
    }

    //==========================================================================
    juce::RangedAudioParameter& LikyuProcessor::parameterAt (int index) const
    {
        jassert (juce::isPositiveAndBelow (index, static_cast<int> (parameters.size())));
        return *parameters[static_cast<std::size_t> (index)];
    }

    double LikyuProcessor::valueOf (int index) const
    {
        const auto& parameter = parameterAt (index);
        return static_cast<double> (parameter.convertFrom0to1 (parameter.getValue()));
    }

    void LikyuProcessor::setValue (int index, double value)
    {
        auto& parameter = parameterAt (index);
        parameter.setValueNotifyingHost (parameter.convertTo0to1 (static_cast<float> (value)));
    }

    juce::AudioProcessorEditor* LikyuProcessor::createEditor()
    {
        return new juce::GenericAudioProcessorEditor (*this);
    }

    juce::AudioProcessorParameter* LikyuProcessor::getBypassParameter() const
    {
        return &parameterAt (dsp::indexOf (dsp::GlobalParam::bypass));
    }

    //==========================================================================
    bool LikyuProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto main = layouts.getMainInputChannelSet();

        // The same layout both sides, any layout, up to 64 channels (PRD, non-goals).
        if (main != layouts.getMainOutputChannelSet() || main.isDisabled() || main.size() > maxChannels)
            return false;

        // The sidechain: absent, mono or stereo, whatever the main width.
        if (layouts.inputBuses.size() > 1)
        {
            const auto sidechain = layouts.getChannelSet (true, 1);

            if (! sidechain.isDisabled()
                && sidechain != juce::AudioChannelSet::mono()
                && sidechain != juce::AudioChannelSet::stereo())
                return false;
        }

        return true;
    }

    /*  Called often (twice per play in Samplitude, at a lookahead change in
        Live and Bitwig: Phase 1): with the same rate and width as before,
        the lookahead and the chain keep everything, so it neither costs nor
        clicks. */
    void LikyuProcessor::prepareToPlay (double sampleRate, int)
    {
        currentSampleRate = sampleRate;

        // Sized for the longest lookahead at this rate (PRD, Realtime rules).
        const auto channels = getMainBusNumInputChannels();
        const auto longest = dsp::maxLookaheadSamples (sampleRate);

        lookaheadFloat.prepare (channels, longest);
        lookaheadDouble.prepare (channels, longest);
        chain.prepare (sampleRate, channels);

        updateLatency();
    }

    void LikyuProcessor::releaseResources() {}

    void LikyuProcessor::reset()
    {
        lookaheadFloat.reset();
        lookaheadDouble.reset();
        chain.reset();
    }

    //==========================================================================
    void LikyuProcessor::parameterValueChanged (int, float)
    {
        /*  The lookahead is not automatable, so this comes from a control or a
            state load, as a rule on the message thread. Where it does not, the
            latency is reported from there all the same, a moment later. */
        if (juce::MessageManager::existsAndIsCurrentThread())
            updateLatency();
        else
            triggerAsyncUpdate();
    }

    void LikyuProcessor::handleAsyncUpdate()
    {
        updateLatency();
    }

    void LikyuProcessor::updateLatency()
    {
        const auto choice = static_cast<int> (std::lround (valueOf (dsp::indexOf (dsp::GlobalParam::lookahead))));
        const auto samples = dsp::lookaheadSamples (choice, currentSampleRate);

        delaySamples.store (samples, std::memory_order_relaxed);

        // The only call of this in the plugin (PRD, Latency). JUCE tells the host when it changes.
        setLatencySamples (samples);
    }

    //==========================================================================
    float LikyuProcessor::Reader::read() const noexcept
    {
        if (continuous != nullptr)
            return continuous->get();

        if (choice != nullptr)
            return static_cast<float> (choice->getIndex());

        if (toggle != nullptr)
            return toggle->get() ? 1.0f : 0.0f;

        return 0.0f;
    }

    void LikyuProcessor::readParameters() noexcept
    {
        for (std::size_t i = 0; i < readers.size() && i < values.size(); ++i)
            values[i] = readers[i].read();
    }

    template <typename Sample>
    void LikyuProcessor::process (juce::AudioBuffer<Sample>& buffer, dsp::Lookahead<Sample>& lookahead, bool hostBypassed)
    {
        // One realtime region for the block; the lookahead's and the chain's nest inside it.
        const dsp::rt::ScopedRealtimeCheck realtime;

        /*  The main bus is the buffer's first channels, input and output in
            place. A sidechain, when there is one, sits after them and is not
            read yet. Flush-to-zero is the chain's own (dsp/Denormals.h). */
        const auto channels = juce::jmin (getMainBusNumInputChannels(), buffer.getNumChannels());
        const auto samples = buffer.getNumSamples();
        auto* const* audio = buffer.getArrayOfWritePointers();

        readParameters();

        if (hostBypassed)
            values[static_cast<std::size_t> (dsp::indexOf (dsp::GlobalParam::bypass))] = 1.0f;

        dispatch.apply (values, chain);

        lookahead.setDelay (delaySamples.load (std::memory_order_relaxed));
        lookahead.process (audio, channels, samples);
        chain.process (audio, channels, samples);
    }

    void LikyuProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        process (buffer, lookaheadFloat, false);
    }

    void LikyuProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
    {
        process (buffer, lookaheadDouble, false);
    }

    /*  JUCE's wrappers call these only when the plugin has no bypass parameter
        of its own (checked at the pin: VST3, AU, LV2 all test
        getBypassParameter() first), so with ours a host's bypass arrives as
        the `bypass` parameter. A host that calls them anyway gets the same:
        the lookahead stays in the path (PRD, Latency) and the chain
        crossfades to the delayed dry signal. */
    void LikyuProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        process (buffer, lookaheadFloat, true);
    }

    void LikyuProcessor::processBlockBypassed (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
    {
        process (buffer, lookaheadDouble, true);
    }

    //==========================================================================
    void LikyuProcessor::getStateInformation (juce::MemoryBlock& destination)
    {
        std::vector<double> current (parameters.size());

        for (std::size_t i = 0; i < current.size(); ++i)
            current[i] = valueOf (static_cast<int> (i));

        const auto text = stateChunk.write (current);
        destination.replaceAll (text.toRawUTF8(), text.getNumBytesAsUTF8());
    }

    void LikyuProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        if (data == nullptr || sizeInBytes <= 0)
            return;

        /*  It may come before any prepareToPlay (Bitwig) or just after the
            first (Samplitude, Live): it only sets parameters, which the next
            block reads. */
        const auto text = juce::String::fromUTF8 (static_cast<const char*> (data), sizeInBytes);
        std::vector<double> loaded;

        if (! stateChunk.read (text, loaded))
            return;

        for (std::size_t i = 0; i < loaded.size() && i < parameters.size(); ++i)
            setValue (static_cast<int> (i), loaded[i]);
    }
}
