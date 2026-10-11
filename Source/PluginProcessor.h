#pragma once

#include "Core/MidiRenderer.h"
#include "Core/PatternSnapshots.h"
#include "Core/PreviewSynth.h"
#include "Core/UndoHistory.h"

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

    // Message thread only. Every change is audible from the next audio block.
    const pitchpaint::Pattern& getPattern() const { return pattern; }
    int getPatternVersion() const { return patternVersion; } // bumped by every change

    void setPattern (pitchpaint::Pattern); // no undo step: for live updates inside a gesture
    void applyEdit (pitchpaint::Pattern);  // one undo step
    void beginGesture();                   // mouse down: remember the pattern ...
    void endGesture();                     // ... mouse up: one undo step if it changed

    void undo();
    void redo();
    bool canUndo() const { return history.canUndo(); }
    bool canRedo() const { return history.canRedo(); }

    // Any thread: loop-local beat at the start of the latest block, and whether the host is playing.
    double getPlayheadBeat() const { return playheadBeat.load(); }
    bool isPlaying() const { return playing.load(); }

private:
    pitchpaint::RenderSettings currentSettings() const;

    pitchpaint::Pattern pattern;
    int patternVersion = 0;
    pitchpaint::UndoHistory history;
    std::optional<pitchpaint::Pattern> gestureStart;
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
