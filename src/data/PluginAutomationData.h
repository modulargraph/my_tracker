#pragma once

#include <JuceHeader.h>
#include <map>
#include <vector>

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
