#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>

constexpr int kNumTracks = 16;

struct Cell
{
    int note = -1;        // MIDI note (-1 = empty, 0-127 = note)
    int instrument = -1;  // Instrument/sample index (-1 = none)
    int volume = -1;      // Volume (-1 = default, 0-127)
    int fx = 0;           // Effect command (0 = none)
    int fxParam = 0;      // Effect parameter

    bool isEmpty() const { return note == -1; }
    void clear() { note = -1; instrument = -1; volume = -1; fx = 0; fxParam = 0; }
};

struct Pattern
{
    int numRows = 64;
    std::vector<std::array<Cell, kNumTracks>> rows;
    juce::String name;

    Pattern();
    explicit Pattern (int rowCount);

    Cell& getCell (int row, int track);
    const Cell& getCell (int row, int track) const;
    void setCell (int row, int track, const Cell& cell);
    void clear();
    void resize (int newNumRows);
};

class PatternData
{
public:
    PatternData();

    Pattern& getCurrentPattern();
    const Pattern& getCurrentPattern() const;

    Pattern& getPattern (int index);
    const Pattern& getPattern (int index) const;

    int getCurrentPatternIndex() const { return currentPattern; }
    void setCurrentPattern (int index);

    int getNumPatterns() const { return static_cast<int> (patterns.size()); }
    void addPattern();
    void addPattern (int numRows);
    void duplicatePattern (int index);
    void removePattern (int index);
    void clearAllPatterns();

    Cell& getCell (int row, int track);
    const Cell& getCell (int row, int track) const;
    void setCell (int row, int track, const Cell& cell);

private:
    std::vector<Pattern> patterns;
    int currentPattern = 0;
};
