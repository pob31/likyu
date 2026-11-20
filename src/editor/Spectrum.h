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

#pragma once

/*
    THE ANALYSER'S SPECTRUM (PRD, Editor; devplan 6f), on the editor's
    timer: never on the audio thread, never fed to the DSP (PRD, non-goals:
    no FFT in the audio path).

      - 8192 samples, Hann window, a radix-2 FFT: 5.9 Hz between bins at
        48 kHz, a frame in well under a millisecond.
      - Read at the curve's own frequencies: the largest bin within a
        twelfth of an octave of each, or the bins either side interpolated
        where a twelfth of an octave holds none. A full-scale sine reads
        0 dB at its frequency, within the Hann window's 1.4 dB between bins.
      - TILTED by 3 dB per octave about 1 kHz, so that pink noise, and music
        roughly, lies flat instead of falling to the right. A sine at 1 kHz
        reads true; one at 125 Hz reads 9 dB under its level.
      - Shown with BALLISTICS: a level rises at once and falls at 24 dB a
        second, so that the picture is steady enough to read.

    The constants are the author's to change, here. No JUCE.
*/

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace likyu::editor
{
    class Spectrum
    {
    public:
        static constexpr int order = 13;
        static constexpr int size = 1 << order;
        static constexpr int bins = size / 2 + 1;

        /** Every level is at least this, in dB. */
        static constexpr double floorDb = -120.0;

        static constexpr double tiltDbPerOctave = 3.0;
        static constexpr double tiltPivotHz = 1000.0;
        static constexpr double fallDbPerSecond = 24.0;

        /** Half the width of the band each display frequency reads, in octaves. */
        static constexpr double halfBandOctaves = 1.0 / 24.0;

        Spectrum()
            : window (static_cast<std::size_t> (size)), work (static_cast<std::size_t> (size)),
              twiddles (static_cast<std::size_t> (size / 2)), power (static_cast<std::size_t> (bins))
        {
            const auto pi = 3.14159265358979323846;
            auto sum = 0.0;

            for (int n = 0; n < size; ++n)
            {
                window[static_cast<std::size_t> (n)] = 0.5 - 0.5 * std::cos (2.0 * pi * n / size);
                sum += window[static_cast<std::size_t> (n)];
            }

            // A full-scale sine's bin is |X| = sum(w) / 2: its power after this scale is 1.
            powerScale = 4.0 / (sum * sum);

            for (int k = 0; k < size / 2; ++k)
                twiddles[static_cast<std::size_t> (k)] = std::polar (1.0, -2.0 * pi * k / size);
        }

        /** The power of each bin of the latest `size` samples, scaled so that
            a full-scale sine's bin is 1. */
        const std::vector<double>& analyse (const float* samples)
        {
            for (int n = 0; n < size; ++n)
                work[static_cast<std::size_t> (reversed (n))]
                    = { window[static_cast<std::size_t> (n)] * static_cast<double> (samples[n]), 0.0 };

            for (int length = 2; length <= size; length <<= 1)
            {
                const auto half = length / 2, step = size / length;

                for (int start = 0; start < size; start += length)
                    for (int k = 0; k < half; ++k)
                    {
                        auto& a = work[static_cast<std::size_t> (start + k)];
                        auto& b = work[static_cast<std::size_t> (start + k + half)];
                        const auto t = twiddles[static_cast<std::size_t> (k * step)] * b;
                        b = a - t;
                        a += t;
                    }
            }

            for (int k = 0; k < bins; ++k)
                power[static_cast<std::size_t> (k)] = std::norm (work[static_cast<std::size_t> (k)]) * powerScale;

            return power;
        }

        /** The levels in dB, tilted, at the frequencies `hz`, of the bins
            `analyse` gave, for a stream at `rate`. */
        static void levelsAt (const std::vector<double>& binPower, double rate, const std::vector<double>& hz,
                              std::vector<double>& db)
        {
            db.assign (hz.size(), floorDb);

            if (! (rate > 0.0) || binPower.size() < 2)
                return;

            const auto binHz = rate / size;
            const auto last = static_cast<int> (binPower.size()) - 1;
            const auto spread = std::exp2 (halfBandOctaves);

            for (std::size_t i = 0; i < hz.size(); ++i)
            {
                const auto f = hz[i];

                if (! (f > 0.0) || f >= 0.5 * rate)
                    continue;

                const auto from = static_cast<int> (std::ceil (f / spread / binHz));
                const auto to = std::min (last, static_cast<int> (std::floor (f * spread / binHz)));
                auto p = 0.0;

                if (from <= to)
                {
                    for (int k = std::max (1, from); k <= to; ++k)
                        p = std::max (p, binPower[static_cast<std::size_t> (k)]);
                }
                else
                {
                    // Narrower than a bin: between the two either side.
                    const auto x = f / binHz;
                    const auto k = std::clamp (static_cast<int> (x), 0, last - 1);
                    const auto t = x - k;
                    p = (1.0 - t) * binPower[static_cast<std::size_t> (k)] + t * binPower[static_cast<std::size_t> (k + 1)];
                }

                const auto tilt = tiltDbPerOctave * std::log2 (f / tiltPivotHz);
                db[i] = p > 0.0 ? std::max (floorDb, 10.0 * std::log10 (p) + tilt) : floorDb;
            }
        }

        /** What is shown: `now` where it is higher, else falling towards it
            at fallDbPerSecond, `seconds` after the last. */
        static void fall (const std::vector<double>& now, double seconds, std::vector<double>& shown)
        {
            if (shown.size() != now.size())
            {
                shown = now;
                return;
            }

            const auto drop = fallDbPerSecond * std::max (0.0, seconds);

            for (std::size_t i = 0; i < now.size(); ++i)
                shown[i] = std::max (now[i], shown[i] - drop);
        }

    private:
        static int reversed (int n) noexcept
        {
            auto r = 0;

            for (int b = 0; b < order; ++b)
                r |= ((n >> b) & 1) << (order - 1 - b);

            return r;
        }

        std::vector<double> window;
        std::vector<std::complex<double>> work;
        std::vector<std::complex<double>> twiddles;
        std::vector<double> power;
        double powerScale = 1.0;
    };
}
