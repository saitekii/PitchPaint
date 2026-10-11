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

} // namespace pitchpaint
