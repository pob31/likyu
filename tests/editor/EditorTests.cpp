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
    The editor itself, headless (devplan 6b): built on the real processor,
    rendered to an image with no window and no message loop, and driven by
    mouse events made by hand.

    What a node says and where a press lands are held by PresentationTests
    and CurveGeometryTests; here it is that the components pass them on to
    the parameters: a drag moves what its part of the handle moves, by what
    the geometry says, inside one gesture.

    TO LOOK AT THE PICTURE without a host: run with LIKYU_SNAPSHOT_DIR set
    to a folder, and the test writes likyu-editor.png there.
*/

#include <doctest/doctest.h>

#include <EditorWindow.h>
#include <PluginProcessor.h>

#include <editor/CurveGeometry.h>
#include <editor/LongPressButton.h>
#include <editor/Palette.h>
#include <editor/QGestures.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

using namespace likyu;
using P = dsp::BandParam;

namespace
{
    constexpr double rate = 48000.0;
    constexpr int blockSize = 256;

    void set (LikyuProcessor& processor, int band, P param, double value)
    {
        processor.setValue (dsp::indexOf (band, param), value);
    }

    double get (const LikyuProcessor& processor, int band, P param)
    {
        return processor.valueOf (dsp::indexOf (band, param));
    }

    /** A tone on both channels, through the processor, block by block. */
    void run (LikyuProcessor& processor, double hz, double amplitude, int blocks)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        auto phase = 0.0;

        for (int block = 0; block < blocks; ++block)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto sample = static_cast<float> (amplitude * std::sin (phase));
                phase += juce::MathConstants<double>::twoPi * hz / rate;
                buffer.setSample (0, i, sample);
                buffer.setSample (1, i, sample);
            }

            processor.processBlock (buffer, midi);
        }
    }

    /** Counts the gestures a host would see, per parameter. */
    struct GestureCounter final : juce::AudioProcessorParameter::Listener
    {
        explicit GestureCounter (LikyuProcessor& p) : processor (p)
        {
            for (int i = 0; i < dsp::numParams; ++i)
                processor.parameterAt (i).addListener (this);
        }

        ~GestureCounter() override
        {
            for (int i = 0; i < dsp::numParams; ++i)
                processor.parameterAt (i).removeListener (this);
        }

        void parameterValueChanged (int, float) override {}

        void parameterGestureChanged (int parameterIndex, bool starting) override
        {
            (starting ? begun : ended)[parameterIndex] += 1;
            open += starting ? 1 : -1;
        }

        int begunFor (int band, P param)    { return begun[dsp::indexOf (band, param)]; }
        int endedFor (int band, P param)    { return ended[dsp::indexOf (band, param)]; }

        LikyuProcessor& processor;
        std::map<int, int> begun, ended;
        int open = 0;
    };

    juce::MouseEvent eventAt (juce::Component& component, juce::Point<float> position, juce::Point<float> pressedAt,
                              juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier, int clicks = 1)
    {
        const auto now = juce::Time::getCurrentTime();

        return { juce::Desktop::getInstance().getMainMouseSource(), position, mods,
                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 &component, &component, now, pressedAt, now, clicks, position != pressedAt };
    }

    /** A press, a move and a release on the curve. */
    void drag (editor::CurveView& curve, juce::Point<float> from, juce::Point<float> to,
               juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
    {
        curve.mouseDown (eventAt (curve, from, from, mods));
        curve.mouseDrag (eventAt (curve, to, from, mods));
        curve.mouseUp (eventAt (curve, to, from, mods));
    }

    struct Opened
    {
        Opened()
        {
            processor.setRateAndBufferSizeDetails (rate, blockSize);
            processor.prepareToPlay (rate, blockSize);
            window.reset (dynamic_cast<EditorWindow*> (processor.createEditor()));
        }

        ~Opened()
        {
            window.reset();
            processor.releaseResources();
        }

        editor::CurveView& curve()              { return window->editorRoot().curveView(); }
        void refresh()                          { window->editorRoot().refreshNow(); }

        editor::geometry::Plot plot()
        {
            return editor::geometry::plotWithin (curve().getWidth(), curve().getHeight());
        }

        /** Where a band's node is: at its parameters, which is where a Fixed band is drawn. */
        juce::Point<float> nodeOf (int band)
        {
            const auto p = plot();
            return { static_cast<float> (editor::geometry::xOfHz (p, get (processor, band, P::freq))),
                     static_cast<float> (editor::geometry::yOfDb (p, get (processor, band, P::gain))) };
        }

        LikyuProcessor processor;
        std::unique_ptr<EditorWindow> window;
    };
}

//==============================================================================
TEST_CASE ("editor: the processor's editor is the window, at its default size, the curve on top")
{
    Opened opened;
    REQUIRE (opened.window != nullptr);

    CHECK (opened.processor.hasEditor());
    CHECK (opened.window->getWidth() == editor::EditorRoot::defaultWidth);
    CHECK (opened.window->getHeight() == editor::EditorRoot::defaultHeight);
    CHECK (opened.window->isResizable());
    CHECK (opened.window->usesWindowsMultiTouch());

    // The curve has the size the geometry's tests hold the hit areas at.
    CHECK (opened.curve().getWidth() == static_cast<int> (editor::geometry::defaultCurveWidth));
    CHECK (opened.curve().getHeight() == static_cast<int> (editor::geometry::defaultCurveHeight));

    // Smaller, larger: it lays out and paints.
    for (const auto size : { juce::Point<int> (editor::EditorRoot::minimumWidth, editor::EditorRoot::minimumHeight),
                             juce::Point<int> (1600, 1000) })
    {
        opened.window->setSize (size.x, size.y);
        const auto image = opened.window->createComponentSnapshot (opened.window->getLocalBounds());
        CHECK (image.getWidth() == size.x);
        CHECK (opened.curve().getWidth() == size.x);
    }
}

TEST_CASE ("editor: opened and closed twice on one processor, before and after it is prepared")
{
    LikyuProcessor processor;

    // A host may open the editor before it prepares the plugin: no rate, no meters.
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        REQUIRE (editor != nullptr);
        CHECK (editor->createComponentSnapshot (editor->getLocalBounds()).isValid());
    }

    processor.setRateAndBufferSizeDetails (rate, blockSize);
    processor.prepareToPlay (rate, blockSize);
    run (processor, 1000.0, 0.25, 4);

    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        REQUIRE (editor != nullptr);
        CHECK (editor->createComponentSnapshot (editor->getLocalBounds()).isValid());
    }

    processor.releaseResources();
}

TEST_CASE ("editor: the picture, with a band of each kind")
{
    Opened opened;
    auto& processor = opened.processor;

    // Band 2: a static low shelf. Band 4: a dynamic bell that the tone cuts.
    // Band 5: a Track bell on the tone. Band 7: a low-pass.
    set (processor, 1, P::on, 1.0);
    set (processor, 1, P::type, static_cast<double> (dsp::BandType::lowShelf));
    set (processor, 1, P::gain, 5.0);

    set (processor, 3, P::on, 1.0);
    set (processor, 3, P::freq, 1000.0);
    set (processor, 3, P::gain, 6.0);
    set (processor, 3, P::q, 2.0);
    set (processor, 3, P::gmode, static_cast<double> (dsp::GainMode::dynamic));
    set (processor, 3, P::thresh, -40.0);
    set (processor, 3, P::range, -12.0);

    set (processor, 4, P::on, 1.0);
    set (processor, 4, P::freq, 2500.0);
    set (processor, 4, P::gain, -8.0);
    set (processor, 4, P::q, 4.0);
    set (processor, 4, P::fmode, static_cast<double> (dsp::FrequencyMode::track));
    set (processor, 4, P::flo, 500.0);
    set (processor, 4, P::fhi, 4000.0);

    set (processor, 6, P::on, 1.0);
    set (processor, 6, P::type, static_cast<double> (dsp::BandType::lowPass));
    set (processor, 6, P::freq, 9000.0);

    run (processor, 1000.0, 0.5, 400);
    opened.curve().selectBand (3);
    opened.refresh();

    // The meters reached the editor's side: the dynamic bell is cut, the tracker is on the tone.
    core::Meters meters;
    processor.meters().read (meters);
    CHECK (meters.bands[3].offsetDb < -3.0f);
    CHECK (meters.bands[4].frequencyHz == doctest::Approx (1000.0).epsilon (0.02));

    const auto image = opened.window->createComponentSnapshot (opened.window->getLocalBounds());
    REQUIRE (image.isValid());

    // Each band's colour is somewhere in the curve's part of the picture, and so is the live line's.
    const auto curveArea = opened.curve().getBounds();

    const auto has = [&image, curveArea] (juce::Colour colour)
    {
        for (int y = curveArea.getY(); y < curveArea.getBottom(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
                if (image.getPixelAt (x, y) == colour)
                    return true;

        return false;
    };

    for (const auto band : { 1, 3, 4, 6 })
    {
        INFO ("band " << band + 1);
        CHECK (has (editor::palette::band (band)));
    }

    CHECK (has (editor::palette::liveLine));
    CHECK_FALSE (has (editor::palette::band (0)));      // band 1 is off: drawn darker

    const auto folder = juce::SystemStats::getEnvironmentVariable ("LIKYU_SNAPSHOT_DIR", {});

    const auto write = [&folder, &opened] (const juce::String& name)
    {
        if (folder.isEmpty())
            return;

        const auto file = juce::File (folder).getChildFile (name);
        file.deleteFile();

        if (juce::FileOutputStream stream (file); stream.openedOk())
            juce::PNGImageFormat().writeImageToStream (opened.window->createComponentSnapshot (opened.window->getLocalBounds()), stream);

        MESSAGE ("wrote " << file.getFullPathName().toStdString());
    };

    write ("likyu-editor.png");

    // The foot in each mode, one picture each: Transient, Env, Track with an Own detector.
    auto& root = opened.window->editorRoot();
    const auto where = [&root] (P param) { return root.footPanel().boundsOf (param); };

    const auto rangeInDynamic = where (P::range);
    const auto keyInDynamic = where (P::key);
    const auto frequencyMode = where (P::fmode);
    CHECK_FALSE (rangeInDynamic.isEmpty());

    set (processor, 3, P::gmode, static_cast<double> (dsp::GainMode::transient));
    set (processor, 3, P::tatk, 60.0);
    set (processor, 3, P::tsus, -40.0);
    opened.refresh();
    write ("likyu-editor-transient.png");

    // From Dynamic to Transient nothing that stays has moved.
    CHECK (where (P::range) == rangeInDynamic);
    CHECK (where (P::key) == keyInDynamic);
    CHECK (where (P::fmode) == frequencyMode);
    CHECK (where (P::thresh).isEmpty());
    CHECK_FALSE (where (P::tatk).isEmpty());

    set (processor, 3, P::gmode, static_cast<double> (dsp::GainMode::staticGain));
    set (processor, 3, P::fmode, static_cast<double> (dsp::FrequencyMode::env));
    set (processor, 3, P::flo, 400.0);
    set (processor, 3, P::fhi, 3000.0);
    opened.refresh();
    write ("likyu-editor-env.png");

    // Static and Env: the gain block is empty and keeps its room; the key has not moved.
    const auto lowInEnv = where (P::flo);
    CHECK (where (P::range).isEmpty());
    CHECK (where (P::key) == keyInDynamic);
    CHECK (where (P::fmode) == frequencyMode);

    root.selectBand (4);
    set (processor, 4, P::gmode, static_cast<double> (dsp::GainMode::dynamic));
    set (processor, 4, P::dfilt, static_cast<double> (dsp::DetectorFilter::own));
    opened.refresh();
    write ("likyu-editor-track.png");

    // Track on another band: Low is where Env has it, and so are the key and the mode's choice.
    CHECK (where (P::flo).withY (0) == lowInEnv.withY (0));
    CHECK (where (P::flo) == lowInEnv);
    CHECK (where (P::key) == keyInDynamic);
    CHECK (where (P::fmode) == frequencyMode);
    CHECK (where (P::range) == rangeInDynamic);

    // The ghost of band 5's Own detector, set apart from its node; a band keyed by a sidechain that is not there.
    set (processor, 4, P::dfreq, 6000.0);
    set (processor, 1, P::gmode, static_cast<double> (dsp::GainMode::dynamic));
    set (processor, 1, P::key, static_cast<double> (dsp::KeySource::sidechain));
    run (processor, 1000.0, 0.5, 40);
    opened.refresh();
    write ("likyu-editor-ghost.png");

    // TEMPORARY: the transient's times, slid open over the frequency controls.
    set (processor, 3, P::fmode, static_cast<double> (dsp::FrequencyMode::fixed));
    set (processor, 3, P::gmode, static_cast<double> (dsp::GainMode::transient));
    root.selectBand (3);
    opened.refresh();
    root.footPanel().setTimesOpen (true);

    for (int i = 0; i < 6; ++i)
        opened.refresh();

    CHECK (root.footPanel().timesPanel().isVisible());
    write ("likyu-editor-times.png");
}

TEST_CASE ("editor: a drag moves what its part of the handle moves, by what the geometry says, in one gesture")
{
    Opened opened;
    auto& processor = opened.processor;
    auto& curve = opened.curve();
    const auto plot = opened.plot();
    const auto sizes = editor::geometry::sizesFor (plot.height, false);

    constexpr int band = 2;     // 200 Hz, 0 dB, a bell
    set (processor, band, P::on, 1.0);
    opened.refresh();

    GestureCounter gestures (processor);
    int selectedTold = -1;
    curve.onSelect = [&selectedTold] (int b) { selectedTold = b; };

    SUBCASE ("the centre: frequency and gain together")
    {
        const auto from = opened.nodeOf (band);
        drag (curve, from, from + juce::Point<float> (90.0f, -40.0f));

        CHECK (get (processor, band, P::freq) == doctest::Approx (editor::geometry::draggedHz (200.0, 90.0, plot, false)).epsilon (1.0e-5));
        CHECK (get (processor, band, P::gain) == doctest::Approx (editor::geometry::draggedDb (0.0, -40.0, plot, false)).epsilon (1.0e-5));
        CHECK (get (processor, band, P::freq) > 300.0);
        CHECK (get (processor, band, P::gain) > 5.0);

        CHECK (gestures.begunFor (band, P::freq) == 1);
        CHECK (gestures.endedFor (band, P::freq) == 1);
        CHECK (gestures.begunFor (band, P::gain) == 1);
        CHECK (gestures.endedFor (band, P::gain) == 1);
        CHECK (gestures.open == 0);

        // The press selected the band, and said so.
        CHECK (curve.selectedBand() == band);
        CHECK (selectedTold == band);
    }

    SUBCASE ("the horizontal arm: frequency only")
    {
        const auto node = opened.nodeOf (band);
        const auto from = node + juce::Point<float> (static_cast<float> (sizes.hit) + 6.0f, 0.0f);
        drag (curve, from, from + juce::Point<float> (-60.0f, -80.0f));

        CHECK (get (processor, band, P::freq) == doctest::Approx (editor::geometry::draggedHz (200.0, -60.0, plot, false)).epsilon (1.0e-5));
        CHECK (get (processor, band, P::gain) == 0.0);
        CHECK (gestures.begunFor (band, P::freq) == 1);
        CHECK (gestures.begunFor (band, P::gain) == 0);
        CHECK (gestures.open == 0);
    }

    SUBCASE ("the vertical arm: gain only")
    {
        const auto node = opened.nodeOf (band);
        const auto from = node + juce::Point<float> (0.0f, -static_cast<float> (sizes.hit) - 6.0f);
        drag (curve, from, from + juce::Point<float> (120.0f, 50.0f));

        CHECK (get (processor, band, P::freq) == doctest::Approx (200.0));
        CHECK (get (processor, band, P::gain) == doctest::Approx (editor::geometry::draggedDb (0.0, 50.0, plot, false)).epsilon (1.0e-5));
        CHECK (gestures.begunFor (band, P::freq) == 0);
        CHECK (gestures.begunFor (band, P::gain) == 1);
        CHECK (gestures.open == 0);
    }

    SUBCASE ("Shift: a tenth as far")
    {
        const auto from = opened.nodeOf (band);
        const auto mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);
        drag (curve, from, from + juce::Point<float> (90.0f, -40.0f), mods);

        CHECK (get (processor, band, P::freq) == doctest::Approx (editor::geometry::draggedHz (200.0, 90.0, plot, true)).epsilon (1.0e-5));
        CHECK (get (processor, band, P::gain) == doctest::Approx (editor::geometry::draggedDb (0.0, -40.0, plot, true)).epsilon (1.0e-5));
    }

    SUBCASE ("Shift pressed mid-drag: no jump, and fine from there")
    {
        const auto from = opened.nodeOf (band);
        const auto there = from + juce::Point<float> (90.0f, 0.0f);
        const auto mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);

        curve.mouseDown (eventAt (curve, from, from));
        curve.mouseDrag (eventAt (curve, there, from));
        const auto coarse = get (processor, band, P::freq);

        curve.mouseDrag (eventAt (curve, there, from, mods));
        CHECK (get (processor, band, P::freq) == doctest::Approx (coarse));

        curve.mouseDrag (eventAt (curve, there + juce::Point<float> (90.0f, 0.0f), from, mods));
        CHECK (get (processor, band, P::freq) == doctest::Approx (editor::geometry::draggedHz (coarse, 90.0, plot, true)).epsilon (1.0e-5));

        curve.mouseUp (eventAt (curve, there, from, mods));
        CHECK (gestures.open == 0);
    }

    SUBCASE ("a type without a gain: the centre moves frequency only")
    {
        set (processor, band, P::type, static_cast<double> (dsp::BandType::highPass));
        set (processor, band, P::gain, 7.0);
        opened.refresh();

        // Its node sits on the 0 dB line, whatever its gain parameter holds.
        const auto p = opened.plot();
        const juce::Point<float> from (static_cast<float> (editor::geometry::xOfHz (p, 200.0)),
                                       static_cast<float> (editor::geometry::yOfDb (p, 0.0)));
        drag (curve, from, from + juce::Point<float> (90.0f, -40.0f));

        CHECK (get (processor, band, P::freq) > 300.0);
        CHECK (get (processor, band, P::gain) == doctest::Approx (7.0));
        CHECK (gestures.begunFor (band, P::gain) == 0);
    }

    SUBCASE ("Env: the centre carries Low and High, and leaves Frequency alone")
    {
        set (processor, band, P::fmode, static_cast<double> (dsp::FrequencyMode::env));
        set (processor, band, P::flo, 100.0);
        set (processor, band, P::fhi, 400.0);
        opened.refresh();

        // Env rests at Low (Up): the node is there.
        const auto p = opened.plot();
        const juce::Point<float> from (static_cast<float> (editor::geometry::xOfHz (p, 100.0)),
                                       static_cast<float> (editor::geometry::yOfDb (p, 0.0)));
        drag (curve, from, from + juce::Point<float> (static_cast<float> (p.width / 3.0), 0.0f));

        CHECK (get (processor, band, P::flo) == doctest::Approx (1000.0).epsilon (1.0e-4));
        CHECK (get (processor, band, P::fhi) == doctest::Approx (4000.0).epsilon (1.0e-4));
        CHECK (get (processor, band, P::freq) == doctest::Approx (200.0));
        CHECK (gestures.begunFor (band, P::flo) == 1);
        CHECK (gestures.begunFor (band, P::fhi) == 1);
        CHECK (gestures.begunFor (band, P::freq) == 0);
        CHECK (gestures.open == 0);
    }

    SUBCASE ("a press on nothing moves nothing")
    {
        const juce::Point<float> empty (static_cast<float> (plot.x) + 3.0f, static_cast<float> (plot.y) + 3.0f);
        drag (curve, empty, empty + juce::Point<float> (200.0f, 100.0f));

        CHECK (get (processor, band, P::freq) == doctest::Approx (200.0));
        CHECK (gestures.begun.empty());
        CHECK (selectedTold == -1);
    }

    SUBCASE ("closing the editor mid-drag ends the gesture")
    {
        const auto from = opened.nodeOf (band);
        curve.mouseDown (eventAt (curve, from, from));
        CHECK (gestures.open == 2);

        opened.window.reset();
        CHECK (gestures.open == 0);
    }
}

TEST_CASE ("editor: the wheel sets Q, a double click puts a gain back at 0 dB")
{
    Opened opened;
    auto& processor = opened.processor;
    auto& curve = opened.curve();

    constexpr int band = 5;     // 3 kHz
    set (processor, band, P::on, 1.0);
    set (processor, band, P::gain, 9.0);
    opened.refresh();

    GestureCounter gestures (processor);
    const auto at = opened.nodeOf (band);

    juce::MouseWheelDetails wheel;
    wheel.deltaX = 0.0f;
    wheel.deltaY = 0.1f;
    wheel.isReversed = false;
    wheel.isSmooth = false;
    wheel.isInertial = false;

    // Over the node: its Q, whichever band is selected.
    curve.selectBand (0);
    curve.mouseWheelMove (eventAt (curve, at, at, juce::ModifierKeys()), wheel);
    CHECK (get (processor, band, P::q) == doctest::Approx (editor::turnedQ (1.0, 0.1, false, false)).epsilon (1.0e-5));
    CHECK (get (processor, 0, P::q) == doctest::Approx (1.0));

    // Over nothing: the selected band's.
    const juce::Point<float> empty (static_cast<float> (opened.plot().x) + 3.0f, static_cast<float> (opened.plot().y) + 3.0f);
    curve.mouseWheelMove (eventAt (curve, empty, empty, juce::ModifierKeys()), wheel);
    CHECK (get (processor, 0, P::q) == doctest::Approx (1.1).epsilon (1.0e-5));

    // Ctrl + wheel is a touchpad's pinch: the other way.
    curve.mouseWheelMove (eventAt (curve, empty, empty, juce::ModifierKeys (juce::ModifierKeys::ctrlModifier)), wheel);
    CHECK (get (processor, 0, P::q) == doctest::Approx (1.0).epsilon (1.0e-5));

    CHECK (gestures.begunFor (band, P::q) == 1);
    CHECK (gestures.begunFor (0, P::q) == 2);
    CHECK (gestures.open == 0);

    // A double click on the node: 0 dB, in one gesture.
    curve.mouseDown (eventAt (curve, at, at, juce::ModifierKeys::leftButtonModifier, 2));
    curve.mouseDoubleClick (eventAt (curve, at, at, juce::ModifierKeys::leftButtonModifier, 2));
    curve.mouseUp (eventAt (curve, at, at, juce::ModifierKeys::leftButtonModifier, 2));
    CHECK (get (processor, band, P::gain) == 0.0);
    CHECK (get (processor, band, P::freq) == doctest::Approx (3000.0));
    CHECK (gestures.open == 0);
}

TEST_CASE ("editor: a column per band, and a foot that follows the selected band and its modes")
{
    Opened opened;
    auto& processor = opened.processor;
    auto& root = opened.window->editorRoot();

    const auto footShows = [&root] (int band, P param)
    {
        const auto shown = root.footPanel().parameters();
        return std::find (shown.begin(), shown.end(), dsp::indexOf (band, param)) != shown.end();
    };

    // A new instance: band 1 selected, a static bell. Its foot holds the mode choices and no more.
    CHECK (opened.curve().selectedBand() == 0);
    CHECK (footShows (0, P::gmode));
    CHECK (footShows (0, P::fmode));
    CHECK (footShows (0, P::qmode));
    CHECK_FALSE (footShows (0, P::thresh));
    CHECK_FALSE (footShows (0, P::key));

    // Eight columns, side by side under the curve, the foot under them.
    for (int band = 0; band < dsp::numBands; ++band)
    {
        const auto bounds = root.bandColumn (band).getBounds();
        const auto width = editor::EditorRoot::defaultWidth / dsp::numBands;
        CHECK (bounds.getWidth() >= width);     // the last one takes the pixels left over
        CHECK (bounds.getX() == band * width);
        CHECK (bounds.getY() == opened.curve().getBottom());
        CHECK (bounds.getBottom() == root.footPanel().getY());
    }

    // A press in a column selects its band: the curve and the foot follow.
    auto& column = root.bandColumn (4);
    column.mouseDown (eventAt (column, { 20.0f, 10.0f }, { 20.0f, 10.0f }));
    CHECK (opened.curve().selectedBand() == 4);
    CHECK (footShows (4, P::gmode));
    CHECK_FALSE (footShows (0, P::gmode));

    // The band's mode changes, from anywhere: the foot shows that mode's controls at the next tick.
    set (processor, 4, P::gmode, static_cast<double> (dsp::GainMode::dynamic));
    opened.refresh();
    for (const auto p : { P::range, P::thresh, P::ratio, P::knee, P::atk, P::rel, P::ddir, P::key, P::dfilt })
        CHECK (footShows (4, p));
    CHECK_FALSE (footShows (4, P::tatk));
    CHECK_FALSE (footShows (4, P::dfreq));

    set (processor, 4, P::dfilt, static_cast<double> (dsp::DetectorFilter::own));
    set (processor, 4, P::fmode, static_cast<double> (dsp::FrequencyMode::track));
    opened.refresh();
    for (const auto p : { P::dtype, P::dfreq, P::dq, P::flo, P::fhi, P::fspeed, P::fconf, P::fhold, P::fcgain })
        CHECK (footShows (4, p));

    // Every control the foot shows is inside it, at the default size and at the smallest.
    for (const auto width : { editor::EditorRoot::defaultWidth, editor::EditorRoot::minimumWidth })
    {
        opened.window->setSize (width, editor::EditorRoot::minimumHeight);
        opened.refresh();

        const auto children = root.footPanel().getChildren();

        for (int i = 0; i < children.size(); ++i)
        {
            INFO ("window width " << width);
            CHECK (root.footPanel().getLocalBounds().contains (children[i]->getBounds()));

            // And no two of them overlap.
            for (int j = i + 1; j < children.size(); ++j)
                CHECK_FALSE (children[i]->getBounds().intersects (children[j]->getBounds()));
        }
    }

    // A press on the curve selects too.
    opened.window->setSize (editor::EditorRoot::defaultWidth, editor::EditorRoot::defaultHeight);
    opened.refresh();
    const auto node = opened.nodeOf (1);
    opened.curve().mouseDown (eventAt (opened.curve(), node, node));
    opened.curve().mouseUp (eventAt (opened.curve(), node, node));
    CHECK (footShows (1, P::gmode));
    CHECK_FALSE (footShows (4, P::gmode));
}

TEST_CASE ("editor: the selected band's channels are cells in the foot; a press takes a channel out of that band, and back")
{
    LikyuProcessor processor;
    const auto fiveOne = juce::AudioChannelSet::create5point1();

    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (fiveOne);
    layout.inputBuses.add (juce::AudioChannelSet::disabled());
    layout.outputBuses.add (fiveOne);
    REQUIRE (processor.setBusesLayout (layout));

    processor.setRateAndBufferSizeDetails (rate, blockSize);
    processor.prepareToPlay (rate, blockSize);

    {
        std::unique_ptr<EditorWindow> window (dynamic_cast<EditorWindow*> (processor.createEditor()));
        REQUIRE (window != nullptr);

        auto& root = window->editorRoot();
        auto& cells = root.footPanel().channelCells();

        // Six cells, shown, inside the foot, none overlapping another.
        CHECK (cells.isVisible());
        REQUIRE (cells.count() == 6);
        CHECK (root.footPanel().getLocalBounds().contains (cells.getBounds()));

        for (int c = 0; c < 6; ++c)
        {
            CHECK (cells.getLocalBounds().contains (cells.cellBounds (c)));

            for (int other = c + 1; other < 6; ++other)
                CHECK_FALSE (cells.cellBounds (c).intersects (cells.cellBounds (other)));
        }

        const auto press = [&cells] (int channel)
        {
            const auto at = cells.cellBounds (channel).getCentre().toFloat();
            cells.mouseDown (eventAt (cells, at, at));
        };

        // Band 1 is selected: the LFE out of it, and of it alone.
        press (3);
        CHECK (processor.bandChannelsLeftOut (0) == 8u);
        CHECK (processor.bandChannelsLeftOut (1) == 0u);
        CHECK (processor.getBandChannels().layout == "L R C Lfe Ls Rs");

        // Another band selected: its own cells, all in; a press there is that band's.
        root.selectBand (7);
        press (0);
        press (1);
        CHECK (processor.bandChannelsLeftOut (7) == 3u);
        CHECK (processor.bandChannelsLeftOut (0) == 8u);

        // Back on band 1: the foot shows what it left out, and a second press takes the LFE back.
        root.selectBand (0);
        press (3);
        CHECK (processor.bandChannelsLeftOut (0) == 0u);
        CHECK (processor.bandChannelsLeftOut (7) == 3u);

        CHECK (window->createComponentSnapshot (window->getLocalBounds()).isValid());
    }

    processor.releaseResources();

    // A mono instance has nothing to choose: no cells. 64 channels: every cell still inside.
    {
        LikyuProcessor mono (1);
        std::unique_ptr<EditorWindow> window (dynamic_cast<EditorWindow*> (mono.createEditor()));
        REQUIRE (window != nullptr);
        CHECK_FALSE (window->editorRoot().footPanel().channelCells().isVisible());
    }

    {
        LikyuProcessor wide;
        juce::AudioProcessor::BusesLayout wideLayout;
        wideLayout.inputBuses.add (juce::AudioChannelSet::discreteChannels (64));
        wideLayout.inputBuses.add (juce::AudioChannelSet::disabled());
        wideLayout.outputBuses.add (juce::AudioChannelSet::discreteChannels (64));
        REQUIRE (wide.setBusesLayout (wideLayout));

        std::unique_ptr<EditorWindow> window (dynamic_cast<EditorWindow*> (wide.createEditor()));
        REQUIRE (window != nullptr);

        auto& cells = window->editorRoot().footPanel().channelCells();
        REQUIRE (cells.count() == 64);

        for (int c = 0; c < 64; ++c)
            CHECK (cells.getLocalBounds().contains (cells.cellBounds (c)));
    }
}

TEST_CASE ("editor: two fingers: on two nodes they move both; the second on a held node, or on nothing, pinches its Q")
{
    Opened opened;
    auto& processor = opened.processor;
    auto& curve = opened.curve();
    const auto plot = opened.plot();

    constexpr int band = 2, other = 5;
    set (processor, band, P::on, 1.0);
    set (processor, other, P::on, 1.0);
    opened.refresh();

    GestureCounter gestures (processor);
    const editor::CurveView::PointerKey first { true, 0 }, second { true, 1 }, third { true, 2 };
    const juce::ModifierKeys none;

    SUBCASE ("one finger each on two nodes: both move, each by its own finger")
    {
        const auto a = opened.nodeOf (band), b = opened.nodeOf (other);
        curve.pointerDown (first, a, none);
        curve.pointerDown (second, b, none);
        curve.pointerMoved (first, a + juce::Point<float> (0.0f, -40.0f), none);
        curve.pointerMoved (second, b + juce::Point<float> (0.0f, 30.0f), none);

        CHECK (get (processor, band, P::gain) == doctest::Approx (editor::geometry::draggedDb (0.0, -40.0, plot, false)).epsilon (1.0e-5));
        CHECK (get (processor, other, P::gain) == doctest::Approx (editor::geometry::draggedDb (0.0, 30.0, plot, false)).epsilon (1.0e-5));
        CHECK (get (processor, band, P::q) == doctest::Approx (1.0));

        curve.pointerUp (first);
        curve.pointerUp (second);
        CHECK (gestures.open == 0);
    }

    SUBCASE ("a second finger beside a held node: closing the two narrows the band")
    {
        const auto a = opened.nodeOf (band);
        const auto b = a + juce::Point<float> (120.0f, 0.0f);       // past the arm's end: on nothing

        curve.pointerDown (first, a, none);
        CHECK (gestures.begunFor (band, P::freq) == 1);

        curve.pointerDown (second, b, none);

        // The node's own drag stopped; the Q's gesture began.
        CHECK (gestures.endedFor (band, P::freq) == 1);
        CHECK (gestures.endedFor (band, P::gain) == 1);
        CHECK (gestures.begunFor (band, P::q) == 1);

        // Half as far apart: twice the Q. The node does not follow either finger.
        curve.pointerMoved (second, a + juce::Point<float> (60.0f, 0.0f), none);
        CHECK (get (processor, band, P::q) == doctest::Approx (2.0).epsilon (1.0e-5));
        CHECK (get (processor, band, P::freq) == doctest::Approx (200.0));

        // Apart again, past where they started: wider than it was.
        curve.pointerMoved (first, a + juce::Point<float> (-120.0f, 0.0f), none);
        CHECK (get (processor, band, P::q) == doctest::Approx (120.0 / 180.0).epsilon (1.0e-5));
        CHECK (get (processor, band, P::freq) == doctest::Approx (200.0));

        // A third finger changes nothing.
        curve.pointerDown (third, opened.nodeOf (other), none);
        curve.pointerMoved (third, opened.nodeOf (other) + juce::Point<float> (0.0f, 50.0f), none);
        CHECK (get (processor, other, P::gain) == 0.0);
        curve.pointerUp (third);

        // One finger lifted ends the pinch; the one left moves nothing.
        curve.pointerUp (second);
        CHECK (gestures.endedFor (band, P::q) == 1);
        const auto q = get (processor, band, P::q);
        curve.pointerMoved (first, a + juce::Point<float> (40.0f, 40.0f), none);
        CHECK (get (processor, band, P::q) == doctest::Approx (q));
        CHECK (get (processor, band, P::freq) == doctest::Approx (200.0));
        curve.pointerUp (first);
        CHECK (gestures.open == 0);
    }

    SUBCASE ("a second finger on the node itself pinches too")
    {
        const auto a = opened.nodeOf (band);
        curve.pointerDown (first, a + juce::Point<float> (-4.0f, 0.0f), none);
        curve.pointerDown (second, a + juce::Point<float> (4.0f, 0.0f), none);
        CHECK (gestures.begunFor (band, P::q) == 1);

        // Fingers that land together are 8 pixels apart at the least; spread to 32, the band is four times wider.
        curve.pointerMoved (second, a + juce::Point<float> (28.0f, 0.0f), none);
        CHECK (get (processor, band, P::q) == doctest::Approx (0.25).epsilon (1.0e-5));

        curve.pointerUp (first);
        curve.pointerUp (second);
        CHECK (gestures.open == 0);
    }

    SUBCASE ("the mouse never pinches: a second press on nothing is nothing")
    {
        const auto a = opened.nodeOf (band);
        curve.pointerDown ({ false, 0 }, a, none);
        curve.pointerDown ({ false, 1 }, a + juce::Point<float> (120.0f, 0.0f), none);
        CHECK (gestures.begunFor (band, P::q) == 0);
        curve.pointerUp ({ false, 0 });
        CHECK (gestures.open == 0);
    }

    SUBCASE ("closing the editor mid-pinch ends the gesture")
    {
        const auto a = opened.nodeOf (band);
        curve.pointerDown (first, a, none);
        curve.pointerDown (second, a + juce::Point<float> (120.0f, 0.0f), none);
        CHECK (gestures.open == 1);
        opened.window.reset();
        CHECK (gestures.open == 0);
    }
}

TEST_CASE ("editor: Listen, Reset and Flatten want a long press")
{
    Opened opened;
    auto& processor = opened.processor;
    auto& root = opened.window->editorRoot();
    auto& listen = root.footPanel().listenButton();
    auto& reset = root.footPanel().resetButton();
    auto& flatten = root.globalRow().flattenButton();
    const auto hold = editor::LongPressButton::defaultHoldMs;

    // Three bands away from their defaults, one of them leaving a channel out.
    for (const auto band : { 0, 3, 6 })
    {
        set (processor, band, P::on, 1.0);
        set (processor, band, P::gain, 7.0);
        set (processor, band, P::q, 3.0);
        set (processor, band, P::gmode, static_cast<double> (dsp::GainMode::dynamic));
        set (processor, band, P::thresh, -50.0);
    }

    processor.setBandChannelsLeftOut (3, 2u);
    root.selectBand (3);
    opened.refresh();

    SUBCASE ("Listen: nothing on a tap; heard from the long press until the release")
    {
        CHECK (listen.isEnabled());

        listen.press();
        listen.heldFor (hold - 50);
        CHECK (processor.listen() == -1);
        listen.release (true);
        CHECK (processor.listen() == -1);

        listen.press();
        listen.heldFor (hold);
        CHECK (processor.listen() == 3);
        listen.heldFor (hold + 2000);
        CHECK (processor.listen() == 3);
        listen.release (true);
        CHECK (processor.listen() == -1);

        // Released off the button: it stops all the same.
        listen.press();
        listen.heldFor (hold);
        listen.release (false);
        CHECK (processor.listen() == -1);
    }

    SUBCASE ("Listen stops when another band is selected, and when the editor closes")
    {
        listen.press();
        listen.heldFor (hold);
        CHECK (processor.listen() == 3);
        root.selectBand (0);
        CHECK (processor.listen() == -1);

        listen.press();
        listen.heldFor (hold);
        CHECK (processor.listen() == 0);
        opened.window.reset();
        CHECK (processor.listen() == -1);
    }

    SUBCASE ("Listen is not offered on a band with no detector in use, or off")
    {
        set (processor, 3, P::gmode, static_cast<double> (dsp::GainMode::staticGain));
        opened.refresh();
        CHECK_FALSE (listen.isEnabled());
        listen.press();
        listen.heldFor (hold);
        CHECK (processor.listen() == -1);
        listen.release (true);

        set (processor, 3, P::gmode, static_cast<double> (dsp::GainMode::dynamic));
        set (processor, 3, P::on, 0.0);
        opened.refresh();
        CHECK_FALSE (listen.isEnabled());
    }

    SUBCASE ("Reset: the shown band back to its defaults, its channels with it; the others as they were")
    {
        // A tap, a long press let go off the button: nothing.
        reset.press();
        reset.heldFor (hold - 1);
        reset.release (true);
        reset.press();
        reset.heldFor (hold);
        reset.release (false);
        CHECK (get (processor, 3, P::gain) == doctest::Approx (7.0));

        reset.press();
        reset.heldFor (hold);
        CHECK (get (processor, 3, P::gain) == doctest::Approx (7.0));     // on release, not before
        reset.release (true);

        const auto& table = dsp::parameterTable();

        for (int p = 0; p < dsp::numBandParams; ++p)
        {
            const auto index = dsp::indexOf (3, static_cast<P> (p));
            INFO (table[static_cast<std::size_t> (index)].id);
            CHECK (processor.valueOf (index) == doctest::Approx (table[static_cast<std::size_t> (index)].defaultValue).epsilon (1.0e-6));
        }

        CHECK (processor.bandChannelsLeftOut (3) == 0u);
        CHECK (get (processor, 0, P::gain) == doctest::Approx (7.0));
        CHECK (get (processor, 6, P::thresh) == doctest::Approx (-50.0));
    }

    SUBCASE ("Flatten: every band back to its defaults; the instance's own parameters stay")
    {
        processor.setValue (dsp::indexOf (dsp::GlobalParam::outGain), -3.0);

        flatten.press();
        flatten.heldFor (hold);
        flatten.release (true);

        const auto& table = dsp::parameterTable();

        for (int index = dsp::numGlobalParams; index < dsp::numParams; ++index)
        {
            INFO (table[static_cast<std::size_t> (index)].id);
            CHECK (processor.valueOf (index) == doctest::Approx (table[static_cast<std::size_t> (index)].defaultValue).epsilon (1.0e-6));
        }

        CHECK (processor.bandChannelsLeftOut (3) == 0u);
        CHECK (processor.valueOf (dsp::indexOf (dsp::GlobalParam::outGain)) == doctest::Approx (-3.0));
    }
}

TEST_CASE ("editor: any values of the 284 parameters paint, as a validator throws them at it")
{
    /*  What pluginval does with the editor open (its "Editor Automation" and
        "Fuzz parameters"): every parameter at a random place of its range,
        again and again. Here with audio running between, so the meters move
        too. With LIKYU_SNAPSHOT_DIR set, each configuration is written as
        likyu-fuzz-NN.png, to look at what a validator shows for an instant. */
    Opened opened;
    auto& processor = opened.processor;
    auto& root = opened.window->editorRoot();
    const auto folder = juce::SystemStats::getEnvironmentVariable ("LIKYU_SNAPSHOT_DIR", {});

    juce::Random random (20261120);
    constexpr int configurations = 48;

    for (int n = 0; n < configurations; ++n)
    {
        for (int i = 0; i < dsp::numParams; ++i)
            processor.parameterAt (i).setValueNotifyingHost (random.nextFloat());

        // Not bypassed, or there is nothing to see move; and a band to look at.
        processor.setValue (dsp::indexOf (dsp::GlobalParam::bypass), 0.0);
        root.selectBand (random.nextInt (dsp::numBands));

        run (processor, 200.0 + 4000.0 * random.nextDouble(), 0.4, 40);
        opened.refresh();

        const auto image = opened.window->createComponentSnapshot (opened.window->getLocalBounds());
        REQUIRE (image.isValid());

        // Every control the foot shows is inside it and none overlaps another, whatever the modes.
        const auto children = root.footPanel().getChildren();

        for (int a = 0; a < children.size(); ++a)
        {
            if (! children[a]->isVisible())
                continue;

            CHECK (root.footPanel().getLocalBounds().contains (children[a]->getBounds()));

            for (int b = a + 1; b < children.size(); ++b)
                if (children[b]->isVisible())
                    CHECK_FALSE (children[a]->getBounds().intersects (children[b]->getBounds()));
        }

        if (folder.isNotEmpty())
        {
            const auto file = juce::File (folder).getChildFile ("likyu-fuzz-" + juce::String (n).paddedLeft ('0', 2) + ".png");
            file.deleteFile();

            if (juce::FileOutputStream stream (file); stream.openedOk())
                juce::PNGImageFormat().writeImageToStream (image, stream);
        }
    }

    // Every value was in range: nothing the editor read was a NaN, and the meters still publish.
    core::Meters meters;
    processor.meters().read (meters);
    CHECK (meters.block > 0);

    for (const auto& band : meters.bands)
    {
        CHECK (std::isfinite (band.offsetDb));
        CHECK (std::isfinite (band.frequencyHz));
    }
}

TEST_CASE ("editor: the ghost node of an Own detector: the selected band's is dragged and wheeled, the others' take no press")
{
    Opened opened;
    auto& processor = opened.processor;
    auto& curve = opened.curve();
    const auto plot = opened.plot();

    // Two dynamic bells, each listening somewhere else than where it acts.
    for (const auto band : { 2, 5 })
    {
        set (processor, band, P::on, 1.0);
        set (processor, band, P::gmode, static_cast<double> (dsp::GainMode::dynamic));
        set (processor, band, P::dfilt, static_cast<double> (dsp::DetectorFilter::own));
    }

    set (processor, 2, P::dfreq, 600.0);
    set (processor, 5, P::dfreq, 9000.0);
    curve.selectBand (2);
    opened.refresh();

    const auto ghostOf = [&opened, &processor, plot] (int band)
    {
        return juce::Point<float> (static_cast<float> (editor::geometry::xOfHz (plot, get (processor, band, P::dfreq))),
                                   opened.nodeOf (band).y);
    };

    GestureCounter gestures (processor);

    // The selected band's ghost: a drag moves the detector's frequency, and nothing of the band.
    auto from = ghostOf (2);
    drag (curve, from, from + juce::Point<float> (80.0f, -30.0f));
    CHECK (get (processor, 2, P::dfreq) == doctest::Approx (editor::geometry::draggedHz (600.0, 80.0, plot, false)).epsilon (1.0e-5));
    CHECK (get (processor, 2, P::freq) == doctest::Approx (200.0));
    CHECK (get (processor, 2, P::gain) == 0.0);
    CHECK (gestures.begunFor (2, P::dfreq) == 1);
    CHECK (gestures.endedFor (2, P::dfreq) == 1);
    CHECK (gestures.begunFor (2, P::freq) == 0);
    CHECK (gestures.open == 0);

    // The wheel over it: the detector's Q, not the band's.
    juce::MouseWheelDetails wheel;
    wheel.deltaX = 0.0f;
    wheel.deltaY = 0.1f;
    wheel.isReversed = false;
    wheel.isSmooth = false;
    wheel.isInertial = false;

    from = ghostOf (2);
    curve.mouseWheelMove (eventAt (curve, from, from, juce::ModifierKeys()), wheel);
    CHECK (get (processor, 2, P::dq) == doctest::Approx (1.1).epsilon (1.0e-5));
    CHECK (get (processor, 2, P::q) == doctest::Approx (1.0));

    // Another band's ghost is drawn, faint, and takes no press: it does not move, and the selection stays.
    const auto others = ghostOf (5);
    drag (curve, others, others + juce::Point<float> (-90.0f, 0.0f));
    CHECK (get (processor, 5, P::dfreq) == doctest::Approx (9000.0));
    CHECK (curve.selectedBand() == 2);

    // Selected, it does.
    curve.selectBand (5);
    drag (curve, others, others + juce::Point<float> (-90.0f, 0.0f));
    CHECK (get (processor, 5, P::dfreq) == doctest::Approx (editor::geometry::draggedHz (9000.0, -90.0, plot, false)).epsilon (1.0e-5));

    // The ghost under its own node (the detector listens where the band acts): the node takes a
    // plain press; with Shift the press takes the ghost, and moves it by the whole drag, not a fine one.
    {
        set (processor, 5, P::dfreq, get (processor, 5, P::freq));
        opened.refresh();

        const auto shift = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);
        const auto both = opened.nodeOf (5);
        const auto bandAt = get (processor, 5, P::freq);

        drag (curve, both, both + juce::Point<float> (70.0f, 0.0f), shift);
        CHECK (get (processor, 5, P::dfreq) == doctest::Approx (editor::geometry::draggedHz (bandAt, 70.0, plot, false)).epsilon (1.0e-5));
        CHECK (get (processor, 5, P::freq) == doctest::Approx (bandAt));

        // Back under the node: a plain press there is the node's, as ever.
        set (processor, 5, P::dfreq, bandAt);
        opened.refresh();
        drag (curve, both, both + juce::Point<float> (-50.0f, 0.0f));
        CHECK (get (processor, 5, P::freq) == doctest::Approx (editor::geometry::draggedHz (bandAt, -50.0, plot, false)).epsilon (1.0e-5));
        CHECK (get (processor, 5, P::dfreq) == doctest::Approx (bandAt));

        // Shift on a node with no ghost under it is still the node's fine drag.
        const auto other = opened.nodeOf (2);
        const auto otherAt = get (processor, 2, P::freq);
        curve.selectBand (2);
        drag (curve, other, other + juce::Point<float> (60.0f, 0.0f), shift);
        CHECK (get (processor, 2, P::freq) == doctest::Approx (editor::geometry::draggedHz (otherAt, 60.0, plot, true)).epsilon (1.0e-5));
        curve.selectBand (5);
    }

    // Follow, or a band that reads no key: no ghost to press.
    set (processor, 5, P::dfilt, static_cast<double> (dsp::DetectorFilter::follow));
    opened.refresh();
    const auto before = get (processor, 5, P::dfreq);
    const auto where = ghostOf (5);
    drag (curve, where, where + juce::Point<float> (60.0f, 0.0f));
    CHECK (get (processor, 5, P::dfreq) == doctest::Approx (before));

    CHECK (opened.window->createComponentSnapshot (opened.window->getLocalBounds()).isValid());
}

TEST_CASE ("editor: the transient's temporary times slide over the frequency controls from a chevron, and close with the band")
{
    Opened opened;
    auto& processor = opened.processor;
    auto& root = opened.window->editorRoot();
    auto& foot = root.footPanel();

    set (processor, 2, P::on, 1.0);
    set (processor, 2, P::gmode, static_cast<double> (dsp::GainMode::transient));
    set (processor, 4, P::on, 1.0);
    set (processor, 4, P::gmode, static_cast<double> (dsp::GainMode::dynamic));

    // No chevron on a band that is not in Transient.
    root.selectBand (4);
    opened.refresh();
    CHECK_FALSE (foot.timesChevron().isVisible());
    CHECK_FALSE (foot.timesPanel().isVisible());

    // In Transient: the chevron, in the Gain block; the panel closed.
    root.selectBand (2);
    opened.refresh();
    CHECK (foot.timesChevron().isVisible());
    CHECK_FALSE (foot.timesAreOpen());
    CHECK_FALSE (foot.timesPanel().isVisible());

    // A press on the chevron: it slides open over a few refreshes, over the Frequency block and nothing else.
    const juce::Point<float> centre (9.0f, 9.0f);
    foot.timesChevron().mouseDown (eventAt (foot.timesChevron(), centre, centre));
    CHECK (foot.timesAreOpen());

    opened.refresh();
    const auto partly = foot.timesPanel().getWidth();
    CHECK (foot.timesPanel().isVisible());

    for (int i = 0; i < 6; ++i)
        opened.refresh();

    const auto open = foot.timesPanel().getBounds();
    CHECK (partly > 0);
    CHECK (partly < open.getWidth());
    CHECK (open.contains (foot.boundsOf (P::fmode)));
    CHECK_FALSE (open.intersects (foot.boundsOf (P::gmode)));
    CHECK_FALSE (open.intersects (foot.boundsOf (P::range)));
    CHECK_FALSE (open.intersects (foot.channelCells().getBounds()));
    CHECK_FALSE (open.intersects (foot.timesChevron().getBounds()));

    // Its four dials are the four temporary host parameters: a wheel on the first moves the first.
    const auto before = processor.temporaryTransientTime (0).get();
    auto* dial = foot.timesPanel().getChildComponent (0);
    REQUIRE (dial != nullptr);

    juce::MouseWheelDetails wheel;
    wheel.deltaX = 0.0f;
    wheel.deltaY = 1.0f;
    wheel.isReversed = false;
    wheel.isSmooth = false;
    wheel.isInertial = false;
    dial->mouseWheelMove (eventAt (*dial, { 20.0f, 30.0f }, { 20.0f, 30.0f }, juce::ModifierKeys()), wheel);
    CHECK (processor.temporaryTransientTime (0).get() > before);
    CHECK (processor.temporaryTransientTime (1).get() == doctest::Approx (dsp::temporaryTransientSpecs()[1].defaultValue));

    // Another band selected: it closes, and is gone a few refreshes later.
    root.selectBand (4);
    CHECK_FALSE (foot.timesAreOpen());

    for (int i = 0; i < 6; ++i)
        opened.refresh();

    CHECK_FALSE (foot.timesPanel().isVisible());

    // Back on the Transient band it stays closed until asked; leaving Transient closes it too.
    root.selectBand (2);
    opened.refresh();
    CHECK_FALSE (foot.timesAreOpen());

    foot.setTimesOpen (true);
    set (processor, 2, P::gmode, static_cast<double> (dsp::GainMode::dynamic));
    opened.refresh();
    CHECK_FALSE (foot.timesAreOpen());
    CHECK_FALSE (foot.timesChevron().isVisible());
}

TEST_CASE ("editor: the analyser, off by default: its toggle turns the tap on, the curve shows the input and the sidechain, and closing turns it off")
{
    LikyuProcessor processor;

    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    REQUIRE (processor.setBusesLayout (layout));

    processor.setRateAndBufferSizeDetails (rate, blockSize);
    processor.prepareToPlay (rate, blockSize);

    // A 1 kHz tone on the input, a 5 kHz one on the sidechain.
    const auto play = [&processor] (int blocks)
    {
        juce::AudioBuffer<float> buffer (4, blockSize);
        juce::MidiBuffer midi;
        static double phase = 0.0;

        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto t = phase + i / rate;
                const auto input = static_cast<float> (0.5 * std::sin (juce::MathConstants<double>::twoPi * 1000.0 * t));
                const auto side = static_cast<float> (0.5 * std::sin (juce::MathConstants<double>::twoPi * 5000.0 * t));
                buffer.setSample (0, i, input);
                buffer.setSample (1, i, input);
                buffer.setSample (2, i, side);
                buffer.setSample (3, i, side);
            }

            phase += blockSize / rate;
            processor.processBlock (buffer, midi);
        }
    };

    {
        std::unique_ptr<EditorWindow> window (dynamic_cast<EditorWindow*> (processor.createEditor()));
        REQUIRE (window != nullptr);

        auto& root = window->editorRoot();
        auto& curve = root.curveView();
        auto& toggle = root.globalRow().analyserToggle();

        // Off: the tap writes nothing, and the curve shows no spectrum.
        CHECK_FALSE (toggle.isOn());
        CHECK_FALSE (processor.analyser().isEnabled());
        play (40);
        root.refreshNow();
        CHECK (curve.inputSpectrum().empty());

        // On.
        toggle.mouseDown (eventAt (toggle, { 10.0f, 10.0f }, { 10.0f, 10.0f }));
        CHECK (toggle.isOn());
        CHECK (curve.isAnalyserOn());
        CHECK (processor.analyser().isEnabled());

        play (60);
        root.refreshNow();

        const auto plot = editor::geometry::plotWithin (curve.getWidth(), curve.getHeight());
        const auto at = [&plot] (const std::vector<double>& db, double hz)
        {
            const auto i = static_cast<std::size_t> (std::lround (editor::geometry::xOfHz (plot, hz) - plot.x));
            return db[std::min (i, db.size() - 1)];
        };

        REQUIRE_FALSE (curve.inputSpectrum().empty());
        REQUIRE_FALSE (curve.sidechainSpectrum().empty());

        // The input's tone where it is, at its level (-6 dBFS, 1 kHz is the tilt's pivot); the sidechain's at 5 kHz.
        CHECK (at (curve.inputSpectrum(), 1000.0) == doctest::Approx (-6.0).epsilon (0.3));
        CHECK (at (curve.inputSpectrum(), 5000.0) < -40.0);
        CHECK (at (curve.sidechainSpectrum(), 5000.0) > -6.0);
        CHECK (at (curve.sidechainSpectrum(), 1000.0) < -40.0);

        if (const auto folder = juce::SystemStats::getEnvironmentVariable ("LIKYU_SNAPSHOT_DIR", {}); folder.isNotEmpty())
        {
            const auto file = juce::File (folder).getChildFile ("likyu-editor-analyser.png");
            file.deleteFile();

            if (juce::FileOutputStream stream (file); stream.openedOk())
                juce::PNGImageFormat().writeImageToStream (window->createComponentSnapshot (window->getLocalBounds()), stream);
        }

        // Off again by its toggle.
        toggle.mouseDown (eventAt (toggle, { 10.0f, 10.0f }, { 10.0f, 10.0f }));
        CHECK_FALSE (processor.analyser().isEnabled());

        // On, then the editor closes: off, and the next editor starts with it off.
        toggle.mouseDown (eventAt (toggle, { 10.0f, 10.0f }, { 10.0f, 10.0f }));
        CHECK (processor.analyser().isEnabled());
    }

    CHECK_FALSE (processor.analyser().isEnabled());

    {
        std::unique_ptr<EditorWindow> window (dynamic_cast<EditorWindow*> (processor.createEditor()));
        CHECK_FALSE (window->editorRoot().globalRow().analyserToggle().isOn());
        CHECK_FALSE (processor.analyser().isEnabled());
    }

    processor.releaseResources();
}
