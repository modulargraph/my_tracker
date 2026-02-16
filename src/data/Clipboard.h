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
    CellEditAction (Pattern& pat, int row, int track, const Cell& newCell)
        : pattern (pat), r (row), t (track), newValue (newCell)
    {
        oldValue = pattern.getCell (r, t);
    }

    bool perform() override
    {
        pattern.setCell (r, t, newValue);
        return true;
    }

    bool undo() override
    {
        pattern.setCell (r, t, oldValue);
        return true;
    }

private:
    Pattern& pattern;
    int r, t;
    Cell oldValue, newValue;
};

class MultiCellEditAction : public juce::UndoableAction
{
public:
    struct CellRecord { int row; int track; Cell oldCell; Cell newCell; };

    MultiCellEditAction (Pattern& pat, std::vector<CellRecord> records)
        : pattern (pat), cells (std::move (records)) {}

    bool perform() override
    {
        for (auto& c : cells)
            pattern.setCell (c.row, c.track, c.newCell);
        return true;
    }

    bool undo() override
    {
        for (auto& c : cells)
            pattern.setCell (c.row, c.track, c.oldCell);
        return true;
    }

private:
    Pattern& pattern;
    std::vector<CellRecord> cells;
};
