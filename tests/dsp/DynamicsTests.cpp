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
    Dynamic mode (PRD, Gain modes and Detection; devplan Phase 3). Acceptance
    rows: dynamic curve, exact bypass return, gain limit (with real offsets),
    channel linking, linked maximum, link exclusion (its DSP half), sidechain
    fallback (its DSP half), layouts, realtime, block-size invariance (again,
    with every new mode). Listen and the meters (devplan 3d). The switches'
    clicks are in MovementTests.cpp.

    A level here is a PEAK level: a sine of amplitude a is 20 log10(a) dB, as
    the band-pass quadrature envelope reads it.
*/

#include <doctest/doctest.h>

#include "DspTestSupport.h"

#include <dsp/Chain.h>
#include <dsp/Detector.h>
#include <dsp/Dynamics.h>
#include <dsp/rt/RtCheck.h>

#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>
#include <utility>

using namespace likyu::dsp;
using namespace likyutest::dsp;

namespace
{
    constexpr double rate = 48000.0;

    double dbOf (double amplitude)
    {
        return 20.0 * std::log10 (amplitude);
    }

    double amplitudeOf (double db)
    {
        return std::pow (10.0, db / 20.0);
    }

    /** Sample by sample through a fresh chain, keeping the offset of `band`
        on every channel after each sample. */
    struct Trace
    {
        std::vector<std::vector<double>> audio;
        std::vector<std::vector<double>> offsets;   // [channel][sample]
    };

    Trace trace (std::vector<std::vector<double>> audio, const std::vector<Event>& events, int band,
                 const std::vector<std::vector<double>>& sidechain = {})
    {
        Trace t;
        const auto channels = static_cast<int> (audio.size());
        const auto length = static_cast<int> (audio.front().size());
        t.offsets.assign (audio.size(), std::vector<double> (static_cast<std::size_t> (length)));

        Chain chain;
        chain.prepare (rate, channels);

        std::vector<double*> pointers (audio.size());
        std::vector<const double*> sidePointers (sidechain.size());
        std::size_t next = 0;

        for (int n = 0; n < length; ++n)
        {
            while (next < events.size() && events[next].at <= n)
                events[next++].apply (chain);

            for (std::size_t c = 0; c < audio.size(); ++c)
                pointers[c] = audio[c].data() + n;

            Key<double> key;
            key.self = pointers.data();
            key.selfChannels = channels;

            for (std::size_t s = 0; s < sidechain.size(); ++s)
                sidePointers[s] = sidechain[s].data() + n;

            if (! sidechain.empty())
            {
                key.sidechain = sidePointers.data();
                key.sidechainChannels = static_cast<int> (sidechain.size());
            }

            chain.process (pointers.data(), channels, key, 1);

            for (int c = 0; c < channels; ++c)
                t.offsets[static_cast<std::size_t> (c)][static_cast<std::size_t> (n)] = chain.offsetDb (band, c);
        }

        t.audio = std::move (audio);
        return t;
    }

    /** Quiet noise with -0 and denormals through it, `amplitude` peak. */
    template <typename Sample>
    std::vector<Sample> awkwardQuiet (int length, std::uint32_t seed, double amplitude)
    {
        auto out = noise<Sample> (length, seed, amplitude);

        for (std::size_t n = 0; n < out.size(); n += 97)
            out[n] = -Sample (0);

        for (std::size_t n = 31; n < out.size(); n += 101)
            out[n] = std::numeric_limits<Sample>::denorm_min() * Sample (n % 7 + 1);

        return out;
    }

    /** Eight bells and shelves in Dynamic mode at 0 dB static. */
    std::vector<Event> dynamicAtZero (double releaseMs, Direction direction = Direction::above, double thresholdDb = -30.0)
    {
        return { { 0, [releaseMs, direction, thresholdDb] (Chain& c)
                   {
                       const BandType types[] = { BandType::bell, BandType::lowShelf, BandType::highShelf };

                       for (int b = 0; b < numBands; ++b)
                       {
                           auto s = dynamicBand (types[b % 3], 60.0 * std::pow (2.0, b), 0.5 + 0.4 * b, 0.0, thresholdDb, -6.0,
                                                 4.0, 5.0, releaseMs, direction);
                           s.qmode = b % 2 == 0 ? QMode::proportional : QMode::constant;
                           c.setBand (b, s);
                       }
                   } } };
    }
}

//==============================================================================
TEST_CASE ("gain computer: the static curve, Above and Below, soft knee, ratio, and Range as limit and sign")
{
    const auto slope3 = 1.0 - 1.0 / 3.0;

    // Hard knee: 14 dB over, ratio 3, 9.33 dB of cut.
    CHECK (dynamicTarget (-10.0, -24.0, 0.0, slope3, -12.0, Direction::above) == doctest::Approx (-14.0 * slope3));

    // Soft knee of 6 dB: at the threshold (0 + 3)^2 / 12 = 0.75 dB over; continuous at both ends.
    CHECK (dynamicTarget (-24.0, -24.0, 6.0, slope3, -12.0, Direction::above) == doctest::Approx (-0.75 * slope3));
    CHECK (dynamicTarget (-21.0, -24.0, 6.0, slope3, -12.0, Direction::above) == doctest::Approx (-3.0 * slope3));
    CHECK (softKnee (3.0, 6.0) == doctest::Approx (3.0));
    CHECK (softKnee (-3.0, 6.0) == 0.0);

    // Under the knee: exactly 0, and a positive 0, so that the return to zero holds.
    for (const auto level : { -27.0, -27.0001, -60.0, -120.0 })
    {
        const auto t = dynamicTarget (level, -24.0, 6.0, slope3, -12.0, Direction::above);
        CHECK (t == 0.0);
        CHECK_FALSE (std::signbit (t));
    }

    // Range: its sign is the direction of the offset, its size the limit.
    CHECK (dynamicTarget (-10.0, -24.0, 0.0, slope3, 12.0, Direction::above) == doctest::Approx (14.0 * slope3));
    CHECK (dynamicTarget (0.0, -80.0, 0.0, 0.95, -6.0, Direction::above) == -6.0);
    CHECK (dynamicTarget (0.0, -80.0, 0.0, 0.95, 0.0, Direction::above) == 0.0);

    // Ratio 1 does nothing.
    CHECK (dynamicTarget (0.0, -80.0, 6.0, 0.0, -30.0, Direction::above) == 0.0);

    // Below reacts when the level falls under the threshold.
    CHECK (dynamicTarget (-40.0, -24.0, 0.0, slope3, -30.0, Direction::below) == doctest::Approx (-16.0 * slope3));
    CHECK (dynamicTarget (-10.0, -24.0, 6.0, slope3, -30.0, Direction::below) == 0.0);
}

TEST_CASE ("gain computer: attack and release are time constants, the offset returns to exactly 0, and nothing stalls at 192 kHz")
{
    GainComputer gc;
    gc.prepare (rate, 60);
    gc.setTarget (-24.0, 6.0, 3.0, -12.0, 5.0, 120.0);
    gc.finish();

    // Attack: a step of the target to -10 dB is 63 % done after 5 ms.
    double offset = 0.0;
    int n = 0;

    while (offset > -10.0 * (1.0 - std::exp (-1.0)))
    {
        gc.smooth (offset, -10.0);
        ++n;
    }

    CHECK (std::abs (n - 240) <= 1);

    // Release: back to a target of 0, 63 % done after 120 ms ...
    offset = -10.0;
    n = 0;

    while (offset < -10.0 * std::exp (-1.0))
    {
        gc.smooth (offset, 0.0);
        ++n;
    }

    CHECK (std::abs (n - 5760) <= 1);

    // ... and exactly 0 once within 0.0001 dB of it: 120 ms ln(10 / 1e-4) from -10 dB.
    offset = -10.0;
    const auto predicted = static_cast<int> (std::ceil (0.120 * rate * std::log (10.0 / returnToZeroDb)));
    int reached = -1;

    for (int i = 1; i < predicted + 100; ++i)
    {
        gc.smooth (offset, 0.0);

        if (reached < 0 && exactlyEqual (offset, 0.0))
            reached = i;
    }

    INFO ("exactly 0 after " << reached << " samples, predicted " << predicted);
    CHECK (std::abs (reached - predicted) <= 2);
    CHECK (offset == 0.0);
    CHECK_FALSE (std::signbit (offset));

    // At 192 kHz, the slowest release and attack still arrive (in float they stall 0.37 dB short).
    GainComputer slow;
    slow.prepare (192000.0, 240);
    slow.setTarget (-24.0, 6.0, 3.0, -30.0, 200.0, 2000.0);
    slow.finish();

    offset = -30.0;

    for (int i = 0; i < static_cast<int> (20.0 * 192000.0); ++i)
        slow.smooth (offset, -10.0);

    CHECK (offset == doctest::Approx (-10.0).epsilon (0.0001));

    offset = 0.0;

    for (int i = 0; i < static_cast<int> (2.0 * 192000.0); ++i)
        slow.smooth (offset, -30.0);

    CHECK (offset == doctest::Approx (-30.0).epsilon (0.0001));
}

//==============================================================================
TEST_CASE ("dynamic curve: a stepped-level tone through a dynamic bell reads the analytic gain computer, attack and release within 10 %")
{
    constexpr double hz = 3000.0;
    constexpr double thresholdDb = -24.0, kneeDb = 6.0, ratio = 3.0, rangeDb = -18.0;
    const auto slope = 1.0 - 1.0 / ratio;

    // Steady state: each level held 0.4 s.
    const double levels[] = { -40.0, -30.0, -26.0, -24.0, -22.0, -18.0, -10.0, -4.0 };
    const auto hold = static_cast<int> (0.4 * rate);
    std::vector<double> input;

    for (const auto level : levels)
    {
        const auto t = tone<double> (hold, hz, amplitudeOf (level), rate, static_cast<double> (input.size()) * 2.0 * pi * hz / rate);
        input.insert (input.end(), t.begin(), t.end());
    }

    const std::vector<Event> setting { { 0, [] (Chain& c) { c.setBand (0, dynamicBand (BandType::bell, hz, 1.0, 0.0, thresholdDb, rangeDb, ratio)); } } };
    const auto steady = trace ({ input }, setting, 0);

    double worstOffset = 0.0, worstGain = 0.0;

    for (std::size_t s = 0; s < std::size (levels); ++s)
    {
        const auto analytic = dynamicTarget (levels[s], thresholdDb, kneeDb, slope, rangeDb, Direction::above);
        const auto end = static_cast<int> (s + 1) * hold;
        const auto offset = steady.offsets[0][static_cast<std::size_t> (end - 1)];

        // The band's gain at its centre is the effective gain: output peak over input peak, last 10 ms.
        double out = 0.0;

        for (int n = end - 480; n < end; ++n)
            out = std::max (out, std::abs (steady.audio[0][static_cast<std::size_t> (n)]));

        const auto gainDb = dbOf (out) - levels[s];

        INFO ("level " << levels[s] << " dB: analytic " << analytic << " dB, offset " << offset << " dB, band gain " << gainDb << " dB");
        CHECK (std::abs (offset - analytic) < 0.25);
        CHECK (std::abs (gainDb - analytic) < 0.25);

        worstOffset = std::max (worstOffset, std::abs (offset - analytic));
        worstGain = std::max (worstGain, std::abs (gainDb - analytic));
    }

    MESSAGE ("dynamic curve: worst offset error " << worstOffset << " dB, worst band gain error " << worstGain << " dB");

    // Timing: -60 dB to -6 dB and back. The target is 0 then -12 dB (18 dB over, ratio 3), then 0.
    for (const auto& [attackMs, releaseMs] : { std::pair { 5.0, 120.0 }, std::pair { 20.0, 500.0 } })
    {
        const auto up = static_cast<int> (0.2 * rate), down = static_cast<int> (0.6 * rate), length = static_cast<int> (2.0 * rate);
        std::vector<double> stepped (static_cast<std::size_t> (length));

        for (int n = 0; n < length; ++n)
            stepped[static_cast<std::size_t> (n)] = amplitudeOf (n >= up && n < down ? -6.0 : -60.0) * std::sin (2.0 * pi * hz * n / rate);

        const std::vector<Event> timed { { 0, [attackMs, releaseMs] (Chain& c)
        {
            c.setBand (0, dynamicBand (BandType::bell, hz, 1.0, 0.0, thresholdDb, rangeDb, ratio, attackMs, releaseMs));
        } } };

        const auto t = trace ({ stepped }, timed, 0);
        const auto& o = t.offsets[0];
        const auto settled = o[static_cast<std::size_t> (down - 1)];
        CHECK (settled == doctest::Approx (-12.0).epsilon (0.01));

        int attackAt = -1, releaseAt = -1;

        for (int n = up; n < down && attackAt < 0; ++n)
            if (o[static_cast<std::size_t> (n)] <= settled * (1.0 - std::exp (-1.0)))
                attackAt = n - up;

        for (int n = down; n < length && releaseAt < 0; ++n)
            if (o[static_cast<std::size_t> (n)] >= settled * std::exp (-1.0))
                releaseAt = n - down;

        const auto attackMeasured = 1000.0 * attackAt / rate, releaseMeasured = 1000.0 * releaseAt / rate;

        INFO ("attack " << attackMs << " ms measured " << attackMeasured << " ms; release " << releaseMs << " ms measured " << releaseMeasured << " ms");
        MESSAGE ("dynamic curve: attack " << attackMs << " ms measured " << attackMeasured << " ms; release " << releaseMs << " ms measured " << releaseMeasured << " ms");
        CHECK (std::abs (attackMeasured - attackMs) < 0.1 * attackMs);
        CHECK (std::abs (releaseMeasured - releaseMs) < 0.1 * releaseMs);
    }
}

TEST_CASE ("quadrature envelope, end to end: a dynamic bell at 60 Hz with the shortest times does not ripple, at its centre or half an octave off")
{
    /*  The row's "1 ms time constants": the attack at 1 ms; the release can
        go no shorter than 5 ms (PRD, Parameters). Every tone is 26 to 28 dB over
        the threshold, so the offset sits on the slope of the curve, where a
        ripple of the level would show in it. */
    constexpr double centre = 60.0, thresholdDb = -40.0, ratio = 4.0;
    const auto length = static_cast<int> (1.0 * rate);
    double worst = 0.0;

    for (const auto hz : { centre, centre / std::sqrt (2.0), centre * std::sqrt (2.0) })
    {
        const auto input = tone<double> (length, hz, 0.25, rate);
        const std::vector<Event> events { { 0, [] (Chain& c)
        {
            c.setBand (0, dynamicBand (BandType::bell, centre, 1.0, 0.0, thresholdDb, -30.0, ratio, 1.0, 5.0));
        } } };

        const auto t = trace ({ input }, events, 0);
        double lowest = 1.0e9, highest = -1.0e9;

        for (int n = length / 2; n < length; ++n)
        {
            lowest = std::min (lowest, t.offsets[0][static_cast<std::size_t> (n)]);
            highest = std::max (highest, t.offsets[0][static_cast<std::size_t> (n)]);
        }

        INFO (hz << " Hz: offset from " << lowest << " to " << highest << " dB");
        CHECK (highest < -3.0);
        CHECK (highest - lowest < 0.1);
        worst = std::max (worst, highest - lowest);
    }

    MESSAGE ("quadrature envelope, end to end: worst offset ripple " << worst << " dB");
}

//==============================================================================
TEST_CASE_TEMPLATE ("exact bypass, return: dynamic bands at 0 dB, a loud passage, then a key under the threshold: the input again, bit for bit", Sample, float, double)
{
    constexpr double releaseMs = 50.0;
    const auto loud = static_cast<int> (0.25 * rate), length = static_cast<int> (1.5 * rate);

    std::vector<std::vector<Sample>> input { awkwardQuiet<Sample> (length, 21, 1.0e-4), awkwardQuiet<Sample> (length, 22, 1.0e-4) };
    const auto burst = std::vector<std::vector<Sample>> { noise<Sample> (loud, 23, 0.5), noise<Sample> (loud, 24, 0.5) };

    for (std::size_t c = 0; c < input.size(); ++c)
        std::copy (burst[c].begin(), burst[c].end(), input[c].begin());

    /*  The stated time: an offset of at most |Range| = 6 dB falls to the
        return to zero's 0.0001 dB in release ln(6 / 0.0001), 11.0 releases,
        counted from the end of the loud passage; the quiet key is 50 dB under
        every threshold, so the targets are exactly 0 from then on. One
        control step more for the step in which it lands. */
    const auto stated = loud + static_cast<int> (std::ceil (releaseMs * 0.001 * rate * std::log (6.0 / returnToZeroDb))) + controlStep;

    for (const auto& sizes : { fixedBlocks (64), fixedBlocks (1), randomBlocks (17) })
    {
        const auto output = render (rate, input, dynamicAtZero (releaseMs), sizes);

        for (std::size_t c = 0; c < input.size(); ++c)
        {
            int lastDifferent = -1;

            for (int n = 0; n < length; ++n)
                if (std::memcmp (&output[c][static_cast<std::size_t> (n)], &input[c][static_cast<std::size_t> (n)], sizeof (Sample)) != 0)
                    lastDifferent = n;

            INFO ("channel " << c << ": the last sample changed is " << lastDifferent << ", the stated time " << stated);
            CHECK (lastDifferent > loud / 2);   // the bands did act
            CHECK (lastDifferent < stated);
        }
    }

    MESSAGE ("exact bypass, return: stated " << 1000.0 * (stated - loud) / rate << " ms after the key fell");
}

TEST_CASE_TEMPLATE ("exact bypass: dynamic bands whose key never crosses the threshold pass the input bit for bit", Sample, float, double)
{
    const auto length = 24000;

    // Above, with a key 50 dB under the threshold.
    const std::vector<std::vector<Sample>> quiet { awkwardQuiet<Sample> (length, 31, 1.0e-4), awkwardQuiet<Sample> (length, 32, 1.0e-4) };
    const auto above = render (rate, quiet, dynamicAtZero (120.0), fixedBlocks (64));

    for (std::size_t c = 0; c < quiet.size(); ++c)
        CHECK (sameBits (above[c], quiet[c]));

    /*  Below, with a steady key far over the threshold: a sine, whose
        quadrature envelope is flat. (On noise it is not: the envelope of a
        narrow band of noise dips, and the cross terms take it to 0 for an
        instant, so a Below band acts, by hundredths of a dB, now and then.)
        A detector's first sample after silence reads nothing (v1^2 - v2 hp
        is exactly 0 there), so a Below band acts for an instant at the start
        and is the input again once that has returned to zero: within
        ln(6 / 0.0001) = 11 releases. */
    constexpr double releaseMs = 20.0;
    const auto woken = static_cast<int> (std::ceil (releaseMs * 0.001 * rate * std::log (6.0 / returnToZeroDb))) + controlStep;
    const std::vector<std::vector<Sample>> loud { tone<Sample> (length, 1000.0, 0.5, rate), tone<Sample> (length, 1000.0, 0.4, rate, 1.0) };
    const auto below = render (rate, loud, dynamicAtZero (releaseMs, Direction::below, -80.0), fixedBlocks (64));

    for (std::size_t c = 0; c < loud.size(); ++c)
    {
        CHECK_FALSE (sameBits (below[c], loud[c]));     // it did act at the start
        CHECK (std::memcmp (below[c].data() + woken, loud[c].data() + woken, (loud[c].size() - static_cast<std::size_t> (woken)) * sizeof (Sample)) == 0);
    }
}

//==============================================================================
TEST_CASE ("gain limit: static gain and Range at their extremes, same sign, with real offsets: never past +-30 dB")
{
    constexpr double hz = 1000.0;
    const auto length = static_cast<int> (0.5 * rate);

    struct Case { std::string name; BandType type; double toneHz, levelDb, staticDb, rangeDb; Direction direction; };

    for (const auto& c : { Case { "bell up", BandType::bell, hz, -70.0, 30.0, 30.0, Direction::below },
                           Case { "bell down", BandType::bell, hz, 0.0, -30.0, -30.0, Direction::above },
                           Case { "high shelf up", BandType::highShelf, 16000.0, -70.0, 30.0, 30.0, Direction::below },
                           Case { "low shelf down", BandType::lowShelf, 30.0, 0.0, -30.0, -30.0, Direction::above } })
    {
        const auto input = tone<double> (length, c.toneHz, amplitudeOf (c.levelDb), rate);
        auto s = dynamicBand (c.type, hz, 0.7, c.staticDb, c.direction == Direction::below ? -24.0 : -80.0, c.rangeDb, 20.0, 1.0, 100.0, c.direction);
        s.dfilt = DetectorFilter::own;
        s.detectorFrequency = c.toneHz;
        const std::vector<Event> events { { 0, [s] (Chain& chain) { chain.setBand (0, s); } } };

        const auto t = trace ({ input }, events, 0);
        const auto offset = t.offsets[0].back();

        double out = 0.0;

        for (int n = length - 4800; n < length; ++n)
            out = std::max (out, std::abs (t.audio[0][static_cast<std::size_t> (n)]));

        const auto gainDb = dbOf (out) - c.levelDb;

        INFO (c.name << ": offset " << offset << " dB, gain " << gainDb << " dB");
        CHECK (std::abs (offset) <= 30.0);
        CHECK (std::abs (gainDb) <= 30.0 + 0.05);
        CHECK (std::abs (gainDb) > 29.0);   // the limit, reached
    }
}

//==============================================================================
TEST_CASE ("a dynamic band at rest is the static band it would be, bit for bit; the two process overloads agree")
{
    const auto length = 20000;
    const std::vector<std::vector<double>> quiet { noise<double> (length, 41, 1.0e-3), noise<double> (length, 42, 1.0e-3) };

    // Moving and constant steps alike: a gain ramp, a frequency ramp, then rest.
    const auto events = [] (bool dynamic)
    {
        const auto make = [dynamic] (double hz, double db)
        {
            return dynamic ? dynamicBand (BandType::bell, hz, 2.0, db, -10.0, -12.0) : band (true, BandType::bell, hz, 2.0, db);
        };

        return std::vector<Event> {
            { 0, [make] (Chain& c) { c.setBand (2, make (500.0, 6.0)); } },
            { 4000, [make] (Chain& c) { c.setBand (2, make (500.0, -3.0)); } },
            { 9000, [make] (Chain& c) { c.setBand (2, make (2000.0, -3.0)); } },
        };
    };

    const auto asStatic = render (rate, quiet, events (false), fixedBlocks (64));
    const auto asDynamic = render (rate, quiet, events (true), fixedBlocks (64));

    for (std::size_t c = 0; c < quiet.size(); ++c)
        CHECK (sameBits (asDynamic[c], asStatic[c]));

    // Acting, keyed by the input through either overload.
    const std::vector<std::vector<double>> loud { noise<double> (length, 43, 0.5), noise<double> (length, 44, 0.5) };
    const std::vector<Event> acting { { 0, [] (Chain& c)
    {
        c.setBand (0, dynamicBand (BandType::bell, 800.0, 1.0, 3.0, -40.0, -9.0));
        c.setBand (5, dynamicBand (BandType::highShelf, 5000.0, 0.7, 0.0, -40.0, -6.0));
    } } };

    auto byInput = loud;
    {
        Chain chain;
        chain.prepare (rate, 2);
        acting[0].apply (chain);
        double* p[] = { byInput[0].data(), byInput[1].data() };
        chain.process (p, 2, length);
    }

    auto byKey = loud;
    {
        Chain chain;
        chain.prepare (rate, 2);
        acting[0].apply (chain);
        const auto copy = loud;
        const double* k[] = { copy[0].data(), copy[1].data() };
        double* p[] = { byKey[0].data(), byKey[1].data() };
        Key<double> key;
        key.self = k;
        key.selfChannels = 2;
        chain.process (p, 2, key, length);
    }

    for (std::size_t c = 0; c < loud.size(); ++c)
    {
        CHECK_FALSE (sameBits (byInput[c], loud[c]));
        CHECK (sameBits (byKey[c], byInput[c]));
    }
}

//==============================================================================
namespace
{
    /** Everything Phase 3 adds, moving: dynamic bands on both keys, the link
        on and off, a channel left out, every new switch, the old ones too. */
    std::vector<Event> dynamicEverything (int period)
    {
        return {
            { 0, [] (Chain& c)
              {
                  c.setBand (0, dynamicBand (BandType::bell, 300.0, 1.0, 2.0, -30.0, -9.0, 3.0, 1.0, 60.0));
                  c.setBand (1, dynamicBand (BandType::lowShelf, 150.0, 0.7, 0.0, -36.0, 6.0, 2.0, 3.0, 200.0, Direction::below));
                  auto sc = dynamicBand (BandType::bell, 2500.0, 2.0, 0.0, -30.0, -12.0, 4.0, 0.5, 80.0);
                  sc.key = KeySource::sidechain;
                  c.setBand (2, sc);
                  auto own = dynamicBand (BandType::highShelf, 6000.0, 0.7, -2.0, -40.0, -6.0);
                  own.dfilt = DetectorFilter::own;
                  own.dtype = DetectorType::highPass;
                  own.detectorFrequency = 5000.0;
                  c.setBand (3, own);
                  c.setBand (4, band (true, BandType::notch, 3000.0, 4.0, 0.0));
                  c.setBand (5, dynamicBand (BandType::bell, 1200.0, 4.0, -3.0, -50.0, -6.0));
                  c.setChain ({ false, -1.0, true });
              } },
            { period, [] (Chain& c)
              {
                  c.setChain ({ false, -1.0, false });                                                     // unlinked
                  c.setBand (0, dynamicBand (BandType::bell, 300.0, 1.0, 2.0, -20.0, -12.0, 6.0, 1.0, 60.0));  // threshold, range
              } },
            { 2 * period, [] (Chain& c)
              {
                  c.setChain ({ false, 0.0, true });
                  c.setBandChannels (5, 1u);                                                                // left channel out of band 6
                  auto s = dynamicBand (BandType::bell, 300.0, 1.0, 2.0, -20.0, -12.0);
                  s.gmode = GainMode::staticGain;                                                           // gain mode
                  c.setBand (0, s);
                  auto own = dynamicBand (BandType::highShelf, 6000.0, 0.7, -2.0, -40.0, -6.0);
                  own.dfilt = DetectorFilter::own;
                  own.dtype = DetectorType::lowPass;                                                        // detector type
                  own.detectorFrequency = 400.0;
                  c.setBand (3, own);
              } },
            { 2 * period + 300, [] (Chain& c)
              {
                  c.setBand (0, dynamicBand (BandType::bell, 300.0, 1.0, 2.0, -20.0, -12.0));               // turned round mid-fade
                  c.setBand (5, dynamicBand (BandType::bell, 1200.0, 4.0, -3.0, -50.0, -6.0, 3.0, 5.0, 120.0, Direction::below));
              } },
            { 3 * period, [] (Chain& c)
              {
                  c.setBandChannels (5, 0u);
                  c.setChain ({ true, 0.0, true });
              } },
            { 4 * period, [] (Chain& c)
              {
                  c.setChain ({ false, 0.0, true });
                  auto sc = dynamicBand (BandType::bell, 2500.0, 2.0, 0.0, -30.0, -12.0, 4.0, 0.5, 80.0);
                  c.setBand (2, sc);                                                                        // key back to Self
              } },
            { period + 500, [] (Chain& c) { c.setListen (3); } },                                          // Listen, Own high-pass
            { period + 900, [] (Chain& c) { c.setListen (2); } },                                          // to the sidechain band
            { 2 * period + 700, [] (Chain& c) { c.setListen (4); } },                                      // a static notch
            { 2 * period + 2000, [] (Chain& c) { c.setListen (-1); } },
            { 4 * period + 50, [] (Chain& c) { c.setListen (2); } },
            { 4 * period + 3000, [] (Chain& c) { c.setListen (-1); } },
        };
    }
}

TEST_CASE_TEMPLATE ("block-size invariance, again: dynamic bands, both keys, the link and every new switch, bit for bit", Sample, float, double)
{
    constexpr int period = 7 * 1024;
    const auto length = 5 * period;

    std::vector<std::vector<Sample>> input { noise<Sample> (length, 51, 0.4), noise<Sample> (length, 52, 0.1) };
    const std::vector<std::vector<Sample>> sidechain { noise<Sample> (length, 53, 0.5), noise<Sample> (length, 54, 0.05) };

    // -0 and denormals among the samples, which the touched marks must keep.
    for (auto& channel : input)
    {
        for (std::size_t n = 0; n < channel.size(); n += 89)
            channel[n] = -Sample (0);

        for (std::size_t n = 13; n < channel.size(); n += 103)
            channel[n] = std::numeric_limits<Sample>::denorm_min();
    }

    const auto reference = render (rate, input, dynamicEverything (period), fixedBlocks (64), sidechain);
    CHECK_FALSE (sameBits (reference[0], input[0]));

    struct Cut { std::string name; BlockSizes sizes; };
    const std::vector<Cut> cuts {
        { "1", fixedBlocks (1) },     { "7", fixedBlocks (7) },       { "128", fixedBlocks (128) },
        { "480", fixedBlocks (480) }, { "4096", fixedBlocks (4096) }, { "random", randomBlocks (98) },
    };

    for (const auto& cut : cuts)
    {
        const auto output = render (rate, input, dynamicEverything (period), cut.sizes, sidechain);

        INFO ("blocks of " << cut.name);

        for (std::size_t c = 0; c < input.size(); ++c)
            CHECK (sameBits (output[c], reference[c]));
    }
}

//==============================================================================
TEST_CASE ("channel linking: a transient on one side of a stereo signal: linked, the same gain on both; unlinked, on its own side only")
{
    const auto length = static_cast<int> (0.4 * rate);
    std::vector<std::vector<double>> input { noise<double> (length, 61, 1.0e-4), noise<double> (length, 62, 1.0e-4) };

    // A 3 kHz burst on the left only, 50 ms, at -6 dB.
    for (int n = 4800; n < 7200; ++n)
        input[0][static_cast<std::size_t> (n)] += 0.5 * std::sin (2.0 * pi * 3000.0 * n / rate);

    for (const auto isLinked : { true, false })
    {
        const std::vector<Event> events { { 0, [isLinked] (Chain& c)
        {
            c.setBand (0, dynamicBand (BandType::bell, 3000.0, 1.0, 0.0, -30.0, -12.0, 4.0, 1.0, 50.0));
            c.setChain ({ false, 0.0, isLinked });
        } } };

        const auto t = trace (input, events, 0);
        double most = 0.0;
        auto same = true, rightStill = true;

        for (int n = 0; n < length; ++n)
        {
            const auto left = t.offsets[0][static_cast<std::size_t> (n)], right = t.offsets[1][static_cast<std::size_t> (n)];
            most = std::min (most, left);
            same = same && exactlyEqual (left, right);
            rightStill = rightStill && exactlyEqual (right, 0.0);
        }

        INFO (std::string (isLinked ? "linked" : "unlinked") << ": deepest offset " << most << " dB");
        CHECK (most < -6.0);

        if (isLinked)
            CHECK (same);
        else
            CHECK (rightStill);
    }
}

TEST_CASE ("linked maximum: a tone in one channel of eight, in all eight, and in two in opposite phase give the same offset")
{
    const auto length = static_cast<int> (0.2 * rate);
    const auto t = tone<double> (length, 1000.0, 0.25, rate);
    std::vector<double> inverted (t.size());

    for (std::size_t n = 0; n < t.size(); ++n)
        inverted[n] = -t[n];

    const std::vector<double> silence (t.size(), 0.0);
    const std::vector<Event> events { { 0, [] (Chain& c) { c.setBand (0, dynamicBand (BandType::bell, 1000.0, 1.0, 0.0, -30.0, -12.0)); } } };

    std::vector<std::vector<double>> one (8, silence), all (8, t), opposite (8, silence);
    one[3] = t;
    opposite[0] = t;
    opposite[5] = inverted;

    const auto a = trace (one, events, 0), b = trace (all, events, 0), c = trace (opposite, events, 0);

    CHECK (a.offsets[0].back() < -3.0);

    for (int ch = 0; ch < 8; ++ch)
    {
        CHECK (a.offsets[static_cast<std::size_t> (ch)] == a.offsets[0]);
        CHECK (b.offsets[static_cast<std::size_t> (ch)] == a.offsets[0]);
        CHECK (c.offsets[static_cast<std::size_t> (ch)] == a.offsets[0]);
    }
}

//==============================================================================
TEST_CASE ("sidechain: a Sidechain band is keyed by the sidechain's maximum; absent, it falls back to Self; present and silent, it is keyed by silence")
{
    const auto length = static_cast<int> (0.3 * rate);
    const auto loudInput = std::vector<std::vector<double>> { tone<double> (length, 2000.0, 0.5, rate), tone<double> (length, 2000.0, 0.5, rate) };
    const auto quietInput = std::vector<std::vector<double>> { noise<double> (length, 71, 1.0e-4), noise<double> (length, 72, 1.0e-4) };

    auto sidechainBand = dynamicBand (BandType::bell, 2000.0, 1.0, 0.0, -30.0, -12.0);
    sidechainBand.key = KeySource::sidechain;
    const std::vector<Event> keyed { { 0, [sidechainBand] (Chain& c) { c.setBand (0, sidechainBand); } } };

    // Present and silent: the loud input does nothing.
    {
        const std::vector<std::vector<double>> silent (2, std::vector<double> (static_cast<std::size_t> (length), 0.0));
        const auto out = render (rate, loudInput, keyed, fixedBlocks (64), silent);

        for (std::size_t c = 0; c < 2; ++c)
            CHECK (sameBits (out[c], loudInput[c]));
    }

    // A tone on the sidechain's right channel only: every main channel, the same offset.
    {
        const std::vector<std::vector<double>> sidechain { std::vector<double> (static_cast<std::size_t> (length), 0.0),
                                                           tone<double> (length, 2000.0, 0.5, rate) };
        const auto t = trace (quietInput, keyed, 0, sidechain);

        CHECK (t.offsets[0].back() < -6.0);
        CHECK (t.offsets[0] == t.offsets[1]);
    }

    // Absent: exactly what the same band keyed by Self does.
    {
        auto selfBand = sidechainBand;
        selfBand.key = KeySource::self;
        const auto fellBack = render (rate, loudInput, keyed, fixedBlocks (64));
        const auto self = render (rate, loudInput, { { 0, [selfBand] (Chain& c) { c.setBand (0, selfBand); } } }, fixedBlocks (64));

        for (std::size_t c = 0; c < 2; ++c)
        {
            CHECK_FALSE (sameBits (fellBack[c], loudInput[c]));
            CHECK (sameBits (fellBack[c], self[c]));
        }
    }
}

//==============================================================================
TEST_CASE ("layouts: 1, 2, 6 and 64 channels unlinked, every channel exactly as if it ran alone")
{
    constexpr int length = 3000;

    const auto events = [] (bool isLinked)
    {
        return std::vector<Event> { { 0, [isLinked] (Chain& c)
        {
            c.setBand (0, dynamicBand (BandType::bell, 700.0, 1.0, 2.0, -40.0, -9.0, 3.0, 0.5, 30.0));
            c.setBand (3, dynamicBand (BandType::highShelf, 4000.0, 0.7, 0.0, -45.0, -6.0, 3.0, 0.5, 30.0));
            c.setBand (6, dynamicBand (BandType::lowShelf, 200.0, 0.7, 0.0, -30.0, 4.0, 2.0, 1.0, 50.0, Direction::below));
            c.setChain ({ false, 0.0, isLinked });
        } } };
    };

    for (const auto channels : { 1, 2, 6, 64 })
    {
        std::vector<std::vector<double>> input;

        for (int c = 0; c < channels; ++c)
            input.push_back (noise<double> (length, static_cast<std::uint32_t> (800 + c), 0.05 + 0.01 * (c % 40)));

        const auto together = render (rate, input, events (false), fixedBlocks (37));

        INFO (channels << " channels");

        for (int c = 0; c < channels; ++c)
        {
            // Alone, linked or not is the same thing.
            const auto alone = render (rate, std::vector<std::vector<double>> { input[static_cast<std::size_t> (c)] },
                                       events (true), fixedBlocks (37));
            CHECK (sameBits (together[static_cast<std::size_t> (c)], alone[0]));
        }
    }
}

TEST_CASE ("realtime: 500 blocks of dynamic bands, both keys, the link and exclusions moving, allocate nothing")
{
    namespace rt = likyu::dsp::rt;
    REQUIRE (rt::isCounting());

    Chain chain;
    chain.prepare (96000.0, 2);

    auto left = noise<double> (64, 1, 0.5), right = noise<double> (64, 2, 0.5);
    auto sideLeft = noise<double> (64, 3, 0.5), sideRight = noise<double> (64, 4, 0.5);
    double* channels[] = { left.data(), right.data() };
    const double* side[] = { sideLeft.data(), sideRight.data() };
    Lcg random { 5 };

    rt::resetCounts();

    for (int block = 0; block < 500; ++block)
    {
        if (block % 5 == 0)
        {
            for (int b = 0; b < numBands; ++b)
            {
                auto s = dynamicBand (static_cast<BandType> (random.next() % numBandTypes), 20.0 * std::pow (1000.0, random.unit()),
                                      0.1 * std::pow (400.0, random.unit()), 60.0 * random.unit() - 30.0,
                                      -80.0 * random.unit(), 60.0 * random.unit() - 30.0, 1.0 + 19.0 * random.unit(),
                                      0.05 + 200.0 * random.unit(), 5.0 + 2000.0 * random.unit(),
                                      static_cast<Direction> (random.next() % numDirections));
                s.on = random.unit() < 0.8;
                s.gmode = static_cast<GainMode> (random.next() % numGainModes);
                s.key = static_cast<KeySource> (random.next() % numKeySources);
                s.dfilt = static_cast<DetectorFilter> (random.next() % numDetectorFilters);
                s.dtype = static_cast<DetectorType> (random.next() % numDetectorTypes);
                s.detectorFrequency = 20.0 * std::pow (1000.0, random.unit());
                chain.setBand (b, s);
            }

            chain.setChain ({ random.unit() < 0.2, 48.0 * random.unit() - 24.0, random.unit() < 0.5 });
            chain.setBandChannels (static_cast<int> (random.next() % numBands), random.next() % 4u);
            chain.setListen (static_cast<int> (random.next() % 12u) - 3);
        }

        Key<double> key;
        key.self = channels;
        key.selfChannels = 2;
        key.sidechain = side;
        key.sidechainChannels = static_cast<int> (random.next() % 3u);
        chain.process (channels, 2, key, 64);
    }

    CHECK (rt::regions() == 500);
    CHECK (rt::violations() == 0);
}

//==============================================================================
namespace
{
    /** A detector as the band builds it, run on its own from silence: what Listen should play. */
    std::vector<float> detectorOutput (const std::vector<double>& key, DetectorType type, double hz, double q)
    {
        Warp warp;
        warp.prepare (rate);
        const auto f = warp.clamp (std::exp2 (std::log2 (hz)));
        const auto k = makeDetectorCoefficients (type, warp.g (f), std::exp2 (-std::log2 (q)), f, rate);

        std::vector<float> out (key.size());
        float s1 = 0.0f, s2 = 0.0f, peak = 0.0f;

        for (std::size_t n = 0; n < key.size(); ++n)
            detectorTick (static_cast<float> (key[n]), s1, s2, peak, k, out[n]);

        return out;
    }

    BandSettings ownDetector (KeySource key)
    {
        auto s = band (true, BandType::bell, 500.0, 1.0, 0.0);
        s.key = key;
        s.dfilt = DetectorFilter::own;
        s.dtype = DetectorType::bandPass;
        s.detectorFrequency = 1000.0;
        s.detectorQ = 4.0;
        return s;
    }
}

TEST_CASE ("listen: Self, each channel hears its own detector; Sidechain, the mean of the sidechain's, on every channel")
{
    const auto length = 4800;
    const std::vector<std::vector<double>> input { tone<double> (length, 1000.0, 0.5, rate), tone<double> (length, 3000.0, 0.5, rate) };

    // Self, set before the first block: Listen from the first sample, a static band's detector.
    {
        const std::vector<Event> events { { 0, [] (Chain& c) { c.setBand (2, ownDetector (KeySource::self)); c.setListen (2); } } };
        const auto output = render (rate, input, events, fixedBlocks (64));

        for (std::size_t c = 0; c < input.size(); ++c)
        {
            const auto expected = detectorOutput (input[c], DetectorType::bandPass, 1000.0, 4.0);

            for (std::size_t n = 0; n < expected.size(); ++n)
                REQUIRE (output[c][n] == static_cast<double> (expected[n]));
        }
    }

    // Sidechain: the mean of its two detectors, on both main channels.
    {
        const std::vector<std::vector<double>> sidechain { tone<double> (length, 1000.0, 0.5, rate), tone<double> (length, 1000.0, 0.2, rate, 1.0) };
        const std::vector<Event> events { { 0, [] (Chain& c) { c.setBand (2, ownDetector (KeySource::sidechain)); c.setListen (2); } } };
        const auto output = render (rate, input, events, fixedBlocks (64), sidechain);

        const auto a = detectorOutput (sidechain[0], DetectorType::bandPass, 1000.0, 4.0);
        const auto b = detectorOutput (sidechain[1], DetectorType::bandPass, 1000.0, 4.0);

        for (std::size_t c = 0; c < input.size(); ++c)
            for (std::size_t n = 0; n < a.size(); ++n)
                REQUIRE (output[c][n] == static_cast<double> ((a[n] + b[n]) * 0.5f));
    }
}

TEST_CASE ("listen: off again, the chain is the exact bypass once the fade out ends")
{
    const auto length = 9600, off = 2400;
    const std::vector<std::vector<double>> input { noise<double> (length, 81, 0.3), noise<double> (length, 82, 0.3) };
    const std::vector<Event> events {
        { 0, [] (Chain& c) { c.setBand (2, ownDetector (KeySource::self)); c.setListen (2); } },
        { off, [] (Chain& c) { c.setListen (-1); } },
    };

    const auto output = render (rate, input, events, randomBlocks (83));
    const auto faded = off + rampSteps (switchFadeSeconds, rate, controlStep) * controlStep + controlStep;

    for (std::size_t c = 0; c < input.size(); ++c)
    {
        CHECK_FALSE (output[c][100] == input[c][100]);
        CHECK (std::equal (output[c].begin() + faded, output[c].end(), input[c].begin() + faded));
    }
}

TEST_CASE ("meters: a band shows its offset, the largest over its lanes, its frequency, and whether its key fell back")
{
    const auto length = 9600;
    const std::vector<std::vector<double>> input { tone<double> (length, 1000.0, 0.5, rate), noise<double> (length, 91, 1.0e-4) };

    Chain chain;
    chain.prepare (rate, 2);
    chain.setBand (0, dynamicBand (BandType::bell, 1000.0, 1.0, 0.0, -30.0, -12.0));
    auto sidechainBand = dynamicBand (BandType::bell, 2000.0, 1.0, 0.0, -30.0, -12.0);
    sidechainBand.key = KeySource::sidechain;
    chain.setBand (1, sidechainBand);
    chain.setChain ({ false, 0.0, false });

    auto audio = input;
    double* p[] = { audio[0].data(), audio[1].data() };
    chain.process (p, 2, length);

    const auto m = chain.bandMeter (0);
    CHECK (m.offsetDb < -6.0);
    CHECK (m.offsetDb == chain.offsetDb (0, 0));
    CHECK (chain.offsetDb (0, 1) == 0.0);
    CHECK (m.frequencyHz == doctest::Approx (1000.0).epsilon (1.0e-9));
    CHECK (m.confidence == 0.0);
    CHECK_FALSE (m.keyFellBack);

    // No sidechain in the call: band 2 fell back to Self, and says so.
    CHECK (chain.bandMeter (1).keyFellBack);

    // A sidechain in the next call: it no longer falls back (from the next grid point).
    const std::vector<double> side (64, 0.0);
    const double* sp[] = { side.data() };
    Key<double> key;
    key.self = p;
    key.selfChannels = 2;
    key.sidechain = sp;
    key.sidechainChannels = 1;
    chain.process (p, 2, key, 64);
    CHECK_FALSE (chain.bandMeter (1).keyFellBack);

    // Out of range: nothing.
    CHECK (chain.bandMeter (-1).offsetDb == 0.0);
    CHECK (chain.bandMeter (numBands).frequencyHz == 0.0);
}
