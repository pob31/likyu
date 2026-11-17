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
    THE HOST PROBE (devplan, Phase 1). Throwaway.

    The real LikyuProcessor, unchanged, with every call a host makes to it
    watched: the probe derives from it, calls it, and writes down what came
    in. So a host is measured against the product's own buses, parameters,
    latency and state, under another name and another plugin code.

    THREADS. The process calls touch atomics and nothing else: the lipogram
    holds here as in the product. Calls that may arrive on the audio thread
    in some host (reset, getStateInformation) only count. Everything else
    writes to a log behind a lock the process calls never take. A timer on
    the message thread turns changes of the counters into log lines.
*/

#include "PluginProcessor.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <vector>

namespace likyu::probe
{
    /** What the process calls saw. Written by the audio thread only. */
    struct AudioCounters
    {
        std::atomic<std::uint64_t> blocks { 0 };
        std::atomic<std::uint64_t> zeroLengthBlocks { 0 };
        std::atomic<std::uint64_t> floatBlocks { 0 };
        std::atomic<std::uint64_t> doubleBlocks { 0 };
        std::atomic<std::uint64_t> bypassedBlocks { 0 };
        std::atomic<std::uint64_t> shortBufferBlocks { 0 };   // fewer channels than the enabled buses add up to

        std::atomic<int> minBlock { std::numeric_limits<int>::max() };
        std::atomic<int> maxBlock { 0 };
        std::atomic<int> lastBlock { 0 };

        std::atomic<int> bufferChannels { 0 };
        std::atomic<int> expectedChannels { 0 };
        std::atomic<int> sidechainChannels { 0 };       // of the bus, as the processor reports it
        std::atomic<int> sidechainInBuffer { 0 };       // of those, how many the buffer really holds
        std::atomic<bool> sidechainEnabled { false };
        std::atomic<bool> sidechainAliasesMain { false };
        std::atomic<bool> playing { false };
        std::atomic<bool> playHeadPresent { false };

        // Peaks since the timer last took them, and since the plugin was made.
        std::atomic<float> mainPeak { 0.0f };
        std::array<std::atomic<float>, 2> sidechainPeak { 0.0f, 0.0f };
        std::atomic<float> mainPeakEver { 0.0f };
        std::array<std::atomic<float>, 2> sidechainPeakEver { 0.0f, 0.0f };
    };

    /** How the sidechain looks from inside the plugin (devplan, Phase 1, second row). */
    enum class SidechainView
    {
        noAudioYet,
        busDisabled,
        bufferShort,      // bus enabled, its channels missing from the buffer
        exactZeros,       // bus enabled, every sample exactly 0
        signal            // bus enabled, something other than 0
    };

    /** The classification, as a pure function so that it reads the same everywhere. */
    SidechainView classifySidechain (bool anyBlock, bool enabled, int busChannels, int channelsInBuffer,
                                     bool signalRecently) noexcept;

    juce::String describe (SidechainView);

    /** Linear peak to text: "-12.3 dB", or "exact zero". */
    juce::String peakText (float peak);

    //==========================================================================
    class ProbeProcessor final : public LikyuProcessor,
                                 private juce::Timer
    {
    public:
        explicit ProbeProcessor (int defaultChannels);
        ~ProbeProcessor() override;

        const juce::String getName() const override             { return "Likyu Probe"; }
        bool hasEditor() const override                         { return true; }
        juce::AudioProcessorEditor* createEditor() override;

        //======================================================================
        bool isBusesLayoutSupported (const BusesLayout&) const override;
        void numChannelsChanged() override;
        void numBusesChanged() override;
        void processorLayoutsChanged() override;

        void prepareToPlay (double sampleRate, int maximumBlockSize) override;
        void releaseResources() override;
        void reset() override;

        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
        void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        void processBlockBypassed (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

        void getStateInformation (juce::MemoryBlock&) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

        //======================================================================
        // For the editor. Message thread.

        /** The lines of the status pane, refreshed by the timer. */
        juce::StringArray statusLines() const;

        /** Everything, as Markdown, to paste into docs/spikes/phase1-hosts.md. */
        juce::String report() const;

        /** A name for a saved report: host, format, time. */
        juce::String reportFileName() const;

        juce::StringArray logLines() const;
        int logRevision() const noexcept                        { return logCounter.load (std::memory_order_relaxed); }
        void clearLog();

        /** A code over the 284 parameter values: equal before a save and after a reload. */
        juce::String fingerprint() const;
        int parametersOffDefault() const;

        /** Sets every parameter but bypass and lookahead to a fixed pattern away from the defaults. */
        void scramble();
        void restoreDefaults();

        int lookaheadChoice() const;
        void setLookaheadChoice (int choice);

        /** Told by the touch pad. */
        void noteTouches (int simultaneous, juce::MouseInputSource::InputSourceType type);

    private:
        void timerCallback() override;

        template <typename Sample>
        void observe (const juce::AudioBuffer<Sample>&, bool bypassed) noexcept;

        void log (const juce::String&) const;
        juce::String layoutText() const;
        static juce::String layoutText (const BusesLayout&);
        juce::String hostText() const;
        SidechainView sidechainView() const noexcept;

        AudioCounters audio;

        std::atomic<int> prepareCalls { 0 };
        std::atomic<int> releaseCalls { 0 };
        std::atomic<int> resetCalls { 0 };
        std::atomic<int> getStateCalls { 0 };
        std::atomic<int> setStateCalls { 0 };
        std::atomic<double> preparedRate { 0.0 };
        std::atomic<int> preparedBlock { 0 };

        // The log. `mutable` because isBusesLayoutSupported is const.
        mutable juce::CriticalSection logLock;
        mutable juce::StringArray logEntries;
        mutable juce::StringArray layoutsAsked;
        mutable std::atomic<int> logCounter { 0 };

        // What the timer last saw, to log the changes. Message thread.
        int seenLatency = -1;
        int prepareCallsAtLatencyChange = 0;
        int latencyChanges = 0;
        int seenBufferChannels = -1;
        int seenResetCalls = 0;
        int seenGetStateCalls = 0;
        int seenPlaying = -1;
        std::uint64_t seenBypassedBlocks = 0;
        bool bypassedRunning = false;
        mutable int layoutQuestions = 0;                // guarded by logLock
        SidechainView seenSidechain = SidechainView::noAudioYet;
        int ticksSinceSidechainSignal = 1000;
        std::array<float, 2> shownSidechainPeak { 0.0f, 0.0f };
        float shownMainPeak = 0.0f;

        // Touches. Message thread.
        int maxTouches = 0;
        bool sawMouse = false, sawTouch = false, sawPen = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProbeProcessor)
    };
}
