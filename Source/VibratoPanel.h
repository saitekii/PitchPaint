#pragma once

#include "Core/Pattern.h"
#include "Theme.h"

// Pop-up panel (shown in a call-out from the toolbar) for the vibrato of the selected strokes, or of new strokes.
class VibratoPanel final : public juce::Component
{
public:
    enum class Target { selection, newStrokes };

    VibratoPanel (pitchpaint::Vibrato initial, Target, const juce::String& heading);

    // live = true while a slider is being dragged (the caller wraps the drag in one undo step).
    std::function<void (pitchpaint::Vibrato, bool live)> onChange;
    std::function<void()> onGestureStart, onGestureEnd;
    std::function<void (pitchpaint::Vibrato)> onApplyToAll; // newStrokes target: "Apply to all strokes"

    // Ready-made vibrato for the "Add vibrato" button: clearly audible, natural speed.
    static constexpr double suggestedDepthCents = 40.0;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    pitchpaint::Vibrato current() const;
    void updateHint();
    void setUp (juce::Slider&, juce::Label&, double min, double max, double step, const juce::String& suffix, double value,
                const juce::String& title, const juce::String& tooltip);

    Target target;
    juce::Label heading, hint;
    juce::Label depthLabel { {}, "Depth" }, rateLabel { {}, "Rate" }, fadeLabel { {}, "Fade-in" };
    juce::Slider depth, rate, fade;
    juce::TextButton offButton { "Off" }, actionButton;
    bool dragging = false;
};
