#pragma once

#include <JuceHeader.h>
#include "PatternData.h"
#include <algorithm>
#include <numeric>

// Per-track note trigger mode
enum class NoteMode { Kill, Release };

struct TrackGroup
{
    juce::String name;
    juce::Colour colour { 0xff5c8abf };
    std::vector<int> trackIndices; // physical track indices, in display order
};

class TrackLayout
{
public:
    TrackLayout() { resetToDefault(); }

    int visualToPhysical (int visualPos) const
    {
        if (visualPos < 0 || visualPos >= kNumTracks)
            return juce::jlimit (0, kNumTracks - 1, visualPos);
        return visualOrder[static_cast<size_t> (visualPos)];
    }

    int physicalToVisual (int physicalTrack) const
    {
        for (int i = 0; i < kNumTracks; ++i)
            if (visualOrder[static_cast<size_t> (i)] == physicalTrack)
                return i;
        return 0;
    }

    void moveTrack (int fromVisual, int toVisual)
    {
        if (fromVisual < 0 || fromVisual >= kNumTracks || toVisual < 0 || toVisual >= kNumTracks)
            return;
        if (fromVisual == toVisual)
            return;

        int physTrack = visualOrder[static_cast<size_t> (fromVisual)];

        // Remove from old position
        for (int i = fromVisual; i < kNumTracks - 1; ++i)
            visualOrder[static_cast<size_t> (i)] = visualOrder[static_cast<size_t> (i + 1)];

        // Insert at new position (shift right)
        int insertAt = (toVisual > fromVisual) ? toVisual : toVisual;
        for (int i = kNumTracks - 1; i > insertAt; --i)
            visualOrder[static_cast<size_t> (i)] = visualOrder[static_cast<size_t> (i - 1)];

        visualOrder[static_cast<size_t> (insertAt)] = physTrack;
    }

    void swapTracks (int visualA, int visualB)
    {
        if (visualA < 0 || visualA >= kNumTracks || visualB < 0 || visualB >= kNumTracks)
            return;
        std::swap (visualOrder[static_cast<size_t> (visualA)],
                   visualOrder[static_cast<size_t> (visualB)]);
    }

    // Move a contiguous visual range one step left or right (delta = -1 or +1)
    void moveVisualRange (int rangeStart, int rangeEnd, int delta)
    {
        if (rangeStart > rangeEnd) std::swap (rangeStart, rangeEnd);
        if (delta == -1 && rangeStart <= 0) return;
        if (delta == +1 && rangeEnd >= kNumTracks - 1) return;

        if (delta == -1)
        {
            // Swap the element just before the range with each element stepping right
            int saved = visualOrder[static_cast<size_t> (rangeStart - 1)];
            for (int i = rangeStart - 1; i < rangeEnd; ++i)
                visualOrder[static_cast<size_t> (i)] = visualOrder[static_cast<size_t> (i + 1)];
            visualOrder[static_cast<size_t> (rangeEnd)] = saved;
        }
        else
        {
            // Swap the element just after the range with each element stepping left
            int saved = visualOrder[static_cast<size_t> (rangeEnd + 1)];
            for (int i = rangeEnd + 1; i > rangeStart; --i)
                visualOrder[static_cast<size_t> (i)] = visualOrder[static_cast<size_t> (i - 1)];
            visualOrder[static_cast<size_t> (rangeStart)] = saved;
        }
    }

    // Track names (indexed by physical track)
    const juce::String& getTrackName (int physicalTrack) const
    {
        return trackNames[static_cast<size_t> (juce::jlimit (0, kNumTracks - 1, physicalTrack))];
    }

    void setTrackName (int physicalTrack, const juce::String& name)
    {
        trackNames[static_cast<size_t> (juce::jlimit (0, kNumTracks - 1, physicalTrack))] = name;
    }

    const std::array<juce::String, kNumTracks>& getTrackNames() const { return trackNames; }

    // Per-track note mode (Kill = note-off at end of row, Release = note-off at next note)
    NoteMode getTrackNoteMode (int physicalTrack) const
    {
        return trackNoteModes[static_cast<size_t> (juce::jlimit (0, kNumTracks - 1, physicalTrack))];
    }

    void setTrackNoteMode (int physicalTrack, NoteMode mode)
    {
        trackNoteModes[static_cast<size_t> (juce::jlimit (0, kNumTracks - 1, physicalTrack))] = mode;
    }

    void toggleTrackNoteMode (int physicalTrack)
    {
        auto& m = trackNoteModes[static_cast<size_t> (juce::jlimit (0, kNumTracks - 1, physicalTrack))];
        m = (m == NoteMode::Kill) ? NoteMode::Release : NoteMode::Kill;
    }

    const std::array<NoteMode, kNumTracks>& getTrackNoteModes() const { return trackNoteModes; }

    static juce::Colour getGroupPaletteColour (int index)
    {
        static const juce::Colour palette[] = {
            juce::Colour (0xff5c8abf),  // blue
            juce::Colour (0xffbf7a3a),  // orange
            juce::Colour (0xff6abf6a),  // green
            juce::Colour (0xffbf5c9e),  // pink
            juce::Colour (0xffc4c44a),  // yellow
            juce::Colour (0xff8a6abf),  // purple
            juce::Colour (0xff4abfbf),  // teal
            juce::Colour (0xffbf4a4a),  // red
        };
        return palette[index % 8];
    }

    int createGroup (const juce::String& name, int visualStart, int visualEnd)
    {
        if (visualStart > visualEnd)
            std::swap (visualStart, visualEnd);

        visualStart = juce::jlimit (0, kNumTracks - 1, visualStart);
        visualEnd = juce::jlimit (0, kNumTracks - 1, visualEnd);

        TrackGroup group;
        group.name = name;
        group.colour = getGroupPaletteColour (static_cast<int> (groups.size()));

        for (int v = visualStart; v <= visualEnd; ++v)
            group.trackIndices.push_back (visualOrder[static_cast<size_t> (v)]);

        groups.push_back (std::move (group));
        return static_cast<int> (groups.size()) - 1;
    }

    void removeGroup (int groupIndex)
    {
        if (groupIndex < 0 || groupIndex >= static_cast<int> (groups.size()))
            return;
        groups.erase (groups.begin() + groupIndex);
    }

    int getGroupForTrack (int physicalTrack) const
    {
        for (int i = 0; i < static_cast<int> (groups.size()); ++i)
        {
            auto& g = groups[static_cast<size_t> (i)];
            for (auto idx : g.trackIndices)
                if (idx == physicalTrack)
                    return i;
        }
        return -1;
    }

    std::pair<int, int> getGroupVisualRange (int groupIndex) const
    {
        if (groupIndex < 0 || groupIndex >= static_cast<int> (groups.size()))
            return { 0, 0 };

        auto& g = groups[static_cast<size_t> (groupIndex)];
        if (g.trackIndices.empty())
            return { 0, 0 };

        int minVisual = kNumTracks;
        int maxVisual = -1;

        for (auto physIdx : g.trackIndices)
        {
            int v = physicalToVisual (physIdx);
            minVisual = juce::jmin (minVisual, v);
            maxVisual = juce::jmax (maxVisual, v);
        }

        return { minVisual, maxVisual };
    }

    bool hasGroups() const { return ! groups.empty(); }
    int getNumGroups() const { return static_cast<int> (groups.size()); }

    const TrackGroup& getGroup (int index) const { return groups[static_cast<size_t> (index)]; }
    TrackGroup& getGroup (int index) { return groups[static_cast<size_t> (index)]; }

    const std::vector<TrackGroup>& getGroups() const { return groups; }
    void addGroup (TrackGroup group) { groups.push_back (std::move (group)); }

    const std::array<int, kNumTracks>& getVisualOrder() const { return visualOrder; }

    void setVisualOrder (const std::array<int, kNumTracks>& order) { visualOrder = order; }

    void resetToDefault()
    {
        std::iota (visualOrder.begin(), visualOrder.end(), 0);
        groups.clear();
        for (auto& n : trackNames) n.clear();
        for (auto& m : trackNoteModes) m = NoteMode::Kill;
    }

    void clear() { resetToDefault(); }

private:
    std::array<int, kNumTracks> visualOrder {};
    std::vector<TrackGroup> groups;
    std::array<juce::String, kNumTracks> trackNames;
    std::array<NoteMode, kNumTracks> trackNoteModes {};
};
