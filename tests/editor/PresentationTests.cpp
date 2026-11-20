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
    What a node, a bracket, a badge and a strip say in each state
    (editor/Presentation.h; PRD, Editor; devplan 6a). One field is changed
    at a time, as the OSCQuery plugin's PresentationTests.cpp does.

    THE REACHABILITY TEST is the phase's "every parameter is reachable from
    the editor", held here: over every combination of type, gain mode,
    frequency mode and detector choice, the strips and the global row
    together show all 35 band parameters and the 4 per-instance ones.
*/

#include <doctest/doctest.h>

#include <editor/Presentation.h>

#include <set>
#include <string>

using namespace likyu;
using namespace likyu::editor::presentation;

namespace
{
    /** A band that is on, a bell at 1 kHz, +4 dB, everything else at its default. */
    dsp::BandSettings bell()
    {
        dsp::BandSettings s;
        s.on = true;
        s.frequency = 1000.0;
        s.gainDb = 4.0;
        return s;
    }

    core::BandMeters metered (float offsetDb, float hz, float confidence = 0.0f, bool fellBack = false)
    {
        core::BandMeters m;
        m.offsetDb = offsetDb;
        m.frequencyHz = hz;
        m.confidence = confidence;
        m.keyFellBack = fellBack;
        return m;
    }

    bool shows (const std::vector<StripSection>& strip, BandParam p)
    {
        for (const auto& section : strip)
            for (const auto shown : section.params)
                if (shown == p)
                    return true;

        return false;
    }

    int timesShown (const std::vector<StripSection>& strip, BandParam p)
    {
        auto count = 0;

        for (const auto& section : strip)
            for (const auto shown : section.params)
                count += shown == p ? 1 : 0;

        return count;
    }

    bool mentions (const std::string& text, const std::string& word)
    {
        return text.find (word) != std::string::npos;
    }
}

//==============================================================================
TEST_CASE ("presentation: which parts of a band are in use follows the DSP")
{
    auto s = bell();
    CHECK (gainModeInEffect (s) == dsp::GainMode::staticGain);
    CHECK_FALSE (readsKey (s));
    CHECK_FALSE (usesDetector (s));

    s.gmode = dsp::GainMode::dynamic;
    CHECK (readsKey (s));
    CHECK (usesDetector (s));

    // A type without a gain is Static whatever its gain mode says.
    s.type = dsp::BandType::highPass;
    CHECK (gainModeInEffect (s) == dsp::GainMode::staticGain);
    CHECK_FALSE (readsKey (s));

    // Track without a gain mode reads a key, and not through the detector filter.
    s.fmode = dsp::FrequencyMode::track;
    CHECK (readsKey (s));
    CHECK_FALSE (usesDetector (s));

    s.fmode = dsp::FrequencyMode::env;
    CHECK (usesDetector (s));
}

TEST_CASE ("presentation: where a band rests")
{
    auto s = bell();
    s.lowHz = 2000.0;
    s.highHz = 4000.0;
    CHECK (restFrequency (s) == 1000.0);

    // Track: the band's Frequency, clamped into the range.
    s.fmode = dsp::FrequencyMode::track;
    CHECK (restFrequency (s) == 2000.0);
    s.frequency = 3000.0;
    CHECK (restFrequency (s) == 3000.0);
    s.frequency = 9000.0;
    CHECK (restFrequency (s) == 4000.0);

    // Env: the start of the mapping, whatever the Frequency.
    s.fmode = dsp::FrequencyMode::env;
    CHECK (restFrequency (s) == 2000.0);
    s.envDirection = dsp::EnvDirection::down;
    CHECK (restFrequency (s) == 4000.0);

    // Low above High: swapped.
    s.lowHz = 4000.0;
    s.highHz = 2000.0;
    CHECK (frequencyRange (s).first == 2000.0);
    CHECK (frequencyRange (s).second == 4000.0);
    CHECK (restFrequency (s) == 4000.0);
}

TEST_CASE ("presentation: a node")
{
    auto s = bell();
    const auto still = metered (0.0f, 1000.0f);

    auto n = nodeOf (s, still, true);
    CHECK (n.on);
    CHECK (n.hasGain);
    CHECK_FALSE (n.live);
    CHECK_FALSE (n.dimmed);
    CHECK (n.hz == 1000.0);
    CHECK (n.gainDb == 4.0);
    CHECK (n.drag == FrequencyDrag::frequency);

    SUBCASE ("a Fixed band is at its parameter, not at the meter's smoothed value")
    {
        CHECK (nodeOf (s, metered (0.0f, 940.0f), true).hz == 1000.0);
    }

    SUBCASE ("off: still there, and at rest")
    {
        s.on = false;
        s.fmode = dsp::FrequencyMode::track;
        n = nodeOf (s, metered (0.0f, 1500.0f, 0.0f), true);
        CHECK_FALSE (n.on);
        CHECK (n.hz == 1000.0);
        CHECK_FALSE (n.dimmed);
    }

    SUBCASE ("a type without a gain sits on 0 dB and has no gain arm")
    {
        s.type = dsp::BandType::notch;
        n = nodeOf (s, still, true);
        CHECK_FALSE (n.hasGain);
        CHECK (n.gainDb == 0.0);
    }

    SUBCASE ("Track: it slides to the meter's frequency, dimmed under the threshold")
    {
        s.fmode = dsp::FrequencyMode::track;
        s.lowHz = 500.0;
        s.highHz = 2000.0;
        s.confidencePercent = 30.0;

        n = nodeOf (s, metered (0.0f, 1500.0f, 0.5f), true);
        CHECK (n.live);
        CHECK (n.hz == 1500.0);
        CHECK_FALSE (n.dimmed);
        CHECK (n.drag == FrequencyDrag::frequency);

        CHECK (nodeOf (s, metered (0.0f, 1500.0f, 0.29f), true).dimmed);
        CHECK_FALSE (nodeOf (s, metered (0.0f, 1500.0f, 0.3f), true).dimmed);

        // No block yet: at rest, and nothing to dim.
        n = nodeOf (s, metered (0.0f, 0.0f, 0.0f), false);
        CHECK (n.hz == 1000.0);
        CHECK_FALSE (n.dimmed);

        // A meter that says nothing usable.
        CHECK (nodeOf (s, metered (0.0f, 0.0f, 1.0f), true).hz == 1000.0);
    }

    SUBCASE ("Env: a sideways drag carries the range")
    {
        s.fmode = dsp::FrequencyMode::env;
        s.lowHz = 500.0;
        s.highHz = 2000.0;
        n = nodeOf (s, metered (0.0f, 800.0f), true);
        CHECK (n.live);
        CHECK (n.hz == 800.0);
        CHECK (n.drag == FrequencyDrag::range);
        CHECK_FALSE (n.dimmed);
    }
}

TEST_CASE ("presentation: the vertical bracket")
{
    auto s = bell();
    CHECK_FALSE (verticalBracketOf (s, metered (-3.0f, 1000.0f), true).shown);

    SUBCASE ("Dynamic: from the static gain towards the Range's sign")
    {
        s.gmode = dsp::GainMode::dynamic;
        s.rangeDb = -6.0;

        auto b = verticalBracketOf (s, metered (-2.5f, 1000.0f), true);
        CHECK (b.shown);
        CHECK (b.restDb == 4.0);
        CHECK (b.lowDb == -2.0);
        CHECK (b.highDb == 4.0);
        CHECK (b.markDb == doctest::Approx (1.5));

        s.rangeDb = 9.0;
        b = verticalBracketOf (s, metered (20.0f, 1000.0f), true);
        CHECK (b.lowDb == 4.0);
        CHECK (b.highDb == 13.0);
        CHECK (b.markDb == doctest::Approx (13.0));      // the offset stops at the Range
    }

    SUBCASE ("Transient: either way")
    {
        s.gmode = dsp::GainMode::transient;
        s.rangeDb = -6.0;
        const auto b = verticalBracketOf (s, metered (5.0f, 1000.0f), true);
        CHECK (b.lowDb == -2.0);
        CHECK (b.highDb == 10.0);
        CHECK (b.markDb == doctest::Approx (9.0));
    }

    SUBCASE ("the ends and the mark stay within +-30 dB")
    {
        s.gmode = dsp::GainMode::dynamic;
        s.gainDb = 24.0;
        s.rangeDb = 30.0;
        const auto b = verticalBracketOf (s, metered (30.0f, 1000.0f), true);
        CHECK (b.highDb == 30.0);
        CHECK (b.markDb == 30.0);
    }

    SUBCASE ("no meters, or the band off: the mark is at rest")
    {
        s.gmode = dsp::GainMode::dynamic;
        CHECK (verticalBracketOf (s, metered (-5.0f, 1000.0f), false).markDb == 4.0);
        s.on = false;
        CHECK (verticalBracketOf (s, metered (-5.0f, 1000.0f), true).markDb == 4.0);
    }

    SUBCASE ("a type without a gain has none")
    {
        s.gmode = dsp::GainMode::dynamic;
        s.type = dsp::BandType::lowPass;
        CHECK_FALSE (verticalBracketOf (s, metered (-5.0f, 1000.0f), true).shown);
    }
}

TEST_CASE ("presentation: the gain now, with confidence scaling")
{
    auto s = bell();
    s.gmode = dsp::GainMode::dynamic;
    s.rangeDb = -12.0;
    s.fmode = dsp::FrequencyMode::track;

    // Without the switch, confidence changes nothing.
    CHECK (liveGainDb (s, metered (-6.0f, 1000.0f, 0.25f), true) == doctest::Approx (-2.0));

    // With it: the offset is scaled, and the static gain where it is a boost.
    s.confidenceScalesGain = true;
    CHECK (liveGainDb (s, metered (-6.0f, 1000.0f, 0.25f), true) == doctest::Approx (4.0 * 0.25 - 6.0 * 0.25));
    CHECK (liveGainDb (s, metered (-6.0f, 1000.0f, 1.0f), true) == doctest::Approx (-2.0));

    // A static cut is not scaled.
    s.gainDb = -4.0;
    CHECK (liveGainDb (s, metered (-6.0f, 1000.0f, 0.5f), true) == doctest::Approx (-4.0 - 3.0));

    // Outside Track the switch says nothing.
    s.fmode = dsp::FrequencyMode::fixed;
    CHECK (liveGainDb (s, metered (-6.0f, 1000.0f, 0.5f), true) == doctest::Approx (-10.0));

    // A Static band has no offset, whatever the meter holds.
    s.gmode = dsp::GainMode::staticGain;
    CHECK (liveGainDb (s, metered (-6.0f, 1000.0f, 0.5f), true) == -4.0);
}

TEST_CASE ("presentation: the horizontal bracket")
{
    auto s = bell();
    CHECK_FALSE (horizontalBracketOf (s).shown);

    s.fmode = dsp::FrequencyMode::track;
    s.lowHz = 4000.0;
    s.highHz = 500.0;
    auto b = horizontalBracketOf (s);
    CHECK (b.shown);
    CHECK (b.lowHz == 500.0);
    CHECK (b.highHz == 4000.0);
    CHECK (b.restHz == 1000.0);

    s.fmode = dsp::FrequencyMode::env;
    s.envDirection = dsp::EnvDirection::down;
    b = horizontalBracketOf (s);
    CHECK (b.shown);
    CHECK (b.restHz == 4000.0);

    // Every type has a frequency mode.
    s.type = dsp::BandType::highPass;
    CHECK (horizontalBracketOf (s).shown);
}

TEST_CASE ("presentation: the ghost node")
{
    auto s = bell();
    s.dfilt = dsp::DetectorFilter::own;
    s.detectorFrequency = 3000.0;
    s.detectorQ = 2.0;
    s.dtype = dsp::DetectorType::highPass;

    // Own, but the band reads no key.
    CHECK_FALSE (ghostOf (s).shown);

    s.gmode = dsp::GainMode::dynamic;
    const auto g = ghostOf (s);
    CHECK (g.shown);
    CHECK (g.hz == 3000.0);
    CHECK (g.q == 2.0);
    CHECK (g.type == dsp::DetectorType::highPass);

    s.dfilt = dsp::DetectorFilter::follow;
    CHECK_FALSE (ghostOf (s).shown);

    // Track with no gain mode does not use the detector filter.
    s.dfilt = dsp::DetectorFilter::own;
    s.gmode = dsp::GainMode::staticGain;
    s.fmode = dsp::FrequencyMode::track;
    CHECK_FALSE (ghostOf (s).shown);

    s.fmode = dsp::FrequencyMode::env;
    CHECK (ghostOf (s).shown);
}

TEST_CASE ("presentation: the key badge")
{
    auto s = bell();
    const auto fine = metered (0.0f, 1000.0f);
    const auto fellBack = metered (0.0f, 1000.0f, 0.0f, true);

    // A band that reads no key has no badge, whatever its key says.
    s.key = dsp::KeySource::sidechain;
    CHECK (badgeOf (s, fellBack, true).key == Key::none);
    CHECK (badgeOf (s, fellBack, true).text.empty());

    s.gmode = dsp::GainMode::dynamic;

    s.key = dsp::KeySource::self;
    auto b = badgeOf (s, fellBack, true);
    CHECK (b.key == Key::self);
    CHECK_FALSE (b.warns);
    CHECK (mentions (b.tooltip, "input"));

    s.key = dsp::KeySource::sidechain;
    b = badgeOf (s, fine, false);
    CHECK (b.key == Key::sidechain);
    CHECK_FALSE (b.warns);

    // The bus absent or disabled: said plainly (PRD, rules).
    b = badgeOf (s, fellBack, false);
    CHECK (b.key == Key::fellBack);
    CHECK (b.warns);
    CHECK (mentions (b.tooltip, "no sidechain"));
    CHECK (mentions (b.tooltip, "input"));

    // A host that feeds the bus silence: no fallback, and no key either.
    b = badgeOf (s, fine, true);
    CHECK (b.key == Key::silentByHost);
    CHECK (b.warns);
    CHECK (mentions (b.tooltip, "silence"));

    // The fallback is the truer of the two when both hold.
    CHECK (badgeOf (s, fellBack, true).key == Key::fellBack);
}

TEST_CASE ("presentation: Listen")
{
    auto s = bell();
    core::Meters meters;

    // Nothing to hear on a band with no detector in use.
    CHECK_FALSE (listenOf (s, 2, meters, false, false).enabled);
    CHECK_FALSE (listenOf (s, 2, meters, false, false).acts);

    s.gmode = dsp::GainMode::dynamic;
    auto l = listenOf (s, 2, meters, false, false);
    CHECK (l.enabled);
    CHECK_FALSE (l.needsArm);
    CHECK (l.acts);
    CHECK_FALSE (l.playing);

    // A guarded host: Arm first.
    l = listenOf (s, 2, meters, true, false);
    CHECK (l.enabled);
    CHECK (l.needsArm);
    CHECK_FALSE (l.acts);
    CHECK (listenOf (s, 2, meters, true, true).acts);

    meters.listening = 2;
    CHECK (listenOf (s, 2, meters, false, false).playing);
    CHECK_FALSE (listenOf (s, 3, meters, false, false).playing);

    s.on = false;
    CHECK_FALSE (listenOf (s, 2, meters, false, false).enabled);
}

TEST_CASE ("presentation: the strip shows the current modes' controls and no others")
{
    using P = BandParam;
    auto s = bell();

    auto strip = stripOf (s);
    for (const auto p : { P::on, P::type, P::freq, P::q, P::gain, P::qmode, P::gmode, P::fmode })
        CHECK (shows (strip, p));
    for (const auto p : { P::range, P::thresh, P::tatk, P::flo, P::fspeed, P::key, P::dfilt, P::dfreq })
        CHECK_FALSE (shows (strip, p));

    SUBCASE ("Dynamic")
    {
        s.gmode = dsp::GainMode::dynamic;
        strip = stripOf (s);
        for (const auto p : { P::range, P::thresh, P::ratio, P::knee, P::atk, P::rel, P::ddir, P::key, P::dfilt })
            CHECK (shows (strip, p));
        for (const auto p : { P::tatk, P::tsus, P::ttime, P::tfloor, P::dtype, P::dfreq, P::dq })
            CHECK_FALSE (shows (strip, p));

        s.dfilt = dsp::DetectorFilter::own;
        strip = stripOf (s);
        for (const auto p : { P::dtype, P::dfreq, P::dq })
            CHECK (shows (strip, p));
    }

    SUBCASE ("Transient")
    {
        s.gmode = dsp::GainMode::transient;
        strip = stripOf (s);
        for (const auto p : { P::range, P::tatk, P::tsus, P::ttime, P::tfloor, P::key, P::dfilt })
            CHECK (shows (strip, p));
        for (const auto p : { P::thresh, P::ratio, P::knee, P::atk, P::rel, P::ddir })
            CHECK_FALSE (shows (strip, p));
    }

    SUBCASE ("Env: no Frequency, which it does not use")
    {
        s.fmode = dsp::FrequencyMode::env;
        strip = stripOf (s);
        for (const auto p : { P::flo, P::fhi, P::fdir, P::fsens, P::fspan, P::fatk, P::frel, P::key, P::dfilt })
            CHECK (shows (strip, p));
        for (const auto p : { P::freq, P::fspeed, P::fconf, P::fhold, P::fcgain })
            CHECK_FALSE (shows (strip, p));
    }

    SUBCASE ("Track: the key, and no detector filter without a gain mode")
    {
        s.fmode = dsp::FrequencyMode::track;
        strip = stripOf (s);
        for (const auto p : { P::freq, P::flo, P::fhi, P::fspeed, P::fconf, P::fhold, P::fcgain, P::key })
            CHECK (shows (strip, p));
        for (const auto p : { P::fdir, P::fsens, P::fspan, P::fatk, P::frel, P::dfilt })
            CHECK_FALSE (shows (strip, p));

        s.gmode = dsp::GainMode::dynamic;
        CHECK (shows (stripOf (s), P::dfilt));
    }

    SUBCASE ("a type without a gain: no gain, no gain mode, no Q mode")
    {
        s.type = dsp::BandType::highPass;
        s.gmode = dsp::GainMode::dynamic;
        strip = stripOf (s);
        for (const auto p : { P::gain, P::gmode, P::qmode, P::range, P::thresh, P::key })
            CHECK_FALSE (shows (strip, p));
        CHECK (shows (strip, P::fmode));
    }

    SUBCASE ("Q mode is the bell's")
    {
        s.type = dsp::BandType::lowShelf;
        strip = stripOf (s);
        CHECK_FALSE (shows (strip, P::qmode));
        CHECK (shows (strip, P::gmode));
    }
}

TEST_CASE ("presentation: every parameter is reachable, and none is shown twice")
{
    std::set<int> reached;

    for (int type = 0; type < dsp::numBandTypes; ++type)
        for (int gmode = 0; gmode < dsp::numGainModes; ++gmode)
            for (int fmode = 0; fmode < dsp::numFrequencyModes; ++fmode)
                for (int dfilt = 0; dfilt < dsp::numDetectorFilters; ++dfilt)
                {
                    dsp::BandSettings s;
                    s.type = static_cast<dsp::BandType> (type);
                    s.gmode = static_cast<dsp::GainMode> (gmode);
                    s.fmode = static_cast<dsp::FrequencyMode> (fmode);
                    s.dfilt = static_cast<dsp::DetectorFilter> (dfilt);

                    const auto strip = stripOf (s);

                    for (int p = 0; p < dsp::numBandParams; ++p)
                    {
                        const auto count = timesShown (strip, static_cast<BandParam> (p));
                        INFO ("type " << type << ", gain mode " << gmode << ", frequency mode " << fmode
                              << ", detector " << dfilt << ", parameter " << p);
                        CHECK (count <= 1);

                        if (count == 1)
                            reached.insert (p);
                    }

                    // The modes themselves never disappear, or a band could not be brought back.
                    CHECK (timesShown (strip, BandParam::on) == 1);
                    CHECK (timesShown (strip, BandParam::type) == 1);
                    CHECK (timesShown (strip, BandParam::fmode) == 1);
                    CHECK (timesShown (strip, BandParam::gmode) == (dsp::hasGain (s.type) ? 1 : 0));
                }

    CHECK (reached.size() == static_cast<std::size_t> (dsp::numBandParams));

    std::set<int> global;
    for (const auto p : globalRow())
        global.insert (static_cast<int> (p));
    CHECK (global.size() == static_cast<std::size_t> (dsp::numGlobalParams));
}

TEST_CASE ("presentation: the columns and the foot together are the strip, with Frequency always in its column")
{
    for (int type = 0; type < dsp::numBandTypes; ++type)
        for (int gmode = 0; gmode < dsp::numGainModes; ++gmode)
            for (int fmode = 0; fmode < dsp::numFrequencyModes; ++fmode)
                for (int dfilt = 0; dfilt < dsp::numDetectorFilters; ++dfilt)
                {
                    dsp::BandSettings s;
                    s.type = static_cast<dsp::BandType> (type);
                    s.gmode = static_cast<dsp::GainMode> (gmode);
                    s.fmode = static_cast<dsp::FrequencyMode> (fmode);
                    s.dfilt = static_cast<dsp::DetectorFilter> (dfilt);

                    const auto strip = stripOf (s);
                    const auto foot = footOf (s);
                    const auto column = columnOf (s);

                    for (const auto& section : foot)
                        CHECK_FALSE (section.params.empty());

                    for (int i = 0; i < dsp::numBandParams; ++i)
                    {
                        const auto p = static_cast<BandParam> (i);
                        INFO ("type " << type << ", gain mode " << gmode << ", frequency mode " << fmode
                              << ", detector " << dfilt << ", parameter " << i);

                        // A parameter is in a column or in the foot, never in both.
                        const auto shownInColumn = inColumn (p) && (p != BandParam::gain || column.showsGain);
                        CHECK (timesShown (foot, p) == (inColumn (p) ? 0 : timesShown (strip, p)));

                        // What the strip shows, the two show; and Frequency besides, dimmed in Env.
                        if (shows (strip, p))
                            CHECK ((shownInColumn || shows (foot, p)));

                        CHECK_FALSE (labelOf (p).empty());
                    }

                    CHECK (column.showsGain == dsp::hasGain (s.type));
                    CHECK (column.frequencyInUse == (s.fmode != dsp::FrequencyMode::env));
                }

    // A static bell's foot: its Q mode, and the two mode choices to leave Static and Fixed by.
    dsp::BandSettings s;
    const auto foot = footOf (s);
    CHECK (shows (foot, BandParam::qmode));
    CHECK (shows (foot, BandParam::gmode));
    CHECK (shows (foot, BandParam::fmode));
    CHECK_FALSE (shows (foot, BandParam::key));
}
