#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include "MixerState.h"
#include <map>
#include <functional>

namespace te = tracktion;

class TrackerEngine;

class InsertPluginManager
{
public:
    explicit InsertPluginManager (TrackerEngine& engine);
    ~InsertPluginManager();

    bool addInsertPlugin (int trackIndex, const juce::PluginDescription& desc);
    void removeInsertPlugin (int trackIndex, int slotIndex);
    void setInsertBypassed (int trackIndex, int slotIndex, bool bypassed);
    te::Plugin* getInsertPlugin (int trackIndex, int slotIndex);
    void rebuildInsertChain (int trackIndex);
    void snapshotInsertPluginStates();

    void openPluginEditor (int trackIndex, int slotIndex);
    void closePluginEditor (int trackIndex, int slotIndex);

    void setMixerState (MixerState* state) { mixerStatePtr = state; }
    MixerState* getMixerState() const { return mixerStatePtr; }

    // Clear all editor windows (call before edit destruction)
    void clearEditorWindows() { pluginEditorWindows.clear(); }

    // Callback when inserts change (for UI refresh)
    std::function<void()> onInsertStateChanged;

private:
    TrackerEngine& engine;
    MixerState* mixerStatePtr = nullptr;
    std::map<juce::String, std::unique_ptr<juce::DocumentWindow>> pluginEditorWindows;
};
