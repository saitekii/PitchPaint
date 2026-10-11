#include "PluginEditor.h"

PitchPaintEditor::PitchPaintEditor (PitchPaintProcessor& p)
    : AudioProcessorEditor (p), processor (p), canvas (p)
{
    for (auto* b : { &pencilButton, &selectButton, &eraserButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1);
        addAndMakeVisible (b);
    }
    pencilButton.onClick = [this] { selectTool (PitchCanvas::Tool::pencil); };
    selectButton.onClick = [this] { selectTool (PitchCanvas::Tool::select); };
    eraserButton.onClick = [this] { selectTool (PitchCanvas::Tool::eraser); };
    pencilButton.setToggleState (true, juce::dontSendNotification);
    pencilButton.setTooltip ("Draw (P)");
    selectButton.setTooltip ("Select and move (S). Shift-click adds, drag empty space to box-select, Shift-drag moves by whole semitones");
    eraserButton.setTooltip ("Erase (E). Right-drag erases with any tool");

    undoButton.onClick = [this] { processor.undo(); };
    redoButton.onClick = [this] { processor.redo(); };
    undoButton.setTooltip ("Undo (Ctrl+Z)");
    redoButton.setTooltip ("Redo (Ctrl+Y)");

    for (const int bars : { 1, 2, 4, 8, 16 })
        lengthBox.addItem (juce::String (bars) + (bars == 1 ? " bar" : " bars"), bars);
    lengthBox.onChange = [this] { setLoopBars (lengthBox.getSelectedId()); };
    canvas.onPatternChanged = [this] { refresh(); };

    modeBox.addItemList (processor.params.getParameter ("mode")->getAllValueStrings(), 1);
    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.params, "mode", modeBox);
    rangeSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 40, 24);
    rangeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.params, "bendRange", rangeSlider);
    previewAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.params, "preview", previewToggle);
    previewToggle.setTooltip ("Built-in sound that plays exactly what is drawn. Use it to check your synth's pitch.");

    synthHint.setColour (juce::Label::textColourId, juce::Colour (0xffffcc80));
    synthHint.setMinimumHorizontalScale (0.7f);
    controlsHint.setText ("Shift: snap  |  Del: delete  |  Ctrl+Z / Ctrl+Y  |  Ctrl+wheel: zoom", juce::dontSendNotification);
    controlsHint.setColour (juce::Label::textColourId, juce::Colours::grey);
    controlsHint.setJustificationType (juce::Justification::centredRight);
    controlsHint.setMinimumHorizontalScale (0.7f);

    for (auto* c : std::initializer_list<juce::Component*> { &undoButton, &redoButton, &lengthLabel, &lengthBox, &modeLabel, &modeBox,
                                                             &rangeLabel, &rangeSlider, &previewToggle, &synthHint, &controlsHint, &canvas })
        addAndMakeVisible (c);

    refresh();
    startTimerHz (5);

    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (720, 320, 3000, 2000);
    setSize (1040, 580);
}

void PitchPaintEditor::selectTool (PitchCanvas::Tool tool)
{
    canvas.setTool (tool);
    pencilButton.setToggleState (tool == PitchCanvas::Tool::pencil, juce::dontSendNotification);
    selectButton.setToggleState (tool == PitchCanvas::Tool::select, juce::dontSendNotification);
    eraserButton.setToggleState (tool == PitchCanvas::Tool::eraser, juce::dontSendNotification);
    if (tool != PitchCanvas::Tool::select)
        canvas.clearSelection();
}

void PitchPaintEditor::setLoopBars (int bars)
{
    if (bars <= 0)
        return;
    auto pattern = processor.getPattern();
    pattern.lengthBeats = bars * 4.0;
    processor.applyEdit (std::move (pattern));
}

void PitchPaintEditor::refresh()
{
    lengthBox.setSelectedId (juce::roundToInt (processor.getPattern().lengthBeats / 4.0), juce::dontSendNotification);
    undoButton.setEnabled (processor.canUndo());
    redoButton.setEnabled (processor.canRedo());

    // The most common setup mistake is a synth whose bend range differs from ours: say what to set.
    const auto range = juce::String ((int) processor.params.getRawParameterValue ("bendRange")->load());
    const bool mpe = processor.params.getRawParameterValue ("mode")->load() < 0.5f;
    synthHint.setText (mpe ? "MPE: turn MPE on in your synth, MPE bend range " + range + " (Surge XT: Menu > MPE > Enable MPE)"
                           : "Mono pitch-bend: set your synth's pitch-bend range to " + range + " semitones; one note at a time",
                       juce::dontSendNotification);
}

void PitchPaintEditor::timerCallback()
{
    refresh(); // picks up parameter changes made by the host
}

bool PitchPaintEditor::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const auto code = key.getKeyCode();
    const auto letter = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code);
    // Some hosts deliver key codes without a text character, so fall back to the code.
    const auto ch = juce::CharacterFunctions::toLowerCase (key.getTextCharacter() != 0 ? key.getTextCharacter() : (juce::juce_wchar) code);

    if (mods.isCommandDown())
    {
        if (letter == 'Z' && mods.isShiftDown()) processor.redo();
        else if (letter == 'Z') processor.undo();
        else if (letter == 'Y') processor.redo();
        else if (letter == 'A') { selectTool (PitchCanvas::Tool::select); canvas.selectAll(); }
        else return false;
        return true;
    }

    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) canvas.deleteSelection();
    else if (code == juce::KeyPress::escapeKey) canvas.clearSelection();
    else if (ch == 'p' || ch == 'b') selectTool (PitchCanvas::Tool::pencil);
    else if (ch == 's' || ch == 'v') selectTool (PitchCanvas::Tool::select);
    else if (ch == 'e') selectTool (PitchCanvas::Tool::eraser);
    else return false;
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
    auto footer = area.removeFromBottom (24).reduced (8, 2);

    for (auto* b : { &pencilButton, &selectButton, &eraserButton })
    {
        b->setBounds (bar.removeFromLeft (64));
        bar.removeFromLeft (4);
    }
    bar.removeFromLeft (10);
    undoButton.setBounds (bar.removeFromLeft (52));
    bar.removeFromLeft (4);
    redoButton.setBounds (bar.removeFromLeft (52));
    bar.removeFromLeft (14);

    lengthLabel.setBounds (bar.removeFromLeft (40));
    lengthBox.setBounds (bar.removeFromLeft (88));
    bar.removeFromLeft (10);
    modeLabel.setBounds (bar.removeFromLeft (52));
    modeBox.setBounds (bar.removeFromLeft (148));
    bar.removeFromLeft (10);
    rangeLabel.setBounds (bar.removeFromLeft (76));
    rangeSlider.setBounds (bar.removeFromLeft (110));
    bar.removeFromLeft (10);
    previewToggle.setBounds (bar.removeFromLeft (80));

    controlsHint.setBounds (footer.removeFromRight (juce::jmin (420, footer.getWidth() / 2)));
    synthHint.setBounds (footer);

    canvas.setBounds (area);
}
