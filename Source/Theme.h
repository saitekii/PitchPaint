#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Design tokens: the only place visual values are defined. See docs/design-system.md.
namespace theme
{

// --- Colour roles (dark theme) -------------------------------------------------------------------------------------
namespace colour
{
    // Layers: canvas < surface < elevated.
    inline const juce::Colour canvas { 0xff17191e };       // drawing area
    inline const juce::Colour surface { 0xff23262d };      // toolbar, footer, note-name gutter
    inline const juce::Colour elevated { 0xff2e323b };     // popup menus, tooltips
    inline const juce::Colour control { 0xff2b2f37 };      // resting button / field fill
    inline const juce::Colour controlHover { 0xff353a44 };
    inline const juce::Colour controlPressed { 0xff3f4550 };

    inline const juce::Colour textPrimary { 0xffe8eaed };
    inline const juce::Colour textSecondary { 0xffa9afba };
    inline const juce::Colour textOnAccent { 0xff0d1520 };

    inline const juce::Colour border { 0xff3a3f4a };       // separators
    inline const juce::Colour borderControl { 0xff6a7180 }; // control outlines (>= 3:1 against surface)

    inline const juce::Colour accent { 0xff5aa9ff };       // active tool, primary interactive elements
    inline const juce::Colour focusRing { 0xff9cc8ff };    // keyboard focus
    inline const juce::Colour selection { 0xffffffff };    // selected strokes (outline, plus a thicker line)
    inline const juce::Colour warning { 0xffffcc80 };      // setup the user must do in their synth
    inline const juce::Colour playhead { 0xffe8e8e8 };

    // Canvas grid: deliberately low contrast so drawn strokes stay the focus (documented exception).
    inline const juce::Colour blackKeyRow { 0xff141619 };
    inline const juce::Colour gridSemitone { 0xff262931 };
    inline const juce::Colour gridOctave { 0xff3d424d };
    inline const juce::Colour gridBeat { 0xff292c34 };
    inline const juce::Colour gridBar { 0xff4a505c };

    // Stroke colours identify strokes only (never meaning), and each has >= 3:1 contrast against the canvas.
    inline const juce::Colour strokes[] = {
        juce::Colour (0xff4fc3f7), juce::Colour (0xffffb74d), juce::Colour (0xff81c784), juce::Colour (0xfff06292),
        juce::Colour (0xffba68c8), juce::Colour (0xfffff176), juce::Colour (0xff4db6ac), juce::Colour (0xffff8a65),
    };

    inline juce::Colour forStroke (std::uint32_t id) { return strokes[id % std::size (strokes)]; }
}

// --- Spacing scale (px) ---------------------------------------------------------------------------------------------
namespace space
{
    constexpr int xs = 4, s = 8, m = 12, l = 16, xl = 24, xxl = 32;
}

// --- Sizes and shape ------------------------------------------------------------------------------------------------
constexpr int controlHeight = 28;
constexpr float radius = 6.0f;
constexpr float focusRingWidth = 2.0f;

// --- Type scale -----------------------------------------------------------------------------------------------------
namespace type
{
    inline juce::FontOptions body() { return juce::FontOptions (14.0f); }
    inline juce::FontOptions label() { return juce::FontOptions (13.0f); }
    inline juce::FontOptions caption() { return juce::FontOptions (12.0f); }
    inline juce::FontOptions emphasis() { return juce::FontOptions (14.0f, juce::Font::bold); }
    inline juce::FontOptions emptyStateTitle() { return juce::FontOptions (18.0f, juce::Font::bold); }
}

// WCAG 2.x contrast ratio between two opaque colours.
inline double contrastRatio (juce::Colour a, juce::Colour b)
{
    const auto luminance = [] (juce::Colour c)
    {
        const auto channel = [] (juce::uint8 v)
        {
            const double s = v / 255.0;
            return s <= 0.04045 ? s / 12.92 : std::pow ((s + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * channel (c.getRed()) + 0.7152 * channel (c.getGreen()) + 0.0722 * channel (c.getBlue());
    };
    const double la = luminance (a), lb = luminance (b);
    return (std::max (la, lb) + 0.05) / (std::min (la, lb) + 0.05);
}

} // namespace theme
