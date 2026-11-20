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
    A band's channels (PRD, Band model, Channels; decided by the author
    2026-11-20): each band applies to a set of the instance's channels, all
    of them by default. A channel a band leaves out is not filtered by it,
    is no part of its key, and has no lane of its.

    The case it was decided for is the first test: on 5.1, a low cut on the
    five full-range channels and not on the LFE, and a low-pass on the LFE
    alone, in one instance.
*/

#include <doctest/doctest.h>

#include "DspTestSupport.h"

#include <dsp/Chain.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace likyu::dsp;
using namespace likyutest::dsp;

namespace
{
    constexpr double rate = 48000.0;
    constexpr int lfe = 3;
    constexpr std::uint64_t allOfSix = 0x3f;

    std::vector<std::vector<double>> noiseOn (int channels, int length, std::uint32_t seed)
    {
        std::vector<std::vector<double>> audio;

        for (int c = 0; c < channels; ++c)
            audio.push_back (noise<double> (length, seed + static_cast<std::uint32_t> (c), 0.2));

        return audio;
    }

    double cents (double hz, double reference)
    {
        return 1200.0 * std::log2 (hz / reference);
    }
}

//==============================================================================
TEST_CASE ("a band's channels: on 5.1, a low cut on five channels and a low-pass on the LFE alone, each exactly as if it ran alone")
{
    constexpr int length = 6000;
    const auto input = noiseOn (6, length, 300);

    const auto lowCut = band (true, BandType::highPass, 120.0, 0.7, 0.0);
    const auto lowPass = band (true, BandType::lowPass, 120.0, 0.7, 0.0);

    const auto together = render (rate, input, { { 0, [lowCut, lowPass] (Chain& c)
    {
        c.setBand (0, lowCut);
        c.setBandChannels (0, std::uint64_t (1) << lfe);                    // every channel but the LFE
        c.setBand (7, lowPass);
        c.setBandChannels (7, allOfSix & ~(std::uint64_t (1) << lfe));      // the LFE alone
    } } }, fixedBlocks (37));

    for (int c = 0; c < 6; ++c)
    {
        const auto own = c == lfe ? lowPass : lowCut;
        const auto alone = render (rate, std::vector<std::vector<double>> { input[static_cast<std::size_t> (c)] },
                                   { { 0, [own] (Chain& chain) { chain.setBand (0, own); } } }, fixedBlocks (37));

        INFO ("channel " << c);
        CHECK (sameBits (together[static_cast<std::size_t> (c)], alone[0]));
        CHECK_FALSE (sameBits (together[static_cast<std::size_t> (c)], input[static_cast<std::size_t> (c)]));
    }
}

TEST_CASE ("a band's channels: a band that leaves every channel out changes nothing, bit for bit; bits beyond the channels are ignored")
{
    constexpr int length = 3000;
    const auto input = noiseOn (2, length, 320);
    const auto cut = band (true, BandType::bell, 1000.0, 1.0, -12.0);

    const auto none = render (rate, input, { { 0, [cut] (Chain& c)
    {
        c.setBand (2, cut);
        c.setBandChannels (2, ~std::uint64_t (0));
    } } }, fixedBlocks (64));

    for (std::size_t c = 0; c < 2; ++c)
        CHECK (sameBits (none[c], input[c]));

    // Channels 3 to 64 left out of a stereo instance: both channels are still in.
    const auto beyond = render (rate, input, { { 0, [cut] (Chain& c)
    {
        c.setBand (2, cut);
        c.setBandChannels (2, ~std::uint64_t (3));
    } } }, fixedBlocks (64));

    const auto plain = render (rate, input, { { 0, [cut] (Chain& c) { c.setBand (2, cut); } } }, fixedBlocks (64));

    for (std::size_t c = 0; c < 2; ++c)
        CHECK (sameBits (beyond[c], plain[c]));
}

TEST_CASE ("a band's channels: a channel left out is no part of the band's key, and the band does not act on it")
{
    const auto length = static_cast<int> (0.6 * rate);
    const auto burstAt = [] (std::vector<std::vector<double>>& audio, int channel, int from)
    {
        for (int n = from; n < from + 4800; ++n)
            audio[static_cast<std::size_t> (channel)][static_cast<std::size_t> (n)] += 0.5 * std::sin (2.0 * pi * 80.0 * n / rate);
    };

    std::vector<std::vector<double>> audio (6, std::vector<double> (static_cast<std::size_t> (length), 0.0));
    burstAt (audio, lfe, 2400);
    burstAt (audio, 0, 14400);
    const auto input = audio;

    Chain chain;
    chain.prepare (rate, 6);
    chain.setBand (0, dynamicBand (BandType::lowShelf, 80.0, 0.7, 0.0, -30.0, -9.0, 4.0, 2.0, 20.0));
    chain.setBandChannels (0, std::uint64_t (1) << lfe);

    std::vector<double*> pointers (6);
    std::vector<std::vector<double>> offsets (6, std::vector<double> (static_cast<std::size_t> (length)));

    for (int n = 0; n < length; ++n)
    {
        for (std::size_t c = 0; c < 6; ++c)
            pointers[c] = audio[c].data() + n;

        chain.process (pointers.data(), 6, 1);

        for (int c = 0; c < 6; ++c)
            offsets[static_cast<std::size_t> (c)][static_cast<std::size_t> (n)] = chain.offsetDb (0, c);
    }

    for (int c = 0; c < 6; ++c)
    {
        const auto duringLfe = offsets[static_cast<std::size_t> (c)][7000];
        const auto duringLeft = offsets[static_cast<std::size_t> (c)][19000];
        INFO ("channel " << c << ": " << duringLfe << " dB in the LFE's burst, " << duringLeft << " dB in the left's");

        // The LFE's burst, linked or not, moves no one: the band does not read the LFE.
        CHECK (duringLfe == 0.0);

        if (c == lfe)
            CHECK (duringLeft == 0.0);      // and the LFE has no offset of this band at all
        else
            CHECK (duringLeft < -3.0);      // the left's burst moves the five the band applies to
    }

    // The LFE went through untouched.
    CHECK (sameBits (audio[lfe], input[lfe]));
    CHECK_FALSE (sameBits (audio[0], input[0]));

    // The band's meter is the lanes it has: the five channels' one lane.
    CHECK (chain.bandMeter (0).offsetDb == doctest::Approx (chain.offsetDb (0, 0)));
}

TEST_CASE ("a band's channels: a Below band's meter does not read a lane of the channels left out")
{
    // A Below band acts on a quiet key: an empty lane would sit at the floor and read the full Range.
    Chain chain;
    chain.prepare (rate, 2);
    chain.setBand (0, dynamicBand (BandType::bell, 1000.0, 1.0, 0.0, -40.0, -12.0, 4.0, 1.0, 20.0, Direction::below));
    chain.setBandChannels (0, 2u);      // the right channel left out
    chain.setChain ({ false, 0.0, false });     // unlinked: a lane per channel the band has

    auto left = tone<double> (9600, 1000.0, 0.5, rate);
    std::vector<double> right (9600, 0.0);
    double* pointers[] = { left.data(), right.data() };
    chain.process (pointers, 2, 9600);

    // The left is loud, over the threshold: no offset. The silent right is not the band's.
    CHECK (std::abs (chain.offsetDb (0, 0)) < 0.01);
    CHECK (chain.offsetDb (0, 1) == 0.0);
    CHECK (std::abs (chain.bandMeter (0).offsetDb) < 0.01);
}

TEST_CASE_TEMPLATE ("a band's channels: block-size invariance with the sets moving, bit for bit", Sample, float, double)
{
    constexpr int length = 12000;

    std::vector<std::vector<Sample>> input;
    for (int c = 0; c < 4; ++c)
        input.push_back (noise<Sample> (length, static_cast<std::uint32_t> (340 + c), 0.2));

    const std::vector<Event> events {
        { 0, [] (Chain& c)
             {
                 c.setBand (0, band (true, BandType::highPass, 150.0, 0.7, 0.0));
                 c.setBand (1, dynamicBand (BandType::bell, 900.0, 1.0, 3.0, -40.0, -9.0, 3.0, 1.0, 40.0));
                 c.setBand (2, trackBand (BandType::bell, 1000.0, -6.0, 300.0, 3000.0));
                 c.setBand (3, envBand (BandType::lowPass, 0.0, 400.0, 4000.0, -50.0, 30.0, 800.0));
                 c.setBandChannels (0, 8u);
             } },
        { 1500, [] (Chain& c) { c.setBandChannels (1, 1u); c.setBandChannels (2, 6u); } },
        { 3100, [] (Chain& c) { c.setBandChannels (0, 0u); c.setBandChannels (3, 9u); c.setLinkExclusions (2u); } },
        { 5003, [] (Chain& c) { c.setBandChannels (1, 15u); c.setChain ({ false, 0.0, false }); } },
        { 7777, [] (Chain& c) { c.setBandChannels (1, 4u); c.setBandChannels (2, 0u); c.setChain ({ false, 0.0, true }); } },
        { 9000, [] (Chain& c) { c.setBandChannels (3, 0u); c.setBandChannels (0, 5u); } },
    };

    const auto reference = render (rate, input, events, fixedBlocks (64));

    for (const auto size : { 1, 7, 480, 4096 })
    {
        const auto other = render (rate, input, events, fixedBlocks (size));

        for (std::size_t c = 0; c < input.size(); ++c)
        {
            INFO ("blocks of " << size << ", channel " << c);
            CHECK (sameBits (other[c], reference[c]));
        }
    }

    const auto random = render (rate, input, events, randomBlocks (77));

    for (std::size_t c = 0; c < input.size(); ++c)
        CHECK (sameBits (random[c], reference[c]));
}

TEST_CASE ("a band's channels: taking a channel out and back is click-free")
{
    // A 500 Hz sine through a +12 dB bell at 500 Hz. The band lets go of the
    // channel through its fade to the identity, and takes it back the same way.
    const auto length = static_cast<int> (0.5 * rate);
    const auto input = std::vector<std::vector<double>> { tone<double> (length, 500.0, 0.2, rate), tone<double> (length, 500.0, 0.2, rate) };

    const auto out = render (rate, input, {
        { 0, [] (Chain& c) { c.setBand (0, band (true, BandType::bell, 500.0, 1.0, 12.0)); } },
        { 7200, [] (Chain& c) { c.setBandChannels (0, 1u); } },
        { 16800, [] (Chain& c) { c.setBandChannels (0, 0u); } } }, fixedBlocks (64));

    // The largest step from one sample to the next is the boosted sine's own.
    const auto own = 0.2 * 3.98 * 2.0 * pi * 500.0 / rate;
    auto worst = 0.0;

    for (std::size_t n = 2400; n + 1 < out[0].size(); ++n)
        worst = std::max (worst, std::abs (out[0][n + 1] - out[0][n]));

    INFO ("largest step " << worst << ", the boosted sine's " << own);
    CHECK (worst < 1.05 * own);

    // Out, the channel is the input itself; back in, it is boosted again; the other never left.
    CHECK (out[0][12000] == input[0][12000]);
    CHECK (out[0][12011] == input[0][12011]);

    auto peakBack = 0.0, peakOther = 0.0;

    for (std::size_t n = 21600; n < out[0].size(); ++n)
    {
        peakBack = std::max (peakBack, std::abs (out[0][n]));
        peakOther = std::max (peakOther, std::abs (out[1][n]));
    }

    CHECK (peakBack == doctest::Approx (0.2 * 3.98).epsilon (0.01));
    CHECK (peakOther == doctest::Approx (peakBack).epsilon (1.0e-6));
}

TEST_CASE ("a band's channels: Track bands on different channels each follow their own, and do not exclude each other")
{
    const auto length = static_cast<int> (0.6 * rate);

    const auto locked = [length] (double leftHz, double rightHz)
    {
        std::vector<std::vector<double>> audio { tone<double> (length, leftHz, 0.3, rate), tone<double> (length, rightHz, 0.3, rate) };

        Chain chain;
        chain.prepare (rate, 2);
        chain.setBand (0, trackBand (BandType::bell, 260.0, -6.0, 150.0, 500.0));
        chain.setBandChannels (0, 2u);      // the left alone
        chain.setBand (1, trackBand (BandType::bell, 260.0, -6.0, 150.0, 500.0));
        chain.setBandChannels (1, 1u);      // the right alone

        double* pointers[] = { audio[0].data(), audio[1].data() };
        chain.process (pointers, 2, length);

        return std::pair { chain.frequencyHz (0, 0), chain.frequencyHz (1, 1) };
    };

    // A tone each: each band on its own channel's.
    {
        const auto [left, right] = locked (220.0, 330.0);
        INFO ("left band at " << left << " Hz, right band at " << right << " Hz");
        CHECK (std::abs (cents (left, 220.0)) < 20.0);
        CHECK (std::abs (cents (right, 330.0)) < 20.0);
    }

    // The same tone on both: both bands on it. Bands of one key exclude each
    // other's peaks (PRD, goal 4); these two do not read the same key.
    {
        const auto [left, right] = locked (220.0, 220.0);
        INFO ("left band at " << left << " Hz, right band at " << right << " Hz");
        CHECK (std::abs (cents (left, 220.0)) < 20.0);
        CHECK (std::abs (cents (right, 220.0)) < 20.0);
    }
}
