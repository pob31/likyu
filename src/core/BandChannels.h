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

#pragma once

/*
    THE CHANNELS EACH BAND LEAVES OUT (PRD, Band model, Channels; decided by
    the author 2026-11-20). Every band applies to every channel until the
    user takes a channel out of it: a low cut that the LFE is spared, a
    low-pass for the LFE alone.

    THE FORM is the link list's (core/LinkList.h), for the same reasons: a
    list in the state chunk, by channel index, with the layout it was made
    for; not host parameters, so the count stays at 284 and no ID is added.
    The editor sets it.

    A LAYOUT CHANGE, as for the link list: the lists apply only while the
    current layout is the one they were made for; in any other layout every
    band applies to every channel. A layout a host sets for a moment erases
    nothing. The lists are made again for a new layout when the user edits
    one there.

    Message thread, or wherever the host saves and loads; the audio thread
    sees only the masks, through atomics in the shim.
*/

#include <dsp/Parameters.h>

#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>

namespace likyu::core
{
    struct BandChannels
    {
        juce::String layout;            // the speaker arrangement they were made for
        int channels = 0;
        std::array<std::uint64_t, dsp::numBands> leftOut {};    // per band; bit c: channel c left out

        /** Some band leaves some channel out. */
        bool any() const noexcept
        {
            for (const auto mask : leftOut)
                if (mask != 0)
                    return true;

            return false;
        }

        bool operator== (const BandChannels&) const = default;
    };

    /** The mask the chain takes for `band`: the list's, when the current
        layout is the one it was made for, limited to its channels; otherwise
        0, every channel. */
    std::uint64_t effectiveMask (const BandChannels& lists, int band, const juce::String& currentLayout,
                                 int currentChannels) noexcept;

    /** The lists with `band` leaving out `leftOut` in the current layout.
        Lists made for another layout do not apply here, and are replaced:
        the other bands start with every channel. */
    BandChannels withBand (const BandChannels& lists, int band, std::uint64_t leftOut,
                           const juce::String& currentLayout, int currentChannels);

    /** The lists as the state chunk keeps them, a list per band in order:
        { "layout": "L R C Lfe Ls Rs", "channels": 6, "bands": [ [ 3 ], [], [], [], [], [], [], [ 0, 1, 2, 4, 5 ] ] }. */
    juce::var toVar (const BandChannels& lists);

    /** False, and `lists` untouched, when `v` is not of that form. Indices
        outside 0 to 63 are dropped; bands the chunk does not list leave
        nothing out, and lists beyond the build's bands are ignored. */
    bool fromVar (const juce::var& v, BandChannels& lists);
}
