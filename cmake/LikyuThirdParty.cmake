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

# LikyuThirdParty.cmake — the one file that knows JUCE exists.
#
# Origin: OSCQuery Automation Plugin cmake/OqaThirdParty.cmake at 1d8caa8 (itself after
# go.dot's cmake/WfgThirdParty.cmake), cut down: no juce_simpleweb, no CLAP, no shared
# JUCE library for executables (the one test executable that needs JUCE compiles its own).
#
# Targets defined here:
#
#   likyu::juce_config  INTERFACE  the JUCE configuration switches, cxx_std_20 and the MSVC
#                                  flags. Linked by EVERYTHING that compiles JUCE code,
#                                  the plugins included, so the switches are stated once.
#   likyu::deps         INTERFACE  likyu::juce_config plus JUCE's headers and the
#                                  definitions JUCE would otherwise attach to its module
#                                  targets. What a translation unit needs to #include a
#                                  JUCE header, and nothing that compiles code. src/core
#                                  links this, so it goes into a plugin and into the tests
#                                  without compiling JUCE itself.
#   likyu::warnings     INTERFACE  the warning policy; our targets only, never vendor
#                                  sources. lib/dsp links it too: it carries flags, no code.
#
# lib/dsp links neither of the first two. It includes the C++ standard library and nothing
# else (PRD, Architecture; scripts/check-dsp-includes.py holds it to that).

# Everything of JUCE is skipped when the plugins are not built: the RTSan job configures
# lib/dsp and its suite alone and should not wait for juceaide.
if(LIKYU_BUILD_PLUGINS)

# ---------------------------------------------------------------------------
# 1. JUCE
# ---------------------------------------------------------------------------
# Not JUCE_MODULES_ONLY: the plugin needs juce_add_plugin, which that option removes. The
# cost is the juceaide helper built once at configure.
add_subdirectory("${CMAKE_SOURCE_DIR}/ThirdParty/JUCE" juce)

# ---------------------------------------------------------------------------
# 2. likyu::juce_config — the switches, stated once
# ---------------------------------------------------------------------------
add_library(likyu_juce_config INTERFACE)
add_library(likyu::juce_config ALIAS likyu_juce_config)

target_compile_features(likyu_juce_config INTERFACE cxx_std_20)

target_compile_definitions(likyu_juce_config INTERFACE
    JUCE_WEB_BROWSER=0              # no webkit on Linux, no WebView2 on Windows
    JUCE_USE_CURL=0                 # no libcurl: this plugin opens no connection
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=0    # a modal loop inside a host is a hang

    # The VST3 class ID is derived differently under each value of this switch, so it is
    # part of the plugin's identity and never changes once a session has saved the plugin
    # (PRD, decided 2026-11-16). 0: there is no VST2 version to replace.
    JUCE_VST3_CAN_REPLACE_VST2=0)

if(MSVC)
    # /bigobj: JUCE attaches it to a few of its own modules only. /utf-8: our sources are
    # UTF-8 and MSVC reads them in the system codepage without it.
    target_compile_options(likyu_juce_config INTERFACE /bigobj /utf-8)
endif()

find_package(Threads REQUIRED)
target_link_libraries(likyu_juce_config INTERFACE Threads::Threads)

# ---------------------------------------------------------------------------
# 3. likyu::deps — JUCE's headers and the definitions its module targets carry
# ---------------------------------------------------------------------------
add_library(likyu_deps INTERFACE)
add_library(likyu::deps ALIAS likyu_deps)

target_link_libraries(likyu_deps INTERFACE likyu::juce_config juce::juce_recommended_config_flags)

# SYSTEM, so vendor warnings never meet -Werror on our code.
target_include_directories(likyu_deps SYSTEM INTERFACE
    "${CMAKE_SOURCE_DIR}/ThirdParty/JUCE/modules")

# JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED is mandatory: without it the first JUCE header any
# of our translation units includes is a hard #error. src/core uses juce_core and nothing
# else, so that is the one module named here.
target_compile_definitions(likyu_deps INTERFACE
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    $<$<CONFIG:Debug>:DEBUG=1>
    $<$<CONFIG:Debug>:_DEBUG=1>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG=1>
    $<$<NOT:$<CONFIG:Debug>>:_NDEBUG=1>
    $<$<PLATFORM_ID:Linux>:LINUX=1>
    JUCE_MODULE_AVAILABLE_juce_core=1
    LIKYU_VERSION_STRING="${PROJECT_VERSION}")

endif() # LIKYU_BUILD_PLUGINS

# ---------------------------------------------------------------------------
# 4. likyu::warnings — our code only
# ---------------------------------------------------------------------------
add_library(likyu_warnings INTERFACE)
add_library(likyu::warnings ALIAS likyu_warnings)

# JUCE's recommended warning set is flags and no code, so lib/dsp may carry it. Without
# JUCE in the build, a plain equivalent.
if(TARGET juce::juce_recommended_warning_flags)
    target_link_libraries(likyu_warnings INTERFACE juce::juce_recommended_warning_flags)
elseif(MSVC)
    target_compile_options(likyu_warnings INTERFACE /W4)
else()
    target_compile_options(likyu_warnings INTERFACE -Wall -Wextra -Wconversion -Wshadow)
endif()

if(MSVC)
    # Stated here as well as in likyu::juce_config, because lib/dsp links this and not that.
    target_compile_options(likyu_warnings INTERFACE /utf-8)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # GCC warns on the #pragma clang / #pragma warning lines JUCE puts in its headers.
    target_compile_options(likyu_warnings INTERFACE -Wno-unknown-pragmas)
endif()

if(LIKYU_WARNINGS_AS_ERRORS)
    target_compile_options(likyu_warnings INTERFACE
        $<$<CXX_COMPILER_ID:MSVC>:/WX>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Werror>)
endif()
