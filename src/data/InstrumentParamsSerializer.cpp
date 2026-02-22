#include "InstrumentParamsSerializer.h"

void InstrumentParamsSerializer::save (juce::ValueTree& root, const std::map<int, InstrumentParams>& instrumentParams)
{
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
}

void InstrumentParamsSerializer::load (const juce::ValueTree& root, std::map<int, InstrumentParams>& instrumentParams, int version)
{
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
}
