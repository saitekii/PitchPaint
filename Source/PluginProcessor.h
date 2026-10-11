#pragma once

#include "Core/MidiRenderer.h"

#include <juce_audio_processors/juce_audio_processors.h>

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
    pitchpaint::RenderSettings currentSettings() const;

    pitchpaint::Pattern pattern;
    pitchpaint::MidiRenderer renderer;
    pitchpaint::MidiOut events;

    double sampleRate = 44100.0;
    bool needsConfigure = true;
    bool wasPlaying = false;
    double expectedPpq = 0.0;
};
