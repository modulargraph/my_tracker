#pragma once

#include <JuceHeader.h>
#include "MixerState.h"

namespace MixerStateSerializer
{
    void save (juce::ValueTree& root, const MixerState& mixerState);
    void load (const juce::ValueTree& root, MixerState& mixerState);
}
