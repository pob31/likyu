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

# The CI job's checks on a farm machine (macOS or Linux), for when a runner is slow or a
# branch is not pushed yet: configure `dev`, build Debug, ctest under C and fr_FR, build
# the Release plugins, pluginval 1.0.4 at strictness 10 on the VST3, auval on macOS, the
# LV2 bundles listed by lilv on Linux. Logs go next to the checkout.
#
#   Usage: scripts/farm-check.sh <checkout>
#
# Moving an unpushed branch there, as go.dot does:
#   git bundle create likyu.bundle <branch> && scp likyu.bundle macmini:/tmp/
#   ssh macmini 'git clone -b <branch> /tmp/likyu.bundle likyu && cd likyu && git submodule update --init --recursive'
#
# Origin: OSCQuery Automation Plugin scripts/farm-check.sh at 1d8caa8, without the CLAP and
# the validators' settings file, with auval and the LV2 listing.
set -uo pipefail
export PATH=/opt/homebrew/bin:$PATH
cd "$1" || exit 1
OS=$(uname -s)
step() { echo; echo "=== $1"; }

step "configure (dev)"
cmake --preset dev > ../farm-likyu-config.log 2>&1 || { tail -30 ../farm-likyu-config.log; exit 1; }
grep '^CMake Warning' ../farm-likyu-config.log | grep -v ThirdParty/ && echo "!! configure warnings outside ThirdParty"

step "build Debug"
cmake --build --preset dev-debug > ../farm-likyu-build.log 2>&1 || { grep -E "error|Error" ../farm-likyu-build.log | head -30; exit 1; }
echo "warnings in our code:"; grep -E "warning:" ../farm-likyu-build.log | grep -v ThirdParty/ | sort -u | head -20; echo "(end)"

step "ctest (C and fr_FR)"
ctest --preset dev-debug > ../farm-likyu-ctest.log 2>&1
RC=$?
tail -8 ../farm-likyu-ctest.log
[ $RC -eq 0 ] || exit $RC

step "build Release plugins"
cmake --build --preset dev-release --target likyu_plugin_All likyu_plugin_lv2_mono_All > ../farm-likyu-release.log 2>&1 || { grep -E "error|Error" ../farm-likyu-release.log | head -30; exit 1; }
ART=build/dev/src/shims/juce/likyu_plugin_artefacts/Release
MONO=build/dev/src/shims/juce/likyu_plugin_lv2_mono_artefacts/Release
ls "$ART" "$MONO"
VST="$ART/VST3/Likyu.vst3"

step "pluginval 1.0.4, strictness 10"
rm -rf ../pluginval && mkdir ../pluginval
if [ "$OS" = Darwin ]; then
  # The AU registrar and pluginval both want the bundle signed as a whole; JUCE's CMake
  # under Ninja does not do it (OSCQuery plugin, Phase 1).
  codesign --force --deep -s - "$VST" "$ART/AU/Likyu.component"
  curl -sSL -o ../pluginval/pv.zip https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_macOS.zip
  unzip -q ../pluginval/pv.zip -d ../pluginval
  PV=../pluginval/pluginval.app/Contents/MacOS/pluginval
  lipo -archs "$VST/Contents/MacOS/Likyu"
else
  curl -sSL -o ../pluginval/pv.zip https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_Linux.zip
  unzip -q ../pluginval/pv.zip -d ../pluginval
  PV=../pluginval/pluginval; chmod +x $PV
fi
EXTRA=""
if [ "$OS" != Darwin ] && [ -z "${DISPLAY:-}" ]; then
  if command -v xvfb-run >/dev/null; then PVRUN="xvfb-run -a"; else PVRUN=""; EXTRA="--skip-gui-tests"; echo "(no DISPLAY and no xvfb: --skip-gui-tests)"; fi
fi
${PVRUN:-} $PV --strictness-level 10 --validate-in-process --timeout-ms 300000 $EXTRA --validate "$VST" > ../farm-likyu-pluginval.log 2>&1
RC=$?
grep -E "FAILED|SUCCESS|!!!" ../farm-likyu-pluginval.log | tail -10
echo "pluginval exit=$RC"
[ $RC -eq 0 ] || exit $RC

if [ "$OS" = Darwin ]; then
  step "auval (aufx Lkyu PxBl)"
  mkdir -p "$HOME/Library/Audio/Plug-Ins/Components"
  rm -rf "$HOME/Library/Audio/Plug-Ins/Components/Likyu.component"
  cp -R "$ART/AU/Likyu.component" "$HOME/Library/Audio/Plug-Ins/Components/"
  killall -9 AudioComponentRegistrar 2>/dev/null
  auval -v aufx Lkyu PxBl > ../farm-likyu-auval.log 2>&1
  RC=$?
  tail -6 ../farm-likyu-auval.log
  echo "auval exit=$RC"
  exit $RC
fi

step "LV2: both plugins listed by lilv"
if command -v lv2ls >/dev/null; then
  STAGE=$(mktemp -d)
  cp -R "$ART/LV2/Likyu.lv2" "$MONO/LV2/Likyu-Mono.lv2" "$STAGE/"
  LISTED=$(LV2_PATH="$STAGE" lv2ls)
  echo "$LISTED"
  echo "$LISTED" | grep -qx "https://pixetbel.org/lv2/likyu/stereo" || { echo "!! stereo LV2 not listed"; exit 1; }
  echo "$LISTED" | grep -qx "https://pixetbel.org/lv2/likyu/mono" || { echo "!! mono LV2 not listed"; exit 1; }
else
  echo "(lv2ls not installed: apt install lilv-utils)"
fi
exit 0
