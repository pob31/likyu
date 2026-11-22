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

# The macOS pkg or the Linux tarball, from a clean checkout to installer/Output/ (devplan,
# Phase 7): configure the platform's release preset, build the plugins and lib/dsp's suite
# in Release, run the suite, then installer/build-pkg.sh or installer/build-tarball-linux.sh.
# The same script runs in .github/workflows/release.yml; Windows has build-installer.ps1.
#
#   scripts/build-installer.sh                 everything
#   scripts/build-installer.sh --skip-build    repackage the tree already built
#   scripts/build-installer.sh --skip-tests    no suite (not for a release)
#
# On macOS the build is universal and the suite runs twice: natively (the arm64 slice,
# NEON) and under Rosetta (the x86_64 slice, which must report the scalar kernels, not
# fused). Without Rosetta the second run is skipped with a warning, unless
# LIKYU_X86_64_SUITE=required, as the release workflow sets it.
# Signing and notarisation: build-pkg.sh's variables, passed through.
#
# Origin: WFS-DIY Plugin/Scripts/build-all.sh, on Likyu's presets, with the suite.

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"
export PATH="/opt/homebrew/bin:$PATH"

SKIP_BUILD=0 SKIP_TESTS=0
for arg in "$@"; do
    case "$arg" in
        --skip-build) SKIP_BUILD=1 ;;
        --skip-tests) SKIP_TESTS=1 ;;
        *) echo "usage: $0 [--skip-build] [--skip-tests]" >&2; exit 1 ;;
    esac
done

case "$(uname -s)" in
    Darwin) PRESET=release-macos; PACKAGE=installer/build-pkg.sh ;;
    Linux)  PRESET=release-linux; PACKAGE=installer/build-tarball-linux.sh ;;
    *) echo "error: $(uname -s): use scripts/build-installer.ps1 on Windows" >&2; exit 1 ;;
esac
TESTS="build/$PRESET/tests/Release"

if [ "$SKIP_BUILD" = 0 ]; then
    cmake --preset "$PRESET"
    cmake --build --preset "$PRESET-release" --parallel \
          --target likyu_plugin_All likyu_plugin_lv2_mono_All likyu_dsp_tests likyu_bench
fi

if [ "$SKIP_TESTS" = 0 ]; then
    ctest --preset "$PRESET-release"

    if [ "$PRESET" = release-macos ]; then
        if arch -x86_64 /usr/bin/true 2>/dev/null; then
            echo "==> The x86_64 slice, under Rosetta"
            report="$(arch -x86_64 "$TESTS/likyu_bench" time --seconds 0.2 --warmup 10)"
            echo "$report" | sed -n 2p
            echo "$report" | grep -q "scalar kernels, not fused" \
                || { echo "error: the x86_64 slice is not the scalar build" >&2; exit 1; }
            arch -x86_64 "$TESTS/likyu_dsp_tests"
        elif [ "${LIKYU_X86_64_SUITE:-}" = required ]; then
            echo "error: no Rosetta here, and LIKYU_X86_64_SUITE=required" >&2
            exit 1
        else
            echo "warning: no Rosetta here: the x86_64 slice's suite did not run" >&2
        fi
    fi
fi

bash "$PACKAGE"
