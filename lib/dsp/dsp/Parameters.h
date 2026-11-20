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
#include <vector>

namespace likyu::dsp
{
    inline constexpr int numBands = 8;

    /** The range of every frequency parameter (PRD: 20 Hz to 20 kHz). */
    inline constexpr double minFrequency = 20.0;
    inline constexpr double maxFrequency = 20000.0;

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
