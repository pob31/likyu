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
        return s;
    }

    dsp::ChainSettings chainSettingsFrom (const Dispatch::Values& values) noexcept
    {
        dsp::ChainSettings s;
        s.bypass = isOn (valueAt (values, dsp::indexOf (dsp::GlobalParam::bypass)));
        s.outputGainDb = static_cast<double> (valueAt (values, dsp::indexOf (dsp::GlobalParam::outGain)));
        return s;
    }

    void Dispatch::apply (const Values& values, dsp::Chain& chain) noexcept
    {
        const auto bypass = dsp::indexOf (dsp::GlobalParam::bypass);
        const auto outGain = dsp::indexOf (dsp::GlobalParam::outGain);

        if (first || ! same (&values[static_cast<std::size_t> (bypass)], &last[static_cast<std::size_t> (bypass)], 1)
                  || ! same (&values[static_cast<std::size_t> (outGain)], &last[static_cast<std::size_t> (outGain)], 1))
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
