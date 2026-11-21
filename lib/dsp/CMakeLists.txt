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

# likyu::dsp — the DSP library (PRD, Architecture). The C++ standard library only: no JUCE
# header, no allocation after prepare. Includes as <dsp/...>.
#
# Phase 0 brought the parameter table and the lookahead delay; Phase 2 the filters, the
# bands and the chain. Detectors arrive in Phase 3.

add_library(likyu_dsp STATIC
    dsp/Band.h
    dsp/BandCoefficients.h
    dsp/Chain.h
    dsp/Chain.cpp
    dsp/Denormals.h
    dsp/Detector.h
    dsp/Dynamics.h
    dsp/Env.h
    dsp/Exp2.h
    dsp/FastLog.h
    dsp/Frequency.h
    dsp/Gain.h
    dsp/Lookahead.h
    dsp/Math.h
    dsp/Parameters.h
    dsp/Parameters.cpp
    dsp/Response.h
    dsp/SeedBank.h
    dsp/Smoother.h
    dsp/Svf.h
    dsp/Tan.h
    dsp/Tracker.h
    dsp/Transient.h
    dsp/rt/RtCheck.h
    dsp/simd/Target.h)
add_library(likyu::dsp ALIAS likyu_dsp)

set_target_properties(likyu_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_compile_features(likyu_dsp PUBLIC cxx_std_20)
target_include_directories(likyu_dsp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(likyu_dsp PRIVATE likyu::warnings)

# No fused multiply-add, anywhere (devplan, Phase 7). A SIMD kernel and the scalar code
# beside it give the same bits only if a multiply and an add are two roundings in both,
# and a compiler left to contract them fuses one and not the other. Apple clang
# contracts by default, GCC with any option that gives it the instruction; MSVC has not
# since Visual Studio 2022, and is told all the same. PUBLIC, like everything below: most
# of lib/dsp is headers, compiled in the plugin's, the editor's and the tests'
# translation units, and one inline function must not be compiled two ways.
# tests/dsp/ArithmeticTests.cpp is the canary.
if(MSVC)
    target_compile_options(likyu_dsp PUBLIC /fp:precise)
else()
    target_compile_options(likyu_dsp PUBLIC -ffp-contract=off)
endif()

# The kernels' instructions (dsp/simd/Target.h). PUBLIC for the same reason: with AVX2
# the whole plugin is an AVX2 binary, which is the decision (2026-11-21). arm64 needs no
# option. On macOS the option is given to an x86_64 slice only, so that a universal
# build stays possible; which build that slice gets is the installers' question.
#
# `auto` and `reference` compute in the fused arithmetic and need the instruction: AVX2
# and FMA together on x86 (MSVC has one option for both). `scalar` gets neither.
if(LIKYU_SIMD STREQUAL "scalar")
    target_compile_definitions(likyu_dsp PUBLIC LIKYU_SIMD_SCALAR=1)
elseif(LIKYU_SIMD STREQUAL "auto" OR LIKYU_SIMD STREQUAL "reference")
    if(LIKYU_SIMD STREQUAL "reference")
        target_compile_definitions(likyu_dsp PUBLIC LIKYU_SIMD_SCALAR=1 LIKYU_SIMD_REFERENCE=1)
    endif()

    if(APPLE)
        target_compile_options(likyu_dsp PUBLIC "SHELL:-Xarch_x86_64 -mavx2" "SHELL:-Xarch_x86_64 -mfma")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
        if(MSVC)
            target_compile_options(likyu_dsp PUBLIC /arch:AVX2)
        else()
            target_compile_options(likyu_dsp PUBLIC -mavx2 -mfma)
        endif()
    endif()
else()
    message(FATAL_ERROR "LIKYU_SIMD is \"${LIKYU_SIMD}\": auto, reference or scalar")
endif()

# PUBLIC: the process paths are templates in headers, so the sanitizer has to reach every
# translation unit that instantiates them, and the executable has to link its runtime.
if(LIKYU_RTSAN)
    target_compile_options(likyu_dsp PUBLIC -fsanitize=realtime)
    target_link_options(likyu_dsp PUBLIC -fsanitize=realtime)
    target_compile_definitions(likyu_dsp PUBLIC LIKYU_RTSAN=1)
endif()
