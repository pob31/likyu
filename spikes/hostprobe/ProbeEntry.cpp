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
    The function JUCE's format wrappers call, for the probe targets.
    LIKYU_DEFAULT_CHANNELS is 2, or 1 for the mono LV2 variant, as for the
    product (src/shims/juce/PluginEntry.cpp).
*/

#include "ProbeProcessor.h"

#ifndef LIKYU_DEFAULT_CHANNELS
 #error "LIKYU_DEFAULT_CHANNELS must be defined by the probe target"
#endif

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new likyu::probe::ProbeProcessor (LIKYU_DEFAULT_CHANNELS);
}
