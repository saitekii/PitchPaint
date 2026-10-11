// Plain console tests for the JUCE-free core (Pattern + MidiRenderer). Run via ctest or directly.
#include "Core/MidiRenderer.h"
#include "Core/PatternSnapshots.h"
#include "Core/PreviewSynth.h"
#include "Core/UndoHistory.h"

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

std::vector<double> beatsOf (const Trajectory& t)
{
    std::vector<double> b;
    for (const auto& p : t.points)
        b.push_back (p.beat);
    return b;
}

void testDrawSegment()
{
    Trajectory t { 1, { { 1.0, 60.0 } } };
    drawSegment (t, { 1.0, 60.0 }, { 1.5, 61.0 });
    drawSegment (t, { 1.5, 61.0 }, { 2.0, 62.0 });
    drawSegment (t, { 2.0, 62.0 }, { 3.0, 63.0 });
    CHECK (beatsOf (t) == std::vector<double> ({ 1.0, 1.5, 2.0, 3.0 }));

    // Dragging back over the stroke redraws it: points between are replaced.
    drawSegment (t, { 3.0, 63.0 }, { 1.2, 50.0 });
    CHECK (beatsOf (t) == std::vector<double> ({ 1.0, 1.2, 3.0 }));
    CHECK (t.pitchAt (1.2) == 50.0);

    // Continuing left past the start extends the stroke.
    drawSegment (t, { 1.2, 50.0 }, { 0.5, 55.0 });
    CHECK (beatsOf (t) == std::vector<double> ({ 0.5, 1.2, 3.0 }));

    // Same beat: replaces the pitch there.
    drawSegment (t, { 0.5, 55.0 }, { 0.5, 57.0 });
    CHECK (t.points.size() == 3 && t.points.front().pitch == 57.0);
}

void testSnapshots()
{
    Pattern p;
    p.trajectories.push_back (line (1, 0.0, 60.0, 1.0, 60.0));
    PatternSnapshots snaps (p);
    CHECK (p.nextId() == 2);

    auto held = snaps.acquire(); // as if the audio thread were mid-block
    p.trajectories.clear();
    snaps.publish (p);
    snaps.publish (p);

    CHECK (held->trajectories.size() == 1);     // still alive while held
    CHECK (snaps.acquire()->trajectories.empty());
    CHECK (held.use_count() == 2);              // retained by the store, so releasing `held` won't free it
}

void testSerialiseRoundTrip()
{
    Pattern p;
    p.lengthBeats = 12.0;
    p.trajectories.push_back ({ 3, { { 0.1, 60.123456789012345 }, { 1.0 / 3.0, 61.0 }, { 2.0, 1e-9 } } });
    p.trajectories.push_back ({ 7, { { 4.0, 127.0 }, { 5.5, 0.0 } }, 1 });
    p.trajectories.push_back ({ 9, {}, 127 });

    const auto text = serialise (p);
    const auto back = parsePattern (text);
    CHECK (back.has_value() && *back == p); // bit-exact doubles, velocities included

    CHECK (parsePattern (serialise (Pattern {})) == Pattern {});
    CHECK (! parsePattern ("").has_value());
    CHECK (! parsePattern ("PitchPaintPattern 4\nlength 4\n").has_value());              // unknown future version

    // Tuning round-trips; a malformed tuning line is rejected; version 2 (no tuning) loads with the default.
    Pattern tuned = p;
    tuned.tuning = { 9, 2, 0.35 };
    CHECK (parsePattern (serialise (tuned)) == tuned);
    CHECK (! parsePattern ("PitchPaintPattern 3\nlength 4\ntuning 12 0 0\n").has_value());
    const auto v2 = parsePattern ("PitchPaintPattern 2\nlength 4\nt 1 v 90 0 60 1 62\n");
    CHECK (v2.has_value() && v2->tuning == Tuning {} && v2->trajectories.size() == 1 && v2->trajectories[0].velocity == 90);
    CHECK (! parsePattern ("PitchPaintPattern 2\nlength 4\nt 1 v 0 0 60 1 60\n").has_value()); // velocity out of range

    // Version 1 (saved before velocity existed) loads with the default velocity.
    const auto v1 = parsePattern ("PitchPaintPattern 1\nlength 4\nt 1 0 60 1 62\n");
    CHECK (v1.has_value() && v1->trajectories.size() == 1 && v1->trajectories[0].velocity == Trajectory::defaultVelocity);
    CHECK (! parsePattern ("PitchPaintPattern 1\nlength 4\nt 1 0.5\n").has_value());   // beat without pitch
    CHECK (! parsePattern ("PitchPaintPattern 1\nlength 4\nt 1 0.5 nan\n").has_value());
    CHECK (! parsePattern ("PitchPaintPattern 1\nlength -1\n").has_value());
    const auto unsorted = parsePattern ("PitchPaintPattern 1\r\nlength 4\r\nt 2 3 60 1 62\r\n");
    CHECK (unsorted.has_value() && unsorted->trajectories.size() == 1 && unsorted->trajectories[0].points[0].beat == 1.0);
}

void testMoveTrajectories()
{
    Pattern p;
    p.lengthBeats = 8.0;
    p.trajectories.push_back (line (1, 1.0, 60.0, 2.0, 62.0));
    p.trajectories.push_back (line (2, 3.0, 70.0, 5.0, 70.0));
    p.trajectories.push_back (line (3, 0.0, 50.0, 1.0, 50.0));

    moveTrajectories (p, { 1, 2 }, 1.5, -2.0);
    CHECK (p.trajectories[0].startBeat() == 2.5 && p.trajectories[0].points[1].pitch == 60.0);
    CHECK (p.trajectories[1].endBeat() == 6.5 && p.trajectories[1].points[0].pitch == 68.0);
    CHECK (p.trajectories[2].startBeat() == 0.0); // unselected untouched

    // Limited so the selection stays inside the loop and the MIDI range, moving as a group.
    moveTrajectories (p, { 1, 2 }, 10.0, 100.0);
    CHECK (p.trajectories[1].endBeat() == 8.0);
    CHECK (p.trajectories[0].startBeat() == 4.0);
    CHECK (p.trajectories[1].points[0].pitch == 127.0);
    moveTrajectories (p, { 3 }, -5.0, -60.0);
    CHECK (p.trajectories[2].startBeat() == 0.0 && p.trajectories[2].points[0].pitch == 0.0);
}

void testCutTrajectory()
{
    const auto make = []
    {
        Pattern p;
        p.lengthBeats = 8.0;
        p.trajectories.push_back (line (1, 0.0, 60.0, 1.0, 60.0));
        p.trajectories.push_back ({ 5, { { 2.0, 60.0 }, { 3.0, 64.0 }, { 6.0, 58.0 } } });
        p.trajectories.push_back (line (3, 7.0, 50.0, 8.0, 50.0));
        return p;
    };

    // Cut from the middle: two parts with exact end pitches, right part directly after the left, new id.
    auto p = make();
    cutTrajectory (p, 5, 2.5, 4.0);
    CHECK (p.trajectories.size() == 4);
    if (p.trajectories.size() == 4)
    {
        const auto& l = p.trajectories[1];
        const auto& r = p.trajectories[2];
        CHECK (l.id == 5 && l.startBeat() == 2.0 && l.endBeat() == 2.5 && l.points.back().pitch == 62.0);
        CHECK (r.id == 6 && r.startBeat() == 4.0 && r.endBeat() == 6.0 && r.points.front().pitch == 62.0);
        CHECK (r.points.size() == 2 && l.points.size() == 2);
        CHECK (p.trajectories[3].id == 3);
    }

    // Cut the start / the end: one part remains, and it keeps the original id.
    p = make();
    cutTrajectory (p, 5, 1.0, 3.0);
    CHECK (p.trajectories.size() == 3 && p.trajectories[1].id == 6 && p.trajectories[1].startBeat() == 3.0);
    p = make();
    cutTrajectory (p, 5, 5.0, 9.0);
    CHECK (p.trajectories.size() == 3 && p.trajectories[1].id == 5 && p.trajectories[1].endBeat() == 5.0
           && p.trajectories[1].points.back().pitch == 60.0);

    // Covering it entirely removes it; slivers shorter than minLength are dropped; no overlap changes nothing.
    p = make();
    cutTrajectory (p, 5, 1.0, 7.0);
    CHECK (p.trajectories.size() == 2);
    p = make();
    cutTrajectory (p, 5, 2.01, 5.99);
    CHECK (p.trajectories.size() == 2);
    p = make();
    cutTrajectory (p, 5, 6.5, 7.0);
    CHECK (p == make());
    cutTrajectory (p, 99, 0.0, 8.0); // unknown id
    CHECK (p == make());
}

void testVelocity()
{
    // Note-ons carry each stroke's own velocity: in MPE, in mono, and on retriggers beyond the bend range.
    Pattern p;
    p.lengthBeats = 8.0;
    p.trajectories.push_back ({ 1, { { 0.0, 60.0 }, { 2.0, 60.0 } }, 30 });
    p.trajectories.push_back ({ 2, { { 1.0, 64.0 }, { 3.0, 64.0 } }, 127 });
    p.trajectories.push_back ({ 3, { { 4.0, 60.0 }, { 6.0, 70.0 } }, 77 }); // 10 semitones: retriggers at +-2

    for (const auto& settings : { RenderSettings { OutputMode::mpe, 48 }, RenderSettings { OutputMode::mono, 2 } })
    {
        MidiRenderer r;
        MidiOut setup;
        r.configure (settings, setup, 0);
        std::vector<std::pair<int, int>> ons; // note, velocity
        for (const auto& a : renderAll (r, p, beatToSample (6.0), 512))
            if (isNoteOn (a.event))
                ons.push_back ({ a.event.bytes[1], a.event.bytes[2] });

        // MPE (+-48) needs no retrigger; mono (+-2) retriggers stroke 3 several times.
        CHECK (settings.mode == OutputMode::mpe ? ons.size() == 3 : ons.size() >= 5);
        if (ons.size() >= 3)
            CHECK (ons[0].second == 30 && ons[1].second == 127);
        for (size_t i = 2; i < ons.size(); ++i)
            CHECK (ons[i].second == 77 || (settings.mode == OutputMode::mono && ons[i].second == 30)); // mono resumes 1
    }

    // Cutting keeps the velocity on both parts.
    cutTrajectory (p, 1, 0.5, 1.0);
    CHECK (p.trajectories[0].velocity == 30 && p.trajectories[1].velocity == 30);

    // The preview sound's level follows velocity (linear): velocity 64 is about half as loud as 127.
    const auto rmsFor = [] (int velocity)
    {
        PreviewSynth synth;
        synth.prepare (sampleRate);
        MidiOut events { { 0, { 0x91, 69, (std::uint8_t) velocity } } };
        std::vector<float> l (9600, 0.0f), r (9600, 0.0f);
        synth.process (events, 48, l.data(), r.data(), 9600);
        double sum = 0.0;
        for (size_t i = 4800; i < l.size(); ++i)
            sum += l[i] * l[i];
        return std::sqrt (sum / 4800.0);
    };
    CHECK (std::abs (rmsFor (64) / rmsFor (127) - 64.0 / 127.0) < 0.01);
}

void testTuning()
{
    // Scale membership and nearest scale note (D minor: D E F G A Bb C).
    const Tuning dMinor { 2, 2, 0.0 };
    CHECK (dMinor.inScale (62) && dMinor.inScale (65) && dMinor.inScale (70) && ! dMinor.inScale (66) && ! dMinor.inScale (61));
    CHECK (dMinor.nearestScaleNote (65.9) == 65.0 && dMinor.nearestScaleNote (66.3) == 67.0); // F# is out: F or G
    CHECK (Tuning {}.nearestScaleNote (60.4) == 60.0);

    // Magnet 0 changes nothing; scale notes and midpoints between neighbouring scale notes never move.
    for (double pitch = 50.0; pitch < 70.0; pitch += 0.173)
        CHECK (Tuning {}.apply (pitch) == pitch);
    const Tuning strong { 0, 1, 1.0 }; // C major, full magnet
    CHECK (strong.apply (64.0) == 64.0 && std::abs (strong.apply (64.5) - 64.5) < 1e-12); // E-F is a half step
    CHECK (std::abs (strong.apply (61.0) - 61.0) < 1e-12);                                  // C-D midpoint

    // It pulls toward notes: 20 cents off becomes well under a cent at full strength, and less pull when weaker.
    CHECK (std::abs (strong.apply (60.2) - 60.0) < 0.01);
    const Tuning medium { 0, 0, 0.5 };
    const double mediumOffset = medium.apply (60.2) - 60.0;
    CHECK (mediumOffset > 0.0 && mediumOffset < 0.05);

    // Continuous and monotonic across a whole range: glides stay glides (no jumps), only their shape changes.
    double previous = strong.apply (55.0);
    double biggestStep = 0.0;
    for (double pitch = 55.0005; pitch < 72.0; pitch += 0.0005)
    {
        const double v = strong.apply (pitch);
        CHECK (v >= previous);
        biggestStep = std::max (biggestStep, v - previous);
        previous = v;
    }
    CHECK (biggestStep < 0.01);

    // The renderer plays the tuned pitch: a stroke held 30 cents sharp of E4 sounds as E4 with a full magnet.
    Pattern p;
    p.lengthBeats = 4.0;
    p.trajectories.push_back (line (1, 0.0, 64.3, 2.0, 64.3));
    p.tuning = strong;
    MidiRenderer r;
    MidiOut out;
    r.configure ({ OutputMode::mpe, 48 }, out, 0);
    out.clear();
    r.render (p, 0.0, bps, 512, out);
    int bend = -1, note = -1;
    for (const auto& e : out)
    {
        if (isBend (e) && bend < 0) bend = bendOf (e);
        if (isNoteOn (e)) note = e.bytes[1];
    }
    CHECK (note == 64 && std::abs (pitchFromBend (bend, 64, 48) - 64.0) < 0.02); // within 2 cents
}

void testSmoothing()
{
    // A held note with hand jitter (+-0.3 semitones): smoothing removes most of it, keeps the ends.
    Trajectory t { 1, {}, 90 };
    unsigned seed = 1;
    for (int i = 0; i <= 200; ++i)
    {
        seed = seed * 1103515245u + 12345u;
        const double jitter = ((seed >> 16) % 1000) / 1000.0 * 0.6 - 0.3;
        t.points.push_back ({ 1.0 + i * 0.01, 60.0 + jitter });
    }
    const auto roughness = [] (const Trajectory& tr)
    {
        double sum = 0.0;
        for (size_t i = 1; i + 1 < tr.points.size(); ++i)
            sum += std::abs (tr.points[i + 1].pitch - 2.0 * tr.points[i].pitch + tr.points[i - 1].pitch);
        return sum / (double) tr.points.size();
    };
    const auto original = t;
    smoothTrajectory (t, 1.0 / 16.0);
    CHECK (t.startBeat() == original.startBeat() && t.endBeat() == original.endBeat());
    CHECK (t.points.front().pitch == original.points.front().pitch && t.points.back().pitch == original.points.back().pitch);
    CHECK (t.velocity == 90);
    CHECK (roughness (t) < roughness (original) * 0.1);
    double maxDeviation = 0.0;
    for (size_t i = 3; i + 3 < t.points.size(); ++i)
        maxDeviation = std::max (maxDeviation, std::abs (t.points[i].pitch - 60.0));
    CHECK (maxDeviation < 0.15);

    // A clean glide stays a glide (start and end pitches kept, still monotonic).
    Trajectory glide = line (2, 0.0, 60.0, 2.0, 67.0);
    smoothTrajectory (glide, 0.25);
    CHECK (glide.points.front().pitch == 60.0 && glide.points.back().pitch == 67.0);
    for (size_t i = 1; i < glide.points.size(); ++i)
        CHECK (glide.points[i].pitch >= glide.points[i - 1].pitch);
}

void testTimeGrid()
{
    CHECK (snapBeat (1.13, 0.25) == 1.25 && snapBeat (1.12, 0.25) == 1.0 && snapBeat (1.13, 0.0) == 1.13);
    CHECK (std::abs (snapBeat (0.30, 1.0 / 3.0) - 1.0 / 3.0) < 1e-12); // triplets

    // Drawn from 1.1 to 2.8 on a one-beat grid: becomes exactly 1.0 .. 3.0, with every drawn point kept.
    Trajectory t { 1, { { 1.1, 60.0 }, { 2.0, 62.0 }, { 2.8, 61.0 } } };
    snapEnds (t, 1.0, 16.0);
    CHECK (t.startBeat() == 1.0 && t.endBeat() == 3.0);
    CHECK (t.points.size() == 5 && t.points[2] == (PitchPoint { 2.0, 62.0 }));
    CHECK (t.points.front().pitch == 60.0 && t.points.back().pitch == 61.0); // ends held where extended

    // Trimmed when the end rounds inward: the end pitch is interpolated, not jumped.
    Trajectory trim { 2, { { 0.0, 60.0 }, { 1.2, 61.2 } } };
    snapEnds (trim, 1.0, 16.0);
    CHECK (trim.endBeat() == 1.0 && std::abs (trim.points.back().pitch - 61.0) < 1e-12);

    // Very short strokes become one grid step; strokes near the loop end stay inside the loop.
    Trajectory tiny { 3, { { 4.02, 60.0 }, { 4.1, 60.0 } } };
    snapEnds (tiny, 0.25, 16.0);
    CHECK (tiny.startBeat() == 4.0 && tiny.endBeat() == 4.25);
    Trajectory atEnd { 4, { { 15.9, 60.0 }, { 15.95, 60.0 } } };
    snapEnds (atEnd, 1.0, 16.0);
    CHECK (atEnd.startBeat() == 15.0 && atEnd.endBeat() == 16.0);
}

void testUndoHistory()
{
    Pattern a, b, c;
    b.lengthBeats = 4.0;
    c.lengthBeats = 8.0;

    UndoHistory h;
    CHECK (! h.canUndo() && ! h.undo (a).has_value());
    h.record (a); // a -> b
    h.record (b); // b -> c
    auto r = h.undo (c);
    CHECK (r && *r == b);
    r = h.undo (b);
    CHECK (r && *r == a && ! h.canUndo());
    r = h.redo (a);
    CHECK (r && *r == b && h.canRedo());

    h.record (b); // new edit from b clears redo
    CHECK (! h.canRedo());

    for (size_t i = 0; i < UndoHistory::maxSteps + 10; ++i)
        h.record (a);
    int steps = 0;
    Pattern cur;
    while (auto prev = h.undo (cur)) { cur = *prev; ++steps; }
    CHECK (steps == (int) UndoHistory::maxSteps);
}

// Frequency from rising zero crossings (interpolated) within [from, to).
double measureHz (const std::vector<float>& x, int from, int to)
{
    double first = -1.0, last = -1.0;
    int count = 0;
    for (int i = from + 1; i < to; ++i)
        if (x[(size_t) i - 1] < 0.0f && x[(size_t) i] >= 0.0f)
        {
            const double t = (i - 1) + x[(size_t) i - 1] / (x[(size_t) i - 1] - x[(size_t) i]);
            if (first < 0.0) first = t; else ++count;
            last = t;
        }
    return count > 0 ? count * sampleRate / (last - first) : 0.0;
}

void testPreviewSynthPlaysDrawnPitch()
{
    // Draw C4 held flat, then a glide up an octave, render through renderer + preview synth, measure Hz.
    Pattern p;
    p.lengthBeats = 8.0;
    p.trajectories.push_back ({ 1, { { 0.0, 60.0 }, { 2.0, 60.0 }, { 4.0, 72.0 }, { 6.0, 72.0 } } });

    for (const auto& settings : { RenderSettings { OutputMode::mpe, 48 }, RenderSettings { OutputMode::mono, 2 } })
    {
        MidiRenderer r;
        PreviewSynth synth;
        synth.prepare (sampleRate);
        MidiOut out;
        r.configure (settings, out, 0);

        const int total = (int) beatToSample (6.0), block = 480;
        std::vector<float> left ((size_t) total, 0.0f), right ((size_t) total, 0.0f);
        for (int s = 0; s < total; s += block)
        {
            const int n = std::min (block, total - s);
            if (s > 0)
                out.clear();
            r.render (p, s * bps, bps, n, out);
            synth.process (out, settings.bendRange, left.data() + s, right.data() + s, n);
        }

        const auto pitchAt = [&] (double beat)
        {
            const int c = (int) beatToSample (beat), w = 2400; // 50 ms window
            return 69.0 + 12.0 * std::log2 (measureHz (left, c - w / 2, c + w / 2) / 440.0);
        };

        CHECK (std::abs (pitchAt (1.0) - 60.0) < 0.02);  // C4 = 261.63 Hz
        CHECK (std::abs (pitchAt (3.0) - 66.0) < 0.05);  // half-way up the glide
        CHECK (std::abs (pitchAt (5.0) - 72.0) < 0.02);  // C5, reached by bend (MPE) or by retriggers (mono ±2)
        std::printf ("    [%s] beat1 %.3f  beat3 %.3f  beat5 %.3f\n", settings.mode == OutputMode::mpe ? "mpe " : "mono",
                     pitchAt (1.0), pitchAt (3.0), pitchAt (5.0));
    }
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
    CHECK (ons[0].event.bytes[1] == 60); // note = nearest semitone to where the curve starts

    // The bend right before the note-on, same timestamp, puts the attack at 60.
    size_t onIndex = 0;
    while (! isNoteOn (ev[onIndex].event)) ++onIndex;
    CHECK (onIndex > 0 && isBend (ev[onIndex - 1].event) && ev[onIndex - 1].sample == ons[0].sample);
    CHECK (std::abs (pitchFromBend (bendOf (ev[onIndex - 1].event), 60, 48) - 60.0) < 0.01);

    // Every bend while sounding matches the curve to within one bend step interval.
    double maxErr = 0.0;
    for (const auto& a : ev)
        if (isBend (a.event) && a.sample >= ons[0].sample && a.sample < offs[0].sample)
        {
            const double beat = a.sample * bps;
            maxErr = std::max (maxErr, std::abs (pitchFromBend (bendOf (a.event), 60, 48) - (60.0 + 2.0 * (beat - 1.0))));
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
        { "pencil draw segment", testDrawSegment },
        { "pattern snapshots", testSnapshots },
        { "serialise round trip", testSerialiseRoundTrip },
        { "move trajectories", testMoveTrajectories },
        { "cut trajectory (split eraser)", testCutTrajectory },
        { "time grid snapping", testTimeGrid },
        { "velocity per stroke", testVelocity },
        { "key, scale and pitch magnet", testTuning },
        { "smoothing", testSmoothing },
        { "undo history", testUndoHistory },
        { "preview synth plays drawn pitch (Hz)", testPreviewSynthPlaysDrawnPitch },
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
