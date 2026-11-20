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
    The curve's geometry, its Q gestures and its two lines
    (editor/CurveGeometry.h, QGestures.h, ResponseCurve.h; PRD, Editor;
    devplan 6a). The Q gestures' cases are Go.dot's (tests/ClientTests.cpp,
    "Q gestures"), with Likyu's range.
*/

#include <doctest/doctest.h>

#include <editor/CurveGeometry.h>
#include <editor/QGestures.h>
#include <editor/ResponseCurve.h>

#include <dsp/Parameters.h>

#include <algorithm>
#include <cmath>
#include <tuple>
#include <vector>

using namespace likyu;
using namespace likyu::editor;
using namespace likyu::editor::geometry;

namespace
{
    /** The plot at the editor's default size. */
    constexpr Plot defaultPlot = plotWithin (defaultCurveWidth, defaultCurveHeight);

    /** The eight bands as a new instance has them: the table's defaults, all at 0 dB. */
    Places defaultPlaces (const Plot& plot)
    {
        Places places;
        const auto& table = dsp::parameterTable();

        for (int band = 0; band < dsp::numBands; ++band)
        {
            const auto hz = table[static_cast<std::size_t> (dsp::indexOf (band, dsp::BandParam::freq))].defaultValue;
            auto& p = places[static_cast<std::size_t> (band)];
            p.shown = true;
            p.gainArm = true;
            p.x = xOfHz (plot, hz);
            p.y = yOfDb (plot, 0.0);
        }

        return places;
    }

    int bandsClaiming (const Places& places, double x, double y, const Sizes& sizes)
    {
        auto count = 0;

        for (const auto& place : places)
            count += partAt (place, x, y, sizes) != Part::none ? 1 : 0;

        return count;
    }
}

//==============================================================================
TEST_CASE ("geometry: frequency and gain on the plot, and back")
{
    const auto& p = defaultPlot;

    CHECK (xOfHz (p, 20.0) == doctest::Approx (p.x));
    CHECK (xOfHz (p, 20000.0) == doctest::Approx (p.right()));
    CHECK (xOfHz (p, 632.4555) == doctest::Approx (p.x + p.width / 2.0).epsilon (1.0e-6));
    CHECK (xOfHz (p, 200.0) - xOfHz (p, 20.0) == doctest::Approx (p.width / 3.0));

    CHECK (yOfDb (p, 0.0) == doctest::Approx (p.y + p.height / 2.0));
    CHECK (yOfDb (p, plotMaxDb) == doctest::Approx (p.y));
    CHECK (yOfDb (p, -plotMaxDb) == doctest::Approx (p.bottom()));

    // A node at the band's limit is inside the plot, not on its edge.
    CHECK (yOfDb (p, 30.0) > p.y + 10.0);
    CHECK (yOfDb (p, -30.0) < p.bottom() - 10.0);

    for (const auto hz : { 20.0, 31.5, 440.0, 1000.0, 12345.0, 20000.0 })
        CHECK (hzOfX (p, xOfHz (p, hz)) == doctest::Approx (hz).epsilon (1.0e-9));

    for (const auto db : { -30.0, -6.0, 0.0, 0.1, 18.0, 30.0 })
        CHECK (dbOfY (p, yOfDb (p, db)) == doctest::Approx (db).epsilon (1.0e-9));

    // Outside the plot: the edge.
    CHECK (xOfHz (p, 5.0) == doctest::Approx (p.x));
    CHECK (xOfHz (p, 90000.0) == doctest::Approx (p.right()));
    CHECK (hzOfX (p, -100.0) == doctest::Approx (20.0));
    CHECK (hzOfX (p, 5000.0) == doctest::Approx (20000.0));
    CHECK (dbOfY (p, -50.0) == doctest::Approx (plotMaxDb));

    // A plot with no size yet gives a number, not a NaN.
    const Plot empty { 0.0, 0.0, 0.0, 0.0 };
    CHECK (std::isfinite (hzOfX (empty, 10.0)));
    CHECK (std::isfinite (dbOfY (empty, 10.0)));
    CHECK (draggedHz (1000.0, 50.0, empty, false) == 1000.0);
    CHECK (draggedDb (3.0, 50.0, empty, false) == 3.0);
}

TEST_CASE ("geometry: what a press lands on")
{
    const auto sizes = sizesFor (defaultPlot.height, false);
    Places places;
    auto& a = places[2];
    a.shown = true;
    a.gainArm = true;
    a.x = 400.0;
    a.y = 180.0;

    CHECK (hitTest (places, -1, 400.0, 180.0, sizes).part == Part::node);
    CHECK (hitTest (places, -1, 400.0, 180.0, sizes).band == 2);
    CHECK (hitTest (places, -1, 400.0 + sizes.hit - 0.5, 180.0, sizes).part == Part::node);

    // The horizontal arm is frequency only, the vertical arm gain only (PRD).
    // Half a pixel inside each edge: the sizes are not whole numbers at every plot height.
    CHECK (hitTest (places, -1, 400.0 + sizes.hit + 4.0, 180.0, sizes).part == Part::frequencyArm);
    CHECK (hitTest (places, -1, 400.0 - sizes.armReach + 0.5, 180.0 + sizes.armTolerance - 0.5, sizes).part == Part::frequencyArm);
    CHECK (hitTest (places, -1, 400.0, 180.0 - sizes.hit - 4.0, sizes).part == Part::gainArm);
    CHECK (hitTest (places, -1, 400.0 + sizes.armTolerance - 0.5, 180.0 + sizes.armReach - 0.5, sizes).part == Part::gainArm);

    // Past an arm's end, or off its line: nothing.
    CHECK (hitTest (places, -1, 400.0 + sizes.armReach + 1.0, 180.0, sizes).part == Part::none);
    CHECK (hitTest (places, -1, 400.0 + 25.0, 180.0 + sizes.armTolerance + 1.0, sizes).part == Part::none);
    CHECK (hitTest (places, -1, 400.0 + 25.0, 180.0 + sizes.armTolerance + 1.0, sizes).band == -1);

    SUBCASE ("a type without a gain has no vertical arm")
    {
        a.gainArm = false;
        CHECK (hitTest (places, -1, 400.0, 180.0 - sizes.hit - 4.0, sizes).part == Part::none);
        CHECK (hitTest (places, -1, 400.0 + sizes.hit + 4.0, 180.0, sizes).part == Part::frequencyArm);
    }

    SUBCASE ("a node wins over another band's arm, wherever it is in the order")
    {
        auto& b = places[5];
        b.shown = true;
        b.gainArm = true;
        b.x = 400.0 + 24.0;     // on band 3's horizontal arm
        b.y = 180.0;

        const auto hit = hitTest (places, 2, b.x, b.y, sizes);
        CHECK (hit.part == Part::node);
        CHECK (hit.band == 5);
    }

    SUBCASE ("two nodes on one spot: the selected one, which is drawn on top")
    {
        auto& b = places[5];
        b = a;

        CHECK (hitTest (places, 5, 400.0, 180.0, sizes).band == 5);
        CHECK (hitTest (places, 2, 400.0, 180.0, sizes).band == 2);

        // Otherwise the nearer centre.
        b.x = 400.0 + 8.0;
        CHECK (hitTest (places, 2, 400.0 + 6.0, 180.0, sizes).band == 5);
        CHECK (hitTest (places, 5, 400.0 + 1.0, 180.0, sizes).band == 2);
    }

    SUBCASE ("the ghost: after any node, before any arm")
    {
        a.ghost = true;
        a.ghostX = 400.0 + 24.0;    // on its own band's horizontal arm
        a.ghostY = 180.0;

        const auto hit = hitTest (places, -1, a.ghostX, a.ghostY, sizes);
        CHECK (hit.part == Part::ghost);
        CHECK (hit.band == 2);

        // A band that is not shown may still show its ghost.
        a.shown = false;
        CHECK (hitTest (places, -1, a.ghostX, a.ghostY, sizes).part == Part::ghost);
        CHECK (hitTest (places, -1, 400.0, 180.0, sizes).part == Part::none);
    }

    SUBCASE ("a touch has larger hit areas")
    {
        const auto touch = sizesFor (defaultPlot.height, true);
        CHECK (touch.hit > sizes.hit);
        CHECK (touch.armTolerance > sizes.armTolerance);
        CHECK (touch.node == sizes.node);
        CHECK (hitTest (places, -1, 400.0 + sizes.hit + 3.0, 180.0, touch).part == Part::node);
    }
}

TEST_CASE ("geometry: at the default size no point belongs to two bands, by mouse or by touch")
{
    // PRD, Editor: arms and nodes have touch-sized hit areas that do not
    // overlap at the default window size.
    const auto places = defaultPlaces (defaultPlot);

    for (const auto touch : { false, true })
    {
        const auto sizes = sizesFor (defaultPlot.height, touch);
        auto worst = 0;
        auto claimed = 0;

        for (double y = defaultPlot.y; y <= defaultPlot.bottom(); y += 1.0)
            for (double x = defaultPlot.x - 40.0; x <= defaultPlot.right() + 40.0; x += 1.0)
            {
                const auto count = bandsClaiming (places, x, y, sizes);
                worst = std::max (worst, count);
                claimed += count;
            }

        INFO ((touch ? "touch" : "mouse"));
        CHECK (worst == 1);
        CHECK (claimed > 0);
    }
}

TEST_CASE ("geometry: a drag is relative to the value at the press")
{
    const auto& p = defaultPlot;

    // A third of the width is a decade.
    CHECK (draggedHz (100.0, p.width / 3.0, p, false) == doctest::Approx (1000.0));
    CHECK (draggedHz (1000.0, -p.width / 3.0, p, false) == doctest::Approx (100.0));
    CHECK (draggedHz (1000.0, 0.0, p, false) == 1000.0);

    // Fine: a tenth as far.
    CHECK (draggedHz (100.0, p.width / 3.0, p, true) == doctest::Approx (100.0 * std::pow (10.0, 0.1)));

    // Held at the range's ends.
    CHECK (draggedHz (15000.0, p.width, p, false) == 20000.0);
    CHECK (draggedHz (30.0, -p.width, p, false) == 20.0);

    // Up is more gain; the plot's height is 66 dB; the band's limit holds.
    CHECK (draggedDb (0.0, -p.height / 66.0 * 6.0, p, false) == doctest::Approx (6.0));
    CHECK (draggedDb (3.0, p.height / 66.0 * 6.0, p, false) == doctest::Approx (-3.0));
    CHECK (draggedDb (0.0, -p.height / 66.0 * 6.0, p, true) == doctest::Approx (0.6));
    CHECK (draggedDb (0.0, 0.0, p, false) == 0.0);
    CHECK (draggedDb (20.0, -p.height, p, false) == 30.0);
    CHECK (draggedDb (-20.0, p.height, p, false) == -30.0);

    // A drag across the plot and back ends where it began.
    const auto there = draggedHz (440.0, 123.0, p, false);
    CHECK (there != doctest::Approx (440.0));
    CHECK (draggedHz (440.0, 0.0, p, false) == doctest::Approx (440.0));
}

TEST_CASE ("geometry: Env's range is carried whole")
{
    const auto& p = defaultPlot;

    auto [low, high] = draggedRange (500.0, 2000.0, p.width / 3.0, p, false);
    CHECK (low == doctest::Approx (5000.0));
    CHECK (high == doctest::Approx (20000.0));

    // Stopped by the end it meets first, the ratio kept.
    std::tie (low, high) = draggedRange (500.0, 2000.0, p.width, p, false);
    CHECK (high == doctest::Approx (20000.0));
    CHECK (high / low == doctest::Approx (4.0));

    std::tie (low, high) = draggedRange (500.0, 2000.0, -p.width, p, false);
    CHECK (low == doctest::Approx (20.0));
    CHECK (high / low == doctest::Approx (4.0));

    // Given out of order: put in order.
    std::tie (low, high) = draggedRange (2000.0, 500.0, 0.0, p, false);
    CHECK (low == doctest::Approx (500.0));
    CHECK (high == doctest::Approx (2000.0));
}

TEST_CASE ("Q gestures")
{
    // Fingers closing narrow the band: Q rises.
    CHECK (pinchedQ (1.0, 100.0, 50.0) == doctest::Approx (2.0));
    CHECK (pinchedQ (1.0, 100.0, 200.0) == doctest::Approx (0.5));
    CHECK (pinchedQ (1.0, 100.0, 100.0) == doctest::Approx (1.0));
    CHECK (pinchedQ (1.0, 100.0, 1.0) == dsp::maxQ);
    CHECK (pinchedQ (1.0, 1.0, 1000.0) == dsp::minQ);
    CHECK (pinchedQ (3.0, 0.0, 50.0) == 3.0);
    CHECK (pinchedQ (3.0, 50.0, 0.0) == 3.0);

    // A wheel step of 0.1 is 10 %, 1 % when fine; a touchpad's pinch is the other way up.
    CHECK (turnedQ (1.0, 0.1, false, false) == doctest::Approx (1.1));
    CHECK (turnedQ (1.0, -0.1, false, false) == doctest::Approx (1.0 / 1.1));
    CHECK (turnedQ (1.0, 0.1, false, true) == doctest::Approx (1.01));
    CHECK (turnedQ (1.0, 0.1, true, false) == doctest::Approx (1.0 / 1.1));
    CHECK (turnedQ (1.0, 0.0, false, false) == 1.0);
    CHECK (turnedQ (39.0, 5.0, false, false) == dsp::maxQ);
    CHECK (turnedQ (0.11, -5.0, false, false) == dsp::minQ);

    // Up and back down is where it started.
    CHECK (turnedQ (turnedQ (2.0, 0.3, false, false), -0.3, false, false) == doctest::Approx (2.0));

    CHECK (magnifiedQ (2.0, 2.0) == doctest::Approx (1.0));
    CHECK (magnifiedQ (2.0, 0.5) == doctest::Approx (4.0));
    CHECK (magnifiedQ (2.0, 0.0) == 2.0);

    // A value that is no number is not passed on.
    CHECK (clampedQ (std::nan ("")) == 1.0);
}

//==============================================================================
TEST_CASE ("the two lines: the static line is the bands at rest, the live line the bands where the meters say")
{
    constexpr double rate = 48000.0;
    const auto hz = curveFrequencies (241);
    CHECK (hz.size() == 241);
    CHECK (hz.front() == doctest::Approx (20.0));
    CHECK (hz.back() == doctest::Approx (20000.0));
    CHECK (hz[80] == doctest::Approx (200.0));

    const auto nearest = [&hz] (double f)
    {
        std::size_t best = 0;

        for (std::size_t i = 0; i < hz.size(); ++i)
            if (std::abs (std::log (hz[i] / f)) < std::abs (std::log (hz[best] / f)))
                best = i;

        return best;
    };

    dsp::BandSettings s;
    s.on = true;
    s.frequency = 2000.0;
    s.gainDb = 6.0;
    s.q = 2.0;
    s.gmode = dsp::GainMode::dynamic;
    s.rangeDb = -12.0;

    core::BandMeters m;
    m.offsetDb = -9.0f;
    m.frequencyHz = 2000.0f;

    std::vector<double> still, live;
    CurveBands bands;

    SUBCASE ("no band on: 0 dB everywhere, exactly")
    {
        responseDb (bands, rate, hz, still);
        for (const auto db : still)
            CHECK (db == 0.0);
    }

    SUBCASE ("a dynamic bell: +6 dB at rest, -3 dB while it works")
    {
        bands[3] = staticBandOf (s);
        responseDb (bands, rate, hz, still);
        CHECK (still[nearest (2000.0)] == doctest::Approx (6.0).epsilon (0.01));
        CHECK (still[nearest (40.0)] == doctest::Approx (0.0).epsilon (0.01));

        bands[3] = liveBandOf (s, m, true);
        responseDb (bands, rate, hz, live);
        CHECK (live[nearest (2000.0)] == doctest::Approx (-3.0).epsilon (0.01));

        // No meters yet: the live line is the static one.
        bands[3] = liveBandOf (s, m, false);
        responseDb (bands, rate, hz, live);
        CHECK (live == still);
    }

    SUBCASE ("a Track band: at rest on the static line, at the tracked frequency on the live one")
    {
        s.gmode = dsp::GainMode::staticGain;
        s.fmode = dsp::FrequencyMode::track;
        s.lowHz = 1000.0;
        s.highHz = 8000.0;
        m.frequencyHz = 5000.0f;

        bands[0] = staticBandOf (s);
        responseDb (bands, rate, hz, still);
        bands[0] = liveBandOf (s, m, true);
        responseDb (bands, rate, hz, live);

        CHECK (still[nearest (2000.0)] == doctest::Approx (6.0).epsilon (0.01));
        CHECK (live[nearest (5000.0)] == doctest::Approx (6.0).epsilon (0.01));
        CHECK (live[nearest (2000.0)] < 3.0);
    }

    SUBCASE ("bands add in dB; a band that is off adds nothing")
    {
        bands[0] = staticBandOf (s);
        bands[1] = staticBandOf (s);
        responseDb (bands, rate, hz, still);
        CHECK (still[nearest (2000.0)] == doctest::Approx (12.0).epsilon (0.01));

        bands[1].on = false;
        responseDb (bands, rate, hz, still);
        CHECK (still[nearest (2000.0)] == doctest::Approx (6.0).epsilon (0.01));
    }

    SUBCASE ("no sample rate yet, and a notch: finite everywhere")
    {
        s.type = dsp::BandType::notch;
        bands[0] = staticBandOf (s);
        responseDb (bands, 0.0, hz, still);

        for (const auto db : still)
        {
            CHECK (std::isfinite (db));
            CHECK (db >= dsp::responseFloorDb);
        }

        CHECK (still[nearest (2000.0)] < -20.0);
    }
}
