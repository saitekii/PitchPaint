#pragma once

#include <cstdint>
#include <vector>

namespace pitchpaint
{

// Pitch is a continuous MIDI note number (60.0 = middle C, 60.5 = a quarter-tone above).
struct PitchPoint
{
    double beat;
    double pitch;
};

// One drawn stroke: a pitch curve over time that sounds as a single note.
// Points are kept sorted by beat; at least two points are needed for it to sound.
struct Trajectory
{
    std::uint32_t id = 0;
    std::vector<PitchPoint> points;

    bool isPlayable() const { return points.size() >= 2 && endBeat() > startBeat(); }
    double startBeat() const { return points.front().beat; }
    double endBeat() const { return points.back().beat; }

    // Linear interpolation, clamped to the end points outside the curve.
    double pitchAt (double beat) const;
};

// A loop of trajectories. Trajectories must lie within [0, lengthBeats].
struct Pattern
{
    double lengthBeats = 16.0;
    std::vector<Trajectory> trajectories;
};

} // namespace pitchpaint
