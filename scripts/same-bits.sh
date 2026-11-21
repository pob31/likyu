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

# Two builds of lib/dsp on one machine give the same bits (devplan, Phase 7): the SIMD
# build and the scalar one, or a commit and the one before. Each build's suite runs and
# writes its ledger (LIKYU_LEDGER), each build's bench writes its own (likyu_bench hash),
# and the two pairs are compared.
#
#   Usage: scripts/same-bits.sh <directory of one build's test executables> <the other's>
#
#     scripts/same-bits.sh build/dev/tests/Release build/scalar/tests/Release
#
# Exit codes: 0 the same bits, 1 a suite failed or a ledger differs, 2 usage.
set -uo pipefail

[ $# -eq 2 ] || { echo "usage: scripts/same-bits.sh <tests directory> <tests directory>"; exit 2; }

WORK=$(mktemp -d)
# A Windows executable wants a Windows path in its environment.
command -v cygpath > /dev/null && WORK=$(cygpath -m "$WORK")

n=0
for dir in "$1" "$2"; do
  n=$((n + 1))
  [ -x "$dir/likyu_dsp_tests" ] || [ -x "$dir/likyu_dsp_tests.exe" ] || { echo "no likyu_dsp_tests in $dir"; exit 2; }

  if ! LIKYU_LEDGER="$WORK/suite-$n.txt" "$dir/likyu_dsp_tests" > "$WORK/suite-$n.log" 2>&1; then
    tail -30 "$WORK/suite-$n.log"
    echo "!! the suite of $dir failed"
    exit 1
  fi

  "$dir/likyu_bench" hash --out "$WORK/bench-$n.txt" > "$WORK/bench-$n.log" 2>&1 || { cat "$WORK/bench-$n.log"; exit 1; }
  echo "$dir: $(sed -n '1p' "$WORK/bench-$n.log" | sed 's/^likyu_bench hash: //'); $(grep -c . "$WORK/suite-$n.txt") and $(grep -c . "$WORK/bench-$n.txt") lines"
done

status=0

for ledger in suite bench; do
  if ! cmp -s "$WORK/$ledger-1.txt" "$WORK/$ledger-2.txt"; then
    echo "!! the $ledger ledgers differ:"
    diff "$WORK/$ledger-1.txt" "$WORK/$ledger-2.txt" | head -40
    status=1
  fi
done

[ $status -eq 0 ] && echo "the same bits"
exit $status
