#pragma once

#include "MixerHitTest.h"
#include <array>
#include <algorithm>

//==============================================================================
// Data-driven section navigation for the mixer.
//
// Each strip type defines an ordered list of sections. nextSection() and
// prevSection() cycle through the list, wrapping around at the ends.
// This replaces the hand-written switch-case cascades that previously lived
// in MixerComponent::nextSection() / prevSection().
//==============================================================================
namespace MixerNavigation
{

using Section   = MixerSection;
using StripType = MixerStripType;

//==============================================================================
// Section orderings per strip type
//==============================================================================

inline constexpr std::array<Section, 6> kTrackOrder =
    { Section::EQ, Section::Comp, Section::Inserts, Section::Sends, Section::Pan, Section::Volume };

inline constexpr std::array<Section, 3> kSendReturnOrder =
    { Section::EQ, Section::Pan, Section::Volume };

inline constexpr std::array<Section, 4> kGroupBusOrder =
    { Section::EQ, Section::Comp, Section::Pan, Section::Volume };

inline constexpr std::array<Section, 6> kMasterOrder =
    { Section::EQ, Section::Comp, Section::Inserts, Section::Limiter, Section::Pan, Section::Volume };

//==============================================================================
// Generic helper: find the next or previous element in a cyclic array.
//==============================================================================

template <std::size_t N>
inline Section cycleSection (const std::array<Section, N>& order, Section current, int direction)
{
    auto it = std::find (order.begin(), order.end(), current);
    if (it == order.end())
        return (direction > 0) ? order.front() : order.back();

    auto idx = static_cast<int> (std::distance (order.begin(), it));
    idx = (idx + direction + static_cast<int> (N)) % static_cast<int> (N);
    return order[static_cast<std::size_t> (idx)];
}

//==============================================================================
// Public API
//==============================================================================

inline Section nextSection (Section current, StripType type)
{
    switch (type)
    {
        case StripType::Master:                        return cycleSection (kMasterOrder,     current, +1);
        case StripType::DelayReturn:
        case StripType::ReverbReturn:                  return cycleSection (kSendReturnOrder, current, +1);
        case StripType::GroupBus:                      return cycleSection (kGroupBusOrder,   current, +1);
        case StripType::Track:     [[fallthrough]];
        default:                                       return cycleSection (kTrackOrder,      current, +1);
    }
}

inline Section prevSection (Section current, StripType type)
{
    switch (type)
    {
        case StripType::Master:                        return cycleSection (kMasterOrder,     current, -1);
        case StripType::DelayReturn:
        case StripType::ReverbReturn:                  return cycleSection (kSendReturnOrder, current, -1);
        case StripType::GroupBus:                      return cycleSection (kGroupBusOrder,   current, -1);
        case StripType::Track:     [[fallthrough]];
        default:                                       return cycleSection (kTrackOrder,      current, -1);
    }
}

} // namespace MixerNavigation
