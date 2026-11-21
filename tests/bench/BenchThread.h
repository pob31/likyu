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
    Where likyu_bench's measuring thread runs (devplan, Phase 7; asked for by
    the bench notes of Phases 2 and 3, whose worst blocks were the scheduler's).
    The one file of the bench that includes a platform's headers.
*/

#include <string>

namespace likyubench
{
    /** The kind of core the thread is kept on. */
    enum class Cores
    {
        any,            // wherever the scheduler puts it
        performance,    // one of the fastest cores
        efficiency      // one of the next kind down, where the machine has two kinds
    };

    /** Puts the calling thread on `cores` and, with `audioPriority`, at the
        priority a host gives its audio thread: MMCSS "Pro Audio" on Windows,
        the user-interactive QoS on macOS (which has no affinity: the QoS is
        what keeps a thread on the performance cores, and the background QoS
        on the efficiency ones), SCHED_FIFO on Linux where it is allowed.

        Never fails. Returns what it obtained, in words, for the report: a
        refusal is said there, not hidden. */
    std::string claimThread (Cores cores, bool audioPriority);
}
