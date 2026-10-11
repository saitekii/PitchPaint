#pragma once

#include "Pattern.h"

#include <array>
#include <cstdint>
#include <vector>

namespace pitchpaint
{

struct MidiEvent
{
    int sample;                     // offset within the current block
    std::array<std::uint8_t, 3> bytes;
};

using MidiOut = std::vector<MidiEvent>;

enum class OutputMode
{
    mpe,  // MPE lower zone: one member channel (2..16) per sounding trajectory
    mono  // single channel 1, one trajectory at a time (latest start wins)
};

struct RenderSettings
{
    OutputMode mode = OutputMode::mpe;
    int bendRange = 48;        // semitones each way; must match the receiving synth
    int memberChannels = 15;   // MPE only: channels 2 .. 1 + memberChannels

    bool operator== (const RenderSettings&) const = default;
};

// Turns a Pattern into note + pitch-bend MIDI, block by block, following the host's beat position.
// Real-time safe as long as `out` has spare capacity.
class MidiRenderer
{
public:
    static constexpr int stepSamples = 64; // pitch-bend update interval
    static constexpr int maxMemberChannels = 15;

    // Releases sounding notes and sends the setup messages (MPE configuration / bend range) for `settings`.
    void configure (const RenderSettings& settings, MidiOut& out, int sample);

    // Renders one block that starts at `ppqStart` (host beat position). The pattern loops every lengthBeats.
    void render (const Pattern& pattern, double ppqStart, double beatsPerSample, int numSamples, MidiOut& out);

    // Note-off for everything sounding (transport stop, seek, loop jump).
    void allNotesOff (MidiOut& out, int sample);

    const RenderSettings& getSettings() const { return settings; }
    int numSoundingVoices() const;

private:
    struct Voice
    {
        std::uint32_t trajectoryId = 0;
        bool sounding = false;
        int note = 0;
        int bend = -1;
        std::uint64_t lastUsed = 0; // note-on or note-off order, for channel allocation
    };

    void syncAt (const Pattern&, double localBeat, int sample, MidiOut&);
    void syncMono (const Pattern&, double localBeat, int sample, MidiOut&);
    void startVoice (Voice&, int channel, const Trajectory&, double localBeat, int sample, MidiOut&);
    void updateVoice (Voice&, int channel, const Trajectory&, double localBeat, int sample, MidiOut&);
    void stopVoice (Voice&, int channel, int sample, MidiOut&);
    int bendValue (double semitones) const;
    int channelFor (int voiceIndex) const;

    RenderSettings settings;
    std::array<Voice, maxMemberChannels> voices {};
    std::uint64_t counter = 0;
    std::vector<int> evalPoints;
    Tuning tuning; // of the pattern being rendered
};

} // namespace pitchpaint
