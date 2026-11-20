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

#include "Dispatch.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace likyu::core
{
    namespace
    {
        float valueAt (const Dispatch::Values& values, int index) noexcept
        {
            return values[static_cast<std::size_t> (index)];
        }

        float bandValue (const Dispatch::Values& values, int band, dsp::BandParam p) noexcept
        {
            return valueAt (values, dsp::indexOf (band, p));
        }

        bool isOn (float value) noexcept
        {
            return value >= 0.5f;
        }

        int choiceIndex (float value, int count) noexcept
        {
            if (! std::isfinite (value))
                return 0;

            return std::clamp (static_cast<int> (std::lround (value)), 0, count - 1);
        }

        /** Bits, not values: -Wfloat-equal stays quiet, and NaN compares as itself. */
        bool same (const float* a, const float* b, int count) noexcept
        {
            return std::memcmp (a, b, static_cast<std::size_t> (count) * sizeof (float)) == 0;
        }
    }

    dsp::BandSettings bandSettingsFrom (const Dispatch::Values& values, int band) noexcept
    {
        using P = dsp::BandParam;

        dsp::BandSettings s;
        s.on = isOn (bandValue (values, band, P::on));
        s.type = static_cast<dsp::BandType> (choiceIndex (bandValue (values, band, P::type), dsp::numBandTypes));
        s.qmode = static_cast<dsp::QMode> (choiceIndex (bandValue (values, band, P::qmode), dsp::numQModes));
        s.frequency = static_cast<double> (bandValue (values, band, P::freq));
        s.q = static_cast<double> (bandValue (values, band, P::q));
        s.gainDb = static_cast<double> (bandValue (values, band, P::gain));

        s.gmode = static_cast<dsp::GainMode> (choiceIndex (bandValue (values, band, P::gmode), dsp::numGainModes));
        s.key = static_cast<dsp::KeySource> (choiceIndex (bandValue (values, band, P::key), dsp::numKeySources));
        s.dfilt = static_cast<dsp::DetectorFilter> (choiceIndex (bandValue (values, band, P::dfilt), dsp::numDetectorFilters));
        s.dtype = static_cast<dsp::DetectorType> (choiceIndex (bandValue (values, band, P::dtype), dsp::numDetectorTypes));
        s.detectorFrequency = static_cast<double> (bandValue (values, band, P::dfreq));
        s.detectorQ = static_cast<double> (bandValue (values, band, P::dq));
        s.rangeDb = static_cast<double> (bandValue (values, band, P::range));
        s.thresholdDb = static_cast<double> (bandValue (values, band, P::thresh));
        s.ratio = static_cast<double> (bandValue (values, band, P::ratio));
        s.kneeDb = static_cast<double> (bandValue (values, band, P::knee));
        s.attackMs = static_cast<double> (bandValue (values, band, P::atk));
        s.releaseMs = static_cast<double> (bandValue (values, band, P::rel));
        s.direction = static_cast<dsp::Direction> (choiceIndex (bandValue (values, band, P::ddir), dsp::numDirections));

        s.transientAttackPercent = static_cast<double> (bandValue (values, band, P::tatk));
        s.transientSustainPercent = static_cast<double> (bandValue (values, band, P::tsus));
        s.transientTime = static_cast<double> (bandValue (values, band, P::ttime));
        s.transientFloorDb = static_cast<double> (bandValue (values, band, P::tfloor));
        return s;
    }

    dsp::ChainSettings chainSettingsFrom (const Dispatch::Values& values) noexcept
    {
        dsp::ChainSettings s;
        s.bypass = isOn (valueAt (values, dsp::indexOf (dsp::GlobalParam::bypass)));
        s.outputGainDb = static_cast<double> (valueAt (values, dsp::indexOf (dsp::GlobalParam::outGain)));
        s.linked = choiceIndex (valueAt (values, dsp::indexOf (dsp::GlobalParam::link)), 2) == 0;
        return s;
    }

    void Dispatch::apply (const Values& values, dsp::Chain& chain) noexcept
    {
        const auto moved = [&values, this] (dsp::GlobalParam p)
        {
            const auto i = static_cast<std::size_t> (dsp::indexOf (p));
            return ! same (&values[i], &last[i], 1);
        };

        if (first || moved (dsp::GlobalParam::bypass) || moved (dsp::GlobalParam::outGain) || moved (dsp::GlobalParam::link))
            chain.setChain (chainSettingsFrom (values));

        for (int band = 0; band < dsp::numBands; ++band)
        {
            const auto start = static_cast<std::size_t> (dsp::indexOf (band, dsp::BandParam::on));

            if (first || ! same (&values[start], &last[start], dsp::numBandParams))
                chain.setBand (band, bandSettingsFrom (values, band));
        }

        last = values;
        first = false;
    }
}
