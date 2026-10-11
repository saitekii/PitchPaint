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

    // Time grid for editing (not a parameter: it doesn't affect the sound). Saved with the project.
    struct GridDivision
    {
        const char* name;
        double beats; // 0 = off
    };
    static constexpr GridDivision gridDivisions[] = {
        { "Off", 0.0 },          { "1 bar", 4.0 },  { "1/2", 2.0 },          { "1/4", 1.0 },          { "1/8", 0.5 },
        { "1/16", 0.25 },        { "1/32", 0.125 }, { "1/8T", 1.0 / 3.0 },   { "1/16T", 1.0 / 6.0 },
    };
    static constexpr int defaultGridIndex = 5; // 1/16
    int getGridIndex() const { return gridIndex.load(); }
    void setGridIndex (int index) { gridIndex.store (juce::jlimit (0, (int) std::size (gridDivisions) - 1, index)); }
    double getGridBeats() const { return gridDivisions[getGridIndex()].beats; }

    // Pencil smoothing while drawing, 0..100 % (pitch only). Saved with the project.
    int getDrawSmoothing() const { return drawSmoothing.load(); }
    void setDrawSmoothing (int percent) { drawSmoothing.store (juce::jlimit (0, 100, percent)); }

    // Velocity given to newly drawn strokes (each stroke keeps its own). Saved with the project.
    int getNewStrokeVelocity() const { return newStrokeVelocity.load(); }
    void setNewStrokeVelocity (int v) { newStrokeVelocity.store (juce::jlimit (1, 127, v)); }

    // Any thread: loop-local beat at the start of the latest block, and whether the host is playing.
    double getPlayheadBeat() const { return playheadBeat.load(); }
    bool isPlaying() const { return playing.load(); }

private:
    pitchpaint::RenderSettings currentSettings() const;

    pitchpaint::Pattern pattern;
    int patternVersion = 0;
    std::atomic<int> gridIndex { defaultGridIndex };
    std::atomic<int> newStrokeVelocity { pitchpaint::Trajectory::defaultVelocity };
    std::atomic<int> drawSmoothing { 40 };
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
