#include "MidiRenderer.h"

#include <algorithm>
#include <cmath>

namespace pitchpaint
{

namespace
{
    // Tolerance so that an evaluation point placed exactly on a start/end boundary counts as having reached it.
    constexpr double beatEpsilon = 1.0e-7;

    // Position within the loop. Values a hair below the loop end snap to 0, so the wrap point counts as wrapped.
    double loopPosition (double ppq, double length)
    {
        double r = std::fmod (ppq, length);
        if (r < 0.0)
            r += length;
        return r > length - beatEpsilon ? 0.0 : r;
    }

    bool isActiveAt (const Trajectory& t, double beat)
    {
        return t.isPlayable() && t.startBeat() <= beat + beatEpsilon && beat + beatEpsilon < t.endBeat();
    }

    const Trajectory* findTrajectory (const Pattern& p, std::uint32_t id)
    {
        for (const auto& t : p.trajectories)
            if (t.id == id)
                return &t;
        return nullptr;
    }

    std::uint8_t status (int type, int channel) { return (std::uint8_t) (type | (channel - 1)); }

    std::uint8_t velocityOf (const Trajectory& t) { return (std::uint8_t) std::clamp (t.velocity, 1, 127); }

    void addRpn (MidiOut& out, int sample, int channel, int rpnLsb, int value)
    {
        const auto cc = [&] (int controller, int v)
        { out.push_back ({ sample, { status (0xB0, channel), (std::uint8_t) controller, (std::uint8_t) v } }); };

        cc (101, 0);
        cc (100, rpnLsb);
        cc (6, value);
        cc (38, 0);
        cc (101, 127); // RPN null, so later data-entry CCs can't change it by accident
        cc (100, 127);
    }
}

void MidiRenderer::configure (const RenderSettings& newSettings, MidiOut& out, int sample)
{
    allNotesOff (out, sample);

    // Dynamics switched off (or to another control): put the old control back to its resting value on every channel
    // we may have used, so the synth isn't left at a low level.
    if (settings.dynamics != DynamicsOutput::off && newSettings.dynamics != settings.dynamics)
    {
        const int neutral = settings.dynamics == DynamicsOutput::cc11 ? 127
                          : settings.dynamics == DynamicsOutput::cc7  ? 100
                          : settings.dynamics == DynamicsOutput::cc74 ? 64 : 0;
        for (int ch = 1; ch <= 1 + maxMemberChannels; ++ch)
            addDynamicsMessage (ch, neutral, sample, out);
    }

    settings = newSettings;
    settings.bendRange = std::clamp (settings.bendRange, 1, 96);
    settings.memberChannels = std::clamp (settings.memberChannels, 1, maxMemberChannels);

    if (settings.mode == OutputMode::mpe)
    {
        addRpn (out, sample, 1, 6, settings.memberChannels); // MPE Configuration Message, lower zone
        // The spec says one member channel is enough, but some synths only listen per channel.
        for (int ch = 2; ch < 2 + settings.memberChannels; ++ch)
            addRpn (out, sample, ch, 0, settings.bendRange);
    }
    else
    {
        addRpn (out, sample, 1, 0, settings.bendRange);
    }
}

void MidiRenderer::render (const Pattern& pattern, double ppqStart, double beatsPerSample, int numSamples, MidiOut& out)
{
    const double length = pattern.lengthBeats;

    if (length <= 0.0 || beatsPerSample <= 0.0)
        return;

    tuning = pattern.tuning;

    // Evaluate at every step boundary, plus exactly where a trajectory starts or ends and where the loop wraps,
    // so note timing is sample-accurate while bends are updated every stepSamples.
    for (int s0 = 0; s0 < numSamples; s0 += stepSamples)
    {
        const int s1 = std::min (s0 + stepSamples, numSamples);
        const double local0 = loopPosition (ppqStart + s0 * beatsPerSample, length);

        evalPoints.clear();
        evalPoints.push_back (s0);

        const auto addCrossing = [&] (double beatsAhead)
        {
            if (beatsAhead <= 0.0)
                return;
            const int pos = s0 + (int) std::ceil (beatsAhead / beatsPerSample - 1.0e-6);
            if (pos > s0 && pos < s1)
                evalPoints.push_back (pos);
        };

        addCrossing (length - local0);

        for (const auto& t : pattern.trajectories)
        {
            if (! t.isPlayable())
                continue;

            for (const double b : { t.startBeat(), t.endBeat() })
            {
                addCrossing (b - local0);
                addCrossing (b - local0 + length);
            }
        }

        std::sort (evalPoints.begin(), evalPoints.end());
        evalPoints.erase (std::unique (evalPoints.begin(), evalPoints.end()), evalPoints.end());

        for (const int pos : evalPoints)
            syncAt (pattern, loopPosition (ppqStart + pos * beatsPerSample, length), pos, out);
    }
}

void MidiRenderer::syncAt (const Pattern& pattern, double beat, int sample, MidiOut& out)
{
    if (settings.mode == OutputMode::mono)
    {
        syncMono (pattern, beat, sample, out);
        return;
    }

    const int numChannels = settings.memberChannels;

    for (int i = 0; i < numChannels; ++i)
    {
        auto& v = voices[(size_t) i];
        if (! v.sounding)
            continue;

        const auto* t = findTrajectory (pattern, v.trajectoryId);
        if (t == nullptr || ! isActiveAt (*t, beat))
            stopVoice (v, channelFor (i), sample, out);
    }

    for (const auto& t : pattern.trajectories)
    {
        if (! isActiveAt (t, beat))
            continue;

        int index = -1;
        for (int i = 0; i < numChannels && index < 0; ++i)
            if (voices[(size_t) i].sounding && voices[(size_t) i].trajectoryId == t.id)
                index = i;

        if (index >= 0)
        {
            updateVoice (voices[(size_t) index], channelFor (index), t, beat, sample, out);
            continue;
        }

        // New note: take the channel that has been free the longest (so release tails on recently freed
        // channels aren't bent by the new note). If all are busy, steal the oldest note.
        int freeIndex = -1, oldestIndex = 0;
        for (int i = 0; i < numChannels; ++i)
        {
            const auto& v = voices[(size_t) i];
            if (! v.sounding && (freeIndex < 0 || v.lastUsed < voices[(size_t) freeIndex].lastUsed))
                freeIndex = i;
            if (v.lastUsed < voices[(size_t) oldestIndex].lastUsed)
                oldestIndex = i;
        }

        const int chosen = freeIndex >= 0 ? freeIndex : oldestIndex;
        auto& v = voices[(size_t) chosen];
        if (v.sounding)
            stopVoice (v, channelFor (chosen), sample, out);

        startVoice (v, channelFor (chosen), t, beat, sample, out);
    }
}

void MidiRenderer::syncMono (const Pattern& pattern, double beat, int sample, MidiOut& out)
{
    // Latest-started trajectory wins; when it ends, an earlier one that is still running takes over again.
    const Trajectory* best = nullptr;
    for (const auto& t : pattern.trajectories)
        if (isActiveAt (t, beat)
            && (best == nullptr || t.startBeat() > best->startBeat()
                || (t.startBeat() == best->startBeat() && t.id > best->id)))
            best = &t;

    auto& v = voices[0];

    if (v.sounding && (best == nullptr || v.trajectoryId != best->id))
        stopVoice (v, 1, sample, out);

    if (best != nullptr)
    {
        if (v.sounding)
            updateVoice (v, 1, *best, beat, sample, out);
        else
            startVoice (v, 1, *best, beat, sample, out);
    }
}

void MidiRenderer::startVoice (Voice& v, int channel, const Trajectory& t, double beat, int sample, MidiOut& out)
{
    const double pitch = soundingPitch (t, tuning, beat, beatsPerSecond); // magnet, then vibrato

    // The note number is the semitone nearest to where the curve starts, so the attack is in tune and the note
    // shown by the host/synth matches the drawing even if the synth applies a different bend range than ours.
    v.trajectoryId = t.id;
    v.note = std::clamp ((int) std::lround (pitch), 0, 127);
    v.bend = bendValue (pitch - v.note);
    v.sounding = true;
    v.lastUsed = ++counter;

    // Bend (and dynamics) first, so the note starts at the right pitch and level.
    out.push_back ({ sample, { status (0xE0, channel), (std::uint8_t) (v.bend & 0x7F), (std::uint8_t) (v.bend >> 7) } });
    sendDynamics (v, channel, t, beat, sample, out, true);
    out.push_back ({ sample, { status (0x90, channel), (std::uint8_t) v.note, velocityOf (t) } });
}

void MidiRenderer::addDynamicsMessage (int channel, int value, int sample, MidiOut& out) const
{
    const auto v = (std::uint8_t) std::clamp (value, 0, 127);
    switch (settings.dynamics)
    {
        case DynamicsOutput::pressure: out.push_back ({ sample, { status (0xD0, channel), v, 0 }, 2 }); break;
        case DynamicsOutput::cc11: out.push_back ({ sample, { status (0xB0, channel), 11, v } }); break;
        case DynamicsOutput::cc1:  out.push_back ({ sample, { status (0xB0, channel), 1, v } }); break;
        case DynamicsOutput::cc74: out.push_back ({ sample, { status (0xB0, channel), 74, v } }); break;
        case DynamicsOutput::cc7:  out.push_back ({ sample, { status (0xB0, channel), 7, v } }); break;
        case DynamicsOutput::off: break;
    }
}

void MidiRenderer::sendDynamics (Voice& v, int channel, const Trajectory& t, double beat, int sample, MidiOut& out, bool force)
{
    if (settings.dynamics == DynamicsOutput::off)
        return;
    const int value = (int) std::lround (dynamicsAt (t, beat) * 127.0);
    if (! force && value == v.dynamics)
        return;
    v.dynamics = value;
    addDynamicsMessage (channel, value, sample, out);
}

void MidiRenderer::updateVoice (Voice& v, int channel, const Trajectory& t, double beat, int sample, MidiOut& out)
{
    const double pitch = soundingPitch (t, tuning, beat, beatsPerSecond); // magnet, then vibrato
    const double offset = pitch - v.note;

    if (std::abs (offset) > settings.bendRange)
    {
        // Out of bend range: retrigger on a new note, placed so the curve has room to keep moving the same way.
        out.push_back ({ sample, { status (0x80, channel), (std::uint8_t) v.note, 0 } });
        const double ahead = offset > 0.0 ? settings.bendRange * 0.5 : -settings.bendRange * 0.5;
        v.note = std::clamp ((int) std::lround (pitch + ahead), 0, 127);
        v.bend = bendValue (pitch - v.note);
        out.push_back ({ sample, { status (0xE0, channel), (std::uint8_t) (v.bend & 0x7F), (std::uint8_t) (v.bend >> 7) } });
        sendDynamics (v, channel, t, beat, sample, out, true);
        out.push_back ({ sample, { status (0x90, channel), (std::uint8_t) v.note, velocityOf (t) } });
        return;
    }

    const int bend = bendValue (offset);
    if (bend != v.bend)
    {
        v.bend = bend;
        out.push_back ({ sample, { status (0xE0, channel), (std::uint8_t) (bend & 0x7F), (std::uint8_t) (bend >> 7) } });
    }
    sendDynamics (v, channel, t, beat, sample, out, false);
}

void MidiRenderer::stopVoice (Voice& v, int channel, int sample, MidiOut& out)
{
    out.push_back ({ sample, { status (0x80, channel), (std::uint8_t) v.note, 0 } });
    v.sounding = false;
    v.bend = -1;
    v.dynamics = -1;
    v.lastUsed = ++counter;
}

void MidiRenderer::allNotesOff (MidiOut& out, int sample)
{
    for (int i = 0; i < maxMemberChannels; ++i)
        if (voices[(size_t) i].sounding)
            stopVoice (voices[(size_t) i], channelFor (i), sample, out);
}

int MidiRenderer::numSoundingVoices() const
{
    return (int) std::count_if (voices.begin(), voices.end(), [] (const Voice& v) { return v.sounding; });
}

int MidiRenderer::bendValue (double semitones) const
{
    return std::clamp (8192 + (int) std::lround (semitones / settings.bendRange * 8191.0), 0, 16383);
}

int MidiRenderer::channelFor (int voiceIndex) const
{
    return settings.mode == OutputMode::mpe ? 2 + voiceIndex : 1;
}

} // namespace pitchpaint
