/*
    This file is part of Likyu — https://github.com/pob31/likyu

    Copyright (C) 2026 Pierre-Olivier Boulant

    Likyu is free software: you can redistribute it and/or modify it under the
    terms of the GNU General Public License as published by the Free Software
    Foundation, either version 3 of the License, or (at your option) any later
    version. Likyu is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
    or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
    (LICENSE, at the repository root) for more details.

    SPDX-License-Identifier: GPL-3.0-or-later
*/

/*
    The one function JUCE's format wrappers call. Kept apart from the
    processor so that the tests can build the processor without it.

    LIKYU_DEFAULT_CHANNELS is set per plugin target (CMakeLists.txt beside
    this file): 2, or 1 for the mono LV2 variant.
*/

#include "PluginProcessor.h"

#ifndef LIKYU_DEFAULT_CHANNELS
 #error "LIKYU_DEFAULT_CHANNELS must be defined by the plugin target"
#endif

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new likyu::LikyuProcessor (LIKYU_DEFAULT_CHANNELS);
}
