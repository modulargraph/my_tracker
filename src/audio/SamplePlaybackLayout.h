#pragma once

#include <algorithm>
#include <vector>

#include "InstrumentParams.h"

namespace SamplePlaybackLayout
{

inline double clampNorm (double v)
{
    return std::clamp (v, 0.0, 1.0);
}

inline double getRegionStartNorm (const InstrumentParams& params)
{
    return clampNorm (params.startPos);
}

inline double getRegionEndNorm (const InstrumentParams& params)
{
    return std::clamp (params.endPos, getRegionStartNorm (params), 1.0);
}

inline double getGranularCenterNorm (const InstrumentParams& params)
{
    const double start = getRegionStartNorm (params);
    const double end = getRegionEndNorm (params);
    return std::clamp (clampNorm (params.granularPosition), start, end);
}

inline std::vector<double> getSliceBoundariesNorm (const InstrumentParams& params)
{
    constexpr double kDuplicateEps = 1.0e-6;

    const double start = getRegionStartNorm (params);
    const double end = getRegionEndNorm (params);

    std::vector<double> boundaries;
    boundaries.reserve (params.slicePoints.size() + 2);
    boundaries.push_back (start);

    for (double slicePos : params.slicePoints)
    {
        const double clampedPos = std::clamp (clampNorm (slicePos), start, end);
        if (clampedPos > boundaries.back() + kDuplicateEps)
            boundaries.push_back (clampedPos);
    }

    if (boundaries.back() < end)
        boundaries.push_back (end);
    else
        boundaries.back() = end;

    // Always expose at least one region [start, end].
    if (boundaries.size() < 2)
        boundaries.push_back (end);

    return boundaries;
}

inline int getSliceRegionCount (const InstrumentParams& params)
{
    const auto boundaries = getSliceBoundariesNorm (params);
    return static_cast<int> (boundaries.size()) - 1;
}

inline int getBeatSliceRegionCount (const InstrumentParams& params, int defaultRegions = 16)
{
    if (params.slicePoints.empty())
        return std::max (1, defaultRegions);

    return static_cast<int> (params.slicePoints.size()) + 1;
}

inline std::vector<double> makeEqualSlicePointsNorm (double startNorm, double endNorm, int regionCount)
{
    const double start = clampNorm (startNorm);
    const double end = std::clamp (endNorm, start, 1.0);

    std::vector<double> points;
    if (regionCount <= 1)
        return points;

    const int numPoints = regionCount - 1;
    points.reserve (static_cast<size_t> (numPoints));

    const double range = end - start;
    if (range <= 0.0)
        return points;

    for (int i = 0; i < numPoints; ++i)
    {
        const double frac = static_cast<double> (i + 1) / static_cast<double> (regionCount);
        points.push_back (start + frac * range);
    }

    return points;
}

} // namespace SamplePlaybackLayout
