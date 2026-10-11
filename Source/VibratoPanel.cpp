#include "VibratoPanel.h"

using namespace theme;

VibratoPanel::VibratoPanel (pitchpaint::Vibrato initial, const juce::String& headingText)
{
    heading.setText (headingText, juce::dontSendNotification);
    heading.setFont (juce::Font (type::emphasis()));
    addAndMakeVisible (heading);

    setUp (depth, depthLabel, 0.0, 200.0, 1.0, " ct", initial.depthCents, "Vibrato depth",
           "How far the pitch swings either way, in cents (100 = one semitone). 0 = no vibrato.");
    setUp (rate, rateLabel, 1.0, 12.0, 0.1, " Hz", initial.rateHz, "Vibrato rate", "Swings per second (5-7 Hz sounds natural).");
    setUp (fade, fadeLabel, 0.0, 4.0, 0.05, " beats", initial.fadeBeats, "Vibrato fade-in",
           "How long after the note starts the vibrato reaches full depth.");

    offButton.setTooltip ("Remove the vibrato (depth 0).");
    offButton.onClick = [this] { depth.setValue (0.0, juce::sendNotificationSync); };
    addAndMakeVisible (offButton);

    setSize (340, 4 * (controlHeight + space::s) + space::m);
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
    for (auto* s : { &depth, &rate, &fade })
    {
        area.removeFromTop (space::s);
        s->setBounds (area.removeFromTop (controlHeight).withTrimmedLeft (64));
    }
}
