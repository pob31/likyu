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
    THE TWO LINES (PRD, Editor: the static response as a line, and the live
    response, static plus current offsets and current frequencies, as a
    second line moving over it).

    Each is the sum, in dB, of the bands that are on, every band through
    dsp/Response.h: the filter's own response at the instance's sample rate,
    warping included. The static line has every band at rest (its Frequency,
    or where its frequency mode rests) at its static gain. The live line has
    it where the meters say it is, at the gain they say it has.

    THE METERS HOLD ONE VALUE PER BAND: the largest offset over the band's
    lanes and the link lane's frequency (dsp/Chain.cpp, bandMeter). With the
    channels unlinked the live line is therefore the channel that moves most,
    not each channel's own.

    The output gain is not in the picture: it is a level, not a shape.

    No JUCE here.
*/

#include "Presentation.h"

#include <dsp/Response.h>

#include <array>
#include <cmath>
#include <vector>

namespace likyu::editor
{
    /** One band of a line. */
    struct CurveBand
    {
        bool on = false;
        dsp::BandType type = dsp::BandType::bell;
        dsp::QMode qmode = dsp::QMode::proportional;
        double hz = 1000.0;
        double q = 1.0;
        double gainDb = 0.0;
    };

    using CurveBands = std::array<CurveBand, dsp::numBands>;

    inline CurveBand staticBandOf (const dsp::BandSettings& s) noexcept
    {
        return { s.on, s.type, s.qmode, presentation::restFrequency (s), s.q, s.gainDb };
    }

    inline CurveBand liveBandOf (const dsp::BandSettings& s, const core::BandMeters& m, bool running) noexcept
    {
        return { s.on, s.type, s.qmode, presentation::liveFrequency (s, m, running), s.q,
                 presentation::liveGainDb (s, m, running) };
    }

    /** `count` frequencies from 20 Hz to 20 kHz, equal ratios apart. */
    inline std::vector<double> curveFrequencies (int count)
    {
        std::vector<double> hz (static_cast<std::size_t> (std::max (2, count)));
        const auto last = static_cast<double> (hz.size() - 1);

        for (std::size_t i = 0; i < hz.size(); ++i)
            hz[i] = dsp::minFrequency * std::pow (dsp::maxFrequency / dsp::minFrequency, static_cast<double> (i) / last);

        return hz;
    }

    /** One band's response at each frequency, in dB; 0 for a band that is off. */
    inline void bandResponseDb (const CurveBand& band, double sampleRate, const std::vector<double>& hz,
                                std::vector<double>& db)
    {
        db.assign (hz.size(), 0.0);

        if (! band.on)
            return;

        // Before the host has prepared the plugin there is no rate: draw at 48 kHz.
        const auto rate = sampleRate > 0.0 ? sampleRate : 48000.0;

        dsp::Warp warp;
        warp.prepare (rate);
        const auto c = dsp::staticBandCoefficients (band.type, band.qmode, band.hz, band.q, band.gainDb, warp);

        if (dsp::isIdentity (c))
            return;

        for (std::size_t i = 0; i < hz.size(); ++i)
            db[i] = dsp::svfMagnitudeDb (c, hz[i], rate);
    }

    /** The sum of the bands that are on, in dB, not below the response's floor. */
    inline void responseDb (const CurveBands& bands, double sampleRate, const std::vector<double>& hz,
                            std::vector<double>& db)
    {
        db.assign (hz.size(), 0.0);
        std::vector<double> one;

        for (const auto& band : bands)
        {
            if (! band.on)
                continue;

            bandResponseDb (band, sampleRate, hz, one);

            for (std::size_t i = 0; i < hz.size(); ++i)
                db[i] = std::max (dsp::responseFloorDb, db[i] + one[i]);
        }
    }
}
