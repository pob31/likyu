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
    FLUSH-TO-ZERO, SET BY lib/dsp ITSELF (PRD, Filter core: flush-to-zero and
    denormals-are-zero set in the process call; devplan 2c: not by a JUCE
    helper, so that a native client of the library gets it too).

    ScopedFlushToZero sets the modes for its lifetime and restores what was
    there before:

      - x86-64: MXCSR's FTZ and DAZ bits (0x8040), through <xmmintrin.h>;
      - arm64 with GCC or Clang: FPCR's FZ bit (24), which flushes inputs and
        results alike, by inline assembly;
      - anything else: nothing, and isActive() says false.

    Standard library only, plus the one intrinsics header
    scripts/check-dsp-includes.py allows for this.
*/

#include <cstdint>

#if defined (__x86_64__) || defined (_M_X64) || defined (__i386__) || defined (_M_IX86)
 #include <xmmintrin.h>
 #define LIKYU_DSP_FTZ_SSE 1
#elif defined (__aarch64__) && (defined (__GNUC__) || defined (__clang__))
 #define LIKYU_DSP_FTZ_ARM64 1
#endif

namespace likyu::dsp
{
    class ScopedFlushToZero
    {
    public:
        ScopedFlushToZero() noexcept
            : saved (read())
        {
            write (saved | bits);
        }

        ~ScopedFlushToZero() noexcept
        {
            write (saved);
        }

        ScopedFlushToZero (const ScopedFlushToZero&) = delete;
        ScopedFlushToZero& operator= (const ScopedFlushToZero&) = delete;

        /** True when the modes are set on this thread now. */
        static bool isActive() noexcept
        {
           #if defined (LIKYU_DSP_FTZ_SSE) || defined (LIKYU_DSP_FTZ_ARM64)
            return (read() & bits) == bits;
           #else
            return false;
           #endif
        }

    private:
       #if defined (LIKYU_DSP_FTZ_SSE)
        using Register = unsigned int;
        static constexpr Register bits = 0x8040u;

        static Register read() noexcept             { return _mm_getcsr(); }
        static void write (Register value) noexcept { _mm_setcsr (value); }
       #elif defined (LIKYU_DSP_FTZ_ARM64)
        using Register = std::uint64_t;
        static constexpr Register bits = Register (1) << 24;

        static Register read() noexcept
        {
            Register value;
            __asm__ __volatile__ ("mrs %0, fpcr" : "=r" (value));
            return value;
        }

        static void write (Register value) noexcept
        {
            __asm__ __volatile__ ("msr fpcr, %0" : : "r" (value));
        }
       #else
        using Register = std::uint32_t;
        static constexpr Register bits = 0;

        static Register read() noexcept             { return 0; }
        static void write (Register) noexcept       {}
       #endif

        Register saved;
    };
}
