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
    THE CURVE'S GEOMETRY (PRD, Editor; devplan 6a): where a frequency and a
    gain are on the plot, what a press lands on, and what a drag makes of a
    value. Plain numbers, so headless tests hold it
    (tests/editor/CurveGeometryTests.cpp).

    THE PLOT is 20 Hz to 20 kHz, logarithmic, by -33 to +33 dB: the band's
    own +-30 dB with room for a node at the limit.

    A HANDLE is a node and its crosshair (PRD: the horizontal arm drags
    frequency only, the vertical arm gain only). The arms start where the
    node's hit area ends and are as long as `armReach` from the centre, so a
    handle's footprint is a plus sign. A type without a gain has no vertical
    arm. Hit areas are sized so that, at the default window size and with the
    eight default frequencies, no point belongs to two bands: a test says so.

    A DRAG IS RELATIVE to the value at the press, never a jump to the
    pointer and never taken from a value the drag itself keeps moving
    (learned in Go.dot's EQ, commit 8f63efcf).

    No JUCE here.
*/

#include <dsp/Gain.h>
#include <dsp/Parameters.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace likyu::editor::geometry
{
    /** The plot's vertical extent, either way, in dB. */
    inline constexpr double plotMaxDb = 33.0;

    /** A fine drag (Shift) moves a tenth as far. */
    inline constexpr double fineFactor = 0.1;

    /** The plot's area, in its component's coordinates. */
    struct Plot
    {
        double x = 0.0, y = 0.0, width = 1.0, height = 1.0;

        double right() const noexcept   { return x + width; }
        double bottom() const noexcept  { return y + height; }
    };

    /** The curve's size in a window of the default size, and the room the
        axis labels take around the plot. */
    inline constexpr double defaultCurveWidth = 960.0;
    inline constexpr double defaultCurveHeight = 380.0;
    inline constexpr double marginLeft = 44.0, marginTop = 12.0, marginRight = 16.0, marginBottom = 28.0;

    /** The plot inside a curve component of this size. */
    constexpr Plot plotWithin (double width, double height) noexcept
    {
        return { marginLeft, marginTop,
                 width > marginLeft + marginRight ? width - marginLeft - marginRight : 0.0,
                 height > marginTop + marginBottom ? height - marginTop - marginBottom : 0.0 };
    }

    inline const double decades = std::log10 (dsp::maxFrequency / dsp::minFrequency);

    inline double xOfHz (const Plot& plot, double hz) noexcept
    {
        const auto clamped = std::clamp (hz, dsp::minFrequency, dsp::maxFrequency);
        return plot.x + plot.width * std::log10 (clamped / dsp::minFrequency) / decades;
    }

    inline double hzOfX (const Plot& plot, double x) noexcept
    {
        const auto t = plot.width > 0.0 ? std::clamp ((x - plot.x) / plot.width, 0.0, 1.0) : 0.0;
        return dsp::minFrequency * std::pow (10.0, t * decades);
    }

    inline double yOfDb (const Plot& plot, double db) noexcept
    {
        const auto clamped = std::clamp (db, -plotMaxDb, plotMaxDb);
        return plot.y + plot.height * (plotMaxDb - clamped) / (2.0 * plotMaxDb);
    }

    inline double dbOfY (const Plot& plot, double y) noexcept
    {
        const auto t = plot.height > 0.0 ? std::clamp ((y - plot.y) / plot.height, 0.0, 1.0) : 0.5;
        return plotMaxDb - t * 2.0 * plotMaxDb;
    }

    //==========================================================================
    /** Sizes in pixels. Radii are from the handle's centre. */
    struct Sizes
    {
        double node = 9.0;              // drawn radius
        double selectedNode = 11.0;
        double ghost = 6.0;
        double hit = 13.0;              // a press this near the centre takes the node
        double armReach = 36.0;         // where an arm ends
        double armTolerance = 7.0;      // how far off an arm's line a press may be
        double scale = 1.0;
    };

    /** The sizes for a plot of this height. A touch has larger hit areas and
        the same drawing. The plot's height sets the scale, as in WFS-DIY's EQ. */
    inline Sizes sizesFor (double plotHeight, bool touch) noexcept
    {
        Sizes s;
        s.scale = std::clamp (plotHeight / 340.0, 0.8, 1.4);
        s.node *= s.scale;
        s.selectedNode *= s.scale;
        s.ghost *= s.scale;
        s.hit = (touch ? 18.0 : 13.0) * s.scale;
        s.armReach = 36.0 * s.scale;
        s.armTolerance = (touch ? 12.0 : 7.0) * s.scale;
        return s;
    }

    /** Where one band's handle and ghost are on the plot. */
    struct Place
    {
        bool shown = false;
        double x = 0.0, y = 0.0;
        bool gainArm = false;

        bool ghost = false;
        double ghostX = 0.0, ghostY = 0.0;
    };

    using Places = std::array<Place, dsp::numBands>;

    enum class Part { none, node, frequencyArm, gainArm, ghost };

    struct Hit
    {
        Part part = Part::none;
        int band = -1;
    };

    /** What of this band's handle a point is on. The node before the ghost,
        the ghost before the arms. */
    inline Part partAt (const Place& place, double px, double py, const Sizes& sizes) noexcept
    {
        if (place.shown && std::hypot (px - place.x, py - place.y) <= sizes.hit)
            return Part::node;

        if (place.ghost && std::hypot (px - place.ghostX, py - place.ghostY) <= sizes.hit)
            return Part::ghost;

        if (! place.shown)
            return Part::none;

        const auto dx = std::abs (px - place.x), dy = std::abs (py - place.y);

        if (dy <= sizes.armTolerance && dx <= sizes.armReach)
            return Part::frequencyArm;

        if (place.gainArm && dx <= sizes.armTolerance && dy <= sizes.armReach)
            return Part::gainArm;

        return Part::none;
    }

    /** What a press lands on. Any node wins over any ghost, and any ghost
        over any arm; among equals the nearest centre, and at equal distance
        the selected band, which is the one drawn on top. */
    inline Hit hitTest (const Places& places, int selected, double px, double py, const Sizes& sizes) noexcept
    {
        Hit best;
        auto bestRank = 0;
        auto bestDistance = 0.0;

        for (int band = 0; band < dsp::numBands; ++band)
        {
            const auto& place = places[static_cast<std::size_t> (band)];
            const auto part = partAt (place, px, py, sizes);

            if (part == Part::none)
                continue;

            const auto rank = part == Part::node ? 3 : part == Part::ghost ? 2 : 1;
            const auto distance = part == Part::ghost ? std::hypot (px - place.ghostX, py - place.ghostY)
                                                      : std::hypot (px - place.x, py - place.y);

            const auto better = rank > bestRank
                                || (rank == bestRank && (distance < bestDistance
                                                         || (! (distance > bestDistance) && band == selected)));

            if (better)
            {
                best = { part, band };
                bestRank = rank;
                bestDistance = distance;
            }
        }

        return best;
    }

    //==========================================================================
    /** A frequency after the pointer moved `dx` pixels from the press. */
    inline double draggedHz (double fromHz, double dx, const Plot& plot, bool fine) noexcept
    {
        if (! (plot.width > 0.0))
            return std::clamp (fromHz, dsp::minFrequency, dsp::maxFrequency);

        const auto moved = (fine ? fineFactor : 1.0) * dx / plot.width * decades;
        return std::clamp (fromHz * std::pow (10.0, moved), dsp::minFrequency, dsp::maxFrequency);
    }

    /** A gain after the pointer moved `dy` pixels from the press (down is less). */
    inline double draggedDb (double fromDb, double dy, const Plot& plot, bool fine) noexcept
    {
        if (! (plot.height > 0.0))
            return std::clamp (fromDb, -dsp::maxGainDb, dsp::maxGainDb);

        const auto moved = (fine ? fineFactor : 1.0) * dy / plot.height * 2.0 * plotMaxDb;
        return std::clamp (fromDb - moved, -dsp::maxGainDb, dsp::maxGainDb);
    }

    /** Low and High carried together by `dx` pixels, their ratio kept: the
        move stops where either would leave the table's range. */
    inline std::pair<double, double> draggedRange (double fromLow, double fromHigh, double dx,
                                                   const Plot& plot, bool fine) noexcept
    {
        const auto low = std::clamp (std::min (fromLow, fromHigh), dsp::minFrequency, dsp::maxFrequency);
        const auto high = std::clamp (std::max (fromLow, fromHigh), dsp::minFrequency, dsp::maxFrequency);

        if (! (plot.width > 0.0))
            return { low, high };

        const auto moved = (fine ? fineFactor : 1.0) * dx / plot.width * decades;
        const auto ratio = std::clamp (std::pow (10.0, moved), dsp::minFrequency / low, dsp::maxFrequency / high);
        return { low * ratio, high * ratio };
    }
}
