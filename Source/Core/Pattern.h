#pragma once

#include "Tuning.h"

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

// Periodic pitch modulation added on top of a stroke when it plays (after the pitch magnet, so the magnet can't
// flatten it). Stored as settings, not points, so it can be changed or removed later.
struct Vibrato
{
    double depthCents = 0.0; // peak deviation; 0 = off
    double rateHz = 5.5;
    double fadeBeats = 0.5;  // ramps in from the note start over this many beats

    bool isOn() const { return depthCents > 0.0; }
    bool operator== (const Vibrato&) const = default;
};

// A value from 0 to 1 over time, for a stroke's dynamics (loudness). Beats are relative to the stroke's start, so the
// envelope moves, pastes and duplicates with its stroke. No points = constant full level.
struct EnvelopePoint
{
    double beat;
    double value;

    bool operator== (const EnvelopePoint&) const = default;
};

struct Envelope
{
    std::vector<EnvelopePoint> points; // sorted by beat

    bool empty() const { return points.empty(); }
    double valueAt (double beat) const; // linear, held flat beyond the ends; 1 when empty
    void shift (double deltaBeats);     // move every point; drops nothing
    bool operator== (const Envelope&) const = default;
};

// Same overwrite-between-the-two-beats behaviour as drawSegment, for envelopes (drawing in the dynamics lane).
void drawEnvelopeSegment (Envelope&, EnvelopePoint from, EnvelopePoint to);

enum class DynamicsShape { swell, fade, hairpin, accent };
Envelope makeDynamicsShape (DynamicsShape, double lengthBeats);

// One drawn stroke: a pitch curve over time that sounds as a single note.
// Points are kept sorted by beat; at least two points are needed for it to sound.
struct Trajectory
{
    static constexpr int defaultVelocity = 100;

    std::uint32_t id = 0;
    std::vector<PitchPoint> points;
    int velocity = defaultVelocity; // MIDI note-on velocity, 1..127
    Vibrato vibrato;
    Envelope dynamics; // loudness over time (relative beats); empty = full

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
    Tuning tuning; // key, scale and pitch magnet applied when playing

    std::uint32_t nextId() const;
    bool operator== (const Pattern&) const = default;
};

// The pitch a stroke sounds at: drawn curve, then the pitch magnet, then vibrato (which needs the tempo for Hz).
double soundingPitch (const Trajectory&, const Tuning&, double beat, double beatsPerSecond);

// A stroke's dynamics level (0..1) at an absolute beat.
double dynamicsAt (const Trajectory&, double beat);

// --- Clipboard ------------------------------------------------------------------------------------------------------
// The given strokes as a pattern of their own (same loop length and tuning), e.g. to serialise to the clipboard.
Pattern extractStrokes (const Pattern&, const std::vector<std::uint32_t>& ids);

// Adds copies of `strokes` shifted by deltaBeats (moved back inside the loop as a group if needed), with new ids.
// Returns the new ids.
std::vector<std::uint32_t> insertStrokes (Pattern&, const std::vector<Trajectory>& strokes, double deltaBeats);

// --- Time grid (pitch is never snapped here) -------------------------------------------------------------------------
// Nearest multiple of `step` beats.
double snapBeat (double beat, double step);

// Moves a trajectory's start and end to the nearest grid lines so its length is a whole number of steps (at least one),
// staying within [0, maxBeat]. The curve in between is kept; where an end moves outward, the end pitch is held.
void snapEnds (Trajectory&, double step, double maxBeat);

// Smooths a trajectory's pitch with a Gaussian of the given width (in beats). Its start, end and their pitches are kept.
// The curve is resampled evenly, so repeated smoothing keeps working on hand-drawn and sparse strokes alike.
void smoothTrajectory (Trajectory&, double sigmaBeats);

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
