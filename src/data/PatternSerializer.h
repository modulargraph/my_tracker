#pragma once

#include <JuceHeader.h>

class PatternData;
struct Pattern;

namespace PatternSerializer
{
    juce::ValueTree patternToValueTree (const Pattern& pattern, int index);
    void valueTreeToPattern (const juce::ValueTree& tree, Pattern& pattern, int version);
    void saveAllPatterns (juce::ValueTree& root, const PatternData& patternData);
    void loadAllPatterns (const juce::ValueTree& root, PatternData& patternData,
                          const juce::ValueTree& settings, int version, int masterFxLaneCount);
}
