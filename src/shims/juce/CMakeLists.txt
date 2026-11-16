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

# The JUCE shim: one AudioProcessor, built as VST3 and LV2 everywhere and AU on macOS.
#
# PLUGIN IDENTITY (PRD, Formats, decided 2026-11-16). The VST3 class ID and the AU type,
# subtype and manufacturer are derived from the manufacturer code, the plugin code and
# JUCE_VST3_CAN_REPLACE_VST2 (cmake/LikyuThirdParty.cmake); an LV2 host knows the plugin by
# its URI. Once a session has saved the plugin, none of these may change, or the session no
# longer finds it.
#
#   Company "Pix et Bel", manufacturer code PxBl, plugin code Lkyu.
#   LV2 URIs https://pixetbel.org/lv2/likyu/stereo and .../mono.
#   Bundle ID io.github.pob31.likyu.
#   "Likyu", in ASCII, for everything a host or a file system reads.
#
# TWO TARGETS, because JUCE's LV2 wrapper writes the audio ports once, from the processor's
# default layout (devplan, facts checked 2026-11-16): VST3 and AU negotiate the layout with
# the host, LV2 cannot. So the main target also carries the stereo LV2, and a second target,
# LV2 only, is the same code with a mono default.
#
# Origin: OSCQuery Automation Plugin src/shims/juce/CMakeLists.txt at 1d8caa8, one product
# in place of the slot-count variants.

set(LIKYU_PLUGIN_COMPANY       "Pix et Bel")
set(LIKYU_PLUGIN_MANUFACTURER  "PxBl")
set(LIKYU_PLUGIN_CODE          "Lkyu")
set(LIKYU_PLUGIN_BUNDLE_ID     "io.github.pob31.likyu")
set(LIKYU_PLUGIN_LV2_URI       "https://pixetbel.org/lv2/likyu")

set(LIKYU_PLUGIN_FORMATS VST3 LV2)
if(APPLE)
    list(APPEND LIKYU_PLUGIN_FORMATS AU)
endif()

# `formats` is the NAME of a list variable. `default_channels` is 2 or 1.
function(likyu_add_plugin target product plugin_name bundle_id lv2_uri default_channels formats)
    juce_add_plugin(${target}
        COMPANY_NAME                "${LIKYU_PLUGIN_COMPANY}"
        PLUGIN_MANUFACTURER_CODE    "${LIKYU_PLUGIN_MANUFACTURER}"
        PLUGIN_CODE                 "${LIKYU_PLUGIN_CODE}"
        BUNDLE_ID                   "${bundle_id}"
        PRODUCT_NAME                "${product}"
        PLUGIN_NAME                 "${plugin_name}"
        DESCRIPTION                 "Dynamic EQ and transient designer"
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
        PluginProcessor.h
        PluginProcessor.cpp
        PluginEntry.cpp

        # The core's object files go INTO the plugin's shared-code archive rather than
        # beside it: GNU ld scans an archive once, in order, and the format targets put the
        # shared-code archive (where the plugin's JUCE modules are) before any other, so a
        # separate liblikyu_core.a could never find juce::JSON. Members of one archive
        # resolve each other whatever the order. lib/dsp needs nothing of JUCE and stays an
        # archive of its own.
        $<TARGET_OBJECTS:likyu_core>)

    target_compile_definitions(${target} PRIVATE
        LIKYU_DEFAULT_CHANNELS=${default_channels})

    # The plugin compiles its own JUCE through juce_add_plugin; likyu::core carries headers
    # and switches only.
    target_link_libraries(${target}
        PRIVATE
            likyu::core
            likyu::dsp
            juce::juce_audio_processors
            likyu::juce_config
            likyu::warnings
            juce::juce_recommended_config_flags)
endfunction()

# The product: VST3, AU, and the stereo LV2.
likyu_add_plugin(likyu_plugin
    "Likyu" "Likyu"
    "${LIKYU_PLUGIN_BUNDLE_ID}"
    "${LIKYU_PLUGIN_LV2_URI}/stereo"
    2 LIKYU_PLUGIN_FORMATS)

# The mono LV2: "Likyu mono" to a host (decided by the author 2026-11-16), Likyu-mono.lv2 on disk.
set(LIKYU_PLUGIN_FORMATS_MONO LV2)
likyu_add_plugin(likyu_plugin_lv2_mono
    "Likyu-mono" "Likyu mono"
    "${LIKYU_PLUGIN_BUNDLE_ID}.mono"
    "${LIKYU_PLUGIN_LV2_URI}/mono"
    1 LIKYU_PLUGIN_FORMATS_MONO)
