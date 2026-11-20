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

    PHASE 4 (devplan): a dynamic EQ and transient designer.

      - The buses: main in and out with the same layout, 1 to 64 channels,
        and one optional sidechain input, mono or stereo, which keys the
        bands set to Sidechain. A bus that is absent or disabled gives them
        Self instead; one that is enabled and silent is a key (PRD, Band
        model; Phase 1 bench).
      - The 284 parameters of dsp/Parameters.h, in real units, each with its
        own value text (core/ValueText.h).
      - The audio path, the host's block in place: the chain (dsp/Chain.h)
        reads the main input as the KEY, undelayed, and the sidechain, then
        delays the audio by the lookahead (the only latency, set only by the
        `lookahead` parameter) and runs its eight bands, its output gain and
        its bypass crossfade.
      - The state chunk, version 1 (core/State.h), with the channels each
        band leaves out (core/BandChannels.h), which apply only to the
        layout they were made for.
      - Listen (PRD, Detection): one band's detector on the output while
        the editor holds its button. An action of the editor, not a
        parameter: setListen(), not saved.
      - TEMPORARY until the Phase 4 listening pass has set them: four more,
        the transient shaper's time constants (temporaryTransientSpecs), not
        automatable and not saved, read every block for every band.
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
#include <core/MeterSnapshot.h>
#include <core/State.h>
#include <dsp/Chain.h>
#include <dsp/Parameters.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
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

        /*  The editor (EditorWindow.h, around editor/EditorRoot.h). JUCE's
            generic editor, the stand-in since Phase 2, is gone with it. The
            temporary parameters below (Listen, the transient constants) are
            not in the editor yet: until they are, a host's own parameter view
            is the way to them. */
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

        /** Around the setValue calls of one drag, for the editor: a host
            records one move, and Live lists the parameter. */
        void beginGesture (int index);
        void endGesture (int index);

        //======================================================================
        /** The main bus's layout as the band channels name it: its speaker arrangement. */
        juce::String currentLayoutName() const;

        //======================================================================
        /** The channels each band leaves out, and the layout they were left
            out of (core/BandChannels.h). Not for the audio thread. */
        core::BandChannels getBandChannels() const;

        /** For the state and the tests. Takes effect at the next block. */
        void setBandChannels (const core::BandChannels& lists);

        /** For the editor: the channels `band` (from 0) leaves out in the
            layout of the moment, bit c for channel c; and setting them,
            which makes the lists this layout's. */
        std::uint64_t bandChannelsLeftOut (int band) const;
        void setBandChannelsLeftOut (int band, std::uint64_t leftOut);

        /** The main bus's channels as the host names them ("L", "Lfe"; "1"
            for a channel it does not name), for the editor's cells. */
        juce::StringArray channelNames() const;

        /** Listen: the band (from 0) whose detector replaces the output, or
            -1 for none. The editor's, while its button is held; not a
            parameter and not saved. Takes effect at the next block, through
            the chain's crossfade. */
        void setListen (int band) noexcept              { listenBand.store (band, std::memory_order_relaxed); }
        int listen() const noexcept                     { return listenBand.load (std::memory_order_relaxed); }

        /** The temporary transient constants, after the table, in the order of
            dsp::temporaryTransientSpecs() (removed once the PRD has its values). */
        juce::AudioParameterFloat& temporaryTransientTime (int index) const
        {
            return *transientTimeParameters[static_cast<std::size_t> (index)];
        }

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
        void updateBandMasks();

        /** `hostBypassed`: called through processBlockBypassed, which a host
            only does when it owns the bypass; the chain then crossfades out
            as for the bypass parameter. */
        template <typename Sample>
        void process (juce::AudioBuffer<Sample>&, bool hostBypassed);

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
        std::atomic<int> listenBand { -1 };

        // Owned by the base class, after the table. TEMPORARY: the listening pass's.
        std::array<juce::AudioParameterFloat*, dsp::numTemporaryTransientTimes> transientTimeParameters {};

        core::MeterSnapshot meterSnapshot;
        std::uint64_t blocksPublished = 0;

        core::Dispatch dispatch;
        core::Dispatch::Values values {};
        dsp::Chain chain;   // the lookahead's delay line included

        std::atomic<int> delaySamples { 0 };
        double currentSampleRate = 0.0;

        // The layout of the moment, and the channels each band leaves out: the lists
        // under the lock, their masks for the audio thread.
        mutable juce::CriticalSection layoutLock;
        juce::String layoutName;
        int layoutChannels = 0;
        core::BandChannels bandChannels;
        std::array<std::atomic<std::uint64_t>, dsp::numBands> bandMasks {};

        core::StateChunk stateChunk;   // holds the fields kept from the last chunk loaded

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LikyuProcessor)
    };
}
