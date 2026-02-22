#pragma once

#include "PluginAutomationData.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

namespace PluginAutomationInternal
{
class AutomationPointEditor
{
public:
    using SelectedPointSet = std::set<int>;

    static bool erasePoint (AutomationLane& lane, int pointIndex, SelectedPointSet& selectedPoints)
    {
        if (! isValidIndex (lane, pointIndex))
            return false;

        lane.points.erase (lane.points.begin() + pointIndex);
        selectedPoints.erase (pointIndex);
        return true;
    }

    static bool eraseSelectedPoints (AutomationLane& lane, SelectedPointSet& selectedPoints)
    {
        if (selectedPoints.empty())
            return false;

        std::vector<int> sorted (selectedPoints.begin(), selectedPoints.end());
        std::sort (sorted.rbegin(), sorted.rend());

        bool erased = false;
        for (int pointIndex : sorted)
        {
            if (! isValidIndex (lane, pointIndex))
                continue;
            lane.points.erase (lane.points.begin() + pointIndex);
            erased = true;
        }

        selectedPoints.clear();
        return erased;
    }

    static void applySelectionDelta (AutomationLane& lane,
                                     const SelectedPointSet& selectedPoints,
                                     int rowDelta,
                                     float valueDelta,
                                     int patternLength)
    {
        for (int pointIndex : selectedPoints)
        {
            if (! isValidIndex (lane, pointIndex))
                continue;

            auto& point = lane.points[static_cast<size_t> (pointIndex)];
            point.row = juce::jlimit (0, patternLength - 1, point.row + rowDelta);
            point.value = juce::jlimit (0.0f, 1.0f, point.value + valueDelta);
        }
    }

    static int findPointByRowAndValue (const AutomationLane& lane, int row, float value)
    {
        for (int i = 0; i < static_cast<int> (lane.points.size()); ++i)
        {
            auto& point = lane.points[static_cast<size_t> (i)];
            if (point.row == row && std::abs (point.value - value) < 1.0e-6f)
                return i;
        }
        return -1;
    }

private:
    static bool isValidIndex (const AutomationLane& lane, int pointIndex)
    {
        return pointIndex >= 0 && pointIndex < static_cast<int> (lane.points.size());
    }
};
}
