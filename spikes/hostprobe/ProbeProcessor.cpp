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

#include "ProbeProcessor.h"

#include "ProbeEditor.h"

#include <cmath>
#include <type_traits>

namespace likyu::probe
{
    namespace
    {
        constexpr auto relaxed = std::memory_order_relaxed;

        constexpr int timerHz = 10;
        constexpr int sidechainHoldTicks = timerHz;     // a second without signal before "exact zeros"
        constexpr int maxLogEntries = 500;
        constexpr int logEntriesKept = 30;
        constexpr int maxLayoutQuestionsLogged = 40;

        void raise (std::atomic<float>& held, float value) noexcept
        {
            if (value > held.load (relaxed))
                held.store (value, relaxed);
        }

        juce::String setText (const juce::AudioChannelSet& set)
        {
            if (set.isDisabled())
                return "disabled";

            // A 64-channel layout's speaker list fills the log; its name and size say enough.
            if (set.size() > 8)
                return set.getDescription() + " (" + juce::String (set.size()) + " channels)";

            return set.getDescription() + " (" + set.getSpeakerArrangementAsString() + ")";
        }

        juce::String lfeText (const juce::AudioChannelSet& set)
        {
            juce::StringArray found;

            for (int i = 0; i < set.size(); ++i)
            {
                const auto type = set.getTypeOfChannel (i);

                if (type == juce::AudioChannelSet::LFE || type == juce::AudioChannelSet::LFE2)
                    found.add ("channel " + juce::String (i + 1));
            }

            return found.isEmpty() ? juce::String ("none reported") : found.joinIntoString (", ");
        }

        /*  A parameter's value as it survives a save: through real units and
            back. A toggle set to 0.58 by a host holds 0.58 until then, and
            is "on" all the same. */
        float settled (const juce::RangedAudioParameter& parameter)
        {
            return parameter.convertTo0to1 (parameter.convertFrom0to1 (parameter.getValue()));
        }

        juce::String operatingSystem()
        {
            return juce::SystemStats::getOperatingSystemName();
        }
    }

    //==========================================================================
    SidechainView classifySidechain (bool anyBlock, bool enabled, int busChannels, int channelsInBuffer,
                                     bool signalRecently) noexcept
    {
        if (! anyBlock)
            return SidechainView::noAudioYet;

        if (! enabled || busChannels == 0)
            return SidechainView::busDisabled;

        if (channelsInBuffer < busChannels)
            return SidechainView::bufferShort;

        return signalRecently ? SidechainView::signal : SidechainView::exactZeros;
    }

    juce::String describe (SidechainView view)
    {
        switch (view)
        {
            case SidechainView::noAudioYet:     return "no audio processed yet";
            case SidechainView::busDisabled:    return "bus disabled";
            case SidechainView::bufferShort:    return "bus enabled, its channels missing from the buffer";
            case SidechainView::exactZeros:     return "bus enabled, exact zeros";
            case SidechainView::signal:         return "bus enabled, signal";
        }

        return {};
    }

    juce::String peakText (float peak)
    {
        if (peak <= 0.0f)
            return "exact zero";

        return juce::String (20.0 * std::log10 (static_cast<double> (peak)), 1) + " dB";
    }

    //==========================================================================
    ProbeProcessor::ProbeProcessor (int defaultChannels)
        : LikyuProcessor (defaultChannels)
    {
        log ("constructed as " + juce::String (getWrapperTypeDescription (wrapperType)) + " in " + hostText()
             + ", " + operatingSystem());
        log ("default layout: " + layoutText());
        startTimerHz (timerHz);
    }

    ProbeProcessor::~ProbeProcessor()
    {
        stopTimer();

        /*  With LIKYU_PROBE_DUMP naming a folder, every instance leaves its
            report there when it goes: for a host run from a script, where
            nobody presses Save report. */
        const auto folder = juce::SystemStats::getEnvironmentVariable ("LIKYU_PROBE_DUMP", {});

        if (folder.isNotEmpty() && juce::File::isAbsolutePath (folder))
        {
            timerCallback();    // the last changes, into the log
            juce::File (folder).getChildFile (reportFileName()).replaceWithText (report());
        }
    }

    juce::AudioProcessorEditor* ProbeProcessor::createEditor()
    {
        return new ProbeEditor (*this);
    }

    //==========================================================================
    bool ProbeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
       #if LIKYU_PROBE_FIXED_MAIN
        // The control variant: the product's rules, and a main bus that is stereo or nothing.
        const auto supported = LikyuProcessor::isBusesLayoutSupported (layouts)
                               && layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
       #else
        const auto supported = LikyuProcessor::isBusesLayoutSupported (layouts);
       #endif
        const auto text = layoutText (layouts) + (supported ? "  -> yes" : "  -> no");

        {
            const juce::ScopedLock lock (logLock);

            if (layoutsAsked.contains (text))
                return supported;

            layoutsAsked.add (text);

            if (++layoutQuestions > maxLayoutQuestionsLogged)
                return supported;
        }

        log ("host asks: " + text);
        return supported;
    }

    void ProbeProcessor::numChannelsChanged()
    {
        LikyuProcessor::numChannelsChanged();
        log ("numChannelsChanged: " + layoutText());
    }

    void ProbeProcessor::numBusesChanged()
    {
        LikyuProcessor::numBusesChanged();
        log ("numBusesChanged: " + layoutText());
    }

    void ProbeProcessor::processorLayoutsChanged()
    {
        LikyuProcessor::processorLayoutsChanged();
        log ("processorLayoutsChanged: " + layoutText());
    }

    void ProbeProcessor::prepareToPlay (double sampleRate, int maximumBlockSize)
    {
        LikyuProcessor::prepareToPlay (sampleRate, maximumBlockSize);

        preparedRate.store (sampleRate, relaxed);
        preparedBlock.store (maximumBlockSize, relaxed);
        const auto calls = prepareCalls.fetch_add (1, relaxed) + 1;

        log ("prepareToPlay #" + juce::String (calls) + ": " + juce::String (sampleRate, 0) + " Hz, block "
             + juce::String (maximumBlockSize) + ", " + (isUsingDoublePrecision() ? "double" : "float")
             + ", " + (isNonRealtime() ? "offline" : "realtime")
             + ", latency " + juce::String (getLatencySamples()) + "; " + layoutText());
    }

    void ProbeProcessor::releaseResources()
    {
        LikyuProcessor::releaseResources();
        const auto calls = releaseCalls.fetch_add (1, relaxed) + 1;
        log ("releaseResources #" + juce::String (calls));
    }

    void ProbeProcessor::reset()
    {
        // Some hosts call this from the audio thread: count, and let the timer write.
        LikyuProcessor::reset();
        resetCalls.fetch_add (1, relaxed);
    }

    //==========================================================================
    template <typename Sample>
    void ProbeProcessor::observe (const juce::AudioBuffer<Sample>& buffer, bool bypassed) noexcept
    {
        const auto numSamples = buffer.getNumSamples();
        const auto numChannels = buffer.getNumChannels();

        audio.blocks.fetch_add (1, relaxed);

        if (bypassed)
            audio.bypassedBlocks.fetch_add (1, relaxed);

        if constexpr (std::is_same_v<Sample, float>)
            audio.floatBlocks.fetch_add (1, relaxed);
        else
            audio.doubleBlocks.fetch_add (1, relaxed);

        audio.lastBlock.store (numSamples, relaxed);

        if (numSamples <= 0)
            audio.zeroLengthBlocks.fetch_add (1, relaxed);

        if (numSamples < audio.minBlock.load (relaxed))
            audio.minBlock.store (numSamples, relaxed);

        if (numSamples > audio.maxBlock.load (relaxed))
            audio.maxBlock.store (numSamples, relaxed);

        const auto expected = juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels());
        audio.bufferChannels.store (numChannels, relaxed);
        audio.expectedChannels.store (expected, relaxed);

        if (numChannels < expected)
            audio.shortBufferBlocks.fetch_add (1, relaxed);

        // The main input, before the processor delays it in place.
        const auto mainChannels = juce::jmin (getMainBusNumInputChannels(), numChannels);

        if (numSamples > 0)
        {
            auto peak = 0.0f;

            for (int channel = 0; channel < mainChannels; ++channel)
                peak = juce::jmax (peak, static_cast<float> (buffer.getMagnitude (channel, 0, numSamples)));

            raise (audio.mainPeak, peak);
            raise (audio.mainPeakEver, peak);
        }

        // The sidechain: is the bus on, are its channels in the buffer, and what is in them.
        const auto* sidechain = getBusCount (true) > 1 ? getBus (true, 1) : nullptr;
        const auto enabled = sidechain != nullptr && sidechain->isEnabled();
        const auto busChannels = enabled ? sidechain->getNumberOfChannels() : 0;
        auto inBuffer = 0;
        auto aliases = false;

        if (busChannels > 0)
        {
            const auto offset = sidechain->getChannelIndexInProcessBlockBuffer (0);
            inBuffer = juce::jlimit (0, busChannels, numChannels - offset);

            for (int i = 0; i < juce::jmin (inBuffer, 2) && numSamples > 0; ++i)
            {
                const auto peak = static_cast<float> (buffer.getMagnitude (offset + i, 0, numSamples));
                raise (audio.sidechainPeak[static_cast<std::size_t> (i)], peak);
                raise (audio.sidechainPeakEver[static_cast<std::size_t> (i)], peak);

                for (int channel = 0; channel < mainChannels; ++channel)
                    aliases = aliases || buffer.getReadPointer (offset + i) == buffer.getReadPointer (channel);
            }
        }

        audio.sidechainEnabled.store (enabled, relaxed);
        audio.sidechainChannels.store (busChannels, relaxed);
        audio.sidechainInBuffer.store (inBuffer, relaxed);

        if (aliases)
            audio.sidechainAliasesMain.store (true, relaxed);

        if (auto* head = getPlayHead())
        {
            audio.playHeadPresent.store (true, relaxed);

            if (const auto position = head->getPosition())
                audio.playing.store (position->getIsPlaying(), relaxed);
        }
    }

    void ProbeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        observe (buffer, false);
        LikyuProcessor::processBlock (buffer, midi);
    }

    void ProbeProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi)
    {
        observe (buffer, false);
        LikyuProcessor::processBlock (buffer, midi);
    }

    void ProbeProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        observe (buffer, true);
        LikyuProcessor::processBlockBypassed (buffer, midi);
    }

    void ProbeProcessor::processBlockBypassed (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi)
    {
        observe (buffer, true);
        LikyuProcessor::processBlockBypassed (buffer, midi);
    }

    //==========================================================================
    void ProbeProcessor::getStateInformation (juce::MemoryBlock& destination)
    {
        // Some hosts ask many times a second: count, and let the timer write.
        LikyuProcessor::getStateInformation (destination);
        getStateCalls.fetch_add (1, relaxed);
    }

    void ProbeProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        LikyuProcessor::setStateInformation (data, sizeInBytes);
        const auto calls = setStateCalls.fetch_add (1, relaxed) + 1;

        log ("setStateInformation #" + juce::String (calls) + ": " + juce::String (sizeInBytes)
             + " bytes; fingerprint now " + fingerprint() + ", "
             + juce::String (parametersOffDefault()) + " parameters off default"
             + (juce::MessageManager::existsAndIsCurrentThread() ? "" : " (not on the message thread)"));
    }

    //==========================================================================
    SidechainView ProbeProcessor::sidechainView() const noexcept
    {
        return classifySidechain (audio.blocks.load (relaxed) > 0,
                                  audio.sidechainEnabled.load (relaxed),
                                  audio.sidechainChannels.load (relaxed),
                                  audio.sidechainInBuffer.load (relaxed),
                                  ticksSinceSidechainSignal < sidechainHoldTicks);
    }

    void ProbeProcessor::timerCallback()
    {
        shownMainPeak = audio.mainPeak.exchange (0.0f, relaxed);

        auto sidechainSignal = false;

        for (std::size_t i = 0; i < shownSidechainPeak.size(); ++i)
        {
            shownSidechainPeak[i] = audio.sidechainPeak[i].exchange (0.0f, relaxed);
            sidechainSignal = sidechainSignal || shownSidechainPeak[i] > 0.0f;
        }


        /*  Is the host calling process at all? A host that stops calling when
            its transport stops leaves the last play head answer standing. */
        const auto blocksNow = audio.blocks.load (relaxed);
        ticksSinceBlock = blocksNow != seenBlocks ? 0 : juce::jmin (ticksSinceBlock + 1, 1000);
        seenBlocks = blocksNow;

        // Only while blocks arrive: a host that stops calling has not sent zeros.
        if (ticksSinceBlock == 0)
            ticksSinceSidechainSignal = sidechainSignal ? 0 : juce::jmin (ticksSinceSidechainSignal + 1, 1000);

        if (const auto running = ticksSinceBlock < 3; running != processing)
        {
            processing = running;
            log (running ? "process calls: started" : "process calls: stopped");
        }

        if (const auto latency = getLatencySamples(); latency != seenLatency)
        {
            if (seenLatency >= 0)
            {
                ++latencyChanges;
                prepareCallsAtLatencyChange = prepareCalls.load (relaxed);
                log ("latency reported to the host: " + juce::String (seenLatency) + " -> " + juce::String (latency)
                     + " samples, " + (! processing                  ? "no process calls at the time"
                                       : audio.playing.load (relaxed) ? "transport playing"
                                                                      : "transport not playing"));
            }

            seenLatency = latency;
        }

        if (const auto channels = audio.bufferChannels.load (relaxed);
            channels != seenBufferChannels && audio.blocks.load (relaxed) > 0)
        {
            seenBufferChannels = channels;
            log ("process buffer: " + juce::String (channels) + " channels; the enabled buses add up to "
                 + juce::String (audio.expectedChannels.load (relaxed)));
        }

        if (const auto view = sidechainView(); view != seenSidechain)
        {
            seenSidechain = view;
            log ("sidechain seen as: " + describe (view));
        }

        if (const auto calls = resetCalls.load (relaxed); calls != seenResetCalls)
        {
            log ("reset x" + juce::String (calls - seenResetCalls) + " (total " + juce::String (calls) + ")");
            seenResetCalls = calls;
        }

        if (const auto calls = getStateCalls.load (relaxed); calls != seenGetStateCalls)
        {
            log ("getStateInformation x" + juce::String (calls - seenGetStateCalls) + " (total " + juce::String (calls)
                 + "); fingerprint " + fingerprint());
            seenGetStateCalls = calls;
        }

        if (audio.playHeadPresent.load (relaxed))
        {
            if (const auto playing = audio.playing.load (relaxed) ? 1 : 0; playing != seenPlaying)
            {
                seenPlaying = playing;
                log (playing == 1 ? "transport: playing" : "transport: stopped");
            }
        }

        const auto bypassed = audio.bypassedBlocks.load (relaxed);
        const auto running = bypassed != seenBypassedBlocks;
        seenBypassedBlocks = bypassed;

        if (running != bypassedRunning)
        {
            bypassedRunning = running;
            log (running ? "the host now calls processBlockBypassed" : "the host no longer calls processBlockBypassed");
        }
    }

    //==========================================================================
    void ProbeProcessor::log (const juce::String& text) const
    {
        const auto now = juce::Time::getCurrentTime();
        const auto line = now.formatted ("%H:%M:%S") + "."
                          + juce::String (now.getMilliseconds()).paddedLeft ('0', 3) + "  " + text;

        const juce::ScopedLock lock (logLock);
        logEntries.add (line);

        // The oldest go, except the first lines, which say how the host set the plugin up.
        if (logEntries.size() > maxLogEntries)
            logEntries.removeRange (logEntriesKept, logEntries.size() - maxLogEntries);

        logCounter.fetch_add (1, relaxed);
    }

    juce::StringArray ProbeProcessor::logLines() const
    {
        const juce::ScopedLock lock (logLock);
        return logEntries;
    }

    void ProbeProcessor::clearLog()
    {
        const juce::ScopedLock lock (logLock);
        logEntries.clear();
        logCounter.fetch_add (1, relaxed);
    }

    juce::String ProbeProcessor::layoutText() const
    {
        return layoutText (getBusesLayout());
    }

    juce::String ProbeProcessor::layoutText (const BusesLayout& layouts)
    {
        juce::StringArray inputs, outputs;

        for (const auto& set : layouts.inputBuses)
            inputs.add (setText (set));

        for (const auto& set : layouts.outputBuses)
            outputs.add (setText (set));

        return "in [" + inputs.joinIntoString (" | ") + "] out [" + outputs.joinIntoString (" | ") + "]";
    }

    juce::String ProbeProcessor::hostText() const
    {
        const juce::PluginHostType host;
        return juce::String (host.getHostDescription()) + " ("
               + juce::File::getSpecialLocation (juce::File::hostApplicationPath).getFileName() + ")";
    }

    //==========================================================================
    juce::String ProbeProcessor::fingerprint() const
    {
        // FNV-1a over the values in ten-thousandths of the host's 0 to 1.
        std::uint32_t hash = 2166136261u;

        for (int i = 0; i < dsp::numParams; ++i)
        {
            auto step = static_cast<std::uint32_t> (std::lround (settled (parameterAt (i)) * 10000.0f));

            for (int byte = 0; byte < 4; ++byte)
            {
                hash = (hash ^ (step & 0xffu)) * 16777619u;
                step >>= 8;
            }
        }

        return juce::String::toHexString (static_cast<int> (hash)).paddedLeft ('0', 8).toUpperCase();
    }

    int ProbeProcessor::parametersOffDefault() const
    {
        auto count = 0;

        for (int i = 0; i < dsp::numParams; ++i)
            if (std::abs (settled (parameterAt (i)) - parameterAt (i).getDefaultValue()) > 1.0e-6f)
                ++count;

        return count;
    }

    void ProbeProcessor::scramble()
    {
        for (int i = 0; i < dsp::numParams; ++i)
        {
            // Bypass would silence the bench and lookahead has its own row.
            if (i == dsp::indexOf (dsp::GlobalParam::bypass) || i == dsp::indexOf (dsp::GlobalParam::lookahead))
                continue;

            auto& parameter = parameterAt (i);
            parameter.beginChangeGesture();
            parameter.setValueNotifyingHost (static_cast<float> ((i * 37 + 11) % 101) / 100.0f);
            parameter.endChangeGesture();
        }

        log ("scrambled from the probe editor: fingerprint " + fingerprint());
    }

    void ProbeProcessor::restoreDefaults()
    {
        for (int i = 0; i < dsp::numParams; ++i)
        {
            if (i == dsp::indexOf (dsp::GlobalParam::bypass) || i == dsp::indexOf (dsp::GlobalParam::lookahead))
                continue;

            auto& parameter = parameterAt (i);
            parameter.beginChangeGesture();
            parameter.setValueNotifyingHost (parameter.getDefaultValue());
            parameter.endChangeGesture();
        }

        log ("defaults restored from the probe editor: fingerprint " + fingerprint());
    }

    int ProbeProcessor::lookaheadChoice() const
    {
        return static_cast<int> (std::lround (valueOf (dsp::indexOf (dsp::GlobalParam::lookahead))));
    }

    void ProbeProcessor::setLookaheadChoice (int choice)
    {
        log ("lookahead choice " + juce::String (choice) + " ("
             + juce::String (dsp::lookaheadMilliseconds[juce::jlimit (0, dsp::numLookaheadChoices - 1, choice)])
             + " ms) set from the probe editor");
        setValue (dsp::indexOf (dsp::GlobalParam::lookahead), static_cast<double> (choice));
    }

    void ProbeProcessor::noteTouches (int simultaneous, juce::MouseInputSource::InputSourceType type)
    {
        const auto first = (type == juce::MouseInputSource::InputSourceType::touch && ! sawTouch)
                           || (type == juce::MouseInputSource::InputSourceType::pen && ! sawPen);

        sawMouse = sawMouse || type == juce::MouseInputSource::InputSourceType::mouse;
        sawTouch = sawTouch || type == juce::MouseInputSource::InputSourceType::touch;
        sawPen = sawPen || type == juce::MouseInputSource::InputSourceType::pen;

        if (first)
            log (type == juce::MouseInputSource::InputSourceType::touch ? "a touch reached the editor"
                                                                        : "a pen reached the editor");

        if (simultaneous > maxTouches)
        {
            maxTouches = simultaneous;

            if (simultaneous > 1)
                log (juce::String (simultaneous) + " pointers down at once in the editor");
        }
    }

    //==========================================================================
    juce::StringArray ProbeProcessor::statusLines() const
    {
        juce::StringArray lines;

        const auto blocks = audio.blocks.load (relaxed);
        const auto minBlock = audio.minBlock.load (relaxed);

        lines.add ("Host       " + hostText() + " - " + getWrapperTypeDescription (wrapperType) + " - " + operatingSystem());

        lines.add ("Prepared   " + juce::String (preparedRate.load (relaxed), 0) + " Hz, block " + juce::String (preparedBlock.load (relaxed))
                   + " - prepareToPlay x" + juce::String (prepareCalls.load (relaxed))
                   + ", releaseResources x" + juce::String (releaseCalls.load (relaxed))
                   + ", reset x" + juce::String (resetCalls.load (relaxed)));

        lines.add ("Blocks     " + juce::String (blocks)
                   + (blocks > 0 ? " - sizes " + juce::String (minBlock) + " to " + juce::String (audio.maxBlock.load (relaxed))
                                       + ", last " + juce::String (audio.lastBlock.load (relaxed))
                                 : juce::String())
                   + " - zero-length " + juce::String (audio.zeroLengthBlocks.load (relaxed))
                   + " - float " + juce::String (audio.floatBlocks.load (relaxed))
                   + ", double " + juce::String (audio.doubleBlocks.load (relaxed))
                   + " - bypassed " + juce::String (audio.bypassedBlocks.load (relaxed)));

        lines.add ("Transport  " + juce::String (! audio.playHeadPresent.load (relaxed) ? "no play head seen"
                                                 : ! processing                         ? "last seen " + juce::String (audio.playing.load (relaxed) ? "playing" : "stopped")
                                                 : audio.playing.load (relaxed)         ? juce::String ("playing")
                                                                                        : juce::String ("stopped"))
                   + " - process calls: " + (processing ? "running" : "none now"));

        const auto mainSet = getChannelLayoutOfBus (true, 0);
        lines.add ("Main bus   " + setText (mainSet) + " - LFE: " + lfeText (mainSet) + " - peak " + peakText (shownMainPeak)
                   + ", highest ever " + peakText (audio.mainPeakEver.load (relaxed)));

        lines.add ("Sidechain  " + setText (getBusCount (true) > 1 ? getChannelLayoutOfBus (true, 1) : juce::AudioChannelSet())
                   + " - process buffer " + juce::String (audio.bufferChannels.load (relaxed)) + " channels, buses add up to "
                   + juce::String (audio.expectedChannels.load (relaxed))
                   + (audio.shortBufferBlocks.load (relaxed) > 0 ? " - SHORT in " + juce::String (audio.shortBufferBlocks.load (relaxed)) + " blocks"
                                                                  : juce::String()));

        lines.add ("  SEEN AS  " + describe (sidechainView()).toUpperCase()
                   + " - now " + peakText (shownSidechainPeak[0]) + ", " + peakText (shownSidechainPeak[1]));

        lines.add ("           highest ever " + peakText (audio.sidechainPeakEver[0].load (relaxed))
                   + ", " + peakText (audio.sidechainPeakEver[1].load (relaxed))
                   + (audio.sidechainAliasesMain.load (relaxed) ? " - SHARES MEMORY WITH THE MAIN INPUT" : ""));

        const auto choice = lookaheadChoice();
        lines.add ("Latency    " + juce::String (getLatencySamples()) + " samples (lookahead "
                   + juce::String (dsp::lookaheadMilliseconds[juce::jlimit (0, dsp::numLookaheadChoices - 1, choice)]) + " ms)"
                   + " - changed " + juce::String (latencyChanges) + " times - prepareToPlay calls since the last change: "
                   + juce::String (prepareCalls.load (relaxed) - prepareCallsAtLatencyChange));

        lines.add ("State      fingerprint " + fingerprint() + " - " + juce::String (parametersOffDefault()) + " of "
                   + juce::String (dsp::numParams) + " off default - get x" + juce::String (getStateCalls.load (relaxed))
                   + ", set x" + juce::String (setStateCalls.load (relaxed)));

        juce::StringArray kinds;

        if (sawMouse)  kinds.add ("mouse");
        if (sawTouch)  kinds.add ("touch");
        if (sawPen)    kinds.add ("pen");

        lines.add ("Pointers   most down at once " + juce::String (maxTouches) + " - kinds seen: "
                   + (kinds.isEmpty() ? juce::String ("none yet") : kinds.joinIntoString (", ")));

        return lines;
    }

    juce::String ProbeProcessor::report() const
    {
        juce::String text;

        text << "### " << hostText() << " - " << getWrapperTypeDescription (wrapperType) << " - " << operatingSystem() << "\n\n";
        text << "Likyu Probe " << LIKYU_VERSION_STRING << ", report of "
             << juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S") << ".\n\n";
        text << "```text\n";

        for (const auto& line : statusLines())
            text << line << "\n";

        text << "Layout     " << layoutText() << "\n";

        {
            const juce::ScopedLock lock (logLock);
            text << "Layouts    " << layoutsAsked.size() << " distinct layouts asked about\n";
        }

        text << "```\n\nLog:\n\n```text\n";

        for (const auto& line : logLines())
            text << line << "\n";

        text << "```\n";
        return text;
    }

    juce::String ProbeProcessor::reportFileName() const
    {
        const juce::PluginHostType host;
        return juce::File::createLegalFileName (juce::String (host.getHostDescription()) + "-"
                                                + getWrapperTypeDescription (wrapperType) + "-"
                                                + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".md")
                   .replaceCharacter (' ', '_');
    }
}
