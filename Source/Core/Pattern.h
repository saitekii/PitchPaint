#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pitchpaint
{

// Pitch is a continuous MIDI note number (60.0 = middle C, 60.5 = a quarter-tone above).
struct PitchPoint
{
    double beat;
    double pitch;

    bool operator== (const PitchPoint&) const = default;
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

    bool operator== (const Trajectory&) const = default;
};

// A loop of trajectories. Parts of trajectories beyond lengthBeats don't play.
struct Pattern
{
    double lengthBeats = 16.0;
    std::vector<Trajectory> trajectories;

    std::uint32_t nextId() const;
    bool operator== (const Pattern&) const = default;
};

// --- Time grid (pitch is never snapped here) -------------------------------------------------------------------------
// Nearest multiple of `step` beats.
double snapBeat (double beat, double step);

// Moves a trajectory's start and end to the nearest grid lines so its length is a whole number of steps (at least one),
// staying within [0, maxBeat]. The curve in between is kept; where an end moves outward, the end pitch is held.
void snapEnds (Trajectory&, double step, double maxBeat);

// Removes the time range [fromBeat, toBeat] from a trajectory (split eraser). The parts on either side remain, with
// end points interpolated exactly at the cut: the left part keeps the id, the right part gets a new one.
// Parts shorter than minLength beats are dropped, so erasing never leaves inaudible slivers.
void cutTrajectory (Pattern&, std::uint32_t id, double fromBeat, double toBeat, double minLength = 1.0 / 32.0);

// Shifts the given trajectories in time and pitch. The shift is limited so they stay inside the loop and 0..127.
void moveTrajectories (Pattern&, const std::vector<std::uint32_t>& ids, double deltaBeats, double deltaPitch);

// Lossless plain-text form, used for plugin state. Parsing returns nothing for malformed text.
std::string serialise (const Pattern&);
std::optional<Pattern> parsePattern (const std::string&);

// Pencil stroke step from `from` to `to`: whatever the trajectory had strictly between the two beats is replaced
// by the straight segment, so drawing back over a stroke redraws it, and drawing left of its start extends it.
// `from` should already be a point of the trajectory (the previous mouse position).
void drawSegment (Trajectory&, PitchPoint from, PitchPoint to);

} // namespace pitchpaint
