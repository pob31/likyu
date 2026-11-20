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
    Movement (PRD, Filter core: smoothing, update rates; Band model:
    click-free switches). Acceptance row "modulation safety", the switches
    the PRD's rule asks to be click-free, and the check on the 16-sample
    sub-block that the PRD leaves "to be confirmed by the modulation tests".

    THE CRITERION, proposed here because the PRD's rule has no number: a
    switch puts nothing above -70 dB re the signal into the meter. Measured
    2026-11-20 with the smoothstep crossfade: -81 dB at worst (bell to low
    shelf, whose g scaling differs), -95 to -99 dB for the others; a linear
    crossfade measured -52 dB there, and a hard switch -0.5 dB.

    HOW A CLICK IS MEASURED. A click is broadband; the test signal is not.
    A 500 Hz sine goes through the band, and the output goes through a
    fourth-order high-pass at 8 kHz (two SVF high-pass sections, the
    instrument ResponseTests.cpp has already checked), which leaves the sine
    96 dB down. The peak of what comes out of it around a switch, relative
    to the sine, is the click. A hard switch, coefficients changed between
    two samples, is the negative control: the instrument must see it.
*/

#include <doctest/doctest.h>

#include "DspTestSupport.h"

#include <dsp/BandCoefficients.h>
#include <dsp/Chain.h>
#include <dsp/Smoother.h>
#include <dsp/Svf.h>
#include <dsp/Tan.h>

#include <cmath>
#include <functional>
#include <string>

using namespace likyu::dsp;
using namespace likyutest::dsp;

namespace
{
    constexpr double rate = 48000.0;

    //==========================================================================
    /** The click meter: a fourth-order high-pass at 8 kHz and a peak hold. */
    struct ClickMeter
    {
        SvfCoefficients<double> section;
        double a1 = 0.0, a2 = 0.0, b1 = 0.0, b2 = 0.0;

        ClickMeter()
        {
            Warp warp;
            warp.prepare (rate);
            const auto k = 1.0 / 0.7071;
            section = makeSvfCoefficients (warp.g (8000.0), k, 1.0, -k, -1.0);
        }

        double operator() (double x) noexcept
        {
            return svfTick (svfTick (x, a1, a2, section), b1, b2, section);
        }
    };

    constexpr double sineHz = 500.0, sineLevel = 0.5;
    constexpr int switchAt = 24000;

    double sine (int n)
    {
        return sineLevel * std::sin (2.0 * pi * sineHz * n / rate);
    }

    double toDb (double ratio)
    {
        return 20.0 * std::log10 (std::max (ratio, 1.0e-12));
    }

    /** The click, in dB relative to the sine: the meter's peak in the 50 ms
        from the switch, against its peak in the steady 250 ms before it. The
        steady figure is the floor the measurement can see. */
    struct Click
    {
        double switchDb;
        double steadyDb;
    };

    Click measure (const std::vector<double>& output)
    {
        ClickMeter meter;
        double steady = 0.0, around = 0.0;

        for (int n = 0; n < static_cast<int> (output.size()); ++n)
        {
            const auto y = std::abs (meter (output[static_cast<std::size_t> (n)]));

            if (n >= switchAt - 12000 && n < switchAt)
                steady = std::max (steady, y);
            else if (n >= switchAt && n < switchAt + 2400)
                around = std::max (around, y);
        }

        return { toDb (around / sineLevel), toDb (steady / sineLevel) };
    }

    /** A band switched from one setting to another at `switchAt`, through the chain. */
    Click switched (const BandSettings& from, const BandSettings& to)
    {
        std::vector<double> input (static_cast<std::size_t> (switchAt + 12000));

        for (int n = 0; n < static_cast<int> (input.size()); ++n)
            input[static_cast<std::size_t> (n)] = sine (n);

        const std::vector<Event> events {
            { 0, [from] (Chain& c) { c.setBand (0, from); } },
            { switchAt, [to] (Chain& c) { c.setBand (0, to); } },
        };

        return measure (render (rate, std::vector<std::vector<double>> { input }, events, fixedBlocks (64))[0]);
    }

    /** The same switch done hard: coefficients changed between two samples. */
    Click switchedHard (const BandSettings& from, const BandSettings& to)
    {
        Warp warp;
        warp.prepare (rate);

        const auto coefficients = [&warp] (const BandSettings& s)
        {
            if (! s.on)
                return makeSvfCoefficients (warp.g (s.frequency), 1.0 / s.q, 1.0, 0.0, 0.0);

            return designBand (s.type, s.qmode, warp.g (s.frequency), 1.0 / s.q, bandA (s.gainDb), bandSqrtA (s.gainDb));
        };

        const auto before = coefficients (from), after = coefficients (to);
        std::vector<double> output (static_cast<std::size_t> (switchAt + 12000));
        double ic1 = 0.0, ic2 = 0.0;

        for (int n = 0; n < static_cast<int> (output.size()); ++n)
            output[static_cast<std::size_t> (n)] = svfTick (sine (n), ic1, ic2, n < switchAt ? before : after);

        return measure (output);
    }

    struct Switch
    {
        std::string name;
        BandSettings from, to;
    };

    std::vector<Switch> switches()
    {
        return {
            { "on", band (false, BandType::bell, 700.0, 2.0, 12.0), band (true, BandType::bell, 700.0, 2.0, 12.0) },
            { "off", band (true, BandType::bell, 700.0, 2.0, 12.0), band (false, BandType::bell, 700.0, 2.0, 12.0) },
            { "bell to low shelf", band (true, BandType::bell, 700.0, 2.0, 12.0), band (true, BandType::lowShelf, 700.0, 2.0, 12.0) },
            { "high-pass to low-pass", band (true, BandType::highPass, 300.0, 0.707, 0.0), band (true, BandType::lowPass, 300.0, 0.707, 0.0) },
            { "notch to band-pass", band (true, BandType::notch, 600.0, 4.0, 0.0), band (true, BandType::bandPass, 600.0, 4.0, 0.0) },
            { "Q mode", band (true, BandType::bell, 500.0, 4.0, -18.0), band (true, BandType::bell, 500.0, 4.0, -18.0, QMode::constant) },
            { "high shelf to bell", band (true, BandType::highShelf, 400.0, 0.7, 12.0), band (true, BandType::bell, 400.0, 0.7, 12.0) },
        };
    }
}

//==============================================================================
TEST_CASE ("ramp: lands exactly on its target after its length, and restarts from where it is")
{
    Ramp ramp;
    ramp.setLength (60);
    ramp.jumpTo (0.0);
    ramp.setTarget (0.1);

    for (int step = 1; step < 60; ++step)
    {
        REQUIRE (ramp.isMoving());
        ramp.step();
    }

    CHECK (ramp.step() == 0.1);     // the target itself, not 60 increments of 0.1 / 60
    CHECK_FALSE (ramp.isMoving());
    CHECK (ramp.step() == 0.1);

    // A new target mid-ramp: a fresh ramp of the full length from where it is.
    ramp.setTarget (1.0);
    for (int step = 0; step < 30; ++step)
        ramp.step();

    const auto halfway = ramp.getCurrent();
    ramp.setTarget (-1.0);

    for (int step = 1; step < 60; ++step)
        ramp.step();

    CHECK (ramp.isMoving());
    CHECK (ramp.step() == -1.0);
    CHECK (halfway == doctest::Approx (0.55));

    // The same target again is no new ramp; the current value as target ends one.
    ramp.setTarget (-1.0);
    CHECK_FALSE (ramp.isMoving());
    ramp.setTarget (5.0);
    ramp.step();
    ramp.setTarget (ramp.getCurrent());
    CHECK_FALSE (ramp.isMoving());

    // Lengths: at least one step; 20 ms in 16-sample steps.
    ramp.setLength (0);
    CHECK (ramp.getLength() == 1);
    CHECK (rampSteps (0.020, 48000.0, 16) == 60);
    CHECK (rampSteps (0.020, 44100.0, 16) == 55);
    CHECK (rampSteps (0.020, 192000.0, 16) == 240);
    CHECK (rampSteps (0.020, 0.0, 16) == 1);
}

//==============================================================================
TEST_CASE ("modulation safety: gain, Q and frequency stepped at audio rate, on a sine and on silence")
{
    for (const auto onSilence : { false, true })
    {
        Chain chain;
        chain.prepare (rate, 1);
        Lcg random { onSilence ? 7u : 8u };

        std::vector<double> audio (1);
        double* pointer = audio.data();
        double peak = 0.0;

        for (int n = 0; n < 96000; ++n)
        {
            // A new target every sample, anywhere in the ranges, the type held per 0.1 s.
            const auto type = static_cast<BandType> ((n / 4800) % numBandTypes);
            chain.setBand (0, band (true, type, 20.0 * std::pow (1000.0, random.unit()), 0.1 * std::pow (400.0, random.unit()),
                                    60.0 * random.unit() - 30.0));

            audio[0] = onSilence ? 0.0 : sine (n);
            chain.process (&pointer, 1, 1);

            REQUIRE (std::isfinite (audio[0]));
            peak = std::max (peak, std::abs (audio[0]));

            if (onSilence)
                REQUIRE (audio[0] == 0.0);
        }

        INFO ("on " << std::string (onSilence ? "silence" : "a sine") << ", peak " << peak);
        CHECK (peak < 50.0);    // +30 dB is 31.6 times; nothing grows past what the gains allow
    }
}

TEST_CASE ("modulation safety: Q 40 at +30 dB swept 20 Hz to 20 kHz and back at audio rate, then left to ring out")
{
    for (const auto r : { 44100.0, 192000.0 })
    {
        Chain chain;
        chain.prepare (r, 1);

        std::vector<double> audio (1);
        double* pointer = audio.data();
        Lcg random { 9 };

        // Half a second of sweeping on noise, a sweep up and back every 4800
        // samples; then the band is left where it is, on silence. At +30 dB a
        // proportional bell of Q 40 has poles of Q 225 and rings for seconds:
        // what is asked is that the ring decays, not that it has ended.
        const auto sweep = static_cast<int> (r / 2), total = 2 * sweep;
        double peak = 0.0, earlyRing = 0.0, lateRing = 0.0;

        for (int n = 0; n < total; ++n)
        {
            if (n < sweep)
            {
                const auto phase = static_cast<double> (n % 4800) / 4800.0;
                const auto octaves = 10.0 * (phase < 0.5 ? 2.0 * phase : 2.0 - 2.0 * phase);
                chain.setBand (0, band (true, BandType::bell, 20.0 * std::pow (2.0, octaves), 40.0, 30.0));
            }

            audio[0] = n < sweep ? 0.1 * random.bipolar() : 0.0;
            chain.process (&pointer, 1, 1);

            REQUIRE (std::isfinite (audio[0]));
            peak = std::max (peak, std::abs (audio[0]));

            if (n >= sweep + sweep / 8 && n < sweep + sweep / 4)
                earlyRing = std::max (earlyRing, std::abs (audio[0]));
            else if (n >= total - sweep / 8)
                lateRing = std::max (lateRing, std::abs (audio[0]));
        }

        INFO ("rate " << r << ", peak " << peak << ", ring early " << earlyRing << ", late " << lateRing);
        CHECK (peak < 100.0);
        CHECK (lateRing < earlyRing);
    }
}

TEST_CASE ("modulation safety: silence stays exactly silent through every switch and every ramp")
{
    std::vector<double> silence (12000, 0.0);
    std::vector<Event> events;

    for (int i = 0; i < 40; ++i)
    {
        events.push_back ({ i * 300, [i] (Chain& c)
        {
            c.setBand (i % numBands, band (i % 3 != 0, static_cast<BandType> (i % numBandTypes), 30.0 * (i + 1), 0.2 * (i + 1), i - 20.0));
            c.setChain ({ i % 7 == 0, i - 20.0 });
        } });
    }

    const auto output = render (rate, std::vector<std::vector<double>> { silence }, events, fixedBlocks (13));

    for (const auto sample : output[0])
        REQUIRE (sample == 0.0);
}

//==============================================================================
TEST_CASE ("switches: on, off, type and Q mode switch without a click; a hard switch would click")
{
    for (const auto& s : switches())
    {
        const auto soft = switched (s.from, s.to);
        const auto hard = switchedHard (s.from, s.to);

        INFO (s.name << ": through the chain " << soft.switchDb << " dB, steady " << soft.steadyDb
                     << " dB; done hard " << hard.switchDb << " dB");

        // The instrument sees a click: a hard switch is far above the floor.
        CHECK (hard.switchDb > soft.steadyDb + 30.0);

        // Through the identity, along a smoothstep: under -70 dB (proposed
        // 2026-11-20; the worst measured then was -81 dB, bell to low shelf).
        CHECK (soft.switchDb < -70.0);
    }
}

TEST_CASE ("switches: the bypass crossfades without a click")
{
    std::vector<double> input (static_cast<std::size_t> (switchAt + 12000));

    for (int n = 0; n < static_cast<int> (input.size()); ++n)
        input[static_cast<std::size_t> (n)] = sine (n);

    for (const auto toBypass : { true, false })
    {
        const std::vector<Event> events {
            { 0, [toBypass] (Chain& c)
              {
                  c.setBand (0, band (true, BandType::bell, 500.0, 2.0, 12.0));
                  c.setChain ({ ! toBypass, 0.0 });
              } },
            { switchAt, [toBypass] (Chain& c) { c.setChain ({ toBypass, 0.0 }); } },
        };

        const auto click = measure (render (rate, std::vector<std::vector<double>> { input }, events, fixedBlocks (64))[0]);

        INFO (std::string (toBypass ? "into" : "out of") << " bypass: " << click.switchDb << " dB, steady " << click.steadyDb << " dB");
        CHECK (click.switchDb < -70.0);
    }
}

//==============================================================================
namespace
{
    /*  THE NEW SWITCHES OF PHASE 3. A dynamic bell at 700 Hz, Q 2, +6 dB
        static, keyed by the 500 Hz sine itself (about -10.6 dB through its
        band-pass) or by a sidechain, switched at `switchAt`. Each switch
        changes the band's steady gain by several dB; the hard control
        switches a static bell between the same two steady gains. */
    constexpr double dynamicStaticDb = 6.0;

    BandSettings dynamicBell (double rangeDb = -12.0)
    {
        return dynamicBand (BandType::bell, 700.0, 2.0, dynamicStaticDb, -30.0, rangeDb, 4.0, 5.0, 120.0);
    }

    struct DynamicSwitch
    {
        std::string name;
        std::vector<double> amplitudes { sineLevel };   // per main channel
        int measured = 0;                               // the channel the click is read on
        bool sidechain = false;                         // a silent mono sidechain ...
        int sidechainFrom = 0;                          // ... from this sample on
        std::function<void (Chain&)> before, after;
    };

    struct DynamicClick
    {
        Click soft, hard;
        double gainBeforeDb = 0.0, gainAfterDb = 0.0;
    };

    DynamicClick switchedDynamic (const DynamicSwitch& d)
    {
        const auto length = switchAt + 12000;
        const auto channels = static_cast<int> (d.amplitudes.size());
        std::vector<std::vector<double>> audio (d.amplitudes.size(), std::vector<double> (static_cast<std::size_t> (length)));

        for (std::size_t c = 0; c < audio.size(); ++c)
            for (int n = 0; n < length; ++n)
                audio[c][static_cast<std::size_t> (n)] = d.amplitudes[c] * std::sin (2.0 * pi * sineHz * n / rate);

        const std::vector<double> silence (static_cast<std::size_t> (length), 0.0);

        Chain chain;
        chain.prepare (rate, channels);
        d.before (chain);

        std::vector<double*> pointers (audio.size());
        DynamicClick result;

        for (int n = 0; n < length; n += 64)
        {
            if (n == switchAt)
            {
                result.gainBeforeDb = dynamicStaticDb + chain.offsetDb (0, d.measured);
                d.after (chain);
            }

            for (std::size_t c = 0; c < audio.size(); ++c)
                pointers[c] = audio[c].data() + n;

            const double* side[] = { silence.data() + n };
            Key<double> key;
            key.self = pointers.data();
            key.selfChannels = channels;

            if (d.sidechain && n >= d.sidechainFrom)
            {
                key.sidechain = side;
                key.sidechainChannels = 1;
            }

            chain.process (pointers.data(), channels, key, std::min (64, length - n));
        }

        result.gainAfterDb = dynamicStaticDb + chain.offsetDb (0, d.measured);
        result.soft = measure (audio[static_cast<std::size_t> (d.measured)]);

        // The hard control: the same two steady gains, a static bell switched between two samples.
        const auto at = [] (double gainDb) { return band (true, BandType::bell, 700.0, 2.0, gainDb); };
        result.hard = switchedHard (at (result.gainBeforeDb), at (result.gainAfterDb));
        return result;
    }

    std::vector<DynamicSwitch> dynamicSwitches()
    {
        const auto withStatic = [] { auto s = dynamicBell(); s.gmode = GainMode::staticGain; return s; };
        const auto own = [] (DetectorType type, double hz, double q)
        {
            auto s = dynamicBell();
            s.dfilt = DetectorFilter::own;
            s.dtype = type;
            s.detectorFrequency = hz;
            s.detectorQ = q;
            return s;
        };
        const auto keyedBy = [] (KeySource key) { auto s = dynamicBell(); s.key = key; return s; };
        const auto shaped = [] { auto s = dynamicBell(); s.gmode = GainMode::transient; s.transientAttackPercent = 100.0; return s; };
        const auto below = [] { auto s = dynamicBell(); s.direction = Direction::below; return s; };

        // Stereo, the left louder: linked, the right is cut as the left is.
        const std::vector<double> loudLeft { 2.0, sineLevel };

        return {
            { "gain mode, Static to Dynamic", { sineLevel }, 0, false, 0,
              [withStatic] (Chain& c) { c.setBand (0, withStatic()); }, [] (Chain& c) { c.setBand (0, dynamicBell()); } },
            { "gain mode, Dynamic to Static", { sineLevel }, 0, false, 0,
              [] (Chain& c) { c.setBand (0, dynamicBell()); }, [withStatic] (Chain& c) { c.setBand (0, withStatic()); } },
            { "gain mode, Dynamic to Transient", { sineLevel }, 0, false, 0,
              [] (Chain& c) { c.setBand (0, dynamicBell()); }, [shaped] (Chain& c) { c.setBand (0, shaped()); } },
            { "gain mode, Transient to Dynamic", { sineLevel }, 0, false, 0,
              [shaped] (Chain& c) { c.setBand (0, shaped()); }, [] (Chain& c) { c.setBand (0, dynamicBell()); } },
            { "detector filter, Follow to Own", { sineLevel }, 0, false, 0,
              [] (Chain& c) { c.setBand (0, dynamicBell()); },
              [own] (Chain& c) { c.setBand (0, own (DetectorType::bandPass, 3000.0, 4.0)); } },
            { "detector type, band-pass to high-pass", { sineLevel }, 0, false, 0,
              [own] (Chain& c) { c.setBand (0, own (DetectorType::bandPass, 500.0, 1.0)); },
              [own] (Chain& c) { c.setBand (0, own (DetectorType::highPass, 5000.0, 0.7)); } },
            { "direction, Above to Below", { sineLevel }, 0, false, 0,
              [] (Chain& c) { c.setBand (0, dynamicBell()); }, [below] (Chain& c) { c.setBand (0, below()); } },
            { "key, Self to a silent sidechain", { sineLevel }, 0, true, 0,
              [keyedBy] (Chain& c) { c.setBand (0, keyedBy (KeySource::self)); },
              [keyedBy] (Chain& c) { c.setBand (0, keyedBy (KeySource::sidechain)); } },
            { "the fallback, a silent sidechain appearing", { sineLevel }, 0, true, switchAt,
              [keyedBy] (Chain& c) { c.setBand (0, keyedBy (KeySource::sidechain)); }, [] (Chain&) {} },
            { "link, Linked to Unlinked", loudLeft, 1, false, 0,
              [] (Chain& c) { c.setBand (0, dynamicBell (-24.0)); c.setChain ({ false, 0.0, true }); },
              [] (Chain& c) { c.setChain ({ false, 0.0, false }); } },
            { "a band's channels, the right left out of the band", loudLeft, 1, false, 0,
              [] (Chain& c) { c.setBand (0, dynamicBell (-24.0)); },
              [] (Chain& c) { c.setBandChannels (0, 2u); } },
        };
    }
}

TEST_CASE ("switches: gain mode, detector, direction, key, the fallback, link and a band's channels switch without a click; a hard switch would click")
{
    for (const auto& s : dynamicSwitches())
    {
        const auto r = switchedDynamic (s);

        INFO (s.name << ": steady gain " << r.gainBeforeDb << " dB, then " << r.gainAfterDb << " dB; through the chain "
                     << r.soft.switchDb << " dB, steady " << r.soft.steadyDb << " dB; done hard " << r.hard.switchDb << " dB");
        MESSAGE (s.name << ": " << r.gainBeforeDb << " to " << r.gainAfterDb << " dB; " << r.soft.switchDb << " dB through the chain, "
                        << r.hard.switchDb << " dB hard");

        // The switch changes the sound, by several dB, and done hard it would click.
        CHECK (std::abs (r.gainAfterDb - r.gainBeforeDb) > 4.0);
        CHECK (r.hard.switchDb > r.soft.steadyDb + 30.0);

        // Through the identity: under the same -70 dB as the Phase 2 switches.
        CHECK (r.soft.switchDb < -70.0);
    }
}

TEST_CASE ("switches: Listen in and out crossfades without a click; switched hard it would click")
{
    // A +6 dB bell at 700 Hz on the sine, its own band-pass detector at 700 Hz, Q 2.
    auto s = band (true, BandType::bell, 700.0, 2.0, 6.0);
    s.dfilt = DetectorFilter::own;
    s.detectorFrequency = 700.0;
    s.detectorQ = 2.0;

    std::vector<double> input (static_cast<std::size_t> (switchAt + 12000));

    for (int n = 0; n < static_cast<int> (input.size()); ++n)
        input[static_cast<std::size_t> (n)] = sine (n);

    for (const auto in : { true, false })
    {
        const std::vector<Event> events {
            { 0, [s, in] (Chain& c) { c.setBand (0, s); c.setListen (in ? -1 : 0); } },
            { switchAt, [in] (Chain& c) { c.setListen (in ? 0 : -1); } },
        };

        const auto soft = measure (render (rate, std::vector<std::vector<double>> { input }, events, fixedBlocks (64))[0]);

        // Hard: the chain's output, then the detector's, from one sample to the next.
        const auto chainOut = render (rate, std::vector<std::vector<double>> { input }, { { 0, [s] (Chain& c) { c.setBand (0, s); } } }, fixedBlocks (64))[0];
        const auto listened = render (rate, std::vector<std::vector<double>> { input }, { { 0, [s] (Chain& c) { c.setBand (0, s); c.setListen (0); } } }, fixedBlocks (64))[0];
        std::vector<double> hardOut (input.size());

        for (std::size_t n = 0; n < input.size(); ++n)
            hardOut[n] = (static_cast<int> (n) < switchAt) == in ? chainOut[n] : listened[n];

        const auto hard = measure (hardOut);

        INFO (std::string (in ? "into" : "out of") << " Listen: " << soft.switchDb << " dB, steady " << soft.steadyDb << " dB; hard " << hard.switchDb << " dB");
        MESSAGE (std::string (in ? "into" : "out of") << " Listen: " << soft.switchDb << " dB through the chain, " << hard.switchDb << " dB hard");
        CHECK (hard.switchDb > soft.steadyDb + 30.0);
        CHECK (soft.switchDb < -70.0);
    }
}

//==============================================================================
namespace
{
    /*  THE SWITCHES OF PHASE 4, where Transient is shaping. A steady sine has
        no attack and no sustain, so its Transient offset is 0 throughout: the
        sine here is the same 500 Hz with its level swung by 6 dB three times
        a second, and the bell (700 Hz, Q 2, +6 dB static, Range 12 dB, keyed
        by the sine) moves with it before and after the switch. Into
        Transient from Static the detector starts cold, so the note the band
        enters on reads as an onset under the fade in: that is measured too.
        Dynamic to and from Transient, with its hard control, is in the list
        above, on the steady sine. */
    double swung (int n)
    {
        return sine (n) * (0.75 + 0.25 * std::sin (2.0 * pi * 3.0 * n / rate));
    }

    BandSettings transientBell (double attackPercent = 100.0, double sustainPercent = 100.0)
    {
        return transientBand (BandType::bell, 700.0, 2.0, dynamicStaticDb, attackPercent, sustainPercent, -12.0);
    }

    struct SwungSwitch
    {
        std::string name;
        BandSettings from, to;
    };

    struct SwungClick
    {
        Click click;
        double offsetBeforeDb = 0.0, offsetAfterDb = 0.0;   // the largest |offset| in the 250 ms before, after
    };

    SwungClick switchedSwung (const SwungSwitch& s)
    {
        const auto length = switchAt + 12000;
        std::vector<double> audio (static_cast<std::size_t> (length));

        for (int n = 0; n < length; ++n)
            audio[static_cast<std::size_t> (n)] = swung (n);

        Chain chain;
        chain.prepare (rate, 1);
        chain.setBand (0, s.from);

        SwungClick result;

        for (int n = 0; n < length; n += controlStep)
        {
            if (n == switchAt)
                chain.setBand (0, s.to);

            double* p[] = { audio.data() + n };
            chain.process (p, 1, controlStep);

            const auto o = std::abs (chain.offsetDb (0, 0));

            if (n >= switchAt - 12000 && n < switchAt)
                result.offsetBeforeDb = std::max (result.offsetBeforeDb, o);
            else if (n >= switchAt)
                result.offsetAfterDb = std::max (result.offsetAfterDb, o);
        }

        result.click = measure (audio);
        return result;
    }

    std::vector<SwungSwitch> swungSwitches()
    {
        auto staticBell = transientBell();
        staticBell.gmode = GainMode::staticGain;
        auto raisedFloor = transientBell();
        raisedFloor.transientFloorDb = -20.0;
        auto slower = transientBell();
        slower.transientTime = 4.0;

        return {
            { "gain mode, Static to Transient", staticBell, transientBell() },
            { "gain mode, Transient to Static", transientBell(), staticBell },
            { "sustain, +100 % to -100 %", transientBell (100.0, 100.0), transientBell (100.0, -100.0) },
            { "attack, -100 % to +100 %", transientBell (-100.0, 100.0), transientBell (100.0, 100.0) },
            { "floor, -60 to -20 dB, inside the swing", transientBell(), raisedFloor },
            { "Time, 1x to 4x", transientBell(), slower },
        };
    }
}

TEST_CASE ("switches: into and out of Transient, and its amounts, floor and Time stepped while it shapes, without a click")
{
    for (const auto& s : swungSwitches())
    {
        const auto r = switchedSwung (s);

        INFO (s.name << ": " << r.click.switchDb << " dB through the chain, steady " << r.click.steadyDb
                     << " dB; offsets up to " << r.offsetBeforeDb << " dB before, " << r.offsetAfterDb << " dB after");
        MESSAGE (s.name << ": " << r.click.switchDb << " dB through the chain; offsets up to " << r.offsetBeforeDb
                        << " dB before, " << r.offsetAfterDb << " dB after");

        // The shaper is at work on one side of the switch at least.
        CHECK (std::max (r.offsetBeforeDb, r.offsetAfterDb) > 2.0);

        CHECK (r.click.switchDb < -70.0);
    }
}

//==============================================================================
TEST_CASE ("sub-block: how far g interpolated over 16 samples bends a glide, measured")
{
    /*  g is linear between grid points, so a glide is not exactly log-linear
        inside a step. On the fastest move a band can make, 20 Hz to 20 kHz in
        one 20 ms ramp (each step then covers 0.18 octave, some 217 cents),
        the bend reaches 44 cents near 20 kHz at 44.1 kHz (measured
        2026-11-20): a fifth of a step that lasts a third of a millisecond. On
        a one-octave move at 1 kHz it is under 0.04 cent. These bounds hold the
        measured figures; the 16-sample constant itself is confirmed by the
        modulation-safety tests above, which is what the PRD asks of it. */
    const auto worstBend = [] (double r, double fromHz, double toHz)
    {
        Warp warp;
        warp.prepare (r);

        Ramp ramp;
        ramp.setLength (rampSteps (smoothingSeconds, r, controlStep));
        ramp.jumpTo (std::log2 (fromHz));
        ramp.setTarget (std::log2 (toHz));

        double worst = 0.0;
        auto start = ramp.getCurrent();

        while (ramp.isMoving())
        {
            const auto end = ramp.step();
            const auto g0 = warp.g (std::exp2 (start)), g1 = warp.g (std::exp2 (end));

            for (int i = 0; i < controlStep; ++i)
            {
                const auto t = (i + 1.0) / controlStep;
                const auto interpolated = lerpExact (g0, g1, t);
                const auto exact = warp.g (std::exp2 (start + t * (end - start)));
                worst = std::max (worst, std::abs (1200.0 * std::log2 (std::atan (interpolated) / std::atan (exact))));
            }

            start = end;
        }

        return worst;
    };

    for (const auto r : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const auto fastest = worstBend (r, minFrequency, maxFrequency);
        const auto octave = worstBend (r, 1000.0, 2000.0);
        const auto topOctave = worstBend (r, 10000.0, 20000.0);

        INFO ("rate " << r << ": 20 Hz to 20 kHz " << fastest << " cent, 1 to 2 kHz " << octave
                      << " cent, 10 to 20 kHz " << topOctave << " cent");
        CHECK (fastest < 50.0);
        CHECK (octave < 0.05);
        CHECK (topOctave < 5.0);
    }
}

//==============================================================================
namespace
{
    /*  THE SWITCHES OF PHASE 5, on the same 500 Hz sine: a bell at +6 dB,
        Q 2, whose frequency mode, direction, key, link or range changes at
        `switchAt`. An Env band reads the sine through its own band-pass at
        500 Hz (-6 dB, over the span: at High for Up, Low for Down); a Track
        band locks on it. The hard control: a static bell switched between
        the two frequencies the band sits at before and after. */
    BandSettings fixedBell()
    {
        return band (true, BandType::bell, 700.0, 2.0, 6.0);
    }

    BandSettings envBell (EnvDirection direction = EnvDirection::up, double highHz = 1200.0)
    {
        return envBand (BandType::bell, 6.0, 300.0, highHz, -50.0, 30.0, 500.0, direction);
    }

    BandSettings trackBell (KeySource key = KeySource::self)
    {
        auto s = trackBand (BandType::bell, 700.0, 6.0, 300.0, 1200.0, 20.0);
        s.q = 2.0;
        s.key = key;
        return s;
    }

    struct FrequencySwitch
    {
        std::string name;
        std::vector<double> amplitudes { sineLevel };
        bool sidechain = false;
        std::function<void (Chain&)> before, after;
    };

    struct FrequencyClick
    {
        Click soft, hard;
        double hzBefore = 0.0, hzAfter = 0.0;
    };

    FrequencyClick switchedFrequency (const FrequencySwitch& f)
    {
        const auto length = switchAt + 12000;
        const auto channels = static_cast<int> (f.amplitudes.size());
        std::vector<std::vector<double>> audio (f.amplitudes.size(), std::vector<double> (static_cast<std::size_t> (length)));

        for (std::size_t c = 0; c < audio.size(); ++c)
            for (int n = 0; n < length; ++n)
                audio[c][static_cast<std::size_t> (n)] = f.amplitudes[c] * std::sin (2.0 * pi * sineHz * n / rate);

        const std::vector<double> silence (static_cast<std::size_t> (length), 0.0);

        Chain chain;
        chain.prepare (rate, channels);
        f.before (chain);

        std::vector<double*> pointers (audio.size());
        FrequencyClick result;
        const auto measured = channels - 1;

        for (int n = 0; n < length; n += 64)
        {
            if (n == switchAt)
            {
                result.hzBefore = chain.frequencyHz (0, measured);
                f.after (chain);
            }

            for (std::size_t c = 0; c < audio.size(); ++c)
                pointers[c] = audio[c].data() + n;

            const double* side[] = { silence.data() + n };
            Key<double> key;
            key.self = pointers.data();
            key.selfChannels = channels;

            if (f.sidechain)
            {
                key.sidechain = side;
                key.sidechainChannels = 1;
            }

            chain.process (pointers.data(), channels, key, std::min (64, length - n));
        }

        result.hzAfter = chain.frequencyHz (0, measured);
        result.soft = measure (audio[static_cast<std::size_t> (measured)]);

        const auto at = [] (double hz) { return band (true, BandType::bell, hz, 2.0, 6.0); };
        result.hard = switchedHard (at (result.hzBefore), at (result.hzAfter));
        return result;
    }

    std::vector<FrequencySwitch> frequencySwitches()
    {
        const std::vector<double> loudLeft { 2.0, sineLevel };
        const auto withRange = [] (double lowHz, double highHz) { auto s = envBell(); s.lowHz = lowHz; s.highHz = highHz; return s; };
        const auto withSpan = [] (double sensitivityDb, double spanDb)
        {
            auto s = envBell();
            s.sensitivityDb = sensitivityDb;
            s.spanDb = spanDb;
            return s;
        };

        return {
            { "frequency mode, Fixed to Env", { sineLevel }, false,
              [] (Chain& c) { c.setBand (0, fixedBell()); }, [] (Chain& c) { c.setBand (0, envBell()); } },
            { "frequency mode, Env to Track", { sineLevel }, false,
              [] (Chain& c) { c.setBand (0, envBell()); }, [] (Chain& c) { c.setBand (0, trackBell()); } },
            { "frequency mode, Track to Fixed", { sineLevel }, false,
              [] (Chain& c) { c.setBand (0, trackBell()); }, [] (Chain& c) { c.setBand (0, fixedBell()); } },
            { "Env direction, Up to Down", { sineLevel }, false,
              [] (Chain& c) { c.setBand (0, envBell()); }, [] (Chain& c) { c.setBand (0, envBell (EnvDirection::down)); } },
            { "Track key, Self to a silent sidechain", { sineLevel }, true,
              [] (Chain& c) { c.setBand (0, trackBell()); }, [] (Chain& c) { c.setBand (0, trackBell (KeySource::sidechain)); } },
            { "Track link, Linked to Unlinked", loudLeft, false,
              [] (Chain& c) { c.setBand (0, trackBell()); c.setBand (1, trackBand (BandType::bell, 300.0, 0.0, 200.0, 400.0)); },
              [] (Chain& c) { c.setChain ({ false, 0.0, false }); } },
            { "Env, High stepped from 1200 to 2400 Hz (it ramps)", { sineLevel }, false,
              [withRange] (Chain& c) { c.setBand (0, withRange (300.0, 1200.0)); },
              [withRange] (Chain& c) { c.setBand (0, withRange (300.0, 2400.0)); } },
            { "Env, Sensitivity and Span stepped (they ramp)", { sineLevel }, false,
              [withSpan] (Chain& c) { c.setBand (0, withSpan (-20.0, 30.0)); },
              [withSpan] (Chain& c) { c.setBand (0, withSpan (-10.0, 12.0)); } },
            { "Track, Low and High stepped so that the sine falls outside (they ramp)", { sineLevel }, false,
              [] (Chain& c) { c.setBand (0, trackBell()); },
              [] (Chain& c) { auto s = trackBell(); s.lowHz = 800.0; s.highHz = 2000.0; c.setBand (0, s); } },
        };
    }
}

TEST_CASE ("switches: the frequency modes, Env's direction, a Track band's key and link, and their ranges stepped, without a click")
{
    for (const auto& f : frequencySwitches())
    {
        const auto r = switchedFrequency (f);

        INFO (f.name << ": " << r.hzBefore << " Hz, then " << r.hzAfter << " Hz; through the chain " << r.soft.switchDb
                     << " dB, steady " << r.soft.steadyDb << " dB; done hard " << r.hard.switchDb << " dB");
        MESSAGE (f.name << ": " << r.hzBefore << " to " << r.hzAfter << " Hz; " << r.soft.switchDb << " dB through the chain, "
                        << r.hard.switchDb << " dB hard");

        CHECK (r.soft.switchDb < -70.0);
    }
}

TEST_CASE ("modulation safety: silence stays exactly silent through every frequency mode and its switches")
{
    std::vector<double> silence (24000, 0.0);
    std::vector<Event> events;

    for (int i = 0; i < 12; ++i)
    {
        events.push_back ({ i * 1700 + 3, [i] (Chain& c)
        {
            auto s = i % 3 == 0 ? envBell (i % 2 == 0 ? EnvDirection::up : EnvDirection::down)
                     : i % 3 == 1 ? trackBell() : fixedBell();
            s.confidenceScalesGain = i % 4 == 1;
            s.gmode = static_cast<GainMode> (i % numGainModes);
            c.setBand (i % numBands, s);
            c.setChain ({ false, 0.0, i % 5 != 2 });
        } });
    }

    const auto output = render (rate, std::vector<std::vector<double>> { silence }, events, fixedBlocks (13));

    for (const auto sample : output[0])
        REQUIRE (sample == 0.0);
}
