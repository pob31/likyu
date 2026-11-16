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
    THE JUCE SHIM: one AudioProcessor for VST3, AU and LV2 (PRD, Formats).

    PHASE 0 (devplan 0b): the plugin a host can already measure. It passes
    audio, and it is shaped like the product as far as a host can see:

      - the buses: main in and out with the same layout, 1 to 64 channels,
        and one optional sidechain input, mono or stereo;
      - the 284 parameters of dsp/Parameters.h, in real units. They do
        nothing yet, lookahead excepted;
      - the lookahead, which is real: a delay ahead of everything, reported
        as latency, changed only by the `lookahead` parameter;
      - a provisional state chunk (core/ProvisionalState.h).

    No editor: a host's generic parameter view is the interface until Phase 6.

    THREADS. The audio thread reads one atomic, the delay in samples. The
    reported latency is set from the message thread only: a change of the
    lookahead parameter arriving on another thread is deferred to it.
*/

#include <dsp/Lookahead.h>
#include <dsp/Parameters.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <vector>

namespace likyu
{
    class LikyuProcessor final : public juce::AudioProcessor,
                                 private juce::AudioProcessorParameter::Listener,
                                 private juce::AsyncUpdater
    {
    public:
        /** The widest main bus the plugin accepts (PRD, non-goals). */
        static constexpr int maxChannels = 64;

        /** `defaultChannels` is the width of the main bus before a host asks
            for another: 2, or 1 for the mono LV2 variant, whose ports JUCE
            writes from this default. The sidechain defaults to the same. */
        explicit LikyuProcessor (int defaultChannels = 2);
        ~LikyuProcessor() override;

        //======================================================================
        const juce::String getName() const override                 { return "Likyu"; }
        bool acceptsMidi() const override                           { return false; }
        bool producesMidi() const override                          { return false; }
        bool isMidiEffect() const override                          { return false; }
        double getTailLengthSeconds() const override                { return 0.0; }

        int getNumPrograms() override                               { return 1; }
        int getCurrentProgram() override                            { return 0; }
        void setCurrentProgram (int) override                       {}
        const juce::String getProgramName (int) override            { return {}; }
        void changeProgramName (int, const juce::String&) override  {}

        bool hasEditor() const override                             { return false; }
        juce::AudioProcessorEditor* createEditor() override         { return nullptr; }

        //======================================================================
        bool isBusesLayoutSupported (const BusesLayout&) const override;
        bool supportsDoublePrecisionProcessing() const override     { return true; }

        void prepareToPlay (double sampleRate, int maximumBlockSize) override;
        void releaseResources() override;
        void reset() override;

        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
        void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        void processBlockBypassed (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

        juce::AudioProcessorParameter* getBypassParameter() const override;

        //======================================================================
        void getStateInformation (juce::MemoryBlock&) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

        //======================================================================
        /** The host parameter built from table entry `index` (dsp/Parameters.h). */
        juce::RangedAudioParameter& parameterAt (int index) const;

        /** A parameter's value in the table's real units. */
        double valueOf (int index) const;

        /** Sets a parameter from real units, telling the host. */
        void setValue (int index, double value);

    private:
        void parameterValueChanged (int parameterIndex, float newValue) override;
        void parameterGestureChanged (int, bool) override {}
        void handleAsyncUpdate() override;

        /** Message thread: the delay for the current lookahead choice and
            sample rate, to the audio thread and to the host. */
        void updateLatency();

        template <typename Sample>
        void process (juce::AudioBuffer<Sample>&, dsp::Lookahead<Sample>&);

        std::vector<juce::RangedAudioParameter*> parameters;   // owned by the base class, table order

        dsp::Lookahead<float> lookaheadFloat;
        dsp::Lookahead<double> lookaheadDouble;
        std::atomic<int> delaySamples { 0 };
        double currentSampleRate = 0.0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LikyuProcessor)
    };
}
