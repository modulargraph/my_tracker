#include "MixerStateSerializer.h"

namespace MixerStateSerializer
{

void save (juce::ValueTree& root, const MixerState& mixerState)
{
    // Mixer state (only save non-default)
    if (! mixerState.isDefault())
    {
        juce::ValueTree mixTree ("Mixer");
        for (int i = 0; i < kNumTracks; ++i)
        {
            auto& t = mixerState.tracks[static_cast<size_t> (i)];
            if (t.isDefault()) continue;

            juce::ValueTree trackTree ("Track");
            trackTree.setProperty ("index", i, nullptr);
            trackTree.setProperty ("volume", t.volume, nullptr);
            trackTree.setProperty ("pan", t.pan, nullptr);
            if (t.muted)  trackTree.setProperty ("muted", true, nullptr);
            if (t.soloed) trackTree.setProperty ("soloed", true, nullptr);
            trackTree.setProperty ("eqLow", t.eqLowGain, nullptr);
            trackTree.setProperty ("eqMid", t.eqMidGain, nullptr);
            trackTree.setProperty ("eqHigh", t.eqHighGain, nullptr);
            trackTree.setProperty ("eqMidFreq", t.eqMidFreq, nullptr);
            trackTree.setProperty ("compThresh", t.compThreshold, nullptr);
            trackTree.setProperty ("compRatio", t.compRatio, nullptr);
            trackTree.setProperty ("compAttack", t.compAttack, nullptr);
            trackTree.setProperty ("compRelease", t.compRelease, nullptr);
            trackTree.setProperty ("reverbSend", t.reverbSend, nullptr);
            trackTree.setProperty ("delaySend", t.delaySend, nullptr);
            mixTree.addChild (trackTree, -1, nullptr);
        }
        root.addChild (mixTree, -1, nullptr);
    }

    // Insert plugin slots (V7+)
    {
        bool hasInserts = false;
        for (auto& slots : mixerState.insertSlots)
            if (! slots.empty())
                hasInserts = true;

        if (hasInserts)
        {
            juce::ValueTree insertsTree ("InsertPlugins");
            for (int i = 0; i < kNumTracks; ++i)
            {
                auto& slots = mixerState.insertSlots[static_cast<size_t> (i)];
                if (slots.empty()) continue;

                juce::ValueTree trackTree ("Track");
                trackTree.setProperty ("index", i, nullptr);

                for (size_t si = 0; si < slots.size(); ++si)
                {
                    auto& slot = slots[si];
                    if (slot.isEmpty()) continue;

                    juce::ValueTree slotTree ("InsertSlot");
                    slotTree.setProperty ("name", slot.pluginName, nullptr);
                    slotTree.setProperty ("identifier", slot.pluginIdentifier, nullptr);
                    slotTree.setProperty ("format", slot.pluginFormatName, nullptr);
                    if (slot.bypassed)
                        slotTree.setProperty ("bypassed", true, nullptr);
                    if (slot.pluginState.isValid())
                        slotTree.addChild (slot.pluginState.createCopy(), -1, nullptr);

                    trackTree.addChild (slotTree, -1, nullptr);
                }

                insertsTree.addChild (trackTree, -1, nullptr);
            }
            root.addChild (insertsTree, -1, nullptr);
        }
    }

    // Send return channels (V9+)
    {
        bool hasSendReturns = false;
        for (auto& sr : mixerState.sendReturns)
            if (! sr.isDefault())
                hasSendReturns = true;

        if (hasSendReturns)
        {
            juce::ValueTree srTree ("SendReturns");
            for (int i = 0; i < 2; ++i)
            {
                auto& sr = mixerState.sendReturns[static_cast<size_t> (i)];
                if (sr.isDefault()) continue;

                juce::ValueTree chTree ("Channel");
                chTree.setProperty ("index", i, nullptr);
                chTree.setProperty ("volume", sr.volume, nullptr);
                chTree.setProperty ("pan", sr.pan, nullptr);
                if (sr.muted) chTree.setProperty ("muted", true, nullptr);
                chTree.setProperty ("eqLow", sr.eqLowGain, nullptr);
                chTree.setProperty ("eqMid", sr.eqMidGain, nullptr);
                chTree.setProperty ("eqHigh", sr.eqHighGain, nullptr);
                chTree.setProperty ("eqMidFreq", sr.eqMidFreq, nullptr);
                srTree.addChild (chTree, -1, nullptr);
            }
            root.addChild (srTree, -1, nullptr);
        }
    }

    // Group bus states (V9+)
    {
        bool hasGroupBuses = false;
        for (auto& gb : mixerState.groupBuses)
            if (! gb.isDefault())
                hasGroupBuses = true;

        if (hasGroupBuses)
        {
            juce::ValueTree gbTree ("GroupBuses");
            for (int i = 0; i < kMaxGroupBuses; ++i)
            {
                auto& gb = mixerState.groupBuses[static_cast<size_t> (i)];
                if (gb.isDefault()) continue;

                juce::ValueTree busTree ("Bus");
                busTree.setProperty ("index", i, nullptr);
                busTree.setProperty ("volume", gb.volume, nullptr);
                busTree.setProperty ("pan", gb.pan, nullptr);
                if (gb.muted) busTree.setProperty ("muted", true, nullptr);
                if (gb.soloed) busTree.setProperty ("soloed", true, nullptr);
                busTree.setProperty ("eqLow", gb.eqLowGain, nullptr);
                busTree.setProperty ("eqMid", gb.eqMidGain, nullptr);
                busTree.setProperty ("eqHigh", gb.eqHighGain, nullptr);
                busTree.setProperty ("eqMidFreq", gb.eqMidFreq, nullptr);
                busTree.setProperty ("compThresh", gb.compThreshold, nullptr);
                busTree.setProperty ("compRatio", gb.compRatio, nullptr);
                busTree.setProperty ("compAttack", gb.compAttack, nullptr);
                busTree.setProperty ("compRelease", gb.compRelease, nullptr);
                gbTree.addChild (busTree, -1, nullptr);
            }
            root.addChild (gbTree, -1, nullptr);
        }
    }

    // Master track state (V9+)
    if (! mixerState.master.isDefault())
    {
        juce::ValueTree masterTree ("MasterTrack");
        masterTree.setProperty ("volume", mixerState.master.volume, nullptr);
        masterTree.setProperty ("pan", mixerState.master.pan, nullptr);
        masterTree.setProperty ("eqLow", mixerState.master.eqLowGain, nullptr);
        masterTree.setProperty ("eqMid", mixerState.master.eqMidGain, nullptr);
        masterTree.setProperty ("eqHigh", mixerState.master.eqHighGain, nullptr);
        masterTree.setProperty ("eqMidFreq", mixerState.master.eqMidFreq, nullptr);
        masterTree.setProperty ("compThresh", mixerState.master.compThreshold, nullptr);
        masterTree.setProperty ("compRatio", mixerState.master.compRatio, nullptr);
        masterTree.setProperty ("compAttack", mixerState.master.compAttack, nullptr);
        masterTree.setProperty ("compRelease", mixerState.master.compRelease, nullptr);
        masterTree.setProperty ("limiterThresh", mixerState.master.limiterThreshold, nullptr);
        masterTree.setProperty ("limiterRelease", mixerState.master.limiterRelease, nullptr);
        root.addChild (masterTree, -1, nullptr);
    }

    // Master insert plugin slots (V9+)
    if (! mixerState.masterInsertSlots.empty())
    {
        juce::ValueTree masterInsertsTree ("MasterInsertPlugins");
        for (size_t si = 0; si < mixerState.masterInsertSlots.size(); ++si)
        {
            auto& slot = mixerState.masterInsertSlots[si];
            if (slot.isEmpty()) continue;

            juce::ValueTree slotTree ("InsertSlot");
            slotTree.setProperty ("name", slot.pluginName, nullptr);
            slotTree.setProperty ("identifier", slot.pluginIdentifier, nullptr);
            slotTree.setProperty ("format", slot.pluginFormatName, nullptr);
            if (slot.bypassed)
                slotTree.setProperty ("bypassed", true, nullptr);
            if (slot.pluginState.isValid())
                slotTree.addChild (slot.pluginState.createCopy(), -1, nullptr);

            masterInsertsTree.addChild (slotTree, -1, nullptr);
        }
        root.addChild (masterInsertsTree, -1, nullptr);
    }
}

void load (const juce::ValueTree& root, MixerState& mixerState)
{
    mixerState.reset();

    // Mixer state (V4+)
    auto mixTree = root.getChildWithName ("Mixer");
    if (mixTree.isValid())
    {
        for (int i = 0; i < mixTree.getNumChildren(); ++i)
        {
            auto trackTree = mixTree.getChild (i);
            if (! trackTree.hasType ("Track")) continue;

            int idx = trackTree.getProperty ("index", -1);
            if (idx < 0 || idx >= kNumTracks) continue;

            auto& t = mixerState.tracks[static_cast<size_t> (idx)];
            t.volume       = trackTree.getProperty ("volume", 0.0);
            t.pan          = trackTree.getProperty ("pan", 0);
            t.muted        = trackTree.getProperty ("muted", false);
            t.soloed       = trackTree.getProperty ("soloed", false);
            t.eqLowGain   = trackTree.getProperty ("eqLow", 0.0);
            t.eqMidGain   = trackTree.getProperty ("eqMid", 0.0);
            t.eqHighGain  = trackTree.getProperty ("eqHigh", 0.0);
            t.eqMidFreq   = trackTree.getProperty ("eqMidFreq", 1000.0);
            t.compThreshold = trackTree.getProperty ("compThresh", 0.0);
            t.compRatio    = trackTree.getProperty ("compRatio", 1.0);
            t.compAttack   = trackTree.getProperty ("compAttack", 10.0);
            t.compRelease  = trackTree.getProperty ("compRelease", 100.0);
            t.reverbSend   = trackTree.getProperty ("reverbSend", -100.0);
            t.delaySend    = trackTree.getProperty ("delaySend", -100.0);
        }
    }

    // Insert plugin slots (V7+)
    for (auto& slots : mixerState.insertSlots)
        slots.clear();

    auto insertsTree = root.getChildWithName ("InsertPlugins");
    if (insertsTree.isValid())
    {
        for (int i = 0; i < insertsTree.getNumChildren(); ++i)
        {
            auto trackTree = insertsTree.getChild (i);
            if (! trackTree.hasType ("Track")) continue;

            int idx = trackTree.getProperty ("index", -1);
            if (idx < 0 || idx >= kNumTracks) continue;

            auto& slots = mixerState.insertSlots[static_cast<size_t> (idx)];

            for (int si = 0; si < trackTree.getNumChildren(); ++si)
            {
                auto slotTree = trackTree.getChild (si);
                if (! slotTree.hasType ("InsertSlot")) continue;

                if (static_cast<int> (slots.size()) >= kMaxInsertSlots)
                    break;

                InsertSlotState slot;
                slot.pluginName = slotTree.getProperty ("name", "").toString();
                slot.pluginIdentifier = slotTree.getProperty ("identifier", "").toString();
                slot.pluginFormatName = slotTree.getProperty ("format", "").toString();
                slot.bypassed = slotTree.getProperty ("bypassed", false);

                // Restore plugin state (first child ValueTree if present)
                if (slotTree.getNumChildren() > 0)
                    slot.pluginState = slotTree.getChild (0).createCopy();

                if (! slot.isEmpty())
                    slots.push_back (std::move (slot));
            }
        }
    }

    // Send return channels (V9+)
    for (auto& sr : mixerState.sendReturns)
        sr = SendReturnState {};
    auto srTree = root.getChildWithName ("SendReturns");
    if (srTree.isValid())
    {
        for (int i = 0; i < srTree.getNumChildren(); ++i)
        {
            auto chTree = srTree.getChild (i);
            if (! chTree.hasType ("Channel")) continue;

            int idx = chTree.getProperty ("index", -1);
            if (idx < 0 || idx >= 2) continue;

            auto& sr = mixerState.sendReturns[static_cast<size_t> (idx)];
            sr.volume    = chTree.getProperty ("volume", 0.0);
            sr.pan       = chTree.getProperty ("pan", 0);
            sr.muted     = chTree.getProperty ("muted", false);
            sr.eqLowGain = chTree.getProperty ("eqLow", 0.0);
            sr.eqMidGain = chTree.getProperty ("eqMid", 0.0);
            sr.eqHighGain = chTree.getProperty ("eqHigh", 0.0);
            sr.eqMidFreq = chTree.getProperty ("eqMidFreq", 1000.0);
        }
    }

    // Group bus states (V9+)
    for (auto& gb : mixerState.groupBuses)
        gb = GroupBusState {};
    auto gbTree = root.getChildWithName ("GroupBuses");
    if (gbTree.isValid())
    {
        for (int i = 0; i < gbTree.getNumChildren(); ++i)
        {
            auto busTree = gbTree.getChild (i);
            if (! busTree.hasType ("Bus")) continue;

            int idx = busTree.getProperty ("index", -1);
            if (idx < 0 || idx >= kMaxGroupBuses) continue;

            auto& gb = mixerState.groupBuses[static_cast<size_t> (idx)];
            gb.volume       = busTree.getProperty ("volume", 0.0);
            gb.pan          = busTree.getProperty ("pan", 0);
            gb.muted        = busTree.getProperty ("muted", false);
            gb.soloed       = busTree.getProperty ("soloed", false);
            gb.eqLowGain    = busTree.getProperty ("eqLow", 0.0);
            gb.eqMidGain    = busTree.getProperty ("eqMid", 0.0);
            gb.eqHighGain   = busTree.getProperty ("eqHigh", 0.0);
            gb.eqMidFreq    = busTree.getProperty ("eqMidFreq", 1000.0);
            gb.compThreshold = busTree.getProperty ("compThresh", 0.0);
            gb.compRatio    = busTree.getProperty ("compRatio", 1.0);
            gb.compAttack   = busTree.getProperty ("compAttack", 10.0);
            gb.compRelease  = busTree.getProperty ("compRelease", 100.0);
        }
    }

    // Master track state (V9+)
    mixerState.master = MasterMixState {};
    auto masterTree = root.getChildWithName ("MasterTrack");
    if (masterTree.isValid())
    {
        mixerState.master.volume         = masterTree.getProperty ("volume", 0.0);
        mixerState.master.pan            = masterTree.getProperty ("pan", 0);
        mixerState.master.eqLowGain      = masterTree.getProperty ("eqLow", 0.0);
        mixerState.master.eqMidGain      = masterTree.getProperty ("eqMid", 0.0);
        mixerState.master.eqHighGain     = masterTree.getProperty ("eqHigh", 0.0);
        mixerState.master.eqMidFreq      = masterTree.getProperty ("eqMidFreq", 1000.0);
        mixerState.master.compThreshold  = masterTree.getProperty ("compThresh", 0.0);
        mixerState.master.compRatio      = masterTree.getProperty ("compRatio", 1.0);
        mixerState.master.compAttack     = masterTree.getProperty ("compAttack", 10.0);
        mixerState.master.compRelease    = masterTree.getProperty ("compRelease", 100.0);
        mixerState.master.limiterThreshold = masterTree.getProperty ("limiterThresh", 0.0);
        mixerState.master.limiterRelease = masterTree.getProperty ("limiterRelease", 50.0);
    }

    // Master insert plugin slots (V9+)
    mixerState.masterInsertSlots.clear();
    auto masterInsertsTree = root.getChildWithName ("MasterInsertPlugins");
    if (masterInsertsTree.isValid())
    {
        for (int si = 0; si < masterInsertsTree.getNumChildren(); ++si)
        {
            auto slotTree = masterInsertsTree.getChild (si);
            if (! slotTree.hasType ("InsertSlot")) continue;

            if (static_cast<int> (mixerState.masterInsertSlots.size()) >= kMaxInsertSlots)
                break;

            InsertSlotState slot;
            slot.pluginName = slotTree.getProperty ("name", "").toString();
            slot.pluginIdentifier = slotTree.getProperty ("identifier", "").toString();
            slot.pluginFormatName = slotTree.getProperty ("format", "").toString();
            slot.bypassed = slotTree.getProperty ("bypassed", false);

            if (slotTree.getNumChildren() > 0)
                slot.pluginState = slotTree.getChild (0).createCopy();

            if (! slot.isEmpty())
                mixerState.masterInsertSlots.push_back (std::move (slot));
        }
    }
}

} // namespace MixerStateSerializer
