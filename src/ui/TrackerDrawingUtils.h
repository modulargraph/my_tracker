#pragma once

#include <JuceHeader.h>
#include "TrackerLookAndFeel.h"

/**
 * Shared UI drawing utilities used across tracker components.
 * Consolidates duplicated drawing code from SampleEditorComponent and SendEffectsComponent.
 */
namespace TrackerDrawingUtils
{

// Shared layout constants
inline constexpr int kListItemHeight = 22;

//==============================================================================
// Vertical bar meter (used for parameter visualization)
//==============================================================================

inline void drawBarMeter (juce::Graphics& g, TrackerLookAndFeel& lookAndFeel,
                          juce::Rectangle<int> area, float value01, bool focused, juce::Colour colour)
{
    auto bg = lookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId);
    auto gridCol = lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId);

    // Column border
    g.setColour (focused ? gridCol.brighter (0.4f) : gridCol);
    g.drawRect (area, 1);

    // Inner bar area with padding
    auto inner = area.reduced (6, 4);

    // Bar background
    g.setColour (bg.brighter (0.04f));
    g.fillRect (inner);

    // Bar outline
    g.setColour (gridCol.withAlpha (0.6f));
    g.drawRect (inner, 1);

    // Bar fill from bottom
    value01 = juce::jlimit (0.0f, 1.0f, value01);
    int fillH = juce::roundToInt (value01 * static_cast<float> (inner.getHeight() - 2));

    if (fillH > 0)
    {
        auto fillRect = juce::Rectangle<int> (
            inner.getX() + 1,
            inner.getBottom() - 1 - fillH,
            inner.getWidth() - 2,
            fillH);

        g.setColour (colour.withAlpha (focused ? 0.85f : 0.5f));
        g.fillRect (fillRect);
    }
}

//==============================================================================
// Scrollable list column (used for enum/list parameter selection)
//==============================================================================

inline void drawListColumn (juce::Graphics& g, TrackerLookAndFeel& lookAndFeel,
                            juce::Rectangle<int> area, const juce::StringArray& items,
                            int selectedIndex, bool focused, juce::Colour colour,
                            bool showScrollIndicators = true)
{
    auto bg = lookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId);
    auto gridCol = lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId);
    auto textCol = lookAndFeel.findColour (TrackerLookAndFeel::textColourId);

    // Column border
    g.setColour (focused ? gridCol.brighter (0.4f) : gridCol);
    g.drawRect (area, 1);

    auto inner = area.reduced (1);
    int numItems = items.size();
    if (numItems == 0) return;

    // Calculate visible items and scrolling
    int maxVisible = inner.getHeight() / kListItemHeight;
    int scrollOffset = 0;

    if (numItems > maxVisible && selectedIndex >= 0)
        scrollOffset = juce::jlimit (0, numItems - maxVisible,
                                      selectedIndex - maxVisible / 2);

    int visibleCount = juce::jmin (numItems - scrollOffset, maxVisible);

    g.setFont (lookAndFeel.getMonoFont (11.0f));

    for (int vi = 0; vi < visibleCount; ++vi)
    {
        int i = scrollOffset + vi;
        int y = inner.getY() + vi * kListItemHeight;
        auto itemRect = juce::Rectangle<int> (inner.getX(), y, inner.getWidth(), kListItemHeight);

        if (i == selectedIndex)
        {
            // Highlighted item: filled background with inverted text
            g.setColour (focused ? colour : colour.withAlpha (0.4f));
            g.fillRect (itemRect);
            g.setColour (focused ? bg : textCol);
        }
        else
        {
            g.setColour (textCol.withAlpha (focused ? 0.65f : 0.35f));
        }

        g.drawText (items[i], itemRect.reduced (6, 0), juce::Justification::centredLeft);
    }

    // Scroll indicators
    if (showScrollIndicators)
    {
        if (scrollOffset > 0)
        {
            g.setColour (textCol.withAlpha (0.3f));
            g.drawText ("...", inner.getX(), inner.getY() - 2, inner.getWidth(), 12,
                        juce::Justification::centredRight);
        }
        if (scrollOffset + visibleCount < numItems)
        {
            g.setColour (textCol.withAlpha (0.3f));
            int bottomY = inner.getY() + visibleCount * kListItemHeight;
            g.drawText ("...", inner.getX(), bottomY, inner.getWidth(), 12,
                        juce::Justification::centredRight);
        }
    }
}

} // namespace TrackerDrawingUtils
