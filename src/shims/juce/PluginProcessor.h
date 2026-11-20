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

    PHASE 3 (devplan): a dynamic EQ.

      - The buses: main in and out with the same layout, 1 to 64 channels,
        and one optional sidechain input, mono or stereo, which keys the
        bands set to Sidechain. A bus that is absent or disabled gives them
        Self instead; one that is enabled and silent is a key (PRD, Band
        model; Phase 1 bench).
      - The 284 parameters of dsp/Parameters.h, in real units, each with its
        own value text (core/ValueText.h). Static and Dynamic act; Transient,
        Env and Track wait for Phases 4 and 5.
      - The audio path, in chunks of at most 256 samples: the main input is
        copied as the KEY, undelayed, before the lookahead delay (the only
        latency, set only by the `lookahead` parameter); then the chain of
        eight bands (dsp/Chain.h) reads that key and the sidechain, and
        applies its output gain and its bypass crossfade.
      - The state chunk, version 1 (core/State.h), with the list of channels
        left out of the link (core/LinkList.h), which applies only to the
        layout it was made for.
      - TEMPORARY until Phase 6: one more host parameter after the 284,
        Listen (dsp/Parameters.h, temporaryListenSpec), not automatable and
        not saved. It puts one band's detector on the output.
      - The meters (core/MeterSnapshot.h), published after every block for
        the editor of Phase 6; meters() reads them.

    The editor until Phase 6 is JUCE's generic one (createEditor()).

    THREADS. Once per block the audio thread reads every parameter's value
    from its atomic and core::Dispatch hands the chain what moved; it also
    reads two more atomics, the delay in samples and the link's mask. The
    reported latency is set from the message thread only: a change of the
    lookahead parameter arriving on another thread is deferred to it. The
    link list and the layout it is compared with are kept under a lock that
    the audio thread never takes.
*/

#include <core/Dispatch.h>
#include <core/LinkList.h>
#include <core/MeterSnapshot.h>
#include <core/State.h>
#include <dsp/Chain.h>
#include <dsp/Lookahead.h>
#include <dsp/Parameters.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cstdint>
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

        /** The longest stretch processed at once: the key's copy is this long.
            A fixed size, because hosts send blocks longer than they announce. */
        static constexpr int chunkSamples = 256;

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

        /*  TEMPORARY, until the editor of Phase 6 replaces it (decided by the
            author, 2026-11-20): JUCE's generic editor, one control per
            parameter. Live lists no parameter of a plugin until something
            moves it from the plugin's side (Phase 1, finding 6), so without
            an editor a Live user could not reach the EQ at all. */
        bool hasEditor() const override                             { return true; }
        juce::AudioProcessorEditor* createEditor() override;

        //======================================================================
        bool isBusesLayoutSupported (const BusesLayout&) const override;
        bool supportsDoublePrecisionProcessing() const override     { return true; }

        void prepareToPlay (double sampleRate, int maximumBlockSize) override;
        void releaseResources() override;
        void reset() override;
        void processorLayoutsChanged() override;

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

        //======================================================================
        /** The channels left out of the link, and the layout they were left
            out of (core/LinkList.h). Not for the audio thread. */
        core::LinkList getLinkList() const;

        /** For the editor (Phase 6) and the tests. Takes effect at the next block. */
        void setLinkList (const core::LinkList& list);

        /** The main bus's layout as a link list names it: its speaker arrangement. */
        juce::String currentLayoutName() const;

        /** The temporary Listen parameter, after the 284 (removed in Phase 6). */
        juce::AudioParameterChoice& temporaryListen() const noexcept    { return *listenParameter; }

        /** The meters of the last block. Its one reader: the editor, or a test. */
        core::MeterSnapshot& meters() noexcept                          { return meterSnapshot; }

    private:
        void parameterValueChanged (int parameterIndex, float newValue) override;
        void parameterGestureChanged (int, bool) override {}
        void handleAsyncUpdate() override;

        /** Message thread: the delay for the current lookahead choice and
            sample rate, to the audio thread and to the host. */
        void updateLatency();

        /** The main bus's layout now, and the link's mask for it. */
        void updateLayout();
        void updateLinkMask();

        /** `hostBypassed`: called through processBlockBypassed, which a host
            only does when it owns the bypass; the chain then crossfades out
            as for the bypass parameter. */
        template <typename Sample>
        void process (juce::AudioBuffer<Sample>&, dsp::Lookahead<Sample>&, std::vector<Sample>& keyStore, bool hostBypassed);

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
        juce::AudioParameterChoice* listenParameter = nullptr; // owned by the base class, after them

        core::MeterSnapshot meterSnapshot;
        std::uint64_t blocksPublished = 0;

        core::Dispatch dispatch;
        core::Dispatch::Values values {};
        dsp::Chain chain;

        dsp::Lookahead<float> lookaheadFloat;
        dsp::Lookahead<double> lookaheadDouble;
        std::atomic<int> delaySamples { 0 };
        double currentSampleRate = 0.0;

        // The undelayed main input, chunkSamples per channel, for the detectors.
        std::vector<float> keyFloat;
        std::vector<double> keyDouble;
        int keyChannels = 0;

        // The link list, and the layout it is compared with.
        mutable juce::CriticalSection linkLock;
        core::LinkList linkList;
        juce::String layoutName;
        int layoutChannels = 0;
        std::atomic<std::uint64_t> linkMask { 0 };

        core::StateChunk stateChunk;   // holds the fields kept from the last chunk loaded

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LikyuProcessor)
    };
}
