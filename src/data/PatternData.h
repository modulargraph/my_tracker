#pragma once

#include <memory>
#include <vector>

struct Cell;
struct Pattern;

class PatternData
{
public:
    PatternData();
    PatternData (const PatternData& other);
    PatternData& operator= (const PatternData& other);
    PatternData (PatternData&& other) noexcept;
    PatternData& operator= (PatternData&& other) noexcept;
    ~PatternData();

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
    std::vector<std::unique_ptr<Pattern>> patterns;
    int currentPattern = 0;
};
