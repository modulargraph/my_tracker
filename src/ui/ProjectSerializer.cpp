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
                                            const juce::String& browserDir)
{
    juce::ValueTree root ("TrackerAdjustProject");
    root.setProperty ("version", 5, nullptr);

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

    // Samples
    juce::ValueTree samples ("Samples");
    for (auto& [index, sampleFile] : loadedSamples)
    {
        juce::ValueTree sample ("Sample");
        sample.setProperty ("index", index, nullptr);
        sample.setProperty ("path", sampleFile.getRelativePathFrom (file.getParentDirectory()), nullptr);
        sample.setProperty ("absPath", sampleFile.getFullPathName(), nullptr);
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

    // Send effects params
    {
        juce::ValueTree sendTree ("SendEffects");

        juce::ValueTree delayTree ("Delay");
        delayTree.setProperty ("time", delayParams.time, nullptr);
        delayTree.setProperty ("syncDiv", delayParams.syncDivision, nullptr);
        delayTree.setProperty ("bpmSync", delayParams.bpmSync, nullptr);
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
                                              juce::String* browserDir)
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

            int fxp0 = cellTree.getProperty ("fxp", 0);
            auto fxToken0 = cellTree.getProperty ("fxc", "").toString();
            auto& firstSlot = cell.getFxSlot (0);
            if (fxToken0.isNotEmpty())
                firstSlot.setSymbolicCommand (static_cast<char> (fxToken0[0]), fxp0);

            // Load additional FX slots
            for (int fxi = 0; fxi < cellTree.getNumChildren(); ++fxi)
            {
                auto fxSlotTree = cellTree.getChild (fxi);
                if (! fxSlotTree.hasType ("FxSlot")) continue;

                int lane = fxSlotTree.getProperty ("lane", -1);
                if (lane < 1) continue;

                auto& slot = cell.getFxSlot (lane);
                int fxp = fxSlotTree.getProperty ("fxp", 0);
                auto fxToken = fxSlotTree.getProperty ("fxc", "").toString();
                if (fxToken.isNotEmpty())
                    slot.setSymbolicCommand (static_cast<char> (fxToken[0]), fxp);
            }

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
