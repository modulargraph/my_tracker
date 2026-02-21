#pragma once

#include <JuceHeader.h>
#include "PatternData.h"
#include "SimpleSampler.h"
#include "InstrumentParams.h"
#include "Arrangement.h"
#include "TrackLayout.h"
#include "MixerState.h"
#include "SendEffectsParams.h"
#include "InstrumentSlotInfo.h"

class ProjectSerializer
{
public:
    static juce::String saveToFile (const juce::File& file, const PatternData& patternData,
                                    double bpm, int rowsPerBeat,
                                    const std::map<int, juce::File>& loadedSamples,
                                    const std::map<int, InstrumentParams>& instrumentParams,
                                    const Arrangement& arrangement,
                                    const TrackLayout& trackLayout,
                                    const MixerState& mixerState,
                                    const DelayParams& delayParams,
                                    const ReverbParams& reverbParams,
                                    int followMode = 0,
                                    const juce::String& browserDir = {},
                                    const std::map<int, InstrumentSlotInfo>* pluginSlots = nullptr);

    static juce::String loadFromFile (const juce::File& file, PatternData& patternData,
                                      double& bpm, int& rowsPerBeat,
                                      std::map<int, juce::File>& loadedSamples,
                                      std::map<int, InstrumentParams>& instrumentParams,
                                      Arrangement& arrangement,
                                      TrackLayout& trackLayout,
                                      MixerState& mixerState,
                                      DelayParams& delayParams,
                                      ReverbParams& reverbParams,
                                      int* followMode = nullptr,
                                      juce::String* browserDir = nullptr,
                                      std::map<int, InstrumentSlotInfo>* pluginSlots = nullptr);

    // Global browser directory persistence (independent of project files)
    static void saveGlobalBrowserDir (const juce::String& dir);
    static juce::String loadGlobalBrowserDir();

    // Global plugin scan path persistence (independent of project files)
    static void saveGlobalPluginScanPaths (const juce::StringArray& paths);
    static juce::StringArray loadGlobalPluginScanPaths();

private:
    static juce::ValueTree patternToValueTree (const Pattern& pattern, int index);
    static void valueTreeToPattern (const juce::ValueTree& tree, Pattern& pattern, int version);
};
