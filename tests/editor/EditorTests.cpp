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

    if (const auto folder = juce::SystemStats::getEnvironmentVariable ("LIKYU_SNAPSHOT_DIR", {}); folder.isNotEmpty())
    {
        const auto file = juce::File (folder).getChildFile ("likyu-editor.png");
        file.deleteFile();

        if (juce::FileOutputStream stream (file); stream.openedOk())
            juce::PNGImageFormat().writeImageToStream (image, stream);

        MESSAGE ("wrote " << file.getFullPathName().toStdString());
    }
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

        for (auto* child : root.footPanel().getChildren())
        {
            INFO ("window width " << width);
            CHECK (root.footPanel().getLocalBounds().contains (child->getBounds()));
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
