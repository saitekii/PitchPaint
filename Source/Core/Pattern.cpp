#include "Pattern.h"

#include <algorithm>

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

} // namespace pitchpaint
