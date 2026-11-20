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
    WHAT THE EDITOR ASKS OF WHOEVER HOSTS IT (PRD, Editor: a plain
    juce::Component root, so each shim can host it). The JUCE shim fills this
    from its processor; the CLAP shim will fill it from its own. The editor
    knows no shim, and no parameter ID: a parameter is its index in the table
    (dsp/Parameters.h).

    Every function is called on the message thread only.
*/

#include <core/MeterSnapshot.h>

#include <functional>

namespace likyu::editor
{
    struct Host
    {
        /** A parameter's value in the table's real units. */
        std::function<double (int index)> valueOf;

        /** Sets it, telling the host. Between beginGesture and endGesture
            when it is a drag, so that a host records one move, and so that
            Live lists the parameter at all (Phase 1, finding 6). */
        std::function<void (int index, double value)> setValue;
        std::function<void (int index)> beginGesture;
        std::function<void (int index)> endGesture;

        /** The meters of the last block; true when newer than the last read.
            The editor is the snapshot's one reader. */
        std::function<bool (core::Meters&)> readMeters;

        /** The rate the plugin runs at; 0 before the host has prepared it. */
        std::function<double()> sampleRate;
    };
}
