#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Time (beats, left to right) versus continuous pitch (bottom to top) drawing surface.
class PitchCanvas final : public juce::Component, private juce::Timer
{
public:
    enum class Tool { pencil, eraser };

    explicit PitchCanvas (PitchPaintProcessor&);

    // Called (message thread) when the pattern was replaced from outside the canvas, e.g. by loading state.
    std::function<void()> onPatternChanged;

    void setTool (Tool t) { tool = t; }
    Tool getTool() const { return tool; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    static constexpr float gutter = 40.0f; // note-name column on the left

    void timerCallback() override;

    juce::Rectangle<float> plotArea() const;
    float beatToX (double beat) const;
    float pitchToY (double pitch) const;
    pitchpaint::PitchPoint pointAt (juce::Point<float>, bool snapToSemitone) const;

    void paintGrid (juce::Graphics&, const pitchpaint::Pattern&);
    void paintTrajectories (juce::Graphics&, const pitchpaint::Pattern&);
    void paintPlayhead (juce::Graphics&, const pitchpaint::Pattern&);

    void eraseAlong (juce::Point<float> from, juce::Point<float> to);
    bool isErasing (const juce::MouseEvent&) const;

    PitchPaintProcessor& processor;
    Tool tool = Tool::pencil;

    double bottomPitch = 36.0; // visible pitch range
    double topPitch = 96.0;

    std::uint32_t drawingId = 0;    // trajectory being drawn, 0 when not drawing
    pitchpaint::PitchPoint lastPoint {};
    juce::Point<float> lastErasePos;

    double lastPaintedPlayhead = -1.0;
    int lastSeenVersion = -1;
};
