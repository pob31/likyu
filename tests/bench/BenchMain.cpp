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
    likyu_bench: measurements of lib/dsp that are figures, not pass or fail
    (devplan 2d, 2f and Phase 7). lib/dsp and the standard library, and one
    file of platform calls for the thread (BenchThread.cpp), so it builds in
    seconds anywhere, the farm included.

      likyu_bench time [--rate 96000] [--block 64] [--channels 2] [--bands 8]
                       [--scenario static|moving|decay|dynamic|dynself|unlinked
                                   |transient|env|track|trackself|seeding]
                       [--lookahead 0|1|2|3] [--buffers float|double]
                       [--seconds 10] [--warmup 200] [--runs 1]
                       [--thread audio|plain] [--core p|e|any] [--json out.json]

        The chain's process call timed per block, back to back after a
        warm-up: microseconds per block (min, median, trimmed mean, p99,
        p999, max, mean) and the median as a share of the block's period,
        which is the share of one core. The shape of WFS-DIY's pipeline-bench,
        without its metronome: there is no pipeline here to pace.

        THE TRIMMED MEAN is the mean of the middle 80 % of the blocks. A
        block of the static EQ takes under 3 us and the Windows clock steps by
        0.1, so a median moves in steps of 3 %; the mean of many such blocks
        does not, and trimming keeps the scheduler's blocks out of it. It is
        the figure to compare two builds with; the median is kept because
        the notes of Phases 2 to 6 are in it.

        THE THREAD (Phase 7): by default at the priority a host gives its
        audio thread and on one performance core (BenchThread.h); --thread
        plain and --core any give the ordinary thread of Phases 2 to 6, and
        --core e the other kind of core where the machine has two. With
        --runs N the whole measurement is made N times, each on a fresh
        chain, and the median of the N is reported after them.

          static   eight bands on, of mixed types, none at 0 dB, nothing moving;
          moving   the same with every band's frequency, Q and gain retargeted
                   every block, so every ramp runs all the time: the worst
                   case of a static EQ;
          decay    static bands, the first half on noise, the second on
                   silence; each half reported, and their medians compared
                   (acceptance row "denormals", its timing half);
          dynamic  eight bells and shelves in Dynamic mode, keyed by a stereo
                   sidechain of noise, linked, every band acting all the time:
                   the PRD's budget case without Track (Phase 3);
          dynself  the same keyed by the input, linked: one detector per band
                   per channel, one gain per band (Phase 7: what a wide layout
                   keyed by itself costs in detection);
          unlinked the same keyed by the input, unlinked: one gain computer
                   and one set of coefficients per channel per band;
          transient eight bells and shelves in Transient mode, keyed by a
                   stereo sidechain of noise bursts (40 ms on, 40 ms 60 dB
                   down), linked, so that every shaper attacks, sustains and
                   gates all the time (Phase 4).
          env      eight bells and shelves in Dynamic and Env, keyed by the
                   stereo sidechain of noise, linked (Phase 5);
          track    eight bells and shelves in Dynamic and Track, keyed by the
                   stereo sidechain, linked: the PRD's budget case (Phase 5).
                   The sidechain is noise with a partial under each band, so
                   the eight trackers lock and deflate each other; the
                   seeding bank runs throughout;
          trackself the same keyed by the input: with --channels 64 the
                   trackers run on the mean of 64 channels;
          seeding  the seeding bank alone (thirty third-octave envelopes) on
                   one key, without the chain: what it costs per key and
                   lane, for Phase 7.

        --lookahead takes the parameter's choice (Off, 0.25, 1, 3 ms). The
        delay line is the chain's since Phase 4 and runs at every setting, Off
        included, so every figure counts it.

        NOT INCLUDED: the shim's reading of the parameters per block. The
        figure is the DSP's.

      likyu_bench suite [--seconds 3] [--runs 3] [--warmup 200]
                        [--thread audio|plain] [--core p|e|any]
                        [--label text] [--json out.json]

        The matrix a bench note is made of (Phase 7), one line a case and all
        of it in one JSON for scripts/bench-compare.py: the scenarios above in
        stereo at 96 kHz with blocks of 64; the budget case at 48 kHz; static,
        dynamic and track with 0, 1, 2 and 4 bands, which is how a figure is
        split into its parts; and 64 channels: static, dynamic, dynself,
        unlinked, track (the devplan's second case) and trackself.

      likyu_bench hash [--out ledger.txt] [--expect ledger.txt]

        THE BITS LEDGER (Phase 7). Every scenario rendered for 1.5 s at
        48 kHz, in stereo with float buffers and on five channels with double
        ones, the blocks of 1 to 256 samples, and on the way a type switch, a
        band turned off, a channel left out, the output gain, the lookahead
        and Listen; then a hash of every output bit and of the meters. An
        optimisation that changes no bit leaves every line as it was, and
        the scalar build's ledger is the SIMD build's.

        The signals and the settings are made with no library call, so the
        ledger does not depend on how a compiler folds or vectorises the
        bench itself. It still depends on the machine and the toolchain,
        because lib/dsp calls the C library at control rate (exp, log2): a
        ledger is compared with one made on the same machine, and is not
        committed. --expect compares and lists the lines that differ.

      likyu_bench precision [--seconds 10]

        The noise floor of the audio path in float and in double (devplan 2d):
        a band at 20 Hz, Q 40, 192 kHz (a bell at +12 and -12 dB, a low-pass)
        on full-scale noise and on a -20 dBFS sine at 1 kHz, each precision
        against the same band in long double. The error is reported relative
        to the output, over the second half of the run. Where long double is
        no wider than double (MSVC), the double row has no reference and says
        so.

    Exit codes: 0 done, 1 the ledger is not the expected one, 2 usage.
*/

#include "BenchThread.h"

#include <dsp/BandCoefficients.h>
#include <dsp/Chain.h>
#include <dsp/Gain.h>
#include <dsp/Svf.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace likyu::dsp;

    //==========================================================================
    struct Lcg
    {
        std::uint32_t state = 1;

        std::uint32_t next() noexcept
        {
            state = state * 1664525u + 1013904223u;
            return state;
        }

        double bipolar() noexcept
        {
            return static_cast<double> (next() >> 8) / 8388608.0 - 1.0;
        }
    };

    struct Distribution
    {
        double min = 0.0, median = 0.0, trimmed = 0.0, p99 = 0.0, p999 = 0.0, max = 0.0, mean = 0.0;
    };

    Distribution distributionOf (std::vector<double> values)
    {
        Distribution d;

        if (values.empty())
            return d;

        std::sort (values.begin(), values.end());
        const auto last = values.size() - 1;
        const auto at = [&values, last] (double q)
        {
            return values[std::min (last, static_cast<std::size_t> (std::llround (q * static_cast<double> (last))))];
        };

        d.min = values.front();
        d.max = values.back();
        d.median = at (0.5);
        d.p99 = at (0.99);
        d.p999 = at (0.999);

        double sum = 0.0;
        for (const auto v : values)
            sum += v;

        d.mean = sum / static_cast<double> (values.size());

        // The middle 80 %: see THE TRIMMED MEAN above.
        const auto cut = values.size() / 10;
        double middle = 0.0;

        for (auto i = cut; i < values.size() - cut; ++i)
            middle += values[i];

        d.trimmed = middle / static_cast<double> (values.size() - 2 * cut);
        return d;
    }

    double medianOf (std::vector<double> values)
    {
        if (values.empty())
            return 0.0;

        std::sort (values.begin(), values.end());
        return values[(values.size() - 1) / 2];
    }

    //==========================================================================
    const char* const scenarioNames[] = { "static", "moving", "decay", "dynamic", "dynself", "unlinked",
                                          "transient", "env", "track", "trackself", "seeding" };

    bool isScenario (const std::string& name)
    {
        for (const auto* s : scenarioNames)
            if (name == s)
                return true;

        return false;
    }

    struct TimeConfig
    {
        double rate = 96000.0;
        int block = 64;
        int channels = 2;
        int bands = numBands;
        std::string scenario = "static";
        int lookahead = 0;
        std::string buffers = "float";
        double seconds = 10.0;
        int warmup = 200;
        int runs = 1;
        std::string json;
    };

    /** 40 Hz times 2^(1.2 b): band b's frequency in every scenario. By
        multiplication, so that the ledger's settings need no library call. */
    double bandHz (int b) noexcept
    {
        auto hz = 40.0;

        for (int i = 0; i < b; ++i)
            hz *= 2.2973967099940698;   // 2^1.2

        return hz;
    }

    /** The static settings: mixed types, none an identity. */
    BandSettings staticBand (int b)
    {
        static constexpr BandType types[] = { BandType::highPass, BandType::lowShelf, BandType::bell, BandType::bell,
                                              BandType::bell, BandType::notch, BandType::highShelf, BandType::lowPass };
        const auto index = static_cast<std::size_t> (b);
        return { true, types[index], b % 2 == 0 ? QMode::proportional : QMode::constant,
                 bandHz (b), 0.7 + 0.5 * b, b % 2 == 0 ? 4.5 : -6.0 };
    }

    /** For `dynamic`, `dynself` and `unlinked`: bells and shelves, every one acting on noise. */
    BandSettings dynamicBand (int b, bool sidechain)
    {
        static constexpr BandType types[] = { BandType::bell, BandType::lowShelf, BandType::highShelf };
        BandSettings s;
        s.on = true;
        s.type = types[static_cast<std::size_t> (b % 3)];
        s.qmode = b % 2 == 0 ? QMode::proportional : QMode::constant;
        s.frequency = bandHz (b);
        s.q = 0.7 + 0.5 * b;
        s.gainDb = b % 2 == 0 ? 3.0 : -2.0;
        s.gmode = GainMode::dynamic;
        s.key = sidechain ? KeySource::sidechain : KeySource::self;
        s.thresholdDb = -70.0;
        s.rangeDb = b % 2 == 0 ? -12.0 : 6.0;
        s.ratio = 4.0;
        s.attackMs = 1.0;
        s.releaseMs = 80.0;
        return s;
    }

    /** For `transient`: bells and shelves, both amounts, keyed by the sidechain. */
    BandSettings transientBand (int b)
    {
        static constexpr BandType types[] = { BandType::bell, BandType::lowShelf, BandType::highShelf };
        BandSettings s;
        s.on = true;
        s.type = types[static_cast<std::size_t> (b % 3)];
        s.qmode = b % 2 == 0 ? QMode::proportional : QMode::constant;
        s.frequency = bandHz (b);
        s.q = 0.7 + 0.5 * b;
        s.gainDb = b % 2 == 0 ? 3.0 : -2.0;
        s.gmode = GainMode::transient;
        s.key = KeySource::sidechain;
        s.rangeDb = b % 2 == 0 ? -12.0 : 6.0;
        s.transientAttackPercent = b % 2 == 0 ? -80.0 : 60.0;
        s.transientSustainPercent = b % 2 == 0 ? 50.0 : -70.0;
        return s;
    }

    /** For `env` and `track`: a Dynamic band whose frequency moves. */
    BandSettings frequencyBand (int b, FrequencyMode fmode, bool sidechain)
    {
        auto s = dynamicBand (b, sidechain);
        s.fmode = fmode;
        s.lowHz = s.frequency / 2.0;
        s.highHz = s.frequency * 2.0;
        s.sensitivityDb = -60.0;
        s.spanDb = 40.0;
        s.envAttackMs = 2.0;
        s.envReleaseMs = 60.0;
        s.speedMs = 50.0;
        return s;
    }

    /** A sine's stand-in made of arithmetic alone, for the ledger: a
        parabola per half period, -1 to 1, from a phase that wraps as an
        unsigned integer does. */
    double parabola (std::uint32_t phase) noexcept
    {
        const auto x = static_cast<double> (static_cast<std::int32_t> (phase)) / 2147483648.0;
        return 4.0 * x * (1.0 - std::abs (x));
    }

    /** For `moving`: a new target every block, so no ramp ever lands. With
        `exact`, for the ledger, the same wandering with no library call. */
    BandSettings movingBand (int b, int block, bool exact)
    {
        auto s = staticBand (b);
        const auto step = (block + 37 * b) % 400;

        if (exact)
        {
            const auto phase = static_cast<std::uint32_t> (step) * 10737418u;   // 2^32 / 400
            const auto sine = parabola (phase), cosine = parabola (phase + 1073741824u);
            s.frequency *= 1.0 + 0.7 * sine;
            s.q *= 1.0 + 0.5 * cosine;
            s.gainDb += 6.0 * sine;
            return s;
        }

        const auto phase = static_cast<double> (step) / 400.0;
        s.frequency *= std::pow (2.0, 2.0 * std::sin (2.0 * pi * phase));
        s.q *= std::pow (2.0, std::cos (2.0 * pi * phase));
        s.gainDb += 6.0 * std::sin (2.0 * pi * phase);
        return s;
    }

    /** What a scenario is, said once. */
    struct Scenario
    {
        explicit Scenario (const std::string& name)
            : moving (name == "moving"), dynamic (name == "dynamic"), dynSelf (name == "dynself"),
              unlinked (name == "unlinked"), transient (name == "transient"), env (name == "env"),
              track (name == "track"), trackSelf (name == "trackself"), seeding (name == "seeding")
        {
        }

        bool moving, dynamic, dynSelf, unlinked, transient, env, track, trackSelf, seeding;

        /** Keyed by the sidechain. */
        bool keyed() const noexcept     { return dynamic || transient || env || track; }

        /** A partial under each band, for the trackers. */
        bool partials() const noexcept  { return track || trackSelf; }

        BandSettings band (int b) const
        {
            if (env || track || trackSelf)
                return frequencyBand (b, env ? FrequencyMode::env : FrequencyMode::track, ! trackSelf);

            if (transient)
                return transientBand (b);

            if (dynamic || dynSelf || unlinked)
                return dynamicBand (b, dynamic);

            return staticBand (b);
        }
    };

    /** A chain set for a scenario, its two seconds of signal read round, and
        the buffers a block is put in. With `exact` the partials are parabolas
        and nothing calls the library: the ledger's signal. */
    template <typename Sample>
    struct Rig
    {
        Rig (const TimeConfig& c, bool exact, int largestBlock)
            : config (c), scenario (c.scenario), ring (static_cast<int> (2.0 * c.rate)),
              burst (static_cast<long long> (0.040 * c.rate))
        {
            chain.prepare (config.rate, config.channels);

            for (int b = 0; b < config.bands; ++b)
                chain.setBand (b, scenario.band (b));

            chain.setChain ({ false, 0.0, ! scenario.unlinked });
            chain.setLookahead (lookaheadSamples (config.lookahead, config.rate));

            source.resize (static_cast<std::size_t> (config.channels));

            for (int c2 = 0; c2 < config.channels; ++c2)
            {
                Lcg random { static_cast<std::uint32_t> (c2 + 1) };
                auto& channel = source[static_cast<std::size_t> (c2)];
                channel.resize (static_cast<std::size_t> (ring));

                for (auto& sample : channel)
                    sample = static_cast<Sample> (0.5 * random.bipolar());

                // A partial a little off each band's frequency, so that each tracker has one to lock on.
                if (scenario.partials())
                {
                    for (int b = 0; b < config.bands; ++b)
                    {
                        const auto hz = 1.1 * bandHz (b);
                        const auto step = static_cast<std::uint32_t> (hz / config.rate * 4294967296.0);

                        for (int n = 0; n < ring; ++n)
                        {
                            const auto partial = exact ? parabola (step * static_cast<std::uint32_t> (n))
                                                       : std::sin (2.0 * pi * hz * n / config.rate);
                            channel[static_cast<std::size_t> (n)] += static_cast<Sample> (0.3 * partial);
                        }
                    }
                }
            }

            buffer.assign (static_cast<std::size_t> (config.channels), std::vector<Sample> (static_cast<std::size_t> (largestBlock)));

            for (auto& channel : buffer)
                pointers.push_back (channel.data());

            side.assign (2, std::vector<Sample> (static_cast<std::size_t> (largestBlock)));
            sidePointers = { side[0].data(), side[1].data() };

            key.self = pointers.data();
            key.selfChannels = config.channels;

            if (scenario.keyed())
            {
                key.sidechain = sidePointers.data();
                key.sidechainChannels = 2;
            }
        }

        /** The block that starts at sample `start`: the audio, and the
            sidechain of its own noise (in bursts, for `transient`). */
        void fill (long long start, int size, bool silent)
        {
            for (int c = 0; c < config.channels; ++c)
            {
                auto& channel = buffer[static_cast<std::size_t> (c)];
                const auto& from = source[static_cast<std::size_t> (c)];

                for (int i = 0; i < size; ++i)
                    channel[static_cast<std::size_t> (i)] = silent ? Sample (0) : from[static_cast<std::size_t> ((start + i) % ring)];
            }

            if (! scenario.keyed())
                return;

            for (std::size_t s = 0; s < side.size(); ++s)
                for (int i = 0; i < size; ++i)
                {
                    const auto gate = scenario.transient && ((start + i) / burst) % 2 != 0 ? Sample (0.001) : Sample (1);
                    side[s][static_cast<std::size_t> (i)] = gate * source[s % source.size()][static_cast<std::size_t> ((start + i + 7919LL * static_cast<long long> (s + 1)) % ring)];
                }
        }

        void process (int size)
        {
            chain.process (pointers.data(), config.channels, key, size);
        }

        TimeConfig config;
        Scenario scenario;
        int ring;
        long long burst;
        Chain chain;
        std::vector<std::vector<Sample>> source, buffer, side;
        std::vector<Sample*> pointers;
        std::array<const Sample*, 2> sidePointers {};
        Key<Sample> key;
    };

    //==========================================================================
    template <typename Sample>
    std::vector<double> timeBlocks (const TimeConfig& config, int totalBlocks, bool silenceInSecondHalf)
    {
        Rig<Sample> rig (config, false, config.block);

        std::vector<double> micros;
        micros.reserve (static_cast<std::size_t> (totalBlocks));

        SeedBankCoefficients bankCoefficients;
        bankCoefficients.prepare (config.rate);
        SeedBank bank;
        std::array<float, controlStep> seedKey {};
        volatile float seedSink = 0.0f;

        for (int block = 0; block < totalBlocks; ++block)
        {
            const auto silent = silenceInSecondHalf && block >= totalBlocks / 2;
            rig.fill (static_cast<long long> (block) * config.block, config.block, silent);

            if (rig.scenario.moving)
                for (int b = 0; b < config.bands; ++b)
                    rig.chain.setBand (b, movingBand (b, block, false));

            // The seeding bank alone, on the first channel, as the chain feeds it: one control step at a time.
            if (rig.scenario.seeding)
            {
                const auto t0 = std::chrono::steady_clock::now();

                for (int done = 0; done < config.block; done += controlStep)
                {
                    for (int i = 0; i < controlStep; ++i)
                        seedKey[static_cast<std::size_t> (i)] = static_cast<float> (rig.buffer[0][static_cast<std::size_t> (std::min (done + i, config.block - 1))]);

                    bank.run (seedKey.data(), std::min (controlStep, config.block - done), bankCoefficients, true);
                }

                const auto t1 = std::chrono::steady_clock::now();
                micros.push_back (std::chrono::duration<double, std::micro> (t1 - t0).count());

                // Read, so that the bank is not optimised away.
                for (const auto p : bank.power)
                    seedSink = seedSink + p;

                continue;
            }

            const auto t0 = std::chrono::steady_clock::now();
            rig.process (config.block);
            const auto t1 = std::chrono::steady_clock::now();

            micros.push_back (std::chrono::duration<double, std::micro> (t1 - t0).count());
        }

        return micros;
    }

    struct Report
    {
        std::string name;
        Distribution us;
        int blocks = 0;
    };

    /** One run of a configuration: one report, or two for `decay`. */
    std::vector<Report> measure (const TimeConfig& config)
    {
        const auto totalBlocks = config.warmup + static_cast<int> (std::ceil (config.seconds * config.rate / config.block));
        const auto decay = config.scenario == "decay";
        const auto micros = config.buffers == "double" ? timeBlocks<double> (config, totalBlocks, decay)
                                                       : timeBlocks<float> (config, totalBlocks, decay);
        std::vector<Report> reports;

        if (decay)
        {
            const auto half = totalBlocks / 2;
            reports.push_back ({ "signal", distributionOf (std::vector<double> (micros.begin() + config.warmup, micros.begin() + half)), half - config.warmup });
            reports.push_back ({ "silence", distributionOf (std::vector<double> (micros.begin() + half, micros.end())), totalBlocks - half });
        }
        else
        {
            reports.push_back ({ config.scenario, distributionOf (std::vector<double> (micros.begin() + config.warmup, micros.end())), totalBlocks - config.warmup });
        }

        return reports;
    }

    double periodUsOf (const TimeConfig& config) noexcept
    {
        return 1.0e6 * config.block / config.rate;
    }

    void print (const TimeConfig& config, const Report& r)
    {
        const auto periodUs = periodUsOf (config);

        std::printf ("%s: %d blocks\n", r.name.c_str(), r.blocks);
        std::printf ("  us per block  min %.3f  median %.3f  trimmed %.3f  p99 %.3f  p999 %.3f  max %.3f  mean %.3f\n",
                     r.us.min, r.us.median, r.us.trimmed, r.us.p99, r.us.p999, r.us.max, r.us.mean);
        std::printf ("  share of one core  median %.3f %%  trimmed %.3f %%  worst block %.2f %% of its period (%.3f us)\n",
                     100.0 * r.us.median / periodUs, 100.0 * r.us.trimmed / periodUs, 100.0 * r.us.max / periodUs, periodUs);
    }

    void appendJson (std::ostringstream& out, const Report& r, bool last)
    {
        out << "{ \"name\": \"" << r.name << "\", \"blocks\": " << r.blocks
            << ", \"us\": { \"min\": " << r.us.min << ", \"median\": " << r.us.median << ", \"trimmed\": " << r.us.trimmed
            << ", \"p99\": " << r.us.p99 << ", \"p999\": " << r.us.p999 << ", \"max\": " << r.us.max
            << ", \"mean\": " << r.us.mean << " } }" << (last ? "\n" : ",\n");
    }

    /** What the figures were made by, for a note's first line. */
    std::string compilerName()
    {
        std::ostringstream out;
       #if defined (__clang__)
        out << "Clang " << __clang_version__;
       #elif defined (__GNUC__)
        out << "GCC " << __VERSION__;
       #elif defined (_MSC_VER)
        out << "MSVC " << _MSC_FULL_VER;
       #else
        out << "unknown compiler";
       #endif
       #if defined (NDEBUG)
        out << ", Release";
       #else
        out << ", Debug (figures from a Debug build are not figures)";
       #endif
        return out.str();
    }

    /** The clock's own step and what an empty interval reads, in ns: what every figure carries. */
    void printClock()
    {
        std::vector<double> empty;
        double step = 1.0e9;

        for (int i = 0; i < 20000; ++i)
        {
            const auto t0 = std::chrono::steady_clock::now();
            const auto t1 = std::chrono::steady_clock::now();
            const auto ns = std::chrono::duration<double, std::nano> (t1 - t0).count();
            empty.push_back (ns);

            if (ns > 0.0)
                step = std::min (step, ns);
        }

        std::printf ("clock: an empty interval reads %.0f ns (median), its smallest step %.0f ns\n", medianOf (empty), step);
    }

    void writeFile (const std::string& path, const std::string& text)
    {
        std::ofstream file (path, std::ios::binary);
        file << text;
        std::printf ("written: %s\n", path.c_str());
    }

    int runTime (const TimeConfig& config, const std::string& thread)
    {
        std::printf ("likyu_bench time: %s, %d bands, %d channels, %.0f Hz, blocks of %d, lookahead %d samples, %s buffers, %.1f s, warm-up %d blocks\n",
                     config.scenario.c_str(), config.bands, config.channels, config.rate, config.block,
                     lookaheadSamples (config.lookahead, config.rate), config.buffers.c_str(), config.seconds, config.warmup);
        std::printf ("%s; thread: %s\n", compilerName().c_str(), thread.c_str());
        printClock();

        std::vector<std::vector<Report>> runs;

        for (int run = 0; run < config.runs; ++run)
        {
            runs.push_back (measure (config));

            for (const auto& r : runs.back())
                print (config, r);

            if (runs.back().size() == 2)
                std::printf ("silence / signal, median: %.3f\n", runs.back()[1].us.median / std::max (runs.back()[0].us.median, 1.0e-9));
        }

        if (config.runs > 1)
        {
            for (std::size_t part = 0; part < runs.front().size(); ++part)
            {
                std::vector<double> medians, trimmed;

                for (const auto& run : runs)
                {
                    medians.push_back (run[part].us.median);
                    trimmed.push_back (run[part].us.trimmed);
                }

                std::printf ("%s, the median of %d runs: median %.3f us (%.3f %%), trimmed %.3f us (%.3f %%)\n",
                             runs.front()[part].name.c_str(), config.runs,
                             medianOf (medians), 100.0 * medianOf (medians) / periodUsOf (config),
                             medianOf (trimmed), 100.0 * medianOf (trimmed) / periodUsOf (config));
            }
        }

        if (! config.json.empty())
        {
            std::ostringstream out;
            out.imbue (std::locale::classic());
            out.precision (6);
            out << "{\n  \"scenario\": \"" << config.scenario << "\", \"rate\": " << config.rate << ", \"block\": " << config.block
                << ", \"channels\": " << config.channels << ", \"bands\": " << config.bands
                << ", \"lookaheadSamples\": " << lookaheadSamples (config.lookahead, config.rate)
                << ", \"buffers\": \"" << config.buffers << "\", \"seconds\": " << config.seconds
                << ", \"periodUs\": " << periodUsOf (config) << ",\n  \"compiler\": \"" << compilerName()
                << "\", \"thread\": \"" << thread << "\",\n  \"runs\": [\n";

            for (std::size_t run = 0; run < runs.size(); ++run)
                for (std::size_t i = 0; i < runs[run].size(); ++i)
                {
                    out << "    ";
                    appendJson (out, runs[run][i], run + 1 == runs.size() && i + 1 == runs[run].size());
                }

            out << "  ]\n}\n";
            writeFile (config.json, out.str());
        }

        return 0;
    }

    //==========================================================================
    struct Case
    {
        const char* name;
        const char* scenario;
        int channels;
        int bands;
        double rate;
    };

    /** A bench note's matrix. The first ten are the notes' own table since Phase 5. */
    constexpr Case suiteCases[] = {
        { "static", "static", 2, 8, 96000.0 },
        { "moving", "moving", 2, 8, 96000.0 },
        { "dynamic", "dynamic", 2, 8, 96000.0 },
        { "dynself", "dynself", 2, 8, 96000.0 },
        { "unlinked", "unlinked", 2, 8, 96000.0 },
        { "transient", "transient", 2, 8, 96000.0 },
        { "env", "env", 2, 8, 96000.0 },
        { "track", "track", 2, 8, 96000.0 },
        { "trackself", "trackself", 2, 8, 96000.0 },
        { "seeding", "seeding", 2, 8, 96000.0 },

        { "track.48k", "track", 2, 8, 48000.0 },

        // A figure split into its parts: nothing, then one band, two, four.
        { "static.b0", "static", 2, 0, 96000.0 },
        { "static.b1", "static", 2, 1, 96000.0 },
        { "static.b2", "static", 2, 2, 96000.0 },
        { "static.b4", "static", 2, 4, 96000.0 },
        { "dynamic.b1", "dynamic", 2, 1, 96000.0 },
        { "dynamic.b2", "dynamic", 2, 2, 96000.0 },
        { "dynamic.b4", "dynamic", 2, 4, 96000.0 },
        { "track.b1", "track", 2, 1, 96000.0 },
        { "track.b2", "track", 2, 2, 96000.0 },
        { "track.b4", "track", 2, 4, 96000.0 },

        // The widest layout. `track.64` is the devplan's second case.
        { "static.64", "static", 64, 8, 96000.0 },
        { "dynamic.64", "dynamic", 64, 8, 96000.0 },
        { "dynself.64", "dynself", 64, 8, 96000.0 },
        { "unlinked.64", "unlinked", 64, 8, 96000.0 },
        { "track.64", "track", 64, 8, 96000.0 },
        { "trackself.64", "trackself", 64, 8, 96000.0 },
    };

    int runSuite (const TimeConfig& base, const std::string& thread, const std::string& label)
    {
        std::printf ("likyu_bench suite: blocks of %d, float buffers, %.1f s a run, %d runs a case, warm-up %d blocks\n",
                     base.block, base.seconds, base.runs, base.warmup);
        std::printf ("%s; thread: %s\n", compilerName().c_str(), thread.c_str());
        printClock();
        std::printf ("\n  %-14s %10s %10s %9s %10s %10s\n", "case", "median us", "trimmed us", "share %", "p99 us", "max us");

        std::ostringstream out;
        out.imbue (std::locale::classic());
        out.precision (6);
        out << "{\n  \"label\": \"" << label << "\", \"compiler\": \"" << compilerName() << "\", \"thread\": \"" << thread
            << "\",\n  \"block\": " << base.block << ", \"seconds\": " << base.seconds << ", \"runs\": " << base.runs
            << ",\n  \"cases\": [\n";

        const auto count = std::size (suiteCases);

        for (std::size_t i = 0; i < count; ++i)
        {
            const auto& c = suiteCases[i];
            auto config = base;
            config.scenario = c.scenario;
            config.channels = c.channels;
            config.bands = c.bands;
            config.rate = c.rate;

            std::vector<Report> runs;
            std::vector<double> medians, trimmed, p99, worst;

            for (int run = 0; run < config.runs; ++run)
            {
                runs.push_back (measure (config).front());
                medians.push_back (runs.back().us.median);
                trimmed.push_back (runs.back().us.trimmed);
                p99.push_back (runs.back().us.p99);
                worst.push_back (runs.back().us.max);
            }

            const auto periodUs = periodUsOf (config);
            std::printf ("  %-14s %10.3f %10.3f %9.3f %10.3f %10.3f\n", c.name, medianOf (medians), medianOf (trimmed),
                         100.0 * medianOf (trimmed) / periodUs, medianOf (p99), *std::max_element (worst.begin(), worst.end()));
            std::fflush (stdout);

            out << "    { \"name\": \"" << c.name << "\", \"scenario\": \"" << c.scenario << "\", \"channels\": " << c.channels
                << ", \"bands\": " << c.bands << ", \"rate\": " << c.rate << ", \"periodUs\": " << periodUs
                << ", \"medianUs\": " << medianOf (medians) << ", \"trimmedUs\": " << medianOf (trimmed)
                << ", \"p99Us\": " << medianOf (p99) << ", \"maxUs\": " << *std::max_element (worst.begin(), worst.end())
                << ",\n      \"runs\": [\n";

            for (std::size_t run = 0; run < runs.size(); ++run)
            {
                out << "        ";
                appendJson (out, runs[run], run + 1 == runs.size());
            }

            out << "      ] }" << (i + 1 == count ? "\n" : ",\n");
        }

        out << "  ]\n}\n";

        if (! base.json.empty())
            writeFile (base.json, out.str());

        return 0;
    }

    //==========================================================================
    /** FNV-1a, 64 bits, over bytes. */
    struct Hash
    {
        std::uint64_t value = 1469598103934665603ull;

        void add (const void* data, std::size_t size) noexcept
        {
            const auto* bytes = static_cast<const unsigned char*> (data);

            for (std::size_t i = 0; i < size; ++i)
            {
                value ^= bytes[i];
                value *= 1099511628211ull;
            }
        }

        template <typename Value>
        void add (const Value& v) noexcept
        {
            add (&v, sizeof (Value));
        }
    };

    /** One line of the ledger: a scenario rendered as a host with awkward
        blocks would, with something of everything changed on the way. */
    template <typename Sample>
    std::uint64_t hashScenario (const std::string& scenario, int channels)
    {
        constexpr int largestBlock = 256;

        TimeConfig config;
        config.scenario = scenario;
        config.channels = channels;
        config.rate = 48000.0;

        Rig<Sample> rig (config, true, largestBlock);
        const auto total = static_cast<long long> (1.5 * config.rate);
        const auto at = [&config] (double seconds) { return static_cast<long long> (seconds * config.rate); };

        struct Event { long long sample; int what; };
        const Event events[] = { { at (0.4), 0 }, { at (0.6), 1 }, { at (0.8), 2 }, { at (1.0), 3 }, { at (1.2), 4 }, { at (1.3), 5 } };
        std::size_t nextEvent = 0;

        Lcg sizes { 99 };
        Hash hash;
        auto block = 0;

        for (long long done = 0; done < total; ++block)
        {
            while (nextEvent < std::size (events) && events[nextEvent].sample <= done)
            {
                switch (events[nextEvent++].what)
                {
                    case 0:     // a type switch, and a band turned off
                    {
                        auto changed = rig.scenario.band (2);
                        changed.type = changed.type == BandType::bell ? BandType::lowShelf : BandType::bell;
                        rig.chain.setBand (2, changed);
                        auto off = rig.scenario.band (5);
                        off.on = false;
                        rig.chain.setBand (5, off);
                        break;
                    }

                    case 1:     rig.chain.setBandChannels (1, 2u); break;                                   // channel 2 left out of band 2
                    case 2:     rig.chain.setChain ({ false, -3.0, ! rig.scenario.unlinked }); break;      // the output gain
                    case 3:     rig.chain.setLookahead (lookaheadSamples (2, config.rate)); break;
                    case 4:     rig.chain.setListen (3); break;
                    default:    rig.chain.setListen (-1); break;
                }
            }

            const auto size = static_cast<int> (std::min<long long> (1 + static_cast<long long> (sizes.next() >> 8) % largestBlock, total - done));
            rig.fill (done, size, false);

            if (rig.scenario.moving)
                for (int b = 0; b < config.bands; ++b)
                    rig.chain.setBand (b, movingBand (b, block, true));

            rig.process (size);

            for (const auto& channel : rig.buffer)
                hash.add (channel.data(), static_cast<std::size_t> (size) * sizeof (Sample));

            done += size;
        }

        for (int b = 0; b < numBands; ++b)
        {
            const auto meter = rig.chain.bandMeter (b);
            hash.add (meter.offsetDb);
            hash.add (meter.frequencyHz);
            hash.add (meter.confidence);
        }

        return hash.value;
    }

    /** The seeding bank on its own: its thirty powers and states after a second. */
    std::uint64_t hashSeeding()
    {
        TimeConfig config;
        config.scenario = "trackself";
        config.channels = 1;
        config.rate = 48000.0;
        Rig<float> rig (config, true, controlStep);

        SeedBankCoefficients coefficients;
        coefficients.prepare (config.rate);
        SeedBank bank;

        for (long long done = 0; done < 48000; done += controlStep)
        {
            rig.fill (done, controlStep, false);
            bank.run (rig.buffer[0].data(), controlStep, coefficients, true);
        }

        Hash hash;
        hash.add (bank.power);
        hash.add (bank.s1);
        hash.add (bank.s2);
        return hash.value;
    }

    int runHash (const std::string& outPath, const std::string& expectPath)
    {
        std::vector<std::pair<std::string, std::uint64_t>> ledger;

        for (const auto* name : scenarioNames)
        {
            const std::string scenario = name;

            if (scenario == "decay" || scenario == "seeding")
                continue;

            ledger.push_back ({ scenario + ".2.float", hashScenario<float> (scenario, 2) });
            ledger.push_back ({ scenario + ".5.double", hashScenario<double> (scenario, 5) });
        }

        ledger.push_back ({ "seeding", hashSeeding() });

        std::ostringstream out;

        for (const auto& line : ledger)
        {
            char text[32];
            std::snprintf (text, sizeof (text), "%016llx", static_cast<unsigned long long> (line.second));
            out << line.first << ' ' << text << '\n';
        }

        std::printf ("likyu_bench hash: %s\n%s", compilerName().c_str(), out.str().c_str());

        if (! outPath.empty())
            writeFile (outPath, out.str());

        if (expectPath.empty())
            return 0;

        std::ifstream file (expectPath);

        if (! file)
        {
            std::fprintf (stderr, "error: cannot read %s\n", expectPath.c_str());
            return 2;
        }

        std::map<std::string, std::string> expected;
        std::string name, value;

        while (file >> name >> value)
            expected[name] = value;

        auto different = 0;

        for (const auto& line : ledger)
        {
            char text[32];
            std::snprintf (text, sizeof (text), "%016llx", static_cast<unsigned long long> (line.second));
            const auto found = expected.find (line.first);

            if (found == expected.end() || found->second != text)
            {
                std::printf ("DIFFERENT: %s is %s, expected %s\n", line.first.c_str(), text,
                             found == expected.end() ? "nothing" : found->second.c_str());
                ++different;
            }
        }

        if (different == 0 && expected.size() == ledger.size())
        {
            std::printf ("the ledger is %s, bit for bit\n", expectPath.c_str());
            return 0;
        }

        std::printf ("%d of %d lines differ from %s (which has %d)\n", different, static_cast<int> (ledger.size()),
                     expectPath.c_str(), static_cast<int> (expected.size()));
        return 1;
    }

    //==========================================================================
    /** One band, in precision T, every sample: the coefficients are made in T too. */
    template <typename T>
    std::vector<T> runBand (BandType type, double gainDb, const std::vector<double>& input, double rate)
    {
        const auto g = std::tan (static_cast<T> (pi) * static_cast<T> (20.0) / static_cast<T> (rate));
        const auto kq = T (1) / T (40);
        const auto a = std::pow (T (10), static_cast<T> (gainDb) / T (40));
        const auto sqrtA = std::sqrt (a);
        const auto c = designBand<T> (type, QMode::proportional, g, kq, hasGain (type) ? a : T (1), hasGain (type) ? sqrtA : T (1));

        std::vector<T> out (input.size());
        T ic1 {}, ic2 {};

        for (std::size_t n = 0; n < input.size(); ++n)
            out[n] = svfTick (static_cast<T> (input[n]), ic1, ic2, c);

        return out;
    }

    template <typename T>
    double errorDb (const std::vector<T>& output, const std::vector<long double>& reference)
    {
        long double error = 0.0L, signal = 0.0L;

        for (auto n = reference.size() / 2; n < reference.size(); ++n)
        {
            const auto e = static_cast<long double> (output[n]) - reference[n];
            error += e * e;
            signal += reference[n] * reference[n];
        }

        if (! (error > 0.0L))
            return -std::numeric_limits<double>::infinity();

        return static_cast<double> (10.0L * std::log10 (error / signal));
    }

    int runPrecision (double seconds)
    {
        constexpr double rate = 192000.0;
        const auto length = static_cast<std::size_t> (seconds * rate);
        const auto wider = std::numeric_limits<long double>::digits > std::numeric_limits<double>::digits;

        std::printf ("likyu_bench precision: one band at 20 Hz, Q 40, %.0f Hz, %.1f s; error in dB relative to the output,\n"
                     "over the second half, against long double (%d mantissa bits; double has %d, float %d)\n\n",
                     rate, seconds, std::numeric_limits<long double>::digits, std::numeric_limits<double>::digits,
                     std::numeric_limits<float>::digits);

        std::vector<double> noise (length), tone (length);
        Lcg random { 4 };

        for (std::size_t n = 0; n < length; ++n)
        {
            noise[n] = random.bipolar();
            tone[n] = 0.1 * std::sin (2.0 * pi * 1000.0 * static_cast<double> (n) / rate);
        }

        struct PrecisionCase { const char* name; BandType type; double gainDb; };
        const PrecisionCase cases[] = { { "bell +12 dB", BandType::bell, 12.0 }, { "bell -12 dB", BandType::bell, -12.0 },
                                        { "low-pass   ", BandType::lowPass, 0.0 } };
        struct Input { const char* name; const std::vector<double>* samples; };
        const Input inputs[] = { { "noise, full scale  ", &noise }, { "sine 1 kHz, -20 dBFS", &tone } };

        std::printf ("  %-12s %-21s %12s %12s\n", "band", "input", "float", "double");

        for (const auto& c : cases)
        {
            for (const auto& in : inputs)
            {
                const auto reference = runBand<long double> (c.type, c.gainDb, *in.samples, rate);
                const auto f = errorDb (runBand<float> (c.type, c.gainDb, *in.samples, rate), reference);
                const auto d = errorDb (runBand<double> (c.type, c.gainDb, *in.samples, rate), reference);

                char doubleText[32];

                if (wider)
                    std::snprintf (doubleText, sizeof (doubleText), "%9.1f dB", d);
                else
                    std::snprintf (doubleText, sizeof (doubleText), "%12s", "n/a");

                std::printf ("  %-12s %-21s %9.1f dB %s\n", c.name, in.name, f, doubleText);
            }
        }

        if (! wider)
            std::printf ("\nlong double is no wider than double with this compiler: run on GCC or Clang, x86-64, for the double column.\n");

        return 0;
    }

    //==========================================================================
    void usage()
    {
        std::fprintf (stderr,
            "usage: likyu_bench time [--rate 96000] [--block 64] [--channels 2] [--bands 8]\n"
            "                        [--scenario static|moving|decay|dynamic|dynself|unlinked\n"
            "                                    |transient|env|track|trackself|seeding]\n"
            "                        [--lookahead 0|1|2|3] [--buffers float|double]\n"
            "                        [--seconds 10] [--warmup 200] [--runs 1]\n"
            "                        [--thread audio|plain] [--core p|e|any] [--json out.json]\n"
            "       likyu_bench suite [--seconds 3] [--runs 3] [--warmup 200]\n"
            "                         [--thread audio|plain] [--core p|e|any] [--label text] [--json out.json]\n"
            "       likyu_bench hash [--out ledger.txt] [--expect ledger.txt]\n"
            "       likyu_bench precision [--seconds 10]\n");
    }
}

int main (int argc, char* argv[])
{
    if (argc < 2)
    {
        usage();
        return 2;
    }

    const std::string mode = argv[1];
    const auto suite = mode == "suite";
    TimeConfig config;
    double seconds = 0.0;
    int runs = 0;
    std::string thread = "audio", core = "p", label, out, expect;

    for (int i = 2; i < argc; ++i)
    {
        const std::string arg = argv[i];

        if (i + 1 >= argc)
        {
            std::fprintf (stderr, "error: %s needs a value\n", arg.c_str());
            usage();
            return 2;
        }

        const std::string value = argv[++i];

        if (arg == "--rate")            config.rate = std::atof (value.c_str());
        else if (arg == "--block")      config.block = std::atoi (value.c_str());
        else if (arg == "--channels")   config.channels = std::atoi (value.c_str());
        else if (arg == "--bands")      config.bands = std::atoi (value.c_str());
        else if (arg == "--scenario")   config.scenario = value;
        else if (arg == "--lookahead")  config.lookahead = std::atoi (value.c_str());
        else if (arg == "--buffers")    config.buffers = value;
        else if (arg == "--seconds")    seconds = std::atof (value.c_str());
        else if (arg == "--warmup")     config.warmup = std::atoi (value.c_str());
        else if (arg == "--runs")       runs = std::atoi (value.c_str());
        else if (arg == "--thread")     thread = value;
        else if (arg == "--core")       core = value;
        else if (arg == "--label")      label = value;
        else if (arg == "--json")       config.json = value;
        else if (arg == "--out")        out = value;
        else if (arg == "--expect")     expect = value;
        else
        {
            std::fprintf (stderr, "error: unknown argument %s\n", arg.c_str());
            usage();
            return 2;
        }
    }

    if (mode == "precision")
        return runPrecision (seconds > 0.0 ? seconds : 10.0);

    if (mode == "hash")
        return runHash (out, expect);

    if (mode != "time" && ! suite)
    {
        usage();
        return 2;
    }

    config.seconds = seconds > 0.0 ? seconds : suite ? 3.0 : 10.0;
    config.runs = runs > 0 ? runs : suite ? 3 : 1;

    if (! (config.rate > 0.0) || config.block <= 0 || config.channels <= 0 || config.channels > Chain::maxChannels
        || config.bands < 0 || config.bands > numBands || config.warmup < 0 || ! isScenario (config.scenario)
        || config.lookahead < 0 || config.lookahead >= numLookaheadChoices
        || (config.buffers != "float" && config.buffers != "double")
        || (thread != "audio" && thread != "plain") || (core != "p" && core != "e" && core != "any"))
    {
        usage();
        return 2;
    }

    const auto cores = core == "p" ? likyubench::Cores::performance
                     : core == "e" ? likyubench::Cores::efficiency : likyubench::Cores::any;
    const auto claimed = likyubench::claimThread (cores, thread == "audio");

    return suite ? runSuite (config, claimed, label) : runTime (config, claimed);
}
