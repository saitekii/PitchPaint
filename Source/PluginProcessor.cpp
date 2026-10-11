#include "PluginProcessor.h"
#include "PluginEditor.h"

PitchPaintProcessor::PitchPaintProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "PARAMS",
              { std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "mode", 1 }, "Output mode",
                                                              juce::StringArray { "MPE", "Mono pitch-bend" }, 0),
                std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "bendRange", 1 }, "Bend range (semitones)", 1, 96, 48),
                std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "preview", 1 }, "Preview sound", false) }),
      snapshots (pattern)
{
}

void PitchPaintProcessor::setPattern (pitchpaint::Pattern p)
{
    pattern = std::move (p);
    ++patternVersion;
    snapshots.publish (pattern);
}

void PitchPaintProcessor::applyEdit (pitchpaint::Pattern p)
{
    if (p == pattern)
        return;
    history.record (pattern);
    setPattern (std::move (p));
}

void PitchPaintProcessor::beginGesture()
{
    gestureStart = pattern;
}

void PitchPaintProcessor::endGesture()
{
    if (gestureStart && *gestureStart != pattern)
        history.record (std::move (*gestureStart));
    gestureStart.reset();
}

void PitchPaintProcessor::undo()
{
    if (auto previous = history.undo (pattern))
        setPattern (std::move (*previous));
}

void PitchPaintProcessor::redo()
{
    if (auto next = history.redo (pattern))
        setPattern (std::move (*next));
}

void PitchPaintProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    events.reserve (8192);
    previewSynth.prepare (sr);
    needsConfigure = true;
}

bool PitchPaintProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

pitchpaint::RenderSettings PitchPaintProcessor::currentSettings() const
{
    pitchpaint::RenderSettings s;
    s.mode = params.getRawParameterValue ("mode")->load() < 0.5f ? pitchpaint::OutputMode::mpe : pitchpaint::OutputMode::mono;
    s.bendRange = (int) params.getRawParameterValue ("bendRange")->load();
    return s;
}

void PitchPaintProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();
    midi.clear();
    events.clear();

    if (const auto settings = currentSettings(); needsConfigure || ! (settings == renderer.getSettings()))
    {
        renderer.configure (settings, events, 0);
        needsConfigure = false;
    }

    const auto pos = getPlayHead() != nullptr ? getPlayHead()->getPosition() : std::nullopt;
    const auto ppq = pos.hasValue() ? pos->getPpqPosition() : std::nullopt;
    const bool isPlayingNow = pos.hasValue() && pos->getIsPlaying() && ppq.hasValue();
    const int numSamples = buffer.getNumSamples();

    const auto snapshot = snapshots.acquire();
    playing.store (isPlayingNow);

    if (! isPlayingNow)
    {
        if (wasPlaying)
            renderer.allNotesOff (events, 0);
        wasPlaying = false;
    }
    else
    {
        const double beatsPerSample = pos->getBpm().orFallback (120.0) / 60.0 / sampleRate;

        // Seek or host loop: release everything; notes restart from the new position.
        if (wasPlaying && std::abs (*ppq - expectedPpq) > 1.0e-3)
            renderer.allNotesOff (events, 0);

        renderer.render (*snapshot, *ppq, beatsPerSample, numSamples, events);
        if (snapshot->lengthBeats > 0.0)
            playheadBeat.store (std::fmod (std::max (0.0, *ppq), snapshot->lengthBeats));
        wasPlaying = true;
        expectedPpq = *ppq + numSamples * beatsPerSample;
    }

    // Built-in reference sound: plays exactly what the MIDI output says, with our bend range.
    const bool previewOn = params.getRawParameterValue ("preview")->load() >= 0.5f;
    if (previewOn)
        previewSynth.process (events, renderer.getSettings().bendRange,
                              buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);
    else if (previewWasOn)
        previewSynth.reset();
    previewWasOn = previewOn;

    for (const auto& e : events)
        midi.addEvent (e.bytes.data(), 3, e.sample);
}

juce::AudioProcessorEditor* PitchPaintProcessor::createEditor()
{
    return new PitchPaintEditor (*this);
}

// State: <PitchPaint version="1"> <PARAMS .../> <Pattern>serialised text</Pattern> </PitchPaint>
void PitchPaintProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    // Hosts may ask from any thread: read the published snapshot, not the message-thread copy.
    const auto current = snapshots.acquire();

    juce::XmlElement root ("PitchPaint");
    root.setAttribute ("version", 1);
    root.setAttribute ("snap", gridDivisions[getGridIndex()].name);
    if (auto paramsXml = params.copyState().createXml())
        root.addChildElement (paramsXml.release());
    root.createNewChildElement ("Pattern")->addTextElement (juce::String (pitchpaint::serialise (*current)));
    copyXmlToBinary (root, dest);
}

void PitchPaintProcessor::setStateInformation (const void* data, int size)
{
    const auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr)
        return;

    // Version 0 (before patterns were saved) stored just the parameter tree.
    const auto* paramsXml = xml->hasTagName ("PitchPaint") ? xml->getChildByName (params.state.getType()) : xml.get();
    if (paramsXml != nullptr && paramsXml->hasTagName (params.state.getType()))
        params.replaceState (juce::ValueTree::fromXml (*paramsXml));

    auto loaded = pitchpaint::Pattern {};
    if (const auto* patternXml = xml->getChildByName ("Pattern"))
        if (auto parsed = pitchpaint::parsePattern (patternXml->getAllSubText().toStdString()))
            loaded = std::move (*parsed);

    const auto snapName = xml->getStringAttribute ("snap", gridDivisions[defaultGridIndex].name);
    for (int i = 0; i < (int) std::size (gridDivisions); ++i)
        if (snapName == gridDivisions[i].name)
            setGridIndex (i);

    const auto apply = [this, loaded]
    {
        history.clear();
        gestureStart.reset();
        setPattern (loaded);
    };

    if (juce::MessageManager::existsAndIsCurrentThread())
        apply();
    else
        juce::MessageManager::callAsync (apply); // the editable copy is message-thread only
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PitchPaintProcessor();
}
