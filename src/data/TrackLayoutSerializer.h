#pragma once

#include <JuceHeader.h>
#include "TrackLayout.h"

namespace TrackLayoutSerializer
{
    void save (juce::ValueTree& root, const TrackLayout& trackLayout);
    void load (const juce::ValueTree& root, TrackLayout& trackLayout);
}
