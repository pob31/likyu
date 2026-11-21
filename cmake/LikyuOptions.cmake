# This file is part of Likyu — https://github.com/pob31/likyu
#
# Copyright (C) 2026 Pierre-Olivier Boulant
#
# Likyu is free software: you can redistribute it and/or modify it under the
# terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version. Likyu is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
# (LICENSE, at the repository root) for more details.
#
# SPDX-License-Identifier: GPL-3.0-or-later

# Every option() of the build, in one place.
#
# Origin: OSCQuery Automation Plugin cmake/OqaOptions.cmake at 1d8caa8, renamed, with
# LIKYU_RTSAN and LIKYU_BUILD_PLUGINS added.

option(LIKYU_BUILD_TESTS "Build the test suites" ON)

# Off for the RTSan job, which builds and runs the DSP suite only and has no use for
# compiling JUCE.
option(LIKYU_BUILD_PLUGINS "Build the plugin targets (VST3, AU, LV2) and the suite that needs JUCE" ON)

# Reaches likyu::warnings only, which is linked to our own targets and never to vendor
# sources, so a JUCE header warning under a future compiler cannot redden the build.
option(LIKYU_WARNINGS_AS_ERRORS "Treat warnings in this project's own code as errors" OFF)

# Clang's realtime sanitizer on lib/dsp and its tests (PRD, Realtime rules). Needs a Clang
# that has -fsanitize=realtime (20 or later); the `rtsan` preset and CI job turn it on.
option(LIKYU_RTSAN "Build lib/dsp and its tests with Clang's realtime sanitizer" OFF)

# What lib/dsp's kernels are written in (lib/dsp/dsp/simd/Target.h; devplan, Phase 7).
# `auto` is AVX2 on x86-64, required of the processor since 2026-11-21 (decided by the
# author), and NEON on arm64. `scalar` is the same kernels with no intrinsic, for a
# processor older than AVX2; CI builds it to hold the other two to the same bits.
set(LIKYU_SIMD "auto" CACHE STRING "lib/dsp's kernels: auto (AVX2 on x86-64, NEON on arm64) or scalar")
set_property(CACHE LIKYU_SIMD PROPERTY STRINGS auto scalar)

# The French locale the suite runs a second time under (devplan, cross-cutting: everything
# that writes numbers runs under fr_FR as well as C). The Windows UCRT spells it "fr-FR"
# and glibc and macOS "fr_FR.UTF-8"; this is the one place that difference is allowed to
# exist.
if(WIN32)
    set(_likyu_locale_fr_default "fr-FR")
else()
    set(_likyu_locale_fr_default "fr_FR.UTF-8")
endif()
set(LIKYU_LOCALE_FR "${_likyu_locale_fr_default}" CACHE STRING
    "Name of the French locale as this platform's setlocale() spells it")
unset(_likyu_locale_fr_default)
