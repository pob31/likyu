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

    PHASE 3: THE DYNAMIC OFFSET. A band in Dynamic mode (a bell or a shelf)
    owns its gain computer (dsp/Dynamics.h) and works out its detector's
    coefficients (dsp/Detector.h) at each grid point; the chain owns the
    detector states and the offsets, per lane (dsp/Chain.h), and asks for
    the coefficients of a sample with an offset through
    coefficientsAt (index, offsetDb). With an offset of exactly 0 the chain
    asks for the Phase 2 coefficients themselves, so a dynamic band at rest
    is bit for bit the static band it would be.

    THE SHAPE grows with the gain mode, the key as it is in effect (the
    sidechain falls back to Self when the bus is absent or disabled), the
    detector filter and type, the direction and the link. Each goes through
    the identity like a type switch. What does not matter for a band is not
    compared, so it costs no fade: a Static band's detector and key, a
    Follow detector's type, a Sidechain band's link. The key and the link
    are the chain's, given each grid point as the band's CONTEXT.

    Standard library only; no allocation anywhere in this class.
*/

#include "BandCoefficients.h"
#include "Detector.h"
#include "Dynamics.h"
#include "Gain.h"
#include "Math.h"
#include "Parameters.h"
#include "Smoother.h"
#include "Svf.h"
#include "Tan.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

        // Phase 3: the detector and the gain computer.
        GainMode gmode = GainMode::staticGain;
        KeySource key = KeySource::self;
        DetectorFilter dfilt = DetectorFilter::follow;
        DetectorType dtype = DetectorType::bandPass;
        double detectorFrequency = 1000.0;
        double detectorQ = 1.0;
        double rangeDb = -6.0;
        double thresholdDb = -24.0;
        double ratio = 3.0;
        double kneeDb = 6.0;
        double attackMs = 5.0;
        double releaseMs = 120.0;
        Direction direction = Direction::above;
    };

    /** What the chain tells every band at each grid point. */
    struct BandContext
    {
        int sidechainChannels = 0;      // 0: the bus is absent or disabled
        bool linked = true;
        std::uint64_t excluded = 0;     // channels left out of the link, among those prepared
    };

    class Band
    {
    public:
        /** What the band is, for the identity fade: see THE SHAPE above. */
        struct Shape
        {
            bool on = false;
            BandType type = BandType::bell;
            QMode qmode = QMode::proportional;
            bool dynamic = false;
            KeySource key = KeySource::self;
            DetectorFilter dfilt = DetectorFilter::follow;
            DetectorType dtype = DetectorType::bandPass;
            Direction direction = Direction::above;
            bool linked = true;
            std::uint64_t excluded = 0;

            /** The same effect: what does not matter for this band is not compared. */
            bool operator== (const Shape& o) const noexcept
            {
                if (! (on == o.on && type == o.type && qmode == o.qmode && dynamic == o.dynamic))
                    return false;

                if (! dynamic)
                    return true;

                if (! (key == o.key && dfilt == o.dfilt && dtype == o.dtype && direction == o.direction))
                    return false;

                if (key == KeySource::sidechain)
                    return true;

                return linked == o.linked && (! linked || excluded == o.excluded);
            }
        };

        /** Which detector the band runs: its key in effect, its filter and type. */
        struct DetectorConfig
        {
            KeySource key = KeySource::self;
            DetectorFilter dfilt = DetectorFilter::follow;
            DetectorType dtype = DetectorType::bandPass;

            bool operator== (const DetectorConfig&) const noexcept = default;
        };

        /** Ramp lengths and the warp for this rate; the next step snaps. */
        void prepare (double sampleRate) noexcept
        {
            warp.prepare (sampleRate);
            rate = sampleRate;

            const auto smoothing = rampSteps (smoothingSeconds, sampleRate, controlStep);
            logFrequency.setLength (smoothing);
            logQ.setLength (smoothing);
            gain.setLength (smoothing);
            fade.setLength (rampSteps (switchFadeSeconds, sampleRate, controlStep));
            detectorLogFrequency.setLength (smoothing);
            detectorLogQ.setLength (smoothing);
            dynamics.prepare (sampleRate, smoothing);
            detectorLf = notANumber;
            detectorStale = true;

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

        /** Before each beginStep(): the key and the link, from the chain. */
        void setContext (const BandContext& c) noexcept
        {
            context = c;
        }

        /** At a grid point: one control step. */
        void beginStep() noexcept
        {
            if (hasPending)
                latch();

            // The context may have changed without any setting doing so.
            target = shapeOf (latched, context);

            if (snapPending)
            {
                snapPending = false;
                logFrequency.finish();
                logQ.finish();
                gain.finish();
                detectorLogFrequency.finish();
                detectorLogQ.finish();
                dynamics.finish();
                active = target;
                ++shapeRevision;
                fade.jumpTo (active.on ? 1.0 : 0.0);
                startOver();
                updateDetector();
                return;
            }

            auto shapeChanged = false;

            if (! (target == active))
            {
                if (! fade.isMoving() && exactlyEqual (fade.getCurrent(), 0.0))
                {
                    active = target;
                    ++shapeRevision;
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

            detectorLogFrequency.step();
            detectorLogQ.step();
            dynamics.step();
            updateDetector();
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

        /** The coefficients for sample `index` with a dynamic offset in dB,
            which is not 0: the effective gain is the static gain plus the
            offset, the offset within the Range and the sum within +-30 dB
            (PRD, decided 2026-11-16). For a band whose type has a gain. */
        SvfCoefficients<double> coefficientsAt (int index, double offsetDb) const noexcept
        {
            const auto t = static_cast<double> (index + 1) / controlStep;

            const auto g = gMoving ? lerpExact (g0, g1, t) : g1;
            const auto kq = kqMoving ? lerpExact (kq0, kq1, t) : kq1;
            const auto w = wMoving ? lerpExact (w0, w1, t) : w1;
            const auto db = dbMoving ? lerpExact (db0, db1, t) : db1;

            const auto effective = effectiveGainDb (db, offsetDb, dynamics.rangeAt (t));
            const auto a = bandA (effective);
            const auto sqrtA = isShelf (active.type) ? bandSqrtA (effective) : 1.0;

            return withFade (designBand (active.type, active.qmode, g, kq, a, sqrtA), w);
        }

        /** The coefficients of samples `from` to `from + n - 1` of the step,
            into `out`: one loop here, so that the per-sample work is inlined
            whatever the caller looks like. */
        void coefficientsFor (int from, int n, SvfCoefficients<double>* out) const noexcept
        {
            for (int i = 0; i < n; ++i)
                out[i] = coefficientsAt (from + i);
        }

        //======================================================================
        /** The shape in effect, which the chain's lanes are built for. */
        const Shape& activeShape() const noexcept           { return active; }

        /** Goes up each time the shape in effect changes, a snap included. */
        int getShapeRevision() const noexcept               { return shapeRevision; }

        /** In Dynamic mode with a gain to move. */
        bool isDynamic() const noexcept                     { return active.dynamic; }

        /** The detector the band runs: the one in effect for a dynamic band,
            which changes only through the identity; for any other band, the
            one asked for, which only Listen hears. */
        DetectorConfig detectorConfig() const noexcept
        {
            const auto& s = active.dynamic ? active : target;
            return { s.key, s.dfilt, s.dtype };
        }

        /** Goes up each time detectorConfig() changes: the states it ran are then stale. */
        int getDetectorRevision() const noexcept            { return detectorRevision; }

        /** For this step. Made when first asked for in a step, and only then:
            a band whose detector does not run costs nothing for it. */
        const DetectorCoefficients& detectorCoefficients() const noexcept
        {
            if (detectorStale)
                makeDetector();

            return detector;
        }

        /** The gain computer's target for a level, at sample `index` of the step. */
        double dynamicTargetAt (double levelDb, int index) const noexcept
        {
            return dynamics.targetAt (levelDb, static_cast<double> (index + 1) / controlStep, active.direction);
        }

        /** One sample of the offset's one-pole. */
        void smoothOffset (double& offset, double targetDb) const noexcept
        {
            dynamics.smooth (offset, targetDb);
        }

        /** The sidechain was asked for and the bus is absent or disabled:
            the band runs on Self, and the editor says so (PRD, Band model). */
        bool keyFellBack() const noexcept
        {
            return latched.key == KeySource::sidechain && context.sidechainChannels <= 0;
        }

        /** The band's frequency now, in Hz. */
        double frequencyHz() const noexcept                 { return std::exp2 (lastLogFrequency); }

    private:
        static constexpr bool isShelf (BandType type) noexcept
        {
            return type == BandType::lowShelf || type == BandType::highShelf;
        }

        /** What the settings and the context make the band. Q mode is a
            bell's only; a Follow detector is a band-pass; a band without a
            gain, or in Transient until Phase 4, is Static. */
        static Shape shapeOf (const BandSettings& s, const BandContext& c) noexcept
        {
            Shape shape;
            shape.on = s.on;
            shape.type = s.type;
            shape.qmode = s.type == BandType::bell ? s.qmode : QMode::proportional;
            shape.dynamic = hasGain (s.type) && s.gmode == GainMode::dynamic;
            shape.key = s.key == KeySource::sidechain && c.sidechainChannels > 0 ? KeySource::sidechain : KeySource::self;
            shape.dfilt = s.dfilt;
            shape.dtype = s.dfilt == DetectorFilter::own ? s.dtype : DetectorType::bandPass;
            shape.direction = s.direction;
            shape.linked = c.linked;
            shape.excluded = c.excluded;
            return shape;
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
            latched = pending;

            if (std::isfinite (pending.detectorFrequency))
                detectorLogFrequency.setTarget (std::log2 (std::clamp (pending.detectorFrequency, minFrequency, maxFrequency)));

            if (std::isfinite (pending.detectorQ))
                detectorLogQ.setTarget (std::log2 (std::clamp (pending.detectorQ, minQ, maxQ)));

            dynamics.setTarget (pending.thresholdDb, pending.kneeDb, pending.ratio, pending.rangeDb,
                                pending.attackMs, pending.releaseMs);

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

        /** At each grid point: which detector, and that its coefficients are to be made again. */
        void updateDetector() noexcept
        {
            const auto config = detectorConfig();

            if (! (config == lastDetectorConfig))
            {
                lastDetectorConfig = config;
                ++detectorRevision;
            }

            detectorStale = true;
        }

        /** The detector's coefficients for this step, from the band's own g
            and 1/Q (Follow, unscaled) or from its detector's ramps (Own);
            made again only when what they are made from has moved. */
        void makeDetector() const noexcept
        {
            detectorStale = false;

            const auto own = lastDetectorConfig.dfilt == DetectorFilter::own;
            const auto type = own ? lastDetectorConfig.dtype : DetectorType::bandPass;
            const auto lf = own ? detectorLogFrequency.getCurrent() : lastLogFrequency;
            const auto lq = own ? detectorLogQ.getCurrent() : lastLogQ;

            if (exactlyEqual (lf, detectorLf) && exactlyEqual (lq, detectorLq) && type == detector.type)
                return;

            const auto hz = warp.clamp (std::exp2 (lf));
            const auto kq = own ? std::exp2 (-lq) : kq1;
            detectorLf = lf;
            detectorLq = lq;
            detector = makeDetectorCoefficients (type, own ? warp.g (hz) : g1, kq, hz, rate);
        }

        static constexpr double notANumber = std::numeric_limits<double>::quiet_NaN();

        Warp warp;
        Ramp logFrequency, logQ, gain, fade;
        Ramp detectorLogFrequency, detectorLogQ;
        GainComputer dynamics;
        double rate = 0.0;

        BandSettings pending, latched;
        bool hasPending = false;
        bool snapPending = true;

        BandContext context;
        Shape target, active;
        int shapeRevision = 0;

        DetectorConfig lastDetectorConfig;
        int detectorRevision = 0;

        // Made on demand (detectorCoefficients()), and what they were made from.
        mutable DetectorCoefficients detector;
        mutable double detectorLf = notANumber, detectorLq = notANumber;
        mutable bool detectorStale = true;

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
