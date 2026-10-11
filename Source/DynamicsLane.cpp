#include "DynamicsLane.h"

using namespace theme;

namespace
{
    constexpr int headerHeight = controlHeight + space::s;

    juce::String strokes (int n) { return juce::String (n) + (n == 1 ? " stroke" : " strokes"); }
}

DynamicsLane::DynamicsLane (PitchPaintProcessor& p, PitchCanvas& c) : processor (p), canvas (c)
{
    setTitle ("Dynamics lane");
    setDescription ("Loudness over time for the selected strokes. Drag to draw; top is loudest.");

    title.setFont (juce::Font (type::emphasis()).withHeight (13.0f));
    target.setFont (juce::Font (type::caption()));
    target.setColour (juce::Label::textColourId, colour::textSecondary);
    sendLabel.setFont (juce::Font (type::caption()));
    sendLabel.setColour (juce::Label::textColourId, colour::textSecondary);
    sendLabel.setJustificationType (juce::Justification::centredRight);
    for (auto* l : { &title, &target, &sendLabel })
    {
        l->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (l);
    }

    const auto shape = [this] (juce::TextButton& b, pitchpaint::DynamicsShape s, const juce::String& tip)
    {
        b.onClick = [this, s] { applyShape (s); };
        b.setTooltip (tip);
        b.setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (b);
    };
    shape (swellButton, pitchpaint::DynamicsShape::swell, "Get louder over each selected stroke.");
    shape (fadeButton, pitchpaint::DynamicsShape::fade, "Get quieter over each selected stroke.");
    shape (hairpinButton, pitchpaint::DynamicsShape::hairpin, "Swell to the middle, then fade.");
    shape (accentButton, pitchpaint::DynamicsShape::accent, "Strong start that settles.");
    clearButton.onClick = [this] { clearDynamics(); };
    clearButton.setTooltip ("Remove the dynamics of the selected strokes (back to full level).");
    clearButton.setMouseClickGrabsKeyboardFocus (false);
    addAndMakeVisible (clearButton);

    sendBox.addItemList (processor.params.getParameter ("dynamics")->getAllValueStrings(), 1);
    sendAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.params, "dynamics", sendBox);
    sendBox.setTitle ("Dynamics output");
    sendBox.setTooltip ("Which MIDI control carries the dynamics. Pressure is the MPE standard. Your synth's patch must "
                        "use it (for example for volume) to hear a change; the Preview sound follows Pressure, CC11 and CC7.");
    sendBox.setMouseClickGrabsKeyboardFocus (false);
    sendLabel.attachToComponent (&sendBox, true);
    addAndMakeVisible (sendBox);

    startTimerHz (30);
}

//==============================================================================
juce::Rectangle<float> DynamicsLane::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedTop ((float) headerHeight).withTrimmedLeft (PitchCanvas::gutter).reduced (0.0f, 4.0f);
}

float DynamicsLane::beatToX (double beat) const
{
    // Same mapping as the canvas above (same width and gutter), so curves line up with their strokes.
    const auto area = plotArea();
    const auto view = canvas.getVisibleBeats();
    return area.getX() + (float) ((beat - view.getStart()) / view.getLength()) * area.getWidth();
}

double DynamicsLane::xToBeat (float x) const
{
    const auto area = plotArea();
    const auto view = canvas.getVisibleBeats();
    return view.getStart() + (double) (x - area.getX()) / area.getWidth() * view.getLength();
}

float DynamicsLane::valueToY (double value) const
{
    const auto area = plotArea();
    return area.getBottom() - (float) value * area.getHeight();
}

double DynamicsLane::yToValue (float y) const
{
    const auto area = plotArea();
    return juce::jlimit (0.0, 1.0, (double) (area.getBottom() - y) / area.getHeight());
}

void DynamicsLane::report (const juce::String& message)
{
    if (onStatus)
        onStatus (message);
}

juce::String DynamicsLane::ensureOutputOn()
{
    auto* param = processor.params.getParameter ("dynamics");
    if (param == nullptr || param->getValue() > 0.0f)
        return {};
    // First use: send it the conventional way for the output mode, and say so.
    const bool mpe = processor.params.getRawParameterValue ("mode")->load() < 0.5f;
    const int index = mpe ? (int) pitchpaint::DynamicsOutput::pressure : (int) pitchpaint::DynamicsOutput::cc11;
    param->setValueNotifyingHost (param->convertTo0to1 ((float) index));
    // Measured: many synth patches ignore these controls (Surge XT's default patch does), so say what's needed.
    return mpe ? " Sending it as MPE pressure (change under Send); your synth patch must use pressure to hear it."
               : " Sending it as CC11 expression (change under Send); your synth patch must use CC11 to hear it.";
}

//==============================================================================
void DynamicsLane::applyShape (pitchpaint::DynamicsShape shape)
{
    const auto& selection = canvas.getSelection();
    if (selection.empty())
    {
        report ("Select strokes first (Select tool, S), then pick a shape or draw their dynamics.");
        return;
    }
    auto pattern = processor.getPattern();
    for (auto& t : pattern.trajectories)
        if (std::find (selection.begin(), selection.end(), t.id) != selection.end() && t.isPlayable())
            t.dynamics = pitchpaint::makeDynamicsShape (shape, t.endBeat() - t.startBeat());
    processor.applyEdit (std::move (pattern));
    const auto note = ensureOutputOn();
    report ("Dynamics on " + strokes ((int) selection.size()) + "." + note + " Ctrl+Z to undo.");
    repaint();
}

void DynamicsLane::clearDynamics()
{
    const auto& selection = canvas.getSelection();
    if (selection.empty())
    {
        report ("Select strokes first (Select tool, S) to clear their dynamics.");
        return;
    }
    auto pattern = processor.getPattern();
    for (auto& t : pattern.trajectories)
        if (std::find (selection.begin(), selection.end(), t.id) != selection.end())
            t.dynamics = {};
    processor.applyEdit (std::move (pattern));
    report ("Dynamics cleared on " + strokes ((int) selection.size()) + " (full level). Ctrl+Z to undo.");
    repaint();
}

//==============================================================================
void DynamicsLane::drawTo (juce::Point<float> pos, bool first)
{
    const pitchpaint::EnvelopePoint point { xToBeat (pos.x), yToValue (pos.y) };
    const auto& selection = canvas.getSelection();
    auto pattern = processor.getPattern();

    for (auto& t : pattern.trajectories)
    {
        if (std::find (selection.begin(), selection.end(), t.id) == selection.end() || ! t.isPlayable())
            continue;
        const double start = t.startBeat(), end = t.endBeat(), length = end - start;
        const bool inside = point.beat >= start && point.beat <= end;
        const bool lastInside = lastPoint.beat >= start && lastPoint.beat <= end;
        const bool crosses = ! first && (lastPoint.beat < start) != (point.beat < start); // swept over the stroke's span
        if (first ? ! inside : (! inside && ! lastInside && ! crosses))
            continue; // this stroke isn't under the pen

        // Undrawn parts of a stroke stay at full level.
        if (t.dynamics.empty())
            t.dynamics.points = { { 0.0, 1.0 }, { length, 1.0 } };

        const auto relative = [&] (pitchpaint::EnvelopePoint e)
        { return pitchpaint::EnvelopePoint { juce::jlimit (0.0, length, e.beat - start), e.value }; };
        const auto from = first ? relative (point) : relative (lastPoint);
        pitchpaint::drawEnvelopeSegment (t.dynamics, from, relative (point));
    }

    lastPoint = point;
    processor.setPattern (std::move (pattern));
    repaint();
}

void DynamicsLane::mouseDown (const juce::MouseEvent& e)
{
    if (! plotArea().contains (e.position))
        return;
    if (canvas.getSelection().empty())
    {
        report ("Select strokes first (Select tool, S), then draw their dynamics here.");
        return;
    }
    drawing = true;
    processor.beginGesture(); // the whole drag is one undo step
    drawTo (e.position, true);
}

void DynamicsLane::mouseDrag (const juce::MouseEvent& e)
{
    if (drawing)
        drawTo (e.position, false);
}

void DynamicsLane::mouseUp (const juce::MouseEvent&)
{
    if (! drawing)
        return;
    drawing = false;
    processor.endGesture();
    const auto note = ensureOutputOn();
    report ("Dynamics drawn on " + strokes ((int) canvas.getSelection().size()) + "." + note + " Ctrl+Z to undo.");
}

//==============================================================================
void DynamicsLane::timerCallback()
{
    const auto view = canvas.getVisibleBeats();
    if (processor.getPatternVersion() != lastVersion || canvas.getSelection() != lastSelection || view != lastView)
    {
        lastVersion = processor.getPatternVersion();
        lastSelection = canvas.getSelection();
        lastView = view;
        const auto count = (int) lastSelection.size();
        target.setText (count == 0 ? juce::String ("select strokes to shape their loudness")
                                   : "for " + strokes (count) + " - drag to draw, top = loud",
                        juce::dontSendNotification);
        for (auto* b : { &swellButton, &fadeButton, &hairpinButton, &accentButton, &clearButton })
            b->setEnabled (count > 0);
        repaint();
    }
}

void DynamicsLane::resized()
{
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (space::s, space::xs);
    title.setBounds (header.removeFromLeft (76));
    sendBox.setBounds (header.removeFromRight (150));
    header.removeFromRight (44); // "Send" label, attached to the box
    for (auto* b : { &clearButton, &accentButton, &hairpinButton, &fadeButton, &swellButton })
    {
        b->setBounds (header.removeFromRight (b == &hairpinButton ? 68 : 58));
        header.removeFromRight (space::xs);
    }
    target.setBounds (header);
}

void DynamicsLane::paint (juce::Graphics& g)
{
    g.fillAll (colour::surface);
    const auto area = plotArea();
    g.setColour (colour::canvas);
    g.fillRect (area.withX (PitchCanvas::gutter).withWidth ((float) getWidth() - PitchCanvas::gutter));

    // Level guides and labels.
    g.setFont (juce::Font (type::caption()).withHeight (11.0f));
    for (const double v : { 0.0, 0.5, 1.0 })
    {
        g.setColour (v == 0.5 ? colour::gridSemitone : colour::gridOctave);
        g.drawHorizontalLine (juce::roundToInt (valueToY (v)), area.getX(), area.getRight());
    }
    g.setColour (colour::textSecondary);
    g.drawText ("loud", juce::Rectangle<float> (0.0f, area.getY() - 2.0f, PitchCanvas::gutter - 6.0f, 12.0f), juce::Justification::centredRight);
    g.drawText ("soft", juce::Rectangle<float> (0.0f, area.getBottom() - 10.0f, PitchCanvas::gutter - 6.0f, 12.0f), juce::Justification::centredRight);

    // Bar lines, matching the canvas.
    const auto view = canvas.getVisibleBeats();
    const auto& pattern = processor.getPattern();
    for (int bar = (int) std::ceil (view.getStart() / 4.0); bar * 4.0 <= std::min (view.getEnd(), pattern.lengthBeats); ++bar)
    {
        g.setColour (colour::gridBar);
        g.drawVerticalLine (juce::roundToInt (beatToX (bar * 4.0)), area.getY(), area.getBottom());
    }

    // Each stroke's level over its span: selected ones bright with a filled area, others faint.
    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (area.toNearestInt());
    const auto& selection = canvas.getSelection();
    for (const auto& t : pattern.trajectories)
    {
        if (! t.isPlayable())
            continue;
        const bool selected = std::find (selection.begin(), selection.end(), t.id) != selection.end();
        if (! selected && t.dynamics.empty())
            continue;

        juce::Path line;
        const float x0 = beatToX (t.startBeat()), x1 = beatToX (t.endBeat());
        const int steps = juce::jlimit (2, 2048, (int) ((x1 - x0) / 3.0f));
        for (int i = 0; i <= steps; ++i)
        {
            const double beat = t.startBeat() + (t.endBeat() - t.startBeat()) * i / steps;
            const juce::Point<float> pt { beatToX (beat), valueToY (pitchpaint::dynamicsAt (t, beat)) };
            if (i == 0)
                line.startNewSubPath (pt);
            else
                line.lineTo (pt);
        }

        const auto c = colour::forStroke (t.id);
        if (selected)
        {
            juce::Path fill (line);
            fill.lineTo (x1, area.getBottom());
            fill.lineTo (x0, area.getBottom());
            fill.closeSubPath();
            g.setColour (c.withAlpha (0.22f));
            g.fillPath (fill);
        }
        g.setColour (selected ? c : c.withAlpha (0.4f));
        g.strokePath (line, juce::PathStrokeType (selected ? 2.0f : 1.0f));
    }
}
