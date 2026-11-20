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

/*
    Dispatch (core/Dispatch.h): the host's values, read once per block, to
    the chain's settings.
*/

#include <doctest/doctest.h>

#include <core/Dispatch.h>
#include <dsp/Parameters.h>

#include <cmath>
#include <cstring>
#include <vector>

using namespace likyu;

namespace
{
    core::Dispatch::Values defaults()
    {
        core::Dispatch::Values values {};
        const auto& table = dsp::parameterTable();

        for (std::size_t i = 0; i < values.size(); ++i)
            values[i] = static_cast<float> (table[i].defaultValue);

        return values;
    }

    float& band (core::Dispatch::Values& values, int b, dsp::BandParam p)
    {
        return values[static_cast<std::size_t> (dsp::indexOf (b, p))];
    }

    /** One block of noise through the chain, after `values` were dispatched. */
    std::vector<double> run (core::Dispatch& dispatch, dsp::Chain& chain, const core::Dispatch::Values& values)
    {
        std::vector<double> audio (256);
        unsigned seed = 3;

        for (auto& sample : audio)
        {
            seed = seed * 1664525u + 1013904223u;
            sample = static_cast<double> (seed >> 8) / 8388608.0 - 1.0;
        }

        dispatch.apply (values, chain);
        double* pointer = audio.data();
        chain.process (&pointer, 1, 256);
        return audio;
    }
}

TEST_CASE ("dispatch: a band's settings are its values, choices rounded and clamped, a toggle on from 0.5")
{
    auto values = defaults();
    band (values, 2, dsp::BandParam::on) = 0.6f;
    band (values, 2, dsp::BandParam::type) = 2.4f;
    band (values, 2, dsp::BandParam::freq) = 3150.0f;
    band (values, 2, dsp::BandParam::q) = 2.5f;
    band (values, 2, dsp::BandParam::gain) = -4.5f;
    band (values, 2, dsp::BandParam::qmode) = 1.0f;

    const auto s = core::bandSettingsFrom (values, 2);
    CHECK (s.on);
    CHECK (s.type == dsp::BandType::highShelf);
    CHECK (s.qmode == dsp::QMode::constant);
    CHECK (s.frequency == 3150.0);
    CHECK (s.q == 2.5);
    CHECK (s.gainDb == -4.5);

    band (values, 2, dsp::BandParam::type) = 99.0f;
    band (values, 2, dsp::BandParam::on) = 0.4f;
    CHECK (core::bandSettingsFrom (values, 2).type == dsp::BandType::bandPass);
    CHECK_FALSE (core::bandSettingsFrom (values, 2).on);

    // The defaults: every band off, a bell at its spread frequency.
    const auto d = core::bandSettingsFrom (defaults(), 0);
    CHECK_FALSE (d.on);
    CHECK (d.type == dsp::BandType::bell);
    CHECK (d.frequency == doctest::Approx (dsp::parameterTable()[static_cast<std::size_t> (dsp::indexOf (0, dsp::BandParam::freq))].defaultValue));

    auto global = defaults();
    global[static_cast<std::size_t> (dsp::indexOf (dsp::GlobalParam::bypass))] = 1.0f;
    global[static_cast<std::size_t> (dsp::indexOf (dsp::GlobalParam::outGain))] = -3.0f;
    CHECK (core::chainSettingsFrom (global).bypass);
    CHECK (core::chainSettingsFrom (global).outputGainDb == -3.0);
}

TEST_CASE ("dispatch: what moves reaches the chain")
{
    dsp::Chain chain, reference;
    chain.prepare (48000.0, 1);
    reference.prepare (48000.0, 1);
    core::Dispatch dispatch, referenceDispatch;

    // Defaults: every band off, the chain is a wire.
    auto values = defaults();
    const auto idle = run (dispatch, chain, values);
    CHECK (run (referenceDispatch, reference, values) == idle);

    // Band 4 on at +9 dB: the next block is not the input.
    band (values, 3, dsp::BandParam::on) = 1.0f;
    band (values, 3, dsp::BandParam::gain) = 9.0f;
    const auto moved = run (dispatch, chain, values);
    const auto movedReference = run (referenceDispatch, reference, values);
    CHECK (moved == movedReference);
    CHECK_FALSE (moved == idle);
}

TEST_CASE ("dispatch: after forget(), everything is handed over again")
{
    dsp::Chain a, b;
    a.prepare (48000.0, 1);
    b.prepare (48000.0, 1);
    core::Dispatch dispatch;

    auto values = defaults();
    band (values, 0, dsp::BandParam::on) = 1.0f;
    band (values, 0, dsp::BandParam::gain) = 6.0f;
    run (dispatch, a, values);

    // A second chain dispatched by the same Dispatch: nothing has moved, so nothing is sent...
    const auto silentHandOver = run (dispatch, b, values);

    // ...until forget(): then b gets the band too.
    dispatch.forget();
    dsp::Chain c;
    c.prepare (48000.0, 1);
    const auto fullHandOver = run (dispatch, c, values);

    CHECK_FALSE (silentHandOver == fullHandOver);
}
