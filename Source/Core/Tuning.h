#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace pitchpaint
{

struct Scale
{
    std::string_view name;
    std::uint16_t mask; // bit n set = n semitones above the key is in the scale
};

inline constexpr std::array<Scale, 10> scales { {
    { "Chromatic", 0b111111111111 },
    { "Major", 0b101010110101 },          // 0 2 4 5 7 9 11
    { "Minor", 0b010110101101 },          // 0 2 3 5 7 8 10
    { "Harmonic minor", 0b100110101101 }, // 0 2 3 5 7 8 11
    { "Dorian", 0b011010101101 },         // 0 2 3 5 7 9 10
    { "Mixolydian", 0b011010110101 },     // 0 2 4 5 7 9 10
    { "Major pentatonic", 0b001010010101 }, // 0 2 4 7 9
    { "Minor pentatonic", 0b010010101001 }, // 0 3 5 7 10
    { "Blues", 0b010011101001 },          // 0 3 5 6 7 10
    { "Whole tone", 0b010101010101 },     // 0 2 4 6 8 10
} };

// Key, scale and pitch magnet for a pattern. The magnet is applied when the pattern plays (and when it's drawn), so the
// strokes keep what was drawn and the magnet can be changed at any time.
struct Tuning
{
    int key = 0;          // pitch class of the scale's root, 0 = C
    int scale = 0;        // index into `scales`
    double magnet = 0.0;  // 0 = off .. 1 = strongest

    bool inScale (int note) const;
    double nearestScaleNote (double pitch) const; // for hard snapping (Shift)

    // Pulls a pitch toward the scale notes either side of it. Continuous and monotonic, so glides stay glides;
    // the pull grows with the magnet and is strongest close to a note.
    double apply (double pitch) const;

    bool operator== (const Tuning&) const = default;
};

} // namespace pitchpaint
