#include "TrackLayoutSerializer.h"
#include <set>

namespace TrackLayoutSerializer
{

void save (juce::ValueTree& root, const TrackLayout& trackLayout)
{
    juce::ValueTree layoutTree ("TrackLayout");

    juce::String orderStr;
    auto& order = trackLayout.getVisualOrder();
    for (int i = 0; i < kNumTracks; ++i)
    {
        if (i > 0) orderStr += ",";
        orderStr += juce::String (order[static_cast<size_t> (i)]);
    }
    juce::ValueTree voTree ("VisualOrder");
    voTree.setProperty ("values", orderStr, nullptr);
    layoutTree.addChild (voTree, -1, nullptr);

    auto& names = trackLayout.getTrackNames();
    for (int i = 0; i < kNumTracks; ++i)
    {
        if (names[static_cast<size_t> (i)].isNotEmpty())
        {
            juce::ValueTree nameTree ("TrackName");
            nameTree.setProperty ("index", i, nullptr);
            nameTree.setProperty ("name", names[static_cast<size_t> (i)], nullptr);
            layoutTree.addChild (nameTree, -1, nullptr);
        }
    }

    // Note modes (only save if any are non-default)
    {
        bool anyRelease = false;
        for (int i = 0; i < kNumTracks; ++i)
            if (trackLayout.getTrackNoteMode (i) == NoteMode::Release)
                anyRelease = true;

        if (anyRelease)
        {
            juce::String modeStr;
            for (int i = 0; i < kNumTracks; ++i)
            {
                if (i > 0) modeStr += ",";
                modeStr += juce::String (static_cast<int> (trackLayout.getTrackNoteMode (i)));
            }
            juce::ValueTree nmTree ("NoteModes");
            nmTree.setProperty ("values", modeStr, nullptr);
            layoutTree.addChild (nmTree, -1, nullptr);
        }
    }

    // FX lane counts (only save if any track has more than 1)
    {
        bool anyMultiFx = false;
        for (int i = 0; i < kNumTracks; ++i)
            if (trackLayout.getTrackFxLaneCount (i) > 1)
                anyMultiFx = true;

        if (anyMultiFx)
        {
            juce::String fxStr;
            for (int i = 0; i < kNumTracks; ++i)
            {
                if (i > 0) fxStr += ",";
                fxStr += juce::String (trackLayout.getTrackFxLaneCount (i));
            }
            juce::ValueTree fxTree ("FxLaneCounts");
            fxTree.setProperty ("values", fxStr, nullptr);
            layoutTree.addChild (fxTree, -1, nullptr);
        }
    }

    // Note lane counts (only save if any track has more than 1)
    {
        bool anyMultiNote = false;
        for (int i = 0; i < kNumTracks; ++i)
            if (trackLayout.getTrackNoteLaneCount (i) > 1)
                anyMultiNote = true;

        if (anyMultiNote)
        {
            juce::String nlStr;
            for (int i = 0; i < kNumTracks; ++i)
            {
                if (i > 0) nlStr += ",";
                nlStr += juce::String (trackLayout.getTrackNoteLaneCount (i));
            }
            juce::ValueTree nlTree ("NoteLaneCounts");
            nlTree.setProperty ("values", nlStr, nullptr);
            layoutTree.addChild (nlTree, -1, nullptr);
        }
    }

    // Master FX lane count (only save if > 1)
    if (trackLayout.getMasterFxLaneCount() > 1)
    {
        juce::ValueTree mfxTree ("MasterFxLanes");
        mfxTree.setProperty ("count", trackLayout.getMasterFxLaneCount(), nullptr);
        layoutTree.addChild (mfxTree, -1, nullptr);
    }

    for (int gi = 0; gi < trackLayout.getNumGroups(); ++gi)
    {
        auto& group = trackLayout.getGroup (gi);
        juce::ValueTree groupTree ("Group");
        groupTree.setProperty ("name", group.name, nullptr);
        groupTree.setProperty ("colour", group.colour.toString(), nullptr);

        for (auto idx : group.trackIndices)
        {
            juce::ValueTree trackTree ("Track");
            trackTree.setProperty ("index", idx, nullptr);
            groupTree.addChild (trackTree, -1, nullptr);
        }
        layoutTree.addChild (groupTree, -1, nullptr);
    }

    root.addChild (layoutTree, -1, nullptr);
}

void load (const juce::ValueTree& root, TrackLayout& trackLayout)
{
    // Track Layout (backward-compatible)
    trackLayout.resetToDefault();
    auto layoutTree = root.getChildWithName ("TrackLayout");
    if (layoutTree.isValid())
    {
        auto voTree = layoutTree.getChildWithName ("VisualOrder");
        if (voTree.isValid())
        {
            juce::String orderStr = voTree.getProperty ("values", "");
            auto tokens = juce::StringArray::fromTokens (orderStr, ",", "");
            if (tokens.size() == kNumTracks)
            {
                std::array<int, kNumTracks> order {};
                bool valid = true;
                std::set<int> seen;
                for (int i = 0; i < kNumTracks; ++i)
                {
                    int val = tokens[i].getIntValue();
                    if (val < 0 || val >= kNumTracks || seen.count (val))
                    {
                        valid = false;
                        break;
                    }
                    seen.insert (val);
                    order[static_cast<size_t> (i)] = val;
                }
                if (valid)
                    trackLayout.setVisualOrder (order);
            }
        }

        for (int i = 0; i < layoutTree.getNumChildren(); ++i)
        {
            auto nameTree = layoutTree.getChild (i);
            if (! nameTree.hasType ("TrackName")) continue;

            int idx = nameTree.getProperty ("index", -1);
            if (idx >= 0 && idx < kNumTracks)
                trackLayout.setTrackName (idx, nameTree.getProperty ("name", "").toString());
        }

        auto nmTree = layoutTree.getChildWithName ("NoteModes");
        if (nmTree.isValid())
        {
            juce::String modeStr = nmTree.getProperty ("values", "");
            auto tokens = juce::StringArray::fromTokens (modeStr, ",", "");
            if (tokens.size() == kNumTracks)
            {
                for (int i = 0; i < kNumTracks; ++i)
                    trackLayout.setTrackNoteMode (i, tokens[i].getIntValue() == 1
                                                         ? NoteMode::Release
                                                         : NoteMode::Kill);
            }
        }

        auto fxLaneTree = layoutTree.getChildWithName ("FxLaneCounts");
        if (fxLaneTree.isValid())
        {
            juce::String fxStr = fxLaneTree.getProperty ("values", "");
            auto tokens = juce::StringArray::fromTokens (fxStr, ",", "");
            if (tokens.size() == kNumTracks)
            {
                for (int i = 0; i < kNumTracks; ++i)
                    trackLayout.setTrackFxLaneCount (i, tokens[i].getIntValue());
            }
        }

        auto nlLaneTree = layoutTree.getChildWithName ("NoteLaneCounts");
        if (nlLaneTree.isValid())
        {
            juce::String nlStr = nlLaneTree.getProperty ("values", "");
            auto tokens = juce::StringArray::fromTokens (nlStr, ",", "");
            if (tokens.size() == kNumTracks)
            {
                for (int i = 0; i < kNumTracks; ++i)
                    trackLayout.setTrackNoteLaneCount (i, tokens[i].getIntValue());
            }
        }

        auto mfxTree = layoutTree.getChildWithName ("MasterFxLanes");
        if (mfxTree.isValid())
            trackLayout.setMasterFxLaneCount (mfxTree.getProperty ("count", 1));

        for (int i = 0; i < layoutTree.getNumChildren(); ++i)
        {
            auto groupTree = layoutTree.getChild (i);
            if (! groupTree.hasType ("Group")) continue;

            TrackGroup group;
            group.name = groupTree.getProperty ("name", "Group").toString();
            group.colour = juce::Colour::fromString (groupTree.getProperty ("colour", "ff5c8abf").toString());

            for (int j = 0; j < groupTree.getNumChildren(); ++j)
            {
                auto trackTree = groupTree.getChild (j);
                if (trackTree.hasType ("Track"))
                {
                    int idx = trackTree.getProperty ("index", -1);
                    if (idx >= 0 && idx < kNumTracks)
                        group.trackIndices.push_back (idx);
                }
            }

            if (! group.trackIndices.empty())
                trackLayout.addGroup (std::move (group));
        }
    }
}

} // namespace TrackLayoutSerializer
