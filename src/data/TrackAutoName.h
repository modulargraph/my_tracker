#pragma once

#include <array>
#include <limits>
#include <map>

#include <JuceHeader.h>

#include "InstrumentSlotInfo.h"
#include "Pattern.h"
#include "TrackLayout.h"
#include "TrackerConstants.h"

namespace TrackAutoName
{
inline juce::String getInstrumentDisplayName (
    int instrument,
    const std::map<int, juce::File>& loadedSamples,
    const std::map<int, InstrumentSlotInfo>& pluginSlots)
{
    if (instrument < 0 || instrument >= 256)
        return {};

    if (auto pluginIt = pluginSlots.find (instrument);
        pluginIt != pluginSlots.end() && pluginIt->second.isPlugin())
        return pluginIt->second.pluginDescription.name.trim();

    if (auto sampleIt = loadedSamples.find (instrument); sampleIt != loadedSamples.end())
        return sampleIt->second.getFileNameWithoutExtension().trim();

    return {};
}

inline juce::String getFallbackTrackDisplayName (
    int track,
    const std::map<int, juce::File>& loadedSamples,
    const std::map<int, InstrumentSlotInfo>& pluginSlots)
{
    for (const auto& [instrument, info] : pluginSlots)
    {
        if (info.isPlugin() && info.ownerTrack == track)
        {
            auto name = getInstrumentDisplayName (instrument, loadedSamples, pluginSlots);
            if (name.isNotEmpty())
                return name;
        }
    }

    if (auto sampleIt = loadedSamples.find (track); sampleIt != loadedSamples.end())
        return sampleIt->second.getFileNameWithoutExtension().trim();

    return {};
}

inline std::array<juce::String, kNumTracks> buildForPattern (
    const Pattern& pattern,
    const std::map<int, juce::File>& loadedSamples,
    const std::map<int, InstrumentSlotInfo>& pluginSlots)
{
    std::array<juce::String, kNumTracks> names {};

    for (int track = 0; track < kNumTracks; ++track)
    {
        std::array<int, 256> counts {};
        std::array<int, 256> firstSeen;
        firstSeen.fill (std::numeric_limits<int>::max());

        int sequence = 0;
        for (int row = 0; row < pattern.numRows; ++row)
        {
            const auto& cell = pattern.getCell (row, track);
            for (int lane = 0; lane < cell.getNumNoteLanes(); ++lane)
            {
                auto slot = cell.getNoteLane (lane);
                if (slot.instrument >= 0 && slot.instrument < 256)
                {
                    ++counts[static_cast<size_t> (slot.instrument)];
                    firstSeen[static_cast<size_t> (slot.instrument)]
                        = juce::jmin (firstSeen[static_cast<size_t> (slot.instrument)], sequence);
                }
                ++sequence;
            }
        }

        int bestInstrument = -1;
        int bestCount = 0;
        int bestFirstSeen = std::numeric_limits<int>::max();
        for (int instrument = 0; instrument < 256; ++instrument)
        {
            const auto count = counts[static_cast<size_t> (instrument)];
            if (count <= 0)
                continue;

            if (getInstrumentDisplayName (instrument, loadedSamples, pluginSlots).isEmpty())
                continue;

            const auto first = firstSeen[static_cast<size_t> (instrument)];
            if (count > bestCount || (count == bestCount && first < bestFirstSeen))
            {
                bestInstrument = instrument;
                bestCount = count;
                bestFirstSeen = first;
            }
        }

        if (bestInstrument >= 0)
            names[static_cast<size_t> (track)] = getInstrumentDisplayName (bestInstrument, loadedSamples, pluginSlots);
        else
            names[static_cast<size_t> (track)] = getFallbackTrackDisplayName (track, loadedSamples, pluginSlots);
    }

    return names;
}

inline bool applyToTrack (
    TrackLayout& layout,
    int physicalTrack,
    const std::array<juce::String, kNumTracks>& names)
{
    if (physicalTrack < 0 || physicalTrack >= kNumTracks)
        return false;

    const auto& name = names[static_cast<size_t> (physicalTrack)];
    if (name.isEmpty())
        return false;

    if (layout.getTrackName (physicalTrack) == name)
        return false;

    layout.setTrackName (physicalTrack, name);
    return true;
}

inline int applyToCurrentTrackLanes (
    TrackLayout& layout,
    const std::array<juce::String, kNumTracks>& names)
{
    int changed = 0;
    const int trackLaneCount = layout.getTrackLaneCount();

    for (int visual = 0; visual < trackLaneCount; ++visual)
    {
        const int physicalTrack = layout.visualToPhysical (visual);
        if (applyToTrack (layout, physicalTrack, names))
            ++changed;
    }

    return changed;
}
}
