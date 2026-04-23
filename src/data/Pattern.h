#pragma once

#include <array>
#include <memory>
#include <vector>
#include <juce_core/juce_core.h>
#include "PatternTypes.h"
#include "TrackerConstants.h"

struct PatternAutomationData;

struct Pattern
{
    Pattern();
    explicit Pattern (int rowCount);
    Pattern (const Pattern& other);
    Pattern& operator= (const Pattern& other);
    Pattern (Pattern&& other) noexcept;
    Pattern& operator= (Pattern&& other) noexcept;
    ~Pattern();

    int numRows = 64;
    std::vector<std::array<Cell, kNumTracks>> rows;
    juce::String name;
    std::vector<std::vector<FxSlot>> masterFxRows;

    Cell& getCell (int row, int track);
    const Cell& getCell (int row, int track) const;
    void setCell (int row, int track, const Cell& cell);
    void clear();
    void resize (int newNumRows);
    bool hasAnyData (int masterFxLaneCount = 1) const;

    FxSlot& getMasterFxSlot (int row, int lane);
    const FxSlot& getMasterFxSlot (int row, int lane) const;
    void ensureMasterFxSlots (int laneCount);

    PatternAutomationData& getAutomationData();
    const PatternAutomationData& getAutomationData() const;
    void clearAutomationData();

private:
    std::unique_ptr<PatternAutomationData> automationData;
};
