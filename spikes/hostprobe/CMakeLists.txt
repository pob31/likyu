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

# The Phase 1 host probe. Throwaway: a project of its own, which the root CMakeLists.txt
# never builds (spikes/README.md). It adds the repository as a subdirectory and makes two
# plugins out of the real processor plus an observer and an editor.
#
#   cmake -S spikes/hostprobe -B build/hostprobe
#   cmake --build build/hostprobe --config Release
#
# IDENTITY. Not the product's, so that both sit side by side in a host and no session ever
# holds the product's IDs through the probe: "Likyu Probe", plugin code Lkyp under PxBl,
# its own bundle ID and LV2 URIs. Same buses, same 284 parameters, same latency.

cmake_minimum_required(VERSION 3.22)

if(CMAKE_HOST_APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

project(likyu_hostprobe VERSION 0.0.1 LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

# As the root sets them, and for the same reasons. Stated again because a directory's
# variables reach its subdirectories and not its parent, and the probe targets live here.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

get_filename_component(LIKYU_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# The product's own targets come along (the install scripts build them from here too); its
# suites do not.
set(LIKYU_BUILD_TESTS OFF CACHE BOOL "The probe does not build the suites" FORCE)
add_subdirectory("${LIKYU_ROOT}" likyu)

set(LIKYU_PROBE_FORMATS VST3 LV2)
if(APPLE)
    list(APPEND LIKYU_PROBE_FORMATS AU)
endif()

# `formats` is the NAME of a list variable. `default_channels` is 2 or 1.
function(likyu_add_probe target product plugin_name bundle_id lv2_uri default_channels formats)
    juce_add_plugin(${target}
        COMPANY_NAME                "Pix et Bel"
        PLUGIN_MANUFACTURER_CODE    "PxBl"
        PLUGIN_CODE                 "${LIKYU_PROBE_CODE}"
        BUNDLE_ID                   "${bundle_id}"
        PRODUCT_NAME                "${product}"
        PLUGIN_NAME                 "${plugin_name}"
        DESCRIPTION                 "Likyu host probe (throwaway)"
        VERSION                     "${PROJECT_VERSION}"
        FORMATS                     ${${formats}}
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            FALSE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD     FALSE
        VST3_CATEGORIES             "Fx" "EQ" "Dynamics"
        AU_MAIN_TYPE                "kAudioUnitType_Effect"
        LV2URI                      "${lv2_uri}")

    target_sources(${target} PRIVATE
        ProbeProcessor.h
        ProbeProcessor.cpp
        ProbeEditor.h
        ProbeEditor.cpp
        ProbeEntry.cpp

        # The product's processor, compiled in as the tests do, and the core's objects
        # inside the shared-code archive for the reason src/shims/juce/CMakeLists.txt gives.
        "${LIKYU_ROOT}/src/shims/juce/PluginProcessor.h"
        "${LIKYU_ROOT}/src/shims/juce/PluginProcessor.cpp"
        $<TARGET_OBJECTS:likyu_core>)

    target_include_directories(${target} PRIVATE "${LIKYU_ROOT}/src/shims/juce")

    target_compile_definitions(${target} PRIVATE
        LIKYU_DEFAULT_CHANNELS=${default_channels})

    target_link_libraries(${target}
        PRIVATE
            likyu::core
            likyu::dsp
            juce::juce_audio_processors
            likyu::juce_config
            likyu::warnings
            juce::juce_recommended_config_flags)
endfunction()

set(LIKYU_PROBE_CODE "Lkyp")
likyu_add_probe(likyu_hostprobe
    "LikyuProbe" "Likyu Probe"
    "io.github.pob31.likyu.probe"
    "https://pixetbel.org/lv2/likyu-probe/stereo"
    2 LIKYU_PROBE_FORMATS)

# The probe without a host: writes its report and a picture of its editor (Snapshot.cpp).
add_executable(likyu_hostprobe_snapshot
    Snapshot.cpp
    ProbeProcessor.h
    ProbeProcessor.cpp
    ProbeEditor.h
    ProbeEditor.cpp
    "${LIKYU_ROOT}/src/shims/juce/PluginProcessor.h"
    "${LIKYU_ROOT}/src/shims/juce/PluginProcessor.cpp")

target_include_directories(likyu_hostprobe_snapshot PRIVATE "${LIKYU_ROOT}/src/shims/juce")

target_link_libraries(likyu_hostprobe_snapshot
    PRIVATE
        likyu::core
        likyu::dsp
        juce::juce_audio_processors
        likyu::juce_config
        likyu::warnings
        juce::juce_recommended_config_flags)

# The probe's editor in a window of its own, no host: the control for the touch row (TouchApp.cpp).
juce_add_gui_app(likyu_hostprobe_touch
    PRODUCT_NAME "LikyuProbeNoHost"
    COMPANY_NAME "Pix et Bel"
    BUNDLE_ID    "io.github.pob31.likyu.probe.nohost"
    VERSION      "${PROJECT_VERSION}")

target_sources(likyu_hostprobe_touch PRIVATE
    TouchApp.cpp
    ProbeProcessor.h
    ProbeProcessor.cpp
    ProbeEditor.h
    ProbeEditor.cpp
    "${LIKYU_ROOT}/src/shims/juce/PluginProcessor.h"
    "${LIKYU_ROOT}/src/shims/juce/PluginProcessor.cpp"
    $<TARGET_OBJECTS:likyu_core>)

target_include_directories(likyu_hostprobe_touch PRIVATE "${LIKYU_ROOT}/src/shims/juce")

target_link_libraries(likyu_hostprobe_touch
    PRIVATE
        likyu::core
        likyu::dsp
        juce::juce_audio_processors
        likyu::juce_config
        likyu::warnings
        juce::juce_recommended_config_flags)

# The same probe refusing every main layout but stereo: the control for one question. REAPER
# gives a four-channel track's channels 3/4 to the main bus of a plugin that accepts any
# width (docs/spikes/phase1-hosts.md); does it give them to the sidechain of one that does not?
set(LIKYU_PROBE_FORMATS_FIXED VST3)
set(LIKYU_PROBE_CODE "Lkyq")
likyu_add_probe(likyu_hostprobe_fixed
    "LikyuProbe-fixed" "Likyu Probe fixed stereo"
    "io.github.pob31.likyu.probe.fixed"
    "https://pixetbel.org/lv2/likyu-probe/fixed"
    2 LIKYU_PROBE_FORMATS_FIXED)
target_compile_definitions(likyu_hostprobe_fixed PRIVATE LIKYU_PROBE_FIXED_MAIN=1)
set(LIKYU_PROBE_CODE "Lkyp")

set(LIKYU_PROBE_FORMATS_MONO LV2)
likyu_add_probe(likyu_hostprobe_lv2_mono
    "LikyuProbe-mono" "Likyu Probe mono"
    "io.github.pob31.likyu.probe.mono"
    "https://pixetbel.org/lv2/likyu-probe/mono"
    1 LIKYU_PROBE_FORMATS_MONO)
