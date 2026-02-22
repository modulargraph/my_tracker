#include "MixerHitTest.h"

MixerHitResult mixerHitTestStrip (juce::Point<int> pos, const MixerHitTestContext& ctx)
{
    using namespace MixerLayout;

    MixerHitResult result;

    // Determine which strip by searching through visible strips
    int vi = -1;
    for (int i = ctx.scrollOffset; i < ctx.totalStripCount; ++i)
    {
        auto stripBounds = ctx.getStripBounds (i);
        if (pos.x >= stripBounds.getX() && pos.x < stripBounds.getRight())
        {
            vi = i;
            break;
        }
        if (stripBounds.getX() > ctx.componentWidth)
            break;
    }
    if (vi < 0 || vi >= ctx.totalStripCount)
        return result;

    result.visualTrack = vi;
    auto bounds = ctx.getStripBounds (vi);
    auto info = ctx.getStripInfo (vi);

    int relY = pos.y;
    int y = 0;

    // Header (all strip types)
    y += kHeaderHeight;
    if (relY < y)
        return result;

    // Helper lambdas for common hit-test sections
    auto hitTestEq = [&] () -> bool
    {
        y += kSectionLabelHeight;
        int eqStart = y;
        y += kEqSectionHeight;
        if (relY < y)
        {
            result.section = MixerSection::EQ;
            int relEqY = relY - eqStart;
            if (relEqY >= (kEqSectionHeight - 18))
            {
                result.param = 3;
                return true;
            }
            int relX = pos.x - bounds.getX();
            int barWidth = (bounds.getWidth() - 16) / 3;
            result.param = juce::jlimit (0, 2, (relX - 4) / (barWidth + 4));
            return true;
        }
        return false;
    };

    auto hitTestComp = [&] () -> bool
    {
        y += kSectionLabelHeight;
        int compStart = y;
        y += kCompSectionHeight;
        if (relY < y)
        {
            result.section = MixerSection::Comp;
            int relX = pos.x - bounds.getX();
            int relCY = relY - compStart;
            int col2 = (relX < bounds.getWidth() / 2) ? 0 : 1;
            int row2 = (relCY < kCompSectionHeight / 2) ? 0 : 1;
            result.param = row2 * 2 + col2;
            return true;
        }
        return false;
    };

    auto hitTestInserts = [&] (int insertHeight, const std::vector<InsertSlotState>& slots) -> bool
    {
        if (insertHeight > 0)
        {
            y += kSectionLabelHeight;
            int insertsStart = y;
            y += insertHeight;
            if (relY < y)
            {
                result.section = MixerSection::Inserts;
                int relInsertY = relY - insertsStart;
                int numSlots = static_cast<int> (slots.size());
                int slotIdx = relInsertY / kInsertRowHeight;
                if (slotIdx < numSlots)
                {
                    result.hitInsertSlot = slotIdx;
                    result.param = slotIdx;
                    int relX = pos.x - bounds.getX();
                    int innerRight = bounds.getRight() - 2;
                    if (relX < 16)
                        result.hitInsertBypass = true;
                    else if (relX > innerRight - bounds.getX() - 18)
                        result.hitInsertRemove = true;
                    else
                        result.hitInsertOpen = true;
                }
                else
                {
                    result.hitInsertAdd = true;
                    result.param = -1;
                }
                return true;
            }
        }
        return false;
    };

    auto hitTestPan = [&] () -> bool
    {
        y += kPanSectionHeight;
        if (relY < y)
        {
            result.section = MixerSection::Pan;
            result.param = 0;
            return true;
        }
        return false;
    };

    auto hitTestMuteSolo = [&] (bool hasSolo) -> bool
    {
        int muteSoloTop = ctx.componentHeight - kMuteSoloHeight;
        if (relY >= muteSoloTop)
        {
            int relX = pos.x - bounds.getX();
            if (hasSolo && relX >= bounds.getWidth() / 2)
                result.hitSolo = true;
            else
                result.hitMute = true;
            return true;
        }
        return false;
    };

    if (info.type == MixerStripType::Track)
    {
        // Regular track: EQ -> Comp -> Inserts -> Sends -> Sep -> Pan -> Sep -> Volume (Mute/Solo at bottom)
        if (hitTestEq()) return result;
        if (hitTestComp()) return result;

        int physTrack = info.index;
        int insertHeight = ctx.getInsertsSectionHeight (physTrack);
        auto& slots = ctx.getTrackInsertSlots (physTrack);
        if (hitTestInserts (insertHeight, slots)) return result;

        // Sends
        y += kSectionLabelHeight;
        int sendsStart = y;
        y += kSendsSectionHeight;
        if (relY < y)
        {
            result.section = MixerSection::Sends;
            result.param = (relY - sendsStart < kSendsSectionHeight / 2) ? 0 : 1;
            return result;
        }

        y += 1; // separator
        if (hitTestPan()) return result;
        y += 1; // separator

        if (hitTestMuteSolo (true)) return result;

        result.section = MixerSection::Volume;
        result.param = 0;
        return result;
    }
    else if (info.type == MixerStripType::DelayReturn || info.type == MixerStripType::ReverbReturn)
    {
        // Send return: EQ -> Sep -> Pan -> Sep -> Volume (Mute at bottom)
        if (hitTestEq()) return result;
        y += 1; // separator
        if (hitTestPan()) return result;
        y += 1; // separator

        if (hitTestMuteSolo (false)) return result;

        result.section = MixerSection::Volume;
        result.param = 0;
        return result;
    }
    else if (info.type == MixerStripType::GroupBus)
    {
        // Group bus: EQ -> Comp -> Sep -> Pan -> Sep -> Volume (Mute/Solo at bottom)
        if (hitTestEq()) return result;
        if (hitTestComp()) return result;
        y += 1; // separator
        if (hitTestPan()) return result;
        y += 1; // separator

        if (hitTestMuteSolo (true)) return result;

        result.section = MixerSection::Volume;
        result.param = 0;
        return result;
    }
    else if (info.type == MixerStripType::Master)
    {
        // Master: EQ -> Comp -> Inserts -> Limiter -> Sep -> Pan -> Sep -> Volume
        if (hitTestEq()) return result;
        if (hitTestComp()) return result;

        int insertHeight = ctx.getMasterInsertsSectionHeight();
        if (hitTestInserts (insertHeight, ctx.getMasterInsertSlots())) return result;

        // Limiter
        y += kSectionLabelHeight;
        int limStart = y;
        y += kLimiterSectionHeight;
        if (relY < y)
        {
            result.section = MixerSection::Limiter;
            int relX = pos.x - bounds.getX();
            result.param = (relX < bounds.getWidth() / 2) ? 0 : 1;
            juce::ignoreUnused (limStart);
            return result;
        }

        y += 1; // separator
        if (hitTestPan()) return result;
        y += 1; // separator

        result.section = MixerSection::Volume;
        result.param = 0;
        return result;
    }

    result.section = MixerSection::Volume;
    result.param = 0;
    return result;
}
