#pragma once

#include <JuceHeader.h>
#include "PluginAutomationData.h"
#include <vector>

class TrackerEngine; // forward declare

class PluginAutomationManager
{
public:
    explicit PluginAutomationManager (TrackerEngine& engine);

    // Resolve plugin ID ("inst:INDEX" or "insert:TRACK:SLOT") to plugin instance
    juce::AudioPluginInstance* resolvePluginInstance (const juce::String& pluginId) const;

    // Find tracked automated parameter
    struct AutomatedParam
    {
        juce::String pluginId;
        int paramIndex = -1;
        float baselineValue = 0.0f;
    };

    AutomatedParam* findAutomatedParam (const juce::String& pluginId, int paramIndex);
    const AutomatedParam* findAutomatedParam (const juce::String& pluginId, int paramIndex) const;

    // Apply automation from pattern data
    void applyPatternAutomation (const PatternAutomationData& automationData,
                                 int patternLength, int rowsPerBeat);

    // Update plugin params for current playback row
    void applyAutomationForPlaybackRow (const PatternAutomationData& automationData, int row);

    // Restore all automated params to baseline values
    void resetAutomationParameters();

    // Direct access to tracked params (for external callers that need it)
    const std::vector<AutomatedParam>& getTrackedParams() const { return lastAutomatedParams; }

private:
    TrackerEngine& engine;
    std::vector<AutomatedParam> lastAutomatedParams;
};
