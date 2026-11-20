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

#include "CurveView.h"

#include "Palette.h"
#include "QGestures.h"
#include "ResponseCurve.h"

#include <dsp/Math.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace likyu::editor
{
    namespace
    {
        using geometry::Part;
        using P = dsp::BandParam;

        /** The live line is drawn when it is this far from the static one somewhere. */
        constexpr double liveVisibleDb = 0.05;

        float px (double v) noexcept
        {
            return static_cast<float> (v);
        }

        /** What of the meters the picture shows. The block counter is not:
            it moves at every block, and the picture need not. */
        bool sameMeters (const core::Meters& a, const core::Meters& b) noexcept
        {
            if ((a.block > 0) != (b.block > 0) || a.listening != b.listening)
                return false;

            for (std::size_t i = 0; i < a.bands.size(); ++i)
            {
                const auto& x = a.bands[i];
                const auto& y = b.bands[i];

                if (! (dsp::exactlyEqual (x.offsetDb, y.offsetDb) && dsp::exactlyEqual (x.frequencyHz, y.frequencyHz)
                       && dsp::exactlyEqual (x.confidence, y.confidence) && x.keyFellBack == y.keyFellBack))
                    return false;
            }

            return true;
        }

        juce::String hertzLabel (int hz)
        {
            return hz >= 1000 ? juce::String (hz / 1000) + "k" : juce::String (hz);
        }

        juce::Font fontOf (double height, bool bold = false)
        {
            return juce::Font (juce::FontOptions (px (height), bold ? juce::Font::bold : juce::Font::plain));
        }
    }

    //==========================================================================
    CurveView::CurveView (Host& hostIn)
        : host (hostIn)
    {
        setOpaque (true);
    }

    CurveView::~CurveView()
    {
        endAllDrags();
    }

    CurveView::PointerKey CurveView::keyOf (const juce::MouseEvent& e) noexcept
    {
        return { e.source.isTouch(), e.source.getIndex() };
    }

    void CurveView::selectBand (int band)
    {
        const auto clamped = std::clamp (band, 0, dsp::numBands - 1);

        if (clamped != selected)
        {
            selected = clamped;
            rebuild();      // the ghost that takes a press is the selected band's
            repaint();
        }
    }

    //==========================================================================
    void CurveView::refresh()
    {
        core::Dispatch::Values now {};

        for (int i = 0; i < dsp::numParams; ++i)
            now[static_cast<std::size_t> (i)] = static_cast<float> (host.valueOf (i));

        core::Meters metered;
        host.readMeters (metered);
        const auto rateNow = host.sampleRate();

        if (read && std::memcmp (now.data(), values.data(), sizeof (float) * now.size()) == 0
            && sameMeters (metered, meters) && dsp::exactlyEqual (rateNow, rate))
            return;

        values = now;
        meters = metered;
        rate = rateNow;
        read = true;

        rebuild();
        repaint();
    }

    void CurveView::resized()
    {
        rebuild();
    }

    void CurveView::rebuild()
    {
        plot = geometry::plotWithin (getWidth(), getHeight());
        const auto running = presentation::metersRunning (meters);

        CurveBands still, live;

        for (int band = 0; band < dsp::numBands; ++band)
        {
            const auto i = static_cast<std::size_t> (band);
            settings[i] = core::bandSettingsFrom (values, band);

            const auto& s = settings[i];
            const auto& m = meters.bands[i];
            const auto node = presentation::nodeOf (s, m, running);

            auto& place = places[i];
            place = {};
            place.shown = true;
            place.gainArm = node.hasGain;
            place.x = geometry::xOfHz (plot, node.hz);
            place.y = geometry::yOfDb (plot, node.gainDb);

            // The ghost, where an Own detector listens, level with its node. Only the selected band's takes a press.
            const auto ghost = presentation::ghostOf (s);
            place.ghost = ghost.shown && s.on && band == selected;
            place.ghostX = geometry::xOfHz (plot, ghost.hz);
            place.ghostY = place.y;

            still[i] = staticBandOf (s);
            live[i] = liveBandOf (s, m, running);
        }

        // One point per pixel of the plot.
        const auto points = static_cast<int> (plot.width) + 1;

        if (points < 2)
        {
            hz.clear();
            staticDb.clear();
            liveDb.clear();
            liveDiffers = false;
            return;
        }

        if (static_cast<int> (hz.size()) != points)
            hz = curveFrequencies (points);

        responseDb (still, rate, hz, staticDb);
        responseDb (live, rate, hz, liveDb);

        liveDiffers = false;

        for (std::size_t i = 0; i < hz.size() && ! liveDiffers; ++i)
            liveDiffers = std::abs (liveDb[i] - staticDb[i]) > liveVisibleDb;
    }

    //==========================================================================
    void CurveView::paint (juce::Graphics& g)
    {
        g.fillAll (palette::background);

        if (! (plot.width > 1.0) || ! (plot.height > 1.0))
            return;

        paintGrid (g);
        paintLines (g);

        // The selected band last: it is the one a press on two nodes takes.
        for (int band = 0; band < dsp::numBands; ++band)
            if (band != selected)
                paintBand (g, band);

        paintBand (g, selected);
    }

    void CurveView::paintGrid (juce::Graphics& g) const
    {
        const juce::Rectangle<float> area (px (plot.x), px (plot.y), px (plot.width), px (plot.height));
        const auto scale = geometry::sizesFor (plot.height, false).scale;

        g.setColour (palette::plot);
        g.fillRect (area);
        g.setFont (fontOf (std::max (9.0, 10.0 * scale)));

        // Frequency: every line of each decade, the decades stronger.
        for (const auto decade : { 10, 100, 1000, 10000 })
            for (int m = 1; m <= 9; ++m)
            {
                const auto f = decade * m;

                if (f < 20 || f > 20000)
                    continue;

                g.setColour (palette::gridLine.withAlpha (m == 1 ? 0.6f : 0.3f));
                g.fillRect (px (geometry::xOfHz (plot, f)), area.getY(), 1.0f, area.getHeight());
            }

        g.setColour (palette::textSecondary);

        for (const auto f : { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 })
        {
            const auto x = px (geometry::xOfHz (plot, f));
            g.drawText (hertzLabel (f), juce::Rectangle<float> (x - 20.0f, area.getBottom() + 5.0f, 40.0f, 14.0f),
                        juce::Justification::centredTop, false);
        }

        // Gain: every 6 dB, 0 dB stronger.
        for (int db = -30; db <= 30; db += 6)
        {
            const auto y = px (geometry::yOfDb (plot, db));

            g.setColour (palette::gridLine.withAlpha (db == 0 ? 0.8f : 0.4f));
            g.fillRect (area.getX(), y, area.getWidth(), 1.0f);

            g.setColour (palette::textSecondary);
            g.drawText ((db > 0 ? "+" : "") + juce::String (db),
                        juce::Rectangle<float> (0.0f, y - 7.0f, area.getX() - 6.0f, 14.0f),
                        juce::Justification::centredRight, false);
        }
    }

    void CurveView::paintLines (juce::Graphics& g) const
    {
        if (hz.size() < 2 || staticDb.size() != hz.size() || liveDb.size() != hz.size())
            return;

        const juce::Rectangle<float> area (px (plot.x), px (plot.y), px (plot.width), px (plot.height));
        const juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (area.getSmallestIntegerContainer());

        const auto last = static_cast<double> (hz.size() - 1);

        const auto pathOf = [this, last] (const std::vector<double>& db)
        {
            juce::Path path;

            for (std::size_t i = 0; i < db.size(); ++i)
            {
                const auto x = px (plot.x + plot.width * static_cast<double> (i) / last);
                const auto y = px (geometry::yOfDb (plot, db[i]));

                if (i == 0)
                    path.startNewSubPath (x, y);
                else
                    path.lineTo (x, y);
            }

            return path;
        };

        const auto line = pathOf (staticDb);
        const auto zero = px (geometry::yOfDb (plot, 0.0));

        // The static response, filled to the 0 dB line as in WFS-DIY's EQ.
        auto fill = line;
        fill.lineTo (area.getRight(), zero);
        fill.lineTo (area.getX(), zero);
        fill.closeSubPath();

        g.setColour (palette::staticFill.withAlpha (0.2f));
        g.fillPath (fill);

        /*  The live response, only while it is somewhere else. Stroked first:
            where the two lines run together the static one is what shows, so
            the live colour appears exactly where the EQ is moving. */
        if (liveDiffers)
        {
            g.setColour (palette::liveLine);
            g.strokePath (pathOf (liveDb), juce::PathStrokeType (2.0f));
        }

        g.setColour (palette::staticLine);
        g.strokePath (line, juce::PathStrokeType (2.0f));
    }

    void CurveView::paintBand (juce::Graphics& g, int band) const
    {
        const auto i = static_cast<std::size_t> (band);
        const auto& s = settings[i];
        const auto& m = meters.bands[i];
        const auto& place = places[i];
        const auto running = presentation::metersRunning (meters);
        const auto node = presentation::nodeOf (s, m, running);
        const auto sizes = geometry::sizesFor (plot.height, false);
        const auto isSelected = band == selected;
        const auto isHovered = hovered.band == band && hovered.part != Part::none;

        const auto x = px (place.x), y = px (place.y);
        const auto scale = px (sizes.scale);
        const auto alpha = node.dimmed ? 0.4f : 1.0f;
        const auto colour = (node.on ? palette::band (band) : palette::band (band).darker (0.6f)).withMultipliedAlpha (alpha);

        // The crosshair: frequency only along the horizontal arm, gain only along the vertical.
        {
            const auto reach = px (sizes.armReach);
            const auto strong = isSelected || isHovered;
            g.setColour (colour.withMultipliedAlpha (strong ? 0.8f : 0.35f));
            g.fillRect (x - reach, y - 0.5f, 2.0f * reach, 1.0f);

            if (place.gainArm)
                g.fillRect (x - 0.5f, y - reach, 1.0f, 2.0f * reach);
        }

        // Env and Track: where the frequency may go, and where it rests.
        if (const auto bracket = presentation::horizontalBracketOf (s); bracket.shown && node.on)
        {
            const auto left = px (geometry::xOfHz (plot, bracket.lowHz));
            const auto right = px (geometry::xOfHz (plot, bracket.highHz));
            const auto rest = px (geometry::xOfHz (plot, bracket.restHz));
            const auto cap = 6.0f * scale;

            g.setColour (colour.withMultipliedAlpha (0.85f));
            g.fillRect (left, y - 1.0f, right - left, 2.0f);
            g.fillRect (left - 1.0f, y - cap, 2.0f, 2.0f * cap);
            g.fillRect (right - 1.0f, y - cap, 2.0f, 2.0f * cap);

            g.setColour (colour.withMultipliedAlpha (0.6f));
            g.fillRect (rest - 0.5f, y - 0.6f * cap, 1.0f, 1.2f * cap);
        }

        // Dynamic and Transient: where the gain may go, and where it is.
        if (const auto bracket = presentation::verticalBracketOf (s, m, running); bracket.shown && node.on)
        {
            const auto top = px (geometry::yOfDb (plot, bracket.highDb));
            const auto bottom = px (geometry::yOfDb (plot, bracket.lowDb));
            const auto mark = px (geometry::yOfDb (plot, bracket.markDb));
            const auto cap = 6.0f * scale;

            g.setColour (colour.withMultipliedAlpha (0.85f));
            g.fillRect (x - 1.0f, top, 2.0f, bottom - top);
            g.fillRect (x - cap, top - 1.0f, 2.0f * cap, 2.0f);
            g.fillRect (x - cap, bottom - 1.0f, 2.0f * cap, 2.0f);

            // The moving mark: a bar across the bracket at the gain now.
            g.setColour (palette::liveLine.withMultipliedAlpha (alpha));
            g.fillRoundedRectangle (x - 1.5f * cap, mark - 2.0f * scale, 3.0f * cap, 4.0f * scale, 2.0f * scale);
        }

        // The ghost: where the band's Own detector listens. Faint, until the band is the selected one.
        if (const auto ghost = presentation::ghostOf (s); ghost.shown && node.on)
        {
            const auto gx = px (geometry::xOfHz (plot, ghost.hz));
            const auto radius = px (sizes.ghost) + (isSelected ? 1.5f * scale : 0.0f);
            const auto onGhost = hovered.band == band && hovered.part == Part::ghost;
            const auto strength = isSelected ? 1.0f : 0.3f;

            // Tied to its node by a dotted line, so that it is read as that band's ear.
            if (isSelected)
            {
                const float dashes[] = { 3.0f, 4.0f };
                g.setColour (colour.withMultipliedAlpha (0.7f));
                g.drawDashedLine ({ x, y, gx, y }, dashes, 2, 1.0f);
            }

            g.setColour (palette::plot.withMultipliedAlpha (strength));
            g.fillEllipse (gx - radius, y - radius, 2.0f * radius, 2.0f * radius);
            g.setColour ((onGhost ? colour.brighter (0.4f) : colour).withMultipliedAlpha (strength));
            g.drawEllipse (gx - radius, y - radius, 2.0f * radius, 2.0f * radius, 2.0f * scale);
            g.fillEllipse (gx - 1.5f * scale, y - 1.5f * scale, 3.0f * scale, 3.0f * scale);
        }

        // The node.
        {
            const auto radius = px (isSelected ? sizes.selectedNode : sizes.node);

            g.setColour (isHovered ? colour.brighter (0.3f) : colour);
            g.fillEllipse (x - radius, y - radius, 2.0f * radius, 2.0f * radius);

            if (isSelected)
            {
                const auto ring = radius + 3.0f * scale;
                g.setColour (palette::textPrimary.withMultipliedAlpha (alpha));
                g.drawEllipse (x - ring, y - ring, 2.0f * ring, 2.0f * ring, 2.0f * scale);
            }

            g.setColour (node.on ? juce::Colours::black : palette::textPrimary.withAlpha (0.8f));
            g.setFont (fontOf (std::max (8.0, 12.0 * sizes.scale), true));
            g.drawText (juce::String (band + 1), juce::Rectangle<float> (x - radius, y - radius, 2.0f * radius, 2.0f * radius),
                        juce::Justification::centred, false);

            // The key badge: what the band is keyed by, and a warning when that is not what was asked.
            if (const auto badge = presentation::badgeOf (s, m, false); badge.key != presentation::Key::none && node.on)
            {
                g.setColour ((badge.warns ? palette::warning : palette::textSecondary).withMultipliedAlpha (alpha));
                g.setFont (fontOf (std::max (8.0, 9.0 * sizes.scale), badge.warns));
                g.drawText (juce::String (badge.text),
                            juce::Rectangle<float> (x + radius + 2.0f * scale, y - radius - 9.0f * scale, 30.0f, 11.0f * scale),
                            juce::Justification::centredLeft, false);
            }
        }
    }

    //==========================================================================
    geometry::Hit CurveView::hitAt (juce::Point<float> at, bool touch) const noexcept
    {
        return geometry::hitTest (places, selected, at.x, at.y, geometry::sizesFor (plot.height, touch));
    }

    geometry::Hit CurveView::hitAt (const juce::MouseEvent& e) const noexcept
    {
        return hitAt (e.position, e.source.isTouch());
    }

    bool CurveView::isDragged (int band) const noexcept
    {
        return std::any_of (drags.begin(), drags.end(), [band] (const auto& entry) { return entry.second.band == band; });
    }

    void CurveView::forEachParameter (const Drag& drag, const std::function<void (int index)>& call) const
    {
        if (drag.movesFrequency)
            call (dsp::indexOf (drag.band, P::freq));

        if (drag.movesRange)
        {
            call (dsp::indexOf (drag.band, P::flo));
            call (dsp::indexOf (drag.band, P::fhi));
        }

        if (drag.movesGain)
            call (dsp::indexOf (drag.band, P::gain));

        if (drag.movesDetector)
            call (dsp::indexOf (drag.band, P::dfreq));
    }

    /*  The drag starts again from here: at the press, and when Shift changes
        mid-drag, so that the node does not jump as the scale changes. The
        values are the parameters' own, read now: never the picture's, which
        for a Track band is where the tracker is. */
    void CurveView::anchor (Drag& drag, juce::Point<float> at, bool fine) const
    {
        drag.press = at;
        drag.fine = fine;
        drag.fromHz = host.valueOf (dsp::indexOf (drag.band, P::freq));
        drag.fromDb = host.valueOf (dsp::indexOf (drag.band, P::gain));
        drag.fromLow = host.valueOf (dsp::indexOf (drag.band, P::flo));
        drag.fromHigh = host.valueOf (dsp::indexOf (drag.band, P::fhi));
        drag.fromDetectorHz = host.valueOf (dsp::indexOf (drag.band, P::dfreq));
    }

    void CurveView::endDrag (const Drag& drag)
    {
        forEachParameter (drag, [this] (int index) { host.endGesture (index); });
    }

    void CurveView::endPinch()
    {
        if (pinch.has_value())
        {
            host.endGesture (dsp::indexOf (pinch->band, P::q));
            pinch.reset();
        }
    }

    void CurveView::endAllDrags()
    {
        for (const auto& entry : drags)
            endDrag (entry.second);

        drags.clear();
        endPinch();
    }

    void CurveView::mouseDown (const juce::MouseEvent& e)
    {
        // A pointer whose release never came is let go at the next lone press (Go.dot's EQ).
        if (juce::Desktop::getInstance().getNumDraggingMouseSources() <= 1)
            endAllDrags();

        pointerDown (keyOf (e), e.position, e.mods);
    }

    void CurveView::mouseDrag (const juce::MouseEvent& e)
    {
        pointerMoved (keyOf (e), e.position, e.mods);
    }

    void CurveView::mouseUp (const juce::MouseEvent& e)
    {
        pointerUp (keyOf (e));
    }

    void CurveView::pointerDown (PointerKey key, juce::Point<float> at, juce::ModifierKeys mods)
    {
        // Two fingers are pinching: a third is ignored.
        if (pinch.has_value())
            return;

        const auto touch = key.first;
        const auto hit = hitAt (at, touch);

        /*  A second finger: on a band a finger already holds, or on nothing
            while one finger holds a node, it pinches that band's Q. On
            another band's handle it is that band's own drag. */
        if (touch)
        {
            auto held = drags.end();
            auto touches = 0;

            for (auto it = drags.begin(); it != drags.end(); ++it)
            {
                if (! it->first.first)
                    continue;

                ++touches;

                if (hit.band >= 0 ? it->second.band == hit.band : true)
                    held = it;
            }

            const auto onHeldBand = hit.band >= 0 && held != drags.end() && held->second.band == hit.band;
            const auto onNothing = hit.part == Part::none && touches == 1 && held != drags.end();

            if (onHeldBand || onNothing)
            {
                Pinch p;
                p.band = held->second.band;
                p.first = held->first;
                p.second = key;
                p.a = held->second.last;
                p.b = at;
                p.fromQ = host.valueOf (dsp::indexOf (p.band, P::q));
                p.fromDistance = std::max (8.0, static_cast<double> (p.a.getDistanceFrom (p.b)));

                // The node's drag stops where the pinch starts.
                endDrag (held->second);
                drags.erase (held);

                host.beginGesture (dsp::indexOf (p.band, P::q));
                pinch = p;
                return;
            }
        }

        if (hit.band < 0 || hit.part == Part::none || isDragged (hit.band))
            return;

        if (hit.band != selected)
        {
            selectBand (hit.band);

            if (onSelect != nullptr)
                onSelect (selected);
        }

        const auto i = static_cast<std::size_t> (hit.band);
        const auto node = presentation::nodeOf (settings[i], meters.bands[i], presentation::metersRunning (meters));
        const auto sideways = hit.part == Part::node || hit.part == Part::frequencyArm;

        Drag drag;
        drag.band = hit.band;
        drag.part = hit.part;
        drag.movesRange = sideways && node.drag == presentation::FrequencyDrag::range;
        drag.movesFrequency = sideways && ! drag.movesRange;
        drag.movesGain = node.hasGain && (hit.part == Part::node || hit.part == Part::gainArm);
        drag.movesDetector = hit.part == Part::ghost;
        drag.last = at;
        anchor (drag, at, mods.isShiftDown());

        forEachParameter (drag, [this] (int index) { host.beginGesture (index); });
        drags[key] = drag;
    }

    void CurveView::pointerMoved (PointerKey key, juce::Point<float> at, juce::ModifierKeys mods)
    {
        if (pinch.has_value() && (key == pinch->first || key == pinch->second))
        {
            (key == pinch->first ? pinch->a : pinch->b) = at;

            const auto distance = std::max (1.0, static_cast<double> (pinch->a.getDistanceFrom (pinch->b)));
            host.setValue (dsp::indexOf (pinch->band, P::q), pinchedQ (pinch->fromQ, pinch->fromDistance, distance));
            refresh();
            return;
        }

        const auto found = drags.find (key);

        if (found == drags.end())
            return;

        auto& drag = found->second;
        drag.last = at;

        if (mods.isShiftDown() != drag.fine)
            anchor (drag, at, mods.isShiftDown());

        const auto dx = static_cast<double> (at.x - drag.press.x);
        const auto dy = static_cast<double> (at.y - drag.press.y);

        if (drag.movesFrequency)
            host.setValue (dsp::indexOf (drag.band, P::freq), geometry::draggedHz (drag.fromHz, dx, plot, drag.fine));

        if (drag.movesRange)
        {
            const auto [low, high] = geometry::draggedRange (drag.fromLow, drag.fromHigh, dx, plot, drag.fine);
            host.setValue (dsp::indexOf (drag.band, P::flo), low);
            host.setValue (dsp::indexOf (drag.band, P::fhi), high);
        }

        if (drag.movesGain)
            host.setValue (dsp::indexOf (drag.band, P::gain), geometry::draggedDb (drag.fromDb, dy, plot, drag.fine));

        if (drag.movesDetector)
            host.setValue (dsp::indexOf (drag.band, P::dfreq), geometry::draggedHz (drag.fromDetectorHz, dx, plot, drag.fine));

        refresh();
    }

    void CurveView::pointerUp (PointerKey key)
    {
        // Either finger of a pinch ends it; the other does nothing more until it too is lifted.
        if (pinch.has_value() && (key == pinch->first || key == pinch->second))
        {
            endPinch();
            return;
        }

        const auto found = drags.find (key);

        if (found == drags.end())
            return;

        endDrag (found->second);
        drags.erase (found);
    }

    void CurveView::mouseMove (const juce::MouseEvent& e)
    {
        const auto hit = hitAt (e);

        if (hit.band != hovered.band || hit.part != hovered.part)
        {
            hovered = hit;
            repaint();
        }

        switch (hit.part)
        {
            case Part::node:            setMouseCursor (juce::MouseCursor::DraggingHandCursor); break;
            case Part::frequencyArm:    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
            case Part::gainArm:         setMouseCursor (juce::MouseCursor::UpDownResizeCursor); break;
            case Part::ghost:           setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
            case Part::none:            setMouseCursor (juce::MouseCursor::NormalCursor); break;
        }
    }

    void CurveView::mouseExit (const juce::MouseEvent&)
    {
        if (hovered.part != Part::none)
        {
            hovered = {};
            repaint();
        }
    }

    void CurveView::mouseDoubleClick (const juce::MouseEvent& e)
    {
        const auto hit = hitAt (e);

        if (hit.part != Part::node || ! dsp::hasGain (settings[static_cast<std::size_t> (hit.band)].type))
            return;

        // A bell or a shelf back to 0 dB, as in Go.dot's EQ.
        const auto index = dsp::indexOf (hit.band, P::gain);
        endAllDrags();
        host.beginGesture (index);
        host.setValue (index, 0.0);
        host.endGesture (index);
        refresh();
    }

    void CurveView::setQ (int band, dsp::BandParam which, double q)
    {
        const auto index = dsp::indexOf (band, which);
        host.beginGesture (index);
        host.setValue (index, q);
        host.endGesture (index);
        refresh();
    }

    void CurveView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        if (! (std::abs (wheel.deltaY) > 0.0f))
            return;

        // The band under the pointer, else the selected one; over a ghost, its detector's Q.
        const auto hit = hitAt (e);
        const auto band = hit.band >= 0 ? hit.band : selected;
        const auto which = hit.part == Part::ghost ? P::dq : P::q;
        const auto q = host.valueOf (dsp::indexOf (band, which));

        // Ctrl + wheel is a Windows touchpad's pinch, the other way up.
        setQ (band, which, turnedQ (q, static_cast<double> (wheel.deltaY), e.mods.isCtrlDown(), e.mods.isShiftDown()));
    }

    void CurveView::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
    {
        const auto hit = hitAt (e);
        const auto band = hit.band >= 0 ? hit.band : selected;
        const auto which = hit.part == Part::ghost ? P::dq : P::q;

        setQ (band, which, magnifiedQ (host.valueOf (dsp::indexOf (band, which)), static_cast<double> (scaleFactor)));
    }
}
