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
    time, into a work buffer sized in prepare().

    EXACT BYPASS. A step in which nothing alters the signal (every band idle
    or an exact identity, the output gain exactly 1, the bypass at rest) does
    not write the host's buffer at all: the output is the input, bit for bit,
    -0 and denormals included, whatever the flush-to-zero mode did to the
    work copy. Bands at 0 dB still run their state, so that a gain moving
    away from 0 finds it warm.

    TINY STATES ARE SNAPPED TO ZERO at the end of each control step, below
    1e-20 (-400 dB). Flush-to-zero alone does not end a decay in this
    structure: once the products underflow, ic = 2 v - ic flips the sign of
    a state forever at the edge of the normal range, and the output holds
    there instead of reaching 0. At the grid points only, so it does not
    depend on the host's blocks either.

    THREADS. setBand() and setChain() are called on the audio thread, before
    process(), with values read from the host's atomics once per block
    (likyu::core does that); they take effect at the next grid point.
    prepare() allocates and belongs to the message thread.

    Standard library only.
*/

#include "Band.h"
#include "Denormals.h"
#include "Parameters.h"
#include "Smoother.h"
#include "rt/RtCheck.h"

#include <algorithm>
#include <array>
#include <cstddef>
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
    };

    class Chain
    {
    public:
        /** The widest main bus (PRD, non-goals). */
        static constexpr int maxChannels = 64;

        /** Allocates; not for the audio thread. With the same rate and channel
            count as last time it does nothing at all: hosts prepare often
            (twice per play, at a latency change) and that must neither cost
            nor click. Otherwise every state is cleared and the next sample
            starts at the targets, with no ramp from the defaults. */
        void prepare (double sampleRate, int numChannels);

        /** Clears every filter state; the next sample starts at the targets. */
        void reset() noexcept;

        /** `index` from 0. Takes effect at the next grid point. */
        void setBand (int index, const BandSettings& settings) noexcept;

        /** Takes effect at the next grid point. */
        void setChain (const ChainSettings& settings) noexcept;

        double getSampleRate() const noexcept   { return rate; }
        int getNumChannels() const noexcept     { return channels; }

        /** In place. Channels beyond those prepared are left as they are. */
        template <typename Sample>
        void process (Sample* const* audio, int numChannels, int numSamples) noexcept LIKYU_AUDIO_THREAD
        {
            const rt::ScopedRealtimeCheck realtime;
            const ScopedFlushToZero flushToZero;

            const auto used = std::min (numChannels, channels);

            if (audio == nullptr || used <= 0 || numSamples <= 0)
                return;

            for (int done = 0; done < numSamples;)
            {
                if (position == 0)
                    beginStep();

                const auto n = std::min (controlStep - position, numSamples - done);

                for (int c = 0; c < used; ++c)
                {
                    const Sample* in = audio[c] + done;
                    double* w = work.data() + c * controlStep;

                    for (int i = 0; i < n; ++i)
                        w[i] = static_cast<double> (in[i]);
                }

                if (runStep (used, n))
                {
                    for (int c = 0; c < used; ++c)
                    {
                        Sample* out = audio[c] + done;
                        const double* w = work.data() + c * controlStep;

                        for (int i = 0; i < n; ++i)
                            out[i] = static_cast<Sample> (w[i]);
                    }
                }

                position = (position + n) % controlStep;
                done += n;
            }
        }

    private:
        /** At a grid point: targets latched, ramps stepped, for the chain and its bands. */
        void beginStep() noexcept LIKYU_AUDIO_THREAD;

        /** `n` samples of the work buffer, from `position` in the step. True
            when the result differs from the input and is to be written back. */
        bool runStep (int numChannels, int n) noexcept LIKYU_AUDIO_THREAD;

        /** One band over the work buffer. True when it changed the signal. */
        bool runBand (int band, int numChannels, int n) noexcept LIKYU_AUDIO_THREAD;

        std::array<Band, numBands> bands {};
        std::array<bool, numBands> stateInUse {};

        std::vector<double> ic1, ic2;   // band * channels + channel
        std::vector<double> work, dry;  // channel * controlStep + sample

        Ramp outputGain, bypassMix;
        ChainSettings pendingChain;
        bool chainPending = false;
        bool snapPending = true;

        double gainDb1 = 0.0, gainFactor0 = 1.0, gainFactor1 = 1.0;
        double mix0 = 0.0, mix1 = 0.0;

        double rate = 0.0;
        int channels = 0;
        int position = 0;
        bool prepared = false;
    };
}
