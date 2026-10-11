#include "LookAndFeel.h"

using namespace theme;

PitchPaintLookAndFeel::PitchPaintLookAndFeel()
{
#if JUCE_WINDOWS
    setDefaultSansSerifTypefaceName ("Segoe UI");
#endif

    setColour (juce::ResizableWindow::backgroundColourId, colour::surface);

    setColour (juce::TextButton::buttonColourId, colour::control);
    setColour (juce::TextButton::buttonOnColourId, colour::accent);
    setColour (juce::TextButton::textColourOffId, colour::textPrimary);
    setColour (juce::TextButton::textColourOnId, colour::textOnAccent);

    setColour (juce::ToggleButton::textColourId, colour::textPrimary);
    setColour (juce::ToggleButton::tickColourId, colour::accent);
    setColour (juce::ToggleButton::tickDisabledColourId, colour::borderControl);

    setColour (juce::ComboBox::backgroundColourId, colour::control);
    setColour (juce::ComboBox::textColourId, colour::textPrimary);
    setColour (juce::ComboBox::outlineColourId, colour::borderControl);
    setColour (juce::ComboBox::arrowColourId, colour::textSecondary);
    setColour (juce::ComboBox::focusedOutlineColourId, colour::focusRing);

    setColour (juce::PopupMenu::backgroundColourId, colour::elevated);
    setColour (juce::PopupMenu::textColourId, colour::textPrimary);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colour::accent);
    setColour (juce::PopupMenu::highlightedTextColourId, colour::textOnAccent);

    setColour (juce::Label::textColourId, colour::textPrimary);

    setColour (juce::Slider::textBoxTextColourId, colour::textPrimary);
    setColour (juce::Slider::textBoxBackgroundColourId, colour::control);
    setColour (juce::Slider::textBoxOutlineColourId, colour::borderControl);
    setColour (juce::Slider::textBoxHighlightColourId, colour::accent.withAlpha (0.4f));

    setColour (juce::Slider::trackColourId, colour::accent);
    setColour (juce::Slider::backgroundColourId, colour::control);
    setColour (juce::Slider::thumbColourId, colour::textPrimary);

    setColour (juce::TextEditor::backgroundColourId, colour::control);
    setColour (juce::TextEditor::textColourId, colour::textPrimary);
    setColour (juce::TextEditor::highlightColourId, colour::accent.withAlpha (0.4f));
    setColour (juce::TextEditor::outlineColourId, colour::borderControl);
    setColour (juce::TextEditor::focusedOutlineColourId, colour::focusRing);
    setColour (juce::CaretComponent::caretColourId, colour::textPrimary);

    setColour (juce::TooltipWindow::backgroundColourId, colour::elevated);
    setColour (juce::TooltipWindow::textColourId, colour::textPrimary);
    setColour (juce::TooltipWindow::outlineColourId, colour::border);
}

void PitchPaintLookAndFeel::drawFocusRing (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    g.setColour (colour::focusRing);
    g.drawRoundedRectangle (bounds.reduced (focusRingWidth * 0.5f), radius + 1.0f, focusRingWidth);
}

void PitchPaintLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                                  bool isHighlighted, bool isDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = button.getToggleState();

    auto fill = on ? colour::accent : colour::control;
    if (isDown)
        fill = on ? colour::accent.darker (0.15f) : colour::controlPressed;
    else if (isHighlighted)
        fill = on ? colour::accent.brighter (0.1f) : colour::controlHover;
    if (! button.isEnabled())
        fill = colour::control.withAlpha (0.5f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, radius);

    if (! on)
    {
        g.setColour (button.isEnabled() ? colour::borderControl : colour::border);
        g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
    }

    if (button.hasKeyboardFocus (false))
        drawFocusRing (g, button.getLocalBounds().toFloat());
}

juce::Font PitchPaintLookAndFeel::getTextButtonFont (juce::TextButton& button, int)
{
    return button.getToggleState() ? juce::Font (type::emphasis()).withHeight (13.0f) : juce::Font (type::label());
}

void PitchPaintLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool isHighlighted, bool isDown)
{
    LookAndFeel_V4::drawToggleButton (g, button, isHighlighted, isDown);
    if (button.hasKeyboardFocus (false))
        drawFocusRing (g, button.getLocalBounds().toFloat());
}

void PitchPaintLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (1.0f);
    g.setColour (box.isMouseOver (true) ? colour::controlHover : colour::control);
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (colour::borderControl);
    g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);

    // Chevron.
    const float cx = (float) width - 14.0f, cy = (float) height * 0.5f;
    juce::Path arrow;
    arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
    arrow.lineTo (cx, cy + 2.0f);
    arrow.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (box.isEnabled() ? colour::textSecondary : colour::border);
    g.strokePath (arrow, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (box.hasKeyboardFocus (true))
        drawFocusRing (g, box.getLocalBounds().toFloat());
}

juce::Font PitchPaintLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (type::label());
}

void PitchPaintLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (space::s, 1, box.getWidth() - space::s - 24, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void PitchPaintLookAndFeel::drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&)
{
    g.setColour (colour::elevated);
    g.fillPath (path);
    g.setColour (colour::border);
    g.strokePath (path, juce::PathStrokeType (1.0f));
}

juce::Font PitchPaintLookAndFeel::getPopupMenuFont()
{
    return juce::Font (type::body());
}
