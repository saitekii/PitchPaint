#pragma once

#include "Core/Pattern.h"
#include "Theme.h"

// Pop-up panel (shown in a call-out from the toolbar) for the vibrato of the selected strokes, or of new strokes.
class VibratoPanel final : public juce::Component
{
public:
    VibratoPanel (pitchpaint::Vibrato initial, const juce::String& heading);

    // live = true while a slider is being dragged (the caller wraps the drag in one undo step).
    std::function<void (pitchpaint::Vibrato, bool live)> onChange;
    std::function<void()> onGestureStart, onGestureEnd;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    pitchpaint::Vibrato current() const;
    void setUp (juce::Slider&, juce::Label&, double min, double max, double step, const juce::String& suffix, double value,
                const juce::String& title, const juce::String& tooltip);

    juce::Label heading;
    juce::Label depthLabel { {}, "Depth" }, rateLabel { {}, "Rate" }, fadeLabel { {}, "Fade-in" };
    juce::Slider depth, rate, fade;
    juce::TextButton offButton { "Off" };
    bool dragging = false;
};
