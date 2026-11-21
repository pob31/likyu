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

/*
    The entry point of likyu_dsp_tests: doctest's own main, and the suite's
    bits ledger. This executable links lib/dsp and the C++ standard library
    only.

    THE LEDGER (devplan, Phase 7). Every rendering a test makes through
    `render` (DspTestSupport.h) is hashed, bit for bit, into the line of the
    test case that made it. With LIKYU_LEDGER naming a file, the lines are
    written there when the run ends, a hash and a name each, in the order the
    cases ran. An optimisation that changes no bit leaves the file as it was;
    two builds on one machine (scalar and SIMD) write the same file. Like
    likyu_bench's, a ledger is compared with one made on the same machine and
    is not committed: lib/dsp calls the C library at control rate, and so do
    the tests' own signals.
*/

// std::getenv, which the UCRT calls unsafe: read once, at the end of the run.
#define _CRT_SECURE_NO_WARNINGS

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "DspTestSupport.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr std::uint64_t fnvOffset = 1469598103934665603ull;

    std::uint64_t ledgerHash = fnvOffset;
    bool ledgerUsed = false;
    std::vector<std::pair<std::string, std::uint64_t>> ledgerLines;

    struct LedgerListener : doctest::IReporter
    {
        explicit LedgerListener (const doctest::ContextOptions&) {}

        void test_case_start (const doctest::TestCaseData& in) override
        {
            name = in.m_name;
            ledgerHash = fnvOffset;
            ledgerUsed = false;
        }

        void test_case_end (const doctest::CurrentTestCaseStats&) override
        {
            if (ledgerUsed)
                ledgerLines.push_back ({ name, ledgerHash });
        }

        void test_run_end (const doctest::TestRunStats&) override
        {
            const auto* path = std::getenv ("LIKYU_LEDGER");

            if (path == nullptr || *path == 0)
                return;

            if (auto* file = std::fopen (path, "wb"))
            {
                for (const auto& line : ledgerLines)
                    std::fprintf (file, "%016llx %s\n", static_cast<unsigned long long> (line.second), line.first.c_str());

                std::fclose (file);
                std::printf ("ledger: %d lines written to %s\n", static_cast<int> (ledgerLines.size()), path);
            }
            else
            {
                std::printf ("ledger: cannot write %s\n", path);
            }
        }

        void report_query (const doctest::QueryData&) override {}
        void test_run_start() override {}
        void test_case_reenter (const doctest::TestCaseData&) override {}
        void test_case_exception (const doctest::TestCaseException&) override {}
        void subcase_start (const doctest::SubcaseSignature&) override {}
        void subcase_end() override {}
        void log_assert (const doctest::AssertData&) override {}
        void log_message (const doctest::MessageData&) override {}
        void test_case_skipped (const doctest::TestCaseData&) override {}

        std::string name;
    };
}

REGISTER_LISTENER ("ledger", 1, LedgerListener);

namespace likyutest::dsp
{
    void ledgerAdd (const void* data, std::size_t size) noexcept
    {
        const auto* bytes = static_cast<const unsigned char*> (data);

        // FNV-1a, 64 bits.
        for (std::size_t i = 0; i < size; ++i)
        {
            ledgerHash ^= bytes[i];
            ledgerHash *= 1099511628211ull;
        }

        ledgerUsed = true;
    }
}
