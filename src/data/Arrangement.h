#pragma once

#include <JuceHeader.h>
#include <vector>

struct ArrangementEntry
{
    int patternIndex = 0;
    int repeats = 1;
};

class Arrangement
{
public:
    Arrangement() = default;

    int getNumEntries() const { return static_cast<int> (entries.size()); }

    const ArrangementEntry& getEntry (int index) const
    {
        jassert (index >= 0 && index < getNumEntries());
        return entries[static_cast<size_t> (index)];
    }

    ArrangementEntry& getEntry (int index)
    {
        jassert (index >= 0 && index < getNumEntries());
        return entries[static_cast<size_t> (index)];
    }

    void addEntry (int patternIndex, int repeats = 1)
    {
        entries.push_back ({ patternIndex, repeats });
    }

    void insertEntry (int position, int patternIndex, int repeats = 1)
    {
        position = juce::jlimit (0, getNumEntries(), position);
        entries.insert (entries.begin() + position, { patternIndex, repeats });
    }

    void removeEntry (int index)
    {
        if (index >= 0 && index < getNumEntries())
            entries.erase (entries.begin() + index);
    }

    void moveEntryUp (int index)
    {
        if (index > 0 && index < getNumEntries())
            std::swap (entries[static_cast<size_t> (index)], entries[static_cast<size_t> (index - 1)]);
    }

    void moveEntryDown (int index)
    {
        if (index >= 0 && index < getNumEntries() - 1)
            std::swap (entries[static_cast<size_t> (index)], entries[static_cast<size_t> (index + 1)]);
    }

    void clear() { entries.clear(); }

    const std::vector<ArrangementEntry>& getEntries() const { return entries; }

    // Keep arrangement indices coherent after a pattern deletion.
    // Entries pointing past the new pattern range are clamped.
    void remapAfterPatternRemoved (int removedPatternIndex, int newPatternCount)
    {
        if (newPatternCount <= 0)
            return;

        for (auto& e : entries)
        {
            if (e.patternIndex > removedPatternIndex)
                --e.patternIndex;

            if (e.patternIndex >= newPatternCount)
                e.patternIndex = newPatternCount - 1;

            if (e.patternIndex < 0)
                e.patternIndex = 0;
        }
    }

private:
    std::vector<ArrangementEntry> entries;
};
