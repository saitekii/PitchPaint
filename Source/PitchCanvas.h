#pragma once

#include "PluginProcessor.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Time (beats, left to right) versus continuous pitch (bottom to top) drawing surface.
class PitchCanvas final : public juce::Component, private juce::Timer
{
public:
    enum class Tool { pencil, line, select, eraser };

    explicit PitchCanvas (PitchPaintProcessor&);

    // Called (message thread) whenever the pattern changed, including from outside the canvas (undo, state load).
    std::function<void()> onPatternChanged;

    // Called (message thread, from the timer) after the selection changed in any way.
    std::function<void()> onSelectionChanged;

    // Short feedback after an edit (shown in the footer and announced to screen readers).
    std::function<void (const juce::String&)> onStatus;

    void setTool (Tool);                 // switching away from Line finishes the line being drawn
    Tool getTool() const { return tool; }

    void selectAll();
    void clearSelection();
    void deleteSelection();
    void nudgeSelection (double deltaBeats, double deltaPitch); // keyboard moves, one undo step each
    void smoothSelection();                                      // one more step of smoothing, one undo step
    void setSelectionVibrato (pitchpaint::Vibrato, bool live);   // live: no undo step (caller wraps a gesture)

    // Clipboard (system clipboard, so it works across PitchPaint instances). Each edit is one undo step.
    bool copySelection();
    void cutSelection();
    void paste();              // at the mouse if over the canvas, else at the original time
    void duplicateSelection(); // right after the selection
    static bool clipboardHasStrokes();

    // Line tool: clicks add points; finish with double-click, Enter or right-click; Esc cancels.
    bool isDrawingLine() const { return lineId != 0; }
    void finishLine();
    void cancelLine();
    void setSelectionVelocity (int velocity, bool live);        // live: no undo step (caller wraps a gesture)
    static float strokeWidth (int velocity);                    // line thickness shows velocity

    // Time-grid step for an action with these modifiers: 0 when snapping is off or Alt is held.
    double gridStep (const juce::ModifierKeys&) const;
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
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;

    static constexpr float gutter = 40.0f; // note-name column on the left (the dynamics lane lines up with it)

private:

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
    void paintStatus (juce::Graphics&, const pitchpaint::Pattern&);
    void updateAccessibleDescription();
    void report (const juce::String&);

    std::vector<juce::Line<float>> segmentsOf (const pitchpaint::Trajectory&) const;
    std::vector<juce::Point<float>> screenPoints (const pitchpaint::Trajectory&) const; // as heard (magnet applied)
    pitchpaint::PitchPoint linePointAt (juce::Point<float>, const juce::ModifierKeys&) const;
    void lineClick (const juce::MouseEvent&);
    void insert (const std::vector<pitchpaint::Trajectory>&, double deltaBeats, const juce::String& verb);
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
    std::optional<juce::Point<float>> hoverPos; // for the eraser circle and the line preview
    juce::ModifierKeys hoverMods;
    std::uint32_t lineId = 0;                   // line being built with the Line tool
    double smoothedPitch = 0.0;                 // pencil smoothing state
    juce::Point<float> lastDrawPos;

    Drag drag = Drag::none;
    juce::Point<float> dragStart;
    std::uint32_t drawingId = 0;     // trajectory being drawn
    pitchpaint::PitchPoint lastPoint {};
    juce::Point<float> lastErasePos;
    pitchpaint::Pattern moveBase;    // pattern at the start of a move; moves are applied to it, not accumulated
    std::vector<std::uint32_t> selection, boxBase, lastNotifiedSelection;
    juce::Rectangle<float> box;

    double lastPaintedPlayhead = -1.0;
    bool lastPaintedPlaying = false;
    bool focusFromKeyboard = false;          // focus ring only for keyboard focus, not after clicks
    std::vector<std::uint32_t> erasedIds;   // strokes touched by the current erase gesture (for feedback)
    int erasedWhole = 0;
    double eraseGrid = 0.0;                 // grid step for the current erase gesture (0 = free)
    int lastSeenVersion = -1;
};
