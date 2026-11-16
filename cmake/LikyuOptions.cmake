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
