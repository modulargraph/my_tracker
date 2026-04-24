#pragma once

#include <optional>

#include "Clipboard.h"

namespace PatternEditUtils
{
inline bool sameFxSlot (const FxSlot& a, const FxSlot& b)
{
    return a.fx == b.fx && a.fxParam == b.fxParam && a.fxCommand == b.fxCommand;
}

inline bool sameNoteSlot (const NoteSlot& a, const NoteSlot& b)
{
    return a.note == b.note && a.instrument == b.instrument && a.volume == b.volume;
}

inline bool sameCell (const Cell& a, const Cell& b)
{
    if (a.note != b.note || a.instrument != b.instrument || a.volume != b.volume)
        return false;
    if (a.extraNoteLanes.size() != b.extraNoteLanes.size())
        return false;

    for (size_t i = 0; i < a.extraNoteLanes.size(); ++i)
    {
        if (! sameNoteSlot (a.extraNoteLanes[i], b.extraNoteLanes[i]))
            return false;
    }

    if (a.fxSlots.size() != b.fxSlots.size())
        return false;

    for (size_t i = 0; i < a.fxSlots.size(); ++i)
    {
        if (! sameFxSlot (a.fxSlots[i], b.fxSlots[i]))
            return false;
    }

    return true;
}

inline std::optional<int> findInstrumentForLastNoteAbove (const Pattern& pattern,
                                                          int row,
                                                          int track,
                                                          int noteLane)
{
    if (track < 0 || track >= kNumTracks || row <= 0 || pattern.numRows <= 0)
        return std::nullopt;

    const int firstRowAbove = juce::jmin (row, pattern.numRows) - 1;
    const int lane = juce::jmax (0, noteLane);
    for (int r = firstRowAbove; r >= 0; --r)
    {
        auto slot = pattern.getCell (r, track).getNoteLane (lane);
        if (slot.note >= 0 && slot.note <= 127 && slot.instrument >= 0)
            return juce::jlimit (0, 255, slot.instrument);
    }

    return std::nullopt;
}

inline int resolveCursorInstrument (const Pattern& pattern,
                                    int row,
                                    int track,
                                    int noteLane,
                                    int currentInstrument,
                                    bool inferFromLastNoteAbove)
{
    const int fallbackInstrument = juce::jlimit (0, 255, currentInstrument);
    if (track < 0 || track >= kNumTracks || row < 0 || row >= pattern.numRows)
        return fallbackInstrument;

    auto slot = pattern.getCell (row, track).getNoteLane (juce::jmax (0, noteLane));
    if (slot.instrument >= 0)
        return juce::jlimit (0, 255, slot.instrument);

    if (inferFromLastNoteAbove)
    {
        if (auto instrument = findInstrumentForLastNoteAbove (pattern, row, track, noteLane))
            return *instrument;
    }

    return fallbackInstrument;
}

inline bool applyPatternEdit (PatternData& patternData,
                              juce::UndoManager* undoManager,
                              int patternIndex,
                              std::vector<MultiCellEditAction::CellRecord> cellRecords,
                              std::vector<MultiCellEditAction::MasterFxRecord> masterFxRecords)
{
    if (patternIndex < 0 || patternIndex >= patternData.getNumPatterns())
        return false;

    if (cellRecords.empty() && masterFxRecords.empty())
        return false;

    if (undoManager != nullptr)
    {
        undoManager->perform (new MultiCellEditAction (patternData, patternIndex,
                                                       std::move (cellRecords),
                                                       std::move (masterFxRecords)));
        return true;
    }

    auto& pat = patternData.getPattern (patternIndex);
    for (auto& rec : cellRecords)
    {
        if (rec.row >= 0 && rec.row < pat.numRows && rec.track >= 0 && rec.track < kNumTracks)
            pat.setCell (rec.row, rec.track, rec.newCell);
    }

    for (auto& rec : masterFxRecords)
    {
        if (rec.row < 0 || rec.row >= pat.numRows || rec.lane < 0)
            continue;
        pat.ensureMasterFxSlots (rec.lane + 1);
        pat.getMasterFxSlot (rec.row, rec.lane) = rec.newSlot;
    }

    return true;
}
}
