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
    DISPATCH (PRD, Architecture: likyu::core dispatches the host's parameter
    values to the DSP; Realtime rules: parameters reach the audio thread as
    atomics read once per block).

    The shim reads the 284 values once per block, in the table's real units
    (JUCE keeps each in an atomic), into a Values array, and calls apply()
    on the audio thread before the chain's process. Dispatch compares them
    with the last block's and hands the chain only what moved: a band's
    settings when any of its 35 values did, the instance's when bypass, the
    output gain or the link did. The comparison is of bits, so a value that a
    host sets again unchanged costs nothing.

    A band reads all 35 of its values: type, frequency, Q, gain and Q mode;
    its gain mode, key and detector; the Dynamic mode's values (Phase 3),
    Transient's (Phase 4), and its frequency mode with Env's and Track's
    (Phase 5). Choices are rounded to their index and clamped; a toggle is
    on from 0.5.

    No allocation; nothing of JUCE.
*/

#include <dsp/Chain.h>
#include <dsp/Parameters.h>

#include <array>

namespace likyu::core
{
    class Dispatch
    {
    public:
        using Values = std::array<float, dsp::numParams>;

        /** Audio thread. The first call after construction or forget() hands
            everything over. */
        void apply (const Values& values, dsp::Chain& chain) noexcept;

        /** The next apply() hands everything over again. */
        void forget() noexcept   { first = true; }

    private:
        Values last {};
        bool first = true;
    };

    /** A band's settings from the values, `band` from 0. */
    dsp::BandSettings bandSettingsFrom (const Dispatch::Values& values, int band) noexcept;

    /** The instance's settings from the values. */
    dsp::ChainSettings chainSettingsFrom (const Dispatch::Values& values) noexcept;
}
