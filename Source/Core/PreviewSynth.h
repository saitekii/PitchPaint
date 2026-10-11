#pragma once

#include "MidiRenderer.h"

#include <array>

namespace pitchpaint
{

// A small built-in synth that plays PitchPaint's own MIDI output the way a correctly configured
// MPE / pitch-bend synth should: note + per-channel bend (with our bend range) -> exact frequency.
// Serves as a reference to compare external synths against, and makes the plugin audible on its own.
class PreviewSynth
{
public:
    void prepare (double sampleRate);
    void reset();

    // Adds the block's audio to left/right while applying `events` (sorted by sample) at their positions.
    void process (const MidiOut& events, int bendRange, float* left, float* right, int numSamples);

    static double noteToHz (double pitch);

private:
    struct Voice
    {
        bool active = false, held = false;
        int channel = 0, note = 0;
        double phase = 0.0, freq = 0.0, targetFreq = 0.0;
        float env = 0.0f;
        float level = 1.0f; // from note-on velocity
        std::uint64_t started = 0;
    };

    void handle (const MidiEvent&, int bendRange);
    void render (float* left, float* right, int start, int end);
    double pitchOf (int channel, int note, int bendRange) const;

    double sampleRate = 44100.0;
    float attackCoef = 0.0f, releaseCoef = 0.0f;
    double glideCoef = 0.0;
    std::array<Voice, 32> voices {};
    std::array<int, 16> bend {};
    std::uint64_t counter = 0;
};

} // namespace pitchpaint
