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
    THE PARAMETER TABLE (PRD, Parameters and state): every parameter of the
    plugin, declared once, in real units. The shims build the host's
    parameters from it; nothing else in the project spells an ID.

    284 entries: 4 per instance, then 35 per band for bands 1 to 8, in the
    PRD's order. IDs are `b<n>_<suffix>`, underscore and never dot, never
    renamed and never positional once frozen (devplan: frozen by the author
    at the end of Phase 5; until then a build is not for saving sessions).

    VALUES. A toggle is 0 or 1. A choice is the index of its entry, 0 to
    count - 1. A continuous parameter is in the unit named, between minimum
    and maximum.

    Standard library only. The table is built on first use, which is never
    on the audio thread: the shims read it when they construct.
*/

#include <string>
#include <string_view>
#include <vector>

namespace likyu::dsp
{
    inline constexpr int numBands = 8;

    /** The range of every frequency parameter (PRD: 20 Hz to 20 kHz). */
    inline constexpr double minFrequency = 20.0;
    inline constexpr double maxFrequency = 20000.0;

    /** The range of every Q parameter (PRD: 0.1 to 40). */
    inline constexpr double minQ = 0.1;
    inline constexpr double maxQ = 40.0;

    /** The range of the output gain (PRD: -24 to +24 dB). */
    inline constexpr double maxOutputGainDb = 24.0;

    /** The ranges of the Dynamic mode's parameters (PRD, Parameters and state),
        which the gain computer clamps to as the table does (dsp/Dynamics.h). */
    inline constexpr double maxRangeDb = 30.0;
    inline constexpr double minThresholdDb = -80.0;
    inline constexpr double maxThresholdDb = 0.0;
    inline constexpr double minRatio = 1.0;
    inline constexpr double maxRatio = 20.0;
    inline constexpr double maxKneeDb = 24.0;
    inline constexpr double minAttackMs = 0.05;
    inline constexpr double maxAttackMs = 200.0;
    inline constexpr double minReleaseMs = 5.0;
    inline constexpr double maxReleaseMs = 2000.0;

    enum class ParamKind { toggle, choice, continuous };

    /** How a continuous parameter is laid along a host's 0 to 1. */
    enum class ParamSkew { linear, logarithmic };

    struct ParamSpec
    {
        std::string id;
        std::string name;
        std::string unit;       // "Hz", "dB", "ms", "%", "x" or empty
        ParamKind kind;
        double minimum;
        double maximum;
        double defaultValue;
        ParamSkew skew;
        bool automatable;
        std::vector<std::string> choices;   // for ParamKind::choice only
    };

    /** The per-instance parameters, in table order. */
    enum class GlobalParam
    {
        bypass, outGain, lookahead, link,
        count
    };

    /** The per-band parameters, in table order: the PRD's suffixes. */
    enum class BandParam
    {
        on, type, freq, q, gain, qmode, gmode, fmode,
        key, dfilt, dtype, dfreq, dq,
        range, thresh, ratio, knee, atk, rel, ddir,
        tatk, tsus, ttime, tfloor,
        flo, fhi, fspeed, fdir, fsens, fspan, fatk, frel,
        fconf, fhold, fcgain,
        count
    };

    inline constexpr int numGlobalParams = static_cast<int> (GlobalParam::count);
    inline constexpr int numBandParams = static_cast<int> (BandParam::count);
    inline constexpr int numParams = numGlobalParams + numBands * numBandParams;

    static_assert (numBandParams == 35, "the PRD counts 35 parameters per band");
    static_assert (numParams == 284, "the PRD counts 284 parameters in all");

    /** The table, numParams entries, built once. */
    const std::vector<ParamSpec>& parameterTable();

    /** The table index of an ID; -1 when the table has no such ID. Not for
        the audio thread (its lookup is built on first use). */
    int indexOfId (std::string_view id);

    //==========================================================================
    /*  A HOST'S 0 TO 1, AND BACK. Linear, or logarithmic (equal steps of the
        host's range are equal ratios of the value); a choice's index spread
        evenly; a toggle at 0 and 1. The shims build their ranges from these,
        and the tests check against them, so the two cannot drift apart. */

    /** Into [0, 1], the value clamped to its range first. */
    double toNormalised (const ParamSpec& spec, double value) noexcept;

    /** From [0, 1], clamped. Not rounded: a choice's caller rounds. */
    double fromNormalised (const ParamSpec& spec, double normalised) noexcept;

    /** The same, between bounds given apart from the spec. A shim whose host
        range is in float passes the float bounds, so that its ends map to
        exactly 0 and 1 (0.1 as a float is a little above 0.1: auval,
        2026-11-20). */
    double toNormalised (ParamSkew skew, double minimum, double maximum, double value) noexcept;
    double fromNormalised (ParamSkew skew, double minimum, double maximum, double normalised) noexcept;

    /** A value within a millionth of a round number becomes that number
        (six significant digits). A host's float mapping returns 5 ms as
        5.0000024; this gives it back as 5, so every default maps to itself
        (auval warned on 18 of them, Phase 1). Anything farther from a round
        number is left exactly as it is. */
    double snapToRound (double value) noexcept;

    constexpr int indexOf (GlobalParam p) noexcept
    {
        return static_cast<int> (p);
    }

    /** `band` counts from 0 here; the ID and the name count from 1. */
    constexpr int indexOf (int band, BandParam p) noexcept
    {
        return numGlobalParams + band * numBandParams + static_cast<int> (p);
    }

    //==========================================================================
    /*  LOOKAHEAD (PRD, Latency): Off, 0.25 ms, 1 ms, 3 ms, converted to whole
        samples at the current rate. The choice's index is the parameter's
        value. */
    inline constexpr double lookaheadMilliseconds[] = { 0.0, 0.25, 1.0, 3.0 };
    inline constexpr int numLookaheadChoices = 4;

    /** Whole samples of delay for a lookahead choice; 0 for an index out of range. */
    int lookaheadSamples (int choice, double sampleRate) noexcept;

    /** The longest delay any choice asks for at this rate: what prepare allocates. */
    int maxLookaheadSamples (double sampleRate) noexcept;
}
