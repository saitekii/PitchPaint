#include "Tuning.h"

#include <algorithm>
#include <cmath>

namespace pitchpaint
{

namespace
{
    std::uint16_t maskOf (int scale)
    {
        return scales[(size_t) std::clamp (scale, 0, (int) scales.size() - 1)].mask;
    }
}

bool Tuning::inScale (int note) const
{
    const int degree = ((note - key) % 12 + 12) % 12;
    return (maskOf (scale) >> degree) & 1;
}

double Tuning::nearestScaleNote (double pitch) const
{
    const int centre = (int) std::lround (pitch);
    for (int distance = 0; distance <= 12; ++distance) // every scale has a note within a tritone; search both ways
    {
        const int below = centre - distance, above = centre + distance;
        const bool belowIn = inScale (below), aboveIn = inScale (above);
        if (belowIn && aboveIn)
            return std::abs (pitch - below) <= std::abs (pitch - above) ? below : above;
        if (belowIn)
            return below;
        if (aboveIn)
            return above;
    }
    return pitch;
}

double Tuning::apply (double pitch) const
{
    if (magnet <= 0.0)
        return pitch;

    // The scale notes a <= pitch < b around this pitch.
    int a = (int) std::floor (pitch);
    while (! inScale (a))
        --a;
    int b = a + 1;
    while (! inScale (b))
        ++b;

    // u in [0, 1) between them; warp it so values near either note move closer to it. gamma = 1 is no change;
    // larger gamma flattens the curve around the notes, keeping both ends and the midpoint fixed (continuity).
    const double u = (pitch - a) / (b - a);
    const double gamma = 1.0 + 6.0 * std::clamp (magnet, 0.0, 1.0);
    const double warped = u < 0.5 ? 0.5 * std::pow (2.0 * u, gamma) : 1.0 - 0.5 * std::pow (2.0 * (1.0 - u), gamma);
    return a + (b - a) * warped;
}

} // namespace pitchpaint
