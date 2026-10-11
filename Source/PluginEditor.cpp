#include "PluginEditor.h"

PitchPaintEditor::PitchPaintEditor (PitchPaintProcessor& p)
    : AudioProcessorEditor (p), processor (p), canvas (p)
{
    for (auto* b : { &pencilButton, &eraserButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1);
        addAndMakeVisible (b);
    }
    pencilButton.onClick = [this] { selectTool (PitchCanvas::Tool::pencil); };
    eraserButton.onClick = [this] { selectTool (PitchCanvas::Tool::eraser); };
    pencilButton.setToggleState (true, juce::dontSendNotification);

    for (const int bars : { 1, 2, 4, 8, 16 })
        lengthBox.addItem (juce::String (bars) + (bars == 1 ? " bar" : " bars"), bars);
    lengthBox.setSelectedId (juce::roundToInt (processor.getPattern().lengthBeats / 4.0), juce::dontSendNotification);
    lengthBox.onChange = [this] { setLoopBars (lengthBox.getSelectedId()); };
    canvas.onPatternChanged = [this]
    { lengthBox.setSelectedId (juce::roundToInt (processor.getPattern().lengthBeats / 4.0), juce::dontSendNotification); };

    modeBox.addItemList (processor.params.getParameter ("mode")->getAllValueStrings(), 1);
    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.params, "mode", modeBox);
    rangeSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 40, 24);
    rangeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.params, "bendRange", rangeSlider);

    hint.setText ("Shift: snap  |  Right-drag: erase  |  Ctrl+wheel: zoom",
                  juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, juce::Colours::grey);
    hint.setJustificationType (juce::Justification::centredRight);
    hint.setMinimumHorizontalScale (0.7f);

    for (auto* c : std::initializer_list<juce::Component*> { &lengthLabel, &lengthBox, &modeLabel, &modeBox, &rangeLabel, &rangeSlider, &hint, &canvas })
        addAndMakeVisible (c);

    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (640, 320, 3000, 2000);
    setSize (1000, 560);
}

void PitchPaintEditor::selectTool (PitchCanvas::Tool tool)
{
    canvas.setTool (tool);
    pencilButton.setToggleState (tool == PitchCanvas::Tool::pencil, juce::dontSendNotification);
    eraserButton.setToggleState (tool == PitchCanvas::Tool::eraser, juce::dontSendNotification);
}

void PitchPaintEditor::setLoopBars (int bars)
{
    if (bars <= 0)
        return;
    auto pattern = processor.getPattern();
    pattern.lengthBeats = bars * 4.0;
    processor.setPattern (std::move (pattern));
    canvas.repaint();
}

bool PitchPaintEditor::keyPressed (const juce::KeyPress& key)
{
    if (key.getTextCharacter() == 'p' || key.getTextCharacter() == 'b')
        selectTool (PitchCanvas::Tool::pencil);
    else if (key.getTextCharacter() == 'e')
        selectTool (PitchCanvas::Tool::eraser);
    else
        return false;
    return true;
}

void PitchPaintEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2b2e36));
}

void PitchPaintEditor::resized()
{
    auto area = getLocalBounds();
    auto bar = area.removeFromTop (40).reduced (8, 6);

    pencilButton.setBounds (bar.removeFromLeft (70));
    bar.removeFromLeft (4);
    eraserButton.setBounds (bar.removeFromLeft (70));
    bar.removeFromLeft (16);

    lengthLabel.setBounds (bar.removeFromLeft (40));
    lengthBox.setBounds (bar.removeFromLeft (90));
    bar.removeFromLeft (12);
    modeLabel.setBounds (bar.removeFromLeft (52));
    modeBox.setBounds (bar.removeFromLeft (150));
    bar.removeFromLeft (12);
    rangeLabel.setBounds (bar.removeFromLeft (76));
    rangeSlider.setBounds (bar.removeFromLeft (110));
    bar.removeFromLeft (12);
    hint.setBounds (bar);

    canvas.setBounds (area);
}
