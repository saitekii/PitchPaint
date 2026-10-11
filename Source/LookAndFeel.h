#pragma once

#include "Theme.h"

// One look for every control, built only from theme tokens. Defines the default, hover, pressed, disabled,
// active (toggled) and keyboard-focus states of buttons, combo boxes, toggles and the bend-range field.
class PitchPaintLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    PitchPaintLookAndFeel();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isHighlighted, bool isDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool isHighlighted, bool isDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isDown, int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    juce::Font getPopupMenuFont() override;

    // Keyboard focus ring, used by the components above and by the canvas.
    static void drawFocusRing (juce::Graphics&, juce::Rectangle<float> bounds);
};
