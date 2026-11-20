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

# likyu::editor (PRD, Architecture): the editor, a client of likyu::core. An INTERFACE
# target, as the OSCQuery plugin's: its sources compile inside whichever target links it,
# with that target's own JUCE GUI modules, so the core never sees a GUI module.
# juce_gui_basics is all it needs: EditorRoot is a plain Component that the JUCE shim wraps
# in an AudioProcessorEditor and a CLAP shim will parent to its host's window.
#
# Presentation.h, CurveGeometry.h, QGestures.h and ResponseCurve.h include no JUCE at all:
# what the editor says is decided there and tested headless (tests/editor/).

add_library(likyu_editor INTERFACE)
add_library(likyu::editor ALIAS likyu_editor)

target_sources(likyu_editor INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/CurveGeometry.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/CurveView.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/CurveView.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/EditorHost.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/EditorRoot.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/EditorRoot.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Palette.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Panels.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Panels.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ParamControls.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/ParamControls.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Presentation.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/QGestures.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/ResponseCurve.h")

target_include_directories(likyu_editor INTERFACE "${PROJECT_SOURCE_DIR}/src")
