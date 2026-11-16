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

#include <core/ProvisionalState.h>

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

        juce::NormalisableRange<float> rangeFor (const dsp::ParamSpec& spec)
        {
            const auto minimum = static_cast<float> (spec.minimum);
            const auto maximum = static_cast<float> (spec.maximum);

            if (spec.skew == dsp::ParamSkew::linear)
                return { minimum, maximum };

            // Equal steps of the host's 0 to 1 are equal ratios of the value.
            return { minimum, maximum,
                     [] (float lo, float hi, float proportion) { return lo * std::pow (hi / lo, proportion); },
                     [] (float lo, float hi, float value)      { return std::log (value / lo) / std::log (hi / lo); },
                     [] (float lo, float hi, float value)      { return juce::jlimit (lo, hi, value); } };
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

            return std::make_unique<juce::AudioParameterFloat> (
                id, name, rangeFor (spec), static_cast<float> (spec.defaultValue),
                juce::AudioParameterFloatAttributes().withLabel (juce::String (spec.unit))
                                                     .withAutomatable (spec.automatable));
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

        for (const auto& spec : table)
        {
            auto parameter = makeParameter (spec);
            parameters.push_back (parameter.get());
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

    void LikyuProcessor::prepareToPlay (double sampleRate, int)
    {
        currentSampleRate = sampleRate;

        // Sized for the longest lookahead at this rate (PRD, Realtime rules).
        const auto channels = getMainBusNumInputChannels();
        const auto longest = dsp::maxLookaheadSamples (sampleRate);

        lookaheadFloat.prepare (channels, longest);
        lookaheadDouble.prepare (channels, longest);

        updateLatency();
    }

    void LikyuProcessor::releaseResources() {}

    void LikyuProcessor::reset()
    {
        lookaheadFloat.reset();
        lookaheadDouble.reset();
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
    template <typename Sample>
    void LikyuProcessor::process (juce::AudioBuffer<Sample>& buffer, dsp::Lookahead<Sample>& lookahead)
    {
        const juce::ScopedNoDenormals noDenormals;

        /*  The main bus is the buffer's first channels, input and output in
            place. A sidechain, when there is one, sits after them and is not
            read yet. */
        const auto channels = juce::jmin (getMainBusNumInputChannels(), buffer.getNumChannels());

        lookahead.setDelay (delaySamples.load (std::memory_order_relaxed));
        lookahead.process (buffer.getArrayOfWritePointers(), channels, buffer.getNumSamples());
    }

    void LikyuProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        process (buffer, lookaheadFloat);
    }

    void LikyuProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
    {
        process (buffer, lookaheadDouble);
    }

    /*  Bypass keeps the lookahead delay in the path (PRD, Latency): bypassing
        changes neither the latency nor the timing. In Phase 0 that is all the
        plugin does anyway. */
    void LikyuProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        process (buffer, lookaheadFloat);
    }

    void LikyuProcessor::processBlockBypassed (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
    {
        process (buffer, lookaheadDouble);
    }

    //==========================================================================
    void LikyuProcessor::getStateInformation (juce::MemoryBlock& destination)
    {
        std::vector<double> values (parameters.size());

        for (std::size_t i = 0; i < values.size(); ++i)
            values[i] = valueOf (static_cast<int> (i));

        const auto text = core::writeProvisionalState (values);
        destination.replaceAll (text.toRawUTF8(), text.getNumBytesAsUTF8());
    }

    void LikyuProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        if (data == nullptr || sizeInBytes <= 0)
            return;

        const auto text = juce::String::fromUTF8 (static_cast<const char*> (data), sizeInBytes);
        auto values = core::defaultParameterValues();

        if (! core::readProvisionalState (text, values))
            return;

        for (std::size_t i = 0; i < values.size() && i < parameters.size(); ++i)
            setValue (static_cast<int> (i), values[i]);
    }
}
