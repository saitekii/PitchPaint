#pragma once

#include "LookAndFeel.h"
#include "PitchCanvas.h"
#include "PluginProcessor.h"

class PitchPaintEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PitchPaintEditor (PitchPaintProcessor&);
    ~PitchPaintEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // Feedback after an action: shown in the footer for a few seconds and announced to screen readers.
    void showStatus (const juce::String&);

private:
    struct Item
    {
        juce::Component* component;
        int width;
    };

    void timerCallback() override;
    void selectTool (PitchCanvas::Tool);
    void setLoopBars (int bars);
    void setGrid (int index);
    void velocityChanged();
    void setTuning (pitchpaint::Tuning, bool live);
    juce::String tuningDescription() const;
    void velocityGestureEnded();
    int lastGridIndex = PitchPaintProcessor::defaultGridIndex; // what G turns snapping back on to
    void undo();
    void redo();
    void refresh(); // toolbar state and footer text from the processor
    std::vector<std::vector<Item>> toolbarGroups();

    PitchPaintLookAndFeel lookAndFeel; // declared first: outlives every child component
    PitchPaintProcessor& processor;
    PitchCanvas canvas;
    juce::TooltipWindow tooltipWindow { this, 600 };

    juce::TextButton pencilButton { "Pencil" }, lineButton { "Line" }, selectButton { "Select" }, eraserButton { "Eraser" };
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" }, smoothButton { "Smooth" };
    juce::Label smoothingLabel { {}, "Smoothing" };
    juce::Slider smoothingSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::Label keyLabel { {}, "Key" }, scaleLabel { {}, "Scale" }, magnetLabel { {}, "Magnet" };
    juce::ComboBox keyBox, scaleBox;
    juce::Slider magnetSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    bool magnetGesture = false;
    juce::Label lengthLabel { {}, "Loop" }, snapLabel { {}, "Snap" }, modeLabel { {}, "Output" }, rangeLabel { {}, "Bend range" };
    juce::ComboBox lengthBox, snapBox, modeBox;
    juce::Slider rangeSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::Label velocityLabel { {}, "Velocity" };
    juce::Slider velocitySlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    bool velocityGesture = false;
    juce::ToggleButton previewToggle { "Preview" };
    juce::Label synthHint, footerRight;

    juce::String status;
    juce::uint32 statusUntil = 0;
    std::vector<float> separatorsX; // between toolbar groups, per row
    std::vector<int> separatorRows;
    int toolbarHeight = 0, footerHeight = 0;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rangeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> previewAttachment;
};
