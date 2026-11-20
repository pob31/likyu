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
    void Chain::prepare (double sampleRate, int numChannels)
    {
        const auto wanted = std::clamp (numChannels, 0, maxChannels);

        if (prepared && exactlyEqual (sampleRate, rate) && wanted == channels)
            return;

        rate = sampleRate;
        channels = wanted;

        const auto states = static_cast<std::size_t> (numBands * channels);
        const auto samples = static_cast<std::size_t> (channels * controlStep);

        ic1.assign (states, 0.0);
        ic2.assign (states, 0.0);
        work.assign (samples, 0.0);
        dry.assign (samples, 0.0);
        stateInUse.fill (false);

        for (auto& band : bands)
            band.prepare (rate);

        outputGain.setLength (rampSteps (smoothingSeconds, rate, controlStep));
        bypassMix.setLength (rampSteps (switchFadeSeconds, rate, controlStep));

        position = 0;
        snapPending = true;
        prepared = true;
    }

    void Chain::reset() noexcept
    {
        std::fill (ic1.begin(), ic1.end(), 0.0);
        std::fill (ic2.begin(), ic2.end(), 0.0);
        stateInUse.fill (false);

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

    //==========================================================================
    void Chain::beginStep() noexcept LIKYU_AUDIO_THREAD
    {
        if (chainPending)
        {
            chainPending = false;

            if (std::isfinite (pendingChain.outputGainDb))
                outputGain.setTarget (std::clamp (pendingChain.outputGainDb, -maxOutputGainDb, maxOutputGainDb));

            bypassMix.setTarget (pendingChain.bypass ? 1.0 : 0.0);
        }

        if (snapPending)
        {
            snapPending = false;
            outputGain.finish();
            bypassMix.finish();

            gainDb1 = outputGain.getCurrent();
            gainFactor0 = gainFactor1 = dbToFactor (gainDb1);
            mix0 = mix1 = bypassMix.getCurrent();
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
        }

        for (auto& band : bands)
            band.beginStep();
    }

    bool Chain::runStep (int numChannels, int n) noexcept LIKYU_AUDIO_THREAD
    {
        const auto mixMoving = ! exactlyEqual (mix0, mix1);
        const auto fullyBypassed = ! mixMoving && exactlyEqual (mix1, 1.0);
        const auto mixing = mixMoving || ! (exactlyEqual (mix1, 0.0) || fullyBypassed);

        if (mixing)
            std::copy (work.begin(), work.begin() + numChannels * controlStep, dry.begin());

        auto changed = false;

        for (int b = 0; b < numBands; ++b)
            changed = runBand (b, numChannels, n) || changed;

        // The output gain, ramped in dB at the grid points, linear between them.
        const auto gainMoving = ! exactlyEqual (gainFactor0, gainFactor1);

        if (gainMoving || ! exactlyEqual (gainFactor1, 1.0))
        {
            changed = true;

            for (int c = 0; c < numChannels; ++c)
            {
                double* w = work.data() + c * controlStep;

                for (int i = 0; i < n; ++i)
                {
                    const auto t = static_cast<double> (position + i + 1) / controlStep;
                    w[i] *= gainMoving ? lerpExact (gainFactor0, gainFactor1, t) : gainFactor1;
                }
            }
        }

        // Fully bypassed: the host's buffer already holds the dry signal.
        if (fullyBypassed)
            return false;

        if (mixing)
        {
            changed = true;

            for (int c = 0; c < numChannels; ++c)
            {
                double* w = work.data() + c * controlStep;
                const double* d = dry.data() + c * controlStep;

                for (int i = 0; i < n; ++i)
                {
                    const auto t = static_cast<double> (position + i + 1) / controlStep;
                    w[i] = lerpExact (w[i], d[i], smoothstep (mixMoving ? lerpExact (mix0, mix1, t) : mix1));
                }
            }
        }

        return changed;
    }

    bool Chain::runBand (int b, int numChannels, int n) noexcept LIKYU_AUDIO_THREAD
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

            return false;
        }

        stateInUse[index] = true;

        // At the end of a control step, a state that has decayed to nothing is nothing.
        const auto stepEnds = position + n == controlStep;
        const auto snap = [stepEnds] (double& state) noexcept
        {
            if (stepEnds && std::abs (state) < stateSnapThreshold)
                state = 0.0;
        };

        if (band.isConstant())
        {
            const auto& k = band.constantCoefficients();

            if (isIdentity (k))
            {
                for (int c = 0; c < numChannels; ++c)
                {
                    const double* w = work.data() + c * controlStep;
                    auto x1 = s1[c], x2 = s2[c];

                    for (int i = 0; i < n; ++i)
                        svfAdvance (w[i], x1, x2, k);

                    snap (x1);
                    snap (x2);
                    s1[c] = x1;
                    s2[c] = x2;
                }

                return false;
            }

            for (int c = 0; c < numChannels; ++c)
            {
                double* w = work.data() + c * controlStep;
                auto x1 = s1[c], x2 = s2[c];

                for (int i = 0; i < n; ++i)
                    w[i] = svfTick (w[i], x1, x2, k);

                snap (x1);
                snap (x2);
                s1[c] = x1;
                s2[c] = x2;
            }

            return true;
        }

        std::array<SvfCoefficients<double>, controlStep> moving;

        for (int i = 0; i < n; ++i)
            moving[static_cast<std::size_t> (i)] = band.coefficientsAt (position + i);

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
        }

        return true;
    }
}
