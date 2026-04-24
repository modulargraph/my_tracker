#pragma once

#include <algorithm>
#include <array>
#include <numeric>
#include <JuceHeader.h>
#include "TrackerConstants.h"

// Per-track note trigger mode
enum class NoteMode { Kill, Release };

struct TrackGroup
{
    juce::String name;
    juce::Colour colour { 0xff5c8abf };
    std::vector<int> trackIndices; // physical track indices, in display order

    bool operator== (const TrackGroup& other) const
    {
        return name == other.name
            && colour == other.colour
            && trackIndices == other.trackIndices;
    }
};

class TrackLayout
{
public:
    struct Snapshot
    {
        std::array<int, kNumTracks> visualOrder {};
        std::vector<TrackGroup> groups;
        std::array<juce::String, kNumTracks> trackNames;
        std::array<NoteMode, kNumTracks> trackNoteModes {};
        std::array<int, kNumTracks> trackFxLaneCounts {};
        std::array<int, kNumTracks> trackNoteLaneCounts {};
        int masterFxLaneCount = 1;
        int trackLaneCount = kDefaultTrackLaneCount;
    };

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

    bool moveTrack (int fromVisual, int toVisual)
    {
        if (fromVisual < 0 || fromVisual >= kNumTracks || toVisual < 0 || toVisual >= kNumTracks)
            return false;
        if (fromVisual == toVisual)
            return false;

        auto candidateOrder = visualOrder;
        int physTrack = candidateOrder[static_cast<size_t> (fromVisual)];

        // Remove from old position
        for (int i = fromVisual; i < kNumTracks - 1; ++i)
            candidateOrder[static_cast<size_t> (i)] = candidateOrder[static_cast<size_t> (i + 1)];

        // Insert at new position (shift right)
        int insertAt = toVisual;
        for (int i = kNumTracks - 1; i > insertAt; --i)
            candidateOrder[static_cast<size_t> (i)] = candidateOrder[static_cast<size_t> (i - 1)];

        candidateOrder[static_cast<size_t> (insertAt)] = physTrack;
        if (! groupsAreConsecutiveInOrder (candidateOrder))
            return false;

        visualOrder = candidateOrder;
        normalizeGroups();
        return true;
    }

    bool swapTracks (int visualA, int visualB)
    {
        if (visualA < 0 || visualA >= kNumTracks || visualB < 0 || visualB >= kNumTracks)
            return false;
        if (visualA == visualB)
            return false;
        auto candidateOrder = visualOrder;
        std::swap (candidateOrder[static_cast<size_t> (visualA)],
                   candidateOrder[static_cast<size_t> (visualB)]);
        if (! groupsAreConsecutiveInOrder (candidateOrder))
            return false;

        visualOrder = candidateOrder;
        normalizeGroups();
        return true;
    }

    // Move a contiguous visual range one step left or right (delta = -1 or +1)
    bool moveVisualRange (int rangeStart, int rangeEnd, int delta)
    {
        if (delta != -1 && delta != 1)
            return false;
        if (rangeStart > rangeEnd) std::swap (rangeStart, rangeEnd);
        rangeStart = juce::jlimit (0, kNumTracks - 1, rangeStart);
        rangeEnd = juce::jlimit (0, kNumTracks - 1, rangeEnd);
        if (delta == -1 && rangeStart <= 0) return false;
        if (delta == +1 && rangeEnd >= kNumTracks - 1) return false;

        auto candidateOrder = visualOrder;

        if (delta == -1)
        {
            // Swap the element just before the range with each element stepping right
            int saved = candidateOrder[static_cast<size_t> (rangeStart - 1)];
            for (int i = rangeStart - 1; i < rangeEnd; ++i)
                candidateOrder[static_cast<size_t> (i)] = candidateOrder[static_cast<size_t> (i + 1)];
            candidateOrder[static_cast<size_t> (rangeEnd)] = saved;
        }
        else
        {
            // Swap the element just after the range with each element stepping left
            int saved = candidateOrder[static_cast<size_t> (rangeEnd + 1)];
            for (int i = rangeEnd + 1; i > rangeStart; --i)
                candidateOrder[static_cast<size_t> (i)] = candidateOrder[static_cast<size_t> (i - 1)];
            candidateOrder[static_cast<size_t> (rangeStart)] = saved;
        }

        if (! groupsAreConsecutiveInOrder (candidateOrder))
            return false;

        visualOrder = candidateOrder;
        normalizeGroups();
        return true;
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

    // Derived display names are transient UI hints. They are not serialized and
    // do not participate in undoable layout snapshots.
    const juce::String& getTrackAutoName (int physicalTrack) const
    {
        return trackAutoNames[static_cast<size_t> (juce::jlimit (0, kNumTracks - 1, physicalTrack))];
    }

    void setTrackAutoName (int physicalTrack, const juce::String& name)
    {
        trackAutoNames[static_cast<size_t> (juce::jlimit (0, kNumTracks - 1, physicalTrack))] = name;
    }

    void setTrackAutoNames (const std::array<juce::String, kNumTracks>& names)
    {
        trackAutoNames = names;
    }

    juce::String getTrackDisplayName (int physicalTrack) const
    {
        const auto idx = static_cast<size_t> (juce::jlimit (0, kNumTracks - 1, physicalTrack));
        if (trackNames[idx].isNotEmpty())
            return trackNames[idx];
        if (trackAutoNames[idx].isNotEmpty())
            return trackAutoNames[idx];
        return juce::String::formatted ("T%02d", static_cast<int> (idx) + 1);
    }

    // Per-track note mode. Notes sustain until the next trigger; Kill hard-cuts
    // the previous sample on a new sample note, while Release uses note-off.
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
        if (! canCreateGroup())
            return -1;

        if (visualStart > visualEnd)
            std::swap (visualStart, visualEnd);

        visualStart = juce::jlimit (0, kNumTracks - 1, visualStart);
        visualEnd = juce::jlimit (0, kNumTracks - 1, visualEnd);

        TrackGroup group;
        group.name = name;
        group.colour = getGroupPaletteColour (static_cast<int> (groups.size()));

        for (int v = visualStart; v <= visualEnd; ++v)
        {
            int phys = visualOrder[static_cast<size_t> (v)];
            if (std::find (group.trackIndices.begin(), group.trackIndices.end(), phys) == group.trackIndices.end())
                group.trackIndices.push_back (phys);
        }

        if (group.trackIndices.empty())
            return -1;

        groups.push_back (std::move (group));
        normalizeGroups();
        return static_cast<int> (groups.size()) - 1;
    }

    bool canCreateGroup() const { return static_cast<int> (groups.size()) < kMaxTrackGroups; }

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
    bool addGroup (TrackGroup group)
    {
        if (! canCreateGroup())
            return false;
        normalizeGroupForOrder (group, visualOrder);
        if (group.trackIndices.empty() || ! groupIsConsecutiveInOrder (group, visualOrder))
            return false;
        groups.push_back (std::move (group));
        normalizeGroups();
        return true;
    }

    const std::array<int, kNumTracks>& getVisualOrder() const { return visualOrder; }

    int getTrackLaneCount() const { return trackLaneCount; }

    void setTrackLaneCount (int count)
    {
        trackLaneCount = juce::jlimit (1, kNumTracks, count);
    }

    void addTrackLane()
    {
        setTrackLaneCount (trackLaneCount + 1);
    }

    void removeTrackLane()
    {
        setTrackLaneCount (trackLaneCount - 1);
    }

    bool setVisualOrder (const std::array<int, kNumTracks>& order)
    {
        if (! isValidVisualOrder (order))
            return false;
        if (! groupsAreConsecutiveInOrder (order))
            return false;

        visualOrder = order;
        normalizeGroups();
        return true;
    }

    void normalizeGroups()
    {
        if (groups.size() > static_cast<size_t> (kMaxTrackGroups))
            groups.resize (static_cast<size_t> (kMaxTrackGroups));

        for (auto& group : groups)
        {
            normalizeGroupForOrder (group, visualOrder);
        }

        groups.erase (std::remove_if (groups.begin(), groups.end(),
                                      [] (const TrackGroup& group)
                                      {
                                          return group.trackIndices.empty();
                                      }),
                      groups.end());
    }

    // Per-track note lane count (minimum 1, maximum 8)
    int getTrackNoteLaneCount (int physicalTrack) const
    {
        return trackNoteLaneCounts[static_cast<size_t> (juce::jlimit (0, kNumTracks - 1, physicalTrack))];
    }

    void setTrackNoteLaneCount (int physicalTrack, int count)
    {
        trackNoteLaneCounts[static_cast<size_t> (juce::jlimit (0, kNumTracks - 1, physicalTrack))]
            = juce::jlimit (1, 8, count);
    }

    void addNoteLane (int physicalTrack)
    {
        auto& c = trackNoteLaneCounts[static_cast<size_t> (juce::jlimit (0, kNumTracks - 1, physicalTrack))];
        if (c < 8) ++c;
    }

    void removeNoteLane (int physicalTrack)
    {
        auto& c = trackNoteLaneCounts[static_cast<size_t> (juce::jlimit (0, kNumTracks - 1, physicalTrack))];
        if (c > 1) --c;
    }

    const std::array<int, kNumTracks>& getTrackNoteLaneCounts() const { return trackNoteLaneCounts; }

    // Per-track FX lane count (minimum 1)
    int getTrackFxLaneCount (int physicalTrack) const
    {
        return trackFxLaneCounts[static_cast<size_t> (juce::jlimit (0, kNumTracks - 1, physicalTrack))];
    }

    void setTrackFxLaneCount (int physicalTrack, int count)
    {
        trackFxLaneCounts[static_cast<size_t> (juce::jlimit (0, kNumTracks - 1, physicalTrack))]
            = juce::jlimit (1, 8, count);
    }

    void addFxLane (int physicalTrack)
    {
        auto& c = trackFxLaneCounts[static_cast<size_t> (juce::jlimit (0, kNumTracks - 1, physicalTrack))];
        if (c < 8) ++c;
    }

    void removeFxLane (int physicalTrack)
    {
        auto& c = trackFxLaneCounts[static_cast<size_t> (juce::jlimit (0, kNumTracks - 1, physicalTrack))];
        if (c > 1) --c;
    }

    const std::array<int, kNumTracks>& getTrackFxLaneCounts() const { return trackFxLaneCounts; }

    // Master lane FX lane count
    int getMasterFxLaneCount() const { return masterFxLaneCount; }
    void setMasterFxLaneCount (int count) { masterFxLaneCount = juce::jlimit (1, 8, count); }

    void resetToDefault()
    {
        std::iota (visualOrder.begin(), visualOrder.end(), 0);
        groups.clear();
        for (auto& n : trackNames) n.clear();
        for (auto& n : trackAutoNames) n.clear();
        for (auto& m : trackNoteModes) m = NoteMode::Kill;
        trackFxLaneCounts.fill (1);
        trackNoteLaneCounts.fill (1);
        masterFxLaneCount = 1;
        trackLaneCount = kDefaultTrackLaneCount;
    }

    void clear() { resetToDefault(); }

    Snapshot createSnapshot() const
    {
        Snapshot s;
        s.visualOrder = visualOrder;
        s.groups = groups;
        s.trackNames = trackNames;
        s.trackNoteModes = trackNoteModes;
        s.trackFxLaneCounts = trackFxLaneCounts;
        s.trackNoteLaneCounts = trackNoteLaneCounts;
        s.masterFxLaneCount = masterFxLaneCount;
        s.trackLaneCount = trackLaneCount;
        return s;
    }

    void applySnapshot (const Snapshot& snapshot)
    {
        visualOrder = isValidVisualOrder (snapshot.visualOrder) ? snapshot.visualOrder : defaultVisualOrder();
        groups = snapshot.groups;
        trackNames = snapshot.trackNames;
        trackNoteModes = snapshot.trackNoteModes;
        trackFxLaneCounts = snapshot.trackFxLaneCounts;
        trackNoteLaneCounts = snapshot.trackNoteLaneCounts;
        masterFxLaneCount = juce::jlimit (1, 8, snapshot.masterFxLaneCount);
        trackLaneCount = juce::jlimit (1, kNumTracks, snapshot.trackLaneCount);
        normalizeGroups();
    }

    static bool snapshotsEqual (const Snapshot& a, const Snapshot& b)
    {
        return a.visualOrder == b.visualOrder
            && a.groups == b.groups
            && a.trackNames == b.trackNames
            && a.trackNoteModes == b.trackNoteModes
            && a.trackFxLaneCounts == b.trackFxLaneCounts
            && a.trackNoteLaneCounts == b.trackNoteLaneCounts
            && a.masterFxLaneCount == b.masterFxLaneCount
            && a.trackLaneCount == b.trackLaneCount;
    }

private:
    static std::array<int, kNumTracks> defaultVisualOrder()
    {
        std::array<int, kNumTracks> order {};
        std::iota (order.begin(), order.end(), 0);
        return order;
    }

    static bool isValidVisualOrder (const std::array<int, kNumTracks>& order)
    {
        std::array<bool, kNumTracks> seen {};
        for (auto phys : order)
        {
            if (phys < 0 || phys >= kNumTracks)
                return false;
            if (seen[static_cast<size_t> (phys)])
                return false;
            seen[static_cast<size_t> (phys)] = true;
        }

        return true;
    }

    static void normalizeGroupForOrder (TrackGroup& group, const std::array<int, kNumTracks>& order)
    {
        std::array<bool, kNumTracks> seen {};
        std::vector<int> normalized;
        normalized.reserve (group.trackIndices.size());

        for (int visual = 0; visual < kNumTracks; ++visual)
        {
            int phys = order[static_cast<size_t> (visual)];
            if (std::find (group.trackIndices.begin(), group.trackIndices.end(), phys) == group.trackIndices.end())
                continue;

            if (seen[static_cast<size_t> (phys)])
                continue;

            normalized.push_back (phys);
            seen[static_cast<size_t> (phys)] = true;
        }

        group.trackIndices = std::move (normalized);
    }

    static bool groupIsConsecutiveInOrder (const TrackGroup& group, const std::array<int, kNumTracks>& order)
    {
        if (group.trackIndices.size() <= 1)
            return true;

        int firstVisual = kNumTracks;
        int lastVisual = -1;
        int memberCount = 0;

        for (int visual = 0; visual < kNumTracks; ++visual)
        {
            int phys = order[static_cast<size_t> (visual)];
            if (std::find (group.trackIndices.begin(), group.trackIndices.end(), phys) == group.trackIndices.end())
                continue;

            firstVisual = juce::jmin (firstVisual, visual);
            lastVisual = visual;
            ++memberCount;
        }

        if (memberCount != static_cast<int> (group.trackIndices.size()))
            return false;

        return lastVisual - firstVisual + 1 == memberCount;
    }

    bool groupsAreConsecutiveInOrder (const std::array<int, kNumTracks>& order) const
    {
        for (const auto& group : groups)
        {
            TrackGroup normalized = group;
            normalizeGroupForOrder (normalized, order);
            if (! groupIsConsecutiveInOrder (normalized, order))
                return false;
        }

        return true;
    }

    std::array<int, kNumTracks> visualOrder {};
    std::vector<TrackGroup> groups;
    std::array<juce::String, kNumTracks> trackNames;
    std::array<juce::String, kNumTracks> trackAutoNames;
    std::array<NoteMode, kNumTracks> trackNoteModes {};
    std::array<int, kNumTracks> trackFxLaneCounts {};
    std::array<int, kNumTracks> trackNoteLaneCounts {};
    int masterFxLaneCount = 1;
    int trackLaneCount = kDefaultTrackLaneCount;
};

class TrackLayoutEditAction : public juce::UndoableAction
{
public:
    TrackLayoutEditAction (TrackLayout& target, TrackLayout::Snapshot oldSnapshot, TrackLayout::Snapshot newSnapshot)
        : layout (target), before (std::move (oldSnapshot)), after (std::move (newSnapshot))
    {
    }

    bool perform() override
    {
        layout.applySnapshot (after);
        return true;
    }

    bool undo() override
    {
        layout.applySnapshot (before);
        return true;
    }

private:
    TrackLayout& layout;
    TrackLayout::Snapshot before;
    TrackLayout::Snapshot after;
};
