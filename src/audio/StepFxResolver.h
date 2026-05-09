#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "PatternTypes.h"
#include "SamplePlaybackLayout.h"

namespace StepFxResolver
{
enum class ArpDirection
{
    Rising,
    Falling,
    Random
};

struct ArpFx
{
    ArpDirection direction = ArpDirection::Rising;
    int timingValue = 1;
    bool multiplier = false;
};

inline uint32_t makeStepHash (int row, int track, int lane, int repeatIndex, int salt)
{
    uint32_t x = 0x811c9dc5u;
    auto mix = [&x] (uint32_t value)
    {
        x ^= value + 0x9e3779b9u + (x << 6u) + (x >> 2u);
        x *= 16777619u;
    };

    mix (static_cast<uint32_t> (row + 1));
    mix (static_cast<uint32_t> (track + 17));
    mix (static_cast<uint32_t> (lane + 31));
    mix (static_cast<uint32_t> (repeatIndex + 47));
    mix (static_cast<uint32_t> (salt + 61));
    return x;
}

inline int clampByte (int value)
{
    return std::clamp (value, 0, 255);
}

inline int clampPercent (int value)
{
    return std::clamp (value, 0, 100);
}

inline int centeredRandomOffset (int range, int maxRange, int row, int track, int lane, int repeatIndex, int salt)
{
    range = std::clamp (range, 0, maxRange);
    if (range <= 0)
        return 0;

    const int span = range * 2 + 1;
    return static_cast<int> (makeStepHash (row, track, lane, repeatIndex, salt) % static_cast<uint32_t> (span)) - range;
}

inline int getFxParam (const std::vector<FxSlot>& slots, char commandLetter, int defaultValue = -1)
{
    int value = defaultValue;
    for (const auto& slot : slots)
        if (slot.getCommandLetter() == commandLetter)
            value = slot.fxParam;

    return value;
}

inline int getPercentFxParam (const std::vector<FxSlot>& slots, char commandLetter)
{
    const int value = getFxParam (slots, commandLetter, -1);
    return value < 0 ? -1 : clampPercent (value);
}

inline ArpFx decodeArpFx (int param)
{
    const int clamped = std::clamp (param, 0, 255);
    const bool multiplier = (clamped & 0x80) != 0;
    const int directionCode = multiplier ? ((clamped >> 4) & 0x03)
                                         : ((clamped >> 4) & 0x0F);
    int timingValue = clamped & 0x0F;
    if (timingValue <= 0)
        timingValue = 1;

    ArpDirection direction = ArpDirection::Rising;
    if (directionCode == 1)
        direction = ArpDirection::Falling;
    else if (directionCode >= 2)
        direction = ArpDirection::Random;

    return { direction, timingValue, multiplier };
}

inline int encodeArpFx (ArpDirection direction, int timingValue, bool multiplier)
{
    int directionCode = 0;
    switch (direction)
    {
        case ArpDirection::Rising:  directionCode = 0; break;
        case ArpDirection::Falling: directionCode = 1; break;
        case ArpDirection::Random:  directionCode = 2; break;
    }

    const int timing = std::clamp (timingValue, 1, 15);
    return (multiplier ? 0x80 : 0x00) | (directionCode << 4) | timing;
}

inline std::vector<FxSlot> copyFxSlots (const Cell& cell)
{
    std::vector<FxSlot> slots;
    slots.reserve (static_cast<size_t> (cell.getNumFxSlots()));
    for (int i = 0; i < cell.getNumFxSlots(); ++i)
        slots.push_back (cell.getFxSlot (i));

    return slots;
}

inline int findRandomFxTargetSlot (const std::vector<FxSlot>& slots, int randomSlotIndex)
{
    const int slotCount = static_cast<int> (slots.size());
    if (slotCount <= 1 || randomSlotIndex < 0 || randomSlotIndex >= slotCount)
        return -1;

    auto canTarget = [&slots] (int idx)
    {
        return idx >= 0
            && idx < static_cast<int> (slots.size())
            && ! slots[static_cast<size_t> (idx)].isEmpty()
            && slots[static_cast<size_t> (idx)].getCommandLetter() != 'x';
    };

    if (slotCount == 2)
    {
        const int other = randomSlotIndex == 0 ? 1 : 0;
        return canTarget (other) ? other : -1;
    }

    for (int offset = 1; offset < slotCount; ++offset)
    {
        const int next = (randomSlotIndex + offset) % slotCount;
        if (canTarget (next))
            return next;
    }

    return -1;
}

inline std::vector<FxSlot> resolveFxSlots (const Cell& cell, int row, int track, int lane, int repeatIndex)
{
    auto slots = copyFxSlots (cell);

    for (int i = 0; i < static_cast<int> (slots.size()); ++i)
    {
        const auto& randomSlot = slots[static_cast<size_t> (i)];
        if (randomSlot.getCommandLetter() != 'x')
            continue;

        const int targetIdx = findRandomFxTargetSlot (slots, i);
        if (targetIdx < 0)
            continue;

        auto& target = slots[static_cast<size_t> (targetIdx)];
        const int offset = centeredRandomOffset (randomSlot.fxParam, 255, row, track, lane, repeatIndex,
                                                 0xF0 + i * 17 + targetIdx);
        const int randomizedParam = target.fxParam + offset;

        if (target.getCommandLetter() == 'T')
            target.setParam (std::max (1, randomizedParam));
        else
            target.setParam (randomizedParam);
    }

    return slots;
}

inline bool chanceAllowsStep (const std::vector<FxSlot>& slots, int row, int track, int lane, int repeatIndex)
{
    const int chancePercent = getPercentFxParam (slots, 'C');
    if (chancePercent < 0 || chancePercent >= 100)
        return true;
    if (chancePercent <= 0)
        return false;

    return static_cast<int> (makeStepHash (row, track, lane, repeatIndex, 0xC) % 100u) < chancePercent;
}

inline NoteSlot resolveNoteSlot (NoteSlot slot,
                                 const std::vector<FxSlot>& slots,
                                 int row,
                                 int track,
                                 int lane,
                                 int repeatIndex)
{
    if (slot.note >= 0 && slot.note < 128)
    {
        const int range = getPercentFxParam (slots, 'n');
        if (range >= 0)
            slot.note = std::clamp (slot.note + centeredRandomOffset (range, 100, row, track, lane, repeatIndex, 0x4E),
                                    0, 127);
    }

    if (slot.instrument >= 0)
    {
        const int range = getFxParam (slots, 'i', -1);
        if (range >= 0)
            slot.instrument = std::clamp (slot.instrument + centeredRandomOffset (range, 47, row, track, lane, repeatIndex, 0x49),
                                          0, 255);
    }

    const int volumeRange = getPercentFxParam (slots, 'v');
    if (volumeRange >= 0)
    {
        const int baseVolume = slot.volume >= 0 ? slot.volume : 127;
        slot.volume = std::clamp (baseVolume + centeredRandomOffset (volumeRange, 100, row, track, lane, repeatIndex, 0x56),
                                  0, 127);
    }

    return slot;
}

inline std::vector<int> resolveChordNotes (int rootNote, const std::vector<FxSlot>& slots)
{
    if (rootNote < 0 || rootNote >= 128)
        return {};

    std::vector<int> notes { rootNote };
    const int chordParam = getFxParam (slots, '0', -1);
    if (chordParam < 0)
        return notes;

    auto addInterval = [&notes, rootNote] (int interval)
    {
        if (interval <= 0)
            return;

        const int note = std::clamp (rootNote + interval, 0, 127);
        if (std::find (notes.begin(), notes.end(), note) == notes.end())
            notes.push_back (note);
    };

    bool foundInterval = false;
    for (int shift = 12; shift >= 0; shift -= 4)
    {
        const int interval = (chordParam >> shift) & 0x0F;
        if (! foundInterval && interval == 0)
            continue;

        foundInterval = true;
        addInterval (interval);
    }

    return notes;
}

inline std::vector<int> resolveArpNotes (int rootNote,
                                         const std::vector<FxSlot>& slots,
                                         int row,
                                         int track,
                                         int lane,
                                         int repeatIndex)
{
    const int arpParam = getFxParam (slots, 'A', -1);
    if (arpParam < 0)
        return {};

    const auto chordNotes = resolveChordNotes (rootNote, slots);
    if (chordNotes.size() <= 1)
        return {};

    const auto arp = decodeArpFx (arpParam);
    const int stepCount = arp.multiplier ? 1 : std::clamp (arp.timingValue, 1, 16);

    std::vector<int> notes;
    notes.reserve (static_cast<size_t> (stepCount));
    const int sequenceOffset = arp.multiplier
                                   ? (row / std::max (1, arp.timingValue)) + repeatIndex
                                   : 0;

    for (int i = 0; i < stepCount; ++i)
    {
        int idx = 0;
        const int sequenceIndex = sequenceOffset + i;
        if (arp.direction == ArpDirection::Falling)
            idx = static_cast<int> (chordNotes.size()) - 1 - (sequenceIndex % static_cast<int> (chordNotes.size()));
        else if (arp.direction == ArpDirection::Random)
            idx = static_cast<int> (makeStepHash (row, track, lane, repeatIndex, 0xA0 + sequenceIndex)
                                    % static_cast<uint32_t> (chordNotes.size()));
        else
            idx = sequenceIndex % static_cast<int> (chordNotes.size());

        notes.push_back (chordNotes[static_cast<size_t> (idx)]);
    }

    return notes;
}

inline SamplePlaybackLayout::RollFx getRollFx (const std::vector<FxSlot>& slots)
{
    const int rollParam = getFxParam (slots, 'R', -1);
    return rollParam < 0 ? SamplePlaybackLayout::RollFx {}
                         : SamplePlaybackLayout::decodeRollFx (rollParam);
}

inline int resolveRollVelocity (int baseVelocity,
                                SamplePlaybackLayout::RollType type,
                                int repeatIndex,
                                int repeatCount)
{
    baseVelocity = std::clamp (baseVelocity, 0, 127);
    repeatCount = std::max (1, repeatCount);
    const double progress = repeatCount <= 1
                                ? 1.0
                                : static_cast<double> (repeatIndex) / static_cast<double> (repeatCount - 1);

    switch (type)
    {
        case SamplePlaybackLayout::RollType::Regular:
        case SamplePlaybackLayout::RollType::NoteDown:
        case SamplePlaybackLayout::RollType::NoteUp:
        case SamplePlaybackLayout::RollType::NoteRandom:
            return baseVelocity;
        case SamplePlaybackLayout::RollType::VolumeDown:
            return std::clamp (static_cast<int> (std::lround (baseVelocity * (1.0 - progress))), 0, 127);
        case SamplePlaybackLayout::RollType::VolumeUp:
            return std::clamp (static_cast<int> (std::lround (baseVelocity * progress)), 0, 127);
    }

    return baseVelocity;
}

inline int resolveRollNoteOffset (SamplePlaybackLayout::RollType type,
                                  int repeatIndex,
                                  int repeatCount,
                                  int row,
                                  int track,
                                  int lane,
                                  int repeat)
{
    switch (type)
    {
        case SamplePlaybackLayout::RollType::Regular:
        case SamplePlaybackLayout::RollType::VolumeDown:
        case SamplePlaybackLayout::RollType::VolumeUp:
            return 0;
        case SamplePlaybackLayout::RollType::NoteDown:
            return -repeatIndex;
        case SamplePlaybackLayout::RollType::NoteUp:
            return repeatIndex;
        case SamplePlaybackLayout::RollType::NoteRandom:
        {
            const int range = std::max (1, repeatCount);
            const int span = range * 2 + 1;
            return static_cast<int> (makeStepHash (row, track, lane, repeat, 0x5200 + repeatIndex)
                                     % static_cast<uint32_t> (span)) - range;
        }
    }

    return 0;
}
}
