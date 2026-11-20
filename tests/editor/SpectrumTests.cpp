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
    The analyser's spectrum (editor/Spectrum.h): a full-scale sine reads
    0 dB at 1 kHz and its tilted level elsewhere, wherever it falls between
    bins; nothing reads the floor; the picture falls at its stated rate.
*/

#include <doctest/doctest.h>

#include <editor/ResponseCurve.h>
#include <editor/Spectrum.h>

#include <cmath>
#include <utility>
#include <vector>

using namespace likyu::editor;

namespace
{
    constexpr double rate = 48000.0;

    std::vector<float> sine (double hz, double amplitude)
    {
        std::vector<float> out (static_cast<std::size_t> (Spectrum::size));

        for (int n = 0; n < Spectrum::size; ++n)
            out[static_cast<std::size_t> (n)] = static_cast<float> (amplitude * std::sin (2.0 * 3.14159265358979323846 * hz * n / rate));

        return out;
    }

    /** The level shown at `hz` for `samples`. */
    double levelAt (Spectrum& spectrum, const std::vector<float>& samples, double hz)
    {
        std::vector<double> db;
        Spectrum::levelsAt (spectrum.analyse (samples.data()), rate, { hz }, db);
        return db[0];
    }
}

TEST_CASE ("spectrum: a full-scale sine reads its tilted level at its frequency, and far below it elsewhere")
{
    Spectrum spectrum;

    // On a bin and between two: within the Hann window's 1.4 dB.
    for (const auto hz : { 1000.0, 1000.0 + 0.5 * rate / Spectrum::size })
    {
        const auto signal = sine (hz, 1.0);
        INFO (hz << " Hz");
        CHECK (levelAt (spectrum, signal, hz) <= 0.01);
        CHECK (levelAt (spectrum, signal, hz) > -1.5);
        CHECK (levelAt (spectrum, signal, 300.0) < -80.0);
        CHECK (levelAt (spectrum, signal, 4000.0) < -80.0);
    }

    // The tilt: three octaves under 1 kHz, 9 dB under; two over, 6 dB over. Neither is on a bin at
    // 48 kHz: the Hann window's 1.4 dB between bins again.
    for (const auto [hz, tilted] : { std::pair { 125.0, -9.0 }, std::pair { 4000.0, 6.0 } })
    {
        const auto level = levelAt (spectrum, sine (hz, 1.0), hz);
        INFO (hz << " Hz: " << level << " dB");
        CHECK (level <= tilted + 0.01);
        CHECK (level > tilted - 1.5);
    }

    // 20 dB down is 20 dB down.
    CHECK (levelAt (spectrum, sine (1000.0, 0.1), 1000.0) == doctest::Approx (-20.0).epsilon (0.05));

    // Silence is the floor; past Nyquist or without a rate, the floor too.
    const std::vector<float> silence (static_cast<std::size_t> (Spectrum::size), 0.0f);
    CHECK (levelAt (spectrum, silence, 1000.0) == Spectrum::floorDb);

    std::vector<double> db;
    Spectrum::levelsAt (spectrum.analyse (sine (1000.0, 1.0).data()), 0.0, { 1000.0 }, db);
    CHECK (db[0] == Spectrum::floorDb);
    Spectrum::levelsAt (spectrum.analyse (sine (1000.0, 1.0).data()), rate, { 30000.0 }, db);
    CHECK (db[0] == Spectrum::floorDb);
}

TEST_CASE ("spectrum: at the curve's frequencies, the low end interpolated between bins, every level finite")
{
    Spectrum spectrum;
    const auto hz = curveFrequencies (1357);
    std::vector<double> db;
    Spectrum::levelsAt (spectrum.analyse (sine (40.0, 0.5).data()), rate, hz, db);

    REQUIRE (db.size() == hz.size());

    auto peak = Spectrum::floorDb;
    auto at = 0.0;

    for (std::size_t i = 0; i < db.size(); ++i)
    {
        CHECK (std::isfinite (db[i]));
        CHECK (db[i] >= Spectrum::floorDb);

        if (db[i] > peak)
        {
            peak = db[i];
            at = hz[i];
        }
    }

    // A 40 Hz sine, between bins 6.8 Hz apart: the peak within a semitone of it.
    CHECK (std::abs (1200.0 * std::log2 (at / 40.0)) < 100.0);
    CHECK (peak == doctest::Approx (20.0 * std::log10 (0.5) + 3.0 * std::log2 (40.0 / 1000.0)).epsilon (0.15));
}

TEST_CASE ("spectrum: what is shown rises at once and falls at its stated rate")
{
    std::vector<double> shown;
    Spectrum::fall ({ -10.0, -50.0 }, 0.0, shown);
    CHECK (shown == std::vector<double> { -10.0, -50.0 });

    // Half a second on, both gone quiet: 12 dB down; the rise is at once.
    Spectrum::fall ({ -100.0, -20.0 }, 0.5, shown);
    CHECK (shown[0] == doctest::Approx (-10.0 - 0.5 * Spectrum::fallDbPerSecond));
    CHECK (shown[1] == -20.0);

    // Never below what is there now.
    Spectrum::fall ({ -30.0, -100.0 }, 10.0, shown);
    CHECK (shown[0] == -30.0);
}
