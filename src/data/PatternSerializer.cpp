#include "PatternSerializer.h"

juce::ValueTree PatternSerializer::patternToValueTree (const Pattern& pattern, int /*index*/)
{
    juce::ValueTree patTree ("Pattern");
    patTree.setProperty ("name", pattern.name, nullptr);
    patTree.setProperty ("numRows", pattern.numRows, nullptr);

    for (int r = 0; r < pattern.numRows; ++r)
    {
        bool hasData = false;
        for (int t = 0; t < kNumTracks; ++t)
        {
            if (! pattern.getCell (r, t).isEmpty())
            {
                hasData = true;
                break;
            }
        }

        // Check master lane too
        if (! hasData && r < static_cast<int> (pattern.masterFxRows.size()))
        {
            for (auto& slot : pattern.masterFxRows[static_cast<size_t> (r)])
                if (! slot.isEmpty()) { hasData = true; break; }
        }

        if (! hasData) continue;

        juce::ValueTree rowTree ("Row");
        rowTree.setProperty ("index", r, nullptr);

        for (int t = 0; t < kNumTracks; ++t)
        {
            const auto& cell = pattern.getCell (r, t);
            if (cell.isEmpty()) continue;

            juce::ValueTree cellTree ("Cell");
            cellTree.setProperty ("track", t, nullptr);
            cellTree.setProperty ("note", cell.note, nullptr);
            cellTree.setProperty ("inst", cell.instrument, nullptr);
            cellTree.setProperty ("vol", cell.volume, nullptr);

            // Save extra note lanes (lane 1+)
            for (int nl = 0; nl < static_cast<int> (cell.extraNoteLanes.size()); ++nl)
            {
                const auto& slot = cell.extraNoteLanes[static_cast<size_t> (nl)];
                if (slot.isEmpty()) continue;

                juce::ValueTree nlTree ("NoteLane");
                nlTree.setProperty ("lane", nl + 1, nullptr);
                nlTree.setProperty ("note", slot.note, nullptr);
                nlTree.setProperty ("inst", slot.instrument, nullptr);
                nlTree.setProperty ("vol", slot.volume, nullptr);
                cellTree.addChild (nlTree, -1, nullptr);
            }

            // Save first FX slot
            if (cell.getNumFxSlots() > 0)
            {
                const auto& slot0 = cell.getFxSlot (0);
                auto letter = slot0.getCommandLetter();
                if (letter != '\0')
                {
                    cellTree.setProperty ("fxc", juce::String::charToString (letter), nullptr);
                    cellTree.setProperty ("fxp", slot0.fxParam, nullptr);
                }
            }

            // Save additional FX slots (index 1+)
            for (int fxi = 1; fxi < cell.getNumFxSlots(); ++fxi)
            {
                const auto& slot = cell.getFxSlot (fxi);
                auto letter = slot.getCommandLetter();
                if (letter == '\0') continue;

                juce::ValueTree fxTree ("FxSlot");
                fxTree.setProperty ("lane", fxi, nullptr);
                fxTree.setProperty ("fxp", slot.fxParam, nullptr);
                fxTree.setProperty ("fxc", juce::String::charToString (letter), nullptr);
                cellTree.addChild (fxTree, -1, nullptr);
            }

            rowTree.addChild (cellTree, -1, nullptr);
        }

        // Save master FX slots for this row
        if (r < static_cast<int> (pattern.masterFxRows.size()))
        {
            auto& mfxRow = pattern.masterFxRows[static_cast<size_t> (r)];
            for (int lane = 0; lane < static_cast<int> (mfxRow.size()); ++lane)
            {
                auto& slot = mfxRow[static_cast<size_t> (lane)];
                auto letter = slot.getCommandLetter();
                if (letter == '\0') continue;

                juce::ValueTree mfxTree ("MasterFx");
                mfxTree.setProperty ("lane", lane, nullptr);
                mfxTree.setProperty ("fxp", slot.fxParam, nullptr);
                mfxTree.setProperty ("fxc", juce::String::charToString (letter), nullptr);
                rowTree.addChild (mfxTree, -1, nullptr);
            }
        }

        patTree.addChild (rowTree, -1, nullptr);
    }

    // Automation data (Phase 5)
    if (! pattern.automationData.isEmpty())
    {
        juce::ValueTree autoTree ("Automation");
        for (const auto& lane : pattern.automationData.lanes)
        {
            if (lane.isEmpty())
                continue;

            juce::ValueTree laneTree ("Lane");
            laneTree.setProperty ("pluginId", lane.pluginId, nullptr);
            laneTree.setProperty ("paramId", lane.parameterId, nullptr);
            laneTree.setProperty ("track", lane.owningTrack, nullptr);

            for (const auto& point : lane.points)
            {
                juce::ValueTree pointTree ("Point");
                pointTree.setProperty ("row", point.row, nullptr);
                pointTree.setProperty ("value", static_cast<double> (point.value), nullptr);
                pointTree.setProperty ("curve", static_cast<int> (point.curveType), nullptr);
                laneTree.addChild (pointTree, -1, nullptr);
            }

            autoTree.addChild (laneTree, -1, nullptr);
        }
        patTree.addChild (autoTree, -1, nullptr);
    }

    return patTree;
}

void PatternSerializer::valueTreeToPattern (const juce::ValueTree& tree, Pattern& pattern, int /*version*/)
{
    pattern.name = tree.getProperty ("name", "Pattern").toString();
    int numRows = tree.getProperty ("numRows", 64);
    pattern.resize (numRows);
    pattern.clear();
    pattern.ensureMasterFxSlots (1);

    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto rowTree = tree.getChild (i);
        if (! rowTree.hasType ("Row")) continue;

        int row = rowTree.getProperty ("index", -1);
        if (row < 0 || row >= numRows) continue;

        for (int j = 0; j < rowTree.getNumChildren(); ++j)
        {
            auto cellTree = rowTree.getChild (j);
            if (! cellTree.hasType ("Cell")) continue;

            int track = cellTree.getProperty ("track", -1);
            if (track < 0 || track >= kNumTracks) continue;

            Cell cell;
            cell.note = cellTree.getProperty ("note", -1);
            cell.instrument = cellTree.getProperty ("inst", -1);
            cell.volume = cellTree.getProperty ("vol", -1);

            // Load extra note lanes and FX slots from child nodes
            for (int ci = 0; ci < cellTree.getNumChildren(); ++ci)
            {
                auto childTree = cellTree.getChild (ci);

                if (childTree.hasType ("NoteLane"))
                {
                    int lane = childTree.getProperty ("lane", -1);
                    if (lane < 1) continue;
                    NoteSlot slot;
                    slot.note = childTree.getProperty ("note", -1);
                    slot.instrument = childTree.getProperty ("inst", -1);
                    slot.volume = childTree.getProperty ("vol", -1);
                    cell.setNoteLane (lane, slot);
                }
                else if (childTree.hasType ("FxSlot"))
                {
                    int lane = childTree.getProperty ("lane", -1);
                    if (lane < 1) continue;
                    auto& slot = cell.getFxSlot (lane);
                    int fxp = childTree.getProperty ("fxp", 0);
                    auto fxToken = childTree.getProperty ("fxc", "").toString();
                    if (fxToken.isNotEmpty())
                        slot.setSymbolicCommand (static_cast<char> (fxToken[0]), fxp);
                }
            }

            // Load first FX slot (inline on Cell node)
            int fxp0 = cellTree.getProperty ("fxp", 0);
            auto fxToken0 = cellTree.getProperty ("fxc", "").toString();
            auto& firstSlot = cell.getFxSlot (0);
            if (fxToken0.isNotEmpty())
                firstSlot.setSymbolicCommand (static_cast<char> (fxToken0[0]), fxp0);

            pattern.setCell (row, track, cell);
        }

        for (int j = 0; j < rowTree.getNumChildren(); ++j)
        {
            auto mfxTree = rowTree.getChild (j);
            if (! mfxTree.hasType ("MasterFx")) continue;

            int lane = mfxTree.getProperty ("lane", -1);
            if (lane < 0) continue;

            int fxp = mfxTree.getProperty ("fxp", 0);
            auto fxToken = mfxTree.getProperty ("fxc", "").toString();

            auto& slot = pattern.getMasterFxSlot (row, lane);
            if (fxToken.isNotEmpty())
                slot.setSymbolicCommand (static_cast<char> (fxToken[0]), fxp);
        }
    }

    // Automation data (Phase 5)
    pattern.automationData = PatternAutomationData {};
    auto autoTree = tree.getChildWithName ("Automation");
    if (autoTree.isValid())
    {
        for (int i = 0; i < autoTree.getNumChildren(); ++i)
        {
            auto laneTree = autoTree.getChild (i);
            if (! laneTree.hasType ("Lane"))
                continue;

            AutomationLane lane;
            lane.pluginId = laneTree.getProperty ("pluginId", "").toString();
            lane.parameterId = laneTree.getProperty ("paramId", -1);
            lane.owningTrack = laneTree.getProperty ("track", -1);

            for (int pi = 0; pi < laneTree.getNumChildren(); ++pi)
            {
                auto pointTree = laneTree.getChild (pi);
                if (! pointTree.hasType ("Point"))
                    continue;

                AutomationPoint point;
                point.row = pointTree.getProperty ("row", 0);
                point.value = static_cast<float> (static_cast<double> (pointTree.getProperty ("value", 0.5)));
                point.curveType = static_cast<AutomationCurveType> (
                    static_cast<int> (pointTree.getProperty ("curve", 0)));
                lane.points.push_back (point);
            }

            lane.sortPoints();
            pattern.automationData.lanes.push_back (std::move (lane));
        }
    }
}

void PatternSerializer::saveAllPatterns (juce::ValueTree& root, const PatternData& patternData)
{
    juce::ValueTree patterns ("Patterns");
    for (int i = 0; i < patternData.getNumPatterns(); ++i)
        patterns.addChild (patternToValueTree (patternData.getPattern (i), i), -1, nullptr);
    root.addChild (patterns, -1, nullptr);
}

void PatternSerializer::loadAllPatterns (const juce::ValueTree& root, PatternData& patternData,
                                          const juce::ValueTree& settings, int version, int masterFxLaneCount)
{
    patternData.clearAllPatterns();
    auto patterns = root.getChildWithName ("Patterns");
    if (patterns.isValid() && patterns.getNumChildren() > 0)
    {
        auto firstPatTree = patterns.getChild (0);
        valueTreeToPattern (firstPatTree, patternData.getPattern (0), version);
        patternData.getPattern (0).ensureMasterFxSlots (masterFxLaneCount);

        for (int i = 1; i < patterns.getNumChildren(); ++i)
        {
            auto patTree = patterns.getChild (i);
            int numRows = patTree.getProperty ("numRows", 64);
            patternData.addPattern (numRows);
            auto& pat = patternData.getPattern (patternData.getNumPatterns() - 1);
            valueTreeToPattern (patTree, pat, version);
            pat.ensureMasterFxSlots (masterFxLaneCount);
        }
    }

    int currentPat = settings.isValid() ? static_cast<int> (settings.getProperty ("currentPattern", 0)) : 0;
    patternData.setCurrentPattern (juce::jlimit (0, patternData.getNumPatterns() - 1, currentPat));
}
