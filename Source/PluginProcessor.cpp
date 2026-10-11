#include "PluginProcessor.h"

namespace
{
    constexpr double loopBeats = 8.0;
    constexpr int stepSamples = 64; // pitch-bend update granularity

    // Hard-coded test curves: {start beat, end beat, pitch(beat)}.
    struct TestCurve
    {
        double start, end;
        double (*pitchAt) (double beat);
    };

    const TestCurve curves[] = {
        { 0.0, 7.5, [] (double b) { return 60.0 + 7.0 * std::sin (juce::MathConstants<double>::twoPi * b / 4.0); } },
        { 2.0, 6.0, [] (double b) { return 72.0 + 5.0 * std::sin (juce::MathConstants<double>::twoPi * (b - 2.0) / 4.0); } },
    };

    int bendValue (double semitones, int range)
    {
        return juce::jlimit (0, 16383, 8192 + juce::roundToInt (semitones / range * 8191.0));
    }

    void addRpn (juce::MidiBuffer& out, int ch, int rpnLsb, int value, int pos)
    {
        out.addEvent (juce::MidiMessage::controllerEvent (ch, 101, 0), pos);
        out.addEvent (juce::MidiMessage::controllerEvent (ch, 100, rpnLsb), pos);
        out.addEvent (juce::MidiMessage::controllerEvent (ch, 6, value), pos);
        out.addEvent (juce::MidiMessage::controllerEvent (ch, 38, 0), pos);
        out.addEvent (juce::MidiMessage::controllerEvent (ch, 101, 127), pos);
        out.addEvent (juce::MidiMessage::controllerEvent (ch, 100, 127), pos);
    }
}

PitchPaintProcessor::PitchPaintProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "PARAMS",
              { std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "mode", 1 }, "Output mode",
                                                              juce::StringArray { "MPE", "Mono pitch-bend" }, 0),
                std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "bendRange", 1 }, "Bend range (semitones)", 1, 96, 48) })
{
}

void PitchPaintProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    configuredMode = configuredRange = -1;
}

bool PitchPaintProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void PitchPaintProcessor::allNotesOff (juce::MidiBuffer& out, int pos)
{
    for (auto& v : voices)
    {
        if (v.on)
            out.addEvent (juce::MidiMessage::noteOff (v.channel, v.baseNote), pos);
        v.on = false;
        v.lastBend = -1;
    }
}

void PitchPaintProcessor::sendConfig (juce::MidiBuffer& out, int pos, bool mpe, int range)
{
    if (mpe)
    {
        addRpn (out, 1, 6, 15, pos);   // MPE Configuration Message: lower zone, 15 member channels
        addRpn (out, 2, 0, range, pos); // member-channel pitch-bend range
    }
    else
    {
        addRpn (out, 1, 0, range, pos);
    }
}

void PitchPaintProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();
    midi.clear();

    const bool mpe = params.getRawParameterValue ("mode")->load() < 0.5f;
    const int range = (int) params.getRawParameterValue ("bendRange")->load();
    const int numVoices = mpe ? 2 : 1;

    if ((mpe ? 0 : 1) != configuredMode || range != configuredRange)
    {
        allNotesOff (midi, 0);
        sendConfig (midi, 0, mpe, range);
        configuredMode = mpe ? 0 : 1;
        configuredRange = range;
    }

    const auto pos = getPlayHead() != nullptr ? getPlayHead()->getPosition() : std::nullopt;
    const bool playing = pos.hasValue() && pos->getIsPlaying();
    const auto ppqOpt = pos.hasValue() ? pos->getPpqPosition() : std::nullopt;
    const auto bpmOpt = pos.hasValue() ? pos->getBpm() : std::nullopt;

    if (! playing || ! ppqOpt.hasValue())
    {
        if (wasPlaying)
            allNotesOff (midi, 0);
        wasPlaying = false;
        return;
    }

    const double ppq0 = *ppqOpt;
    const double beatsPerSample = bpmOpt.orFallback (120.0) / 60.0 / sampleRate;
    const int numSamples = buffer.getNumSamples();

    // Transport jumped (seek, host loop) -> release everything, voices restart from the new position.
    if (wasPlaying && std::abs (ppq0 - expectedPpq) > 1.0e-3)
        allNotesOff (midi, 0);

    wasPlaying = true;
    expectedPpq = ppq0 + numSamples * beatsPerSample;

    for (int s = 0; s < numSamples; s += stepSamples)
    {
        const double beat = std::fmod (std::max (0.0, ppq0 + s * beatsPerSample), loopBeats);

        for (int i = 0; i < (int) voices.size(); ++i)
        {
            auto& v = voices[(size_t) i];
            const auto& c = curves[i];
            const bool active = i < numVoices && beat >= c.start && beat < c.end;

            if (active)
            {
                if (! v.on)
                {
                    v.channel = mpe ? 2 + i : 1;
                    v.baseNote = juce::roundToInt (c.pitchAt (c.start));
                }

                const int bend = bendValue (c.pitchAt (beat) - v.baseNote, range);

                if (bend != v.lastBend) // bend goes before note-on at the same timestamp
                    midi.addEvent (juce::MidiMessage::pitchWheel (v.channel, bend), s);
                v.lastBend = bend;

                if (! v.on)
                    midi.addEvent (juce::MidiMessage::noteOn (v.channel, v.baseNote, (juce::uint8) 100), s);
                v.on = true;
            }
            else if (v.on)
            {
                midi.addEvent (juce::MidiMessage::noteOff (v.channel, v.baseNote), s);
                v.on = false;
                v.lastBend = -1;
            }
        }
    }
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
