#include "PitchCanvas.h"

namespace
{
    namespace colours
    {
        const juce::Colour background { 0xff1b1d23 };
        const juce::Colour blackKeyRow { 0xff17191e };
        const juce::Colour gutter { 0xff22252c };
        const juce::Colour semitoneLine { 0xff2a2d35 };
        const juce::Colour octaveLine { 0xff3c414c };
        const juce::Colour beatLine { 0xff2a2d35 };
        const juce::Colour barLine { 0xff464b57 };
        const juce::Colour label { 0xff9aa0ab };
        const juce::Colour playhead { 0xffe8e8e8 };
    }

    juce::Colour trajectoryColour (std::uint32_t id)
    {
        static const juce::Colour palette[] = {
            juce::Colour (0xff4fc3f7), juce::Colour (0xffffb74d), juce::Colour (0xff81c784), juce::Colour (0xfff06292),
            juce::Colour (0xffba68c8), juce::Colour (0xfffff176), juce::Colour (0xff4db6ac), juce::Colour (0xffff8a65),
        };
        return palette[id % std::size (palette)];
    }

    bool isBlackKey (int note)
    {
        const int pc = ((note % 12) + 12) % 12;
        return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
    }

    constexpr float eraseRadius = 8.0f;
    constexpr float hitRadius = 6.0f;
}

PitchCanvas::PitchCanvas (PitchPaintProcessor& p) : processor (p)
{
    setOpaque (true);
    setWantsKeyboardFocus (true); // keys bubble up to the editor's shortcuts
    startTimerHz (30);
}

juce::Rectangle<float> PitchCanvas::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (gutter);
}

juce::Range<double> PitchCanvas::getVisibleBeats() const
{
    const double length = processor.getPattern().lengthBeats;
    if (showWholeLoop || viewLength >= length)
        return { 0.0, length };
    const double start = juce::jlimit (0.0, length - viewLength, viewStart);
    return { start, start + viewLength };
}

float PitchCanvas::beatToX (double beat) const
{
    const auto area = plotArea();
    const auto view = getVisibleBeats();
    return area.getX() + (float) ((beat - view.getStart()) / view.getLength()) * area.getWidth();
}

double PitchCanvas::xToBeat (float x) const
{
    const auto area = plotArea();
    const auto view = getVisibleBeats();
    return view.getStart() + (double) (x - area.getX()) / area.getWidth() * view.getLength();
}

float PitchCanvas::pitchToY (double pitch) const
{
    const auto area = plotArea();
    return area.getY() + (float) ((topPitch - pitch) / (topPitch - bottomPitch)) * area.getHeight();
}

pitchpaint::PitchPoint PitchCanvas::pointAt (juce::Point<float> pos, bool snapToSemitone) const
{
    const auto area = plotArea();
    const double beat = juce::jlimit (0.0, processor.getPattern().lengthBeats, xToBeat (pos.x));
    double pitch = topPitch - (double) (pos.y - area.getY()) / area.getHeight() * (topPitch - bottomPitch);
    pitch = juce::jlimit (0.0, 127.0, snapToSemitone ? std::round (pitch) : pitch);
    return { beat, pitch };
}

//==============================================================================
void PitchCanvas::paint (juce::Graphics& g)
{
    const auto& pattern = processor.getPattern();
    paintGrid (g, pattern);

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (plotArea().toNearestInt());
    paintTrajectories (g, pattern);
    paintPlayhead (g, pattern);

    if (tool == Tool::eraser && hoverPos)
    {
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawEllipse (juce::Rectangle<float> (2.0f * eraseRadius, 2.0f * eraseRadius).withCentre (*hoverPos), 1.0f);
    }
}

void PitchCanvas::paintGrid (juce::Graphics& g, const pitchpaint::Pattern& pattern)
{
    const auto area = plotArea();
    g.fillAll (colours::background);

    const float pxPerSemitone = area.getHeight() / (float) (topPitch - bottomPitch);
    const bool labelEveryNote = pxPerSemitone >= 14.0f;

    g.setColour (colours::gutter);
    g.fillRect (getLocalBounds().toFloat().withWidth (gutter));
    g.setFont (juce::FontOptions (11.0f));

    // A horizontal line sits exactly on each semitone: drawing along it is in tune.
    for (int n = (int) std::ceil (bottomPitch - 0.5); n <= (int) std::floor (topPitch + 0.5); ++n)
    {
        const float y = pitchToY (n);

        if (isBlackKey (n))
        {
            g.setColour (colours::blackKeyRow);
            g.fillRect (juce::Rectangle<float> (area.getX(), y - pxPerSemitone * 0.5f, area.getWidth(), pxPerSemitone));
        }

        const bool isC = n % 12 == 0;
        g.setColour (isC ? colours::octaveLine : colours::semitoneLine);
        g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());

        if (isC || labelEveryNote)
        {
            g.setColour (isC ? colours::label : colours::label.withAlpha (0.6f));
            g.drawText (juce::MidiMessage::getMidiNoteName (n, true, true, 4),
                        juce::Rectangle<float> (0.0f, y - 7.0f, gutter - 6.0f, 14.0f), juce::Justification::centredRight);
        }
    }

    // Vertical lines: bars, beats, and quarter beats once zoomed in far enough to see them.
    const auto view = getVisibleBeats();
    const double pxPerBeat = area.getWidth() / view.getLength();
    const double step = pxPerBeat >= 60.0 ? 0.25 : (pxPerBeat >= 10.0 ? 1.0 : 4.0);
    const double lastBeat = std::min (view.getEnd(), pattern.lengthBeats);

    for (auto k = (long long) std::ceil (view.getStart() / step); k * step <= lastBeat + 1.0e-9; ++k)
    {
        const double beat = k * step;
        const bool isBar = std::fmod (beat, 4.0) == 0.0;
        const bool isBeat = std::fmod (beat, 1.0) == 0.0;
        g.setColour (isBar ? colours::barLine : (isBeat ? colours::beatLine : colours::beatLine.withAlpha (0.5f)));
        const int x = juce::roundToInt (beatToX (beat));
        g.drawVerticalLine (x, area.getY(), area.getBottom());

        if (isBar && beat < pattern.lengthBeats)
        {
            g.setColour (colours::label);
            g.drawText (juce::String ((int) (beat / 4.0) + 1), juce::Rectangle<float> ((float) x + 4.0f, area.getY() + 2.0f, 40.0f, 14.0f),
                        juce::Justification::centredLeft);
        }
    }
}

void PitchCanvas::paintTrajectories (juce::Graphics& g, const pitchpaint::Pattern& pattern)
{
    for (const auto& t : pattern.trajectories)
    {
        if (t.points.empty())
            continue;

        const bool selected = isSelected (t.id);
        g.setColour (trajectoryColour (t.id));

        if (t.points.size() == 1)
        {
            const juce::Point<float> p { beatToX (t.points[0].beat), pitchToY (t.points[0].pitch) };
            g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (p));
            continue;
        }

        juce::Path path;
        path.startNewSubPath (beatToX (t.points[0].beat), pitchToY (t.points[0].pitch));
        for (size_t i = 1; i < t.points.size(); ++i)
            path.lineTo (beatToX (t.points[i].beat), pitchToY (t.points[i].pitch));

        if (selected)
        {
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.strokePath (path, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (trajectoryColour (t.id));
        }
        g.strokePath (path, juce::PathStrokeType (selected ? 3.0f : 2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (! box.isEmpty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (box);
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.drawRect (box, 1.0f);
    }
}

void PitchCanvas::paintPlayhead (juce::Graphics& g, const pitchpaint::Pattern& pattern)
{
    if (! processor.isPlaying())
        return;

    const double beat = processor.getPlayheadBeat();
    const auto area = plotArea();
    const float x = beatToX (beat);

    g.setColour (colours::playhead.withAlpha (0.7f));
    g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());

    // Mark where each sounding trajectory is right now.
    for (const auto& t : pattern.trajectories)
    {
        if (! t.isPlayable() || beat < t.startBeat() || beat >= t.endBeat())
            continue;

        const juce::Point<float> p { x, pitchToY (t.pitchAt (beat)) };
        g.setColour (trajectoryColour (t.id));
        g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (p));
        g.setColour (colours::playhead);
        g.drawEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (p), 1.5f);
    }
}

void PitchCanvas::timerCallback()
{
    if (const int version = processor.getPatternVersion(); version != lastSeenVersion)
    {
        lastSeenVersion = version;

        // Undo, state loads etc. can remove trajectories: drop them from the selection.
        const auto& pattern = processor.getPattern();
        std::erase_if (selection, [&] (std::uint32_t id)
                       { return std::none_of (pattern.trajectories.begin(), pattern.trajectories.end(),
                                              [id] (const auto& t) { return t.id == id; }); });
        if (onPatternChanged)
            onPatternChanged();
        repaint();
    }

    const double beat = processor.isPlaying() ? processor.getPlayheadBeat() : -1.0;
    if (beat != lastPaintedPlayhead)
    {
        lastPaintedPlayhead = beat;
        repaint();
    }
}

//==============================================================================
std::vector<juce::Line<float>> PitchCanvas::segmentsOf (const pitchpaint::Trajectory& t) const
{
    std::vector<juce::Line<float>> lines;
    if (t.points.size() == 1)
    {
        const juce::Point<float> a { beatToX (t.points[0].beat), pitchToY (t.points[0].pitch) };
        lines.push_back ({ a, a });
    }
    for (size_t i = 0; i + 1 < t.points.size(); ++i)
        lines.push_back ({ { beatToX (t.points[i].beat), pitchToY (t.points[i].pitch) },
                           { beatToX (t.points[i + 1].beat), pitchToY (t.points[i + 1].pitch) } });
    return lines;
}

bool PitchCanvas::sweepHits (const pitchpaint::Trajectory& t, juce::Line<float> swept, float radius) const
{
    const auto distance = [] (juce::Line<float> a, juce::Line<float> b)
    {
        juce::Point<float> p;
        if (a.intersects (b, p))
            return 0.0f;
        return std::min ({ a.getDistanceFromPoint (b.getStart(), p), a.getDistanceFromPoint (b.getEnd(), p),
                           b.getDistanceFromPoint (a.getStart(), p), b.getDistanceFromPoint (a.getEnd(), p) });
    };

    for (const auto& seg : segmentsOf (t))
        if (distance (swept, seg) <= radius)
            return true;
    return false;
}

std::uint32_t PitchCanvas::trajectoryAt (juce::Point<float> pos) const
{
    const auto& trajectories = processor.getPattern().trajectories;
    for (auto it = trajectories.rbegin(); it != trajectories.rend(); ++it) // topmost (last drawn) first
        if (sweepHits (*it, { pos, pos }, hitRadius))
            return it->id;
    return 0;
}

bool PitchCanvas::isSelected (std::uint32_t id) const
{
    return std::find (selection.begin(), selection.end(), id) != selection.end();
}

void PitchCanvas::selectAll()
{
    selection.clear();
    for (const auto& t : processor.getPattern().trajectories)
        selection.push_back (t.id);
    repaint();
}

void PitchCanvas::clearSelection()
{
    selection.clear();
    repaint();
}

void PitchCanvas::deleteSelection()
{
    if (selection.empty())
        return;
    auto pattern = processor.getPattern();
    std::erase_if (pattern.trajectories, [this] (const auto& t) { return isSelected (t.id); });
    selection.clear();
    processor.applyEdit (std::move (pattern));
    repaint();
}

//==============================================================================
void PitchCanvas::mouseDown (const juce::MouseEvent& e)
{
    dragStart = e.position;
    processor.beginGesture();

    if (tool == Tool::eraser || e.mods.isRightButtonDown())
    {
        drag = Drag::erasing;
        lastErasePos = e.position;
        eraseAlong (e.position, e.position, e.mods.isShiftDown());
        return;
    }

    if (tool == Tool::select)
    {
        const auto hit = trajectoryAt (e.position);
        if (hit != 0)
        {
            if (e.mods.isShiftDown() && isSelected (hit))
                std::erase (selection, hit);
            else if (e.mods.isShiftDown())
                selection.push_back (hit);
            else if (! isSelected (hit))
                selection = { hit };

            drag = isSelected (hit) ? Drag::moving : Drag::none;
            moveBase = processor.getPattern();
        }
        else
        {
            if (! e.mods.isShiftDown())
                selection.clear();
            drag = Drag::boxSelecting;
            boxBase = selection;
            box = {};
        }
        repaint();
        return;
    }

    drag = Drag::drawing;
    auto pattern = processor.getPattern();
    lastPoint = pointAt (e.position, e.mods.isShiftDown());
    drawingId = pattern.nextId();
    pattern.trajectories.push_back ({ drawingId, { lastPoint } });
    processor.setPattern (std::move (pattern));
    repaint();
}

void PitchCanvas::mouseDrag (const juce::MouseEvent& e)
{
    switch (drag)
    {
        case Drag::erasing:
            eraseAlong (lastErasePos, e.position, e.mods.isShiftDown());
            lastErasePos = e.position;
            hoverPos = e.position;
            break;

        case Drag::moving:
        {
            const auto area = plotArea();
            const double deltaBeats = (e.position.x - dragStart.x) / area.getWidth() * getVisibleBeats().getLength();
            double deltaPitch = -(e.position.y - dragStart.y) / area.getHeight() * (topPitch - bottomPitch);
            if (e.mods.isShiftDown())
                deltaPitch = std::round (deltaPitch); // keep intonation: move by whole semitones

            auto pattern = moveBase;
            pitchpaint::moveTrajectories (pattern, selection, deltaBeats, deltaPitch);
            processor.setPattern (std::move (pattern));
            repaint();
            break;
        }

        case Drag::boxSelecting:
        {
            box = juce::Rectangle<float> (dragStart, e.position);
            selection = boxBase;
            for (const auto& t : processor.getPattern().trajectories)
            {
                if (isSelected (t.id))
                    continue;
                for (const auto& seg : segmentsOf (t))
                    if (box.contains (seg.getStart()) || box.intersects (seg))
                    {
                        selection.push_back (t.id);
                        break;
                    }
            }
            repaint();
            break;
        }

        case Drag::drawing:
        {
            const auto point = pointAt (e.position, e.mods.isShiftDown());
            if (point.beat == lastPoint.beat && point.pitch == lastPoint.pitch)
                break;

            auto pattern = processor.getPattern();
            for (auto& t : pattern.trajectories)
                if (t.id == drawingId)
                    pitchpaint::drawSegment (t, lastPoint, point);

            lastPoint = point;
            processor.setPattern (std::move (pattern));
            repaint();
            break;
        }

        case Drag::none:
            break;
    }
}

void PitchCanvas::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::drawing)
    {
        // A click without a drag leaves a single point that can't sound: drop it.
        auto pattern = processor.getPattern();
        if (std::erase_if (pattern.trajectories, [this] (const auto& t) { return t.id == drawingId && ! t.isPlayable(); }) > 0)
            processor.setPattern (std::move (pattern));
        drawingId = 0;
    }

    processor.endGesture(); // one undo step for the whole stroke / move / erase swipe
    drag = Drag::none;
    box = {};
    repaint();
}

void PitchCanvas::eraseAlong (juce::Point<float> from, juce::Point<float> to, bool wholeStrokes)
{
    auto pattern = processor.getPattern();
    bool changed = false;

    if (wholeStrokes)
    {
        // Delete every trajectory the mouse swept over since the last event, so fast swipes don't skip strokes.
        changed = std::erase_if (pattern.trajectories, [&] (const auto& t) { return sweepHits (t, { from, to }, eraseRadius); }) > 0;
    }
    else
    {
        // Split eraser: walk the swept path in small steps; wherever a stroke is under the eraser circle, cut out the
        // time span the circle covers. Cutting a stroke in the middle leaves two strokes.
        const auto steps = juce::jmax (1, (int) std::ceil (from.getDistanceFrom (to) / 2.0f));
        for (int i = 0; i <= steps; ++i)
        {
            const auto pos = from + (to - from) * ((float) i / (float) steps);
            std::vector<std::uint32_t> hit;
            for (const auto& t : pattern.trajectories)
                if (sweepHits (t, { pos, pos }, eraseRadius))
                    hit.push_back (t.id);

            for (const auto id : hit)
                pitchpaint::cutTrajectory (pattern, id, xToBeat (pos.x - eraseRadius), xToBeat (pos.x + eraseRadius));
            changed |= ! hit.empty();
        }
    }

    if (changed)
    {
        processor.setPattern (std::move (pattern));
        repaint();
    }
}

void PitchCanvas::mouseMove (const juce::MouseEvent& e)
{
    hoverPos = e.position;
    if (tool == Tool::eraser)
        repaint();
}

void PitchCanvas::mouseExit (const juce::MouseEvent&)
{
    hoverPos.reset();
    repaint();
}

void PitchCanvas::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // wheel: scroll pitch | Shift+wheel or sideways wheel: scroll time | Ctrl+wheel: zoom time | Alt+wheel: zoom pitch
    const auto view = getVisibleBeats();
    const double length = processor.getPattern().lengthBeats;

    const auto setView = [&] (double start, double span)
    {
        span = juce::jlimit (0.5, length, span);
        showWholeLoop = span >= length;
        viewLength = span;
        viewStart = juce::jlimit (0.0, length - span, start);
    };

    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        const double anchor = xToBeat (e.position.x);
        const double newSpan = view.getLength() * (1.0 - wheel.deltaY * 0.5);
        const double frac = (anchor - view.getStart()) / view.getLength();
        setView (anchor - frac * juce::jlimit (0.5, length, newSpan), newSpan);
    }
    else if (e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
    {
        const float delta = wheel.deltaX != 0.0f ? -wheel.deltaX : -wheel.deltaY;
        setView (view.getStart() + delta * view.getLength() * 0.25, view.getLength());
    }
    else
    {
        double span = topPitch - bottomPitch;

        if (e.mods.isAltDown())
        {
            // Zoom pitch around the pitch under the mouse.
            const double anchor = pointAt (e.position, false).pitch;
            const double newSpan = juce::jlimit (6.0, 128.0, span * (1.0 - wheel.deltaY * 0.5));
            const double frac = (anchor - bottomPitch) / span;
            bottomPitch = anchor - frac * newSpan;
            topPitch = bottomPitch + newSpan;
        }
        else
        {
            const double shift = wheel.deltaY * span * 0.25;
            bottomPitch += shift;
            topPitch += shift;
        }

        // Keep the view inside the MIDI range.
        span = topPitch - bottomPitch;
        bottomPitch = juce::jlimit (-0.5, 127.5 - span, bottomPitch);
        topPitch = bottomPitch + span;
    }

    repaint();
}
