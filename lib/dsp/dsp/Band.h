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

    PHASE 4: TRANSIENT. A band in Transient mode (a bell or a shelf) owns
    its transient shaper (dsp/Transient.h) beside the gain computer; the same
    detector, the same lanes, the same coefficientsAt (index, offsetDb): only
    the source of the offset differs (PRD, Gain modes). Range limits both.

    PHASE 5: THE FREQUENCY MODES. A band in Env or Track (any type) has
    lanes like a band with an offset, and each lane its own frequency
    (dsp/Env.h, dsp/Tracker.h), kept by the chain. At each grid point the
    chain makes a lane's g for the step's end here (warpG), from the lane's
    frequency at the end of the step before, and asks for each sample's
    coefficients through coefficientsWithG (index, g, offsetDb, gainScale):
    one control step late, so that the output still depends on nothing but
    the sample index. Low and High are the band's own (dsp/Frequency.h);
    Env's Sensitivity, Span and times are its EnvComputer's.

    THE SHAPE grows with the gain mode, the frequency mode, the key as it is
    in effect (the sidechain falls back to Self when the bus is absent or
    disabled), the detector filter and type, the directions and the link.
    Each goes through the identity like a type switch. What does not matter
    for a band is not compared, so it costs no fade: the detector and key of
    a band that reads no key (Static and Fixed), the detector of a band that
    does not use it (Track without a gain mode), a Follow detector's type, a
    Sidechain band's link, the direction of a band that is not Dynamic, Env's
    direction outside Env. Transient has no switch of its own: its amounts,
    floor and Time are continuous; nor have Low, High, Sensitivity and Span,
    which ramp. The key and the link are the chain's, given each grid point
    as the band's CONTEXT.

    Standard library only; no allocation anywhere in this class.
*/

#include "BandCoefficients.h"
#include "Detector.h"
#include "Dynamics.h"
#include "Env.h"
#include "Frequency.h"
#include "Gain.h"
#include "Math.h"
#include "Parameters.h"
#include "Smoother.h"
#include "Svf.h"
#include "Tan.h"
#include "Tracker.h"
#include "Transient.h"

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

        // Phase 4: the transient shaper, in the table's units.
        double transientAttackPercent = 0.0;
        double transientSustainPercent = 0.0;
        double transientTime = 1.0;
        double transientFloorDb = -60.0;

        // Phase 5: the frequency modes, in the table's units.
        FrequencyMode fmode = FrequencyMode::fixed;
        double lowHz = 500.0;
        double highHz = 2000.0;
        double speedMs = 50.0;
        EnvDirection envDirection = EnvDirection::up;
        double sensitivityDb = -40.0;
        double spanDb = 30.0;
        double envAttackMs = 5.0;
        double envReleaseMs = 120.0;
        double confidencePercent = 30.0;
        double holdMs = 200.0;
        bool confidenceScalesGain = false;
    };

    /** What the chain tells every band at each grid point. */
    struct BandContext
    {
        int sidechainChannels = 0;      // 0: the bus is absent or disabled
        bool linked = true;
        std::uint64_t excluded = 0;     // channels left out of the link, among those prepared
        std::uint64_t leftOut = 0;      // channels this band does not apply to, among those prepared
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
            GainMode gmode = GainMode::staticGain;   // in effect: Static for a type without a gain
            FrequencyMode fmode = FrequencyMode::fixed;
            EnvDirection fdir = EnvDirection::up;
            KeySource key = KeySource::self;
            DetectorFilter dfilt = DetectorFilter::follow;
            DetectorType dtype = DetectorType::bandPass;
            Direction direction = Direction::above;
            bool linked = true;
            std::uint64_t excluded = 0;

            /** The channels the band does not apply to (PRD, Band model,
                Channels): not filtered, not read as a key, not in a lane. It
                matters for every band, a static one included. */
            std::uint64_t leftOut = 0;

            /** Dynamic or Transient: a detector, lanes and an offset. */
            bool hasOffset() const noexcept     { return gmode != GainMode::staticGain; }

            /** Env or Track: lanes, and a frequency per lane. */
            bool hasFrequencyMode() const noexcept  { return fmode != FrequencyMode::fixed; }

            /** Lanes on a key: an offset, a moving frequency, or both. */
            bool readsKey() const noexcept      { return hasOffset() || hasFrequencyMode(); }

            /** The band's detector gives a level that something uses. Track
                reads the raw key, not the detector. */
            bool usesDetector() const noexcept  { return hasOffset() || fmode == FrequencyMode::env; }

            /** The same effect: what does not matter for this band is not compared. */
            bool operator== (const Shape& o) const noexcept
            {
                if (! (on == o.on && type == o.type && qmode == o.qmode && gmode == o.gmode && fmode == o.fmode
                       && leftOut == o.leftOut))
                    return false;

                if (! readsKey())
                    return true;

                if (key != o.key)
                    return false;

                if (usesDetector() && ! (dfilt == o.dfilt && dtype == o.dtype))
                    return false;

                if (gmode == GainMode::dynamic && direction != o.direction)
                    return false;

                if (fmode == FrequencyMode::env && fdir != o.fdir)
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
            transient.prepare (sampleRate, smoothing);
            range.setLength (smoothing);
            env.prepare (sampleRate, smoothing);
            track.prepare (sampleRate);
            confidenceWeight.setLength (smoothing);
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
                transient.finish();
                range.finish();
                env.finish();
                confidenceWeight.finish();
                cw0 = cw1 = confidenceWeight.getCurrent();
                active = target;
                ++shapeRevision;
                fade.jumpTo (active.on ? 1.0 : 0.0);
                startOver();
                track.step (range, lastLogFrequency);
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
            transient.step();
            range.step();
            env.step();
            track.step (range, lastLogFrequency);
            cw0 = cw1;
            cw1 = confidenceWeight.step();
            updateDetector();
        }

        /** The transient shaper's four constants, instance-wide (dsp/Transient.h).
            Takes effect at the next grid point. */
        void setTransientTimes (const TransientTimes& times) noexcept
        {
            transient.setTimes (times);
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

        /** The coefficients for sample `index` of a lane whose own g (from
            warpG, interpolated by the caller) is `g`, with a dynamic offset in
            dB (0 allowed) and a gain scale in [0, 1] (Track's confidence, 1
            otherwise; PRD: it scales the offset, and the static gain where it
            is a boost). The offset within the Range, the sum within +-30 dB. */
        SvfCoefficients<double> coefficientsWithG (int index, double g, double offsetDb, double gainScale) const noexcept
        {
            const auto t = static_cast<double> (index + 1) / controlStep;

            const auto kq = kqMoving ? lerpExact (kq0, kq1, t) : kq1;
            const auto w = wMoving ? lerpExact (w0, w1, t) : w1;

            auto a = a1;
            auto sqrtA = sqrtA1;

            if (hasGain (active.type) && (dbMoving || ! exactlyEqual (offsetDb, 0.0) || ! exactlyEqual (gainScale, 1.0)))
            {
                const auto db = dbMoving ? lerpExact (db0, db1, t) : db1;
                const auto scaled = db > 0.0 ? db * gainScale : db;
                const auto limit = std::abs (dynamics.rangeAt (t));
                const auto effective = std::clamp (scaled + std::clamp (offsetDb, -limit, limit) * gainScale, -maxGainDb, maxGainDb);
                a = bandA (effective);
                sqrtA = isShelf (active.type) ? bandSqrtA (effective) : 1.0;
            }

            return withFade (designBand (active.type, active.qmode, g, kq, a, sqrtA), w);
        }

        /** g = tan(pi f / fs) for a log2 frequency, clamped as the band's own. */
        double warpG (double logHz) const noexcept          { return warp.g (std::exp2 (logHz)); }

        /** The band's own g at the step's start and end. */
        double gStart() const noexcept                      { return g0; }
        double gEnd() const noexcept                        { return g1; }

        /** The band's own Frequency now, in log2 Hz. */
        double ownLogFrequency() const noexcept             { return lastLogFrequency; }

        /** A Follow detector's coefficients at a lane's own g and frequency:
            a band-pass at the band's unscaled 1/Q (dsp/Detector.h). */
        DetectorCoefficients followDetectorAt (double g, double logHz) const noexcept
        {
            return makeDetectorCoefficients (DetectorType::bandPass, g, kq1, warp.clamp (std::exp2 (logHz)), rate);
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

        /** In Dynamic or Transient mode with a gain to move: a detector, lanes, an offset. */
        bool hasOffset() const noexcept                     { return active.hasOffset(); }

        /** The gain mode in effect: Static for a type without a gain. */
        GainMode gainMode() const noexcept                  { return active.gmode; }

        /** The frequency mode in effect. */
        FrequencyMode frequencyMode() const noexcept        { return active.fmode; }

        /** Env or Track in effect: a frequency per lane. */
        bool hasFrequencyMode() const noexcept              { return active.hasFrequencyMode(); }

        /** Lanes on a key, in effect, and not idle: the chain reads its key. */
        bool readsKey() const noexcept                      { return active.readsKey() && ! idle; }

        /** The band's detector level is used, in effect. */
        bool usesDetector() const noexcept                  { return active.usesDetector(); }

        /** Low and High, on the grid (dsp/Frequency.h). */
        const FrequencyRange& frequencyRange() const noexcept   { return range; }

        /** Env's follower, on the grid (dsp/Env.h). */
        const EnvComputer& envComputer() const noexcept     { return env; }

        /** Track's trackers, on the grid (dsp/Tracker.h). */
        const TrackerComputer& trackerComputer() const noexcept     { return track; }

        /** Track: how much confidence scales the gain (PRD: fcgain), 0 to 1 at
            `t` of the step; it ramps, so turning fcgain on or off is not a
            step in the gain. The scale is then 1 - weight (1 - confidence). */
        double confidenceWeightAt (double t) const noexcept
        {
            return exactlyEqual (cw0, cw1) ? cw1 : lerpExact (cw0, cw1, t);
        }

        /** In this step, confidence scales the gain of a Track band with a gain. */
        bool confidenceScales() const noexcept
        {
            return active.fmode == FrequencyMode::track && hasGain (active.type)
                && ! (exactlyEqual (cw0, 0.0) && exactlyEqual (cw1, 0.0));
        }

        /** Env: the log2 frequency a follower's level maps to at `t` of the step. */
        double envLogFrequency (double levelDb, double t) const noexcept
        {
            const auto u = env.positionAt (levelDb, t);
            const auto [low, high] = range.at (t);
            return lerpExact (low, high, active.fdir == EnvDirection::up ? u : 1.0 - u);
        }

        /** Env at rest, the start of its mapping, at the step's end: Low for
            Up, High for Down (decided by the author 2026-11-20). */
        double envRestLogFrequency() const noexcept
        {
            const auto [low, high] = range.atEnd();
            return active.fdir == EnvDirection::up ? low : high;
        }

        /** The detector the band runs: the one in effect for a band with an
            offset, which changes only through the identity; for any other
            band, the one asked for, which only Listen hears. */
        DetectorConfig detectorConfig() const noexcept
        {
            const auto& s = active.usesDetector() ? active : target;
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

        /** Transient: `n` samples of one lane's offsets from its powers, from
            sample `index` of the step. True when any of them is not 0. */
        bool transientOffsets (const float* power, int n, int index, TransientEnvelopes& envelopes,
                               double* offsets) const noexcept
        {
            return transient.run (power, n, index, controlStep, dynamics.rangeStart(), dynamics.rangeEnd(),
                                  envelopes, offsets);
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
            gain is Static (PRD, non-goals). */
        static Shape shapeOf (const BandSettings& s, const BandContext& c) noexcept
        {
            Shape shape;
            shape.on = s.on;
            shape.type = s.type;
            shape.qmode = s.type == BandType::bell ? s.qmode : QMode::proportional;
            shape.gmode = hasGain (s.type) ? s.gmode : GainMode::staticGain;
            shape.fmode = s.fmode;
            shape.fdir = s.envDirection;
            shape.key = s.key == KeySource::sidechain && c.sidechainChannels > 0 ? KeySource::sidechain : KeySource::self;
            shape.dfilt = s.dfilt;
            shape.dtype = s.dfilt == DetectorFilter::own ? s.dtype : DetectorType::bandPass;
            shape.direction = s.direction;
            shape.linked = c.linked;
            shape.excluded = c.excluded;
            shape.leftOut = c.leftOut;
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
            transient.setTarget (pending.transientAttackPercent, pending.transientSustainPercent,
                                 pending.transientTime, pending.transientFloorDb);
            range.setTarget (pending.lowHz, pending.highHz);
            env.setTarget (pending.sensitivityDb, pending.spanDb, pending.envAttackMs, pending.envReleaseMs);
            track.setTarget (pending.speedMs, pending.confidencePercent, pending.holdMs);
            confidenceWeight.setTarget (pending.confidenceScalesGain ? 1.0 : 0.0);

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
        TransientComputer transient;
        FrequencyRange range;
        EnvComputer env;
        TrackerComputer track;
        Ramp confidenceWeight;
        double cw0 = 0.0, cw1 = 0.0;
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
