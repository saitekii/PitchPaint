#pragma once

#include "PitchCanvas.h"
#include "PluginProcessor.h"
#include "Theme.h"

// Lane under the canvas for the dynamics (loudness over time) of the selected strokes. Shares the canvas's time axis.
class DynamicsLane final : public juce::Component, private juce::Timer
{
public:
    DynamicsLane (PitchPaintProcessor&, PitchCanvas&);

    std::function<void (const juce::String&)> onStatus; // feedback for the footer

    static constexpr int preferredHeight = 108;

    void applyShape (pitchpaint::DynamicsShape);
    void clearDynamics();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Rectangle<float> plotArea() const;
    float beatToX (double beat) const;
    double xToBeat (float x) const;
    float valueToY (double value) const;
    double yToValue (float y) const;
    juce::String ensureOutputOn(); // first use: switch Send on; returns a note for the status message
    void report (const juce::String&);
    void drawTo (juce::Point<float>, bool first);

    PitchPaintProcessor& processor;
    PitchCanvas& canvas;

    juce::Label title { {}, "Dynamics" }, target, sendLabel { {}, "Send" };
    juce::TextButton swellButton { "Swell" }, fadeButton { "Fade" }, hairpinButton { "Hairpin" }, accentButton { "Accent" },
        clearButton { "Clear" };
    juce::ComboBox sendBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sendAttachment;

    bool drawing = false;
    pitchpaint::EnvelopePoint lastPoint {}; // absolute beat
    int lastVersion = -1;
    std::vector<std::uint32_t> lastSelection;
    juce::Range<double> lastView;
};
