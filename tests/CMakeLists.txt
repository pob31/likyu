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

# Two executables.
#
#   likyu_dsp_tests   lib/dsp alone: the C++ standard library and doctest, no JUCE. Builds
#                     and runs in seconds, and is all the RTSan job builds.
#   likyu_tests       the core and the plugin's processor, with JUCE compiled into the
#                     executable. Run twice by CTest, under C and under French
#                     (LIKYU_LOCALE_FR), because the state chunk is number text.
#
# Both replace operator new (support/RtAllocCounter.cpp), so an allocation inside a
# realtime region is counted (dsp/rt/RtCheck.h).

# ---------------------------------------------------------------------------
# lib/dsp
# ---------------------------------------------------------------------------
add_executable(likyu_dsp_tests
    dsp/DspTestMain.cpp
    dsp/ParameterTableTests.cpp
    dsp/LookaheadTests.cpp
    dsp/ResponseTests.cpp
    dsp/SvfTests.cpp
    dsp/TanTests.cpp
    support/RtAllocCounter.cpp)

# doctest from its own submodule, SYSTEM so its warnings stay off our -Werror.
target_include_directories(likyu_dsp_tests SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/ThirdParty/doctest")
target_link_libraries(likyu_dsp_tests PRIVATE likyu::dsp likyu::warnings)

add_test(NAME dsp COMMAND likyu_dsp_tests)

# The RTSan job's canary: a violation on purpose, which the sanitizer must die on.
if(LIKYU_RTSAN)
    add_executable(likyu_rtsan_canary dsp/RtsanCanary.cpp)
    target_link_libraries(likyu_rtsan_canary PRIVATE likyu::dsp)
    add_test(NAME rtsan.canary COMMAND likyu_rtsan_canary)
    set_tests_properties(rtsan.canary PROPERTIES WILL_FAIL TRUE)
endif()

# ---------------------------------------------------------------------------
# The core and the processor
# ---------------------------------------------------------------------------
if(LIKYU_BUILD_PLUGINS)
    add_executable(likyu_tests
        TestMain.cpp
        TestSupport.h
        LocaleTests.cpp
        core/ProvisionalStateTests.cpp
        plugin/ProcessorTests.cpp
        support/RtAllocCounter.cpp

        # The processor itself, without PluginEntry.cpp: no format wrapper here.
        "${PROJECT_SOURCE_DIR}/src/shims/juce/PluginProcessor.h"
        "${PROJECT_SOURCE_DIR}/src/shims/juce/PluginProcessor.cpp")

    target_include_directories(likyu_tests SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/ThirdParty/doctest")
    target_include_directories(likyu_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/shims/juce")

    target_compile_definitions(likyu_tests PRIVATE
        LIKYU_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        LIKYU_LOCALE_FR="${LIKYU_LOCALE_FR}")

    # juce_audio_processors PRIVATE compiles the JUCE modules into this executable, as
    # juce_add_plugin does for a plugin; likyu::core brings headers and switches only.
    target_link_libraries(likyu_tests
        PRIVATE
            likyu::core
            likyu::dsp
            juce::juce_audio_processors
            likyu::juce_config
            likyu::warnings
            juce::juce_recommended_config_flags)

    add_test(NAME unit.C COMMAND likyu_tests --likyu-locale=C)
    add_test(NAME unit.fr_FR COMMAND likyu_tests --likyu-locale=${LIKYU_LOCALE_FR})
endif()
