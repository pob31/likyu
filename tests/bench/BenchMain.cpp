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
    (devplan 2d and 2f). lib/dsp and the standard library only, so it builds
    in seconds anywhere, the farm included.

      likyu_bench time [--rate 96000] [--block 64] [--channels 2] [--bands 8]
                       [--scenario static|moving|decay|dynamic|unlinked]
                       [--buffers float|double] [--seconds 10] [--warmup 200]
                       [--json out.json]

        The chain's process call timed per block, back to back after a
        warm-up: microseconds per block (min, median, p99, p999, max, mean)
        and the median and mean as a share of the block's period, which is
        the share of one core. The shape of WFS-DIY's pipeline-bench, without
        its metronome: there is no pipeline here to pace.

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
          unlinked the same keyed by the input, unlinked: one gain computer
                   and one set of coefficients per channel per band.

        NOT INCLUDED: the shim's reading of 284 parameters per block, the
        key's copy and the lookahead delay. The figure is the DSP's.

      likyu_bench precision [--seconds 10]

        The noise floor of the audio path in float and in double (devplan 2d):
        a band at 20 Hz, Q 40, 192 kHz (a bell at +12 and -12 dB, a low-pass)
        on full-scale noise and on a -20 dBFS sine at 1 kHz, each precision
        against the same band in long double. The error is reported relative
        to the output, over the second half of the run. Where long double is
        no wider than double (MSVC), the double row has no reference and says
        so.

    Exit codes: 0 done, 2 usage.
*/

#include <dsp/BandCoefficients.h>
#include <dsp/Chain.h>
#include <dsp/Gain.h>
#include <dsp/Svf.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using namespace likyu::dsp;

    //==========================================================================
    struct Lcg
    {
        std::uint32_t state = 1;

        double bipolar() noexcept
        {
            state = state * 1664525u + 1013904223u;
            return static_cast<double> (state >> 8) / 8388608.0 - 1.0;
        }
    };

    struct Distribution
    {
        double min = 0.0, median = 0.0, p99 = 0.0, p999 = 0.0, max = 0.0, mean = 0.0;
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
        return d;
    }

    //==========================================================================
    struct TimeConfig
    {
        double rate = 96000.0;
        int block = 64;
        int channels = 2;
        int bands = numBands;
        std::string scenario = "static";
        std::string buffers = "float";
        double seconds = 10.0;
        int warmup = 200;
        std::string json;
    };

    /** The static settings: mixed types, none an identity. */
    BandSettings staticBand (int b)
    {
        static constexpr BandType types[] = { BandType::highPass, BandType::lowShelf, BandType::bell, BandType::bell,
                                              BandType::bell, BandType::notch, BandType::highShelf, BandType::lowPass };
        const auto index = static_cast<std::size_t> (b);
        return { true, types[index], b % 2 == 0 ? QMode::proportional : QMode::constant,
                 40.0 * std::pow (2.0, 1.2 * b), 0.7 + 0.5 * b, b % 2 == 0 ? 4.5 : -6.0 };
    }

    /** For `dynamic` and `unlinked`: bells and shelves, every one acting on noise. */
    BandSettings dynamicBand (int b, bool sidechain)
    {
        static constexpr BandType types[] = { BandType::bell, BandType::lowShelf, BandType::highShelf };
        BandSettings s;
        s.on = true;
        s.type = types[static_cast<std::size_t> (b % 3)];
        s.qmode = b % 2 == 0 ? QMode::proportional : QMode::constant;
        s.frequency = 40.0 * std::pow (2.0, 1.2 * b);
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

    /** For `moving`: a new target every block, so no ramp ever lands. */
    BandSettings movingBand (int b, int block)
    {
        auto s = staticBand (b);
        const auto phase = static_cast<double> ((block + 37 * b) % 400) / 400.0;
        s.frequency *= std::pow (2.0, 2.0 * std::sin (2.0 * pi * phase));
        s.q *= std::pow (2.0, std::cos (2.0 * pi * phase));
        s.gainDb += 6.0 * std::sin (2.0 * pi * phase);
        return s;
    }

    template <typename Sample>
    std::vector<double> timeBlocks (const TimeConfig& config, int totalBlocks, bool silenceInSecondHalf)
    {
        Chain chain;
        chain.prepare (config.rate, config.channels);

        const auto dynamic = config.scenario == "dynamic", unlinked = config.scenario == "unlinked";

        for (int b = 0; b < config.bands; ++b)
            chain.setBand (b, dynamic || unlinked ? dynamicBand (b, dynamic) : staticBand (b));

        if (unlinked)
            chain.setChain ({ false, 0.0, false });

        // Two seconds of noise per channel, precomputed, read round.
        const auto ring = static_cast<int> (2.0 * config.rate);
        std::vector<std::vector<Sample>> source (static_cast<std::size_t> (config.channels));

        for (int c = 0; c < config.channels; ++c)
        {
            Lcg random { static_cast<std::uint32_t> (c + 1) };
            auto& channel = source[static_cast<std::size_t> (c)];
            channel.resize (static_cast<std::size_t> (ring));

            for (auto& sample : channel)
                sample = static_cast<Sample> (0.5 * random.bipolar());
        }

        std::vector<std::vector<Sample>> buffer (static_cast<std::size_t> (config.channels),
                                                 std::vector<Sample> (static_cast<std::size_t> (config.block)));
        std::vector<Sample*> pointers;

        for (auto& channel : buffer)
            pointers.push_back (channel.data());

        // A stereo sidechain of its own noise, for `dynamic`.
        std::vector<std::vector<Sample>> side (2, std::vector<Sample> (static_cast<std::size_t> (config.block)));
        const Sample* sidePointers[] = { side[0].data(), side[1].data() };
        Key<Sample> key;
        key.self = pointers.data();
        key.selfChannels = config.channels;

        if (dynamic)
        {
            key.sidechain = sidePointers;
            key.sidechainChannels = 2;
        }

        std::vector<double> micros;
        micros.reserve (static_cast<std::size_t> (totalBlocks));

        for (int block = 0; block < totalBlocks; ++block)
        {
            const auto silent = silenceInSecondHalf && block >= totalBlocks / 2;
            const auto start = static_cast<long long> (block) * config.block;

            for (int c = 0; c < config.channels; ++c)
            {
                auto& channel = buffer[static_cast<std::size_t> (c)];
                const auto& from = source[static_cast<std::size_t> (c)];

                for (int i = 0; i < config.block; ++i)
                    channel[static_cast<std::size_t> (i)] = silent ? Sample (0)
                                                                   : from[static_cast<std::size_t> ((start + i) % ring)];
            }

            if (dynamic)
                for (std::size_t s = 0; s < side.size(); ++s)
                    for (int i = 0; i < config.block; ++i)
                        side[s][static_cast<std::size_t> (i)] = source[s % source.size()][static_cast<std::size_t> ((start + i + 7919LL * static_cast<long long> (s + 1)) % ring)];

            if (config.scenario == "moving")
                for (int b = 0; b < config.bands; ++b)
                    chain.setBand (b, movingBand (b, block));

            const auto t0 = std::chrono::steady_clock::now();
            chain.process (pointers.data(), config.channels, key, config.block);
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

    void print (const TimeConfig& config, const Report& r)
    {
        const auto periodUs = 1.0e6 * config.block / config.rate;

        std::printf ("%s: %d blocks\n", r.name.c_str(), r.blocks);
        std::printf ("  us per block  min %.3f  median %.3f  p99 %.3f  p999 %.3f  max %.3f  mean %.3f\n",
                     r.us.min, r.us.median, r.us.p99, r.us.p999, r.us.max, r.us.mean);
        std::printf ("  share of one core  median %.3f %%  mean %.3f %%  worst block %.2f %% of its period (%.3f us)\n",
                     100.0 * r.us.median / periodUs, 100.0 * r.us.mean / periodUs, 100.0 * r.us.max / periodUs, periodUs);
    }

    void appendJson (std::ostringstream& out, const Report& r, bool last)
    {
        out << "    { \"name\": \"" << r.name << "\", \"blocks\": " << r.blocks
            << ", \"us\": { \"min\": " << r.us.min << ", \"median\": " << r.us.median << ", \"p99\": " << r.us.p99
            << ", \"p999\": " << r.us.p999 << ", \"max\": " << r.us.max << ", \"mean\": " << r.us.mean << " } }"
            << (last ? "\n" : ",\n");
    }

    template <typename Sample>
    int runTime (const TimeConfig& config)
    {
        const auto totalBlocks = config.warmup + static_cast<int> (std::ceil (config.seconds * config.rate / config.block));
        const auto decay = config.scenario == "decay";
        const auto micros = timeBlocks<Sample> (config, totalBlocks, decay);

        std::printf ("likyu_bench time: %s, %d bands, %d channels, %.0f Hz, blocks of %d, %s buffers, %.1f s, warm-up %d blocks\n",
                     config.scenario.c_str(), config.bands, config.channels, config.rate, config.block,
                     config.buffers.c_str(), config.seconds, config.warmup);

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

        for (const auto& r : reports)
            print (config, r);

        if (decay)
            std::printf ("silence / signal, median: %.3f\n", reports[1].us.median / std::max (reports[0].us.median, 1.0e-9));

        if (! config.json.empty())
        {
            std::ostringstream out;
            out.imbue (std::locale::classic());
            out.precision (6);
            out << "{\n  \"scenario\": \"" << config.scenario << "\", \"rate\": " << config.rate << ", \"block\": " << config.block
                << ", \"channels\": " << config.channels << ", \"bands\": " << config.bands
                << ", \"buffers\": \"" << config.buffers << "\", \"seconds\": " << config.seconds
                << ", \"periodUs\": " << 1.0e6 * config.block / config.rate << ",\n  \"runs\": [\n";

            for (std::size_t i = 0; i < reports.size(); ++i)
                appendJson (out, reports[i], i + 1 == reports.size());

            out << "  ]\n}\n";

            std::ofstream file (config.json, std::ios::binary);
            file << out.str();
            std::printf ("written: %s\n", config.json.c_str());
        }

        return 0;
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

        struct Case { const char* name; BandType type; double gainDb; };
        const Case cases[] = { { "bell +12 dB", BandType::bell, 12.0 }, { "bell -12 dB", BandType::bell, -12.0 },
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
            "                        [--scenario static|moving|decay|dynamic|unlinked]\n"
            "                        [--buffers float|double] [--seconds 10] [--warmup 200] [--json out.json]\n"
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
    TimeConfig config;
    double precisionSeconds = 10.0;

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
        else if (arg == "--buffers")    config.buffers = value;
        else if (arg == "--seconds")    config.seconds = precisionSeconds = std::atof (value.c_str());
        else if (arg == "--warmup")     config.warmup = std::atoi (value.c_str());
        else if (arg == "--json")       config.json = value;
        else
        {
            std::fprintf (stderr, "error: unknown argument %s\n", arg.c_str());
            usage();
            return 2;
        }
    }

    if (mode == "precision")
        return runPrecision (precisionSeconds > 0.0 ? precisionSeconds : 10.0);

    if (mode != "time")
    {
        usage();
        return 2;
    }

    if (! (config.rate > 0.0) || config.block <= 0 || config.channels <= 0 || config.channels > Chain::maxChannels
        || config.bands < 0 || config.bands > numBands || ! (config.seconds > 0.0) || config.warmup < 0
        || (config.scenario != "static" && config.scenario != "moving" && config.scenario != "decay"
            && config.scenario != "dynamic" && config.scenario != "unlinked")
        || (config.buffers != "float" && config.buffers != "double"))
    {
        usage();
        return 2;
    }

    return config.buffers == "double" ? runTime<double> (config) : runTime<float> (config);
}
