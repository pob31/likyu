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

"""Two or more `likyu_bench suite --json` files as the Markdown table of a bench note.

    scripts/bench-compare.py before.json after.json [more.json ...]
                             [--names Before After ...] [--figure trimmed|median]
                             [--no-ratio]

One row a case, one column a file: microseconds per block and the share of one core,
then the last file against the first (not with --no-ratio: three machines side by side
are not a before and an after). The figure is the trimmed mean unless asked
otherwise (tests/bench/BenchMain.cpp says why). A case missing from a file is a dash.

Exit codes: 0 done, 2 usage or an unreadable file.
"""

import argparse
import json
import sys


def load(path):
    try:
        with open(path, encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, ValueError) as error:
        sys.exit(f"error: {path}: {error}")

    if "cases" not in data:
        sys.exit(f"error: {path} is not a suite file (no \"cases\")")

    return data


def cell(case, figure):
    if case is None:
        return "–"

    us = case[figure + "Us"]
    share = 100.0 * us / case["periodUs"]
    digits = 3 if share < 1.0 else 2 if share < 10.0 else 1
    return f"{us:.1f} µs ({share:.{digits}f} %)"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("files", nargs="+")
    parser.add_argument("--names", nargs="+", default=None)
    parser.add_argument("--figure", choices=["trimmed", "median"], default="trimmed")
    parser.add_argument("--no-ratio", action="store_true")
    arguments = parser.parse_args()

    if len(arguments.files) < 2:
        parser.error("two files at least")

    suites = [load(path) for path in arguments.files]
    names = arguments.names or [suite.get("label") or path for suite, path in zip(suites, arguments.files)]

    if len(names) != len(suites):
        parser.error("as many --names as files")

    order = []

    for suite in suites:
        for case in suite["cases"]:
            if case["name"] not in order:
                order.append(case["name"])

    by_name = [{case["name"]: case for case in suite["cases"]} for suite in suites]

    for suite, name in zip(suites, names):
        print(f"- **{name}**: {suite.get('compiler', '?')}; thread: {suite.get('thread', '?')}; "
              f"{suite.get('runs', '?')} runs of {suite.get('seconds', '?')} s")

    print()
    ratio_head = "" if arguments.no_ratio else f" {names[-1]} / {names[0]} |"
    print("| Case | " + " | ".join(names) + " |" + ratio_head)
    print("| --- | " + " | ".join("---" for _ in names) + " |" + ("" if arguments.no_ratio else " --- |"))

    for name in order:
        cases = [table.get(name) for table in by_name]
        first, last = cases[0], cases[-1]
        ratio = "–"

        if first is not None and last is not None and first[arguments.figure + "Us"] > 0.0:
            ratio = f"{last[arguments.figure + 'Us'] / first[arguments.figure + 'Us']:.2f}"

        print(f"| `{name}` | " + " | ".join(cell(case, arguments.figure) for case in cases) + " |"
              + ("" if arguments.no_ratio else f" {ratio} |"))

    return 0


if __name__ == "__main__":
    sys.exit(main())
