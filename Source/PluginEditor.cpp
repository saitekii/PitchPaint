#include "PluginEditor.h"

using namespace theme;

namespace
{
    constexpr juce::uint32 statusMillis = 4000;
    const juce::String shortcutsHint ("P/L/S/E: tools | Shift: snap pitch | Alt: ignore grid | G: snap | Ctrl+C/V/D | Ctrl+Z/Y");
}

PitchPaintEditor::PitchPaintEditor (PitchPaintProcessor& p)
    : AudioProcessorEditor (p), processor (p), canvas (p)
{
    setLookAndFeel (&lookAndFeel);

    // Tools: one active at a time (radio group); the active one uses the accent colour and bold text.
    for (auto* b : { &pencilButton, &lineButton, &selectButton, &eraserButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1);
    }
    pencilButton.onClick = [this] { selectTool (PitchCanvas::Tool::pencil); };
    lineButton.onClick = [this] { selectTool (PitchCanvas::Tool::line); };
    lineButton.setTooltip ("Line (L): click to place points joined by straight lines: flat = steady note, slanted = "
                           "clean glide. Double-click, Enter or right-click finishes; Esc cancels.");
    selectButton.onClick = [this] { selectTool (PitchCanvas::Tool::select); };
    eraserButton.onClick = [this] { selectTool (PitchCanvas::Tool::eraser); };
    pencilButton.setToggleState (true, juce::dontSendNotification);
    pencilButton.setTooltip ("Draw pitch curves (P). Hold Shift to snap to semitones.");
    selectButton.setTooltip ("Select and move (S). Shift-click adds; drag on empty space to box-select; "
                             "drag or use the arrow keys to move; Shift-drag moves by whole semitones.");
    eraserButton.setTooltip ("Erase (E): cuts out what it passes over. Hold Shift to delete whole strokes. "
                             "Right-drag erases with any tool.");

    smoothButton.onClick = [this] { canvas.smoothSelection(); };
    smoothButton.setTooltip ("Smooth the selected strokes, or all strokes when nothing is selected (M). "
                             "Press again for more. Start and end pitches stay.");

    smoothingSlider.setRange (0.0, 100.0, 1.0);
    smoothingSlider.setTextValueSuffix ("%");
    smoothingSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 52, controlHeight);
    smoothingSlider.setTitle ("Pencil smoothing");
    smoothingSlider.setTooltip ("Steadies the pencil's pitch while you draw, so hand jitter doesn't become wobble. "
                                "Timing is never smoothed. 0% = raw mouse.");
    smoothingSlider.onValueChange = [this]
    {
        processor.setDrawSmoothing (juce::roundToInt (smoothingSlider.getValue()));
        if (! smoothingSlider.isMouseButtonDown())
            showStatus ("Pencil smoothing " + juce::String (processor.getDrawSmoothing()) + "%.");
    };

    for (int i = 0; i < 12; ++i)
        keyBox.addItem (juce::MidiMessage::getMidiNoteName (i, true, false, 4), i + 1);
    keyBox.setTitle ("Key");
    keyBox.setTooltip ("Root note of the scale the magnet and Shift-snap use.");
    keyBox.onChange = [this]
    {
        auto t = processor.getPattern().tuning;
        t.key = keyBox.getSelectedId() - 1;
        setTuning (t, false);
    };
    for (int i = 0; i < (int) pitchpaint::scales.size(); ++i)
        scaleBox.addItem (juce::String (pitchpaint::scales[(size_t) i].name.data(), pitchpaint::scales[(size_t) i].name.size()), i + 1);
    scaleBox.setTitle ("Scale");
    scaleBox.setTooltip ("Notes the magnet pulls toward and Shift snaps to. Notes outside the scale are shaded.");
    scaleBox.onChange = [this]
    {
        auto t = processor.getPattern().tuning;
        t.scale = scaleBox.getSelectedId() - 1;
        setTuning (t, false);
    };

    magnetSlider.setRange (0.0, 100.0, 1.0);
    magnetSlider.setTextValueSuffix ("%");
    magnetSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 52, controlHeight);
    magnetSlider.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
    magnetSlider.setTitle ("Pitch magnet");
    magnetSlider.setTooltip ("Pulls pitches toward the scale's notes as they play: held notes settle in tune, glides stay "
                             "smooth. Doesn't change what you drew, so you can adjust it any time. 0% = off.");
    magnetSlider.onDragStart = [this] { magnetGesture = true; processor.beginGesture(); };
    magnetSlider.onValueChange = [this]
    {
        auto t = processor.getPattern().tuning;
        t.magnet = magnetSlider.getValue() / 100.0;
        setTuning (t, magnetGesture);
    };
    magnetSlider.onDragEnd = [this]
    {
        magnetGesture = false;
        processor.endGesture(); // one undo step per drag / press
        showStatus (tuningDescription());
    };

    editMenuButton.setButtonText (juce::String::fromUTF8 ("Edit \xe2\x96\xbe")); // "Edit" + small down triangle
    editMenuButton.setTitle ("Edit menu");
    editMenuButton.setTooltip ("Cut, copy, paste, duplicate, delete, select all, smooth.");
    editMenuButton.onClick = [this] { showEditMenu(); };
    vibratoButton.setTooltip ("Vibrato for the selected strokes, or for new strokes when nothing is selected.");
    vibratoButton.onClick = [this] { openVibratoPanel(); };

    undoButton.onClick = [this] { undo(); };
    redoButton.onClick = [this] { redo(); };
    undoButton.setTooltip ("Undo (Ctrl+Z)");
    redoButton.setTooltip ("Redo (Ctrl+Y)");

    for (const int bars : { 1, 2, 4, 8, 16 })
        lengthBox.addItem (juce::String (bars) + (bars == 1 ? " bar" : " bars"), bars);
    lengthBox.onChange = [this] { setLoopBars (lengthBox.getSelectedId()); };
    lengthBox.setTitle ("Loop length");
    lengthBox.setTooltip ("Length of the pattern, which loops while the host plays.");

    for (int i = 0; i < (int) std::size (PitchPaintProcessor::gridDivisions); ++i)
        snapBox.addItem (PitchPaintProcessor::gridDivisions[i].name, i + 1);
    snapBox.onChange = [this] { setGrid (snapBox.getSelectedId() - 1); };
    snapBox.setTitle ("Snap to time grid");
    snapBox.setTooltip ("Snaps stroke starts and ends (so note lengths are whole grid steps), moves and eraser cuts "
                        "to this grid. With snapping on, a click places a one-step note. Pitch stays free "
                        "(Shift snaps pitch). Hold Alt to ignore the grid; G turns snapping on and off.");

    velocitySlider.setRange (1.0, 127.0, 1.0);
    velocitySlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, controlHeight);
    velocitySlider.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
    velocitySlider.setTitle ("Velocity");
    velocitySlider.setTooltip ("Velocity of the selected strokes, or of new strokes when nothing is selected. "
                               "Line thickness shows each stroke's velocity.");
    velocitySlider.onDragStart = [this]
    {
        velocityGesture = true;
        if (! canvas.getSelection().empty())
            processor.beginGesture();
    };
    velocitySlider.onValueChange = [this] { velocityChanged(); };
    velocitySlider.onDragEnd = [this] { velocityGestureEnded(); };
    canvas.onSelectionChanged = [this] { refresh(); };

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

    for (auto* label : { &smoothingLabel, &keyLabel, &scaleLabel, &magnetLabel, &lengthLabel, &snapLabel, &velocityLabel, &modeLabel, &rangeLabel })
    {
        label->setFont (juce::Font (type::caption()));
        label->setColour (juce::Label::textColourId, colour::textSecondary);
        label->setJustificationType (juce::Justification::centredRight);
        label->setInterceptsMouseClicks (false, false);
    }
    lengthLabel.attachToComponent (&lengthBox, true); // also links the label for assistive technology
    snapLabel.attachToComponent (&snapBox, true);
    smoothingLabel.attachToComponent (&smoothingSlider, true);
    keyLabel.attachToComponent (&keyBox, true);
    scaleLabel.attachToComponent (&scaleBox, true);
    magnetLabel.attachToComponent (&magnetSlider, true);
    velocityLabel.attachToComponent (&velocitySlider, true);
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
    for (auto* c : std::initializer_list<juce::Component*> { &pencilButton, &lineButton, &selectButton, &eraserButton, &undoButton,
                                                             &redoButton, &editMenuButton, &smoothButton, &vibratoButton,
                                                             &smoothingSlider, &lengthBox, &snapBox,
                                                             &keyBox, &scaleBox, &magnetSlider, &velocitySlider, &modeBox,
                                                             &rangeSlider, &previewToggle, &canvas })
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
    setResizeLimits (560, 480, 3000, 2000);
    setSize (1300, 640);
}

PitchPaintEditor::~PitchPaintEditor()
{
    setLookAndFeel (nullptr);
}

void PitchPaintEditor::selectTool (PitchCanvas::Tool tool)
{
    canvas.setTool (tool);
    pencilButton.setToggleState (tool == PitchCanvas::Tool::pencil, juce::dontSendNotification);
    lineButton.setToggleState (tool == PitchCanvas::Tool::line, juce::dontSendNotification);
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

void PitchPaintEditor::setGrid (int index)
{
    if (index < 0 || index == processor.getGridIndex())
        return;
    processor.setGridIndex (index);
    if (index > 0)
        lastGridIndex = index;
    canvas.repaint();
    showStatus (index == 0 ? juce::String ("Snap off: timing is free.")
                           : "Snap to " + juce::String (PitchPaintProcessor::gridDivisions[index].name) + ".");
}

void PitchPaintEditor::velocityChanged()
{
    const int v = juce::roundToInt (velocitySlider.getValue());
    if (canvas.getSelection().empty())
    {
        processor.setNewStrokeVelocity (v);
        if (! velocityGesture)
            showStatus ("New strokes: velocity " + juce::String (v) + ".");
        return;
    }
    canvas.setSelectionVelocity (v, velocityGesture);
    if (! velocityGesture) // typed into the field: a single edit
        showStatus ("Velocity " + juce::String (v) + " on " + juce::String ((int) canvas.getSelection().size())
                    + (canvas.getSelection().size() == 1 ? " stroke" : " strokes") + ". Ctrl+Z to undo.");
}

void PitchPaintEditor::velocityGestureEnded()
{
    velocityGesture = false;
    const int v = juce::roundToInt (velocitySlider.getValue());
    const auto count = (int) canvas.getSelection().size();
    if (count == 0)
    {
        showStatus ("New strokes: velocity " + juce::String (v) + ".");
        return;
    }
    processor.endGesture(); // the whole drag or button press is one undo step
    showStatus ("Velocity " + juce::String (v) + " on " + juce::String (count) + (count == 1 ? " stroke" : " strokes")
                + ". Ctrl+Z to undo.");
}

void PitchPaintEditor::setTuning (pitchpaint::Tuning tuning, bool live)
{
    auto pattern = processor.getPattern();
    if (pattern.tuning == tuning)
        return;
    pattern.tuning = tuning;
    if (live)
        processor.setPattern (std::move (pattern)); // inside a gesture
    else
    {
        processor.applyEdit (std::move (pattern));
        showStatus (tuningDescription() + " Ctrl+Z to undo.");
    }
    canvas.repaint();
}

juce::String PitchPaintEditor::tuningDescription() const
{
    const auto& t = processor.getPattern().tuning;
    const auto& scale = pitchpaint::scales[(size_t) t.scale].name;
    const auto scaleName = juce::String (scale.data(), scale.size());
    const auto key = t.scale == 0 ? juce::String() : juce::MidiMessage::getMidiNoteName (t.key, true, false, 4) + " ";
    if (t.magnet <= 0.0)
        return "Magnet off; Shift snaps to " + key + scaleName + ".";
    return "Magnet " + juce::String (juce::roundToInt (t.magnet * 100.0)) + "% toward " + key + scaleName + ".";
}

void PitchPaintEditor::showEditMenu()
{
    enum { cut = 1, copy, paste, duplicate, remove, selectAll, smooth };
    const bool hasSelection = ! canvas.getSelection().empty();

    juce::PopupMenu menu;
    const auto add = [&menu] (int id, const juce::String& text, const juce::String& shortcut, bool enabled)
    {
        juce::PopupMenu::Item item (text);
        item.itemID = id;
        item.isEnabled = enabled;
        item.shortcutKeyDescription = shortcut;
        menu.addItem (item);
    };
    add (cut, "Cut", "Ctrl+X", hasSelection);
    add (copy, "Copy", "Ctrl+C", hasSelection);
    add (paste, "Paste", "Ctrl+V", PitchCanvas::clipboardHasStrokes());
    add (duplicate, "Duplicate", "Ctrl+D", hasSelection);
    add (remove, "Delete", "Del", hasSelection);
    menu.addSeparator();
    add (selectAll, "Select all", "Ctrl+A", ! processor.getPattern().trajectories.empty());
    add (smooth, hasSelection ? "Smooth selection" : "Smooth all strokes", "M", ! processor.getPattern().trajectories.empty());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&editMenuButton),
                        [safe = juce::Component::SafePointer<PitchPaintEditor> (this)] (int result)
                        {
                            if (safe == nullptr)
                                return;
                            auto& c = safe->canvas;
                            switch (result)
                            {
                                case cut: c.cutSelection(); break;
                                case copy: c.copySelection(); break;
                                case paste: safe->selectTool (PitchCanvas::Tool::select); c.paste(); break;
                                case duplicate: c.duplicateSelection(); break;
                                case remove: c.deleteSelection(); break;
                                case selectAll: safe->selectTool (PitchCanvas::Tool::select); c.selectAll(); break;
                                case smooth: c.smoothSelection(); break;
                                default: break;
                            }
                        });
}

void PitchPaintEditor::openVibratoPanel()
{
    const auto selection = canvas.getSelection();
    auto initial = processor.getNewStrokeVibrato();
    for (const auto& t : processor.getPattern().trajectories)
        if (! selection.empty() && t.id == selection.front())
            initial = t.vibrato;
    if (! initial.isOn())
        initial.depthCents = 0.0; // keep rate and fade from before, so turning depth up starts somewhere sensible

    const auto heading = selection.empty() ? juce::String ("Vibrato for new strokes")
                                           : "Vibrato for " + juce::String ((int) selection.size())
                                                 + (selection.size() == 1 ? " selected stroke" : " selected strokes");
    auto panel = std::make_unique<VibratoPanel> (initial,
                                                 selection.empty() ? VibratoPanel::Target::newStrokes : VibratoPanel::Target::selection,
                                                 heading);

    const auto describe = [] (pitchpaint::Vibrato v)
    {
        return v.isOn() ? "Vibrato " + juce::String (juce::roundToInt (v.depthCents)) + " ct at " + juce::String (v.rateHz, 1) + " Hz"
                        : juce::String ("Vibrato off");
    };
    panel->onGestureStart = [this] { if (! canvas.getSelection().empty()) processor.beginGesture(); };
    panel->onChange = [this, describe] (pitchpaint::Vibrato v, bool live)
    {
        if (canvas.getSelection().empty())
        {
            processor.setNewStrokeVibrato (v);
            if (! live)
                showStatus (describe (v) + " for new strokes.");
            return;
        }
        canvas.setSelectionVibrato (v, live);
        if (! live)
            showStatus (describe (v) + " on the selection. Ctrl+Z to undo.");
    };
    panel->onApplyToAll = [this, describe] (pitchpaint::Vibrato v)
    {
        auto pattern = processor.getPattern();
        for (auto& t : pattern.trajectories)
            t.vibrato = v;
        const auto count = (int) pattern.trajectories.size();
        processor.applyEdit (std::move (pattern));
        processor.setNewStrokeVibrato (v);
        showStatus (describe (v) + " on all " + juce::String (count) + (count == 1 ? " stroke" : " strokes")
                    + " and new strokes. Ctrl+Z to undo.");
        canvas.repaint();
    };
    panel->onGestureEnd = [this, describe]
    {
        const auto& sel = canvas.getSelection();
        if (sel.empty())
        {
            showStatus (describe (processor.getNewStrokeVibrato()) + " for new strokes.");
            return;
        }
        processor.endGesture(); // a whole slider drag is one undo step
        for (const auto& t : processor.getPattern().trajectories)
            if (t.id == sel.front())
                showStatus (describe (t.vibrato) + " on the selection. Ctrl+Z to undo.");
    };

    juce::CallOutBox::launchAsynchronously (std::move (panel), vibratoButton.getBounds(), this);
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
    snapBox.setSelectedId (processor.getGridIndex() + 1, juce::dontSendNotification);
    const auto& tuning = processor.getPattern().tuning;
    keyBox.setSelectedId (tuning.key + 1, juce::dontSendNotification);
    scaleBox.setSelectedId (tuning.scale + 1, juce::dontSendNotification);
    if (! magnetGesture)
        magnetSlider.setValue (juce::roundToInt (tuning.magnet * 100.0), juce::dontSendNotification);
    if (! smoothingSlider.isMouseButtonDown (true))
        smoothingSlider.setValue (processor.getDrawSmoothing(), juce::dontSendNotification);
    smoothButton.setEnabled (! processor.getPattern().trajectories.empty()); // selection, or everything

    // The velocity field edits the selection when there is one, otherwise the velocity for new strokes.
    const auto& selection = canvas.getSelection();
    int shownVelocity = processor.getNewStrokeVelocity();
    for (const auto& t : processor.getPattern().trajectories)
        if (! selection.empty() && t.id == selection.front())
            shownVelocity = t.velocity;
    if (! velocityGesture)
        velocitySlider.setValue (shownVelocity, juce::dontSendNotification);
    const auto velocityText = selection.empty() ? juce::String ("Velocity") : juce::String ("Selected velocity");
    const bool velocityLabelChanged = velocityLabel.getText() != velocityText;
    velocityLabel.setText (velocityText, juce::dontSendNotification);
    velocityLabel.setColour (juce::Label::textColourId, selection.empty() ? colour::textSecondary : colour::accent);
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

    const bool layoutChanged = velocityLabelChanged || hint != synthHint.getText() || right != footerRight.getText();
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
        else if (letter == 'C') canvas.copySelection();
        else if (letter == 'X') canvas.cutSelection();
        else if (letter == 'V') { selectTool (PitchCanvas::Tool::select); canvas.paste(); }
        else if (letter == 'D') canvas.duplicateSelection();
        else return false;
        return true;
    }

    // Arrow keys move the selection: up/down by a semitone, left/right by one grid step (a quarter beat with
    // snapping off); Alt for fine steps.
    if (! canvas.getSelection().empty())
    {
        const bool fine = mods.isAltDown();
        const double grid = processor.getGridBeats();
        const double pitchStep = fine ? 0.1 : 1.0, timeStep = fine ? 1.0 / 32.0 : (grid > 0.0 ? grid : 0.25);
        if (code == juce::KeyPress::upKey)    { canvas.nudgeSelection (0.0, pitchStep); return true; }
        if (code == juce::KeyPress::downKey)  { canvas.nudgeSelection (0.0, -pitchStep); return true; }
        if (code == juce::KeyPress::leftKey)  { canvas.nudgeSelection (-timeStep, 0.0); return true; }
        if (code == juce::KeyPress::rightKey) { canvas.nudgeSelection (timeStep, 0.0); return true; }
    }

    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) canvas.deleteSelection();
    else if (code == juce::KeyPress::escapeKey) { if (canvas.isDrawingLine()) canvas.cancelLine(); else canvas.clearSelection(); }
    else if (code == juce::KeyPress::returnKey) canvas.finishLine();
    else if (ch == 'l') selectTool (PitchCanvas::Tool::line);
    else if (ch == 'm') canvas.smoothSelection();
    else if (ch == 'p' || ch == 'b') selectTool (PitchCanvas::Tool::pencil);
    else if (ch == 's' || ch == 'v') selectTool (PitchCanvas::Tool::select);
    else if (ch == 'e') selectTool (PitchCanvas::Tool::eraser);
    else if (ch == 'g') setGrid (processor.getGridIndex() > 0 ? 0 : lastGridIndex);
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
        { { &pencilButton, 64 }, { &lineButton, 56 }, { &selectButton, 64 }, { &eraserButton, 64 } },
        { { &undoButton, 54 }, { &redoButton, 54 }, { &editMenuButton, 64 } },
        { { &smoothButton, 66 }, { &vibratoButton, 84 } },
        { { nullptr, labelWidth (smoothingLabel) }, { &smoothingSlider, 112 } },
        { { nullptr, labelWidth (lengthLabel) }, { &lengthBox, 88 }, { nullptr, labelWidth (snapLabel) }, { &snapBox, 80 } },
        { { nullptr, labelWidth (keyLabel) }, { &keyBox, 64 }, { nullptr, labelWidth (scaleLabel) }, { &scaleBox, 148 },
          { nullptr, labelWidth (magnetLabel) }, { &magnetSlider, 112 } },
        { { nullptr, labelWidth (velocityLabel) }, { &velocitySlider, 104 } },
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
