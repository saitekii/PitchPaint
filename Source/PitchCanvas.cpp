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
}

PitchCanvas::PitchCanvas (PitchPaintProcessor& p) : processor (p)
{
    setOpaque (true);
    startTimerHz (30);
}

juce::Rectangle<float> PitchCanvas::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (gutter);
}

float PitchCanvas::beatToX (double beat) const
{
    const auto area = plotArea();
    return area.getX() + (float) (beat / processor.getPattern().lengthBeats) * area.getWidth();
}

float PitchCanvas::pitchToY (double pitch) const
{
    const auto area = plotArea();
    return area.getY() + (float) ((topPitch - pitch) / (topPitch - bottomPitch)) * area.getHeight();
}

pitchpaint::PitchPoint PitchCanvas::pointAt (juce::Point<float> pos, bool snapToSemitone) const
{
    const auto area = plotArea();
    const double length = processor.getPattern().lengthBeats;
    const double beat = juce::jlimit (0.0, length, (double) (pos.x - area.getX()) / area.getWidth() * length);
    double pitch = topPitch - (double) (pos.y - area.getY()) / area.getHeight() * (topPitch - bottomPitch);
    pitch = juce::jlimit (0.0, 127.0, snapToSemitone ? std::round (pitch) : pitch);
    return { beat, pitch };
}

//==============================================================================
void PitchCanvas::paint (juce::Graphics& g)
{
    const auto& pattern = processor.getPattern();
    paintGrid (g, pattern);
    paintTrajectories (g, pattern);
    paintPlayhead (g, pattern);
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

    for (int beat = 0; beat <= (int) pattern.lengthBeats; ++beat)
    {
        g.setColour (beat % 4 == 0 ? colours::barLine : colours::beatLine);
        g.drawVerticalLine (juce::roundToInt (beatToX (beat)), area.getY(), area.getBottom());
    }
}

void PitchCanvas::paintTrajectories (juce::Graphics& g, const pitchpaint::Pattern& pattern)
{
    for (const auto& t : pattern.trajectories)
    {
        if (t.points.empty())
            continue;

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

        g.strokePath (path, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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
bool PitchCanvas::isErasing (const juce::MouseEvent& e) const
{
    return tool == Tool::eraser || e.mods.isRightButtonDown();
}

void PitchCanvas::mouseDown (const juce::MouseEvent& e)
{
    if (isErasing (e))
    {
        lastErasePos = e.position;
        eraseAlong (e.position, e.position);
        return;
    }

    auto pattern = processor.getPattern();
    lastPoint = pointAt (e.position, e.mods.isShiftDown());
    drawingId = pattern.nextId();
    pattern.trajectories.push_back ({ drawingId, { lastPoint } });
    processor.setPattern (std::move (pattern));
    repaint();
}

void PitchCanvas::mouseDrag (const juce::MouseEvent& e)
{
    if (isErasing (e))
    {
        eraseAlong (lastErasePos, e.position);
        lastErasePos = e.position;
        return;
    }

    if (drawingId == 0)
        return;

    const auto point = pointAt (e.position, e.mods.isShiftDown());
    if (point.beat == lastPoint.beat && point.pitch == lastPoint.pitch)
        return;

    auto pattern = processor.getPattern();
    for (auto& t : pattern.trajectories)
        if (t.id == drawingId)
            pitchpaint::drawSegment (t, lastPoint, point);

    lastPoint = point;
    processor.setPattern (std::move (pattern));
    repaint();
}

void PitchCanvas::mouseUp (const juce::MouseEvent&)
{
    if (drawingId == 0)
        return;

    // A click without a drag leaves a single point that can't sound: drop it.
    auto pattern = processor.getPattern();
    const auto removed = std::erase_if (pattern.trajectories, [this] (const pitchpaint::Trajectory& t)
                                        { return t.id == drawingId && ! t.isPlayable(); });
    if (removed > 0)
        processor.setPattern (std::move (pattern));

    drawingId = 0;
    repaint();
}

void PitchCanvas::eraseAlong (juce::Point<float> from, juce::Point<float> to)
{
    // Erase every trajectory the mouse swept over since the last event, so fast swipes don't skip strokes.
    const juce::Line<float> swept (from, to);

    const auto distance = [] (juce::Line<float> a, juce::Line<float> b)
    {
        juce::Point<float> p;
        if (a.intersects (b, p))
            return 0.0f;
        return std::min ({ a.getDistanceFromPoint (b.getStart(), p), a.getDistanceFromPoint (b.getEnd(), p),
                           b.getDistanceFromPoint (a.getStart(), p), b.getDistanceFromPoint (a.getEnd(), p) });
    };

    const auto hits = [&] (const pitchpaint::Trajectory& t)
    {
        for (size_t i = 0; i < t.points.size(); ++i)
        {
            const juce::Point<float> a { beatToX (t.points[i].beat), pitchToY (t.points[i].pitch) };
            const auto b = i + 1 < t.points.size()
                             ? juce::Point<float> { beatToX (t.points[i + 1].beat), pitchToY (t.points[i + 1].pitch) }
                             : a;
            if (distance (swept, { a, b }) <= eraseRadius)
                return true;
        }
        return false;
    };

    auto pattern = processor.getPattern();
    if (std::erase_if (pattern.trajectories, hits) > 0)
    {
        processor.setPattern (std::move (pattern));
        repaint();
    }
}

void PitchCanvas::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const double span = topPitch - bottomPitch;

    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        // Zoom around the pitch under the mouse.
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
    const double newSpan = topPitch - bottomPitch;
    bottomPitch = juce::jlimit (-0.5, 127.5 - newSpan, bottomPitch);
    topPitch = bottomPitch + newSpan;
    repaint();
}
