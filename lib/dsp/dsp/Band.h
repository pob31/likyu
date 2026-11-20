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
    ONE BAND'S CONTROL STATE: what its coefficients are, sample by sample. The
    filter states, one pair per channel, live in the chain (dsp/Chain.h),
    which drives this one control step at a time.

    THE CONTROL GRID (PRD, Filter core, update rates). A control step is 16
    samples on a grid that counts from prepare() and runs across process
    calls. At each grid point the band

      - takes the targets that arrived since the last one (so a change lands
        at the same sample however the host cut its blocks);
      - steps its ramps: frequency in log2 Hz, Q in log2, gain in dB, over
        20 ms (dsp/Smoother.h);
      - computes g = tan(pi f / fs) at the step's end (dsp/Tan.h).

    Inside a step, g, 1/Q and the gain in dB are interpolated linearly to the
    step's end, and A, the scaling of g and k, a1 to a3 and the mix terms
    follow every sample. A step in which nothing moves has one set of
    coefficients, computed once and kept.

    STRUCTURAL CHANGES GO THROUGH THE IDENTITY (PRD, Band model: mode, type
    and on/off switches are click-free, a short crossfade of the mix terms).
    On/off, type and Q mode (a bell's only) do not ramp: the mix fades to
    (1, 0, 0) over 10 ms, the change is made there, where it cannot be heard,
    and the mix fades back in, both along a smoothstep, which has no corner
    to spray above the signal. The filter state carries across. A change
    arriving mid-fade turns the fade round. A band that is off and faded out
    is IDLE: the chain skips it and clears its state.

    Values arriving here are in the table's real units (dsp/Parameters.h)
    and are clamped to its ranges; the gain to the effective-gain limit.

    Standard library only; no allocation anywhere in this class.
*/

#include "BandCoefficients.h"
#include "Gain.h"
#include "Math.h"
#include "Parameters.h"
#include "Smoother.h"
#include "Svf.h"
#include "Tan.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace likyu::dsp
{
    /** Samples per control step: the 16-sample sub-block of the PRD. */
    inline constexpr int controlStep = 16;

    /** Parameter smoothing (PRD: about 20 ms). */
    inline constexpr double smoothingSeconds = 0.020;

    /** Each half of a structural switch, and of the bypass crossfade (proposed,
        to be confirmed by ear). */
    inline constexpr double switchFadeSeconds = 0.010;

    /** What a band is asked to be, in real units. */
    struct BandSettings
    {
        bool on = false;
        BandType type = BandType::bell;
        QMode qmode = QMode::proportional;
        double frequency = 1000.0;
        double q = 1.0;
        double gainDb = 0.0;
    };

    class Band
    {
    public:
        /** Ramp lengths and the warp for this rate; the next step snaps. */
        void prepare (double sampleRate) noexcept
        {
            warp.prepare (sampleRate);

            const auto smoothing = rampSteps (smoothingSeconds, sampleRate, controlStep);
            logFrequency.setLength (smoothing);
            logQ.setLength (smoothing);
            gain.setLength (smoothing);
            fade.setLength (rampSteps (switchFadeSeconds, sampleRate, controlStep));

            snapPending = true;
        }

        /** Takes effect at the next grid point. */
        void setTarget (const BandSettings& settings) noexcept
        {
            pending = settings;
            hasPending = true;
        }

        /** The next grid point jumps to the targets: no ramp, no fade. */
        void snap() noexcept
        {
            snapPending = true;
        }

        /** At a grid point: one control step. */
        void beginStep() noexcept
        {
            if (hasPending)
                latch();

            if (snapPending)
            {
                snapPending = false;
                logFrequency.finish();
                logQ.finish();
                gain.finish();
                active = target;
                fade.jumpTo (active.on ? 1.0 : 0.0);
                startOver();
                return;
            }

            auto shapeChanged = false;

            if (! (target == active))
            {
                if (! fade.isMoving() && exactlyEqual (fade.getCurrent(), 0.0))
                {
                    active = target;
                    shapeChanged = true;
                }
                else
                {
                    fade.setTarget (0.0);
                }
            }

            if (target == active)
                fade.setTarget (active.on ? 1.0 : 0.0);

            g0 = g1;
            kq0 = kq1;
            db0 = db1;
            w0 = w1;

            stepRamps (shapeChanged);
            settle (shapeChanged);
        }

        /** Off and faded out: nothing to compute, the state may be cleared. */
        bool isIdle() const noexcept        { return idle; }

        /** One set of coefficients for the whole step. */
        bool isConstant() const noexcept    { return constant; }

        const SvfCoefficients<double>& constantCoefficients() const noexcept { return fixed; }

        /** The coefficients for sample `index` (0 to 15) of the current step. */
        SvfCoefficients<double> coefficientsAt (int index) const noexcept
        {
            const auto t = static_cast<double> (index + 1) / controlStep;

            const auto g = gMoving ? lerpExact (g0, g1, t) : g1;
            const auto kq = kqMoving ? lerpExact (kq0, kq1, t) : kq1;
            const auto w = wMoving ? lerpExact (w0, w1, t) : w1;

            auto a = a1;
            auto sqrtA = sqrtA1;

            if (dbMoving)
            {
                const auto db = lerpExact (db0, db1, t);
                a = bandA (db);
                sqrtA = isShelf (active.type) ? bandSqrtA (db) : 1.0;
            }

            return withFade (designBand (active.type, active.qmode, g, kq, a, sqrtA), w);
        }

    private:
        struct Shape
        {
            bool on = false;
            BandType type = BandType::bell;
            QMode qmode = QMode::proportional;

            bool operator== (const Shape&) const noexcept = default;
        };

        static constexpr bool isShelf (BandType type) noexcept
        {
            return type == BandType::lowShelf || type == BandType::highShelf;
        }

        /** Q mode is a bell's only: for another type it is not a change. */
        static Shape shapeOf (const BandSettings& s) noexcept
        {
            return { s.on, s.type, s.type == BandType::bell ? s.qmode : QMode::proportional };
        }

        /** The mix crossfaded with the identity: `position` 0 is (1, 0, 0),
            1 is the band's own mix, and the curve between is a smoothstep. */
        static SvfCoefficients<double> withFade (SvfCoefficients<double> c, double position) noexcept
        {
            if (exactlyEqual (position, 1.0))
                return c;

            const auto w = smoothstep (position);
            c.m0 = (1.0 - w) + w * c.m0;
            c.m1 *= w;
            c.m2 *= w;
            return c;
        }

        void latch() noexcept
        {
            hasPending = false;
            target = shapeOf (pending);

            if (std::isfinite (pending.frequency))
                logFrequency.setTarget (std::log2 (std::clamp (pending.frequency, minFrequency, maxFrequency)));

            if (std::isfinite (pending.q))
                logQ.setTarget (std::log2 (std::clamp (pending.q, minQ, maxQ)));

            if (std::isfinite (pending.gainDb))
                gain.setTarget (effectiveGainDb (pending.gainDb, 0.0, 0.0));
        }

        /** After a snap: every value at its target, nothing moving. */
        void startOver() noexcept
        {
            lastLogFrequency = logFrequency.getCurrent();
            lastLogQ = logQ.getCurrent();
            g1 = warp.g (std::exp2 (lastLogFrequency));
            kq1 = std::exp2 (-lastLogQ);
            db1 = gain.getCurrent();
            w1 = fade.getCurrent();
            updateAmplitude();

            g0 = g1;
            kq0 = kq1;
            db0 = db1;
            w0 = w1;

            settle (true);
        }

        void stepRamps (bool shapeChanged) noexcept
        {
            const auto lf = logFrequency.step();

            if (! exactlyEqual (lf, lastLogFrequency))
            {
                lastLogFrequency = lf;
                g1 = warp.g (std::exp2 (lf));
            }

            const auto lq = logQ.step();

            if (! exactlyEqual (lq, lastLogQ))
            {
                lastLogQ = lq;
                kq1 = std::exp2 (-lq);
            }

            const auto db = gain.step();

            if (! exactlyEqual (db, db1) || shapeChanged)
            {
                db1 = db;
                updateAmplitude();
            }

            w1 = fade.step();
        }

        void updateAmplitude() noexcept
        {
            a1 = hasGain (active.type) ? bandA (db1) : 1.0;
            sqrtA1 = isShelf (active.type) ? bandSqrtA (db1) : 1.0;
        }

        void settle (bool shapeChanged) noexcept
        {
            gMoving = ! exactlyEqual (g0, g1);
            kqMoving = ! exactlyEqual (kq0, kq1);
            dbMoving = hasGain (active.type) && ! exactlyEqual (db0, db1);
            wMoving = ! exactlyEqual (w0, w1);
            constant = ! (gMoving || kqMoving || dbMoving || wMoving);

            if (constant && (shapeChanged || ! (fixedShape == active) || ! exactlyEqual (fixedG, g1)
                             || ! exactlyEqual (fixedKq, kq1) || ! exactlyEqual (fixedA, a1) || ! exactlyEqual (fixedW, w1)))
            {
                fixed = withFade (designBand (active.type, active.qmode, g1, kq1, a1, sqrtA1), w1);
                fixedShape = active;
                fixedG = g1;
                fixedKq = kq1;
                fixedA = a1;
                fixedW = w1;
            }

            idle = ! active.on && ! fade.isMoving() && exactlyEqual (w1, 0.0);
        }

        static constexpr double notANumber = std::numeric_limits<double>::quiet_NaN();

        Warp warp;
        Ramp logFrequency, logQ, gain, fade;

        BandSettings pending;
        bool hasPending = false;
        bool snapPending = true;

        Shape target, active;

        // The step's start (0) and end (1) values.
        double g0 = 0.0, g1 = 0.0;
        double kq0 = 1.0, kq1 = 1.0;
        double db0 = 0.0, db1 = 0.0;
        double w0 = 0.0, w1 = 0.0;
        double a1 = 1.0, sqrtA1 = 1.0;
        double lastLogFrequency = notANumber, lastLogQ = notANumber;

        bool gMoving = false, kqMoving = false, dbMoving = false, wMoving = false;
        bool constant = true;
        bool idle = true;

        // The constant coefficients, and what they were made from: a change
        // of any of these, the shape included, makes them again.
        SvfCoefficients<double> fixed {};
        Shape fixedShape;
        double fixedG = notANumber, fixedKq = notANumber, fixedA = notANumber, fixedW = notANumber;
    };
}
