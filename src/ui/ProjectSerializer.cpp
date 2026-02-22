#include <set>
#include "ProjectSerializer.h"

juce::String ProjectSerializer::saveToFile (const juce::File& file, const PatternData& patternData,
                                            double bpm, int rowsPerBeat,
                                            const std::map<int, juce::File>& loadedSamples,
                                            const std::map<int, InstrumentParams>& instrumentParams,
                                            const Arrangement& arrangement,
                                            const TrackLayout& trackLayout,
                                            const MixerState& mixerState,
                                            const DelayParams& delayParams,
                                            const ReverbParams& reverbParams,
                                            int followMode,
                                            const juce::String& browserDir,
                                            const std::map<int, InstrumentSlotInfo>* pluginSlots)
{
    juce::ValueTree root ("TrackerAdjustProject");
    root.setProperty ("version", 9, nullptr);

    // Settings
    juce::ValueTree settings ("Settings");
    settings.setProperty ("bpm", bpm, nullptr);
    settings.setProperty ("rowsPerBeat", rowsPerBeat, nullptr);
    settings.setProperty ("currentPattern", patternData.getCurrentPatternIndex(), nullptr);
    if (followMode != 0)
        settings.setProperty ("followMode", followMode, nullptr);
    if (browserDir.isNotEmpty())
        settings.setProperty ("browserDir", browserDir, nullptr);
    root.addChild (settings, -1, nullptr);

    // Samples (embedded as base64 for self-contained projects)
    juce::ValueTree samples ("Samples");
    for (auto& [index, sampleFile] : loadedSamples)
    {
        juce::ValueTree sample ("Sample");
        sample.setProperty ("index", index, nullptr);
        sample.setProperty ("path", sampleFile.getRelativePathFrom (file.getParentDirectory()), nullptr);
        sample.setProperty ("absPath", sampleFile.getFullPathName(), nullptr);
        sample.setProperty ("filename", sampleFile.getFileName(), nullptr);

        // Embed sample data as base64 for self-contained projects
        juce::MemoryBlock fileData;
        if (sampleFile.existsAsFile() && sampleFile.loadFileAsData (fileData))
            sample.setProperty ("data", fileData.toBase64Encoding(), nullptr);

        samples.addChild (sample, -1, nullptr);
    }
    root.addChild (samples, -1, nullptr);

    // Instrument params (only save non-default)
    juce::ValueTree paramsTree ("InstrumentParams");
    for (auto& [index, params] : instrumentParams)
    {
        if (params.isDefault()) continue;

        juce::ValueTree paramTree ("Param");
        paramTree.setProperty ("index", index, nullptr);

        // General
        paramTree.setProperty ("volume", params.volume, nullptr);
        paramTree.setProperty ("panning", params.panning, nullptr);
        paramTree.setProperty ("tune", params.tune, nullptr);
        paramTree.setProperty ("finetune", params.finetune, nullptr);

        // Filter
        paramTree.setProperty ("filterType", static_cast<int> (params.filterType), nullptr);
        paramTree.setProperty ("cutoff", params.cutoff, nullptr);
        paramTree.setProperty ("resonance", params.resonance, nullptr);

        // Effects
        paramTree.setProperty ("overdrive", params.overdrive, nullptr);
        paramTree.setProperty ("bitDepth", params.bitDepth, nullptr);
        paramTree.setProperty ("reverbSend", params.reverbSend, nullptr);
        paramTree.setProperty ("delaySend", params.delaySend, nullptr);

        // Sample position
        paramTree.setProperty ("startPos", params.startPos, nullptr);
        paramTree.setProperty ("endPos", params.endPos, nullptr);
        paramTree.setProperty ("loopStart", params.loopStart, nullptr);
        paramTree.setProperty ("loopEnd", params.loopEnd, nullptr);

        // Playback
        paramTree.setProperty ("playMode", static_cast<int> (params.playMode), nullptr);
        paramTree.setProperty ("reversed", params.reversed, nullptr);

        // Granular
        paramTree.setProperty ("grainPos", params.granularPosition, nullptr);
        paramTree.setProperty ("grainLen", params.granularLength, nullptr);
        paramTree.setProperty ("grainShape", static_cast<int> (params.granularShape), nullptr);
        paramTree.setProperty ("grainLoop", static_cast<int> (params.granularLoop), nullptr);

        // Slices
        if (! params.slicePoints.empty())
        {
            juce::String sliceStr;
            for (size_t i = 0; i < params.slicePoints.size(); ++i)
            {
                if (i > 0) sliceStr += ",";
                sliceStr += juce::String (params.slicePoints[i], 6);
            }
            paramTree.setProperty ("slices", sliceStr, nullptr);
        }

        // Modulations
        for (int d = 0; d < InstrumentParams::kNumModDests; ++d)
        {
            auto& mod = params.modulations[static_cast<size_t> (d)];
            if (mod.isDefault()) continue;

            juce::ValueTree modTree ("Mod");
            modTree.setProperty ("dest", d, nullptr);
            modTree.setProperty ("type", static_cast<int> (mod.type), nullptr);
            modTree.setProperty ("lfoShape", static_cast<int> (mod.lfoShape), nullptr);
            modTree.setProperty ("lfoSpeed", mod.lfoSpeed, nullptr);
            if (mod.lfoSpeedMode != InstrumentParams::Modulation::LFOSpeedMode::Steps)
                modTree.setProperty ("lfoSpeedMode", static_cast<int> (mod.lfoSpeedMode), nullptr);
            if (mod.lfoSpeedMs != 500)
                modTree.setProperty ("lfoSpeedMs", mod.lfoSpeedMs, nullptr);
            modTree.setProperty ("amount", mod.amount, nullptr);
            modTree.setProperty ("attackS", mod.attackS, nullptr);
            modTree.setProperty ("decayS", mod.decayS, nullptr);
            modTree.setProperty ("sustain", mod.sustain, nullptr);
            modTree.setProperty ("releaseS", mod.releaseS, nullptr);
            if (mod.modMode != InstrumentParams::Modulation::ModMode::PerNote)
                modTree.setProperty ("modMode", static_cast<int> (mod.modMode), nullptr);
            paramTree.addChild (modTree, -1, nullptr);
        }

        paramsTree.addChild (paramTree, -1, nullptr);
    }
    root.addChild (paramsTree, -1, nullptr);

    // Arrangement
    if (arrangement.getNumEntries() > 0)
    {
        juce::ValueTree arrTree ("Arrangement");
        for (int i = 0; i < arrangement.getNumEntries(); ++i)
        {
            auto& entry = arrangement.getEntry (i);
            juce::ValueTree entryTree ("Entry");
            entryTree.setProperty ("pattern", entry.patternIndex, nullptr);
            entryTree.setProperty ("repeats", entry.repeats, nullptr);
            arrTree.addChild (entryTree, -1, nullptr);
        }
        root.addChild (arrTree, -1, nullptr);
    }

    // Track Layout
    {
        juce::ValueTree layoutTree ("TrackLayout");

        juce::String orderStr;
        auto& order = trackLayout.getVisualOrder();
        for (int i = 0; i < kNumTracks; ++i)
        {
            if (i > 0) orderStr += ",";
            orderStr += juce::String (order[static_cast<size_t> (i)]);
        }
        juce::ValueTree voTree ("VisualOrder");
        voTree.setProperty ("values", orderStr, nullptr);
        layoutTree.addChild (voTree, -1, nullptr);

        auto& names = trackLayout.getTrackNames();
        for (int i = 0; i < kNumTracks; ++i)
        {
            if (names[static_cast<size_t> (i)].isNotEmpty())
            {
                juce::ValueTree nameTree ("TrackName");
                nameTree.setProperty ("index", i, nullptr);
                nameTree.setProperty ("name", names[static_cast<size_t> (i)], nullptr);
                layoutTree.addChild (nameTree, -1, nullptr);
            }
        }

        // Note modes (only save if any are non-default)
        {
            bool anyRelease = false;
            for (int i = 0; i < kNumTracks; ++i)
                if (trackLayout.getTrackNoteMode (i) == NoteMode::Release)
                    anyRelease = true;

            if (anyRelease)
            {
                juce::String modeStr;
                for (int i = 0; i < kNumTracks; ++i)
                {
                    if (i > 0) modeStr += ",";
                    modeStr += juce::String (static_cast<int> (trackLayout.getTrackNoteMode (i)));
                }
                juce::ValueTree nmTree ("NoteModes");
                nmTree.setProperty ("values", modeStr, nullptr);
                layoutTree.addChild (nmTree, -1, nullptr);
            }
        }

        // FX lane counts (only save if any track has more than 1)
        {
            bool anyMultiFx = false;
            for (int i = 0; i < kNumTracks; ++i)
                if (trackLayout.getTrackFxLaneCount (i) > 1)
                    anyMultiFx = true;

            if (anyMultiFx)
            {
                juce::String fxStr;
                for (int i = 0; i < kNumTracks; ++i)
                {
                    if (i > 0) fxStr += ",";
                    fxStr += juce::String (trackLayout.getTrackFxLaneCount (i));
                }
                juce::ValueTree fxTree ("FxLaneCounts");
                fxTree.setProperty ("values", fxStr, nullptr);
                layoutTree.addChild (fxTree, -1, nullptr);
            }
        }

        // Note lane counts (only save if any track has more than 1)
        {
            bool anyMultiNote = false;
            for (int i = 0; i < kNumTracks; ++i)
                if (trackLayout.getTrackNoteLaneCount (i) > 1)
                    anyMultiNote = true;

            if (anyMultiNote)
            {
                juce::String nlStr;
                for (int i = 0; i < kNumTracks; ++i)
                {
                    if (i > 0) nlStr += ",";
                    nlStr += juce::String (trackLayout.getTrackNoteLaneCount (i));
                }
                juce::ValueTree nlTree ("NoteLaneCounts");
                nlTree.setProperty ("values", nlStr, nullptr);
                layoutTree.addChild (nlTree, -1, nullptr);
            }
        }

        // Master FX lane count (only save if > 1)
        if (trackLayout.getMasterFxLaneCount() > 1)
        {
            juce::ValueTree mfxTree ("MasterFxLanes");
            mfxTree.setProperty ("count", trackLayout.getMasterFxLaneCount(), nullptr);
            layoutTree.addChild (mfxTree, -1, nullptr);
        }

        for (int gi = 0; gi < trackLayout.getNumGroups(); ++gi)
        {
            auto& group = trackLayout.getGroup (gi);
            juce::ValueTree groupTree ("Group");
            groupTree.setProperty ("name", group.name, nullptr);
            groupTree.setProperty ("colour", group.colour.toString(), nullptr);

            for (auto idx : group.trackIndices)
            {
                juce::ValueTree trackTree ("Track");
                trackTree.setProperty ("index", idx, nullptr);
                groupTree.addChild (trackTree, -1, nullptr);
            }
            layoutTree.addChild (groupTree, -1, nullptr);
        }

        root.addChild (layoutTree, -1, nullptr);
    }

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

    // Plugin instrument slots (V7+)
    if (pluginSlots != nullptr && ! pluginSlots->empty())
    {
        juce::ValueTree pluginSlotsTree ("PluginInstrumentSlots");
        for (auto& [index, info] : *pluginSlots)
        {
            if (! info.isPlugin())
                continue;

            juce::ValueTree slotTree ("PluginSlot");
            slotTree.setProperty ("index", index, nullptr);
            slotTree.setProperty ("ownerTrack", info.ownerTrack, nullptr);
            slotTree.setProperty ("pluginName", info.pluginDescription.name, nullptr);
            slotTree.setProperty ("pluginId", info.pluginDescription.fileOrIdentifier, nullptr);
            slotTree.setProperty ("pluginFormat", info.pluginDescription.pluginFormatName, nullptr);
            slotTree.setProperty ("pluginUid", info.pluginDescription.uniqueId, nullptr);
            slotTree.setProperty ("pluginDeprecatedUid", info.pluginDescription.deprecatedUid, nullptr);
            slotTree.setProperty ("pluginManufacturer", info.pluginDescription.manufacturerName, nullptr);
            slotTree.setProperty ("pluginCategory", info.pluginDescription.category, nullptr);
            slotTree.setProperty ("isInstrument", info.pluginDescription.isInstrument, nullptr);
            if (info.pluginState.isValid())
                slotTree.addChild (info.pluginState.createCopy(), -1, nullptr);
            pluginSlotsTree.addChild (slotTree, -1, nullptr);
        }
        root.addChild (pluginSlotsTree, -1, nullptr);
    }

    // Send effects params
    {
        juce::ValueTree sendTree ("SendEffects");

        juce::ValueTree delayTree ("Delay");
        delayTree.setProperty ("time", delayParams.time, nullptr);
        delayTree.setProperty ("syncDiv", delayParams.syncDivision, nullptr);
        delayTree.setProperty ("bpmSync", delayParams.bpmSync, nullptr);
        delayTree.setProperty ("dotted", delayParams.dotted, nullptr);
        delayTree.setProperty ("feedback", delayParams.feedback, nullptr);
        delayTree.setProperty ("filterType", delayParams.filterType, nullptr);
        delayTree.setProperty ("filterCutoff", delayParams.filterCutoff, nullptr);
        delayTree.setProperty ("wet", delayParams.wet, nullptr);
        delayTree.setProperty ("stereoWidth", delayParams.stereoWidth, nullptr);
        sendTree.addChild (delayTree, -1, nullptr);

        juce::ValueTree reverbTree ("Reverb");
        reverbTree.setProperty ("roomSize", reverbParams.roomSize, nullptr);
        reverbTree.setProperty ("decay", reverbParams.decay, nullptr);
        reverbTree.setProperty ("damping", reverbParams.damping, nullptr);
        reverbTree.setProperty ("preDelay", reverbParams.preDelay, nullptr);
        reverbTree.setProperty ("wet", reverbParams.wet, nullptr);
        sendTree.addChild (reverbTree, -1, nullptr);

        root.addChild (sendTree, -1, nullptr);
    }

    // Patterns
    juce::ValueTree patterns ("Patterns");
    for (int i = 0; i < patternData.getNumPatterns(); ++i)
        patterns.addChild (patternToValueTree (patternData.getPattern (i), i), -1, nullptr);
    root.addChild (patterns, -1, nullptr);

    // Write to file
    auto xml = root.createXml();
    if (xml == nullptr)
        return "Failed to create XML";

    if (! xml->writeTo (file))
        return "Failed to write file: " + file.getFullPathName();

    return {};
}

juce::String ProjectSerializer::loadFromFile (const juce::File& file, PatternData& patternData,
                                              double& bpm, int& rowsPerBeat,
                                              std::map<int, juce::File>& loadedSamples,
                                              std::map<int, InstrumentParams>& instrumentParams,
                                              Arrangement& arrangement,
                                              TrackLayout& trackLayout,
                                              MixerState& mixerState,
                                              DelayParams& delayParams,
                                              ReverbParams& reverbParams,
                                              int* followMode,
                                              juce::String* browserDir,
                                              std::map<int, InstrumentSlotInfo>* pluginSlots)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
        return "Failed to parse XML file";

    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("TrackerAdjustProject"))
        return "Not a valid Tracker Adjust project file";

    int version = root.getProperty ("version", 1);

    // Settings
    auto settings = root.getChildWithName ("Settings");
    if (settings.isValid())
    {
        bpm = settings.getProperty ("bpm", 120.0);
        rowsPerBeat = settings.getProperty ("rowsPerBeat", 4);

        if (followMode != nullptr)
            *followMode = settings.getProperty ("followMode", 0);

        if (browserDir != nullptr)
            *browserDir = settings.getProperty ("browserDir", "").toString();
    }

    // Samples
    loadedSamples.clear();
    auto samples = root.getChildWithName ("Samples");
    if (samples.isValid())
    {
        for (int i = 0; i < samples.getNumChildren(); ++i)
        {
            auto sample = samples.getChild (i);
            int index = sample.getProperty ("index", -1);
            juce::String absPath = sample.getProperty ("absPath", "");
            juce::String relPath = sample.getProperty ("path", "");

            juce::File sampleFile (absPath);
            if (! sampleFile.existsAsFile())
                sampleFile = file.getParentDirectory().getChildFile (relPath);

            // If file not found on disk, extract from embedded data
            if (! sampleFile.existsAsFile())
            {
                juce::String base64Data = sample.getProperty ("data", "").toString();
                if (base64Data.isNotEmpty())
                {
                    juce::MemoryBlock fileData;
                    if (fileData.fromBase64Encoding (base64Data))
                    {
                        juce::String filename = sample.getProperty ("filename", "").toString();
                        if (filename.isEmpty())
                            filename = "sample_" + juce::String (index) + ".wav";

                        // Extract to a directory next to the project file
                        auto samplesDir = file.getParentDirectory().getChildFile (
                            file.getFileNameWithoutExtension() + "_samples");
                        samplesDir.createDirectory();
                        sampleFile = samplesDir.getChildFile (filename);

                        if (! sampleFile.existsAsFile())
                            sampleFile.replaceWithData (fileData.getData(), fileData.getSize());
                    }
                }
            }

            if (sampleFile.existsAsFile() && index >= 0)
                loadedSamples[index] = sampleFile;
        }
    }

    // Instrument params (backward-compatible)
    instrumentParams.clear();
    auto paramsTree = root.getChildWithName ("InstrumentParams");
    if (paramsTree.isValid())
    {
        for (int i = 0; i < paramsTree.getNumChildren(); ++i)
        {
            auto paramTree = paramsTree.getChild (i);
            if (! paramTree.hasType ("Param")) continue;

            int index = paramTree.getProperty ("index", -1);
            if (index < 0) continue;

            InstrumentParams params;

            if (version >= 2)
            {
                // V2 format: full params
                params.volume     = paramTree.getProperty ("volume", 0.0);
                params.panning    = paramTree.getProperty ("panning", 0);
                params.tune       = paramTree.getProperty ("tune", 0);
                params.finetune   = paramTree.getProperty ("finetune", 0);

                {
                    int ft = static_cast<int> (paramTree.getProperty ("filterType", 0));
                    if (ft >= 0 && ft <= static_cast<int> (InstrumentParams::FilterType::BandPass))
                        params.filterType = static_cast<InstrumentParams::FilterType> (ft);
                }
                params.cutoff     = paramTree.getProperty ("cutoff", 100);
                params.resonance  = paramTree.getProperty ("resonance", 0);

                params.overdrive  = paramTree.getProperty ("overdrive", 0);
                params.bitDepth   = paramTree.getProperty ("bitDepth", 16);
                params.reverbSend = paramTree.getProperty ("reverbSend", -100.0);
                params.delaySend  = paramTree.getProperty ("delaySend", -100.0);

                params.startPos   = paramTree.getProperty ("startPos", 0.0);
                params.endPos     = paramTree.getProperty ("endPos", 1.0);
                params.loopStart  = paramTree.getProperty ("loopStart", 0.0);
                params.loopEnd    = paramTree.getProperty ("loopEnd", 1.0);

                {
                    int pm = static_cast<int> (paramTree.getProperty ("playMode", 0));
                    if (pm >= 0 && pm <= static_cast<int> (InstrumentParams::PlayMode::Granular))
                        params.playMode = static_cast<InstrumentParams::PlayMode> (pm);
                }
                params.reversed   = paramTree.getProperty ("reversed", false);

                // wtWindow / wtPosition properties are ignored (wavetable mode removed)

                params.granularPosition = paramTree.getProperty ("grainPos", 0.0);
                params.granularLength   = paramTree.getProperty ("grainLen", 500);
                params.granularShape    = static_cast<InstrumentParams::GranShape> (
                    static_cast<int> (paramTree.getProperty ("grainShape", 1)));
                params.granularLoop     = static_cast<InstrumentParams::GranLoop> (
                    static_cast<int> (paramTree.getProperty ("grainLoop", 0)));

                // Slices
                juce::String sliceStr = paramTree.getProperty ("slices", "");
                if (sliceStr.isNotEmpty())
                {
                    auto tokens = juce::StringArray::fromTokens (sliceStr, ",", "");
                    for (auto& tok : tokens)
                        params.slicePoints.push_back (tok.getDoubleValue());
                }

                // Modulations
                for (int m = 0; m < paramTree.getNumChildren(); ++m)
                {
                    auto modTree = paramTree.getChild (m);
                    if (! modTree.hasType ("Mod")) continue;

                    int dest = modTree.getProperty ("dest", -1);
                    if (dest < 0 || dest >= InstrumentParams::kNumModDests) continue;

                    auto& mod = params.modulations[static_cast<size_t> (dest)];
                    mod.type     = static_cast<InstrumentParams::Modulation::Type> (
                        static_cast<int> (modTree.getProperty ("type", 0)));
                    mod.lfoShape = static_cast<InstrumentParams::Modulation::LFOShape> (
                        static_cast<int> (modTree.getProperty ("lfoShape", 2)));
                    mod.lfoSpeed = modTree.getProperty ("lfoSpeed", 24);
                    mod.lfoSpeedMode = static_cast<InstrumentParams::Modulation::LFOSpeedMode> (
                        static_cast<int> (modTree.getProperty ("lfoSpeedMode", 0)));
                    mod.lfoSpeedMs = modTree.getProperty ("lfoSpeedMs", 500);
                    mod.amount   = modTree.getProperty ("amount", 100);
                    mod.attackS  = modTree.getProperty ("attackS", 0.020);
                    mod.decayS   = modTree.getProperty ("decayS", 0.030);
                    mod.sustain  = modTree.getProperty ("sustain", 100);
                    mod.releaseS = modTree.getProperty ("releaseS", 0.050);
                    mod.modMode  = static_cast<InstrumentParams::Modulation::ModMode> (
                        static_cast<int> (modTree.getProperty ("modMode", 0)));
                }
            }
            else
            {
                // V1 format: legacy backward compatibility
                params.startPos = paramTree.getProperty ("startPos", 0.0);
                params.endPos   = paramTree.getProperty ("endPos", 1.0);
                params.reversed = paramTree.getProperty ("reversed", false);

                // Map old ADSR (ms) to Volume modulation envelope (seconds)
                double attackMs  = paramTree.getProperty ("attackMs", 5.0);
                double decayMs   = paramTree.getProperty ("decayMs", 50.0);
                double susLevel  = paramTree.getProperty ("sustainLevel", 1.0);
                double releaseMs = paramTree.getProperty ("releaseMs", 50.0);

                // Only create modulation if the old ADSR was non-default
                if (attackMs != 5.0 || decayMs != 50.0 || susLevel != 1.0 || releaseMs != 50.0)
                {
                    auto& volMod = params.modulations[static_cast<size_t> (InstrumentParams::ModDest::Volume)];
                    volMod.type     = InstrumentParams::Modulation::Type::Envelope;
                    volMod.attackS  = attackMs * 0.001;
                    volMod.decayS   = decayMs * 0.001;
                    volMod.sustain  = static_cast<int> (susLevel * 100.0);
                    volMod.releaseS = releaseMs * 0.001;
                }
            }

            instrumentParams[index] = params;
        }
    }

    // Arrangement (backward-compatible)
    arrangement.clear();
    auto arrTree = root.getChildWithName ("Arrangement");
    if (arrTree.isValid())
    {
        for (int i = 0; i < arrTree.getNumChildren(); ++i)
        {
            auto entryTree = arrTree.getChild (i);
            if (! entryTree.hasType ("Entry")) continue;

            int patIdx = entryTree.getProperty ("pattern", 0);
            int repeats = entryTree.getProperty ("repeats", 1);
            arrangement.addEntry (patIdx, repeats);
        }
    }

    // Track Layout (backward-compatible)
    trackLayout.resetToDefault();
    auto layoutTree = root.getChildWithName ("TrackLayout");
    if (layoutTree.isValid())
    {
        auto voTree = layoutTree.getChildWithName ("VisualOrder");
        if (voTree.isValid())
        {
            juce::String orderStr = voTree.getProperty ("values", "");
            auto tokens = juce::StringArray::fromTokens (orderStr, ",", "");
            if (tokens.size() == kNumTracks)
            {
                std::array<int, kNumTracks> order {};
                bool valid = true;
                std::set<int> seen;
                for (int i = 0; i < kNumTracks; ++i)
                {
                    int val = tokens[i].getIntValue();
                    if (val < 0 || val >= kNumTracks || seen.count (val))
                    {
                        valid = false;
                        break;
                    }
                    seen.insert (val);
                    order[static_cast<size_t> (i)] = val;
                }
                if (valid)
                    trackLayout.setVisualOrder (order);
            }
        }

        for (int i = 0; i < layoutTree.getNumChildren(); ++i)
        {
            auto nameTree = layoutTree.getChild (i);
            if (! nameTree.hasType ("TrackName")) continue;

            int idx = nameTree.getProperty ("index", -1);
            if (idx >= 0 && idx < kNumTracks)
                trackLayout.setTrackName (idx, nameTree.getProperty ("name", "").toString());
        }

        auto nmTree = layoutTree.getChildWithName ("NoteModes");
        if (nmTree.isValid())
        {
            juce::String modeStr = nmTree.getProperty ("values", "");
            auto tokens = juce::StringArray::fromTokens (modeStr, ",", "");
            if (tokens.size() == kNumTracks)
            {
                for (int i = 0; i < kNumTracks; ++i)
                    trackLayout.setTrackNoteMode (i, tokens[i].getIntValue() == 1
                                                         ? NoteMode::Release
                                                         : NoteMode::Kill);
            }
        }

        auto fxLaneTree = layoutTree.getChildWithName ("FxLaneCounts");
        if (fxLaneTree.isValid())
        {
            juce::String fxStr = fxLaneTree.getProperty ("values", "");
            auto tokens = juce::StringArray::fromTokens (fxStr, ",", "");
            if (tokens.size() == kNumTracks)
            {
                for (int i = 0; i < kNumTracks; ++i)
                    trackLayout.setTrackFxLaneCount (i, tokens[i].getIntValue());
            }
        }

        auto nlLaneTree = layoutTree.getChildWithName ("NoteLaneCounts");
        if (nlLaneTree.isValid())
        {
            juce::String nlStr = nlLaneTree.getProperty ("values", "");
            auto tokens = juce::StringArray::fromTokens (nlStr, ",", "");
            if (tokens.size() == kNumTracks)
            {
                for (int i = 0; i < kNumTracks; ++i)
                    trackLayout.setTrackNoteLaneCount (i, tokens[i].getIntValue());
            }
        }

        auto mfxTree = layoutTree.getChildWithName ("MasterFxLanes");
        if (mfxTree.isValid())
            trackLayout.setMasterFxLaneCount (mfxTree.getProperty ("count", 1));

        for (int i = 0; i < layoutTree.getNumChildren(); ++i)
        {
            auto groupTree = layoutTree.getChild (i);
            if (! groupTree.hasType ("Group")) continue;

            TrackGroup group;
            group.name = groupTree.getProperty ("name", "Group").toString();
            group.colour = juce::Colour::fromString (groupTree.getProperty ("colour", "ff5c8abf").toString());

            for (int j = 0; j < groupTree.getNumChildren(); ++j)
            {
                auto trackTree = groupTree.getChild (j);
                if (trackTree.hasType ("Track"))
                {
                    int idx = trackTree.getProperty ("index", -1);
                    if (idx >= 0 && idx < kNumTracks)
                        group.trackIndices.push_back (idx);
                }
            }

            if (! group.trackIndices.empty())
                trackLayout.addGroup (std::move (group));
        }
    }

    // Mixer state (V4+)
    mixerState.reset();
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

    // Plugin instrument slots (V7+)
    if (pluginSlots != nullptr)
    {
        pluginSlots->clear();
        auto pluginSlotsTree = root.getChildWithName ("PluginInstrumentSlots");
        if (pluginSlotsTree.isValid())
        {
            for (int i = 0; i < pluginSlotsTree.getNumChildren(); ++i)
            {
                auto slotTree = pluginSlotsTree.getChild (i);
                if (! slotTree.hasType ("PluginSlot")) continue;

                int index = slotTree.getProperty ("index", -1);
                if (index < 0 || index >= 256) continue;

                InstrumentSlotInfo info;
                info.sourceType = InstrumentSourceType::PluginInstrument;
                info.ownerTrack = slotTree.getProperty ("ownerTrack", -1);
                info.pluginDescription.name = slotTree.getProperty ("pluginName", "").toString();
                info.pluginDescription.fileOrIdentifier = slotTree.getProperty ("pluginId", "").toString();
                info.pluginDescription.pluginFormatName = slotTree.getProperty ("pluginFormat", "").toString();
                info.pluginDescription.uniqueId = slotTree.getProperty ("pluginUid", 0);
                info.pluginDescription.deprecatedUid = slotTree.getProperty ("pluginDeprecatedUid", 0);
                info.pluginDescription.manufacturerName = slotTree.getProperty ("pluginManufacturer", "").toString();
                info.pluginDescription.category = slotTree.getProperty ("pluginCategory", "").toString();
                info.pluginDescription.isInstrument = slotTree.getProperty ("isInstrument", true);

                // Restore plugin state (first child ValueTree if present)
                if (slotTree.getNumChildren() > 0)
                    info.pluginState = slotTree.getChild (0).createCopy();

                (*pluginSlots)[index] = info;
            }
        }
    }

    // Send effects params (V4+)
    delayParams = DelayParams {};
    reverbParams = ReverbParams {};
    auto sendTree = root.getChildWithName ("SendEffects");
    if (sendTree.isValid())
    {
        auto delayTree = sendTree.getChildWithName ("Delay");
        if (delayTree.isValid())
        {
            delayParams.time         = delayTree.getProperty ("time", 250.0);
            delayParams.syncDivision = delayTree.getProperty ("syncDiv", 4);
            delayParams.bpmSync      = delayTree.getProperty ("bpmSync", true);
            delayParams.dotted       = delayTree.getProperty ("dotted", false);
            delayParams.feedback     = delayTree.getProperty ("feedback", 40.0);
            delayParams.filterType   = delayTree.getProperty ("filterType", 0);
            delayParams.filterCutoff = delayTree.getProperty ("filterCutoff", 80.0);
            delayParams.wet          = delayTree.getProperty ("wet", 50.0);
            delayParams.stereoWidth  = delayTree.getProperty ("stereoWidth", 50.0);
        }

        auto reverbTree = sendTree.getChildWithName ("Reverb");
        if (reverbTree.isValid())
        {
            reverbParams.roomSize = reverbTree.getProperty ("roomSize", 50.0);
            reverbParams.decay    = reverbTree.getProperty ("decay", 50.0);
            reverbParams.damping  = reverbTree.getProperty ("damping", 50.0);
            reverbParams.preDelay = reverbTree.getProperty ("preDelay", 10.0);
            reverbParams.wet      = reverbTree.getProperty ("wet", 30.0);
        }
    }

    // Patterns
    patternData.clearAllPatterns();
    auto patterns = root.getChildWithName ("Patterns");
    if (patterns.isValid() && patterns.getNumChildren() > 0)
    {
        // clearAllPatterns() keeps one default pattern at index 0, so fill it first.
        auto firstPatTree = patterns.getChild (0);
        valueTreeToPattern (firstPatTree, patternData.getPattern (0), version);
        patternData.getPattern (0).ensureMasterFxSlots (trackLayout.getMasterFxLaneCount());

        for (int i = 1; i < patterns.getNumChildren(); ++i)
        {
            auto patTree = patterns.getChild (i);
            int numRows = patTree.getProperty ("numRows", 64);
            patternData.addPattern (numRows);
            auto& pat = patternData.getPattern (patternData.getNumPatterns() - 1);
            valueTreeToPattern (patTree, pat, version);
            pat.ensureMasterFxSlots (trackLayout.getMasterFxLaneCount());
        }
    }

    int currentPat = settings.isValid() ? static_cast<int> (settings.getProperty ("currentPattern", 0)) : 0;
    patternData.setCurrentPattern (juce::jlimit (0, patternData.getNumPatterns() - 1, currentPat));

    return {};
}

juce::ValueTree ProjectSerializer::patternToValueTree (const Pattern& pattern, int /*index*/)
{
    juce::ValueTree patTree ("Pattern");
    patTree.setProperty ("name", pattern.name, nullptr);
    patTree.setProperty ("numRows", pattern.numRows, nullptr);

    for (int r = 0; r < pattern.numRows; ++r)
    {
        bool hasData = false;
        for (int t = 0; t < kNumTracks; ++t)
        {
            if (! pattern.getCell (r, t).isEmpty())
            {
                hasData = true;
                break;
            }
        }

        // Check master lane too
        if (! hasData && r < static_cast<int> (pattern.masterFxRows.size()))
        {
            for (auto& slot : pattern.masterFxRows[static_cast<size_t> (r)])
                if (! slot.isEmpty()) { hasData = true; break; }
        }

        if (! hasData) continue;

        juce::ValueTree rowTree ("Row");
        rowTree.setProperty ("index", r, nullptr);

        for (int t = 0; t < kNumTracks; ++t)
        {
            const auto& cell = pattern.getCell (r, t);
            if (cell.isEmpty()) continue;

            juce::ValueTree cellTree ("Cell");
            cellTree.setProperty ("track", t, nullptr);
            cellTree.setProperty ("note", cell.note, nullptr);
            cellTree.setProperty ("inst", cell.instrument, nullptr);
            cellTree.setProperty ("vol", cell.volume, nullptr);

            // Save extra note lanes (lane 1+)
            for (int nl = 0; nl < static_cast<int> (cell.extraNoteLanes.size()); ++nl)
            {
                const auto& slot = cell.extraNoteLanes[static_cast<size_t> (nl)];
                if (slot.isEmpty()) continue;

                juce::ValueTree nlTree ("NoteLane");
                nlTree.setProperty ("lane", nl + 1, nullptr);
                nlTree.setProperty ("note", slot.note, nullptr);
                nlTree.setProperty ("inst", slot.instrument, nullptr);
                nlTree.setProperty ("vol", slot.volume, nullptr);
                cellTree.addChild (nlTree, -1, nullptr);
            }

            // Save first FX slot
            if (cell.getNumFxSlots() > 0)
            {
                const auto& slot0 = cell.getFxSlot (0);
                auto letter = slot0.getCommandLetter();
                if (letter != '\0')
                {
                    cellTree.setProperty ("fxc", juce::String::charToString (letter), nullptr);
                    cellTree.setProperty ("fxp", slot0.fxParam, nullptr);
                }
            }

            // Save additional FX slots (index 1+)
            for (int fxi = 1; fxi < cell.getNumFxSlots(); ++fxi)
            {
                const auto& slot = cell.getFxSlot (fxi);
                auto letter = slot.getCommandLetter();
                if (letter == '\0') continue;

                juce::ValueTree fxTree ("FxSlot");
                fxTree.setProperty ("lane", fxi, nullptr);
                fxTree.setProperty ("fxp", slot.fxParam, nullptr);
                fxTree.setProperty ("fxc", juce::String::charToString (letter), nullptr);
                cellTree.addChild (fxTree, -1, nullptr);
            }

            rowTree.addChild (cellTree, -1, nullptr);
        }

        // Save master FX slots for this row
        if (r < static_cast<int> (pattern.masterFxRows.size()))
        {
            auto& mfxRow = pattern.masterFxRows[static_cast<size_t> (r)];
            for (int lane = 0; lane < static_cast<int> (mfxRow.size()); ++lane)
            {
                auto& slot = mfxRow[static_cast<size_t> (lane)];
                auto letter = slot.getCommandLetter();
                if (letter == '\0') continue;

                juce::ValueTree mfxTree ("MasterFx");
                mfxTree.setProperty ("lane", lane, nullptr);
                mfxTree.setProperty ("fxp", slot.fxParam, nullptr);
                mfxTree.setProperty ("fxc", juce::String::charToString (letter), nullptr);
                rowTree.addChild (mfxTree, -1, nullptr);
            }
        }

        patTree.addChild (rowTree, -1, nullptr);
    }

    // Automation data (Phase 5)
    if (! pattern.automationData.isEmpty())
    {
        juce::ValueTree autoTree ("Automation");
        for (const auto& lane : pattern.automationData.lanes)
        {
            if (lane.isEmpty())
                continue;

            juce::ValueTree laneTree ("Lane");
            laneTree.setProperty ("pluginId", lane.pluginId, nullptr);
            laneTree.setProperty ("paramId", lane.parameterId, nullptr);
            laneTree.setProperty ("track", lane.owningTrack, nullptr);

            for (const auto& point : lane.points)
            {
                juce::ValueTree pointTree ("Point");
                pointTree.setProperty ("row", point.row, nullptr);
                pointTree.setProperty ("value", static_cast<double> (point.value), nullptr);
                pointTree.setProperty ("curve", static_cast<int> (point.curveType), nullptr);
                laneTree.addChild (pointTree, -1, nullptr);
            }

            autoTree.addChild (laneTree, -1, nullptr);
        }
        patTree.addChild (autoTree, -1, nullptr);
    }

    return patTree;
}

void ProjectSerializer::valueTreeToPattern (const juce::ValueTree& tree, Pattern& pattern, int /*version*/)
{
    pattern.name = tree.getProperty ("name", "Pattern").toString();
    int numRows = tree.getProperty ("numRows", 64);
    pattern.resize (numRows);
    pattern.clear();
    pattern.ensureMasterFxSlots (1);

    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto rowTree = tree.getChild (i);
        if (! rowTree.hasType ("Row")) continue;

        int row = rowTree.getProperty ("index", -1);
        if (row < 0 || row >= numRows) continue;

        for (int j = 0; j < rowTree.getNumChildren(); ++j)
        {
            auto cellTree = rowTree.getChild (j);
            if (! cellTree.hasType ("Cell")) continue;

            int track = cellTree.getProperty ("track", -1);
            if (track < 0 || track >= kNumTracks) continue;

            Cell cell;
            cell.note = cellTree.getProperty ("note", -1);
            cell.instrument = cellTree.getProperty ("inst", -1);
            cell.volume = cellTree.getProperty ("vol", -1);

            // Load extra note lanes and FX slots from child nodes
            for (int ci = 0; ci < cellTree.getNumChildren(); ++ci)
            {
                auto childTree = cellTree.getChild (ci);

                if (childTree.hasType ("NoteLane"))
                {
                    int lane = childTree.getProperty ("lane", -1);
                    if (lane < 1) continue;
                    NoteSlot slot;
                    slot.note = childTree.getProperty ("note", -1);
                    slot.instrument = childTree.getProperty ("inst", -1);
                    slot.volume = childTree.getProperty ("vol", -1);
                    cell.setNoteLane (lane, slot);
                }
                else if (childTree.hasType ("FxSlot"))
                {
                    int lane = childTree.getProperty ("lane", -1);
                    if (lane < 1) continue;
                    auto& slot = cell.getFxSlot (lane);
                    int fxp = childTree.getProperty ("fxp", 0);
                    auto fxToken = childTree.getProperty ("fxc", "").toString();
                    if (fxToken.isNotEmpty())
                        slot.setSymbolicCommand (static_cast<char> (fxToken[0]), fxp);
                }
            }

            // Load first FX slot (inline on Cell node)
            int fxp0 = cellTree.getProperty ("fxp", 0);
            auto fxToken0 = cellTree.getProperty ("fxc", "").toString();
            auto& firstSlot = cell.getFxSlot (0);
            if (fxToken0.isNotEmpty())
                firstSlot.setSymbolicCommand (static_cast<char> (fxToken0[0]), fxp0);

            pattern.setCell (row, track, cell);
        }

        for (int j = 0; j < rowTree.getNumChildren(); ++j)
        {
            auto mfxTree = rowTree.getChild (j);
            if (! mfxTree.hasType ("MasterFx")) continue;

            int lane = mfxTree.getProperty ("lane", -1);
            if (lane < 0) continue;

            int fxp = mfxTree.getProperty ("fxp", 0);
            auto fxToken = mfxTree.getProperty ("fxc", "").toString();

            auto& slot = pattern.getMasterFxSlot (row, lane);
            if (fxToken.isNotEmpty())
                slot.setSymbolicCommand (static_cast<char> (fxToken[0]), fxp);
        }
    }

    // Automation data (Phase 5)
    pattern.automationData = PatternAutomationData {};
    auto autoTree = tree.getChildWithName ("Automation");
    if (autoTree.isValid())
    {
        for (int i = 0; i < autoTree.getNumChildren(); ++i)
        {
            auto laneTree = autoTree.getChild (i);
            if (! laneTree.hasType ("Lane"))
                continue;

            AutomationLane lane;
            lane.pluginId = laneTree.getProperty ("pluginId", "").toString();
            lane.parameterId = laneTree.getProperty ("paramId", -1);
            lane.owningTrack = laneTree.getProperty ("track", -1);

            for (int pi = 0; pi < laneTree.getNumChildren(); ++pi)
            {
                auto pointTree = laneTree.getChild (pi);
                if (! pointTree.hasType ("Point"))
                    continue;

                AutomationPoint point;
                point.row = pointTree.getProperty ("row", 0);
                point.value = static_cast<float> (static_cast<double> (pointTree.getProperty ("value", 0.5)));
                point.curveType = static_cast<AutomationCurveType> (
                    static_cast<int> (pointTree.getProperty ("curve", 0)));
                lane.points.push_back (point);
            }

            lane.sortPoints();
            pattern.automationData.lanes.push_back (std::move (lane));
        }
    }
}

//==============================================================================
// Global browser directory persistence
//==============================================================================

static juce::File getGlobalPrefsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("TrackerAdjust")
               .getChildFile ("prefs.xml");
}

void ProjectSerializer::saveGlobalBrowserDir (const juce::String& dir)
{
    auto prefsFile = getGlobalPrefsFile();
    if (! prefsFile.getParentDirectory().createDirectory())
        return;

    juce::ValueTree root ("TrackerAdjustPrefs");

    // Load existing prefs if any
    if (prefsFile.existsAsFile())
    {
        auto xml = juce::XmlDocument::parse (prefsFile);
        if (xml != nullptr)
        {
            auto loaded = juce::ValueTree::fromXml (*xml);
            if (loaded.isValid())
                root = loaded;
        }
    }

    root.setProperty ("browserDir", dir, nullptr);

    if (auto xml = root.createXml())
        xml->writeTo (prefsFile);
}

juce::String ProjectSerializer::loadGlobalBrowserDir()
{
    auto prefsFile = getGlobalPrefsFile();
    if (! prefsFile.existsAsFile())
        return {};

    auto xml = juce::XmlDocument::parse (prefsFile);
    if (xml == nullptr)
        return {};

    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.isValid())
        return {};
    return root.getProperty ("browserDir", "").toString();
}

//==============================================================================
// Global plugin scan path persistence
//==============================================================================

void ProjectSerializer::saveGlobalPluginScanPaths (const juce::StringArray& paths)
{
    auto prefsFile = getGlobalPrefsFile();
    if (! prefsFile.getParentDirectory().createDirectory())
        return;

    juce::ValueTree root ("TrackerAdjustPrefs");

    // Load existing prefs if any
    if (prefsFile.existsAsFile())
    {
        auto xml = juce::XmlDocument::parse (prefsFile);
        if (xml != nullptr)
        {
            auto loaded = juce::ValueTree::fromXml (*xml);
            if (loaded.isValid())
                root = loaded;
        }
    }

    // Remove any existing scan paths child
    auto existing = root.getChildWithName ("PluginScanPaths");
    if (existing.isValid())
        root.removeChild (existing, nullptr);

    // Add new scan paths
    juce::ValueTree scanPathsTree ("PluginScanPaths");
    for (auto& path : paths)
    {
        juce::ValueTree pathTree ("Path");
        pathTree.setProperty ("dir", path, nullptr);
        scanPathsTree.addChild (pathTree, -1, nullptr);
    }
    root.addChild (scanPathsTree, -1, nullptr);

    if (auto xml = root.createXml())
        xml->writeTo (prefsFile);
}

juce::StringArray ProjectSerializer::loadGlobalPluginScanPaths()
{
    auto prefsFile = getGlobalPrefsFile();
    if (! prefsFile.existsAsFile())
        return {};

    auto xml = juce::XmlDocument::parse (prefsFile);
    if (xml == nullptr)
        return {};

    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.isValid())
        return {};

    auto scanPathsTree = root.getChildWithName ("PluginScanPaths");
    if (! scanPathsTree.isValid())
        return {};

    juce::StringArray paths;
    for (int i = 0; i < scanPathsTree.getNumChildren(); ++i)
    {
        auto pathTree = scanPathsTree.getChild (i);
        auto dir = pathTree.getProperty ("dir", "").toString();
        if (dir.isNotEmpty())
            paths.add (dir);
    }

    return paths;
}
