#!/usr/bin/env python3
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

"""lib/dsp includes the C++ standard library and nothing else.

Produces: nothing. Exits 0 when every #include under lib/dsp/ is a standard
         header, one of the few intrinsics headers named below, or a file of
         lib/dsp itself; 1 otherwise.
Usage:   python3 scripts/check-dsp-includes.py
Needs:   python3 only. It runs in the `pins` CI job, which has no compiler.

Why (PRD, Architecture; goal 7): Go.dot or an app links lib/dsp directly,
without a second copy of JUCE in the process, and the DSP suite builds and
runs in seconds. One convenient JUCE include would end both, and nothing in
the build would say so: the plugin targets have JUCE on their include path
anyway. So this says so.

The rule, per #include line:
  <name>        `name` is lower-case letters, digits and underscores only,
                which is every C++ standard header (<vector>, <cstdint>), or
                it is in INTRINSICS.
  <dsp/...>     a file that exists under lib/dsp/.
  "..."         a file that exists beside the including file or under lib/dsp/.
"""

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
DSP_ROOT = REPO_ROOT / "lib" / "dsp"

# Not standard, and needed: flush-to-zero (Phase 2) and the SIMD lanes (Phase 7).
INTRINSICS = {"immintrin.h", "xmmintrin.h", "pmmintrin.h", "emmintrin.h", "arm_neon.h"}

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]')
STANDARD_RE = re.compile(r"^[a-z0-9_]+$")


def main() -> int:
    # A Windows console is cp1252 and the PRD's file name has a macron in it.
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    print(f"check-dsp-includes: {DSP_ROOT}")
    failures: list[str] = []
    checked = 0

    for path in sorted(DSP_ROOT.rglob("*")):
        if path.suffix not in {".h", ".hpp", ".cpp", ".c"}:
            continue
        rel = path.relative_to(REPO_ROOT).as_posix()
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
            match = INCLUDE_RE.match(line)
            if not match:
                continue
            checked += 1
            bracket, name = match.groups()
            if bracket == "<":
                ok = bool(STANDARD_RE.match(name)) or name in INTRINSICS \
                    or (name.startswith("dsp/") and (DSP_ROOT / name).is_file())
            else:
                ok = (path.parent / name).is_file() or (DSP_ROOT / name).is_file()
            if not ok:
                failures.append(f"{rel}:{number}: {line.strip()}")

    if failures:
        print("\ncheck-dsp-includes: FAILED\n")
        print("  lib/dsp may include the C++ standard library and its own files only:\n")
        for i, f in enumerate(failures, start=1):
            print(f"  {i}. {f}")
        return 1

    print(f"  ok  {checked} includes")
    print("\ncheck-dsp-includes: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
