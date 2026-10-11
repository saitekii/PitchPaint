#include "VibratoPanel.h"

using namespace theme;

VibratoPanel::VibratoPanel (pitchpaint::Vibrato initial, Target t, const juce::String& headingText)
    : target (t)
{
    heading.setText (headingText, juce::dontSendNotification);
    heading.setFont (juce::Font (type::emphasis()));
    addAndMakeVisible (heading);

    // Plain-language status: what these settings will change, and what to do next.
    hint.setFont (juce::Font (type::caption()));
    hint.setColour (juce::Label::textColourId, colour::textSecondary);
    hint.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (hint);

    setUp (depth, depthLabel, 0.0, 200.0, 1.0, " ct", initial.depthCents, "Vibrato depth",
           "How far the pitch swings either way, in cents (100 = one semitone). 0 = no vibrato.");
    setUp (rate, rateLabel, 1.0, 12.0, 0.1, " Hz", initial.rateHz, "Vibrato rate", "Swings per second (5-7 Hz sounds natural).");
    setUp (fade, fadeLabel, 0.0, 4.0, 0.05, " beats", initial.fadeBeats, "Vibrato fade-in",
           "How long after the note starts the vibrato reaches full depth.");

    offButton.setTooltip ("Remove the vibrato (depth 0).");
    offButton.onClick = [this] { depth.setValue (0.0, juce::sendNotificationSync); };
    addAndMakeVisible (offButton);

    if (target == Target::selection)
    {
        actionButton.setButtonText ("Add vibrato");
        actionButton.setTooltip ("Add a natural vibrato (40 ct, keeping the rate and fade-in shown) to the selected strokes.");
        actionButton.onClick = [this] { depth.setValue (suggestedDepthCents, juce::sendNotificationSync); };
    }
    else
    {
        actionButton.setButtonText ("Apply to all strokes");
        actionButton.setTooltip ("Give every stroke in the pattern this vibrato (one undo step).");
        actionButton.onClick = [this]
        {
            auto v = current();
            if (! v.isOn())
                v.depthCents = suggestedDepthCents; // "apply" with depth 0 would do nothing visible
            depth.setValue (v.depthCents, juce::dontSendNotification);
            if (onApplyToAll)
                onApplyToAll (v);
            updateHint();
        };
    }
    actionButton.setToggleState (true, juce::dontSendNotification); // accent: the primary action of this panel
    addAndMakeVisible (actionButton);

    updateHint();
    setSize (360, 2 * controlHeight + 3 * (controlHeight + space::s) + 3 * space::s + 34);
}

void VibratoPanel::updateHint()
{
    const bool on = depth.getValue() > 0.0;
    if (target == Target::selection)
    {
        hint.setText (on ? "Changes apply to the selected strokes right away (Ctrl+Z undoes)."
                         : "Vibrato is off for the selection. Raise Depth, or click Add vibrato.",
                      juce::dontSendNotification);
        actionButton.setVisible (! on);
    }
    else
    {
        hint.setText ("Nothing selected: these settings are for strokes you draw next. "
                      "To change existing strokes, select them (S) or apply to all.",
                      juce::dontSendNotification);
    }
}

void VibratoPanel::setUp (juce::Slider& s, juce::Label& l, double min, double max, double step, const juce::String& suffix,
                          double value, const juce::String& title, const juce::String& tooltip)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 76, controlHeight - 4);
    s.setRange (min, max, step);
    s.setTextValueSuffix (suffix);
    s.setValue (value, juce::dontSendNotification);
    s.setTitle (title);
    s.setTooltip (tooltip);
    s.onDragStart = [this]
    {
        dragging = true;
        if (onGestureStart)
            onGestureStart();
    };
    s.onValueChange = [this]
    {
        updateHint();
        if (onChange)
            onChange (current(), dragging);
    };
    s.onDragEnd = [this]
    {
        dragging = false;
        if (onGestureEnd)
            onGestureEnd();
    };
    addAndMakeVisible (s);

    l.setFont (juce::Font (type::caption()));
    l.setColour (juce::Label::textColourId, colour::textSecondary);
    l.attachToComponent (&s, true);
}

pitchpaint::Vibrato VibratoPanel::current() const
{
    return { depth.getValue(), rate.getValue(), fade.getValue() };
}

void VibratoPanel::paint (juce::Graphics&) {}

void VibratoPanel::resized()
{
    auto area = getLocalBounds().reduced (space::s);
    auto top = area.removeFromTop (controlHeight);
    offButton.setBounds (top.removeFromRight (56));
    heading.setBounds (top);
    hint.setBounds (area.removeFromTop (34));
    for (auto* s : { &depth, &rate, &fade })
    {
        area.removeFromTop (space::s);
        s->setBounds (area.removeFromTop (controlHeight).withTrimmedLeft (64));
    }
    area.removeFromTop (space::s);
    actionButton.setBounds (area.removeFromTop (controlHeight).removeFromRight (target == Target::selection ? 120 : 160));
}
