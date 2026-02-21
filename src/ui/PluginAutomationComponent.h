#pragma once

#include <JuceHeader.h>
#include "PluginAutomationData.h"
#include "TrackerLookAndFeel.h"
#include "PatternData.h"

//==============================================================================
// Info about an automatable plugin target (for dropdown population)
//==============================================================================

struct AutomatablePluginInfo
{
    juce::String pluginId;       // Unique identifier (e.g. "inst:3" or insert identifier)
    juce::String displayName;    // Shown in dropdown
    int owningTrack = -1;        // Track that owns this plugin
    bool isInstrument = false;   // True if this is an instrument plugin

    // Parameter list for this plugin (populated when selected)
    struct ParamInfo
    {
        int index = 0;
        juce::String name;
    };
    std::vector<ParamInfo> parameters;
};

//==============================================================================
// Bottom automation panel for drawing per-pattern plugin parameter curves
//==============================================================================

class PluginAutomationComponent : public juce::Component
{
public:
    static constexpr int kPanelHeight = 140;
    static constexpr int kControlsWidth = 180;

    PluginAutomationComponent (TrackerLookAndFeel& lnf);
    ~PluginAutomationComponent() override = default;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

    //==============================================================================
    // Data binding

    /** Set the automation data for the current pattern. */
    void setAutomationData (PatternAutomationData* data);

    /** Set the pattern length (number of rows) for the x-axis. */
    void setPatternLength (int numRows);

    /** Set the current cursor track (to filter plugins). */
    void setCurrentTrack (int trackIndex);

    /** Set the baseline parameter value (0-1) for the currently selected parameter. */
    void setBaseline (float baselineValue);

    /** Populate the plugin dropdown with available plugins for the current track. */
    void setAvailablePlugins (const std::vector<AutomatablePluginInfo>& plugins);

    /** Get the currently selected plugin ID. */
    juce::String getSelectedPluginId() const;

    /** Get the currently selected parameter index. */
    int getSelectedParameterIndex() const;

    //==============================================================================
    // Callbacks

    /** Called when automation data changes (points added/moved/removed). */
    std::function<void()> onAutomationChanged;

    /** Called when the selected plugin changes (to populate parameters). */
    std::function<void (const juce::String& pluginId)> onPluginSelected;

    /** Called when the selected parameter changes (to update baseline). */
    std::function<void (const juce::String& pluginId, int paramIndex)> onParameterSelected;

private:
    TrackerLookAndFeel& lookAndFeel;

    // Data
    PatternAutomationData* automationData = nullptr;
    int patternLength = 64;
    int currentTrack = 0;
    float baseline = 0.5f;

    // UI controls
    juce::ComboBox pluginDropdown;
    juce::ComboBox parameterDropdown;
    juce::Label pluginLabel;
    juce::Label paramLabel;

    // Available plugins and their parameter lists
    std::vector<AutomatablePluginInfo> availablePlugins;
    bool suppressSelectionCallbacks = false;

    // Graph area
    juce::Rectangle<int> getGraphBounds() const;

    // Point interaction
    int dragPointIndex = -1;
    bool isDragging = false;

    // Convert between screen coords and data coords
    juce::Point<float> dataToScreen (float row, float value) const;
    juce::Point<float> screenToData (juce::Point<float> screenPos) const;
    int findPointNear (juce::Point<float> screenPos, float maxDist = 8.0f) const;

    // Get the current automation lane (or nullptr)
    AutomationLane* getCurrentLane();
    const AutomationLane* getCurrentLane() const;

    void pluginSelectionChanged();
    void parameterSelectionChanged();
    void drawGrid (juce::Graphics& g, juce::Rectangle<int> bounds) const;
    void drawCurve (juce::Graphics& g, juce::Rectangle<int> bounds) const;
    void drawBaseline (juce::Graphics& g, juce::Rectangle<int> bounds) const;
    void drawPoints (juce::Graphics& g, juce::Rectangle<int> bounds) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginAutomationComponent)
};
