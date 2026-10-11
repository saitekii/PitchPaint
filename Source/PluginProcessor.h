#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

// Spike: emits hard-coded continuous pitch curves as MIDI note + pitch-bend,
// to verify that VST3 MIDI output (incl. pitch bend) reaches a downstream synth.
class PitchPaintProcessor final : public juce::AudioProcessor
{
public:
    PitchPaintProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState params;

private:
    struct Voice
    {
        int channel = 1;
        int baseNote = 60;
        bool on = false;
        int lastBend = -1;
    };

    void allNotesOff (juce::MidiBuffer&, int sampleOffset);
    void sendConfig (juce::MidiBuffer&, int sampleOffset, bool mpe, int bendRange);

    double sampleRate = 44100.0;
    std::array<Voice, 2> voices;
    bool wasPlaying = false;
    double expectedPpq = 0.0;
    int configuredMode = -1, configuredRange = -1;
};
