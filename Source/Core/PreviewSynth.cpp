#include "PreviewSynth.h"

#include <algorithm>
#include <cmath>

namespace pitchpaint
{

namespace
{
    constexpr double twoPi = 6.283185307179586;
    constexpr float gain = 0.2f;
}

double PreviewSynth::noteToHz (double pitch)
{
    return 440.0 * std::pow (2.0, (pitch - 69.0) / 12.0);
}

void PreviewSynth::prepare (double sr)
{
    sampleRate = sr;
    attackCoef = 1.0f - (float) std::exp (-1.0 / (0.005 * sr)); // ~5 ms
    releaseCoef = (float) std::exp (-1.0 / (0.08 * sr));         // ~80 ms
    glideCoef = 1.0 - std::exp (-1.0 / (0.002 * sr));            // ~2 ms, smooths the 64-sample bend steps
    reset();
}

void PreviewSynth::reset()
{
    voices = {};
    bend.fill (8192);
}

double PreviewSynth::pitchOf (int channel, int note, int bendRange) const
{
    return note + (bend[(size_t) channel] - 8192) / 8191.0 * bendRange;
}

void PreviewSynth::handle (const MidiEvent& e, int bendRange)
{
    const int type = e.bytes[0] & 0xF0;
    const int ch = e.bytes[0] & 0x0F;

    if (type == 0xE0)
    {
        bend[(size_t) ch] = e.bytes[1] | (e.bytes[2] << 7);
        for (auto& v : voices)
            if (v.active && v.held && v.channel == ch)
                v.targetFreq = noteToHz (pitchOf (ch, v.note, bendRange));
    }
    else if (type == 0x90 && e.bytes[2] > 0)
    {
        // Free voice, else steal the oldest.
        auto* v = &voices[0];
        for (auto& candidate : voices)
        {
            if (! candidate.active) { v = &candidate; break; }
            if (candidate.started < v->started) v = &candidate;
        }
        *v = {};
        v->active = v->held = true;
        v->channel = ch;
        v->note = e.bytes[1];
        v->freq = v->targetFreq = noteToHz (pitchOf (ch, v->note, bendRange));
        v->started = ++counter;
    }
    else if (type == 0x80 || type == 0x90)
    {
        for (auto& v : voices)
            if (v.active && v.held && v.channel == ch && v.note == e.bytes[1])
                v.held = false;
    }
}

void PreviewSynth::render (float* left, float* right, int start, int end)
{
    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        for (int i = start; i < end; ++i)
        {
            v.env = v.held ? v.env + (1.0f - v.env) * attackCoef : v.env * releaseCoef;
            v.freq += (v.targetFreq - v.freq) * glideCoef;
            v.phase += v.freq / sampleRate;
            v.phase -= std::floor (v.phase);

            // Sine plus a little 2nd harmonic: clear pitch, still a single zero crossing per cycle.
            const float s = gain * v.env * (float) (std::sin (twoPi * v.phase) + 0.25 * std::sin (2.0 * twoPi * v.phase));
            left[i] += s;
            right[i] += s;
        }

        if (! v.held && v.env < 1.0e-4f)
            v.active = false;
    }
}

void PreviewSynth::process (const MidiOut& events, int bendRange, float* left, float* right, int numSamples)
{
    int pos = 0;
    for (const auto& e : events)
    {
        const int at = std::clamp (e.sample, pos, numSamples);
        render (left, right, pos, at);
        pos = at;
        handle (e, bendRange);
    }
    render (left, right, pos, numSamples);
}

} // namespace pitchpaint
