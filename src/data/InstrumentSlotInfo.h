#pragma once

#include <JuceHeader.h>

//==============================================================================
// Instrument source type: whether a slot uses a sample or a hosted plugin
//==============================================================================

enum class InstrumentSourceType
{
    Sample,           // Traditional sample-based instrument (default)
    PluginInstrument  // Hosted VST/AU plugin instrument
};

//==============================================================================
// Per-instrument-slot metadata for plugin instruments
//==============================================================================

struct InstrumentSlotInfo
{
    InstrumentSourceType sourceType = InstrumentSourceType::Sample;

    // Plugin instrument fields (only valid when sourceType == PluginInstrument)
    juce::PluginDescription pluginDescription;  // Identifies which plugin
    int ownerTrack = -1;                         // Which track owns this plugin instrument (-1 = unassigned)

    bool isPlugin() const { return sourceType == InstrumentSourceType::PluginInstrument; }
    bool isSample() const { return sourceType == InstrumentSourceType::Sample; }
    bool hasOwner() const { return ownerTrack >= 0; }

    void clear()
    {
        sourceType = InstrumentSourceType::Sample;
        pluginDescription = {};
        ownerTrack = -1;
    }

    void setPlugin (const juce::PluginDescription& desc, int track)
    {
        sourceType = InstrumentSourceType::PluginInstrument;
        pluginDescription = desc;
        ownerTrack = track;
    }
};

//==============================================================================
// Track content mode: derived from which instruments are used on a track
//==============================================================================

enum class TrackContentMode
{
    Empty,            // No instruments assigned to this track
    Sample,           // Only sample instruments on this track
    PluginInstrument  // Only plugin instruments on this track
};
