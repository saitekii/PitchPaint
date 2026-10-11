#include "PluginEditor.h"

using namespace theme;

namespace
{
    constexpr juce::uint32 statusMillis = 4000;
    const juce::String shortcutsHint ("Shift: snap | Ctrl+Z/Y | Shift+wheel: scroll time | Ctrl+wheel: zoom time | Alt+wheel: zoom pitch");
}

PitchPaintEditor::PitchPaintEditor (PitchPaintProcessor& p)
    : AudioProcessorEditor (p), processor (p), canvas (p)
{
    setLookAndFeel (&lookAndFeel);

    // Tools: one active at a time (radio group); the active one uses the accent colour and bold text.
    for (auto* b : { &pencilButton, &selectButton, &eraserButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1);
    }
    pencilButton.onClick = [this] { selectTool (PitchCanvas::Tool::pencil); };
    selectButton.onClick = [this] { selectTool (PitchCanvas::Tool::select); };
    eraserButton.onClick = [this] { selectTool (PitchCanvas::Tool::eraser); };
    pencilButton.setToggleState (true, juce::dontSendNotification);
    pencilButton.setTooltip ("Draw pitch curves (P). Hold Shift to snap to semitones.");
    selectButton.setTooltip ("Select and move (S). Shift-click adds; drag on empty space to box-select; "
                             "drag or use the arrow keys to move; Shift-drag moves by whole semitones.");
    eraserButton.setTooltip ("Erase (E): cuts out what it passes over. Hold Shift to delete whole strokes. "
                             "Right-drag erases with any tool.");

    undoButton.onClick = [this] { undo(); };
    redoButton.onClick = [this] { redo(); };
    undoButton.setTooltip ("Undo (Ctrl+Z)");
    redoButton.setTooltip ("Redo (Ctrl+Y)");

    for (const int bars : { 1, 2, 4, 8, 16 })
        lengthBox.addItem (juce::String (bars) + (bars == 1 ? " bar" : " bars"), bars);
    lengthBox.onChange = [this] { setLoopBars (lengthBox.getSelectedId()); };
    lengthBox.setTitle ("Loop length");
    lengthBox.setTooltip ("Length of the pattern, which loops while the host plays.");

    modeBox.addItemList (processor.params.getParameter ("mode")->getAllValueStrings(), 1);
    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.params, "mode", modeBox);
    modeBox.setTitle ("Output mode");
    modeBox.setTooltip ("MPE: each stroke on its own channel (synth must be in MPE mode). "
                        "Mono pitch-bend: one stroke at a time, works with any synth.");

    rangeSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, controlHeight);
    rangeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.params, "bendRange", rangeSlider);
    rangeSlider.setTitle ("Pitch-bend range in semitones");
    rangeSlider.setTooltip ("Must match the pitch-bend range set in your synth, or pitches will be wrong.");

    previewAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.params, "preview", previewToggle);
    previewToggle.setTooltip ("Built-in sound that plays exactly what is drawn. Use it to check your synth's pitch.");

    for (auto* label : { &lengthLabel, &modeLabel, &rangeLabel })
    {
        label->setFont (juce::Font (type::caption()));
        label->setColour (juce::Label::textColourId, colour::textSecondary);
        label->setJustificationType (juce::Justification::centredRight);
        label->setInterceptsMouseClicks (false, false);
    }
    lengthLabel.attachToComponent (&lengthBox, true); // also links the label for assistive technology
    modeLabel.attachToComponent (&modeBox, true);
    rangeLabel.attachToComponent (&rangeSlider, true);

    synthHint.setFont (juce::Font (type::caption()));
    synthHint.setColour (juce::Label::textColourId, colour::warning);
    footerRight.setFont (juce::Font (type::caption()));
    footerRight.setJustificationType (juce::Justification::centredRight);

    canvas.onPatternChanged = [this] { refresh(); };
    canvas.onStatus = [this] (const juce::String& s) { showStatus (s); };

    // Keyboard: Tab moves through the toolbar in reading order, then the canvas. Clicking a toolbar control
    // doesn't take keyboard focus, so shortcuts keep working and focus rings appear only for keyboard use.
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);
    int order = 1;
    for (auto* c : std::initializer_list<juce::Component*> { &pencilButton, &selectButton, &eraserButton, &undoButton, &redoButton,
                                                             &lengthBox, &modeBox, &rangeSlider, &previewToggle, &canvas })
    {
        c->setExplicitFocusOrder (order++);
        if (c != &canvas)
            c->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (c);
    }
    addAndMakeVisible (synthHint);
    addAndMakeVisible (footerRight);

    refresh();
    startTimerHz (5);

    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (560, 360, 3000, 2000);
    setSize (1040, 600);
}

PitchPaintEditor::~PitchPaintEditor()
{
    setLookAndFeel (nullptr);
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
    if (bars <= 0 || bars * 4.0 == processor.getPattern().lengthBeats)
        return;
    auto pattern = processor.getPattern();
    pattern.lengthBeats = bars * 4.0;
    processor.applyEdit (std::move (pattern));
    showStatus ("Loop set to " + juce::String (bars) + (bars == 1 ? " bar" : " bars") + ". Ctrl+Z to undo.");
}

void PitchPaintEditor::undo()
{
    if (! processor.canUndo())
    {
        showStatus ("Nothing to undo.");
        return;
    }
    processor.undo();
    showStatus ("Undone.");
}

void PitchPaintEditor::redo()
{
    if (! processor.canRedo())
    {
        showStatus ("Nothing to redo.");
        return;
    }
    processor.redo();
    showStatus ("Redone.");
}

void PitchPaintEditor::showStatus (const juce::String& message)
{
    status = message;
    statusUntil = juce::Time::getMillisecondCounter() + statusMillis;
    if (auto* handler = getAccessibilityHandler())
        handler->postAnnouncement (message, juce::AccessibilityHandler::AnnouncementPriority::medium);
    refresh();
}

void PitchPaintEditor::refresh()
{
    lengthBox.setSelectedId (juce::roundToInt (processor.getPattern().lengthBeats / 4.0), juce::dontSendNotification);
    undoButton.setEnabled (processor.canUndo());
    redoButton.setEnabled (processor.canRedo());

    // Persistent: the most common setup mistake is a synth whose bend range differs from ours, so say what to set.
    const auto range = juce::String ((int) processor.params.getRawParameterValue ("bendRange")->load());
    const bool mpe = processor.params.getRawParameterValue ("mode")->load() < 0.5f;
    const auto hint = mpe ? "Synth setup: turn MPE on, MPE bend range " + range + " (Surge XT: Menu > MPE > Enable MPE)"
                          : "Synth setup: pitch-bend range " + range + " semitones. Mono plays one stroke at a time.";

    // Transient feedback replaces the shortcut reminder for a few seconds.
    const bool showingStatus = status.isNotEmpty() && juce::Time::getMillisecondCounter() < statusUntil;
    const auto right = showingStatus ? status : shortcutsHint;
    const auto rightColour = showingStatus ? colour::textPrimary : colour::textSecondary;

    const bool layoutChanged = hint != synthHint.getText() || right != footerRight.getText();
    synthHint.setText (hint, juce::dontSendNotification);
    footerRight.setText (right, juce::dontSendNotification);
    footerRight.setColour (juce::Label::textColourId, rightColour);
    if (layoutChanged)
        resized(); // the footer may need a second row
}

void PitchPaintEditor::timerCallback()
{
    refresh(); // parameter changes made by the host, and expiry of the status message
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
        if (letter == 'Z' && mods.isShiftDown()) redo();
        else if (letter == 'Z') undo();
        else if (letter == 'Y') redo();
        else if (letter == 'A') { selectTool (PitchCanvas::Tool::select); canvas.selectAll(); }
        else return false;
        return true;
    }

    // Arrow keys move the selection: up/down by a semitone, left/right by a quarter beat; Alt for fine steps.
    if (! canvas.getSelection().empty())
    {
        const bool fine = mods.isAltDown();
        const double pitchStep = fine ? 0.1 : 1.0, timeStep = fine ? 1.0 / 32.0 : 0.25;
        if (code == juce::KeyPress::upKey)    { canvas.nudgeSelection (0.0, pitchStep); return true; }
        if (code == juce::KeyPress::downKey)  { canvas.nudgeSelection (0.0, -pitchStep); return true; }
        if (code == juce::KeyPress::leftKey)  { canvas.nudgeSelection (-timeStep, 0.0); return true; }
        if (code == juce::KeyPress::rightKey) { canvas.nudgeSelection (timeStep, 0.0); return true; }
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
    g.fillAll (colour::surface);

    // Group separators in the toolbar, and lines between toolbar / canvas / footer.
    g.setColour (colour::border);
    for (size_t i = 0; i < separatorsX.size(); ++i)
    {
        const int top = separatorRows[i] * (controlHeight + space::s) + space::s;
        g.drawVerticalLine (juce::roundToInt (separatorsX[i]), (float) top, (float) (top + controlHeight));
    }
    g.drawHorizontalLine (toolbarHeight - 1, 0.0f, (float) getWidth());
    g.drawHorizontalLine (getHeight() - footerHeight, 0.0f, (float) getWidth());
}

std::vector<std::vector<PitchPaintEditor::Item>> PitchPaintEditor::toolbarGroups()
{
    const auto labelWidth = [] (const juce::Label& l)
    { return juce::roundToInt (juce::GlyphArrangement::getStringWidth (l.getFont(), l.getText())) + space::s; };

    // Attached labels sit left of their control: reserve their width as a spacer before the control.
    return {
        { { &pencilButton, 64 }, { &selectButton, 64 }, { &eraserButton, 64 } },
        { { &undoButton, 54 }, { &redoButton, 54 } },
        { { nullptr, labelWidth (lengthLabel) }, { &lengthBox, 88 } },
        { { nullptr, labelWidth (modeLabel) }, { &modeBox, 140 }, { nullptr, labelWidth (rangeLabel) }, { &rangeSlider, 104 } },
        { { &previewToggle, 92 } },
    };
}

void PitchPaintEditor::resized()
{
    // Toolbar: groups flow left to right and wrap onto another row when the window is narrow.
    separatorsX.clear();
    separatorRows.clear();
    const int rowHeight = controlHeight + space::s;
    const int maxX = getWidth() - space::s;
    int x = space::s, row = 0;
    bool firstInRow = true;

    for (const auto& group : toolbarGroups())
    {
        int groupWidth = 0;
        for (const auto& item : group)
            groupWidth += item.width + space::xs;
        groupWidth -= space::xs;

        if (! firstInRow && x + space::l + groupWidth > maxX)
        {
            ++row;
            x = space::s;
            firstInRow = true;
        }
        if (! firstInRow)
        {
            separatorsX.push_back ((float) x + space::l * 0.5f);
            separatorRows.push_back (row);
            x += space::l;
        }

        for (const auto& item : group)
        {
            if (item.component != nullptr)
                item.component->setBounds (x, space::s + row * rowHeight, item.width, controlHeight);
            x += item.width + space::xs;
        }
        x -= space::xs;
        firstInRow = false;
    }
    toolbarHeight = (row + 1) * rowHeight + space::s;

    // Footer: setup hint (essential, never truncated) and feedback / shortcuts; two rows if both don't fit.
    const int lineHeight = 18;
    const auto textWidth = [] (const juce::Label& l)
    { return juce::roundToInt (juce::GlyphArrangement::getStringWidth (l.getFont(), l.getText())) + space::s; };
    const int available = getWidth() - 2 * space::s;
    const bool oneRow = textWidth (synthHint) + space::xl + textWidth (footerRight) <= available;
    footerHeight = (oneRow ? lineHeight : 2 * lineHeight) + space::s;

    auto footer = getLocalBounds().removeFromBottom (footerHeight).reduced (space::s, space::xs);
    if (oneRow)
    {
        footerRight.setBounds (footer.removeFromRight (textWidth (footerRight)));
        synthHint.setBounds (footer);
    }
    else
    {
        synthHint.setBounds (footer.removeFromTop (lineHeight));
        footerRight.setBounds (footer);
    }
    synthHint.setMinimumHorizontalScale (oneRow ? 1.0f : 0.8f);
    footerRight.setMinimumHorizontalScale (0.8f);

    canvas.setBounds (getLocalBounds().withTrimmedTop (toolbarHeight).withTrimmedBottom (footerHeight));
    repaint();
}
