#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "InstrumentParams.h"

namespace SamplePlaybackLayout
{

inline double clampNorm (double v)
{
    return std::clamp (v, 0.0, 1.0);
}

inline double clampGranularLengthSteps (double steps)
{
    return std::clamp (steps,
                       InstrumentParams::kMinGranularLengthSteps,
                       InstrumentParams::kMaxGranularLengthSteps);
}

inline double snapGranularLengthSteps (double steps)
{
    return clampGranularLengthSteps (std::round (steps * 2.0) * 0.5);
}

inline double getGranularLengthFrequencyHz (const InstrumentParams& params,
                                            int midiNote,
                                            double pitchOffsetSemitones)
{
    constexpr double middleCHz = 261.6255653005986;
    const double semitones = static_cast<double> (midiNote - 60)
                           + static_cast<double> (params.tune)
                           + static_cast<double> (params.finetune) / 100.0
                           + pitchOffsetSemitones;
    return middleCHz * std::pow (2.0, semitones / 12.0);
}

inline double getGranularRenderLengthSamples (const InstrumentParams& params,
                                              int midiNote,
                                              double outputSampleRate,
                                              double pitchOffsetSemitones)
{
    const double safeSampleRate = std::max (1.0, outputSampleRate);

    if (params.granularLengthMode == InstrumentParams::GranLengthMode::Steps)
    {
        const double frequency = std::max (1.0, getGranularLengthFrequencyHz (params, midiNote,
                                                                               pitchOffsetSemitones));
        return clampGranularLengthSteps (params.granularLengthSteps) * safeSampleRate / frequency;
    }

    return static_cast<double> (std::max (1, params.granularLength)) * 0.001 * safeSampleRate;
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

inline double getGranularCenterNorm (const InstrumentParams& params, double positionOffsetByRegion)
{
    const double start = getRegionStartNorm (params);
    const double end = getRegionEndNorm (params);
    const double regionLen = end - start;
    return std::clamp (getGranularCenterNorm (params) + positionOffsetByRegion * regionLen,
                       start, end);
}

inline double getLoopStartNorm (const InstrumentParams& params)
{
    const double start = getRegionStartNorm (params);
    const double end = getRegionEndNorm (params);
    return std::clamp (clampNorm (params.loopStart), start, end);
}

inline double getLoopEndNorm (const InstrumentParams& params)
{
    const double loopStart = getLoopStartNorm (params);
    const double end = getRegionEndNorm (params);
    return std::clamp (clampNorm (params.loopEnd), loopStart, end);
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

inline int clampSliceRegionIndex (const InstrumentParams& params, int index)
{
    const int regionCount = getSliceRegionCount (params);
    return std::clamp (index, 0, std::max (0, regionCount - 1));
}

inline int getBeatSliceRegionCount (const InstrumentParams& params, int defaultRegions = 8)
{
    if (params.slicePoints.empty())
        return std::max (1, defaultRegions);

    return getSliceRegionCount (params);
}

inline std::vector<double> makeEqualSliceBoundariesNorm (double startNorm, double endNorm, int regionCount)
{
    const double start = clampNorm (startNorm);
    const double end = std::clamp (endNorm, start, 1.0);
    const int count = std::max (1, regionCount);

    std::vector<double> boundaries;
    boundaries.reserve (static_cast<size_t> (count + 1));

    const double range = end - start;
    if (range <= 0.0)
    {
        boundaries.push_back (start);
        boundaries.push_back (end);
        return boundaries;
    }

    for (int i = 0; i <= count; ++i)
    {
        const double frac = static_cast<double> (i) / static_cast<double> (count);
        boundaries.push_back (start + frac * range);
    }

    return boundaries;
}

inline std::vector<double> getBeatSliceBoundariesNorm (const InstrumentParams& params, int defaultRegions = 8)
{
    if (! params.slicePoints.empty())
        return getSliceBoundariesNorm (params);

    return makeEqualSliceBoundariesNorm (getRegionStartNorm (params),
                                         getRegionEndNorm (params),
                                         defaultRegions);
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
