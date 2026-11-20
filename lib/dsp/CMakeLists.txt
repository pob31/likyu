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
    dsp/Gain.h
    dsp/Lookahead.h
    dsp/Math.h
    dsp/Parameters.h
    dsp/Parameters.cpp
    dsp/Smoother.h
    dsp/Svf.h
    dsp/Tan.h
    dsp/rt/RtCheck.h)
add_library(likyu::dsp ALIAS likyu_dsp)

set_target_properties(likyu_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_compile_features(likyu_dsp PUBLIC cxx_std_20)
target_include_directories(likyu_dsp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(likyu_dsp PRIVATE likyu::warnings)

# PUBLIC: the process paths are templates in headers, so the sanitizer has to reach every
# translation unit that instantiates them, and the executable has to link its runtime.
if(LIKYU_RTSAN)
    target_compile_options(likyu_dsp PUBLIC -fsanitize=realtime)
    target_link_options(likyu_dsp PUBLIC -fsanitize=realtime)
    target_compile_definitions(likyu_dsp PUBLIC LIKYU_RTSAN=1)
endif()
