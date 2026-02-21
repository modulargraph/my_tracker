#include "PatternData.h"

//==============================================================================
// Pattern
//==============================================================================

Pattern::Pattern()
    : Pattern (64)
{
}

Pattern::Pattern (int rowCount)
    : numRows (rowCount), name ("Pattern")
{
    rows.resize (static_cast<size_t> (numRows));
    masterFxRows.resize (static_cast<size_t> (numRows), std::vector<FxSlot> (1));
}

Cell& Pattern::getCell (int row, int track)
{
    jassert (row >= 0 && row < numRows);
    jassert (track >= 0 && track < kNumTracks);
    return rows[static_cast<size_t> (row)][static_cast<size_t> (track)];
}

const Cell& Pattern::getCell (int row, int track) const
{
    jassert (row >= 0 && row < numRows);
    jassert (track >= 0 && track < kNumTracks);
    return rows[static_cast<size_t> (row)][static_cast<size_t> (track)];
}

void Pattern::setCell (int row, int track, const Cell& cell)
{
    jassert (row >= 0 && row < numRows);
    jassert (track >= 0 && track < kNumTracks);
    rows[static_cast<size_t> (row)][static_cast<size_t> (track)] = cell;
}

void Pattern::clear()
{
    for (auto& row : rows)
        for (auto& cell : row)
            cell.clear();

    for (auto& mfxRow : masterFxRows)
        for (auto& slot : mfxRow)
            slot.clear();
}

void Pattern::resize (int newNumRows)
{
    int oldNumRows = numRows;
    numRows = juce::jlimit (1, 256, newNumRows);

    // Only grow the vector, never shrink it — preserves data from trimmed rows
    if (static_cast<int> (rows.size()) < numRows)
    {
        int oldSize = static_cast<int> (rows.size());
        rows.resize (static_cast<size_t> (numRows));

        // Initialize any newly created rows (beyond what was ever allocated)
        for (int i = oldSize; i < numRows; ++i)
            rows[static_cast<size_t> (i)] = std::array<Cell, kNumTracks> {};
    }

    // Grow master FX rows to match
    if (static_cast<int> (masterFxRows.size()) < numRows)
    {
        int laneCount = masterFxRows.empty() ? 1 : static_cast<int> (masterFxRows[0].size());
        masterFxRows.resize (static_cast<size_t> (numRows), std::vector<FxSlot> (static_cast<size_t> (laneCount)));
    }

    // When shrinking, numRows decreases but rows.size() stays the same.
    // Old data is preserved and will reappear if the pattern is expanded again.
    juce::ignoreUnused (oldNumRows);
}

FxSlot& Pattern::getMasterFxSlot (int row, int lane)
{
    jassert (row >= 0 && row < numRows);
    if (row < 0 || row >= static_cast<int> (masterFxRows.size()))
    {
        static FxSlot dummy;
        dummy.clear();
        return dummy;
    }
    auto& mfxRow = masterFxRows[static_cast<size_t> (row)];
    while (static_cast<int> (mfxRow.size()) <= lane)
        mfxRow.push_back ({});
    return mfxRow[static_cast<size_t> (lane)];
}

const FxSlot& Pattern::getMasterFxSlot (int row, int lane) const
{
    static const FxSlot emptySlot {};
    if (row < 0 || row >= static_cast<int> (masterFxRows.size()))
        return emptySlot;
    auto& mfxRow = masterFxRows[static_cast<size_t> (row)];
    if (lane < 0 || lane >= static_cast<int> (mfxRow.size()))
        return emptySlot;
    return mfxRow[static_cast<size_t> (lane)];
}

void Pattern::ensureMasterFxSlots (int laneCount)
{
    for (auto& mfxRow : masterFxRows)
        while (static_cast<int> (mfxRow.size()) < laneCount)
            mfxRow.push_back ({});
}

//==============================================================================
// PatternData
//==============================================================================

PatternData::PatternData()
{
    patterns.emplace_back (64);
}

Pattern& PatternData::getCurrentPattern()
{
    return patterns[static_cast<size_t> (currentPattern)];
}

const Pattern& PatternData::getCurrentPattern() const
{
    return patterns[static_cast<size_t> (currentPattern)];
}

Pattern& PatternData::getPattern (int index)
{
    jassert (index >= 0 && index < static_cast<int> (patterns.size()));
    return patterns[static_cast<size_t> (index)];
}

const Pattern& PatternData::getPattern (int index) const
{
    jassert (index >= 0 && index < static_cast<int> (patterns.size()));
    return patterns[static_cast<size_t> (index)];
}

void PatternData::setCurrentPattern (int index)
{
    if (index >= 0 && index < static_cast<int> (patterns.size()))
        currentPattern = index;
}

void PatternData::addPattern()
{
    patterns.emplace_back (64);
}

void PatternData::addPattern (int numRows)
{
    patterns.emplace_back (numRows);
}

void PatternData::duplicatePattern (int index)
{
    if (index >= 0 && index < static_cast<int> (patterns.size()))
    {
        Pattern copy = patterns[static_cast<size_t> (index)];
        copy.name = copy.name + " (copy)";
        patterns.insert (patterns.begin() + index + 1, std::move (copy));
    }
}

void PatternData::clearAllPatterns()
{
    patterns.clear();
    patterns.emplace_back (64);  // Always keep at least one pattern
    currentPattern = 0;
}

void PatternData::removePattern (int index)
{
    if (index >= 0 && index < static_cast<int> (patterns.size()) && patterns.size() > 1)
    {
        patterns.erase (patterns.begin() + index);
        if (currentPattern >= static_cast<int> (patterns.size()))
            currentPattern = static_cast<int> (patterns.size()) - 1;
    }
}

Cell& PatternData::getCell (int row, int track)
{
    return getCurrentPattern().getCell (row, track);
}

const Cell& PatternData::getCell (int row, int track) const
{
    return getCurrentPattern().getCell (row, track);
}

void PatternData::setCell (int row, int track, const Cell& cell)
{
    getCurrentPattern().setCell (row, track, cell);
}
