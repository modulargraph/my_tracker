#pragma once

#include <JuceHeader.h>

namespace PluginAutomationInternal
{
class AutomationCoordinateMapper
{
public:
    AutomationCoordinateMapper (juce::Rectangle<int> graphBounds,
                                int rowsInPattern,
                                float zoom,
                                float viewStart)
        : bounds (graphBounds.toFloat()),
          patternLength (juce::jmax (1, rowsInPattern)),
          zoomLevel (juce::jmax (1.0f, zoom)),
          viewStartRow (viewStart)
    {
    }

    float getVisibleRange() const
    {
        return static_cast<float> (patternLength) / zoomLevel;
    }

    float clampViewStart (float candidate) const
    {
        float visibleRange = getVisibleRange();
        float maxStart = juce::jmax (0.0f, static_cast<float> (patternLength) - visibleRange);
        return juce::jlimit (0.0f, maxStart, candidate);
    }

    juce::Point<float> dataToScreen (float row, float value) const
    {
        if (bounds.isEmpty())
            return { bounds.getX(), bounds.getBottom() };

        float visibleRange = getVisibleRange();
        float width = juce::jmax (1.0f, bounds.getWidth());
        float height = juce::jmax (1.0f, bounds.getHeight());
        float x = bounds.getX() + ((row - viewStartRow) / visibleRange) * width;
        float y = bounds.getBottom() - value * height;
        return { x, y };
    }

    juce::Point<float> screenToData (juce::Point<float> screenPos) const
    {
        if (bounds.isEmpty())
            return { 0.0f, 0.0f };

        float visibleRange = getVisibleRange();
        float width = juce::jmax (1.0f, bounds.getWidth());
        float height = juce::jmax (1.0f, bounds.getHeight());
        float row = viewStartRow + ((screenPos.x - bounds.getX()) / width) * visibleRange;
        float value = 1.0f - (screenPos.y - bounds.getY()) / height;
        return { juce::jlimit (0.0f, static_cast<float> (patternLength - 1), row),
                 juce::jlimit (0.0f, 1.0f, value) };
    }

private:
    juce::Rectangle<float> bounds;
    int patternLength = 1;
    float zoomLevel = 1.0f;
    float viewStartRow = 0.0f;
};
}
