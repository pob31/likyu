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

# The bench on a farm machine (devplan, Phase 7), from the author's machine: a commit is
# taken there as a git bundle, lib/dsp's suite and the bench are built in Release without
# JUCE, the suite runs, and the bench's figures and the two bits ledgers come back.
#
#   Usage: scripts/farm-bench.sh <host> [<commit>] [<variant> [<cmake arguments>...]]
#
#     <host>      macmini or nuc, as ~/.ssh/config names them
#     <commit>    what to measure; HEAD by default. Committed work only: a bundle carries
#                 no working tree.
#     <variant>   a name for a second build tree with the cmake arguments that follow,
#                 e.g.  scalar -DLIKYU_SIMD=scalar ; "default" by default
#
#   What comes back, in build/farm/<host>/<short sha>-<variant>/:
#     suite.json         likyu_bench suite, for scripts/bench-compare.py
#     ledger-bench.txt   likyu_bench hash
#     ledger-suite.txt   likyu_dsp_tests' own ledger (LIKYU_LEDGER)
#     log.txt            everything the machine said
#
# The clone stays on the machine (~/likyu-bench), so the next commit builds only what
# changed. farm-check.sh is the other script: the CI job's checks, plugins and validators.
set -euo pipefail

HOST=${1:?usage: scripts/farm-bench.sh <host> [<commit>] [<variant> [<cmake arguments>...]]}
REF=${2:-HEAD}
VARIANT=${3:-default}
shift $(( $# < 3 ? $# : 3 ))
EXTRA="$*"

cd "$(git rev-parse --show-toplevel)"
SHA=$(git rev-parse "$REF")
SHORT=$(git rev-parse --short "$REF")
OUT="build/farm/$HOST/$SHORT-$VARIANT"
mkdir -p "$OUT"

# A bundle needs a ref, and HEAD or a bare sha is not one: a branch of our own for it.
git branch -f farm-bench "$SHA" > /dev/null
git bundle create "$OUT/likyu.bundle" farm-bench > /dev/null 2>&1
git branch -D farm-bench > /dev/null
scp -q "$OUT/likyu.bundle" "$HOST:/tmp/likyu-bench.bundle"
rm -f "$OUT/likyu.bundle"

ssh "$HOST" bash -s -- "$SHA" "$VARIANT" "$HOST $SHORT $VARIANT" "$EXTRA" > "$OUT/log.txt" 2>&1 <<'REMOTE' || { tail -40 "$OUT/log.txt"; exit 1; }
set -euo pipefail
export PATH=/opt/homebrew/bin:$PATH
SHA=$1; VARIANT=$2; LABEL=$3; EXTRA=$4

if [ ! -d "$HOME/likyu-bench/.git" ]; then
  git clone -q /tmp/likyu-bench.bundle "$HOME/likyu-bench"
fi

cd "$HOME/likyu-bench"
git fetch -q /tmp/likyu-bench.bundle farm-bench
git checkout -q -f "$SHA"
git submodule update -q --init ThirdParty/doctest

BUILD="build/bench-$VARIANT"
# shellcheck disable=SC2086
cmake --preset dev -B "$BUILD" -DLIKYU_BUILD_PLUGINS=OFF $EXTRA
cmake --build "$BUILD" --config Release --target likyu_dsp_tests likyu_bench

RESULTS="$HOME/likyu-bench/$BUILD/results"
rm -rf "$RESULTS" && mkdir -p "$RESULTS"

echo "=== suite (Release)"
LIKYU_LEDGER="$RESULTS/ledger-suite.txt" "$BUILD/tests/Release/likyu_dsp_tests" | tail -4

echo "=== ledger"
"$BUILD/tests/Release/likyu_bench" hash --out "$RESULTS/ledger-bench.txt"

echo "=== bench"
"$BUILD/tests/Release/likyu_bench" suite --label "$LABEL" --json "$RESULTS/suite.json"
REMOTE

scp -q "$HOST:likyu-bench/build/bench-$VARIANT/results/*" "$OUT/"
sed -n '/^=== suite/,$p' "$OUT/log.txt"
echo
echo "in $OUT: $(ls "$OUT" | tr '\n' ' ')"
