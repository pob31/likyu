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

# likyu::core (PRD, Architecture): dispatch of host parameter values to the DSP, the state
# chunk, the meter snapshot. One STATIC target that the shims and the editor link; juce_core
# and nothing else of JUCE, so a headless test drives all of it. Includes as <core/...>.
#
# Phase 2: the dispatch of parameter values to the chain, their text, the state chunk.
# Phase 3: the list of channels left out of the link (retired 2026-11-20 for the channels of each band).

add_library(likyu_core STATIC
    BandChannels.h
    BandChannels.cpp
    Dispatch.h
    Dispatch.cpp
    MeterSnapshot.h
    State.h
    State.cpp
    ValueText.h
    ValueText.cpp)
add_library(likyu::core ALIAS likyu_core)

set_target_properties(likyu_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(likyu_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(likyu_core PUBLIC likyu::dsp likyu::deps PRIVATE likyu::warnings)
