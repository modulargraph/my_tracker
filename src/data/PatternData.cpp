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
}

void Pattern::resize (int newNumRows)
{
    numRows = newNumRows;
    rows.resize (static_cast<size_t> (numRows));
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
