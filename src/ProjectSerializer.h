#pragma once

#include <JuceHeader.h>
#include "PatternData.h"
#include "SimpleSampler.h"

class ProjectSerializer
{
public:
    static juce::String saveToFile (const juce::File& file, const PatternData& patternData,
                                    double bpm, int rowsPerBeat,
                                    const std::map<int, juce::File>& loadedSamples);

    static juce::String loadFromFile (const juce::File& file, PatternData& patternData,
                                      double& bpm, int& rowsPerBeat,
                                      std::map<int, juce::File>& loadedSamples);

private:
    static juce::ValueTree patternToValueTree (const Pattern& pattern, int index);
    static void valueTreeToPattern (const juce::ValueTree& tree, Pattern& pattern);
};
