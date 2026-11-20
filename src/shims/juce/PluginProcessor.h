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

    PHASE 2 (devplan): a static EQ.

      - The buses: main in and out with the same layout, 1 to 64 channels,
        and one optional sidechain input, mono or stereo, not read yet.
      - The 284 parameters of dsp/Parameters.h, in real units, each with its
        own value text (core/ValueText.h). The bands' type, frequency, Q,
        gain and Q mode, the bypass and the output gain act; the others wait
        for Phases 3 to 5.
      - The audio path: the lookahead delay (the only latency, set only by
        the `lookahead` parameter), then the chain of eight bands
        (dsp/Chain.h), its output gain and its bypass crossfade.
      - The state chunk, version 1 (core/State.h).

    No editor: a host's generic parameter view is the interface until Phase 6.

    THREADS. Once per block the audio thread reads every parameter's value
    from its atomic and core::Dispatch hands the chain what moved; it also
    reads one more atomic, the delay in samples. The reported latency is set
    from the message thread only: a change of the lookahead parameter
    arriving on another thread is deferred to it.
*/

#include <core/Dispatch.h>
#include <core/State.h>
#include <dsp/Chain.h>
#include <dsp/Lookahead.h>
#include <dsp/Parameters.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <vector>

namespace likyu
{
    /*  Not `final` while the Phase 1 host probe exists: it derives from this
        class to watch what a host does to the real processor
        (spikes/hostprobe/, which is not part of this build). `final` comes
        back when that spike is deleted. */
    class LikyuProcessor : public juce::AudioProcessor,
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

        /** `hostBypassed`: called through processBlockBypassed, which a host
            only does when it owns the bypass; the chain then crossfades out
            as for the bypass parameter. */
        template <typename Sample>
        void process (juce::AudioBuffer<Sample>&, dsp::Lookahead<Sample>&, bool hostBypassed);

        /** Audio thread: every parameter's real value, from its atomic. */
        void readParameters() noexcept;

        /** How to read one parameter's real value without converting it. */
        struct Reader
        {
            juce::AudioParameterFloat* continuous = nullptr;
            juce::AudioParameterChoice* choice = nullptr;
            juce::AudioParameterBool* toggle = nullptr;

            float read() const noexcept;
        };

        std::vector<juce::RangedAudioParameter*> parameters;   // owned by the base class, table order
        std::vector<Reader> readers;                           // the same, by kind

        core::Dispatch dispatch;
        core::Dispatch::Values values {};
        dsp::Chain chain;

        dsp::Lookahead<float> lookaheadFloat;
        dsp::Lookahead<double> lookaheadDouble;
        std::atomic<int> delaySamples { 0 };
        double currentSampleRate = 0.0;

        core::StateChunk stateChunk;   // holds the fields kept from the last chunk loaded

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LikyuProcessor)
    };
}
