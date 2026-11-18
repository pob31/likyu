#!/usr/bin/env bash
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

# Builds the host probe and the product in Release and copies the bundles to the user's
# plugin folders (devplan, Phase 1). macOS and Linux; install.ps1 is the Windows one.
#
#   spikes/hostprobe/install.sh               build, then copy
#   spikes/hostprobe/install.sh --skip-build  copy what is already built
#   spikes/hostprobe/install.sh --uninstall   remove what this script copied
set -euo pipefail
export PATH=/opt/homebrew/bin:$PATH

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD="$ROOT/build/hostprobe"
PROBE="$BUILD/likyu_hostprobe_artefacts/Release"
PROBE_MONO="$BUILD/likyu_hostprobe_lv2_mono_artefacts/Release"
PROBE_FIXED="$BUILD/likyu_hostprobe_fixed_artefacts/Release"
PRODUCT="$BUILD/likyu/src/shims/juce/likyu_plugin_artefacts/Release"
PRODUCT_MONO="$BUILD/likyu/src/shims/juce/likyu_plugin_lv2_mono_artefacts/Release"

if [ "$(uname -s)" = Darwin ]; then
  VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
  LV2_DIR="$HOME/Library/Audio/Plug-Ins/LV2"
  AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
else
  VST3_DIR="$HOME/.vst3"
  LV2_DIR="$HOME/.lv2"
  AU_DIR=""
fi

# source, destination folder
BUNDLES=(
  "$PROBE/VST3/LikyuProbe.vst3|$VST3_DIR"
  "$PROBE_FIXED/VST3/LikyuProbe-fixed.vst3|$VST3_DIR"
  "$PRODUCT/VST3/Likyu.vst3|$VST3_DIR"
  "$PROBE/LV2/LikyuProbe.lv2|$LV2_DIR"
  "$PROBE_MONO/LV2/LikyuProbe-mono.lv2|$LV2_DIR"
  "$PRODUCT/LV2/Likyu.lv2|$LV2_DIR"
  "$PRODUCT_MONO/LV2/Likyu-mono.lv2|$LV2_DIR"
)
if [ -n "$AU_DIR" ]; then
  BUNDLES+=("$PROBE/AU/LikyuProbe.component|$AU_DIR" "$PRODUCT/AU/Likyu.component|$AU_DIR")
fi

if [ "${1:-}" = --uninstall ]; then
  for entry in "${BUNDLES[@]}"; do
    target="${entry#*|}/$(basename "${entry%%|*}")"
    if [ -e "$target" ]; then rm -rf "$target"; echo "removed $target"; fi
  done
  exit 0
fi

if [ "${1:-}" != --skip-build ]; then
  cmake -S "$ROOT/spikes/hostprobe" -B "$BUILD" -G "Ninja Multi-Config"
  cmake --build "$BUILD" --config Release \
    --target likyu_hostprobe_All likyu_hostprobe_fixed_All likyu_hostprobe_lv2_mono_All \
             likyu_plugin_All likyu_plugin_lv2_mono_All
fi

for entry in "${BUNDLES[@]}"; do
  source="${entry%%|*}"
  folder="${entry#*|}"
  [ -e "$source" ] || { echo "not built: $source"; exit 1; }
  mkdir -p "$folder"
  target="$folder/$(basename "$source")"
  rm -rf "$target"
  cp -R "$source" "$target"
  # The AU registrar and several hosts want the bundle signed as a whole; JUCE's CMake
  # under Ninja does not do it (scripts/farm-check.sh).
  if [ "$(uname -s)" = Darwin ]; then codesign --force --deep -s - "$target" 2>/dev/null || true; fi
  echo "installed $target"
done

if [ "$(uname -s)" = Darwin ]; then killall -9 AudioComponentRegistrar 2>/dev/null || true; fi
