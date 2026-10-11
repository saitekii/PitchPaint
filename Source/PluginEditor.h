#pragma once

#include "PitchCanvas.h"
#include "PluginProcessor.h"

class PitchPaintEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PitchPaintEditor (PitchPaintProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void selectTool (PitchCanvas::Tool);
    void setLoopBars (int bars);

    PitchPaintProcessor& processor;
    PitchCanvas canvas;

    juce::TextButton pencilButton { "Pencil" }, eraserButton { "Eraser" };
    juce::Label lengthLabel { {}, "Loop" }, modeLabel { {}, "Output" }, rangeLabel { {}, "Bend range" };
    juce::ComboBox lengthBox, modeBox;
    juce::Slider rangeSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::ToggleButton previewToggle { "Preview" };
    juce::Label hint;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rangeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> previewAttachment;
};
