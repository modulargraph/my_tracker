#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>
#include <juce_core/juce_core.h>

//==============================================================================
// Automation curve interpolation type
//==============================================================================

enum class AutomationCurveType
{
    Linear = 0,   // Linear interpolation between points
    Step   = 1,   // Step/hold: value jumps at the point
    Smooth = 2,   // Catmull-Rom spline (smooth curves through points)
    SCurve = 3    // Smoothstep (ease-in/ease-out S-curve)
};

//==============================================================================
// A single automation point (row-position, normalised value, curve type)
//==============================================================================

struct AutomationPoint
{
    int row = 0;            // Row index in the pattern (0-based)
    float value = 0.0f;     // Normalised parameter value (0.0 - 1.0)
    AutomationCurveType curveType = AutomationCurveType::Linear;

    bool operator< (const AutomationPoint& other) const { return row < other.row; }
    bool operator== (const AutomationPoint& other) const
    {
        return row == other.row
            && std::abs (value - other.value) < 1.0e-6f
            && curveType == other.curveType;
    }
};

//==============================================================================
// Helpers for building and destructively transforming stored automation points.
//==============================================================================

namespace AutomationCurveTools
{
enum class StandardCurve
{
    Sine = 0,
    Triangle,
    RampUp,
    Pulse,
    QuarterGate,
    OffbeatGate,
    StairUp,
    Accent,
    EaseIn,
    EaseOut,
    Dip,
    Swell
};

enum class StandardStamp
{
    UpOneBar = 0,
    UpTwoBars,
    DownOneBar,
    DownTwoBars
};

inline float wrapPhase (float phase)
{
    return phase - std::floor (phase);
}

inline float smooth01 (float x)
{
    x = juce::jlimit (0.0f, 1.0f, x);
    return x * x * (3.0f - 2.0f * x);
}

inline float evaluateStandardCurve (StandardCurve curve, float phase, int row, int patternLength)
{
    auto x = juce::jlimit (0.0f, 1.0f, phase);
    constexpr float twoPi = 6.28318530717958647692f;

    switch (curve)
    {
        case StandardCurve::Sine:
            return juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * std::sin (twoPi * x));

        case StandardCurve::Triangle:
        {
            auto p = wrapPhase (x);
            return p < 0.5f ? p * 2.0f : (1.0f - p) * 2.0f;
        }

        case StandardCurve::RampUp:
            return x;

        case StandardCurve::Pulse:
            return x < 0.5f ? 1.0f : 0.0f;

        case StandardCurve::QuarterGate:
        {
            auto p = wrapPhase (x * 8.0f);
            return p < 0.48f ? 1.0f : 0.0f;
        }

        case StandardCurve::OffbeatGate:
        {
            auto p = wrapPhase (x * 8.0f);
            return (p >= 0.45f && p < 0.9f) ? 1.0f : 0.0f;
        }

        case StandardCurve::StairUp:
        {
            constexpr int steps = 8;
            auto step = juce::jlimit (0, steps - 1, static_cast<int> (std::floor (x * static_cast<float> (steps))));
            return static_cast<float> (step) / static_cast<float> (steps - 1);
        }

        case StandardCurve::Accent:
        {
            static constexpr float pattern[] = { 1.0f, 0.22f, 0.58f, 0.22f, 0.82f, 0.22f, 0.44f, 0.22f };
            auto safeLength = juce::jmax (1, patternLength);
            auto scaled = static_cast<float> (row) / static_cast<float> (safeLength) * 8.0f;
            auto idx = juce::jlimit (0, 7, static_cast<int> (std::floor (scaled)));
            return pattern[static_cast<size_t> (idx)];
        }

        case StandardCurve::EaseIn:
            return x * x;

        case StandardCurve::EaseOut:
        {
            auto inv = 1.0f - x;
            return 1.0f - inv * inv;
        }

        case StandardCurve::Dip:
            return std::abs (2.0f * x - 1.0f);

        case StandardCurve::Swell:
            return juce::jlimit (0.0f, 1.0f, std::sin (3.14159265358979323846f * x));
    }

    return x;
}

inline bool isSteppedStandardCurve (StandardCurve curve)
{
    return curve == StandardCurve::Pulse
        || curve == StandardCurve::QuarterGate
        || curve == StandardCurve::OffbeatGate
        || curve == StandardCurve::StairUp
        || curve == StandardCurve::Accent;
}

inline bool isRisingStamp (StandardStamp stamp)
{
    return stamp == StandardStamp::UpOneBar
        || stamp == StandardStamp::UpTwoBars;
}

inline int getStampBars (StandardStamp stamp)
{
    return stamp == StandardStamp::UpTwoBars || stamp == StandardStamp::DownTwoBars ? 2 : 1;
}

inline int getStampLengthRows (StandardStamp stamp, int rowsPerBeat)
{
    return juce::jmax (1, rowsPerBeat) * 4 * getStampBars (stamp);
}

inline std::vector<AutomationPoint> makeStandardCurvePoints (StandardCurve curve, int patternLength)
{
    auto rows = juce::jmax (1, patternLength);
    std::vector<AutomationPoint> result;
    result.reserve (static_cast<size_t> (rows));

    auto curveType = isSteppedStandardCurve (curve) ? AutomationCurveType::Step
                                                    : AutomationCurveType::Linear;

    for (int row = 0; row < rows; ++row)
    {
        auto phase = rows <= 1 ? 0.0f
                               : static_cast<float> (row) / static_cast<float> (rows - 1);
        result.push_back ({ row,
                            juce::jlimit (0.0f, 1.0f, evaluateStandardCurve (curve, phase, row, rows)),
                            curveType });
    }

    return result;
}

inline void sortAndCoalescePoints (std::vector<AutomationPoint>& points)
{
    std::stable_sort (points.begin(), points.end());

    std::vector<AutomationPoint> coalesced;
    coalesced.reserve (points.size());

    for (auto point : points)
    {
        if (! coalesced.empty() && coalesced.back().row == point.row)
            coalesced.back() = point;
        else
            coalesced.push_back (point);
    }

    points = std::move (coalesced);
}

inline std::vector<AutomationPoint> makeStampPoints (StandardStamp stamp,
                                                     int startRow,
                                                     int rowsPerBeat,
                                                     int patternLength,
                                                     float startValue,
                                                     float endValue)
{
    auto rows = juce::jmax (1, patternLength);
    auto firstRow = juce::jlimit (0, rows - 1, startRow);
    auto lengthRows = getStampLengthRows (stamp, rowsPerBeat);
    auto lastRow = juce::jlimit (firstRow, rows - 1, firstRow + lengthRows - 1);
    auto targetValue = isRisingStamp (stamp) ? 1.0f : 0.0f;
    auto start = juce::jlimit (0.0f, 1.0f, startValue);
    auto end = juce::jlimit (0.0f, 1.0f, endValue);

    std::vector<AutomationPoint> result;
    result.reserve (static_cast<size_t> (lastRow - firstRow + 1));

    for (int row = firstRow; row <= lastRow; ++row)
    {
        auto phase = lastRow == firstRow ? 1.0f
                                         : static_cast<float> (row - firstRow) / static_cast<float> (lastRow - firstRow);
        constexpr float attackEnd = 0.14f;
        float value = start;

        if (phase <= attackEnd)
        {
            auto t = smooth01 (phase / attackEnd);
            value = start + (targetValue - start) * t;
        }
        else
        {
            auto t = (phase - attackEnd) / (1.0f - attackEnd);
            auto decay = std::pow (1.0f - juce::jlimit (0.0f, 1.0f, t), 2.2f);
            value = end + (targetValue - end) * decay;
        }

        result.push_back ({ row, juce::jlimit (0.0f, 1.0f, value), AutomationCurveType::Linear });
    }

    return result;
}

inline std::vector<AutomationPoint> replacePointsInRange (const std::vector<AutomationPoint>& source,
                                                          const std::vector<AutomationPoint>& replacement,
                                                          int startRow,
                                                          int endRow)
{
    std::vector<AutomationPoint> result;
    result.reserve (source.size() + replacement.size());

    auto first = juce::jmin (startRow, endRow);
    auto last = juce::jmax (startRow, endRow);

    for (auto point : source)
        if (point.row < first || point.row > last)
            result.push_back (point);

    result.insert (result.end(), replacement.begin(), replacement.end());
    sortAndCoalescePoints (result);
    return result;
}

inline std::vector<AutomationPoint> transformPoints (const std::vector<AutomationPoint>& source,
                                                     const std::vector<int>& targetIndices,
                                                     float valueMagnitude,
                                                     float rowStretch,
                                                     float valueCentre,
                                                     int patternLength,
                                                     float valueOffset = 0.0f)
{
    if (source.empty())
        return {};

    auto rows = juce::jmax (1, patternLength);
    auto points = source;
    std::vector<bool> targeted (source.size(), targetIndices.empty());

    for (auto idx : targetIndices)
        if (idx >= 0 && idx < static_cast<int> (targeted.size()))
            targeted[static_cast<size_t> (idx)] = true;

    int minRow = std::numeric_limits<int>::max();
    int maxRow = std::numeric_limits<int>::min();
    bool hasTarget = false;

    for (int i = 0; i < static_cast<int> (source.size()); ++i)
    {
        if (! targeted[static_cast<size_t> (i)])
            continue;

        minRow = juce::jmin (minRow, source[static_cast<size_t> (i)].row);
        maxRow = juce::jmax (maxRow, source[static_cast<size_t> (i)].row);
        hasTarget = true;
    }

    if (! hasTarget)
        return points;

    auto centreRow = (static_cast<float> (minRow) + static_cast<float> (maxRow)) * 0.5f;
    auto centreValue = juce::jlimit (0.0f, 1.0f, valueCentre);
    auto magnitude = juce::jmax (0.0f, valueMagnitude);
    auto stretch = juce::jmax (0.0f, rowStretch);

    for (int i = 0; i < static_cast<int> (points.size()); ++i)
    {
        if (! targeted[static_cast<size_t> (i)])
            continue;

        auto& point = points[static_cast<size_t> (i)];
        auto stretchedRow = centreRow + (static_cast<float> (source[static_cast<size_t> (i)].row) - centreRow) * stretch;
        auto scaledValue = centreValue + (source[static_cast<size_t> (i)].value - centreValue) * magnitude
                         + valueOffset;

        point.row = juce::jlimit (0, rows - 1, juce::roundToInt (stretchedRow));
        point.value = juce::jlimit (0.0f, 1.0f, scaledValue);
    }

    sortAndCoalescePoints (points);
    return points;
}
} // namespace AutomationCurveTools

//==============================================================================
// An automation lane: targets one parameter of one plugin on one track
//==============================================================================

struct AutomationLane
{
    juce::String pluginId;      // Plugin instance identifier (pluginDescription.createIdentifierString()
                                 // or instrument slot index as string for instrument plugins)
    int parameterId = -1;        // Parameter index within the plugin
    int owningTrack = -1;        // Track that owns this automation lane
    std::vector<AutomationPoint> points;

    bool isEmpty() const { return points.empty(); }

    /** Sort points by row. */
    void sortPoints()
    {
        std::sort (points.begin(), points.end());
    }

    /** Get the interpolated value at a given fractional row position.
     *  Returns defaultValue if no points exist. */
    float getValueAtRow (float rowPosition, float defaultValue = 0.5f) const
    {
        if (points.empty())
            return defaultValue;

        // Before first point: hold at first point value
        if (rowPosition <= static_cast<float> (points.front().row))
            return points.front().value;

        // After last point: hold at last point value
        if (rowPosition >= static_cast<float> (points.back().row))
            return points.back().value;

        // Find surrounding points
        for (size_t i = 0; i + 1 < points.size(); ++i)
        {
            const auto& a = points[i];
            const auto& b = points[i + 1];

            if (rowPosition >= static_cast<float> (a.row) && rowPosition <= static_cast<float> (b.row))
            {
                // At the exact position of the next point, return that point's value
                // (step transitions happen at the point itself, and linear interpolation
                // converges to b.value at t=1.0)
                if (rowPosition >= static_cast<float> (b.row))
                    return b.value;

                if (a.curveType == AutomationCurveType::Step)
                    return a.value;

                float range = static_cast<float> (b.row - a.row);
                if (range <= 0.0f)
                    return a.value;

                float t = (rowPosition - static_cast<float> (a.row)) / range;

                if (a.curveType == AutomationCurveType::SCurve)
                {
                    // Smoothstep: ease-in/ease-out
                    t = t * t * (3.0f - 2.0f * t);
                    return a.value + t * (b.value - a.value);
                }

                if (a.curveType == AutomationCurveType::Smooth)
                {
                    // Catmull-Rom spline interpolation
                    float p0 = (i > 0) ? points[i - 1].value : a.value;
                    float p1 = a.value;
                    float p2 = b.value;
                    float p3 = (i + 2 < points.size()) ? points[i + 2].value : b.value;

                    float t2 = t * t;
                    float t3 = t2 * t;
                    float result = 0.5f * ((2.0f * p1)
                                         + (-p0 + p2) * t
                                         + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
                                         + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
                    return juce::jlimit (0.0f, 1.0f, result);
                }

                // Linear interpolation
                return a.value + t * (b.value - a.value);
            }
        }

        return defaultValue;
    }

    /** Add or update a point at the given row. If a point already exists at
     *  that row, its value is updated. */
    void setPoint (int row, float value, AutomationCurveType curve = AutomationCurveType::Linear)
    {
        value = juce::jlimit (0.0f, 1.0f, value);

        for (auto& p : points)
        {
            if (p.row == row)
            {
                p.value = value;
                p.curveType = curve;
                return;
            }
        }

        points.push_back ({ row, value, curve });
        sortPoints();
    }

    /** Remove a point at the given row. Returns true if a point was removed. */
    bool removePoint (int row)
    {
        for (auto it = points.begin(); it != points.end(); ++it)
        {
            if (it->row == row)
            {
                points.erase (it);
                return true;
            }
        }
        return false;
    }

    /** Remove the point closest to the given row within a tolerance. */
    bool removePointNear (int row, int tolerance = 1)
    {
        int bestIdx = -1;
        int bestDist = tolerance + 1;

        for (int i = 0; i < static_cast<int> (points.size()); ++i)
        {
            int dist = std::abs (points[static_cast<size_t> (i)].row - row);
            if (dist < bestDist)
            {
                bestDist = dist;
                bestIdx = i;
            }
        }

        if (bestIdx >= 0)
        {
            points.erase (points.begin() + bestIdx);
            return true;
        }

        return false;
    }

    bool operator== (const AutomationLane& other) const
    {
        return pluginId == other.pluginId
            && parameterId == other.parameterId
            && owningTrack == other.owningTrack
            && points == other.points;
    }
};

//==============================================================================
// Per-pattern automation data: collection of automation lanes
//==============================================================================

struct PatternAutomationData
{
    std::vector<AutomationLane> lanes;

    bool isEmpty() const { return lanes.empty(); }

    /** Find a lane targeting a specific plugin parameter, or return nullptr. */
    AutomationLane* findLane (const juce::String& pluginId, int parameterId)
    {
        for (auto& lane : lanes)
            if (lane.pluginId == pluginId && lane.parameterId == parameterId)
                return &lane;
        return nullptr;
    }

    const AutomationLane* findLane (const juce::String& pluginId, int parameterId) const
    {
        for (auto& lane : lanes)
            if (lane.pluginId == pluginId && lane.parameterId == parameterId)
                return &lane;
        return nullptr;
    }

    /** Get or create a lane for the given plugin parameter. */
    AutomationLane& getOrCreateLane (const juce::String& pluginId, int parameterId, int owningTrack)
    {
        auto* existing = findLane (pluginId, parameterId);
        if (existing != nullptr)
            return *existing;

        lanes.push_back ({ pluginId, parameterId, owningTrack, {} });
        return lanes.back();
    }

    /** Find all lanes for a given plugin (for multi-lane overlay). */
    std::vector<AutomationLane*> findLanesForPlugin (const juce::String& pluginId)
    {
        std::vector<AutomationLane*> result;
        for (auto& lane : lanes)
            if (lane.pluginId == pluginId)
                result.push_back (&lane);
        return result;
    }

    std::vector<const AutomationLane*> findLanesForPlugin (const juce::String& pluginId) const
    {
        std::vector<const AutomationLane*> result;
        for (auto& lane : lanes)
            if (lane.pluginId == pluginId)
                result.push_back (&lane);
        return result;
    }

    /** Remove a lane for the given plugin parameter. Returns true if removed. */
    bool removeLane (const juce::String& pluginId, int parameterId)
    {
        for (auto it = lanes.begin(); it != lanes.end(); ++it)
        {
            if (it->pluginId == pluginId && it->parameterId == parameterId)
            {
                lanes.erase (it);
                return true;
            }
        }
        return false;
    }

    /** Remove all lanes targeting a specific plugin ID. */
    void removeAllLanesForPlugin (const juce::String& pluginId)
    {
        lanes.erase (std::remove_if (lanes.begin(), lanes.end(),
                                      [&pluginId] (const AutomationLane& lane)
                                      { return lane.pluginId == pluginId; }),
                     lanes.end());
    }

    /** Remove all lanes for a given owning track. */
    void removeAllLanesForTrack (int trackIndex)
    {
        lanes.erase (std::remove_if (lanes.begin(), lanes.end(),
                                      [trackIndex] (const AutomationLane& lane)
                                      { return lane.owningTrack == trackIndex; }),
                     lanes.end());
    }

    /** Remove automation for a deleted insert slot and shift later positional insert IDs down. */
    bool remapInsertLanesAfterSlotRemoved (int trackIndex, int removedSlotIndex)
    {
        if (trackIndex < 0 || removedSlotIndex < 0)
            return false;

        bool changed = false;
        const auto prefix = "insert:" + juce::String (trackIndex) + ":";

        lanes.erase (std::remove_if (lanes.begin(), lanes.end(),
                                      [&] (AutomationLane& lane)
                                      {
                                          if (! lane.pluginId.startsWith (prefix))
                                              return false;

                                          int slotIndex = lane.pluginId.substring (prefix.length()).getIntValue();
                                          if (slotIndex == removedSlotIndex)
                                          {
                                              changed = true;
                                              return true;
                                          }

                                          if (slotIndex > removedSlotIndex)
                                          {
                                              lane.pluginId = prefix + juce::String (slotIndex - 1);
                                              changed = true;
                                          }

                                          return false;
                                      }),
                     lanes.end());

        return changed;
    }

    /** Remove all empty lanes (lanes with no points). */
    void removeEmptyLanes()
    {
        lanes.erase (std::remove_if (lanes.begin(), lanes.end(),
                                      [] (const AutomationLane& lane)
                                      { return lane.isEmpty(); }),
                     lanes.end());
    }

    /** Deep copy (used for pattern duplication). */
    PatternAutomationData clone() const
    {
        PatternAutomationData copy;
        copy.lanes = lanes; // vector deep copy
        return copy;
    }

    bool operator== (const PatternAutomationData& other) const
    {
        return lanes == other.lanes;
    }
};
