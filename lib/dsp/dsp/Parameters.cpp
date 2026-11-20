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

#include "Parameters.h"

#include "Math.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

namespace likyu::dsp
{
    namespace
    {
        using Choices = std::vector<std::string>;

        ParamSpec toggle (std::string id, std::string name, bool on, bool automatable = true)
        {
            return { std::move (id), std::move (name), {}, ParamKind::toggle, 0.0, 1.0, on ? 1.0 : 0.0,
                     ParamSkew::linear, automatable, {} };
        }

        ParamSpec choice (std::string id, std::string name, Choices entries, int defaultIndex, bool automatable = true)
        {
            const auto last = static_cast<double> (entries.size()) - 1.0;
            return { std::move (id), std::move (name), {}, ParamKind::choice, 0.0, last,
                     static_cast<double> (defaultIndex), ParamSkew::linear, automatable, std::move (entries) };
        }

        ParamSpec linear (std::string id, std::string name, std::string unit, double minimum, double maximum, double value)
        {
            return { std::move (id), std::move (name), std::move (unit), ParamKind::continuous, minimum, maximum, value,
                     ParamSkew::linear, true, {} };
        }

        ParamSpec logarithmic (std::string id, std::string name, std::string unit, double minimum, double maximum, double value)
        {
            return { std::move (id), std::move (name), std::move (unit), ParamKind::continuous, minimum, maximum, value,
                     ParamSkew::logarithmic, true, {} };
        }

        /*  "Spread per band" (PRD): where each band's Frequency rests by
            default. Marked tune: they may move until the first public beta. */
        constexpr double defaultFrequency[numBands] = { 30.0, 80.0, 200.0, 500.0, 1200.0, 3000.0, 7000.0, 14000.0 };

        void appendBand (std::vector<ParamSpec>& table, int band)
        {
            const auto number = std::to_string (band + 1);
            const auto id = [&number] (const char* suffix) { return "b" + number + "_" + suffix; };
            const auto name = [&number] (const char* what) { return "Band " + number + " " + what; };

            const auto frequency = defaultFrequency[band];

            /*  One octave either side of the band's DEFAULT frequency: fixed
                values, not linked to `freq` (PRD, decided 2026-11-16). */
            const auto low = std::max (minFrequency, frequency * 0.5);
            const auto high = std::min (maxFrequency, frequency * 2.0);

            // All types.
            table.push_back (toggle (id ("on"), name ("On"), false));
            table.push_back (choice (id ("type"), name ("Type"),
                                     { "Bell", "Low shelf", "High shelf", "High-pass", "Low-pass", "Notch", "Band-pass" }, 0));
            table.push_back (logarithmic (id ("freq"), name ("Frequency"), "Hz", minFrequency, maxFrequency, frequency));
            table.push_back (logarithmic (id ("q"), name ("Q"), "", minQ, maxQ, 1.0));
            table.push_back (linear (id ("gain"), name ("Gain"), "dB", -30.0, 30.0, 0.0));
            table.push_back (choice (id ("qmode"), name ("Q Mode"), { "Proportional", "Constant" }, 0));
            table.push_back (choice (id ("gmode"), name ("Gain Mode"), { "Static", "Dynamic", "Transient" }, 0));
            table.push_back (choice (id ("fmode"), name ("Frequency Mode"), { "Fixed", "Env", "Track" }, 0));

            // Detector.
            table.push_back (choice (id ("key"), name ("Key"), { "Self", "Sidechain" }, 0));
            table.push_back (choice (id ("dfilt"), name ("Detector Filter"), { "Follow", "Own" }, 0));
            table.push_back (choice (id ("dtype"), name ("Detector Type"), { "Band-pass", "High-pass", "Low-pass" }, 0));
            table.push_back (logarithmic (id ("dfreq"), name ("Detector Frequency"), "Hz", minFrequency, maxFrequency, 1000.0));
            table.push_back (logarithmic (id ("dq"), name ("Detector Q"), "", minQ, maxQ, 1.0));

            // Dynamic (range also limits Transient).
            table.push_back (linear (id ("range"), name ("Range"), "dB", -maxRangeDb, maxRangeDb, -6.0));
            table.push_back (linear (id ("thresh"), name ("Threshold"), "dB", minThresholdDb, maxThresholdDb, -24.0));
            table.push_back (linear (id ("ratio"), name ("Ratio"), "", minRatio, maxRatio, 3.0));
            table.push_back (linear (id ("knee"), name ("Knee"), "dB", 0.0, maxKneeDb, 6.0));
            table.push_back (logarithmic (id ("atk"), name ("Attack"), "ms", minAttackMs, maxAttackMs, 5.0));
            table.push_back (logarithmic (id ("rel"), name ("Release"), "ms", minReleaseMs, maxReleaseMs, 120.0));
            table.push_back (choice (id ("ddir"), name ("Direction"), { "Above", "Below" }, 0));

            // Transient.
            table.push_back (linear (id ("tatk"), name ("Transient Attack"), "%", -maxTransientAmountPercent, maxTransientAmountPercent, 0.0));
            table.push_back (linear (id ("tsus"), name ("Transient Sustain"), "%", -maxTransientAmountPercent, maxTransientAmountPercent, 0.0));
            table.push_back (logarithmic (id ("ttime"), name ("Transient Time"), "x", minTransientTime, maxTransientTime, 1.0));
            table.push_back (linear (id ("tfloor"), name ("Transient Floor"), "dB", minTransientFloorDb, maxTransientFloorDb, -60.0));

            // Env and Track.
            table.push_back (logarithmic (id ("flo"), name ("Frequency Low"), "Hz", minFrequency, maxFrequency, low));
            table.push_back (logarithmic (id ("fhi"), name ("Frequency High"), "Hz", minFrequency, maxFrequency, high));
            table.push_back (logarithmic (id ("fspeed"), name ("Track Speed"), "ms", 1.0, 1000.0, 50.0));
            table.push_back (choice (id ("fdir"), name ("Env Direction"), { "Up", "Down" }, 0));
            table.push_back (linear (id ("fsens"), name ("Env Sensitivity"), "dB", -80.0, 0.0, -40.0));
            table.push_back (linear (id ("fspan"), name ("Env Span"), "dB", 6.0, 60.0, 30.0));
            table.push_back (logarithmic (id ("fatk"), name ("Env Attack"), "ms", 0.05, 200.0, 5.0));
            table.push_back (logarithmic (id ("frel"), name ("Env Release"), "ms", 5.0, 2000.0, 120.0));
            table.push_back (linear (id ("fconf"), name ("Track Confidence"), "%", 0.0, 100.0, 50.0));
            table.push_back (linear (id ("fhold"), name ("Track Hold"), "ms", 0.0, 2000.0, 200.0));
            table.push_back (toggle (id ("fcgain"), name ("Confidence Scales Gain"), false));
        }

        std::vector<ParamSpec> buildTable()
        {
            std::vector<ParamSpec> table;
            table.reserve (static_cast<std::size_t> (numParams));

            // Per instance. Lookahead and link are choices of the instance, not automatable.
            table.push_back (toggle ("bypass", "Bypass", false));
            table.push_back (linear ("out_gain", "Output Gain", "dB", -maxOutputGainDb, maxOutputGainDb, 0.0));
            table.push_back (choice ("lookahead", "Lookahead", { "Off", "0.25 ms", "1 ms", "3 ms" }, 0, false));
            table.push_back (choice ("link", "Link", { "Linked", "Unlinked" }, 0, false));

            for (int band = 0; band < numBands; ++band)
                appendBand (table, band);

            return table;
        }
    }

    const std::vector<ParamSpec>& parameterTable()
    {
        static const std::vector<ParamSpec> table = buildTable();
        return table;
    }

    const ParamSpec& temporaryListenSpec()
    {
        static const ParamSpec spec = []
        {
            Choices entries { "Off" };

            for (int band = 0; band < numBands; ++band)
                entries.push_back ("Band " + std::to_string (band + 1));

            return choice ("tmp_listen", "Listen (temporary)", std::move (entries), 0, false);
        }();

        return spec;
    }

    int indexOfId (std::string_view id)
    {
        static const auto byId = []
        {
            std::map<std::string, int, std::less<>> map;
            const auto& table = parameterTable();

            for (std::size_t i = 0; i < table.size(); ++i)
                map.emplace (table[i].id, static_cast<int> (i));

            return map;
        }();

        const auto found = byId.find (id);
        return found == byId.end() ? -1 : found->second;
    }

    //==========================================================================
    double toNormalised (ParamSkew skew, double minimum, double maximum, double value) noexcept
    {
        const auto v = std::clamp (value, minimum, maximum);

        if (skew == ParamSkew::logarithmic)
            return std::clamp (std::log (v / minimum) / std::log (maximum / minimum), 0.0, 1.0);

        return (v - minimum) / (maximum - minimum);
    }

    double fromNormalised (ParamSkew skew, double minimum, double maximum, double normalised) noexcept
    {
        const auto p = std::clamp (normalised, 0.0, 1.0);

        if (skew == ParamSkew::logarithmic)
            return std::clamp (minimum * std::pow (maximum / minimum, p), minimum, maximum);

        return minimum + p * (maximum - minimum);
    }

    double toNormalised (const ParamSpec& spec, double value) noexcept
    {
        return toNormalised (spec.skew, spec.minimum, spec.maximum, value);
    }

    double fromNormalised (const ParamSpec& spec, double normalised) noexcept
    {
        return fromNormalised (spec.skew, spec.minimum, spec.maximum, normalised);
    }

    double snapToRound (double value) noexcept
    {
        if (! std::isfinite (value) || exactlyEqual (value, 0.0))
            return value;

        const auto magnitude = std::floor (std::log10 (std::abs (value)));
        const auto scale = std::pow (10.0, 5.0 - magnitude);
        const auto round = std::round (value * scale) / scale;

        return std::abs (round - value) <= 1.0e-6 * std::abs (value) ? round : value;
    }

    //==========================================================================
    int lookaheadSamples (int choice, double sampleRate) noexcept
    {
        if (choice < 0 || choice >= numLookaheadChoices || ! (sampleRate > 0.0))
            return 0;

        return static_cast<int> (std::lround (lookaheadMilliseconds[choice] * 0.001 * sampleRate));
    }

    int maxLookaheadSamples (double sampleRate) noexcept
    {
        int longest = 0;

        for (int choice = 0; choice < numLookaheadChoices; ++choice)
            longest = std::max (longest, lookaheadSamples (choice, sampleRate));

        return longest;
    }
}
