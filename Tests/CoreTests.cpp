// Plain console tests for the JUCE-free core (Pattern + MidiRenderer). Run via ctest or directly.
#include "Core/MidiRenderer.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <vector>

using namespace pitchpaint;

namespace
{
int failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (! (cond)) {                                                          \
            std::printf ("    FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++failures;                                                          \
        }                                                                        \
    } while (false)

constexpr double sampleRate = 48000.0;
constexpr double bps = 120.0 / 60.0 / sampleRate; // beats per sample at 120 BPM

int typeOf (const MidiEvent& e) { return e.bytes[0] & 0xF0; }
int channelOf (const MidiEvent& e) { return (e.bytes[0] & 0x0F) + 1; }
bool isNoteOn (const MidiEvent& e) { return typeOf (e) == 0x90 && e.bytes[2] > 0; }
bool isNoteOff (const MidiEvent& e) { return typeOf (e) == 0x80; }
bool isBend (const MidiEvent& e) { return typeOf (e) == 0xE0; }
int bendOf (const MidiEvent& e) { return e.bytes[1] | (e.bytes[2] << 7); }

struct Absolute
{
    long long sample;
    MidiEvent event;
};

Trajectory line (std::uint32_t id, double b0, double p0, double b1, double p1)
{
    return { id, { { b0, p0 }, { b1, p1 } } };
}

// Renders [0, totalSamples) in blocks of `blockSize` and returns events with absolute sample positions.
std::vector<Absolute> renderAll (MidiRenderer& r, const Pattern& p, long long totalSamples, int blockSize, double startBeat = 0.0)
{
    std::vector<Absolute> all;
    MidiOut out;
    for (long long s = 0; s < totalSamples; s += blockSize)
    {
        const int n = (int) std::min<long long> (blockSize, totalSamples - s);
        out.clear();
        r.render (p, startBeat + s * bps, bps, n, out);
        for (const auto& e : out)
            all.push_back ({ s + e.sample, e });
    }
    return all;
}

double pitchFromBend (int bend, int note, int range) { return note + (bend - 8192) / 8191.0 * range; }

long long beatToSample (double beat) { return (long long) std::ceil (beat / bps - 1.0e-6); }

// ---------------------------------------------------------------------------------------------------------------

void testPitchInterpolation()
{
    Trajectory t { 1, { { 1.0, 60.0 }, { 2.0, 62.0 }, { 4.0, 58.0 } } };
    CHECK (t.pitchAt (0.0) == 60.0);
    CHECK (t.pitchAt (1.5) == 61.0);
    CHECK (t.pitchAt (3.0) == 60.0);
    CHECK (t.pitchAt (9.0) == 58.0);
    CHECK (t.isPlayable());
    const Trajectory single { 2, { { 1.0, 60.0 } } };
    CHECK (! single.isPlayable());
}

void testMpeConfigMessages()
{
    MidiRenderer r;
    MidiOut out;
    r.configure ({ OutputMode::mpe, 48, 15 }, out, 0);

    // MCM on channel 1: CC101=0, CC100=6, CC6=15
    CHECK (out.size() == 6 * 16);
    CHECK (channelOf (out[0]) == 1 && out[1].bytes[1] == 100 && out[1].bytes[2] == 6 && out[2].bytes[1] == 6 && out[2].bytes[2] == 15);
    // RPN 0 (bend range) = 48 on channel 2
    CHECK (channelOf (out[6]) == 2 && out[7].bytes[2] == 0 && out[8].bytes[1] == 6 && out[8].bytes[2] == 48);
    CHECK (channelOf (out.back()) == 16);
}

void testSingleNoteTimingAndBend()
{
    MidiRenderer r;
    MidiOut setup;
    r.configure ({ OutputMode::mpe, 48 }, setup, 0);

    Pattern p;
    p.lengthBeats = 8.0;
    p.trajectories.push_back (line (1, 1.0, 60.0, 3.0, 64.0));

    const auto ev = renderAll (r, p, beatToSample (8.0), 512);

    std::vector<Absolute> ons, offs;
    for (const auto& a : ev)
    {
        if (isNoteOn (a.event)) ons.push_back (a);
        if (isNoteOff (a.event)) offs.push_back (a);
    }

    CHECK (ons.size() == 1 && offs.size() == 1);
    if (ons.size() != 1 || offs.size() != 1)
        return;

    CHECK (ons[0].sample == beatToSample (1.0));  // sample-accurate start
    CHECK (offs[0].sample == beatToSample (3.0)); // sample-accurate end
    CHECK (channelOf (ons[0].event) == 2);
    CHECK (ons[0].event.bytes[1] == 62); // note centred on the curve (60..64)

    // The bend right before the note-on, same timestamp, puts the attack at 60.
    size_t onIndex = 0;
    while (! isNoteOn (ev[onIndex].event)) ++onIndex;
    CHECK (onIndex > 0 && isBend (ev[onIndex - 1].event) && ev[onIndex - 1].sample == ons[0].sample);
    CHECK (std::abs (pitchFromBend (bendOf (ev[onIndex - 1].event), 62, 48) - 60.0) < 0.01);

    // Every bend while sounding matches the curve to within one bend step interval.
    double maxErr = 0.0;
    for (const auto& a : ev)
        if (isBend (a.event) && a.sample >= ons[0].sample && a.sample < offs[0].sample)
        {
            const double beat = a.sample * bps;
            maxErr = std::max (maxErr, std::abs (pitchFromBend (bendOf (a.event), 62, 48) - (60.0 + 2.0 * (beat - 1.0))));
        }
    CHECK (maxErr < 0.01);
}

void testBlockSizeIndependence()
{
    Pattern p;
    p.lengthBeats = 4.0;
    p.trajectories.push_back (line (1, 0.0, 60.0, 3.7, 55.0));
    p.trajectories.push_back (line (2, 1.3, 70.0, 3.9, 71.0));
    p.trajectories.push_back ({ 3, { { 0.25, 50.0 }, { 0.5, 52.5 }, { 2.0, 49.0 } } });

    std::vector<std::vector<Absolute>> results;
    for (const int block : { 64, 512, 37, 1000, 4096 })
    {
        MidiRenderer r;
        MidiOut setup;
        r.configure ({ OutputMode::mpe, 48 }, setup, 0);
        results.push_back (renderAll (r, p, beatToSample (12.0), block));
    }

    // Bend updates sit on a 64-sample grid relative to block starts, so only compare note events exactly.
    const auto notes = [] (const std::vector<Absolute>& v)
    {
        std::vector<std::string> s;
        for (const auto& a : v)
            if (isNoteOn (a.event) || isNoteOff (a.event))
                s.push_back (std::to_string (a.sample) + ":" + std::to_string (a.event.bytes[0]) + ":" + std::to_string (a.event.bytes[1]));
        return s;
    };

    for (size_t i = 1; i < results.size(); ++i)
    {
        const auto a = notes (results[0]), b = notes (results[i]);
        CHECK (a == b);
        for (size_t k = 0; k < std::max (a.size(), b.size()); ++k)
            if (k >= a.size() || k >= b.size() || a[k] != b[k])
            {
                std::printf ("    run %zu differs at #%zu: %s vs %s\n", i, k, k < a.size() ? a[k].c_str() : "-", k < b.size() ? b[k].c_str() : "-");
                break;
            }
    }
    CHECK (notes (results[0]).size() == 3 * 3 * 2); // 3 trajectories x 3 loops, on + off
}

void testOverlappingVoicesUseDistinctChannels()
{
    MidiRenderer r;
    MidiOut setup;
    r.configure ({ OutputMode::mpe, 48 }, setup, 0);

    Pattern p;
    p.lengthBeats = 8.0;
    p.trajectories.push_back (line (1, 0.0, 60.0, 4.0, 60.0));
    p.trajectories.push_back (line (2, 1.0, 64.0, 5.0, 64.0));
    p.trajectories.push_back (line (3, 2.0, 67.0, 3.0, 67.0));

    std::set<int> channels;
    for (const auto& a : renderAll (r, p, beatToSample (2.5), 256))
        if (isNoteOn (a.event))
            channels.insert (channelOf (a.event));

    CHECK (channels == std::set<int> ({ 2, 3, 4 }));
    CHECK (r.numSoundingVoices() == 3);
}

void testFreedChannelIsNotReusedImmediately()
{
    MidiRenderer r;
    MidiOut setup;
    r.configure ({ OutputMode::mpe, 48, 3 }, setup, 0);

    Pattern p;
    p.lengthBeats = 16.0;
    p.trajectories.push_back (line (1, 0.0, 60.0, 1.0, 60.0)); // ch 2, released at beat 1
    p.trajectories.push_back (line (2, 2.0, 62.0, 3.0, 62.0)); // should take ch 3 (never used), not ch 2
    p.trajectories.push_back (line (3, 4.0, 64.0, 5.0, 64.0)); // ch 4
    p.trajectories.push_back (line (4, 6.0, 65.0, 7.0, 65.0)); // back to ch 2, the longest-free

    std::vector<int> order;
    for (const auto& a : renderAll (r, p, beatToSample (8.0), 512))
        if (isNoteOn (a.event))
            order.push_back (channelOf (a.event));

    CHECK (order == std::vector<int> ({ 2, 3, 4, 2 }));
}

void testVoiceStealingWhenChannelsRunOut()
{
    MidiRenderer r;
    MidiOut setup;
    r.configure ({ OutputMode::mpe, 48, 2 }, setup, 0);

    Pattern p;
    p.lengthBeats = 8.0;
    p.trajectories.push_back (line (1, 0.0, 60.0, 4.0, 60.0));
    p.trajectories.push_back (line (2, 1.0, 62.0, 4.0, 62.0));
    p.trajectories.push_back (line (3, 2.0, 64.0, 4.0, 64.0)); // steals the oldest (trajectory 1, channel 2)

    int maxHeld = 0, held = 0;
    bool stoleFromChannel2 = false;
    for (const auto& a : renderAll (r, p, beatToSample (3.0), 512))
    {
        if (isNoteOn (a.event)) maxHeld = std::max (maxHeld, ++held);
        if (isNoteOff (a.event))
        {
            --held;
            stoleFromChannel2 |= channelOf (a.event) == 2 && a.event.bytes[1] == 60 && a.sample == beatToSample (2.0);
        }
    }

    CHECK (maxHeld == 2);
    CHECK (stoleFromChannel2);
}

void testMonoLatestStartWinsAndResumes()
{
    MidiRenderer r;
    MidiOut setup;
    r.configure ({ OutputMode::mono, 12 }, setup, 0);

    Pattern p;
    p.lengthBeats = 8.0;
    p.trajectories.push_back (line (1, 0.0, 60.0, 6.0, 60.0));
    p.trajectories.push_back (line (2, 2.0, 67.0, 4.0, 67.0));

    std::vector<std::pair<long long, int>> ons, offs;
    for (const auto& a : renderAll (r, p, beatToSample (7.0), 512))
    {
        CHECK (channelOf (a.event) == 1);
        if (isNoteOn (a.event)) ons.push_back ({ a.sample, a.event.bytes[1] });
        if (isNoteOff (a.event)) offs.push_back ({ a.sample, a.event.bytes[1] });
    }

    using P = std::vector<std::pair<long long, int>>;
    CHECK (ons == P ({ { 0, 60 }, { beatToSample (2.0), 67 }, { beatToSample (4.0), 60 } }));
    CHECK (offs == P ({ { beatToSample (2.0), 60 }, { beatToSample (4.0), 67 }, { beatToSample (6.0), 60 } }));
}

void testRetriggerBeyondBendRange()
{
    MidiRenderer r;
    MidiOut setup;
    r.configure ({ OutputMode::mono, 2 }, setup, 0);

    Pattern p;
    p.lengthBeats = 8.0;
    p.trajectories.push_back (line (1, 0.0, 60.0, 4.0, 70.0)); // 10 semitones > 2 * 2

    const auto ev = renderAll (r, p, beatToSample (3.99), 512);
    int note = -1, noteOns = 0;
    double maxErr = 0.0;
    for (size_t i = 0; i < ev.size(); ++i)
    {
        const auto& a = ev[i];
        if (isNoteOn (a.event))
        {
            note = a.event.bytes[1];
            ++noteOns;
        }
        if (isBend (a.event))
        {
            // A bend right before a note-on belongs to that new note.
            const bool precedesNoteOn = i + 1 < ev.size() && isNoteOn (ev[i + 1].event) && ev[i + 1].sample == a.sample;
            const int n = precedesNoteOn ? ev[i + 1].event.bytes[1] : note;
            maxErr = std::max (maxErr, std::abs (pitchFromBend (bendOf (a.event), n, 2) - (60.0 + 2.5 * a.sample * bps)));
        }
    }
    CHECK (noteOns >= 3 && noteOns <= 6);
    CHECK (maxErr < 0.05);
}

void testLoopWraps()
{
    MidiRenderer r;
    MidiOut setup;
    r.configure ({ OutputMode::mpe, 48 }, setup, 0);

    Pattern p;
    p.lengthBeats = 2.0;
    p.trajectories.push_back (line (1, 0.5, 60.0, 1.5, 60.0));

    std::vector<long long> ons;
    for (const auto& a : renderAll (r, p, beatToSample (6.0), 333, 0.0))
        if (isNoteOn (a.event))
            ons.push_back (a.sample);

    CHECK (ons == std::vector<long long> ({ beatToSample (0.5), beatToSample (2.5), beatToSample (4.5) }));
}

void testAllNotesOffAndDeletedTrajectory()
{
    MidiRenderer r;
    MidiOut out;
    r.configure ({ OutputMode::mpe, 48 }, out, 0);

    Pattern p;
    p.lengthBeats = 8.0;
    p.trajectories.push_back (line (7, 0.0, 60.0, 8.0, 60.0));
    p.trajectories.push_back (line (8, 0.0, 64.0, 8.0, 64.0));

    out.clear();
    r.render (p, 0.0, bps, 512, out);
    CHECK (r.numSoundingVoices() == 2);

    // Deleting a sounding trajectory releases its note on the next block.
    p.trajectories.pop_back();
    out.clear();
    r.render (p, 512 * bps, bps, 512, out);
    CHECK (r.numSoundingVoices() == 1);
    CHECK (! out.empty() && isNoteOff (out.front()) && out.front().bytes[1] == 64);

    out.clear();
    r.allNotesOff (out, 10);
    CHECK (r.numSoundingVoices() == 0);
    CHECK (out.size() == 1 && isNoteOff (out[0]) && out[0].sample == 10);
}

void testSwitchingModeReleasesOldChannels()
{
    MidiRenderer r;
    MidiOut out;
    r.configure ({ OutputMode::mpe, 48 }, out, 0);

    Pattern p;
    p.lengthBeats = 8.0;
    p.trajectories.push_back (line (1, 0.0, 60.0, 8.0, 60.0));
    r.render (p, 0.0, bps, 512, out);

    out.clear();
    r.configure ({ OutputMode::mono, 12 }, out, 0);
    CHECK (! out.empty() && isNoteOff (out[0]) && channelOf (out[0]) == 2);
    CHECK (r.numSoundingVoices() == 0);
}
} // namespace

int main()
{
    const std::pair<const char*, std::function<void()>> tests[] = {
        { "pitch interpolation", testPitchInterpolation },
        { "MPE config messages", testMpeConfigMessages },
        { "single note timing and bend", testSingleNoteTimingAndBend },
        { "block size independence", testBlockSizeIndependence },
        { "overlapping voices use distinct channels", testOverlappingVoicesUseDistinctChannels },
        { "freed channel not reused immediately", testFreedChannelIsNotReusedImmediately },
        { "voice stealing", testVoiceStealingWhenChannelsRunOut },
        { "mono latest start wins and resumes", testMonoLatestStartWinsAndResumes },
        { "retrigger beyond bend range", testRetriggerBeyondBendRange },
        { "loop wrap", testLoopWraps },
        { "all notes off / deleted trajectory", testAllNotesOffAndDeletedTrajectory },
        { "mode switch releases old channels", testSwitchingModeReleasesOldChannels },
    };

    for (const auto& [name, fn] : tests)
    {
        const int before = failures;
        fn();
        std::printf ("%s %s\n", failures == before ? "[pass]" : "[FAIL]", name);
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILURES", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
