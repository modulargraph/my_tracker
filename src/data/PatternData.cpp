#include "PatternData.h"
#include "Pattern.h"

PatternData::PatternData()
{
    patterns.push_back (std::make_unique<Pattern> (64));
}

PatternData::PatternData (const PatternData& other)
    : currentPattern (other.currentPattern)
{
    patterns.reserve (other.patterns.size());
    for (const auto& pattern : other.patterns)
        patterns.push_back (std::make_unique<Pattern> (*pattern));

    if (patterns.empty())
    {
        patterns.push_back (std::make_unique<Pattern> (64));
        currentPattern = 0;
    }
}

PatternData& PatternData::operator= (const PatternData& other)
{
    if (this == &other)
        return *this;

    patterns.clear();
    patterns.reserve (other.patterns.size());
    for (const auto& pattern : other.patterns)
        patterns.push_back (std::make_unique<Pattern> (*pattern));

    if (patterns.empty())
    {
        patterns.push_back (std::make_unique<Pattern> (64));
        currentPattern = 0;
    }
    else
    {
        currentPattern = juce::jlimit (0, static_cast<int> (patterns.size()) - 1, other.currentPattern);
    }

    return *this;
}

PatternData::PatternData (PatternData&& other) noexcept = default;
PatternData& PatternData::operator= (PatternData&& other) noexcept = default;
PatternData::~PatternData() = default;

Pattern& PatternData::getCurrentPattern()
{
    return *patterns[static_cast<size_t> (currentPattern)];
}

const Pattern& PatternData::getCurrentPattern() const
{
    return *patterns[static_cast<size_t> (currentPattern)];
}

Pattern& PatternData::getPattern (int index)
{
    jassert (index >= 0 && index < static_cast<int> (patterns.size()));
    return *patterns[static_cast<size_t> (index)];
}

const Pattern& PatternData::getPattern (int index) const
{
    jassert (index >= 0 && index < static_cast<int> (patterns.size()));
    return *patterns[static_cast<size_t> (index)];
}

void PatternData::setCurrentPattern (int index)
{
    if (index >= 0 && index < static_cast<int> (patterns.size()))
        currentPattern = index;
}

void PatternData::addPattern()
{
    patterns.push_back (std::make_unique<Pattern> (64));
}

void PatternData::addPattern (int numRows)
{
    patterns.push_back (std::make_unique<Pattern> (numRows));
}

void PatternData::duplicatePattern (int index)
{
    if (index >= 0 && index < static_cast<int> (patterns.size()))
    {
        auto copy = std::make_unique<Pattern> (*patterns[static_cast<size_t> (index)]);
        copy->name = copy->name + " (copy)";
        patterns.insert (patterns.begin() + index + 1, std::move (copy));
    }
}

void PatternData::clearAllPatterns()
{
    patterns.clear();
    patterns.push_back (std::make_unique<Pattern> (64));
    currentPattern = 0;
}

void PatternData::removePattern (int index)
{
    if (index >= 0 && index < static_cast<int> (patterns.size()) && patterns.size() > 1)
    {
        patterns.erase (patterns.begin() + index);
        if (currentPattern > index)
            --currentPattern;
        else if (currentPattern >= static_cast<int> (patterns.size()))
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
