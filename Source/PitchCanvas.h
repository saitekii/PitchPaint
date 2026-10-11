#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Time (beats, left to right) versus continuous pitch (bottom to top) drawing surface.
class PitchCanvas final : public juce::Component, private juce::Timer
{
public:
    enum class Tool { pencil, select, eraser };

    explicit PitchCanvas (PitchPaintProcessor&);

    // Called (message thread) whenever the pattern changed, including from outside the canvas (undo, state load).
    std::function<void()> onPatternChanged;

    void setTool (Tool t) { tool = t; }
    Tool getTool() const { return tool; }

    void selectAll();
    void clearSelection();
    void deleteSelection();
    const std::vector<std::uint32_t>& getSelection() const { return selection; }

    // Visible time range in beats (the whole loop unless zoomed in with Ctrl+wheel).
    juce::Range<double> getVisibleBeats() const;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    static constexpr float gutter = 40.0f; // note-name column on the left

    enum class Drag { none, drawing, erasing, moving, boxSelecting };

    void timerCallback() override;

    juce::Rectangle<float> plotArea() const;
    float beatToX (double beat) const;
    double xToBeat (float x) const; // unclamped
    float pitchToY (double pitch) const;
    pitchpaint::PitchPoint pointAt (juce::Point<float>, bool snapToSemitone) const;

    void paintGrid (juce::Graphics&, const pitchpaint::Pattern&);
    void paintTrajectories (juce::Graphics&, const pitchpaint::Pattern&);
    void paintPlayhead (juce::Graphics&, const pitchpaint::Pattern&);

    std::vector<juce::Line<float>> segmentsOf (const pitchpaint::Trajectory&) const;
    bool sweepHits (const pitchpaint::Trajectory&, juce::Line<float> swept, float radius) const;
    std::uint32_t trajectoryAt (juce::Point<float>) const; // 0 if none
    bool isSelected (std::uint32_t id) const;
    void eraseAlong (juce::Point<float> from, juce::Point<float> to, bool wholeStrokes);

    PitchPaintProcessor& processor;
    Tool tool = Tool::pencil;

    double bottomPitch = 36.0; // visible pitch range
    double topPitch = 96.0;
    bool showWholeLoop = true;      // visible time range; follows loop-length changes while showing the whole loop
    double viewStart = 0.0, viewLength = 16.0;
    std::optional<juce::Point<float>> hoverPos; // for the eraser circle

    Drag drag = Drag::none;
    juce::Point<float> dragStart;
    std::uint32_t drawingId = 0;     // trajectory being drawn
    pitchpaint::PitchPoint lastPoint {};
    juce::Point<float> lastErasePos;
    pitchpaint::Pattern moveBase;    // pattern at the start of a move; moves are applied to it, not accumulated
    std::vector<std::uint32_t> selection, boxBase;
    juce::Rectangle<float> box;

    double lastPaintedPlayhead = -1.0;
    int lastSeenVersion = -1;
};
