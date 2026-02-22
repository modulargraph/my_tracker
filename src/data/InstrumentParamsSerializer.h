#pragma once

#include <JuceHeader.h>
#include "InstrumentParams.h"
#include <map>

namespace InstrumentParamsSerializer
{
    void save (juce::ValueTree& root, const std::map<int, InstrumentParams>& instrumentParams);
    void load (const juce::ValueTree& root, std::map<int, InstrumentParams>& instrumentParams, int version);
}
