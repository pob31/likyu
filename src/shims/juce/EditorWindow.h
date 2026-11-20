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
    THE JUCE SHIM'S EDITOR WINDOW: the AudioProcessorEditor JUCE's wrappers
    want, holding the editor's root (editor/EditorRoot.h), which knows no
    shim. Resizing and its limits are this window's; the root only lays out.
    The shape is the OSCQuery plugin's EditorWindow.

    MULTITOUCH (Phase 1, finding 5): JUCE 9 leaves it off on Windows unless
    the editor asks. With it on, every finger reaches the editor and the
    system's pinch gesture no longer does; the curve computes its own.
*/

#include "PluginProcessor.h"

#include <editor/EditorRoot.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace likyu
{
    class EditorWindow final : public juce::AudioProcessorEditor
    {
    public:
        explicit EditorWindow (LikyuProcessor&);

        bool usesWindowsMultiTouch() const override     { return true; }

        void resized() override;

        /** The root, for a test or a snapshot. */
        editor::EditorRoot& editorRoot() noexcept       { return *root; }

    private:
        std::unique_ptr<editor::EditorRoot> root;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorWindow)
    };
}
