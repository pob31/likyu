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
    Transient mode (PRD, Gain modes: Transient; devplan Phase 4). Acceptance
    rows: transient, transient after silence, exact bypass with Transient
    idle, exact bypass return (its Transient half), gain limit, and again with
    the new mode: block-size invariance, channel linking, layouts, realtime.
    The switches' clicks are in MovementTests.cpp.

    A level here is a PEAK level, as in DynamicsTests.cpp. The shaper's
    timing is checked against the one-poles it is made of: two one-poles
    from the same start toward the same step differ by
    e^(-t / slow) - e^(-t / fast) of the step, which peaks at
    t = ln(slow / fast) fast slow / (slow - fast).
*/

#include <doctest/doctest.h>

#include "DspTestSupport.h"

#include <dsp/Chain.h>
#include <dsp/Dynamics.h>
#include <dsp/Transient.h>
#include <dsp/rt/RtCheck.h>

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <tuple>
#include <utility>

using namespace likyu::dsp;
using namespace likyutest::dsp;

namespace
{
    constexpr double rate = 48000.0;

    int samplesOf (double milliseconds)
    {
        return static_cast<int> (std::lround (milliseconds * 0.001 * rate));
    }

    double msOf (int samples)
    {
        return 1000.0 * samples / rate;
    }

    //==========================================================================
    /** Levels in dB, one per sample, through a transient shaper on the grid,
        as the chain drives it: its offsets. */
    std::vector<double> shape (const std::vector<double>& levelsDb, double attackPercent, double sustainPercent,
                               double time = 1.0, double floorDb = -60.0, double rangeDb = 30.0)
    {
        TransientComputer shaper;
        shaper.prepare (rate, rampSteps (smoothingSeconds, rate, controlStep));
        shaper.setTarget (attackPercent, sustainPercent, time, floorDb);
        shaper.finish();

        TransientEnvelopes envelopes;
        std::vector<double> out (levelsDb.size());
        std::array<float, controlStep> power {};

        for (std::size_t n = 0; n < levelsDb.size(); n += controlStep)
        {
            shaper.step();
            const auto m = std::min<std::size_t> (controlStep, levelsDb.size() - n);

            for (std::size_t i = 0; i < m; ++i)
                power[i] = static_cast<float> (std::pow (10.0, levelsDb[n + i] / 10.0));

            shaper.run (power.data(), static_cast<int> (m), 0, controlStep, rangeDb, rangeDb, envelopes, out.data() + n);
        }

        return out;
    }

    /** The PRD's warning, built on purpose: the same three envelopes on L,
        not on max(L, floor), the gate as the shaper's. */
    std::vector<double> shapeWithoutFloor (const std::vector<double>& levelsDb, double attackPercent, double floorDb = -60.0)
    {
        const auto fa = onePole (transientFastAttackMs, rate), sa = onePole (transientSlowAttackMs, rate);
        const auto fr = onePole (transientFastReleaseMs, rate);
        auto fast = levelsDb.front(), slow = levelsDb.front();
        std::vector<double> out (levelsDb.size());

        for (std::size_t n = 0; n < levelsDb.size(); ++n)
        {
            const auto x = levelsDb[n];
            fast = x + (x > fast ? fa : fr) * (fast - x);
            slow = x + (x > slow ? sa : fr) * (slow - x);
            out[n] = std::clamp (attackPercent / 100.0 * std::max (0.0, fast - slow), -30.0, 30.0)
                   * TransientComputer::gate (fast - floorDb);
        }

        return out;
    }

    struct Peak
    {
        double atMs, value;
    };

    /** Where e^(-t / slow) - e^(-t / fast) peaks, and its height there. */
    Peak differencePeak (double fastMs, double slowMs)
    {
        const auto t = std::log (slowMs / fastMs) * fastMs * slowMs / (slowMs - fastMs);
        return { t, std::exp (-t / slowMs) - std::exp (-t / fastMs) };
    }

    /** The largest |offset| in [from, to), its sample and its value. */
    struct Extreme
    {
        int at = -1;
        double value = 0.0;
    };

    Extreme extreme (const std::vector<double>& offsets, int from, int to)
    {
        Extreme e;

        for (int n = from; n < to; ++n)
        {
            const auto o = offsets[static_cast<std::size_t> (n)];

            if (std::abs (o) > std::abs (e.value))
                e = { n, o };
        }

        return e;
    }

    bool allZero (const std::vector<double>& offsets, int from, int to)
    {
        for (int n = from; n < to; ++n)
            if (! exactlyEqual (offsets[static_cast<std::size_t> (n)], 0.0))
                return false;

        return true;
    }

    /** Samples in [from, to) whose |offset| is over half the extreme's. */
    int duration (const std::vector<double>& offsets, int from, int to)
    {
        const auto half = 0.5 * std::abs (extreme (offsets, from, to).value);
        int count = 0;

        for (int n = from; n < to; ++n)
            count += std::abs (offsets[static_cast<std::size_t> (n)]) > half ? 1 : 0;

        return count;
    }

    //==========================================================================
    /** Sample by sample through a fresh chain: the offset of band 0 on
        channel 0 after each sample. Audio after `skip` samples only; those
        before are processed in one call. */
    std::vector<double> offsetTrace (std::vector<std::vector<double>> audio, const BandSettings& settings, int skip = 0)
    {
        const auto channels = static_cast<int> (audio.size());
        const auto length = static_cast<int> (audio.front().size());

        Chain chain;
        chain.prepare (rate, channels);
        chain.setBand (0, settings);

        std::vector<double*> pointers (audio.size());
        std::vector<double> out (static_cast<std::size_t> (length), 0.0);

        const auto run = [&] (int at, int n)
        {
            for (std::size_t c = 0; c < audio.size(); ++c)
                pointers[c] = audio[c].data() + at;

            chain.process (pointers.data(), channels, n);
        };

        if (skip > 0)
            run (0, skip);

        for (int n = skip; n < length; ++n)
        {
            run (n, 1);
            out[static_cast<std::size_t> (n)] = chain.offsetDb (0, 0);
        }

        return out;
    }

    /** A tone of peak `amplitude` from `from` to `to`, nothing elsewhere. */
    std::vector<double> toneBurst (int length, double hz, double amplitude, int from, int to)
    {
        std::vector<double> out (static_cast<std::size_t> (length), 0.0);

        for (int n = from; n < std::min (to, length); ++n)
            out[static_cast<std::size_t> (n)] = amplitude * std::sin (2.0 * pi * hz * (n - from) / rate);

        return out;
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

    /** Eight bells and shelves in Transient mode at 0 dB static. */
    std::vector<Event> transientAtZero (double attackPercent, double sustainPercent, double rangeDb = -6.0)
    {
        return { { 0, [attackPercent, sustainPercent, rangeDb] (Chain& c)
                   {
                       const BandType types[] = { BandType::bell, BandType::lowShelf, BandType::highShelf };

                       for (int b = 0; b < numBands; ++b)
                       {
                           const auto sign = b % 2 == 0 ? 1.0 : -1.0;
                           auto s = transientBand (types[b % 3], 60.0 * std::pow (2.0, b), 0.5 + 0.4 * b, 0.0,
                                                   sign * attackPercent, -sign * sustainPercent, rangeDb);
                           s.qmode = b % 2 == 0 ? QMode::proportional : QMode::constant;
                           c.setBand (b, s);
                       }
                   } } };
    }
}

//==============================================================================
TEST_CASE ("transient: the attack amount moves the offset at a rise, with its sign, and Time scales the timing")
{
    constexpr double floorDb = -60.0, levelDb = -20.0;
    const auto rise = samplesOf (250.0), length = samplesOf (1000.0);

    // Under the floor, then 40 dB over it.
    std::vector<double> levels (static_cast<std::size_t> (length), -70.0);
    std::fill (levels.begin() + rise, levels.end(), levelDb);
    double worstDb = 0.0, worstTiming = 0.0;

    for (const auto time : { 0.5, 1.0, 2.0 })
    {
        const auto expected = differencePeak (transientFastAttackMs * time, transientSlowAttackMs * time);

        for (const auto amount : { 50.0, -50.0 })
        {
            const auto offsets = shape (levels, amount, 0.0, time, floorDb);
            const auto peak = extreme (offsets, rise, length);
            const auto atMs = msOf (peak.at - rise + 1);
            const auto value = amount / 100.0 * (levelDb - floorDb) * expected.value;

            INFO ("Time " << time << ", attack " << amount << " %: peak " << peak.value << " dB at " << atMs
                          << " ms; expected " << value << " dB at " << expected.atMs << " ms");

            CHECK (allZero (offsets, 0, rise));
            CHECK (peak.value * amount > 0.0);
            CHECK (std::abs (peak.value - value) < 0.25);
            CHECK (std::abs (atMs - expected.atMs) < 0.1 * expected.atMs);
            worstDb = std::max (worstDb, std::abs (peak.value - value));
            worstTiming = std::max (worstTiming, std::abs (atMs - expected.atMs) / expected.atMs);

            // Back to exactly 0 once the two have met at the level.
            CHECK (allZero (offsets, length - samplesOf (100.0), length));
        }

        // The sustain pair attacks as one: a rise has no sustain signal at all.
        CHECK (allZero (shape (levels, 0.0, 100.0, time, floorDb), 0, length));
        MESSAGE ("transient, attack: Time " << time << ", the attack signal peaks at " << expected.atMs << " ms");
    }

    MESSAGE ("transient, attack: worst peak " << worstDb << " dB off, its time " << 100.0 * worstTiming << " % off");
}

TEST_CASE ("transient: the sustain amount moves the offset as the level falls, with its sign, and Time scales the timing")
{
    constexpr double floorDb = -60.0, fromDb = -10.0, toDb = -40.0;
    const auto fall = samplesOf (1000.0), length = samplesOf (3000.0);

    std::vector<double> levels (static_cast<std::size_t> (length), fromDb);
    std::fill (levels.begin() + fall, levels.end(), toDb);
    double worstDb = 0.0, worstTiming = 0.0;

    for (const auto time : { 0.5, 1.0, 2.0 })
    {
        const auto expected = differencePeak (transientFastReleaseMs * time, transientSlowReleaseMs * time);

        for (const auto amount : { 50.0, -50.0 })
        {
            const auto offsets = shape (levels, 0.0, amount, time, floorDb);
            const auto peak = extreme (offsets, fall, length);
            const auto atMs = msOf (peak.at - fall + 1);
            const auto value = amount / 100.0 * (fromDb - toDb) * expected.value;

            INFO ("Time " << time << ", sustain " << amount << " %: peak " << peak.value << " dB at " << atMs
                          << " ms; expected " << value << " dB at " << expected.atMs << " ms");

            CHECK (allZero (offsets, 0, fall));
            CHECK (peak.value * amount > 0.0);
            CHECK (std::abs (peak.value - value) < 0.25);
            CHECK (std::abs (atMs - expected.atMs) < 0.1 * expected.atMs);
            worstDb = std::max (worstDb, std::abs (peak.value - value));
            worstTiming = std::max (worstTiming, std::abs (atMs - expected.atMs) / expected.atMs);
        }

        // The attack pair releases as one: a fall has no attack signal at all.
        CHECK (allZero (shape (levels, 100.0, 0.0, time, floorDb), 0, length));
        MESSAGE ("transient, sustain: Time " << time << ", the sustain signal peaks at " << expected.atMs << " ms");
    }

    MESSAGE ("transient, sustain: worst peak " << worstDb << " dB off, its time " << 100.0 * worstTiming << " % off");
}

TEST_CASE ("transient: the offset never passes |Range|, and nothing moves while the level stays under the floor")
{
    const auto length = samplesOf (2000.0);

    // Range limits both ways, whatever its sign.
    std::vector<double> step (static_cast<std::size_t> (length), -90.0);
    std::fill (step.begin() + length / 2, step.end(), 0.0);

    for (const auto rangeDb : { 4.0, -4.0 })
    {
        const auto up = shape (step, 100.0, 0.0, 1.0, -60.0, rangeDb);
        const auto down = shape (step, -100.0, 0.0, 1.0, -60.0, rangeDb);
        CHECK (extreme (up, 0, length).value == doctest::Approx (4.0));
        CHECK (extreme (down, 0, length).value == doctest::Approx (-4.0));
    }

    // Under the floor: steps, bursts and silence, from -120 dB to just under it.
    std::vector<double> under (static_cast<std::size_t> (length));
    Lcg random { 7 };

    for (int n = 0; n < length; ++n)
        under[static_cast<std::size_t> (n)] = (n / 977) % 3 == 0 ? -120.0 : -61.0 - 50.0 * random.unit();

    for (const auto floorDb : { -60.0, -30.0 })
        for (const auto amount : { 100.0, -100.0 })
            for (const auto time : { 0.25, 4.0 })
            {
                INFO ("floor " << floorDb << ", amounts " << amount << " %, Time " << time);
                CHECK (allZero (shape (under, amount, amount, time, floorDb), 0, length));
            }
}

TEST_CASE ("transient: the amounts and the floor ramp over 20 ms on the grid, so a change is not a step in the gain")
{
    /*  A level falling 10 dB a second, whose sustain signal settles at about
        10 dB/s x (200 - 20) ms = 1.8 dB, and at 3 s a change: the sustain
        amount flipped, or the floor raised past the level. The largest move
        of the offset from one sample to the next, against the same change
        with ramps one control step long. */
    const auto length = samplesOf (4000.0), at = samplesOf (3000.0) / controlStep * controlStep;

    const auto largestMove = [&] (int rampLength, double sustainAfter, double floorAfter)
    {
        TransientComputer shaper;
        shaper.prepare (rate, rampLength);
        shaper.setTarget (0.0, 100.0, 1.0, -60.0);
        shaper.finish();

        TransientEnvelopes envelopes;
        std::array<float, controlStep> power {};
        std::array<double, controlStep> out {};
        double previous = 0.0, largest = 0.0;

        for (int n = 0; n < length; n += controlStep)
        {
            if (n == at)
                shaper.setTarget (0.0, sustainAfter, 1.0, floorAfter);

            shaper.step();

            for (int i = 0; i < controlStep; ++i)
                power[static_cast<std::size_t> (i)] = static_cast<float> (std::pow (10.0, (-10.0 - 10.0 * (n + i) / rate) / 10.0));

            shaper.run (power.data(), controlStep, 0, controlStep, 12.0, 12.0, envelopes, out.data());

            for (int i = 0; i < controlStep; ++i)
            {
                if (n + i > samplesOf (1000.0))
                    largest = std::max (largest, std::abs (out[static_cast<std::size_t> (i)] - previous));

                previous = out[static_cast<std::size_t> (i)];
            }
        }

        return largest;
    };

    const auto steps = rampSteps (smoothingSeconds, rate, controlStep);

    for (const auto& [name, sustainAfter, floorAfter] : { std::tuple { std::string ("sustain, +100 % to -100 %"), -100.0, -60.0 },
                                                          std::tuple { std::string ("floor, -60 to -30 dB, past the level"), 100.0, -30.0 } })
    {
        const auto ramped = largestMove (steps, sustainAfter, floorAfter);
        const auto quick = largestMove (1, sustainAfter, floorAfter);

        INFO (name << ": the offset moves by at most " << ramped << " dB a sample; with one-step ramps, " << quick << " dB");
        CHECK (ramped < 0.05);
        CHECK (quick > 10.0 * ramped);
    }
}

//==============================================================================
TEST_CASE ("transient, end to end: a tone burst through a Transient bell at its centre has the sign and timing of the settings")
{
    constexpr double hz = 1000.0;
    const auto onset = samplesOf (100.0), stop = samplesOf (400.0), length = samplesOf (900.0);
    const std::vector<std::vector<double>> input { toneBurst (length, hz, 0.5, onset, stop) };

    for (const auto sign : { 1.0, -1.0 })
    {
        // Attack: at the onset, within the first few ms, and gone well before the stop.
        const auto attack = offsetTrace (input, transientBand (BandType::bell, hz, 1.0, 0.0, 100.0 * sign, 0.0, -12.0));
        const auto hit = extreme (attack, onset, stop);

        INFO ("attack " << 100.0 * sign << " %: " << hit.value << " dB at " << msOf (hit.at - onset) << " ms");
        CHECK (allZero (attack, 0, onset));
        CHECK (hit.value * sign > 6.0);
        CHECK (msOf (hit.at - onset) < 10.0);
        CHECK (std::abs (extreme (attack, onset + samplesOf (80.0), length).value) < 0.5);

        // Sustain: nothing at the onset, while the tone holds or once the
        // tail is gated; the tail itself carries it.
        const auto sustain = offsetTrace (input, transientBand (BandType::bell, hz, 1.0, 0.0, 0.0, 100.0 * sign, -12.0));
        const auto tail = extreme (sustain, stop, length);

        INFO ("sustain " << 100.0 * sign << " %: " << tail.value << " dB at " << msOf (tail.at - stop) << " ms after the stop");
        CHECK (std::abs (extreme (sustain, 0, stop).value) < 0.05);
        CHECK (tail.value * sign > 6.0);
        CHECK (msOf (tail.at - stop) < 40.0);
        CHECK (allZero (sustain, stop + samplesOf (250.0), length));
    }
}

TEST_CASE_TEMPLATE ("transient, end to end: a burst that stays under the floor passes bit for bit; over it, it does not", Sample, float, double)
{
    const auto length = samplesOf (600.0);

    for (const auto& [amplitude, moves] : { std::pair { 3.0e-4, false }, std::pair { 3.0e-2, true } })   // -70 dB, -30 dB
    {
        auto burst = awkwardQuiet<Sample> (length, 11, 1.0e-5);

        for (int n = samplesOf (100.0); n < samplesOf (300.0); ++n)
            burst[static_cast<std::size_t> (n)] += static_cast<Sample> (amplitude * std::sin (2.0 * pi * 1000.0 * n / rate));

        const std::vector<std::vector<Sample>> input { burst };
        const std::vector<Event> events { { 0, [] (Chain& c)
        {
            c.setBand (0, transientBand (BandType::bell, 1000.0, 1.0, 0.0, 100.0, -100.0, -12.0));
            c.setBand (1, transientBand (BandType::highShelf, 800.0, 0.7, 0.0, -100.0, 100.0, 9.0));
        } } };

        const auto output = render (rate, input, events, fixedBlocks (64));

        INFO ("a burst of peak " << amplitude);
        CHECK (sameBits (output[0], input[0]) != moves);
    }
}

//==============================================================================
TEST_CASE ("transient, after silence: the first burst after 5 s of silence is shaped as the ones that follow")
{
    constexpr double hz = 1000.0;
    const auto silence = samplesOf (5000.0), period = samplesOf (1000.0), bursts = 3;
    const auto length = silence + bursts * period;

    /*  Each burst: the tone at -6 dB falling 150 dB a second, the decay of a
        struck note, over a room's noise at -80 dB from the first burst on, so
        that the bursts after the first start from a quiet that is not -120 dB. */
    std::vector<double> audio (static_cast<std::size_t> (length), 0.0);
    const auto room = noise<double> (length - silence, 12, 1.0e-4);

    for (int n = silence; n < length; ++n)
    {
        const auto t = static_cast<double> ((n - silence) % period) / rate;
        audio[static_cast<std::size_t> (n)] = 0.5 * std::pow (10.0, -150.0 * t / 20.0) * std::sin (2.0 * pi * hz * n / rate)
                                            + room[static_cast<std::size_t> (n - silence)];
    }

    struct Case { std::string name; double attack, sustain; };

    for (const auto& c : { Case { "attack", 50.0, 0.0 }, Case { "sustain", 0.0, 50.0 } })
    {
        const auto offsets = offsetTrace ({ audio }, transientBand (BandType::bell, hz, 1.0, 0.0, c.attack, c.sustain, 30.0), silence);

        std::vector<Extreme> peaks;
        std::vector<int> durations;

        for (int k = 0; k < bursts; ++k)
        {
            const auto from = silence + k * period;
            peaks.push_back (extreme (offsets, from, from + period));
            durations.push_back (duration (offsets, from, from + period));
        }

        for (int k = 1; k < bursts; ++k)
        {
            const auto& first = peaks.front();
            const auto& later = peaks[static_cast<std::size_t> (k)];
            const auto durationRatio = static_cast<double> (durations.front()) / durations[static_cast<std::size_t> (k)];

            INFO (c.name << ": the first burst peaks at " << first.value << " dB, over half of it for " << msOf (durations.front())
                         << " ms; burst " << k + 1 << " at " << later.value << " dB, for " << msOf (durations[static_cast<std::size_t> (k)]) << " ms");
            CHECK (std::abs (first.value) > 6.0);
            CHECK (std::abs (first.value - later.value) < 1.0);
            CHECK (std::abs (durationRatio - 1.0) < 0.1);
        }

        MESSAGE ("transient, after silence (" << c.name << "): first " << peaks.front().value << " dB for " << msOf (durations.front())
                                             << " ms, then " << peaks.back().value << " dB for " << msOf (durations.back()) << " ms");
    }
}

TEST_CASE ("transient, after silence, the negative control: on L instead of max(L, floor), the first burst is not shaped as the others")
{
    // The same scene in levels: -120 dB, then bursts over a room at -90 dB.
    const auto silence = samplesOf (1000.0), period = samplesOf (1000.0), bursts = 3;
    std::vector<double> levels (static_cast<std::size_t> (silence + bursts * period), -120.0);

    for (int n = silence; n < static_cast<int> (levels.size()); ++n)
        levels[static_cast<std::size_t> (n)] = std::max (-6.0 - 150.0 * static_cast<double> ((n - silence) % period) / rate, -90.0);

    const auto withFloor = shape (levels, 25.0, 0.0);
    const auto withoutFloor = shapeWithoutFloor (levels, 25.0);
    const auto firstOf = [&] (const std::vector<double>& o) { return extreme (o, silence, silence + period).value; };
    const auto lastOf = [&] (const std::vector<double>& o) { return extreme (o, silence + 2 * period, silence + 3 * period).value; };

    INFO ("with the floor: " << firstOf (withFloor) << " then " << lastOf (withFloor) << " dB; without: "
                             << firstOf (withoutFloor) << " then " << lastOf (withoutFloor) << " dB");
    CHECK (std::abs (firstOf (withFloor) - lastOf (withFloor)) < 0.01);
    CHECK (std::abs (firstOf (withoutFloor) - lastOf (withoutFloor)) > 3.0);
}

//==============================================================================
TEST_CASE_TEMPLATE ("exact bypass: Transient bands idle pass the input bit for bit: amounts at 0, or a key under the floor", Sample, float, double)
{
    const auto length = 24000;

    // Amounts at 0, on loud noise with -0 and denormals among it.
    auto loud = awkwardQuiet<Sample> (length, 31, 0.5);
    const std::vector<std::vector<Sample>> loudInput { loud, awkwardQuiet<Sample> (length, 32, 0.4) };
    const auto idle = render (rate, loudInput, transientAtZero (0.0, 0.0), fixedBlocks (64));

    for (std::size_t c = 0; c < loudInput.size(); ++c)
        CHECK (sameBits (idle[c], loudInput[c]));

    // Full amounts, on a key 20 dB under the floor.
    const std::vector<std::vector<Sample>> quiet { awkwardQuiet<Sample> (length, 33, 1.0e-4), awkwardQuiet<Sample> (length, 34, 1.0e-4) };
    const auto under = render (rate, quiet, transientAtZero (100.0, 100.0), fixedBlocks (64));

    for (std::size_t c = 0; c < quiet.size(); ++c)
        CHECK (sameBits (under[c], quiet[c]));
}

TEST_CASE_TEMPLATE ("exact bypass, return: Transient bands at 0 dB, a loud passage, then a key under the floor: the input again, bit for bit", Sample, float, double)
{
    constexpr double rangeDb = -6.0, floorDb = -60.0, peakDb = 6.0;
    const auto loud = static_cast<int> (0.25 * rate), length = static_cast<int> (1.0 * rate);

    std::vector<std::vector<Sample>> input { awkwardQuiet<Sample> (length, 21, 1.0e-4), awkwardQuiet<Sample> (length, 22, 1.0e-4) };
    const auto burst = std::vector<std::vector<Sample>> { noise<Sample> (loud, 23, 0.25), noise<Sample> (loud, 24, 0.25) };

    for (std::size_t c = 0; c < input.size(); ++c)
        std::copy (burst[c].begin(), burst[c].end(), input[c].begin());

    /*  The stated time. Once the key is under the floor every envelope runs
        on the floor, and the offset is at most |Range| gate(E_fast - floor),
        the gate under 3 u^2 for u = (E_fast - floor) / W. E_fast falls from
        at most P (here +6 dB, over any level the band-pass of this noise
        reaches) with the fast release: the offset is under the return to
        zero's 0.0001 dB after (fast release / 2) ln(3 |Range| (P - floor)^2
        / (W^2 0.0001)), counted from the end of the loud passage. One control
        step more for the step in which it lands. */
    const auto tau = transientFastReleaseMs * 0.001 * rate;
    const auto bound = 3.0 * std::abs (rangeDb) * std::pow (peakDb - floorDb, 2.0) / (transientGateWidthDb * transientGateWidthDb * returnToZeroDb);
    const auto stated = loud + static_cast<int> (std::ceil (0.5 * tau * std::log (bound))) + controlStep;
    int latest = -1;

    for (const auto& sizes : { fixedBlocks (64), fixedBlocks (1), randomBlocks (17) })
    {
        const auto output = render (rate, input, transientAtZero (60.0, 40.0, rangeDb), sizes);

        for (std::size_t c = 0; c < input.size(); ++c)
        {
            int lastDifferent = -1;

            for (int n = 0; n < length; ++n)
                if (std::memcmp (&output[c][static_cast<std::size_t> (n)], &input[c][static_cast<std::size_t> (n)], sizeof (Sample)) != 0)
                    lastDifferent = n;

            INFO ("channel " << c << ": the last sample changed is " << lastDifferent << ", the stated time " << stated);
            CHECK (lastDifferent > loud / 2);   // the bands did act
            CHECK (lastDifferent < stated);
            latest = std::max (latest, lastDifferent);
        }
    }

    MESSAGE ("exact bypass, return (Transient): stated " << msOf (stated - loud) << " ms after the key fell, measured "
                                                         << msOf (latest + 1 - loud) << " ms");
}

//==============================================================================
TEST_CASE ("gain limit: a Transient band at +-30 dB static whose offsets push the same way is the static band itself, bit for bit")
{
    const auto length = samplesOf (800.0);
    const auto input = toneBurst (length, 1000.0, 0.5, samplesOf (100.0), samplesOf (300.0));

    for (const auto staticDb : { 30.0, -30.0 })
    {
        const auto sign = staticDb > 0.0 ? 1.0 : -1.0;
        const auto shaped = transientBand (BandType::bell, 1000.0, 1.0, staticDb, 100.0 * sign, 100.0 * sign, 30.0 * sign);
        const std::vector<Event> transient { { 0, [shaped] (Chain& c) { c.setBand (0, shaped); } } };
        const std::vector<Event> plain { { 0, [staticDb] (Chain& c) { c.setBand (0, band (true, BandType::bell, 1000.0, 1.0, staticDb)); } } };

        const auto offsets = offsetTrace ({ input }, shaped);
        const auto pushed = extreme (offsets, 0, length);

        INFO ("static " << staticDb << " dB: offsets up to " << pushed.value << " dB");
        CHECK (pushed.value * sign > 20.0);
        CHECK (std::abs (pushed.value) <= 30.0);

        // Static + offset is clamped to the static gain itself: the same bits.
        const auto a = render (rate, std::vector<std::vector<double>> { input }, transient, fixedBlocks (64));
        const auto b = render (rate, std::vector<std::vector<double>> { input }, plain, fixedBlocks (64));
        CHECK (sameBits (a[0], b[0]));
    }
}

//==============================================================================
namespace
{
    std::vector<Event> transientEverything (int period)
    {
        return {
            { 0, [] (Chain& c)
              {
                  c.setBand (0, transientBand (BandType::bell, 300.0, 1.0, 2.0, 80.0, -40.0, -9.0));
                  c.setBand (1, transientBand (BandType::lowShelf, 150.0, 0.7, 0.0, -60.0, 70.0, 6.0, 0.5, -50.0));
                  auto sc = transientBand (BandType::bell, 2500.0, 2.0, 0.0, -100.0, 0.0, -12.0, 2.0);
                  sc.key = KeySource::sidechain;
                  c.setBand (2, sc);
                  auto own = transientBand (BandType::highShelf, 6000.0, 0.7, -2.0, 50.0, 50.0, -6.0);
                  own.dfilt = DetectorFilter::own;
                  own.dtype = DetectorType::highPass;
                  own.detectorFrequency = 5000.0;
                  c.setBand (3, own);
                  c.setBand (4, dynamicBand (BandType::bell, 1200.0, 4.0, -3.0, -50.0, -6.0));
                  c.setChain ({ false, -1.0, true });
              } },
            { period, [] (Chain& c)
              {
                  c.setChain ({ false, -1.0, false });                                                        // unlinked
                  c.setBand (0, transientBand (BandType::bell, 300.0, 1.0, 2.0, -50.0, 90.0, -12.0, 3.0, -40.0));  // amounts, Time, floor
              } },
            { 2 * period, [] (Chain& c)
              {
                  c.setChain ({ false, 0.0, true });
                  c.setLinkExclusions (1u);                                                                    // left channel out
                  auto s = transientBand (BandType::bell, 1200.0, 4.0, -3.0, 70.0, 20.0, -6.0);
                  c.setBand (4, s);                                                                            // Dynamic to Transient
                  c.setBand (1, dynamicBand (BandType::lowShelf, 150.0, 0.7, 0.0, -36.0, 6.0));               // and back
              } },
            { 2 * period + 300, [] (Chain& c)
              {
                  c.setBand (1, transientBand (BandType::lowShelf, 150.0, 0.7, 0.0, -60.0, 70.0, 6.0, 0.5, -50.0));   // turned round mid-fade
              } },
            { 3 * period, [] (Chain& c)
              {
                  c.setLinkExclusions (0u);
                  c.setChain ({ true, 0.0, true });
              } },
            { 3 * period + 2000, [] (Chain& c) { c.setChain ({ false, 0.0, true }); } },
        };
    }
}

TEST_CASE_TEMPLATE ("block-size invariance, again: Transient bands, both keys, the link and their own values moving, bit for bit", Sample, float, double)
{
    constexpr int period = 7 * 1024;
    const auto length = 5 * period;

    std::vector<std::vector<Sample>> input { noise<Sample> (length, 71, 0.4), noise<Sample> (length, 72, 0.1) };
    const std::vector<std::vector<Sample>> sidechain { noise<Sample> (length, 73, 0.5), noise<Sample> (length, 74, 0.05) };

    // Bursts, so that the shapers act all along; -0 and denormals among the samples.
    for (auto& channel : input)
    {
        for (std::size_t n = 0; n < channel.size(); ++n)
            channel[n] *= static_cast<Sample> ((n / 1500) % 2 == 0 ? 1.0 : 0.01);

        for (std::size_t n = 0; n < channel.size(); n += 89)
            channel[n] = -Sample (0);

        for (std::size_t n = 13; n < channel.size(); n += 103)
            channel[n] = std::numeric_limits<Sample>::denorm_min();
    }

    const auto reference = render (rate, input, transientEverything (period), fixedBlocks (64), sidechain);
    CHECK_FALSE (sameBits (reference[0], input[0]));

    struct Cut { std::string name; BlockSizes sizes; };
    const std::vector<Cut> cuts {
        { "1", fixedBlocks (1) },     { "7", fixedBlocks (7) },       { "128", fixedBlocks (128) },
        { "480", fixedBlocks (480) }, { "4096", fixedBlocks (4096) }, { "random", randomBlocks (99) },
    };

    for (const auto& cut : cuts)
    {
        const auto output = render (rate, input, transientEverything (period), cut.sizes, sidechain);

        INFO ("blocks of " << cut.name);

        for (std::size_t c = 0; c < input.size(); ++c)
            CHECK (sameBits (output[c], reference[c]));
    }
}

TEST_CASE ("channel linking, Transient: a burst on one side of a stereo signal: linked, the same offset on both; unlinked, on its own side only")
{
    const auto length = samplesOf (400.0);
    std::vector<std::vector<double>> input { noise<double> (length, 61, 1.0e-4), noise<double> (length, 62, 1.0e-4) };

    for (int n = 4800; n < 7200; ++n)
        input[0][static_cast<std::size_t> (n)] += 0.5 * std::sin (2.0 * pi * 3000.0 * n / rate);

    for (const auto isLinked : { true, false })
    {
        Chain chain;
        chain.prepare (rate, 2);
        chain.setBand (0, transientBand (BandType::bell, 3000.0, 1.0, 0.0, -100.0, 0.0, -12.0));
        chain.setChain ({ false, 0.0, isLinked });

        auto audio = input;
        double most = 0.0;
        auto same = true, rightStill = true;

        for (int n = 0; n < length; ++n)
        {
            double* p[] = { audio[0].data() + n, audio[1].data() + n };
            chain.process (p, 2, 1);

            const auto left = chain.offsetDb (0, 0), right = chain.offsetDb (0, 1);
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

TEST_CASE ("layouts, Transient: 1, 2, 6 and 64 channels unlinked, every channel exactly as if it ran alone")
{
    constexpr int length = 3000;

    const auto events = [] (bool isLinked)
    {
        return std::vector<Event> { { 0, [isLinked] (Chain& c)
        {
            c.setBand (0, transientBand (BandType::bell, 700.0, 1.0, 2.0, 80.0, -50.0, -9.0, 0.5));
            c.setBand (3, transientBand (BandType::highShelf, 4000.0, 0.7, 0.0, -100.0, 30.0, -6.0, 0.25, -70.0));
            c.setBand (6, transientBand (BandType::lowShelf, 200.0, 0.7, 0.0, 40.0, 100.0, 4.0, 1.0, -80.0));
            c.setChain ({ false, 0.0, isLinked });
        } } };
    };

    for (const auto channels : { 1, 2, 6, 64 })
    {
        std::vector<std::vector<double>> input;

        for (int c = 0; c < channels; ++c)
        {
            auto channel = noise<double> (length, static_cast<std::uint32_t> (900 + c), 0.05 + 0.01 * (c % 40));

            for (int n = 0; n < length; ++n)
                channel[static_cast<std::size_t> (n)] *= (n + 97 * c) % 1000 < 300 ? 1.0 : 0.02;

            input.push_back (channel);
        }

        const auto together = render (rate, input, events (false), fixedBlocks (37));

        INFO (channels << " channels");
        CHECK_FALSE (sameBits (together[0], input[0]));

        for (int c = 0; c < channels; ++c)
        {
            const auto alone = render (rate, std::vector<std::vector<double>> { input[static_cast<std::size_t> (c)] },
                                       events (true), fixedBlocks (37));
            CHECK (sameBits (together[static_cast<std::size_t> (c)], alone[0]));
        }
    }
}

TEST_CASE ("realtime: 500 blocks of Transient bands, both keys, their values and the link moving, allocate nothing")
{
    namespace rt = likyu::dsp::rt;
    REQUIRE (rt::isCounting());

    Chain chain;
    chain.prepare (96000.0, 2);

    auto left = noise<double> (64, 1, 0.5), right = noise<double> (64, 2, 0.5);
    auto sideLeft = noise<double> (64, 3, 0.5), sideRight = noise<double> (64, 4, 0.5);
    double* channels[] = { left.data(), right.data() };
    const double* side[] = { sideLeft.data(), sideRight.data() };
    Lcg random { 6 };

    rt::resetCounts();

    for (int block = 0; block < 500; ++block)
    {
        if (block % 5 == 0)
        {
            for (int b = 0; b < numBands; ++b)
            {
                auto s = transientBand (static_cast<BandType> (random.next() % numBandTypes), 20.0 * std::pow (1000.0, random.unit()),
                                        0.1 * std::pow (400.0, random.unit()), 60.0 * random.unit() - 30.0,
                                        200.0 * random.unit() - 100.0, 200.0 * random.unit() - 100.0, 60.0 * random.unit() - 30.0,
                                        0.25 * std::pow (16.0, random.unit()), -90.0 + 70.0 * random.unit());
                s.on = random.unit() < 0.8;
                s.gmode = static_cast<GainMode> (random.next() % numGainModes);
                s.key = static_cast<KeySource> (random.next() % numKeySources);
                s.dfilt = static_cast<DetectorFilter> (random.next() % numDetectorFilters);
                s.dtype = static_cast<DetectorType> (random.next() % numDetectorTypes);
                chain.setBand (b, s);
            }

            chain.setChain ({ random.unit() < 0.2, 48.0 * random.unit() - 24.0, random.unit() < 0.5 });
            chain.setLinkExclusions (random.next() % 4u);
        }

        // Loud and quiet blocks, so that the gates open and close.
        const auto gain = block % 7 < 3 ? 1.0 : 1.0e-4;

        for (int n = 0; n < 64; ++n)
        {
            left[static_cast<std::size_t> (n)] = gain * random.bipolar();
            right[static_cast<std::size_t> (n)] = gain * random.bipolar();
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
