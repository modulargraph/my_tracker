#include "Pattern.h"
#include "PluginAutomationData.h"

namespace
{
PatternAutomationData& ensureAutomationData (std::unique_ptr<PatternAutomationData>& data)
{
    if (data == nullptr)
        data = std::make_unique<PatternAutomationData>();

    return *data;
}
}

Pattern::Pattern()
    : Pattern (64)
{
}

Pattern::Pattern (int rowCount)
    : numRows (rowCount),
      name ("Pattern"),
      automationData (std::make_unique<PatternAutomationData>())
{
    rows.resize (static_cast<size_t> (numRows));
    masterFxRows.resize (static_cast<size_t> (numRows), std::vector<FxSlot> (1));
}

Pattern::Pattern (const Pattern& other)
    : numRows (other.numRows),
      rows (other.rows),
      name (other.name),
      masterFxRows (other.masterFxRows),
      automationData (std::make_unique<PatternAutomationData> (other.getAutomationData()))
{
}

Pattern& Pattern::operator= (const Pattern& other)
{
    if (this == &other)
        return *this;

    numRows = other.numRows;
    rows = other.rows;
    name = other.name;
    masterFxRows = other.masterFxRows;
    ensureAutomationData (automationData) = other.getAutomationData();
    return *this;
}

Pattern::Pattern (Pattern&& other) noexcept = default;
Pattern& Pattern::operator= (Pattern&& other) noexcept = default;
Pattern::~Pattern() = default;

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
    numRows = juce::jlimit (1, 256, newNumRows);

    if (static_cast<int> (rows.size()) < numRows)
    {
        const auto oldSize = static_cast<int> (rows.size());
        rows.resize (static_cast<size_t> (numRows));

        for (int i = oldSize; i < numRows; ++i)
            rows[static_cast<size_t> (i)] = std::array<Cell, kNumTracks> {};
    }

    if (static_cast<int> (masterFxRows.size()) < numRows)
    {
        const auto laneCount = masterFxRows.empty() ? 1 : static_cast<int> (masterFxRows[0].size());
        masterFxRows.resize (static_cast<size_t> (numRows), std::vector<FxSlot> (static_cast<size_t> (laneCount)));
    }
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

PatternAutomationData& Pattern::getAutomationData()
{
    return ensureAutomationData (automationData);
}

const PatternAutomationData& Pattern::getAutomationData() const
{
    static const PatternAutomationData empty;
    return automationData != nullptr ? *automationData : empty;
}

void Pattern::clearAutomationData()
{
    ensureAutomationData (automationData) = PatternAutomationData {};
}
