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

#include "Chain.h"

#include <cmath>

namespace likyu::dsp
{
    namespace
    {
        /** Below this, a detector state is 0; a state that is not finite is reset. */
        constexpr float detectorSnapThreshold = 1.0e-20f;

        void cleanDetectorState (float& state) noexcept
        {
            if (! std::isfinite (state) || std::abs (state) < detectorSnapThreshold)
                state = 0.0f;
        }

        /** Room for the lanes of one band: as many as channels, and at least one. */
        int laneStride (int channels) noexcept
        {
            return std::max (1, channels);
        }

        /** Bit i: sample i's coefficients are the identity mix (1, 0, 0). */
        std::uint32_t identities (const StretchCoefficients& k, int n) noexcept
        {
            std::uint32_t skip = 0;

            for (int i = 0; i < n; ++i)
                if (k.isIdentity (i))
                    skip |= 1u << i;

            return skip;
        }

        /** One channel through per-sample coefficients; a sample marked in
            `skip` only advances the state, and is not written. The state is
            copied into locals first, so that a store to `w` cannot be taken
            for a store to it; each sample's terms are read one by one. */
        void tickOrAdvance (double* w, int n, double& state1, double& state2, const StretchCoefficients& k,
                            std::uint32_t skip) noexcept
        {
            auto x1 = state1, x2 = state2;

            if (skip == 0)
            {
                for (int i = 0; i < n; ++i)
                    w[i] = svfTick (w[i], x1, x2, k.a1[i], k.a2[i], k.a3[i], k.m0[i], k.m1[i], k.m2[i]);
            }
            else
            {
                for (int i = 0; i < n; ++i)
                {
                    if ((skip >> i) & 1u)
                        svfAdvance (w[i], x1, x2, k.a1[i], k.a2[i], k.a3[i]);
                    else
                        w[i] = svfTick (w[i], x1, x2, k.a1[i], k.a2[i], k.a3[i], k.m0[i], k.m1[i], k.m2[i]);
                }
            }

            state1 = x1;
            state2 = x2;
        }
    }

    void Chain::prepare (double sampleRate, int numChannels)
    {
        const auto wanted = std::clamp (numChannels, 0, maxChannels);

        if (prepared && exactlyEqual (sampleRate, rate) && wanted == channels)
            return;

        rate = sampleRate;
        channels = wanted;

        const auto states = static_cast<std::size_t> (numBands * channels);
        const auto samples = static_cast<std::size_t> (channels * controlStep);
        const auto slots = static_cast<std::size_t> (channels + maxSidechainChannels);
        const auto stride = static_cast<std::size_t> (laneStride (channels));

        ic1.assign (states, 0.0);
        ic2.assign (states, 0.0);
        work.assign (samples, 0.0);
        dry.assign (samples, 0.0);
        touched.assign (static_cast<std::size_t> (channels), 0u);
        stateInUse.fill (false);

        detectorS1.assign (slots * numBands, 0.0f);
        detectorS2.assign (slots * numBands, 0.0f);
        detectorPeak.assign (slots * numBands, 0.0f);
        keys.assign (slots * controlStep, 0.0f);
        listened.assign (slots * controlStep, 0.0f);
        lanePower.assign (stride * controlStep, 0.0f);
        slotPower.assign (slots * controlStep * numBands, 0.0f);
        laneOffsets.assign (stride * controlStep, 0.0);
        offsets.assign (stride * numBands, 0.0);
        envelopes.assign (stride * numBands, TransientEnvelopes {});
        laneFrequencies.assign (stride * numBands, LaneFrequency {});
        envStates.assign (stride * numBands, EnvState {});
        laneDetectors.assign (stride * numBands, DetectorCoefficients {});
        trackers.assign (stride * numBands, TrackerState {});
        laneConfidence.assign (stride * numBands * controlStep, 1.0);
        banks.assign (stride * (numKeySources + numBands), SeedBank {});
        bankCoefficients.prepare (rate);
        modulated.fill (false);
        perLaneDetector.fill (false);
        scaled.fill (false);
        anyTrack = false;
        lanes = {};
        lanesBuiltFor.fill (-1);
        detectorBuiltFor.fill (-1);
        detectorInUse.fill (false);
        anyOffset.fill (false);

        for (auto& band : bands)
            band.prepare (rate);

        // The longest lookahead at this rate; the delay in effect is kept.
        lineFloat.prepare (channels, maxLookaheadSamples (rate));
        lineDouble.prepare (channels, maxLookaheadSamples (rate));

        outputGain.setLength (rampSteps (smoothingSeconds, rate, controlStep));
        bypassMix.setLength (rampSteps (switchFadeSeconds, rate, controlStep));
        listenFade.setLength (rampSteps (switchFadeSeconds, rate, controlStep));

        position = 0;
        snapPending = true;
        prepared = true;
    }

    void Chain::reset() noexcept
    {
        std::fill (ic1.begin(), ic1.end(), 0.0);
        std::fill (ic2.begin(), ic2.end(), 0.0);
        std::fill (detectorS1.begin(), detectorS1.end(), 0.0f);
        std::fill (detectorS2.begin(), detectorS2.end(), 0.0f);
        std::fill (detectorPeak.begin(), detectorPeak.end(), 0.0f);
        std::fill (offsets.begin(), offsets.end(), 0.0);
        std::fill (envelopes.begin(), envelopes.end(), TransientEnvelopes {});
        std::fill (laneFrequencies.begin(), laneFrequencies.end(), LaneFrequency {});
        std::fill (envStates.begin(), envStates.end(), EnvState {});
        std::fill (trackers.begin(), trackers.end(), TrackerState {});
        std::fill (banks.begin(), banks.end(), SeedBank {});
        modulated.fill (false);
        perLaneDetector.fill (false);
        scaled.fill (false);
        anyTrack = false;
        stateInUse.fill (false);
        detectorInUse.fill (false);
        lanesBuiltFor.fill (-1);
        detectorBuiltFor.fill (-1);

        for (auto& band : bands)
            band.snap();

        lineFloat.reset();
        lineDouble.reset();

        position = 0;
        snapPending = true;
    }

    void Chain::setBand (int index, const BandSettings& settings) noexcept
    {
        if (index >= 0 && index < numBands)
            bands[static_cast<std::size_t> (index)].setTarget (settings);
    }

    void Chain::setChain (const ChainSettings& settings) noexcept
    {
        pendingChain = settings;
        chainPending = true;
    }

    void Chain::setBandChannels (int band, std::uint64_t channelsLeftOut) noexcept
    {
        if (band >= 0 && band < numBands)
        {
            pendingBandLeftOut[static_cast<std::size_t> (band)] = channelsLeftOut;
            bandChannelsPending = true;
        }
    }

    void Chain::setListen (int band) noexcept
    {
        pendingListen = band >= 0 && band < numBands ? band : -1;
        listenPending = true;
    }

    void Chain::setLookahead (int samples) noexcept
    {
        pendingLookahead = std::max (0, samples);
        lookaheadPending = true;
    }

    void Chain::setTransientTimes (const TransientTimes& times) noexcept
    {
        pendingTimes = times;
        timesPending = true;
    }

    BandMeter Chain::bandMeter (int band) const noexcept
    {
        BandMeter m;

        if (band < 0 || band >= numBands)
            return m;

        const auto index = static_cast<std::size_t> (band);
        const auto& b = bands[index];
        const auto& l = lanes[index];
        const auto stride = laneStride (channels);

        for (int lane = 0; lane < l.count; ++lane)
        {
            const auto o = offsets[static_cast<std::size_t> (band * stride + lane)];

            if (std::abs (o) > std::abs (m.offsetDb))
                m.offsetDb = o;
        }

        const auto hz = channels > 0 ? frequencyHz (band, l.count > 0 ? static_cast<int> (l.channelsByLane[0]) : 0)
                                     : b.frequencyHz();
        m.frequencyHz = std::isfinite (hz) ? hz : 0.0;

        if (b.frequencyMode() == FrequencyMode::track && l.count > 0)
            m.confidence = trackers[static_cast<std::size_t> (band * stride)].confidence;
        m.keyFellBack = b.keyFellBack();
        return m;
    }

    int Chain::laneOf (int band, int channel) const noexcept
    {
        if (band < 0 || band >= numBands || channel < 0 || channel >= channels)
            return -1;

        const auto& l = lanes[static_cast<std::size_t> (band)];
        const auto lane = l.ofChannel[static_cast<std::size_t> (channel)];
        return l.count == 0 || lane == Lanes::noLane ? -1 : static_cast<int> (lane);
    }

    double Chain::offsetDb (int band, int channel) const noexcept
    {
        const auto lane = laneOf (band, channel);
        return lane < 0 ? 0.0 : offsets[static_cast<std::size_t> (band * laneStride (channels) + lane)];
    }

    double Chain::frequencyHz (int band, int channel) const noexcept
    {
        if (band < 0 || band >= numBands)
            return 0.0;

        const auto& b = bands[static_cast<std::size_t> (band)];
        const auto lane = laneOf (band, channel);

        if (lane < 0 || ! b.hasFrequencyMode())
            return b.frequencyHz();

        const auto& f = laneFrequencies[static_cast<std::size_t> (band * laneStride (channels) + lane)];

        if (f.own || ! (f.hasOutput || f.started))
            return b.frequencyHz();

        return pow2 (f.hasOutput ? f.logHz : f.applied);
    }

    double Chain::appliedFrequencyHz (int band, int channel) const noexcept
    {
        if (band < 0 || band >= numBands)
            return 0.0;

        const auto& b = bands[static_cast<std::size_t> (band)];
        const auto lane = laneOf (band, channel);

        if (lane < 0 || ! b.hasFrequencyMode())
            return b.frequencyHz();

        const auto& f = laneFrequencies[static_cast<std::size_t> (band * laneStride (channels) + lane)];
        return f.own || ! f.started ? b.frequencyHz() : pow2 (f.applied);
    }

    //==========================================================================
    void Chain::beginStep() noexcept LIKYU_AUDIO_THREAD
    {
        if (chainPending)
        {
            chainPending = false;

            if (std::isfinite (pendingChain.outputGainDb))
                outputGain.setTarget (std::clamp (pendingChain.outputGainDb, -maxOutputGainDb, maxOutputGainDb));

            bypassMix.setTarget (pendingChain.bypass ? 1.0 : 0.0);
            linked = pendingChain.linked;
        }

        if (bandChannelsPending)
        {
            bandChannelsPending = false;
            bandLeftOut = pendingBandLeftOut;
        }

        if (listenPending)
        {
            listenPending = false;
            listenTarget = pendingListen;
        }

        if (lookaheadPending)
        {
            lookaheadPending = false;
            lineFloat.setDelay (pendingLookahead);
            lineDouble.setDelay (pendingLookahead);
        }

        sidechainChannels = sidechainNow;

        if (snapPending)
        {
            snapPending = false;
            outputGain.finish();
            bypassMix.finish();

            gainDb1 = outputGain.getCurrent();
            gainFactor0 = gainFactor1 = dbToFactor (gainDb1);
            mix0 = mix1 = bypassMix.getCurrent();

            listenBand = listenTarget;
            listenFade.jumpTo (listenBand >= 0 ? 1.0 : 0.0);
            listen0 = listen1 = listenFade.getCurrent();
        }
        else
        {
            gainFactor0 = gainFactor1;
            mix0 = mix1;

            const auto db = outputGain.step();

            if (! exactlyEqual (db, gainDb1))
            {
                gainDb1 = db;
                gainFactor1 = dbToFactor (db);
            }

            mix1 = bypassMix.step();

            // Listen: out, the band changed where nothing of it is heard, back in.
            if (listenTarget != listenBand)
            {
                if (! listenFade.isMoving() && exactlyEqual (listenFade.getCurrent(), 0.0))
                    listenBand = listenTarget;
                else
                    listenFade.setTarget (0.0);
            }

            if (listenTarget == listenBand)
                listenFade.setTarget (listenBand >= 0 ? 1.0 : 0.0);

            listen0 = listen1;
            listen1 = listenFade.step();
        }

        const auto prepared64 = channels >= 64 ? ~std::uint64_t (0) : (std::uint64_t (1) << channels) - 1u;
        const BandContext context { sidechainChannels, linked };

        selfKeyed = sidechainKeyed = false;
        anyTrack = false;

        const auto newTimes = timesPending;
        timesPending = false;

        for (int b = 0; b < numBands; ++b)
        {
            auto& band = bands[static_cast<std::size_t> (b)];
            auto own = context;
            own.leftOut = bandLeftOut[static_cast<std::size_t> (b)] & prepared64;
            band.setContext (own);

            if (newTimes)
                band.setTransientTimes (pendingTimes);

            band.beginStep();

            if (band.readsKey())
            {
                if (band.activeShape().key == KeySource::sidechain)
                    sidechainKeyed = true;
                else
                    selfKeyed = true;
            }

            if (band.getShapeRevision() != lanesBuiltFor[static_cast<std::size_t> (b)])
                buildLanes (b);

            if (band.getDetectorRevision() != detectorBuiltFor[static_cast<std::size_t> (b)])
            {
                clearDetectors (b);
                detectorBuiltFor[static_cast<std::size_t> (b)] = band.getDetectorRevision();
            }

            updateFrequencies (b);
        }

        if (anyTrack)
            seedTrackers();

        if (listenBand >= 0)
        {
            if (bands[static_cast<std::size_t> (listenBand)].detectorConfig().key == KeySource::sidechain)
                sidechainKeyed = true;
            else
                selfKeyed = true;
        }
    }

    void Chain::buildLanes (int b) noexcept LIKYU_AUDIO_THREAD
    {
        const auto index = static_cast<std::size_t> (b);
        const auto& band = bands[index];
        auto& l = lanes[index];
        const auto stride = laneStride (channels);

        l = Lanes {};
        lanesBuiltFor[index] = band.getShapeRevision();
        std::fill (offsets.begin() + b * stride, offsets.begin() + (b + 1) * stride, 0.0);
        std::fill (envelopes.begin() + b * stride, envelopes.begin() + (b + 1) * stride, TransientEnvelopes {});
        std::fill (laneFrequencies.begin() + b * stride, laneFrequencies.begin() + (b + 1) * stride, LaneFrequency {});
        std::fill (envStates.begin() + b * stride, envStates.begin() + (b + 1) * stride, EnvState {});
        std::fill (trackers.begin() + b * stride, trackers.begin() + (b + 1) * stride, TrackerState {});

        const auto& shape = band.activeShape();
        const auto applies = [&shape] (int c) { return ((shape.leftOut >> c) & 1u) == 0; };

        // A channel the band does not apply to: its filter and its detector at rest.
        for (int c = 0; c < channels; ++c)
        {
            if (applies (c))
                continue;

            ic1[static_cast<std::size_t> (b * channels + c)] = 0.0;
            ic2[static_cast<std::size_t> (b * channels + c)] = 0.0;

            const auto slot = slotIndex (c, b);
            detectorS1[slot] = detectorS2[slot] = detectorPeak[slot] = 0.0f;
        }

        // A Track band's key starts again: its key's seeding banks with it, and its own.
        if (shape.fmode == FrequencyMode::track)
        {
            for (const auto first : { static_cast<std::size_t> (shape.key == KeySource::sidechain ? stride : 0),
                                      static_cast<std::size_t> ((numKeySources + b) * stride) })
                std::fill (banks.begin() + static_cast<std::ptrdiff_t> (first),
                           banks.begin() + static_cast<std::ptrdiff_t> (first) + stride, SeedBank {});
        }

        if (! shape.readsKey() || channels <= 0)
            return;

        // No lane for a channel the band does not apply to.
        for (int c = 0; c < channels; ++c)
            l.ofChannel[static_cast<std::size_t> (c)] = Lanes::noLane;

        auto next = 0;

        if (shape.key == KeySource::sidechain)
        {
            // One lane for every channel of the band, on the sidechain's maximum.
            for (int c = 0; c < channels; ++c)
                if (applies (c))
                {
                    l.ofChannel[static_cast<std::size_t> (c)] = 0;
                    next = 1;
                }
        }
        else if (shape.linked)
        {
            // Linked: one lane for every channel of the band.
            for (int c = 0; c < channels; ++c)
                if (applies (c))
                {
                    l.ofChannel[static_cast<std::size_t> (c)] = 0;
                    next = 1;
                }
        }
        else
        {
            for (int c = 0; c < channels; ++c)
                if (applies (c))
                    l.ofChannel[static_cast<std::size_t> (c)] = static_cast<std::uint8_t> (next++);
        }

        l.count = next;

        // A Self band's key slots are its channels; a Sidechain band's all go to lane 0 (already 0).
        if (shape.key == KeySource::self)
            for (int c = 0; c < channels; ++c)
                l.ofSlot[static_cast<std::size_t> (c)] = l.ofChannel[static_cast<std::size_t> (c)];

        // The band's channels grouped by lane, for applying each lane's gain.
        std::array<int, maxChannels + 1> start {};

        for (int c = 0; c < channels; ++c)
            if (applies (c))
                ++start[static_cast<std::size_t> (l.ofChannel[static_cast<std::size_t> (c)]) + 1];

        for (int lane = 0; lane < l.count; ++lane)
            start[static_cast<std::size_t> (lane) + 1] += start[static_cast<std::size_t> (lane)];

        for (int lane = 0; lane <= l.count; ++lane)
            l.firstOfLane[static_cast<std::size_t> (lane)] = static_cast<std::uint8_t> (start[static_cast<std::size_t> (lane)]);

        for (int c = 0; c < channels; ++c)
        {
            if (! applies (c))
                continue;

            auto& at = start[static_cast<std::size_t> (l.ofChannel[static_cast<std::size_t> (c)])];
            l.channelsByLane[static_cast<std::size_t> (at++)] = static_cast<std::uint8_t> (c);
        }
    }

    void Chain::updateFrequencies (int b) noexcept LIKYU_AUDIO_THREAD
    {
        const auto index = static_cast<std::size_t> (b);
        const auto& band = bands[index];
        const auto& l = lanes[index];

        modulated[index] = false;
        perLaneDetector[index] = false;
        scaled[index] = false;

        if (! band.hasFrequencyMode() || band.isIdle() || l.count == 0)
            return;

        const auto stride = laneStride (channels);
        const auto env = band.frequencyMode() == FrequencyMode::env;
        const auto follow = band.detectorConfig().dfilt == DetectorFilter::follow;
        const auto own = band.ownLogFrequency();
        const auto& tracker = band.trackerComputer();

        if (! env)
        {
            anyTrack = true;
            scaled[index] = band.confidenceScales();
        }

        for (int lane = 0; lane < l.count; ++lane)
        {
            const auto i = static_cast<std::size_t> (b * stride + lane);
            auto& f = laneFrequencies[i];

            // Env: the follower's frequency, or the start of the mapping
            // before it has read anything. Track: the tracker's output, or
            // its rest, the band's Frequency clamped into [Low, High], as it
            // is now.
            double target = 0.0;

            if (env)
            {
                target = f.hasOutput ? f.logHz : band.envRestLogFrequency();
            }
            else
            {
                auto& s = trackers[i];
                tracker.beginStep (s);
                target = s.atRest ? tracker.current().rest : s.output;
            }

            const auto atOwn = ! env && exactlyEqual (target, own);
            const auto g = atOwn ? band.gEnd() : band.warpG (target);

            f.g0 = ! f.started ? g : (f.own ? band.gStart() : f.g1);
            f.g1 = g;
            f.applied = target;
            f.own = atOwn;
            f.started = true;

            if (! atOwn)
                modulated[index] = true;

            if (follow)
                laneDetectors[i] = atOwn ? band.detectorCoefficients() : band.followDetectorAt (g, target);
        }

        perLaneDetector[index] = follow;
    }

    void Chain::clearDetectors (int b) noexcept LIKYU_AUDIO_THREAD
    {
        for (int slot = 0; slot < channels + maxSidechainChannels; ++slot)
        {
            const auto i = slotIndex (slot, b);
            detectorS1[i] = detectorS2[i] = detectorPeak[i] = 0.0f;
        }
    }

    //==========================================================================
    bool Chain::runStep (int numChannels, int n) noexcept LIKYU_AUDIO_THREAD
    {
        const auto all = allSamples (n);
        std::fill (touched.begin(), touched.begin() + numChannels, 0u);

        const auto mixMoving = ! exactlyEqual (mix0, mix1);
        const auto fullyBypassed = ! mixMoving && exactlyEqual (mix1, 1.0);
        const auto mixing = mixMoving || ! (exactlyEqual (mix1, 0.0) || fullyBypassed);

        if (mixing)
            std::copy (work.begin(), work.begin() + numChannels * controlStep, dry.begin());

        if (anyTrack)
            track (n);

        runDetectors (numChannels, n);

        for (int b = 0; b < numBands; ++b)
        {
            detect (b, numChannels, n);
            runBand (b, numChannels, n);
        }

        // The output gain, ramped in dB at the grid points, linear between them.
        const auto gainMoving = ! exactlyEqual (gainFactor0, gainFactor1);

        if (gainMoving || ! exactlyEqual (gainFactor1, 1.0))
        {
            for (int c = 0; c < numChannels; ++c)
            {
                double* w = work.data() + c * controlStep;

                for (int i = 0; i < n; ++i)
                {
                    const auto t = static_cast<double> (position + i + 1) / controlStep;
                    w[i] *= gainMoving ? lerpExact (gainFactor0, gainFactor1, t) : gainFactor1;
                }

                touched[static_cast<std::size_t> (c)] = all;
            }
        }

        // Listen: the listened band's detector in place of the chain.
        if (listenBand >= 0 && ! (exactlyEqual (listen0, 0.0) && exactlyEqual (listen1, 0.0)))
        {
            const auto listenMoving = ! exactlyEqual (listen0, listen1);
            const auto sidechain = bands[static_cast<std::size_t> (listenBand)].detectorConfig().key == KeySource::sidechain;
            std::array<float, controlStep> mean {};

            if (sidechain && sidechainChannels > 0)
            {
                for (int s = 0; s < sidechainChannels; ++s)
                {
                    const float* y = listened.data() + (channels + s) * controlStep;

                    for (int i = 0; i < n; ++i)
                        mean[static_cast<std::size_t> (i)] += y[i];
                }

                const auto scale = 1.0f / static_cast<float> (sidechainChannels);

                for (int i = 0; i < n; ++i)
                    mean[static_cast<std::size_t> (i)] *= scale;
            }

            for (int c = 0; c < numChannels; ++c)
            {
                double* w = work.data() + c * controlStep;
                const float* y = sidechain ? mean.data() : listened.data() + c * controlStep;

                for (int i = 0; i < n; ++i)
                {
                    const auto t = static_cast<double> (position + i + 1) / controlStep;
                    const auto weight = smoothstep (listenMoving ? lerpExact (listen0, listen1, t) : listen1);
                    w[i] = lerpExact (w[i], static_cast<double> (y[i]), weight);
                }

                touched[static_cast<std::size_t> (c)] = all;
            }
        }

        // Fully bypassed: the host's buffer already holds the dry signal.
        if (fullyBypassed)
            return false;

        if (mixing)
        {
            for (int c = 0; c < numChannels; ++c)
            {
                double* w = work.data() + c * controlStep;
                const double* d = dry.data() + c * controlStep;

                for (int i = 0; i < n; ++i)
                {
                    const auto t = static_cast<double> (position + i + 1) / controlStep;
                    w[i] = lerpExact (w[i], d[i], smoothstep (mixMoving ? lerpExact (mix0, mix1, t) : mix1));
                }

                touched[static_cast<std::size_t> (c)] = all;
            }
        }

        for (int c = 0; c < numChannels; ++c)
            if (touched[static_cast<std::size_t> (c)] != 0)
                return true;

        return false;
    }

    void Chain::laneKey (int b, int lane, int n, float* out) const noexcept LIKYU_AUDIO_THREAD
    {
        const auto& l = lanes[static_cast<std::size_t> (b)];
        std::fill (out, out + n, 0.0f);

        // The sidechain's channels, or the channels of this lane.
        const auto sidechain = bands[static_cast<std::size_t> (b)].activeShape().key == KeySource::sidechain;
        const auto from = sidechain ? 0 : static_cast<int> (l.firstOfLane[static_cast<std::size_t> (lane)]);
        const auto to = sidechain ? sidechainChannels : static_cast<int> (l.firstOfLane[static_cast<std::size_t> (lane) + 1]);

        if (to <= from)
            return;

        for (int j = from; j < to; ++j)
        {
            const auto slot = sidechain ? channels + j : static_cast<int> (l.channelsByLane[static_cast<std::size_t> (j)]);
            const float* k = keys.data() + slot * controlStep;

            for (int i = 0; i < n; ++i)
                out[i] += k[i];
        }

        if (to - from > 1)
        {
            const auto scale = 1.0f / static_cast<float> (to - from);

            for (int i = 0; i < n; ++i)
                out[i] *= scale;
        }
    }

    int Chain::trackMembers (int source, int leader, std::array<int, numBands>& members, int& laneCount) const noexcept
    {
        auto count = 0;
        laneCount = 0;

        const auto tracks = [this, source] (int b)
        {
            const auto index = static_cast<std::size_t> (b);
            const auto& band = bands[index];

            return band.frequencyMode() == FrequencyMode::track && ! band.isIdle() && lanes[index].count > 0
                   && static_cast<int> (band.activeShape().key) == source;
        };

        // What tells two bands' keys apart: nothing on the sidechain, their channels on the input.
        const auto keyOf = [this, source] (int b)
        {
            return source == static_cast<int> (KeySource::sidechain)
                       ? std::uint64_t (0) : bands[static_cast<std::size_t> (b)].activeShape().leftOut;
        };

        if (! tracks (leader))
            return 0;

        for (int b = 0; b < leader; ++b)
            if (tracks (b) && keyOf (b) == keyOf (leader))
                return 0;

        for (int b = leader; b < numBands; ++b)
        {
            if (! tracks (b) || keyOf (b) != keyOf (leader))
                continue;

            members[static_cast<std::size_t> (count++)] = b;
            laneCount = std::max (laneCount, lanes[static_cast<std::size_t> (b)].count);
        }

        return count;
    }

    /*  The bands that apply to every channel, and every band on the
        sidechain, share their key source's banks, as before a band could
        leave a channel out. A group that leaves channels out reads another
        key and has its leader's own. */
    std::size_t Chain::bankBase (int source, int leader) const noexcept
    {
        const auto stride = static_cast<std::size_t> (laneStride (channels));
        const auto whole = source == static_cast<int> (KeySource::sidechain)
                           || bands[static_cast<std::size_t> (leader)].activeShape().leftOut == 0;

        return (whole ? static_cast<std::size_t> (source) : static_cast<std::size_t> (numKeySources + leader)) * stride;
    }

    void Chain::track (int n) noexcept LIKYU_AUDIO_THREAD
    {
        const auto stride = laneStride (channels);
        const auto stepEnds = position + n == controlStep;
        std::array<int, numBands> members {};

        for (int source = 0; source < numKeySources; ++source)
            for (int leader = 0; leader < numBands; ++leader)
        {
            auto laneCount = 0;
            const auto count = trackMembers (source, leader, members, laneCount);
            const auto base = count > 0 ? bankBase (source, leader) : std::size_t (0);

            for (int lane = 0; lane < laneCount; ++lane)
            {
                // The members with this lane; the key from the first of them.
                std::array<TrackerMember, maxTrackerGroup> group {};
                auto size = 0, first = -1;

                for (int m = 0; m < count; ++m)
                {
                    const auto b = members[static_cast<std::size_t> (m)];
                    const auto index = static_cast<std::size_t> (b);

                    if (lane >= lanes[index].count)
                        continue;

                    first = first < 0 ? b : first;
                    const auto i = static_cast<std::size_t> (b * stride + lane);
                    group[static_cast<std::size_t> (size++)]
                        = { &bands[index].trackerComputer(), &trackers[i],
                            scaled[index] ? laneConfidence.data() + i * controlStep : nullptr };
                }

                if (size == 0)
                    continue;

                laneKey (first, lane, n, trackKey.data());
                banks[base + static_cast<std::size_t> (lane)].run (trackKey.data(), n, bankCoefficients, stepEnds);
                runTrackerGroup (trackKey.data(), n, group.data(), size);

                for (int m = 0; m < size; ++m)
                {
                    auto& f = laneFrequencies[static_cast<std::size_t> (group[static_cast<std::size_t> (m)].state - trackers.data())];
                    f.logHz = group[static_cast<std::size_t> (m)].state->output;
                    f.hasOutput = true;
                }
            }
        }
    }

    void Chain::seedTrackers() noexcept LIKYU_AUDIO_THREAD
    {
        const auto stride = laneStride (channels);
        std::array<int, numBands> members {};

        for (int source = 0; source < numKeySources; ++source)
            for (int leader = 0; leader < numBands; ++leader)
        {
            auto laneCount = 0;
            const auto count = trackMembers (source, leader, members, laneCount);
            const auto base = count > 0 ? bankBase (source, leader) : std::size_t (0);

            for (int lane = 0; lane < laneCount; ++lane)
            {
                const auto& bank = banks[base + static_cast<std::size_t> (lane)];
                std::array<bool, numSeedRegions> claimed {};

                const auto stateOf = [this, stride, lane] (int b) -> TrackerState&
                {
                    return trackers[static_cast<std::size_t> (b * stride + lane)];
                };

                // The locked and held claim their regions first; they do not move.
                for (int m = 0; m < count; ++m)
                {
                    const auto b = members[static_cast<std::size_t> (m)];

                    if (lane < lanes[static_cast<std::size_t> (b)].count && stateOf (b).phase != TrackPhase::released)
                        claimed[static_cast<std::size_t> (bankCoefficients.regionOf (stateOf (b).estimate))] = true;
                }

                // Then the released, in band order, each claiming where it ends.
                for (int m = 0; m < count; ++m)
                {
                    const auto b = members[static_cast<std::size_t> (m)];

                    if (lane >= lanes[static_cast<std::size_t> (b)].count || stateOf (b).phase != TrackPhase::released)
                        continue;

                    auto& s = stateOf (b);
                    const auto& step = bands[static_cast<std::size_t> (b)].trackerComputer().current();
                    const auto inRange = [&step] (int r)
                    {
                        const auto lf = seedRegionLogHz (r);
                        return lf >= step.low && lf <= step.high;
                    };

                    auto current = bankCoefficients.regionOf (s.estimate);
                    auto best = -1;
                    auto bestLevel = levelFloorDb - 1.0f;

                    for (int r = 0; r < bankCoefficients.count; ++r)
                    {
                        if (claimed[static_cast<std::size_t> (r)] || ! inRange (r))
                            continue;

                        const auto level = bank.levelDb (r);

                        if (level > bestLevel)
                        {
                            best = r;
                            bestLevel = level;
                        }
                    }

                    if (best >= 0)
                    {
                        const auto currentFree = ! claimed[static_cast<std::size_t> (current)] && inRange (current);
                        const auto stay = currentFree
                                          && static_cast<double> (bank.levelDb (current)) + seedHysteresisDb >= static_cast<double> (bestLevel);

                        if (! stay)
                        {
                            s.estimate = std::clamp (seedRegionLogHz (best), step.low, step.high);
                            s.confirmed = 0;
                            current = best;
                        }
                    }

                    claimed[static_cast<std::size_t> (current)] = true;
                }
            }
        }
    }

    void Chain::runDetectors (int numChannels, int n) noexcept LIKYU_AUDIO_THREAD
    {
        const auto stride = laneStride (channels);
        const auto stepEnds = position + n == controlStep;

        // What each band's detector reads in this stretch, decided as detect() decides it.
        struct Reads
        {
            bool dynamic = false, listening = false;
            int first = 0, last = 0;
            const DetectorCoefficients* perLane = nullptr;
        };

        std::array<Reads, numBands> reads {};
        auto any = false;

        for (int b = 0; b < numBands; ++b)
        {
            const auto index = static_cast<std::size_t> (b);
            const auto& band = bands[index];
            const auto& l = lanes[index];
            auto& r = reads[index];
            r.dynamic = band.usesDetector() && ! band.isIdle() && l.count > 0;
            r.listening = b == listenBand;

            if (! r.dynamic && ! r.listening)
                continue;

            const auto sidechain = band.detectorConfig().key == KeySource::sidechain;
            r.first = sidechain ? channels : 0;
            r.last = r.first + (sidechain ? sidechainChannels : numChannels);
            r.perLane = perLaneDetector[index] && l.count > 0 ? laneDetectors.data() + b * stride : nullptr;
            any = true;
        }

        if (! any)
            return;

        for (int slot = 0; slot < channels + maxSidechainChannels; ++slot)
        {
            // The bands that read this key channel: those that count it, and the one listened to.
            DetectorLanes c;
            auto active = false, heard = false;

            for (int b = 0; b < numBands; ++b)
            {
                const auto index = static_cast<std::size_t> (b);
                const auto& r = reads[index];

                if (slot < r.first || slot >= r.last)
                    continue;

                const auto laneOfSlot = lanes[index].ofSlot[static_cast<std::size_t> (slot)];
                const auto counted = r.dynamic && laneOfSlot != Lanes::noLane;

                if (! counted && ! r.listening)
                    continue;

                c.set (b, r.perLane != nullptr && laneOfSlot != Lanes::noLane ? r.perLane[laneOfSlot]
                                                                              : bands[index].detectorCoefficients());
                active = true;
                heard = heard || r.listening;
            }

            if (! active)
                continue;

            const auto i0 = slotIndex (slot, 0);
            runDetectorLanes (keys.data() + slot * controlStep, n, detectorS1.data() + i0, detectorS2.data() + i0,
                              detectorPeak.data() + i0, c, slotPower.data() + static_cast<std::size_t> (slot * controlStep) * numBands,
                              heard ? slotOutput.data() : nullptr);

            if (heard)
            {
                float* to = listened.data() + slot * controlStep;

                for (int i = 0; i < n; ++i)
                    to[i] = slotOutput[static_cast<std::size_t> (i * numBands + listenBand)];
            }

            // At the end of a control step, a state that has decayed to nothing is nothing.
            if (stepEnds)
            {
                for (int b = 0; b < numBands; ++b)
                {
                    if (c.active[b] == 0)
                        continue;

                    const auto i = slotIndex (slot, b);
                    cleanDetectorState (detectorS1[i]);
                    cleanDetectorState (detectorS2[i]);
                    cleanDetectorState (detectorPeak[i]);
                }
            }
        }
    }

    void Chain::detect (int b, int numChannels, int n) noexcept LIKYU_AUDIO_THREAD
    {
        const auto index = static_cast<std::size_t> (b);
        const auto& band = bands[index];
        const auto& l = lanes[index];

        anyOffset[index] = false;

        // A level per lane, for an offset or for Env.
        const auto dynamic = band.usesDetector() && ! band.isIdle() && l.count > 0;
        const auto listening = b == listenBand;

        if (! dynamic && ! listening)
        {
            if (detectorInUse[index])
            {
                clearDetectors (b);
                detectorInUse[index] = false;
            }

            return;
        }

        detectorInUse[index] = true;

        const auto sidechain = band.detectorConfig().key == KeySource::sidechain;
        const auto first = sidechain ? channels : 0;
        const auto last = first + (sidechain ? sidechainChannels : numChannels);

        if (dynamic)
            for (int lane = 0; lane < l.count; ++lane)
                std::fill (lanePower.begin() + lane * controlStep, lanePower.begin() + lane * controlStep + n, 0.0f);

        // The maximum of the key channels' powers into each lane, in their
        // order; the detectors ran for every band at once (runDetectors). A
        // channel the band does not apply to is no part of its key.
        for (int slot = dynamic ? first : last; slot < last; ++slot)
        {
            const auto laneOfSlot = l.ofSlot[static_cast<std::size_t> (slot)];

            if (laneOfSlot == Lanes::noLane)
                continue;

            const float* p = slotPower.data() + static_cast<std::size_t> (slot * controlStep) * numBands + index;
            float* power = lanePower.data() + laneOfSlot * controlStep;

            for (int i = 0; i < n; ++i)
                power[i] = std::max (power[i], p[i * numBands]);
        }

        if (! dynamic)
            return;

        const auto stride = laneStride (channels);

        // Env: each lane's follower, and where it maps at the stretch's end.
        if (band.frequencyMode() == FrequencyMode::env)
        {
            const auto t = static_cast<double> (position + n) / controlStep;

            for (int lane = 0; lane < l.count; ++lane)
            {
                const auto i = static_cast<std::size_t> (b * stride + lane);
                auto& e = envStates[i];
                band.envComputer().run (lanePower.data() + lane * controlStep, n, position, controlStep, e);

                auto& f = laneFrequencies[i];
                f.logHz = band.envLogFrequency (e.level, t);
                f.hasOutput = true;
            }
        }

        if (! band.hasOffset())
            return;

        // Transient: the lane's envelopes, then its offsets, in the shaper's own loop.
        if (band.gainMode() == GainMode::transient)
        {
            for (int lane = 0; lane < l.count; ++lane)
            {
                const auto i = static_cast<std::size_t> (b * stride + lane);
                double* o = laneOffsets.data() + lane * controlStep;
                const auto moved = band.transientOffsets (lanePower.data() + lane * controlStep, n, position, envelopes[i], o);
                offsets[i] = o[n - 1];
                anyOffset[index] = anyOffset[index] || moved;
            }

            return;
        }

        // Dynamic: one level per lane, its target, and the offset's one-pole, per sample.
        for (int lane = 0; lane < l.count; ++lane)
        {
            auto& offset = offsets[static_cast<std::size_t> (b * stride + lane)];
            const float* power = lanePower.data() + lane * controlStep;
            double* o = laneOffsets.data() + lane * controlStep;
            auto moved = false;

            for (int i = 0; i < n; ++i)
            {
                const auto level = static_cast<double> (powerToDb (power[i]));
                band.smoothOffset (offset, band.dynamicTargetAt (level, position + i));
                o[i] = offset;
                moved = moved || ! exactlyEqual (offset, 0.0);
            }

            anyOffset[index] = anyOffset[index] || moved;
        }
    }

    void Chain::runBand (int b, int numChannels, int n) noexcept LIKYU_AUDIO_THREAD
    {
        const auto index = static_cast<std::size_t> (b);
        const auto& band = bands[index];
        double* s1 = ic1.data() + b * channels;
        double* s2 = ic2.data() + b * channels;

        if (band.isIdle())
        {
            if (stateInUse[index])
            {
                std::fill (s1, s1 + channels, 0.0);
                std::fill (s2, s2 + channels, 0.0);
                stateInUse[index] = false;
            }

            return;
        }

        stateInUse[index] = true;

        const auto all = allSamples (n);

        // The channels the band does not apply to are passed over: not filtered, not marked.
        const auto leftOut = band.activeShape().leftOut;
        const auto applies = [leftOut] (int c) noexcept { return ((leftOut >> c) & 1u) == 0; };

        // At the end of a control step, a state that has decayed to nothing is nothing.
        const auto stepEnds = position + n == controlStep;
        const auto snap = [stepEnds] (double& state) noexcept
        {
            if (stepEnds && std::abs (state) < stateSnapThreshold)
                state = 0.0;
        };

        if (! anyOffset[index] && ! modulated[index] && ! scaled[index])
        {
            // Phase 2: one set of coefficients for the step, or one per sample.
            if (band.isConstant())
            {
                const auto& k = band.constantCoefficients();
                const auto identity = isIdentity (k);

                for (int c = 0; c < numChannels; ++c)
                {
                    if (! applies (c))
                        continue;

                    double* w = work.data() + c * controlStep;
                    auto x1 = s1[c], x2 = s2[c];

                    if (identity)
                    {
                        for (int i = 0; i < n; ++i)
                            svfAdvance (w[i], x1, x2, k);
                    }
                    else
                    {
                        for (int i = 0; i < n; ++i)
                            w[i] = svfTick (w[i], x1, x2, k);

                        touched[static_cast<std::size_t> (c)] |= all;
                    }

                    snap (x1);
                    snap (x2);
                    s1[c] = x1;
                    s2[c] = x2;
                }

                return;
            }

            StretchCoefficients moving;
            band.coefficientsAlong (position, n, band.gStart(), band.gEnd(), nullptr, nullptr, false, moving);

            // A moving band whose mix is the identity (a bell or a shelf at
            // 0 dB whose g or k moves) is not written either.
            const auto skip = identities (moving, n);

            for (int c = 0; c < numChannels; ++c)
            {
                if (! applies (c))
                    continue;

                double* w = work.data() + c * controlStep;
                auto x1 = s1[c], x2 = s2[c];

                tickOrAdvance (w, n, x1, x2, moving, skip);

                snap (x1);
                snap (x2);
                s1[c] = x1;
                s2[c] = x2;
                touched[static_cast<std::size_t> (c)] |= all & ~skip;
            }

            return;
        }

        runBandPerLane (b, numChannels, n);
    }

    void Chain::runBandPerLane (int b, int numChannels, int n) noexcept LIKYU_AUDIO_THREAD
    {
        const auto index = static_cast<std::size_t> (b);
        const auto& band = bands[index];
        double* s1 = ic1.data() + b * channels;
        double* s2 = ic2.data() + b * channels;
        const auto all = allSamples (n);
        const auto stepEnds = position + n == controlStep;
        const auto snap = [stepEnds] (double& state) noexcept
        {
            if (stepEnds && std::abs (state) < stateSnapThreshold)
                state = 0.0;
        };

        /*  Lane by lane: offsets, frequencies, or both. A sample of a lane at
            the band's own frequency whose offset is exactly 0 has the Phase 2
            coefficients, and a sample whose mix is the identity is skipped,
            on this path as on Phase 2's: whichever path a stretch takes, the
            bits are the same. A lane off the band's own frequency runs on its
            own g, interpolated across the step from the one before. */
        const auto& l = lanes[index];
        const auto withOffsets = anyOffset[index];
        const auto withFrequencies = modulated[index];
        const auto withScale = scaled[index];
        const auto stride = laneStride (channels);
        StretchCoefficients coefficients;

        for (int lane = 0; lane < l.count; ++lane)
        {
            const auto from = l.firstOfLane[static_cast<std::size_t> (lane)];
            const auto to = l.firstOfLane[static_cast<std::size_t> (lane) + 1];

            if (from == to)
                continue;

            const double* o = withOffsets ? laneOffsets.data() + lane * controlStep : nullptr;
            const auto i0 = static_cast<std::size_t> (b * stride + lane);
            const auto& f = laneFrequencies[i0];

            // The stretch's coefficients, four samples to a register (Band::coefficientsAlong).
            if (withScale)
            {
                // Confidence scales the gain: the lane's own g, or the band's.
                const auto* confidence = laneConfidence.data() + i0 * controlStep;
                const auto own = ! withFrequencies || f.own;
                std::array<double, controlStep> scales;
                scales.fill (1.0);

                for (int i = 0; i < n; ++i)
                {
                    const auto t = static_cast<double> (position + i + 1) / controlStep;
                    scales[static_cast<std::size_t> (i)] = 1.0 - band.confidenceWeightAt (t) * (1.0 - confidence[i]);
                }

                band.coefficientsAlong (position, n, own ? band.gStart() : f.g0, own ? band.gEnd() : f.g1, o,
                                        scales.data(), true, coefficients);
            }
            else if (! withFrequencies || f.own)
            {
                // An offset of exactly 0 is the band's own coefficients, which a
                // constant band's are (Band::constantCoefficients), to the bit.
                band.coefficientsAlong (position, n, band.gStart(), band.gEnd(), o, nullptr, false, coefficients);
            }
            else
            {
                band.coefficientsAlong (position, n, f.g0, f.g1, o, nullptr, true, coefficients);
            }

            const auto skip = identities (coefficients, n);

            for (auto j = from; j < to; ++j)
            {
                const auto c = static_cast<int> (l.channelsByLane[j]);

                if (c >= numChannels)
                    continue;

                double* w = work.data() + c * controlStep;
                auto x1 = s1[c], x2 = s2[c];

                tickOrAdvance (w, n, x1, x2, coefficients, skip);

                snap (x1);
                snap (x2);
                s1[c] = x1;
                s2[c] = x2;
                touched[static_cast<std::size_t> (c)] |= all & ~skip;
            }
        }
    }
}
