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
    THE CHAIN (PRD, Signal flow): eight bands in series, band 1 first, over 1
    to 64 channels, then the output gain, then the bypass crossfade to the
    chain's own input. In the plugin the lookahead delay sits ahead of it
    (dsp/Lookahead.h), so the chain's input is the delayed dry signal the
    PRD's bypass crossfades to.

    ONE GRID. Processing runs in control steps of 16 samples (dsp/Band.h) on
    a counter that starts at prepare() and runs across calls; a host block
    that ends mid-step leaves the rest of the step to the next call. Every
    value is a function of the sample index and of where targets were set,
    so the output does not depend on how a host cuts its blocks: bit for bit
    (acceptance row "block-size invariance").

    PRECISION. States and coefficients are double (PRD, decided 2026-11-16);
    host buffers of either precision are converted at the edge, one step at a
    time, into a work buffer sized in prepare(). Detection is float.

    EXACT BYPASS. A sample that nothing alters (every band idle or an exact
    identity there, the output gain exactly 1, the bypass at rest) is not
    written back to the host's buffer: the output is the input, bit for bit,
    -0 and denormals included, whatever the flush-to-zero mode did to the
    work copy. Each stage marks the samples it changed, per channel, and
    only those are written. Bands at 0 dB still run their state, so that a
    gain moving away from 0 finds it warm.

    TINY STATES ARE SNAPPED TO ZERO at the end of each control step, below
    1e-20 (-400 dB), the detectors' with the bands'. Flush-to-zero alone does
    not end a decay in this structure: once the products underflow,
    ic = 2 v - ic flips the sign of a state forever at the edge of the normal
    range, and the output holds there instead of reaching 0. At the grid
    points only, so it does not depend on the host's blocks either.

    DETECTION (PRD, Detection; Phase 3). Each band reads a KEY, never the
    audio between stages: the plugin's main input, undelayed (Self), or the
    sidechain bus (Sidechain), passed to process() beside the audio. With
    the sidechain absent or disabled (no channels in the call) a Sidechain
    band falls back to Self; a bus that is there and silent is a key. One
    float detector per band per key channel; their levels become offsets in
    LANES, one gain computer state each:

      - a Sidechain band: one lane for every channel, on the maximum over
        the sidechain's channels;
      - Self, Linked: one lane for the channels in the link, on the maximum
        over theirs, and one lane per channel left out, on its own key;
      - Self, Unlinked: one lane per channel.

    The maximum is taken on power, then one level per lane: a tone in one
    channel of eight, in all eight, or in two in opposite phase gives the
    same offset, bit for bit (acceptance row "linked maximum").

    A DYNAMIC OFFSET OF EXACTLY 0 IS PHASE 2. A sample whose offset is 0 is
    computed with the band's own Phase 2 coefficients, and an identity is
    skipped only in a step where the band is constant, exactly as Phase 2
    skips it; so which path a stretch of samples takes changes no bit, and
    a band whose offsets have all returned to 0 is again the exact bypass.

    LISTEN (PRD, Detection; Phase 3d): one band's detector output replaces
    the chain's output, a solo in place. Self: each channel hears its own
    detector. Sidechain: the mean of the sidechain's detectors, on every
    channel. It crossfades in and out along a smoothstep, 10 ms each way; a
    change of band goes out and back in. The bypass, after it, still wins.

    METERS: per band, the offset now (the largest over its lanes), the
    frequency now, the tracker's confidence (0 until Phase 5) and whether
    the key fell back to Self. Read on the audio thread after process();
    likyu::core publishes them (core/MeterSnapshot.h).

    THREADS. setBand(), setChain(), setLinkExclusions(), setListen() are
    called on the audio thread, before process(), with values read from the
    host's atomics once per block (likyu::core does that); they take effect
    at the next grid point. prepare() allocates and belongs to the message
    thread.

    Standard library only.
*/

#include "Band.h"
#include "Denormals.h"
#include "FastLog.h"
#include "Parameters.h"
#include "Smoother.h"
#include "rt/RtCheck.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace likyu::dsp
{
    /** Below this a filter state is set to exactly 0, at the end of a control step. */
    inline constexpr double stateSnapThreshold = 1.0e-20;

    /** What the instance is asked to be, beyond its bands. */
    struct ChainSettings
    {
        bool bypass = false;
        double outputGainDb = 0.0;
        bool linked = true;
    };

    /** Where the detectors read, beside the audio: the main input undelayed
        (`self`, one pointer per main channel) and the sidechain bus (none,
        one or two channels; 0 when absent or disabled). Read only. */
    template <typename Sample>
    struct Key
    {
        const Sample* const* self = nullptr;
        int selfChannels = 0;
        const Sample* const* sidechain = nullptr;
        int sidechainChannels = 0;
    };

    /** What a band shows of itself now (PRD, Detection: metering). */
    struct BandMeter
    {
        double offsetDb = 0.0;
        double frequencyHz = 0.0;
        double confidence = 0.0;
        bool keyFellBack = false;
    };

    class Chain
    {
    public:
        /** The widest main bus (PRD, non-goals). */
        static constexpr int maxChannels = 64;

        /** The widest sidechain (PRD, Formats: mono or stereo). */
        static constexpr int maxSidechainChannels = 2;

        /** Allocates; not for the audio thread. With the same rate and channel
            count as last time it does nothing at all: hosts prepare often
            (twice per play, at a latency change) and that must neither cost
            nor click. Otherwise every state is cleared and the next sample
            starts at the targets, with no ramp from the defaults. */
        void prepare (double sampleRate, int numChannels);

        /** Clears every filter, detector and offset; the next sample starts at the targets. */
        void reset() noexcept;

        /** `index` from 0. Takes effect at the next grid point. */
        void setBand (int index, const BandSettings& settings) noexcept;

        /** Takes effect at the next grid point. */
        void setChain (const ChainSettings& settings) noexcept;

        /** The channels left out of the link, bit c for channel c (PRD,
            Detection; the list itself is the state chunk's). Takes effect at
            the next grid point; bits beyond the channels prepared are ignored. */
        void setLinkExclusions (std::uint64_t channelsLeftOut) noexcept;

        /** The band (from 0) whose detector replaces the output, or -1 for
            none. Takes effect at the next grid point, through a crossfade. */
        void setListen (int band) noexcept;

        /** The band listened to now, or -1; it stays until the fade out ends. */
        int getListenBand() const noexcept      { return listenBand; }

        double getSampleRate() const noexcept   { return rate; }
        int getNumChannels() const noexcept     { return channels; }

        /** The dynamic offset of `band` (from 0) on `channel` now, in dB: 0
            for a band that is not dynamic. For tests and the meters. */
        double offsetDb (int band, int channel) const noexcept;

        /** What `band` (from 0) shows now. */
        BandMeter bandMeter (int band) const noexcept;

        /** In place, keyed by the input itself and no sidechain: the plugin's
            case with the lookahead off, and a native client's. Channels
            beyond those prepared are left as they are. */
        template <typename Sample>
        void process (Sample* const* audio, int numChannels, int numSamples) noexcept LIKYU_AUDIO_THREAD
        {
            Key<Sample> key;
            key.self = audio;
            key.selfChannels = numChannels;
            process (audio, numChannels, key, numSamples);
        }

        /** In place, keyed by `key`, which may be the audio itself: every key
            sample of a stretch is read before any audio of it is written. */
        template <typename Sample>
        void process (Sample* const* audio, int numChannels, const Key<Sample>& key, int numSamples) noexcept LIKYU_AUDIO_THREAD
        {
            const rt::ScopedRealtimeCheck realtime;
            const ScopedFlushToZero flushToZero;

            const auto used = std::min (numChannels, channels);

            if (audio == nullptr || used <= 0 || numSamples <= 0)
                return;

            sidechainNow = key.sidechain != nullptr ? std::clamp (key.sidechainChannels, 0, maxSidechainChannels) : 0;

            for (int done = 0; done < numSamples;)
            {
                if (position == 0)
                    beginStep();

                const auto n = std::min (controlStep - position, numSamples - done);

                // The key first: it may be the audio, which is written below.
                // Only what a detector of this step reads.
                for (int c = 0; c < (selfKeyed ? used : 0); ++c)
                {
                    const Sample* in = key.self != nullptr && c < key.selfChannels ? key.self[c] : nullptr;
                    float* k = keys.data() + c * controlStep;

                    for (int i = 0; i < n; ++i)
                        k[i] = in != nullptr ? static_cast<float> (in[done + i]) : 0.0f;
                }

                for (int s = 0; s < (sidechainKeyed ? maxSidechainChannels : 0); ++s)
                {
                    const Sample* in = s < sidechainNow ? key.sidechain[s] : nullptr;
                    float* k = keys.data() + (channels + s) * controlStep;

                    for (int i = 0; i < n; ++i)
                        k[i] = in != nullptr ? static_cast<float> (in[done + i]) : 0.0f;
                }

                for (int c = 0; c < used; ++c)
                {
                    const Sample* in = audio[c] + done;
                    double* w = work.data() + c * controlStep;

                    for (int i = 0; i < n; ++i)
                        w[i] = static_cast<double> (in[i]);
                }

                if (runStep (used, n))
                {
                    const auto all = allSamples (n);

                    for (int c = 0; c < used; ++c)
                    {
                        const auto marked = touched[static_cast<std::size_t> (c)];

                        if (marked == 0)
                            continue;

                        Sample* out = audio[c] + done;
                        const double* w = work.data() + c * controlStep;

                        if (marked == all)
                        {
                            for (int i = 0; i < n; ++i)
                                out[i] = static_cast<Sample> (w[i]);
                        }
                        else
                        {
                            for (int i = 0; i < n; ++i)
                                if ((marked >> i) & 1u)
                                    out[i] = static_cast<Sample> (w[i]);
                        }
                    }
                }

                position = (position + n) % controlStep;
                done += n;
            }
        }

    private:
        /** Which lane each channel's gain comes from, and each key slot's
            level goes to. Slots: the main channels, then the sidechain's. */
        struct Lanes
        {
            int count = 0;
            std::array<std::uint8_t, maxChannels> ofChannel {};
            std::array<std::uint8_t, maxChannels + maxSidechainChannels> ofSlot {};
            std::array<std::uint8_t, maxChannels> channelsByLane {};    // channels, grouped by lane
            std::array<std::uint8_t, maxChannels + 1> firstOfLane {};   // into channelsByLane
        };

        static constexpr std::uint32_t allSamples (int n) noexcept
        {
            return n >= 32 ? 0xffffffffu : (1u << n) - 1u;
        }

        /** At a grid point: targets latched, ramps stepped, for the chain and its bands. */
        void beginStep() noexcept LIKYU_AUDIO_THREAD;

        /** `n` samples of the work buffer, from `position` in the step. True
            when anything is to be written back (the touched marks say what). */
        bool runStep (int numChannels, int n) noexcept LIKYU_AUDIO_THREAD;

        /** One band's detectors and lanes over the key: its offsets for the stretch. */
        void detect (int band, int numChannels, int n) noexcept LIKYU_AUDIO_THREAD;

        /** One band over the work buffer, marking what it changed. */
        void runBand (int band, int numChannels, int n) noexcept LIKYU_AUDIO_THREAD;

        /** The lanes of a band for its shape in effect; offsets back to 0. */
        void buildLanes (int band) noexcept LIKYU_AUDIO_THREAD;

        void clearDetectors (int band) noexcept LIKYU_AUDIO_THREAD;

        std::size_t slotIndex (int slot, int band) const noexcept
        {
            return static_cast<std::size_t> (slot * numBands + band);
        }

        std::array<Band, numBands> bands {};
        std::array<bool, numBands> stateInUse {};

        std::vector<double> ic1, ic2;   // band * channels + channel
        std::vector<double> work, dry;  // channel * controlStep + sample
        std::vector<std::uint32_t> touched;   // per channel: the samples of the stretch written

        // Detection, float, structure-of-arrays: slot * numBands + band.
        std::vector<float> detectorS1, detectorS2, detectorPeak;
        std::array<bool, numBands> detectorInUse {};
        std::array<int, numBands> detectorBuiltFor {};   // the band's detector revision
        std::vector<float> keys;                // slot * controlStep + sample
        std::vector<float> listened;            // the listened band's detector outputs, the same
        std::vector<float> lanePower;           // lane * controlStep + sample, one band at a time
        std::vector<double> laneOffsets;        // the same, the offsets
        std::array<bool, numBands> anyOffset {};
        std::vector<double> offsets;            // band * channels + lane: the smoothed offset, dB
        std::array<Lanes, numBands> lanes {};
        std::array<int, numBands> lanesBuiltFor {};      // the band's shape revision

        Ramp outputGain, bypassMix;
        ChainSettings pendingChain;
        bool chainPending = false;
        bool snapPending = true;

        bool linked = true;
        std::uint64_t excluded = 0, pendingExcluded = 0;
        bool exclusionsPending = false;
        int sidechainNow = 0, sidechainChannels = 0;
        bool selfKeyed = false, sidechainKeyed = false;   // a detector of this step reads that key

        Ramp listenFade;
        int pendingListen = -1, listenTarget = -1, listenBand = -1;
        bool listenPending = false;
        double listen0 = 0.0, listen1 = 0.0;

        double gainDb1 = 0.0, gainFactor0 = 1.0, gainFactor1 = 1.0;
        double mix0 = 0.0, mix1 = 0.0;

        double rate = 0.0;
        int channels = 0;
        int position = 0;
        bool prepared = false;
    };
}
