#include "PitchCanvas.h"
#include "LookAndFeel.h"

namespace
{
    namespace colour = theme::colour;

    juce::String strokes (int n) { return juce::String (n) + (n == 1 ? " stroke" : " strokes"); }

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
    setTitle ("Pitch canvas");
    setHelpText ("Time runs left to right, pitch bottom to top. Pencil: drag to draw. Select: click or drag a box, "
                 "then drag or use the arrow keys to move. Eraser: drag across strokes.");
    updateAccessibleDescription();
    startTimerHz (30);
}

void PitchCanvas::updateAccessibleDescription()
{
    const auto& p = processor.getPattern();
    const int bars = juce::roundToInt (p.lengthBeats / 4.0);
    setDescription (strokes ((int) p.trajectories.size()) + " in a " + juce::String (bars) + "-bar"
                    + " loop. " + juce::String ((int) selection.size()) + " selected.");
}

void PitchCanvas::focusGained (FocusChangeType cause)
{
    focusFromKeyboard = cause == focusChangedByTabKey || cause == focusChangedDirectly;
    repaint();
}

void PitchCanvas::focusLost (FocusChangeType)
{
    focusFromKeyboard = false;
    repaint();
}

void PitchCanvas::report (const juce::String& message)
{
    if (onStatus)
        onStatus (message);
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
        g.setColour (colour::textPrimary.withAlpha (0.6f));
        g.drawEllipse (juce::Rectangle<float> (2.0f * eraseRadius, 2.0f * eraseRadius).withCentre (*hoverPos), 1.0f);
    }

    paintStatus (g, pattern);

    if (focusFromKeyboard && hasKeyboardFocus (false))
        PitchPaintLookAndFeel::drawFocusRing (g, plotArea());
}

void PitchCanvas::paintStatus (juce::Graphics& g, const pitchpaint::Pattern& pattern)
{
    const auto area = plotArea();

    // Transport state, so it's clear why nothing plays while the host is stopped.
    const bool playing = processor.isPlaying();
    const auto chipText = playing ? juce::String ("Playing") : juce::String ("Stopped - press Play in your DAW");
    const juce::Font chipFont (theme::type::caption());
    const float chipW = juce::GlyphArrangement::getStringWidth (chipFont, chipText) + 2.0f * theme::space::m + 10.0f;
    const auto chip = juce::Rectangle<float> (area.getRight() - chipW - theme::space::s, area.getY() + theme::space::s, chipW, 22.0f);
    g.setColour (colour::surface.withAlpha (0.9f));
    g.fillRoundedRectangle (chip, 11.0f);
    // Shape as well as colour: a filled triangle for playing, a square for stopped.
    const auto icon = chip.withWidth (10.0f).withSizeKeepingCentre (8.0f, 8.0f).translated ((float) theme::space::m, 0.0f);
    g.setColour (playing ? colour::accent : colour::textSecondary);
    if (playing)
    {
        juce::Path tri;
        tri.addTriangle (icon.getTopLeft(), icon.getBottomLeft(), { icon.getRight(), icon.getCentreY() });
        g.fillPath (tri);
    }
    else
    {
        g.fillRect (icon);
    }
    g.setColour (colour::textSecondary);
    g.setFont (chipFont);
    g.drawText (chipText, chip.withTrimmedLeft ((float) theme::space::m + 14.0f), juce::Justification::centredLeft);

    if (! pattern.trajectories.empty())
        return;

    // Empty state: what this screen is and what to do first.
    const auto message = area.withSizeKeepingCentre (juce::jmin (460.0f, area.getWidth() - 2.0f * theme::space::xl), 84.0f);
    g.setColour (colour::textPrimary);
    g.setFont (juce::Font (theme::type::emptyStateTitle()));
    g.drawText ("Draw a pitch curve", message.withHeight (28.0f), juce::Justification::centred);
    g.setColour (colour::textSecondary);
    g.setFont (juce::Font (theme::type::body()));
    g.drawFittedText ("Drag with the Pencil anywhere on the grid. Pitch is continuous; hold Shift to snap to semitones.",
                      message.withTrimmedTop (34.0f).toNearestInt(), juce::Justification::centredTop, 2);
}

double PitchCanvas::gridStep (const juce::ModifierKeys& mods) const
{
    return mods.isAltDown() ? 0.0 : processor.getGridBeats(); // Alt: ignore the grid for this action
}

void PitchCanvas::nudgeSelection (double deltaBeats, double deltaPitch)
{
    if (selection.empty())
        return;
    auto pattern = processor.getPattern();
    pitchpaint::moveTrajectories (pattern, selection, deltaBeats, deltaPitch);
    processor.applyEdit (std::move (pattern));
    repaint();
}

void PitchCanvas::paintGrid (juce::Graphics& g, const pitchpaint::Pattern& pattern)
{
    const auto area = plotArea();
    g.fillAll (colour::canvas);

    const float pxPerSemitone = area.getHeight() / (float) (topPitch - bottomPitch);
    const bool labelEveryNote = pxPerSemitone >= 14.0f;

    g.setColour (colour::surface);
    g.fillRect (getLocalBounds().toFloat().withWidth (gutter));
    g.setFont (juce::FontOptions (11.0f));

    // A horizontal line sits exactly on each semitone: drawing along it is in tune.
    for (int n = (int) std::ceil (bottomPitch - 0.5); n <= (int) std::floor (topPitch + 0.5); ++n)
    {
        const float y = pitchToY (n);

        if (isBlackKey (n))
        {
            g.setColour (colour::blackKeyRow);
            g.fillRect (juce::Rectangle<float> (area.getX(), y - pxPerSemitone * 0.5f, area.getWidth(), pxPerSemitone));
        }

        const bool isC = n % 12 == 0;
        g.setColour (isC ? colour::gridOctave : colour::gridSemitone);
        g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());

        if (isC || labelEveryNote)
        {
            g.setColour (isC ? colour::textSecondary : colour::textSecondary.withAlpha (0.6f));
            g.drawText (juce::MidiMessage::getMidiNoteName (n, true, true, 4),
                        juce::Rectangle<float> (0.0f, y - 7.0f, gutter - 6.0f, 14.0f), juce::Justification::centredRight);
        }
    }

    // Vertical lines: bars, beats, and quarter beats once zoomed in far enough to see them.
    const auto view = getVisibleBeats();
    const double pxPerBeat = area.getWidth() / view.getLength();
    // With snapping on, minor lines show the snap grid (when they're at least 8 px apart).
    const double grid = processor.getGridBeats();
    const double step = grid > 0.0 && grid * pxPerBeat >= 8.0 ? grid
                                                              : (pxPerBeat >= 60.0 ? 0.25 : (pxPerBeat >= 10.0 ? 1.0 : 4.0));
    const double lastBeat = std::min (view.getEnd(), pattern.lengthBeats);

    for (auto k = (long long) std::ceil (view.getStart() / step); k * step <= lastBeat + 1.0e-9; ++k)
    {
        const double beat = k * step;
        const auto onMultiple = [beat] (double m) { const double r = std::fmod (beat, m); return r < 1e-6 || m - r < 1e-6; };
        const bool isBar = onMultiple (4.0);
        const bool isBeat = onMultiple (1.0);
        g.setColour (isBar ? colour::gridBar : (isBeat ? colour::gridBeat : colour::gridBeat.withAlpha (0.5f)));
        const int x = juce::roundToInt (beatToX (beat));
        g.drawVerticalLine (x, area.getY(), area.getBottom());

        if (isBar && beat < pattern.lengthBeats)
        {
            g.setColour (colour::textSecondary);
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
        g.setColour (colour::forStroke (t.id));

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
            g.setColour (colour::selection.withAlpha (0.85f));
            g.strokePath (path, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (colour::forStroke (t.id));
        }
        g.strokePath (path, juce::PathStrokeType (selected ? 3.0f : 2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (! box.isEmpty())
    {
        g.setColour (colour::selection.withAlpha (0.08f));
        g.fillRect (box);
        g.setColour (colour::selection.withAlpha (0.5f));
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

    g.setColour (colour::playhead.withAlpha (0.7f));
    g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());

    // Mark where each sounding trajectory is right now.
    for (const auto& t : pattern.trajectories)
    {
        if (! t.isPlayable() || beat < t.startBeat() || beat >= t.endBeat())
            continue;

        const juce::Point<float> p { x, pitchToY (t.pitchAt (beat)) };
        g.setColour (colour::forStroke (t.id));
        g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (p));
        g.setColour (colour::playhead);
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
        updateAccessibleDescription();
        repaint();
    }

    if (const bool playing = processor.isPlaying(); playing != lastPaintedPlaying)
    {
        lastPaintedPlaying = playing;
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
    const auto removed = std::erase_if (pattern.trajectories, [this] (const auto& t) { return isSelected (t.id); });
    selection.clear();
    processor.applyEdit (std::move (pattern));
    report ("Deleted " + strokes ((int) removed) + ". Ctrl+Z to undo.");
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
        erasedIds.clear();
        erasedWhole = 0;
        lastErasePos = e.position;
        eraseGrid = gridStep (e.mods);
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
    if (const double grid = gridStep (e.mods); grid > 0.0)
        lastPoint.beat = juce::jlimit (0.0, processor.getPattern().lengthBeats, pitchpaint::snapBeat (lastPoint.beat, grid));
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
            double deltaBeats = (e.position.x - dragStart.x) / area.getWidth() * getVisibleBeats().getLength();
            if (const double grid = gridStep (e.mods); grid > 0.0)
            {
                // Snap the selection's start, not the mouse offset, so off-grid strokes land on the grid.
                double start = std::numeric_limits<double>::max();
                for (const auto& t : moveBase.trajectories)
                    if (isSelected (t.id) && ! t.points.empty())
                        start = std::min (start, t.startBeat());
                if (start < std::numeric_limits<double>::max())
                    deltaBeats = pitchpaint::snapBeat (start + deltaBeats, grid) - start;
            }
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

void PitchCanvas::mouseUp (const juce::MouseEvent& e)
{
    if (drag == Drag::drawing)
    {
        auto pattern = processor.getPattern();
        if (const double grid = gridStep (e.mods); grid > 0.0)
        {
            // Start and end on grid lines, so the note length is whole grid steps. A click becomes a one-step note.
            for (auto& t : pattern.trajectories)
                if (t.id == drawingId)
                    pitchpaint::snapEnds (t, grid, pattern.lengthBeats);
        }
        else
        {
            // Without a grid, a click without a drag leaves a single point that can't sound: drop it.
            std::erase_if (pattern.trajectories, [this] (const auto& t) { return t.id == drawingId && ! t.isPlayable(); });
        }
        processor.setPattern (std::move (pattern));
        drawingId = 0;
    }

    if (drag == Drag::erasing && erasedWhole > 0)
        report ("Deleted " + strokes (erasedWhole) + ". Ctrl+Z to undo.");
    else if (drag == Drag::erasing && ! erasedIds.empty())
        report ("Erased part of " + strokes ((int) erasedIds.size()) + ". Ctrl+Z to undo.");

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
        const auto removed = std::erase_if (pattern.trajectories, [&] (const auto& t) { return sweepHits (t, { from, to }, eraseRadius); });
        erasedWhole += (int) removed;
        changed = removed > 0;
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
            {
                double cutFrom = xToBeat (pos.x - eraseRadius), cutTo = xToBeat (pos.x + eraseRadius);
                if (eraseGrid > 0.0) // cut whole grid cells so the remaining pieces stay on the grid
                {
                    cutFrom = std::floor (cutFrom / eraseGrid + 1e-9) * eraseGrid;
                    cutTo = std::ceil (cutTo / eraseGrid - 1e-9) * eraseGrid;
                }
                pitchpaint::cutTrajectory (pattern, id, cutFrom, cutTo);
                if (std::find (erasedIds.begin(), erasedIds.end(), id) == erasedIds.end())
                    erasedIds.push_back (id);
            }
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
