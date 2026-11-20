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
        laneOffsets.assign (stride * controlStep, 0.0);
        offsets.assign (stride * numBands, 0.0);
        lanes = {};
        lanesBuiltFor.fill (-1);
        detectorBuiltFor.fill (-1);
        detectorInUse.fill (false);
        anyOffset.fill (false);

        for (auto& band : bands)
            band.prepare (rate);

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
        stateInUse.fill (false);
        detectorInUse.fill (false);
        lanesBuiltFor.fill (-1);
        detectorBuiltFor.fill (-1);

        for (auto& band : bands)
            band.snap();

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

    void Chain::setLinkExclusions (std::uint64_t channelsLeftOut) noexcept
    {
        pendingExcluded = channelsLeftOut;
        exclusionsPending = true;
    }

    void Chain::setListen (int band) noexcept
    {
        pendingListen = band >= 0 && band < numBands ? band : -1;
        listenPending = true;
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

        const auto hz = b.frequencyHz();
        m.frequencyHz = std::isfinite (hz) ? hz : 0.0;
        m.keyFellBack = b.keyFellBack();
        return m;
    }

    double Chain::offsetDb (int band, int channel) const noexcept
    {
        if (band < 0 || band >= numBands || channel < 0 || channel >= channels)
            return 0.0;

        const auto& l = lanes[static_cast<std::size_t> (band)];

        if (l.count == 0)
            return 0.0;

        const auto lane = l.ofChannel[static_cast<std::size_t> (channel)];
        return offsets[static_cast<std::size_t> (band * laneStride (channels) + lane)];
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

        if (exclusionsPending)
        {
            exclusionsPending = false;
            excluded = pendingExcluded;
        }

        if (listenPending)
        {
            listenPending = false;
            listenTarget = pendingListen;
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
        const BandContext context { sidechainChannels, linked, excluded & prepared64 };

        selfKeyed = sidechainKeyed = false;

        for (int b = 0; b < numBands; ++b)
        {
            auto& band = bands[static_cast<std::size_t> (b)];
            band.setContext (context);
            band.beginStep();

            if (band.isDynamic() && ! band.isIdle())
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
        }

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

        const auto& shape = band.activeShape();

        if (! shape.dynamic || channels <= 0)
            return;

        if (shape.key == KeySource::sidechain)
        {
            // One lane for every channel, on the sidechain's maximum.
            l.count = 1;
        }
        else if (shape.linked)
        {
            // The link's lane first (when any channel is in it), then one per channel left out.
            auto anyIn = false;

            for (int c = 0; c < channels; ++c)
                anyIn = anyIn || ((shape.excluded >> c) & 1u) == 0;

            auto next = anyIn ? 1 : 0;

            for (int c = 0; c < channels; ++c)
            {
                const auto out = ((shape.excluded >> c) & 1u) != 0;
                l.ofChannel[static_cast<std::size_t> (c)] = static_cast<std::uint8_t> (out ? next++ : 0);
            }

            l.count = next;
        }
        else
        {
            for (int c = 0; c < channels; ++c)
                l.ofChannel[static_cast<std::size_t> (c)] = static_cast<std::uint8_t> (c);

            l.count = channels;
        }

        // A Self band's key slots are its channels; a Sidechain band's all go to lane 0 (already 0).
        if (shape.key == KeySource::self)
            for (int c = 0; c < channels; ++c)
                l.ofSlot[static_cast<std::size_t> (c)] = l.ofChannel[static_cast<std::size_t> (c)];

        // The channels grouped by lane, for applying each lane's gain.
        std::array<int, maxChannels + 1> start {};

        for (int c = 0; c < channels; ++c)
            ++start[static_cast<std::size_t> (l.ofChannel[static_cast<std::size_t> (c)]) + 1];

        for (int lane = 0; lane < l.count; ++lane)
            start[static_cast<std::size_t> (lane) + 1] += start[static_cast<std::size_t> (lane)];

        for (int lane = 0; lane <= l.count; ++lane)
            l.firstOfLane[static_cast<std::size_t> (lane)] = static_cast<std::uint8_t> (start[static_cast<std::size_t> (lane)]);

        for (int c = 0; c < channels; ++c)
        {
            auto& at = start[static_cast<std::size_t> (l.ofChannel[static_cast<std::size_t> (c)])];
            l.channelsByLane[static_cast<std::size_t> (at++)] = static_cast<std::uint8_t> (c);
        }
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

    void Chain::detect (int b, int numChannels, int n) noexcept LIKYU_AUDIO_THREAD
    {
        const auto index = static_cast<std::size_t> (b);
        const auto& band = bands[index];
        const auto& l = lanes[index];

        anyOffset[index] = false;

        const auto dynamic = band.isDynamic() && ! band.isIdle() && l.count > 0;
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

        const auto& k = band.detectorCoefficients();
        const auto sidechain = band.detectorConfig().key == KeySource::sidechain;
        const auto first = sidechain ? channels : 0;
        const auto last = first + (sidechain ? sidechainChannels : numChannels);
        const auto stepEnds = position + n == controlStep;

        if (dynamic)
            for (int lane = 0; lane < l.count; ++lane)
                std::fill (lanePower.begin() + lane * controlStep, lanePower.begin() + lane * controlStep + n, 0.0f);

        // Every key channel's detector; the maximum of the powers into its lane.
        for (int slot = first; slot < last; ++slot)
        {
            const auto i0 = slotIndex (slot, b);
            auto s1 = detectorS1[i0], s2 = detectorS2[i0], peak = detectorPeak[i0];
            const float* x = keys.data() + slot * controlStep;
            float* power = lanePower.data() + (dynamic ? l.ofSlot[static_cast<std::size_t> (slot)] : 0) * controlStep;
            float* heard = listened.data() + slot * controlStep;
            float output = 0.0f;

            for (int i = 0; i < n; ++i)
            {
                const auto p = detectorTick (x[i], s1, s2, peak, k, output);

                if (dynamic)
                    power[i] = std::max (power[i], p);

                if (listening)
                    heard[i] = output;
            }

            if (stepEnds)
            {
                cleanDetectorState (s1);
                cleanDetectorState (s2);
                cleanDetectorState (peak);
            }

            detectorS1[i0] = s1;
            detectorS2[i0] = s2;
            detectorPeak[i0] = peak;
        }

        if (! dynamic)
            return;

        // One level per lane, its target, and the offset's one-pole, per sample.
        const auto stride = laneStride (channels);

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

        // At the end of a control step, a state that has decayed to nothing is nothing.
        const auto stepEnds = position + n == controlStep;
        const auto snap = [stepEnds] (double& state) noexcept
        {
            if (stepEnds && std::abs (state) < stateSnapThreshold)
                state = 0.0;
        };

        if (! anyOffset[index])
        {
            // Phase 2: one set of coefficients for the step, or one per sample.
            if (band.isConstant())
            {
                const auto& k = band.constantCoefficients();
                const auto identity = isIdentity (k);

                for (int c = 0; c < numChannels; ++c)
                {
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

            std::array<SvfCoefficients<double>, controlStep> moving;
            band.coefficientsFor (position, n, moving.data());

            for (int c = 0; c < numChannels; ++c)
            {
                double* w = work.data() + c * controlStep;
                auto x1 = s1[c], x2 = s2[c];

                for (int i = 0; i < n; ++i)
                    w[i] = svfTick (w[i], x1, x2, moving[static_cast<std::size_t> (i)]);

                snap (x1);
                snap (x2);
                s1[c] = x1;
                s2[c] = x2;
                touched[static_cast<std::size_t> (c)] |= all;
            }

            return;
        }

        runBandWithOffsets (b, numChannels, n);
    }

    void Chain::runBandWithOffsets (int b, int numChannels, int n) noexcept LIKYU_AUDIO_THREAD
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

        /*  With offsets, lane by lane. A sample whose offset is exactly 0 has
            the Phase 2 coefficients, and is skipped as an identity only where
            Phase 2 would skip it, in a constant step: whichever path a
            stretch takes, the bits are the same. */
        const auto& l = lanes[index];
        const auto constant = band.isConstant();
        std::array<SvfCoefficients<double>, controlStep> coefficients;

        for (int lane = 0; lane < l.count; ++lane)
        {
            const auto from = l.firstOfLane[static_cast<std::size_t> (lane)];
            const auto to = l.firstOfLane[static_cast<std::size_t> (lane) + 1];

            if (from == to)
                continue;

            const double* o = laneOffsets.data() + lane * controlStep;
            std::uint32_t skip = 0;

            for (int i = 0; i < n; ++i)
            {
                auto& k = coefficients[static_cast<std::size_t> (i)];

                if (exactlyEqual (o[i], 0.0))
                {
                    if (constant)
                    {
                        k = band.constantCoefficients();

                        if (isIdentity (k))
                            skip |= 1u << i;
                    }
                    else
                    {
                        k = band.coefficientsAt (position + i);
                    }
                }
                else
                {
                    k = band.coefficientsAt (position + i, o[i]);
                }
            }

            for (auto j = from; j < to; ++j)
            {
                const auto c = static_cast<int> (l.channelsByLane[j]);

                if (c >= numChannels)
                    continue;

                double* w = work.data() + c * controlStep;
                auto x1 = s1[c], x2 = s2[c];

                for (int i = 0; i < n; ++i)
                {
                    const auto& k = coefficients[static_cast<std::size_t> (i)];

                    if ((skip >> i) & 1u)
                        svfAdvance (w[i], x1, x2, k);
                    else
                        w[i] = svfTick (w[i], x1, x2, k);
                }

                snap (x1);
                snap (x2);
                s1[c] = x1;
                s2[c] = x2;
                touched[static_cast<std::size_t> (c)] |= all & ~skip;
            }
        }
    }
}
