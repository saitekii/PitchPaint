#include "Pattern.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>

namespace pitchpaint
{

double Trajectory::pitchAt (double beat) const
{
    if (points.empty())
        return 0.0;

    if (beat <= points.front().beat)
        return points.front().pitch;

    if (beat >= points.back().beat)
        return points.back().pitch;

    const auto next = std::upper_bound (points.begin(), points.end(), beat,
                                        [] (double b, const PitchPoint& p) { return b < p.beat; });
    const auto& b = *next;
    const auto& a = *(next - 1);
    const double span = b.beat - a.beat;

    if (span <= 0.0)
        return b.pitch;

    return a.pitch + (b.pitch - a.pitch) * (beat - a.beat) / span;
}

std::uint32_t Pattern::nextId() const
{
    std::uint32_t id = 0;
    for (const auto& t : trajectories)
        id = std::max (id, t.id);
    return id + 1;
}

void drawSegment (Trajectory& t, PitchPoint from, PitchPoint to)
{
    auto& pts = t.points;
    const double lo = std::min (from.beat, to.beat);
    const double hi = std::max (from.beat, to.beat);

    pts.erase (std::remove_if (pts.begin(), pts.end(),
                               [&] (const PitchPoint& p) { return (p.beat > lo && p.beat < hi) || p.beat == to.beat; }),
               pts.end());

    const auto pos = std::lower_bound (pts.begin(), pts.end(), to.beat,
                                       [] (const PitchPoint& p, double b) { return p.beat < b; });
    pts.insert (pos, to);
}

double snapBeat (double beat, double step)
{
    return step > 0.0 ? std::round (beat / step) * step : beat;
}

double soundingPitch (const Trajectory& t, const Tuning& tuning, double beat, double beatsPerSecond)
{
    double pitch = tuning.apply (t.pitchAt (beat));
    if (t.vibrato.isOn() && ! t.points.empty() && beatsPerSecond > 0.0)
    {
        const double sinceStart = std::max (0.0, beat - t.startBeat());
        double fade = t.vibrato.fadeBeats > 0.0 ? std::min (1.0, sinceStart / t.vibrato.fadeBeats) : 1.0;
        fade = fade * fade * (3.0 - 2.0 * fade); // smooth start
        const double seconds = sinceStart / beatsPerSecond;
        pitch += t.vibrato.depthCents / 100.0 * fade * std::sin (6.283185307179586 * t.vibrato.rateHz * seconds);
    }
    return pitch;
}

Pattern extractStrokes (const Pattern& p, const std::vector<std::uint32_t>& ids)
{
    Pattern out;
    out.lengthBeats = p.lengthBeats;
    out.tuning = p.tuning;
    for (const auto& t : p.trajectories)
        if (std::find (ids.begin(), ids.end(), t.id) != ids.end())
            out.trajectories.push_back (t);
    return out;
}

std::vector<std::uint32_t> insertStrokes (Pattern& p, const std::vector<Trajectory>& strokes, double deltaBeats)
{
    std::vector<std::uint32_t> ids;
    std::uint32_t next = p.nextId();
    for (auto t : strokes)
    {
        if (t.points.empty())
            continue;
        t.id = next++;
        for (auto& pt : t.points)
            pt.beat += deltaBeats;
        p.trajectories.push_back (std::move (t));
        ids.push_back (p.trajectories.back().id);
    }
    moveTrajectories (p, ids, 0.0, 0.0); // pulls the group back inside the loop / MIDI range if it went past
    return ids;
}

void snapEnds (Trajectory& t, double step, double maxBeat)
{
    if (t.points.empty() || step <= 0.0)
        return;

    double start = snapBeat (t.startBeat(), step);
    double end = snapBeat (t.endBeat(), step);
    if (end - start < step * 0.5) // shorter than one step after rounding: make it exactly one
        end = start + step;

    // Keep inside the loop, shifting the pair back if the end would run past it.
    if (end > maxBeat)
    {
        start -= end - std::max (maxBeat, step);
        end = std::max (maxBeat, step);
    }
    start = std::max (0.0, start);

    std::vector<PitchPoint> points { { start, t.pitchAt (start) } }; // pitchAt holds the end pitch outside the curve
    for (const auto& pt : t.points)
        if (pt.beat > start && pt.beat < end)
            points.push_back (pt);
    points.push_back ({ end, t.pitchAt (end) });
    t.points = std::move (points);
}

void smoothTrajectory (Trajectory& t, double sigma)
{
    if (! t.isPlayable() || sigma <= 0.0)
        return;

    const double start = t.startBeat(), end = t.endBeat();
    const double step = std::max (sigma / 4.0, 1.0 / 512.0);
    const int n = std::max (2, (int) std::ceil ((end - start) / step) + 1);
    const double h = (end - start) / (n - 1);

    std::vector<double> raw ((size_t) n);
    for (int i = 0; i < n; ++i)
        raw[(size_t) i] = t.pitchAt (start + i * h);

    // Gaussian over +-3 sigma, renormalised where the window runs past the ends.
    const int radius = (int) std::ceil (3.0 * sigma / h);
    std::vector<PitchPoint> points ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        double sum = 0.0, weight = 0.0;
        for (int k = std::max (0, i - radius); k <= std::min (n - 1, i + radius); ++k)
        {
            const double d = (k - i) * h / sigma;
            const double w = std::exp (-0.5 * d * d);
            sum += w * raw[(size_t) k];
            weight += w;
        }
        points[(size_t) i] = { start + i * h, sum / weight };
    }
    points.front().pitch = raw.front(); // keep where the note starts and lands
    points.back() = { end, raw.back() };
    t.points = std::move (points);
}

void cutTrajectory (Pattern& p, std::uint32_t id, double fromBeat, double toBeat, double minLength)
{
    const auto it = std::find_if (p.trajectories.begin(), p.trajectories.end(), [id] (const Trajectory& t) { return t.id == id; });
    if (it == p.trajectories.end() || it->points.empty())
        return;

    if (fromBeat > toBeat)
        std::swap (fromBeat, toBeat);

    const Trajectory original = *it;
    if (toBeat <= original.startBeat() || fromBeat >= original.endBeat())
        return; // no overlap

    Trajectory left { original.id, {}, original.velocity, original.vibrato }, right { p.nextId(), {}, original.velocity, original.vibrato };

    if (fromBeat > original.startBeat())
    {
        for (const auto& pt : original.points)
            if (pt.beat < fromBeat)
                left.points.push_back (pt);
        left.points.push_back ({ fromBeat, original.pitchAt (fromBeat) });
    }

    if (toBeat < original.endBeat())
    {
        right.points.push_back ({ toBeat, original.pitchAt (toBeat) });
        for (const auto& pt : original.points)
            if (pt.beat > toBeat)
                right.points.push_back (pt);
    }

    const auto keep = [minLength] (const Trajectory& t)
    { return t.points.size() >= 2 && t.endBeat() - t.startBeat() >= minLength; };

    // Replace the original in place (keeps drawing order); the right part goes directly after it.
    const auto index = it - p.trajectories.begin();
    p.trajectories.erase (it);
    auto insertAt = p.trajectories.begin() + index;
    if (keep (right))
        insertAt = p.trajectories.insert (insertAt, std::move (right));
    if (keep (left))
        p.trajectories.insert (insertAt, std::move (left));
}

void moveTrajectories (Pattern& p, const std::vector<std::uint32_t>& ids, double deltaBeats, double deltaPitch)
{
    const auto selected = [&] (const Trajectory& t) { return std::find (ids.begin(), ids.end(), t.id) != ids.end(); };

    double minBeat = std::numeric_limits<double>::max(), maxBeat = std::numeric_limits<double>::lowest();
    double minPitch = minBeat, maxPitch = maxBeat;
    for (const auto& t : p.trajectories)
        if (selected (t))
            for (const auto& pt : t.points)
            {
                minBeat = std::min (minBeat, pt.beat);
                maxBeat = std::max (maxBeat, pt.beat);
                minPitch = std::min (minPitch, pt.pitch);
                maxPitch = std::max (maxPitch, pt.pitch);
            }

    if (minBeat > maxBeat)
        return; // nothing selected

    const auto limit = [] (double d, double lo, double hi) { return std::clamp (d, lo, std::max (lo, hi)); };
    deltaBeats = limit (deltaBeats, -minBeat, p.lengthBeats - maxBeat);
    deltaPitch = limit (deltaPitch, -minPitch, 127.0 - maxPitch);

    for (auto& t : p.trajectories)
        if (selected (t))
            for (auto& pt : t.points)
            {
                pt.beat += deltaBeats;
                pt.pitch += deltaPitch;
            }
}

namespace
{
    // Version 2 added "v <velocity>" after the id, version 3 the "tuning <key> <scale> <magnet>" line, version 4
    // an optional "vib <cents> <hz> <fade>" after the velocity. Older versions still load, with defaults.
    constexpr const char* header = "PitchPaintPattern 4";

    void appendNumber (std::string& s, double v)
    {
        char buf[32];
        const auto r = std::to_chars (buf, buf + sizeof (buf), v); // shortest form that round-trips exactly
        s.append (buf, r.ptr);
    }
}

std::string serialise (const Pattern& p)
{
    std::string s = header;
    s += "\nlength ";
    appendNumber (s, p.lengthBeats);
    s += "\ntuning " + std::to_string (p.tuning.key) + ' ' + std::to_string (p.tuning.scale) + ' ';
    appendNumber (s, p.tuning.magnet);

    for (const auto& t : p.trajectories)
    {
        s += "\nt " + std::to_string (t.id) + " v " + std::to_string (t.velocity);
        if (t.vibrato.isOn())
        {
            s += " vib ";
            appendNumber (s, t.vibrato.depthCents);
            s += ' ';
            appendNumber (s, t.vibrato.rateHz);
            s += ' ';
            appendNumber (s, t.vibrato.fadeBeats);
        }
        for (const auto& pt : t.points)
        {
            s += ' ';
            appendNumber (s, pt.beat);
            s += ' ';
            appendNumber (s, pt.pitch);
        }
    }
    return s + "\n";
}

std::optional<Pattern> parsePattern (const std::string& text)
{
    const char* pos = text.data();
    const char* const end = pos + text.size();

    const auto skipSpace = [&] { while (pos < end && (*pos == ' ' || *pos == '\n' || *pos == '\r' || *pos == '\t')) ++pos; };
    const auto word = [&]
    {
        skipSpace();
        const char* start = pos;
        while (pos < end && ! (*pos == ' ' || *pos == '\n' || *pos == '\r' || *pos == '\t')) ++pos;
        return std::string (start, pos);
    };
    const auto number = [&] (double& out)
    {
        skipSpace();
        const auto r = std::from_chars (pos, end, out);
        if (r.ec != std::errc() || ! std::isfinite (out))
            return false;
        pos = r.ptr;
        return true;
    };
    const auto atLineEnd = [&]
    {
        while (pos < end && (*pos == ' ' || *pos == '\t' || *pos == '\r')) ++pos;
        return pos >= end || *pos == '\n';
    };

    const auto magic = word();
    const auto version = word();
    if (magic != "PitchPaintPattern" || (version != "1" && version != "2" && version != "3" && version != "4") || word() != "length")
        return std::nullopt;

    Pattern p;
    if (! number (p.lengthBeats) || p.lengthBeats <= 0.0)
        return std::nullopt;

    if (const char* lineStart = pos; word() == "tuning")
    {
        double key = 0.0, scale = 0.0, magnet = 0.0;
        if (! number (key) || ! number (scale) || ! number (magnet)
            || key < 0.0 || key > 11.0 || scale < 0.0 || scale >= (double) scales.size() || magnet < 0.0 || magnet > 1.0)
            return std::nullopt;
        p.tuning = { (int) key, (int) scale, magnet };
    }
    else
    {
        pos = lineStart; // not a tuning line: the trajectories start here
    }

    for (skipSpace(); pos < end; skipSpace())
    {
        double id = 0.0;
        if (word() != "t" || ! number (id) || id < 1.0 || id > 4294967295.0)
            return std::nullopt;

        Trajectory t { (std::uint32_t) id, {} };
        skipSpace();
        if (pos < end && *pos == 'v')
        {
            double velocity = 0.0;
            if (word() != "v" || ! number (velocity) || velocity < 1.0 || velocity > 127.0)
                return std::nullopt;
            t.velocity = (int) velocity;
        }
        if (const char* save = pos; word() == "vib")
        {
            Vibrato v;
            if (! number (v.depthCents) || ! number (v.rateHz) || ! number (v.fadeBeats) || v.depthCents < 0.0
                || v.depthCents > 1200.0 || v.rateHz <= 0.0 || v.rateHz > 50.0 || v.fadeBeats < 0.0)
                return std::nullopt;
            t.vibrato = v;
        }
        else
        {
            pos = save;
        }
        while (! atLineEnd())
        {
            PitchPoint pt {};
            if (! number (pt.beat) || ! number (pt.pitch))
                return std::nullopt;
            t.points.push_back (pt);
        }

        std::stable_sort (t.points.begin(), t.points.end(), [] (const PitchPoint& a, const PitchPoint& b) { return a.beat < b.beat; });
        p.trajectories.push_back (std::move (t));
    }

    return p;
}

} // namespace pitchpaint
