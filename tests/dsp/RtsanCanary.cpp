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
    THE CANARY OF THE RTSAN JOB. Built only with LIKYU_RTSAN, and registered
    with CTest as a test that must FAIL: it allocates inside a function marked
    LIKYU_AUDIO_THREAD, which Clang's realtime sanitizer must catch and die
    on. If this program ever exits 0 under the job, the sanitizer is not
    looking, and the suite's silence beside it means nothing.
*/

#include <dsp/rt/RtCheck.h>

#include <cstdlib>

namespace
{
    void* volatile sink = nullptr;

    void process() noexcept LIKYU_AUDIO_THREAD
    {
        sink = std::malloc (64);    // the violation this program is about
        std::free (sink);
    }
}

int main()
{
    process();
    return 0;
}
