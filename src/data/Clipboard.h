#pragma once

#include "PatternData.h"
#include <vector>
#include <array>

struct ClipboardData
{
    int numRows = 0;
    int numTracks = 0;
    std::vector<std::vector<Cell>> cells; // [row][track]

    bool isEmpty() const { return numRows == 0 || numTracks == 0; }

    void copyFromPattern (const Pattern& pat, int startRow, int endRow, int startTrack, int endTrack)
    {
        numRows = endRow - startRow + 1;
        numTracks = endTrack - startTrack + 1;
        cells.resize (static_cast<size_t> (numRows));

        for (int r = 0; r < numRows; ++r)
        {
            cells[static_cast<size_t> (r)].resize (static_cast<size_t> (numTracks));
            for (int t = 0; t < numTracks; ++t)
                cells[static_cast<size_t> (r)][static_cast<size_t> (t)] = pat.getCell (startRow + r, startTrack + t);
        }
    }

    void pasteToPattern (Pattern& pat, int destRow, int destTrack) const
    {
        for (int r = 0; r < numRows; ++r)
        {
            int row = destRow + r;
            if (row >= pat.numRows) break;
            for (int t = 0; t < numTracks; ++t)
            {
                int track = destTrack + t;
                if (track >= kNumTracks) break;
                pat.setCell (row, track, cells[static_cast<size_t> (r)][static_cast<size_t> (t)]);
            }
        }
    }
};

// Singleton clipboard
inline ClipboardData& getClipboard()
{
    static ClipboardData instance;
    return instance;
}

//==============================================================================
// Undo actions
//==============================================================================

class CellEditAction : public juce::UndoableAction
{
public:
    CellEditAction (PatternData& data, int patternIndex, int row, int track, const Cell& newCell)
        : patternData (data), patIdx (patternIndex), r (row), t (track), newValue (newCell)
    {
        oldValue = patternData.getPattern (patIdx).getCell (r, t);
    }

    bool perform() override
    {
        if (patIdx < patternData.getNumPatterns())
            patternData.getPattern (patIdx).setCell (r, t, newValue);
        return true;
    }

    bool undo() override
    {
        if (patIdx < patternData.getNumPatterns())
            patternData.getPattern (patIdx).setCell (r, t, oldValue);
        return true;
    }

private:
    PatternData& patternData;
    int patIdx, r, t;
    Cell oldValue, newValue;
};

class MultiCellEditAction : public juce::UndoableAction
{
public:
    struct CellRecord { int row; int track; Cell oldCell; Cell newCell; };
    struct MasterFxRecord { int row; int lane; FxSlot oldSlot; FxSlot newSlot; };

    MultiCellEditAction (PatternData& data, int patternIndex, std::vector<CellRecord> records)
        : patternData (data), patIdx (patternIndex), cells (std::move (records)) {}

    MultiCellEditAction (PatternData& data, int patternIndex,
                         std::vector<CellRecord> cellRecords,
                         std::vector<MasterFxRecord> masterRecords)
        : patternData (data), patIdx (patternIndex),
          cells (std::move (cellRecords)),
          masterFx (std::move (masterRecords)) {}

    bool perform() override
    {
        if (patIdx >= 0 && patIdx < patternData.getNumPatterns())
        {
            auto& pat = patternData.getPattern (patIdx);
            for (auto& c : cells)
            {
                if (c.row >= 0 && c.row < pat.numRows && c.track >= 0 && c.track < kNumTracks)
                    pat.setCell (c.row, c.track, c.newCell);
            }

            for (auto& m : masterFx)
            {
                if (m.row < 0 || m.row >= pat.numRows || m.lane < 0)
                    continue;
                pat.ensureMasterFxSlots (m.lane + 1);
                pat.getMasterFxSlot (m.row, m.lane) = m.newSlot;
            }
        }
        return true;
    }

    bool undo() override
    {
        if (patIdx >= 0 && patIdx < patternData.getNumPatterns())
        {
            auto& pat = patternData.getPattern (patIdx);
            for (auto& c : cells)
            {
                if (c.row >= 0 && c.row < pat.numRows && c.track >= 0 && c.track < kNumTracks)
                    pat.setCell (c.row, c.track, c.oldCell);
            }

            for (auto& m : masterFx)
            {
                if (m.row < 0 || m.row >= pat.numRows || m.lane < 0)
                    continue;
                pat.ensureMasterFxSlots (m.lane + 1);
                pat.getMasterFxSlot (m.row, m.lane) = m.oldSlot;
            }
        }
        return true;
    }

private:
    PatternData& patternData;
    int patIdx;
    std::vector<CellRecord> cells;
    std::vector<MasterFxRecord> masterFx;
};
