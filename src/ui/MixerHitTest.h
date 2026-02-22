#pragma once

#include <JuceHeader.h>
#include <vector>
#include "MixerState.h"

//==============================================================================
// Types shared between MixerComponent and hit-testing logic
//==============================================================================

enum class MixerSection { EQ, Comp, Inserts, Sends, Pan, Volume, Limiter };
enum class MixerStripType { Track, DelayReturn, ReverbReturn, GroupBus, Master };

struct MixerStripInfo
{
    MixerStripType type = MixerStripType::Track;
    int index = 0;
};

//==============================================================================
// Hit-testing result and function
//==============================================================================

struct MixerHitResult
{
    int visualTrack = -1;
    MixerSection section = MixerSection::Volume;
    int param = -1;
    bool hitMute = false;
    bool hitSolo = false;
    bool hitInsertAdd = false;
    int hitInsertSlot = -1;
    bool hitInsertBypass = false;
    bool hitInsertOpen = false;
    bool hitInsertRemove = false;
};

//==============================================================================
// Layout constants used by hit-testing (and painting)
//==============================================================================

namespace MixerLayout
{
    static constexpr int kStripWidth          = 104;
    static constexpr int kStripGap            = 1;
    static constexpr int kSeparatorWidth      = 6;
    static constexpr int kHeaderHeight        = 31;
    static constexpr int kEqSectionHeight     = 104;
    static constexpr int kCompSectionHeight   = 104;
    static constexpr int kLimiterSectionHeight = 57;
    static constexpr int kInsertRowHeight     = 20;
    static constexpr int kInsertAddButtonHeight = 20;
    static constexpr int kSendsSectionHeight  = 57;
    static constexpr int kPanSectionHeight    = 36;
    static constexpr int kMuteSoloHeight      = 31;
    static constexpr int kSectionLabelHeight  = 18;
}

//==============================================================================
// Context passed to the hit-test function, providing the information it needs
// from MixerComponent without coupling to the class directly.
//==============================================================================

struct MixerHitTestContext
{
    int scrollOffset = 0;
    int componentWidth = 0;
    int componentHeight = 0;
    int totalStripCount = 0;

    // Callbacks to query strip geometry and state
    std::function<juce::Rectangle<int> (int visualIndex)> getStripBounds;
    std::function<MixerStripInfo (int visualIndex)> getStripInfo;
    std::function<int (int physTrack)> getInsertsSectionHeight;
    std::function<int()> getMasterInsertsSectionHeight;
    std::function<const std::vector<InsertSlotState>& (int physTrack)> getTrackInsertSlots;
    std::function<const std::vector<InsertSlotState>& ()> getMasterInsertSlots;
};

MixerHitResult mixerHitTestStrip (juce::Point<int> pos, const MixerHitTestContext& ctx);
