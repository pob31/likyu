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
    THE CHANNELS LEFT OUT OF THE LINK (PRD, Detection and Parameters and
    state). Every channel starts in the link; the user leaves a channel out
    by hand, an LFE or any other (decided 2026-11-16, amended 2026-11-18).

    THE FORM (decided by the author 2026-11-20): a per-instance list in the
    state chunk, by channel index, together with the layout it was made for;
    not a host parameter, so the parameter count stays at 284. The editor
    sets it (Phase 6); until then state and tests do.

    A LAYOUT CHANGE (decided by the author the same day): the list applies
    only while the current layout is the one it was made for; in any other
    layout every channel is in the link. A layout a host sets for a moment
    erases nothing: REAPER sets 64 channels for an instant while loading,
    and Bitwig loads the state before it sets the layout (Phase 1 bench). A
    list is made again for a new layout when the user edits it there.

    A layout is named by the shim, which has the host's channel set: its
    speaker arrangement as JUCE writes it ("L R C Lfe Ls Rs"; a discrete
    layout "1 2 3 4"), with the channel count beside it.

    Message thread, or wherever the host saves and loads; the audio thread
    sees only the mask, through an atomic in the shim.
*/

#include <juce_core/juce_core.h>

#include <cstdint>

namespace likyu::core
{
    struct LinkList
    {
        juce::String layout;            // the speaker arrangement it was made for
        int channels = 0;
        std::uint64_t excluded = 0;     // bit c: channel c left out

        bool operator== (const LinkList&) const = default;
    };

    /** The mask the chain takes: the list's, when the current layout is the
        one it was made for, limited to its channels; otherwise 0, every
        channel in the link. */
    std::uint64_t effectiveMask (const LinkList& list, const juce::String& currentLayout, int currentChannels) noexcept;

    /** The list as the state chunk keeps it:
        { "layout": "L R C Lfe Ls Rs", "channels": 6, "excluded": [ 3 ] }. */
    juce::var toVar (const LinkList& list);

    /** False, and `list` untouched, when `v` is not a list of that form.
        Indices outside 0 to 63 are dropped. */
    bool fromVar (const juce::var& v, LinkList& list);
}
