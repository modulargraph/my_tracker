#pragma once

#include <array>
#include <vector>
#include <JuceHeader.h>
#include "PatternData.h"

// Maximum number of insert plugin slots per track
static constexpr int kMaxInsertSlots = 8;

// Maximum number of group buses
static constexpr int kMaxGroupBuses = 8;

// Represents a single insert plugin slot on a track
struct InsertSlotState
{
    juce::String pluginName;                // Display name of the plugin
    juce::String pluginIdentifier;          // Unique identifier for loading (PluginDescription::createIdentifierString)
    juce::String pluginFormatName;          // e.g. "VST3", "AudioUnit"
    juce::ValueTree pluginState;            // Saved plugin state (ValueTree snapshot)
    bool bypassed = false;

    bool isEmpty() const { return pluginIdentifier.isEmpty(); }
};

struct TrackMixState
{
    double volume = 0.0;       // dB, -inf (-100) to +12
    int pan = 0;               // -50 to +50
    bool muted = false;
    bool soloed = false;

    // EQ
    double eqLowGain = 0.0;   // dB, -12 to +12
    double eqMidGain = 0.0;
    double eqHighGain = 0.0;
    double eqMidFreq = 1000.0; // Hz, 200 to 8000

    // Compressor
    double compThreshold = 0.0;  // dB, -60 to 0
    double compRatio = 1.0;      // 1:1 to 20:1
    double compAttack = 10.0;    // ms, 0.1 to 100
    double compRelease = 100.0;  // ms, 10 to 1000

    // Send levels
    double reverbSend = -100.0;  // dB, -100 (off) to 0
    double delaySend = -100.0;   // dB, -100 (off) to 0

    bool isDefault() const
    {
        return volume == 0.0 && pan == 0
            && ! muted && ! soloed
            && eqLowGain == 0.0 && eqMidGain == 0.0 && eqHighGain == 0.0
            && eqMidFreq == 1000.0
            && compThreshold == 0.0 && compRatio == 1.0
            && compAttack == 10.0 && compRelease == 100.0
            && reverbSend == -100.0 && delaySend == -100.0;
    }
};

// Send return channel state (for delay and reverb return mixer lanes)
struct SendReturnState
{
    double volume = 0.0;       // dB, -inf (-100) to +12
    int pan = 0;               // -50 to +50
    bool muted = false;

    // EQ
    double eqLowGain = 0.0;   // dB, -12 to +12
    double eqMidGain = 0.0;
    double eqHighGain = 0.0;
    double eqMidFreq = 1000.0; // Hz, 200 to 8000

    bool isDefault() const
    {
        return volume == 0.0 && pan == 0 && ! muted
            && eqLowGain == 0.0 && eqMidGain == 0.0 && eqHighGain == 0.0
            && eqMidFreq == 1000.0;
    }
};

// Group bus state (one per track group)
struct GroupBusState
{
    double volume = 0.0;       // dB, -inf (-100) to +12
    int pan = 0;               // -50 to +50
    bool muted = false;
    bool soloed = false;

    // EQ
    double eqLowGain = 0.0;   // dB, -12 to +12
    double eqMidGain = 0.0;
    double eqHighGain = 0.0;
    double eqMidFreq = 1000.0; // Hz, 200 to 8000

    // Compressor
    double compThreshold = 0.0;  // dB, -60 to 0
    double compRatio = 1.0;      // 1:1 to 20:1
    double compAttack = 10.0;    // ms, 0.1 to 100
    double compRelease = 100.0;  // ms, 10 to 1000

    bool isDefault() const
    {
        return volume == 0.0 && pan == 0 && ! muted && ! soloed
            && eqLowGain == 0.0 && eqMidGain == 0.0 && eqHighGain == 0.0
            && eqMidFreq == 1000.0
            && compThreshold == 0.0 && compRatio == 1.0
            && compAttack == 10.0 && compRelease == 100.0;
    }
};

// Master track state
struct MasterMixState
{
    double volume = 0.0;       // dB, -inf (-100) to +12
    int pan = 0;               // -50 to +50

    // EQ
    double eqLowGain = 0.0;   // dB, -12 to +12
    double eqMidGain = 0.0;
    double eqHighGain = 0.0;
    double eqMidFreq = 1000.0; // Hz, 200 to 8000

    // Compressor
    double compThreshold = 0.0;  // dB, -60 to 0
    double compRatio = 1.0;      // 1:1 to 20:1
    double compAttack = 10.0;    // ms, 0.1 to 100
    double compRelease = 100.0;  // ms, 10 to 1000

    // Limiter
    double limiterThreshold = 0.0;  // dB, -24 to 0 (0 = off)
    double limiterRelease = 50.0;   // ms, 1 to 500

    bool isDefault() const
    {
        return volume == 0.0 && pan == 0
            && eqLowGain == 0.0 && eqMidGain == 0.0 && eqHighGain == 0.0
            && eqMidFreq == 1000.0
            && compThreshold == 0.0 && compRatio == 1.0
            && compAttack == 10.0 && compRelease == 100.0
            && limiterThreshold == 0.0 && limiterRelease == 50.0;
    }
};

struct MixerState
{
    std::array<TrackMixState, kNumTracks> tracks {};

    // Per-track insert plugin slots (between channel strip and track output)
    std::array<std::vector<InsertSlotState>, kNumTracks> insertSlots {};

    // Send return channels (delay = 0, reverb = 1)
    std::array<SendReturnState, 2> sendReturns {};

    // Group bus states (indexed by group index in TrackLayout)
    std::array<GroupBusState, kMaxGroupBuses> groupBuses {};

    // Master track state
    MasterMixState master {};

    // Master insert plugin slots
    std::vector<InsertSlotState> masterInsertSlots {};

    bool isDefault() const
    {
        for (auto& t : tracks)
            if (! t.isDefault())
                return false;
        for (auto& slots : insertSlots)
            if (! slots.empty())
                return false;
        for (auto& sr : sendReturns)
            if (! sr.isDefault())
                return false;
        for (auto& gb : groupBuses)
            if (! gb.isDefault())
                return false;
        if (! master.isDefault())
            return false;
        if (! masterInsertSlots.empty())
            return false;
        return true;
    }

    void reset()
    {
        for (auto& t : tracks)
            t = TrackMixState {};
        for (auto& slots : insertSlots)
            slots.clear();
        for (auto& sr : sendReturns)
            sr = SendReturnState {};
        for (auto& gb : groupBuses)
            gb = GroupBusState {};
        master = MasterMixState {};
        masterInsertSlots.clear();
    }
};
