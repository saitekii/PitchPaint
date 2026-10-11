#include "PluginProcessor.h"

namespace
{
    // Until the drawing canvas exists: the spike's two test curves, sampled into trajectories.
    pitchpaint::Pattern makeDemoPattern()
    {
        using namespace pitchpaint;
        constexpr double twoPi = juce::MathConstants<double>::twoPi;

        const auto sample = [] (std::uint32_t id, double start, double end, auto&& pitchAt)
        {
            Trajectory t { id, {} };
            for (double b = start; b < end; b += 1.0 / 32.0)
                t.points.push_back ({ b, pitchAt (b) });
            t.points.push_back ({ end, pitchAt (end) });
            return t;
        };

        Pattern p;
        p.lengthBeats = 8.0;
        p.trajectories.push_back (sample (1, 0.0, 7.5, [] (double b) { return 60.0 + 7.0 * std::sin (twoPi * b / 4.0); }));
        p.trajectories.push_back (sample (2, 2.0, 6.0, [] (double b) { return 72.0 + 5.0 * std::sin (twoPi * (b - 2.0) / 4.0); }));
        return p;
    }
}

PitchPaintProcessor::PitchPaintProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "PARAMS",
              { std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "mode", 1 }, "Output mode",
                                                              juce::StringArray { "MPE", "Mono pitch-bend" }, 0),
                std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "bendRange", 1 }, "Bend range (semitones)", 1, 96, 48) }),
      pattern (makeDemoPattern())
{
}

void PitchPaintProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    events.reserve (8192);
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
    const bool playing = pos.hasValue() && pos->getIsPlaying() && ppq.hasValue();
    const int numSamples = buffer.getNumSamples();

    if (! playing)
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

        renderer.render (pattern, *ppq, beatsPerSample, numSamples, events);
        wasPlaying = true;
        expectedPpq = *ppq + numSamples * beatsPerSample;
    }

    for (const auto& e : events)
        midi.addEvent (e.bytes.data(), 3, e.sample);
}

juce::AudioProcessorEditor* PitchPaintProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

void PitchPaintProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = params.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void PitchPaintProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        params.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PitchPaintProcessor();
}
