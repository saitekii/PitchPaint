#pragma once

#include "Core/MidiRenderer.h"
#include "Core/PatternSnapshots.h"
#include "Core/PreviewSynth.h"

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

    // Message thread only. setPattern makes the change audible from the next audio block.
    const pitchpaint::Pattern& getPattern() const { return pattern; }
    void setPattern (pitchpaint::Pattern);
    int getPatternVersion() const { return patternVersion; } // bumped by every setPattern

    // Any thread: loop-local beat at the start of the latest block, and whether the host is playing.
    double getPlayheadBeat() const { return playheadBeat.load(); }
    bool isPlaying() const { return playing.load(); }

private:
    pitchpaint::RenderSettings currentSettings() const;

    pitchpaint::Pattern pattern;
    int patternVersion = 0;
    pitchpaint::PatternSnapshots snapshots;
    pitchpaint::MidiRenderer renderer;
    pitchpaint::PreviewSynth previewSynth;
    bool previewWasOn = false;
    std::atomic<double> playheadBeat { 0.0 };
    std::atomic<bool> playing { false };
    pitchpaint::MidiOut events;

    double sampleRate = 44100.0;
    bool needsConfigure = true;
    bool wasPlaying = false;
    double expectedPpq = 0.0;
};
