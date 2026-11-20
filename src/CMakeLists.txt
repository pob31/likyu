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

# The core first, then the editor, a client of it, then the shim that links both: JUCE's
# (VST3, AU, LV2). Our own CLAP shim (src/shims/clap/) comes later.
#
# Both need JUCE, so neither is configured for the RTSan job, which builds lib/dsp alone.
if(LIKYU_BUILD_PLUGINS)
    add_subdirectory(core)
    add_subdirectory(editor)
    add_subdirectory(shims/juce)
endif()
