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

#include <algorithm>
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

TEST_CASE ("dispatch: a band's detector and Dynamic values, and the link, as the chain takes them")
{
    using P = dsp::BandParam;

    auto values = defaults();
    band (values, 4, P::gmode) = 1.0f;
    band (values, 4, P::key) = 1.0f;
    band (values, 4, P::dfilt) = 1.0f;
    band (values, 4, P::dtype) = 2.0f;
    band (values, 4, P::dfreq) = 250.0f;
    band (values, 4, P::dq) = 4.0f;
    band (values, 4, P::range) = 9.0f;
    band (values, 4, P::thresh) = -36.0f;
    band (values, 4, P::ratio) = 8.0f;
    band (values, 4, P::knee) = 12.0f;
    band (values, 4, P::atk) = 0.5f;
    band (values, 4, P::rel) = 400.0f;
    band (values, 4, P::ddir) = 1.0f;

    const auto s = core::bandSettingsFrom (values, 4);
    CHECK (s.gmode == dsp::GainMode::dynamic);
    CHECK (s.key == dsp::KeySource::sidechain);
    CHECK (s.dfilt == dsp::DetectorFilter::own);
    CHECK (s.dtype == dsp::DetectorType::lowPass);
    CHECK (s.detectorFrequency == 250.0);
    CHECK (s.detectorQ == 4.0);
    CHECK (s.rangeDb == 9.0);
    CHECK (s.thresholdDb == -36.0);
    CHECK (s.ratio == 8.0);
    CHECK (s.kneeDb == 12.0);
    CHECK (s.attackMs == doctest::Approx (0.5));
    CHECK (s.releaseMs == 400.0);
    CHECK (s.direction == dsp::Direction::below);

    // Out of range, rounded and clamped like every choice.
    band (values, 4, P::gmode) = 7.0f;
    CHECK (core::bandSettingsFrom (values, 4).gmode == dsp::GainMode::transient);

    // The defaults: Static, Self, Follow, the table's Dynamic values.
    const auto d = core::bandSettingsFrom (defaults(), 0);
    CHECK (d.gmode == dsp::GainMode::staticGain);
    CHECK (d.key == dsp::KeySource::self);
    CHECK (d.dfilt == dsp::DetectorFilter::follow);
    CHECK (d.rangeDb == -6.0);
    CHECK (d.thresholdDb == -24.0);
    CHECK (d.direction == dsp::Direction::above);

    auto global = defaults();
    CHECK (core::chainSettingsFrom (global).linked);
    global[static_cast<std::size_t> (dsp::indexOf (dsp::GlobalParam::link))] = 1.0f;
    CHECK_FALSE (core::chainSettingsFrom (global).linked);
}

TEST_CASE ("dispatch: the link, when it moves, reaches the chain")
{
    // Stereo, a loud left: linked, a dynamic band cuts the right as much as the left.
    auto values = defaults();
    band (values, 0, dsp::BandParam::on) = 1.0f;
    band (values, 0, dsp::BandParam::gmode) = 1.0f;
    band (values, 0, dsp::BandParam::freq) = 1000.0f;
    band (values, 0, dsp::BandParam::thresh) = -40.0f;

    dsp::Chain chain;
    chain.prepare (48000.0, 2);
    core::Dispatch dispatch;

    std::vector<double> left (4800), right (4800, 0.0);
    double* channels[] = { left.data(), right.data() };

    const auto block = [&]
    {
        for (std::size_t n = 0; n < left.size(); ++n)
            left[n] = 0.5 * std::sin (2.0 * 3.14159265358979323846 * 1000.0 * static_cast<double> (n) / 48000.0);

        std::fill (right.begin(), right.end(), 0.0);
        dispatch.apply (values, chain);
        chain.process (channels, 2, static_cast<int> (left.size()));
    };

    block();
    CHECK (chain.offsetDb (0, 1) < -3.0);

    values[static_cast<std::size_t> (dsp::indexOf (dsp::GlobalParam::link))] = 1.0f;

    for (int i = 0; i < 20; ++i)
        block();

    CHECK (chain.offsetDb (0, 0) < -3.0);
    CHECK (chain.offsetDb (0, 1) == 0.0);
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
