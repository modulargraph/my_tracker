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

} // namespace SamplePlaybackLayout
