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
    WHAT A NODE, A BRACKET, A BADGE AND A STRIP SAY (PRD, Editor, last
    bullet; devplan 6a): pure functions over a band's settings and its
    meters, so that headless tests assert them (tests/editor/
    PresentationTests.cpp) and the components only paint what they return.
    The pattern is the OSCQuery plugin's SlotPresentation.h.

    No JUCE here, and no parameter ID: a parameter is a dsp::BandParam.

    THE WORDING is the author's to change, in this one place.

    WHICH PARTS OF A BAND ARE IN USE follows the DSP (dsp/Band.h, THE SHAPE):
    a type without a gain is Static whatever its gain mode says; a band reads
    a key when it is Dynamic, Transient, Env or Track; its detector filter
    shapes that key except in Track without a gain mode, where the tracker
    reads the key itself.
*/

#include <core/MeterSnapshot.h>
#include <dsp/Band.h>
#include <dsp/Gain.h>
#include <dsp/Parameters.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace likyu::editor::presentation
{
    using dsp::BandParam;

    //==========================================================================
    /** The gain mode in effect: Static for a type without a gain (PRD, non-goals). */
    inline dsp::GainMode gainModeInEffect (const dsp::BandSettings& s) noexcept
    {
        return dsp::hasGain (s.type) ? s.gmode : dsp::GainMode::staticGain;
    }

    /** Dynamic, Transient, Env or Track: the band reads a key. */
    inline bool readsKey (const dsp::BandSettings& s) noexcept
    {
        return gainModeInEffect (s) != dsp::GainMode::staticGain || s.fmode != dsp::FrequencyMode::fixed;
    }

    /** The detector filter shapes what the band reads: every keyed band but
        a Track band with no gain mode. */
    inline bool usesDetector (const dsp::BandSettings& s) noexcept
    {
        return gainModeInEffect (s) != dsp::GainMode::staticGain || s.fmode == dsp::FrequencyMode::env;
    }

    /** Low and High in order and inside the table's range (PRD, range rules). */
    inline std::pair<double, double> frequencyRange (const dsp::BandSettings& s) noexcept
    {
        const auto low = std::clamp (s.lowHz, dsp::minFrequency, dsp::maxFrequency);
        const auto high = std::clamp (s.highHz, dsp::minFrequency, dsp::maxFrequency);
        return low <= high ? std::pair { low, high } : std::pair { high, low };
    }

    /** Where the band sits with no key: its Frequency; in Track, that clamped
        into [Low, High]; in Env the start of the mapping, Low for Up and High
        for Down (decided 2026-11-20). */
    inline double restFrequency (const dsp::BandSettings& s) noexcept
    {
        const auto own = std::clamp (s.frequency, dsp::minFrequency, dsp::maxFrequency);
        const auto [low, high] = frequencyRange (s);

        switch (s.fmode)
        {
            case dsp::FrequencyMode::fixed:  return own;
            case dsp::FrequencyMode::track:  return std::clamp (own, low, high);
            case dsp::FrequencyMode::env:    return s.envDirection == dsp::EnvDirection::up ? low : high;
        }

        return own;
    }

    /** The meters say something: a block has been published since the editor
        opened or the processor started. */
    inline bool metersRunning (const core::Meters& meters) noexcept
    {
        return meters.block > 0;
    }

    /** Where the band is now: the meter's frequency for an Env or Track band
        that is on and metered, its rest otherwise. A Fixed band is drawn at
        its parameter, so that it follows the hand and not the smoothing. */
    inline double liveFrequency (const dsp::BandSettings& s, const core::BandMeters& m, bool running) noexcept
    {
        const auto metered = static_cast<double> (m.frequencyHz);

        if (s.on && running && s.fmode != dsp::FrequencyMode::fixed && std::isfinite (metered) && metered > 0.0)
            return std::clamp (metered, dsp::minFrequency, dsp::maxFrequency);

        return restFrequency (s);
    }

    /** Track's confidence against the band's threshold; true elsewhere. */
    inline bool confident (const dsp::BandSettings& s, const core::BandMeters& m, bool running) noexcept
    {
        if (s.fmode != dsp::FrequencyMode::track || ! s.on || ! running)
            return true;

        return static_cast<double> (m.confidence) * 100.0 >= s.confidencePercent;
    }

    /** The band's gain now, in dB: the static gain and the metered offset as
        the filter combines them (dsp/Band.h, coefficientsWithG), confidence
        scaling included. 0 for a type without a gain. */
    inline double liveGainDb (const dsp::BandSettings& s, const core::BandMeters& m, bool running) noexcept
    {
        if (! dsp::hasGain (s.type))
            return 0.0;

        const auto own = std::clamp (s.gainDb, -dsp::maxGainDb, dsp::maxGainDb);

        if (! s.on || ! running)
            return own;

        const auto scales = s.fmode == dsp::FrequencyMode::track && s.confidenceScalesGain;
        const auto scale = scales ? std::clamp (static_cast<double> (m.confidence), 0.0, 1.0) : 1.0;
        const auto offset = gainModeInEffect (s) != dsp::GainMode::staticGain ? static_cast<double> (m.offsetDb) : 0.0;

        if (! scales)
            return dsp::effectiveGainDb (own, std::isfinite (offset) ? offset : 0.0, s.rangeDb);

        const auto limit = std::abs (s.rangeDb);
        const auto scaled = own > 0.0 ? own * scale : own;
        const auto bounded = std::clamp (std::isfinite (offset) ? offset : 0.0, -limit, limit);
        return std::clamp (scaled + bounded * scale, -dsp::maxGainDb, dsp::maxGainDb);
    }

    //==========================================================================
    /** What a sideways drag of the node moves. In Env the band's Frequency is
        not used, so the drag carries Low and High together. */
    enum class FrequencyDrag { frequency, range };

    struct Node
    {
        bool on = false;            // off: drawn darker, still draggable
        bool hasGain = true;        // a gain arm, and a place off the 0 dB line
        bool dimmed = false;        // Track, under the confidence threshold
        bool live = false;          // Env or Track: it slides inside its bracket
        double hz = 1000.0;         // where it is drawn
        double gainDb = 0.0;        // where it is drawn: the static gain, or 0
        FrequencyDrag drag = FrequencyDrag::frequency;
    };

    inline Node nodeOf (const dsp::BandSettings& s, const core::BandMeters& m, bool running) noexcept
    {
        Node n;
        n.on = s.on;
        n.hasGain = dsp::hasGain (s.type);
        n.live = s.fmode != dsp::FrequencyMode::fixed;
        n.dimmed = ! confident (s, m, running);
        n.hz = liveFrequency (s, m, running);
        n.gainDb = n.hasGain ? std::clamp (s.gainDb, -dsp::maxGainDb, dsp::maxGainDb) : 0.0;
        n.drag = s.fmode == dsp::FrequencyMode::env ? FrequencyDrag::range : FrequencyDrag::frequency;
        return n;
    }

    //==========================================================================
    /** Dynamic and Transient: where the gain may go, and where it is. */
    struct VerticalBracket
    {
        bool shown = false;
        double restDb = 0.0;        // the static gain
        double lowDb = 0.0;         // the bracket's ends, within +-30 dB
        double highDb = 0.0;
        double markDb = 0.0;        // the gain now
    };

    inline VerticalBracket verticalBracketOf (const dsp::BandSettings& s, const core::BandMeters& m, bool running) noexcept
    {
        VerticalBracket b;
        const auto mode = gainModeInEffect (s);

        if (mode == dsp::GainMode::staticGain)
            return b;

        b.shown = true;
        b.restDb = std::clamp (s.gainDb, -dsp::maxGainDb, dsp::maxGainDb);

        // Dynamic: the Range's sign is the offset's. Transient: either way.
        const auto range = std::clamp (s.rangeDb, -dsp::maxRangeDb, dsp::maxRangeDb);
        const auto down = mode == dsp::GainMode::transient ? -std::abs (range) : std::min (range, 0.0);
        const auto up = mode == dsp::GainMode::transient ? std::abs (range) : std::max (range, 0.0);

        b.lowDb = std::clamp (b.restDb + down, -dsp::maxGainDb, dsp::maxGainDb);
        b.highDb = std::clamp (b.restDb + up, -dsp::maxGainDb, dsp::maxGainDb);
        b.markDb = liveGainDb (s, m, running);
        return b;
    }

    /** Env and Track: where the frequency may go, and where it rests. */
    struct HorizontalBracket
    {
        bool shown = false;
        double lowHz = 0.0;
        double highHz = 0.0;
        double restHz = 0.0;
    };

    inline HorizontalBracket horizontalBracketOf (const dsp::BandSettings& s) noexcept
    {
        HorizontalBracket b;

        if (s.fmode == dsp::FrequencyMode::fixed)
            return b;

        const auto [low, high] = frequencyRange (s);
        b.shown = true;
        b.lowHz = low;
        b.highHz = high;
        b.restHz = restFrequency (s);
        return b;
    }

    //==========================================================================
    /** The detector's own node (PRD, Editor: a ghost node, draggable). */
    struct Ghost
    {
        bool shown = false;
        double hz = 1000.0;
        double q = 1.0;
        dsp::DetectorType type = dsp::DetectorType::bandPass;
    };

    inline Ghost ghostOf (const dsp::BandSettings& s) noexcept
    {
        Ghost g;
        g.shown = usesDetector (s) && s.dfilt == dsp::DetectorFilter::own;
        g.hz = std::clamp (s.detectorFrequency, dsp::minFrequency, dsp::maxFrequency);
        g.q = std::clamp (s.detectorQ, dsp::minQ, dsp::maxQ);
        g.type = s.dtype;
        return g;
    }

    //==========================================================================
    enum class Key
    {
        none,           // the band reads no key: no badge
        self,
        sidechain,
        fellBack        // Sidechain chosen, the bus absent or disabled: keyed by the input
    };

    struct Badge
    {
        Key key = Key::none;
        bool warns = false;
        std::string text;       // on the node
        std::string tooltip;    // in full
    };

    /** No host is singled out (2026-11-22): Go.dot, once said to feed the
        sidechain silence, routes a key to it since its namespace draft §141,
        and keeps the bus disabled until one is picked - the fallback below. */
    inline Badge badgeOf (const dsp::BandSettings& s, const core::BandMeters& m)
    {
        Badge b;

        if (! readsKey (s))
            return b;

        if (s.key == dsp::KeySource::self)
        {
            b.key = Key::self;
            b.text = "IN";
            b.tooltip = "Keyed by the plugin's input.";
            return b;
        }

        if (m.keyFellBack)
        {
            b.key = Key::fellBack;
            b.warns = true;
            b.text = "SC?";
            b.tooltip = "Sidechain chosen, but the host gives this plugin no sidechain: keyed by the input instead.";
            return b;
        }

        b.key = Key::sidechain;
        b.text = "SC";
        b.tooltip = "Keyed by the sidechain.";
        return b;
    }

    //==========================================================================
    /** Listen (PRD, Detection): the band's detector, solo in place, while its
        button is held. The guard is the button's own, a long press in every
        host (decided by the author 2026-11-20, replacing the Arm switch and
        the recognition of a live host): nothing of it is decided here. */
    struct Listen
    {
        bool enabled = false;       // the band is on and has a detector to hear
        bool playing = false;       // the chain says this band is heard now
    };

    inline Listen listenOf (const dsp::BandSettings& s, int band, const core::Meters& meters) noexcept
    {
        return { s.on && usesDetector (s), meters.listening == band };
    }

    //==========================================================================
    /** The strip (PRD, Editor: only the controls of the selected band's
        current gain mode and frequency mode are shown; the others keep their
        values). In the order they are laid out. */
    struct StripSection
    {
        std::string title;
        std::vector<BandParam> params;
    };

    inline std::vector<StripSection> stripOf (const dsp::BandSettings& s)
    {
        using P = BandParam;
        std::vector<StripSection> sections;

        // The band itself. Frequency is not shown in Env, which does not use it.
        {
            StripSection band { "Band", { P::on, P::type } };

            if (s.fmode != dsp::FrequencyMode::env)
                band.params.push_back (P::freq);

            band.params.push_back (P::q);

            if (dsp::hasGain (s.type))
                band.params.push_back (P::gain);

            if (s.type == dsp::BandType::bell)
                band.params.push_back (P::qmode);

            sections.push_back (std::move (band));
        }

        // The gain axis: bells and shelves only.
        if (dsp::hasGain (s.type))
        {
            StripSection gain { "Gain", { P::gmode } };

            switch (s.gmode)
            {
                case dsp::GainMode::staticGain:
                    break;

                case dsp::GainMode::dynamic:
                    gain.params.insert (gain.params.end(),
                                        { P::range, P::thresh, P::ratio, P::knee, P::atk, P::rel, P::ddir });
                    break;

                case dsp::GainMode::transient:
                    gain.params.insert (gain.params.end(),
                                        { P::range, P::tatk, P::tsus, P::ttime, P::tfloor });
                    break;
            }

            sections.push_back (std::move (gain));
        }

        // The frequency axis: every type.
        {
            StripSection frequency { "Frequency", { P::fmode } };

            switch (s.fmode)
            {
                case dsp::FrequencyMode::fixed:
                    break;

                case dsp::FrequencyMode::env:
                    frequency.params.insert (frequency.params.end(),
                                             { P::flo, P::fhi, P::fdir, P::fsens, P::fspan, P::fatk, P::frel });
                    break;

                case dsp::FrequencyMode::track:
                    frequency.params.insert (frequency.params.end(),
                                             { P::flo, P::fhi, P::fspeed, P::fconf, P::fhold, P::fcgain });
                    break;
            }

            sections.push_back (std::move (frequency));
        }

        // The key, and the detector where it shapes the key.
        if (readsKey (s))
        {
            StripSection key { "Key", { P::key } };

            if (usesDetector (s))
            {
                key.params.push_back (P::dfilt);

                if (s.dfilt == dsp::DetectorFilter::own)
                    key.params.insert (key.params.end(), { P::dtype, P::dfreq, P::dq });
            }

            sections.push_back (std::move (key));
        }

        return sections;
    }

    //==========================================================================
    /*  THE COLUMNS AND THE FOOT (decided by the author 2026-11-20, on seeing
        the first curve: the layout of WFS-DIY's output EQ). Under the curve,
        one column per band with its main controls, always there; under the
        columns, one foot panel shared by the bands, showing the selected
        band's gain mode, frequency mode and key with their controls. Together
        they show what stripOf lists, and Frequency always. */

    /** A band's column: On, Type, Frequency, Q and Gain. */
    struct Column
    {
        bool on = false;
        bool showsGain = true;          // bells and shelves
        bool frequencyInUse = true;     // not in Env, which does not use it: drawn dimmed
    };

    inline Column columnOf (const dsp::BandSettings& s) noexcept
    {
        return { s.on, dsp::hasGain (s.type), s.fmode != dsp::FrequencyMode::env };
    }

    inline bool inColumn (BandParam p) noexcept
    {
        return p == BandParam::on || p == BandParam::type || p == BandParam::freq
            || p == BandParam::q || p == BandParam::gain;
    }

    /** The foot panel for a band: the strip's sections without the column's
        controls. The Q mode, a bell's, stays in a "Band" section of its own. */
    inline std::vector<StripSection> footOf (const dsp::BandSettings& s)
    {
        auto sections = stripOf (s);

        for (auto& section : sections)
            section.params.erase (std::remove_if (section.params.begin(), section.params.end(), inColumn),
                                  section.params.end());

        sections.erase (std::remove_if (sections.begin(), sections.end(),
                                        [] (const StripSection& section) { return section.params.empty(); }),
                        sections.end());
        return sections;
    }

    /*  WHERE EACH CONTROL OF THE FOOT IS (decided by the author 2026-11-20).
        Four blocks side by side, each of a fixed number of columns of two
        rows, so that nothing moves when a mode changes: a control has one
        place, whatever the modes, and a block that has nothing to show
        keeps its room.

          - A mode's choice (Q mode, Gain mode, Freq mode, Key) is alone in
            its block's first column: nothing beneath it.
          - Then pairs, one above the other: what the mode reads, how far it
            goes, how fast; and what is left alone in a last column.
          - What two modes share is in the same place in both: Range in
            Dynamic and Transient, Low and High in Env and Track. */
    enum class FootBlock { band, gain, frequency, key, count };

    inline constexpr int numFootBlocks = static_cast<int> (FootBlock::count);
    inline constexpr int footRows = 2;

    /** The columns each block keeps, in order. */
    inline constexpr int footColumns[numFootBlocks] = { 1, 5, 5, 3 };

    inline std::string footTitle (FootBlock block)
    {
        switch (block)
        {
            case FootBlock::band:       return "Band";
            case FootBlock::gain:       return "Gain";
            case FootBlock::frequency:  return "Frequency";
            case FootBlock::key:        return "Key";
            case FootBlock::count:      break;
        }

        return {};
    }

    struct FootPlace
    {
        FootBlock block = FootBlock::band;
        int column = 0, row = 0;

        bool operator== (const FootPlace&) const noexcept = default;
    };

    /** A foot control's place. The column's controls have none: see inColumn. */
    inline FootPlace footPlaceOf (BandParam p) noexcept
    {
        using P = BandParam;
        using B = FootBlock;

        switch (p)
        {
            case P::qmode:   return { B::band, 0, 0 };

            // Dynamic: what it reads, how far, how fast, and the knee.
            case P::gmode:   return { B::gain, 0, 0 };
            case P::ddir:    return { B::gain, 1, 0 };
            case P::thresh:  return { B::gain, 1, 1 };
            case P::range:   return { B::gain, 2, 0 };
            case P::ratio:   return { B::gain, 2, 1 };
            case P::atk:     return { B::gain, 3, 0 };
            case P::rel:     return { B::gain, 3, 1 };
            case P::knee:    return { B::gain, 4, 0 };

            // Transient: its two amounts, Range where Dynamic has it and the floor, the time.
            case P::tatk:    return { B::gain, 1, 0 };
            case P::tsus:    return { B::gain, 1, 1 };
            case P::tfloor:  return { B::gain, 2, 1 };
            case P::ttime:   return { B::gain, 3, 0 };

            // Env: what it reads, where it goes, how fast, and the span.
            case P::fmode:   return { B::frequency, 0, 0 };
            case P::fdir:    return { B::frequency, 1, 0 };
            case P::fsens:   return { B::frequency, 1, 1 };
            case P::flo:     return { B::frequency, 2, 0 };
            case P::fhi:     return { B::frequency, 2, 1 };
            case P::fatk:    return { B::frequency, 3, 0 };
            case P::frel:    return { B::frequency, 3, 1 };
            case P::fspan:   return { B::frequency, 4, 0 };

            // Track: its confidence, Low and High where Env has them, its times.
            case P::fconf:   return { B::frequency, 1, 0 };
            case P::fcgain:  return { B::frequency, 1, 1 };
            case P::fspeed:  return { B::frequency, 3, 0 };
            case P::fhold:   return { B::frequency, 3, 1 };

            // The key, then its detector: the filter and its type, its frequency and Q.
            case P::key:     return { B::key, 0, 0 };
            case P::dfilt:   return { B::key, 1, 0 };
            case P::dtype:   return { B::key, 1, 1 };
            case P::dfreq:   return { B::key, 2, 0 };
            case P::dq:      return { B::key, 2, 1 };

            case P::on:
            case P::type:
            case P::freq:
            case P::q:
            case P::gain:
            case P::count:
                break;
        }

        return {};
    }

    /** A control's name, short enough for a dial. */
    inline std::string labelOf (BandParam p)
    {
        using P = BandParam;

        switch (p)
        {
            case P::on:      return "On";
            case P::type:    return "Type";
            case P::freq:    return "Freq";
            case P::q:       return "Q";
            case P::gain:    return "Gain";
            case P::qmode:   return "Q mode";
            case P::gmode:   return "Gain mode";
            case P::fmode:   return "Freq mode";
            case P::key:     return "Key";
            case P::dfilt:   return "Detector";
            case P::dtype:   return "Det. type";
            case P::dfreq:   return "Det. freq";
            case P::dq:      return "Det. Q";
            case P::range:   return "Range";
            case P::thresh:  return "Threshold";
            case P::ratio:   return "Ratio";
            case P::knee:    return "Knee";
            case P::atk:     return "Attack";
            case P::rel:     return "Release";
            case P::ddir:    return "Acts";
            case P::tatk:    return "Attack";
            case P::tsus:    return "Sustain";
            case P::ttime:   return "Time";
            case P::tfloor:  return "Floor";
            case P::flo:     return "Low";
            case P::fhi:     return "High";
            case P::fspeed:  return "Speed";
            case P::fdir:    return "Direction";
            case P::fsens:   return "Sensitivity";
            case P::fspan:   return "Span";
            case P::fatk:    return "Attack";
            case P::frel:    return "Release";
            case P::fconf:   return "Confidence";
            case P::fhold:   return "Hold";
            case P::fcgain:  return "Scales gain";
            case P::count:   break;
        }

        return {};
    }

    /** The per-instance row above the curve: all four, always. */
    inline std::vector<dsp::GlobalParam> globalRow()
    {
        return { dsp::GlobalParam::bypass, dsp::GlobalParam::outGain, dsp::GlobalParam::lookahead, dsp::GlobalParam::link };
    }
}
