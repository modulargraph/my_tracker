#pragma once

#include <atomic>
#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include "InstrumentParams.h"
#include "TrackerSamplerPlugin.h"

namespace te = tracktion;

class InstrumentEffectsPlugin;

// Shared global modulation state for an instrument (used when ModMode == Global)
struct GlobalModState
{
    // Per-destination envelope state (atomic for audio-thread safety)
    struct AtomicEnvState
    {
        std::atomic<int> stage { 0 };    // 0=Idle, 1=Attack, 2=Decay, 3=Sustain, 4=Release
        std::atomic<float> level { 0.0f };
    };
    std::array<AtomicEnvState, InstrumentParams::kNumModDests> envStates {};

    // Track how many notes are active across all tracks using this instrument
    std::atomic<int> activeNoteCount { 0 };

    // Deduplication: only advance envelopes once per audio block
    std::atomic<uint64_t> lastProcessedBlock { 0 };
};

class SimpleSampler
{
public:
    SimpleSampler() = default;

    // Load a sample into the bank/maps for an instrument (no track config)
    juce::String loadInstrumentSample (const juce::File& sampleFile, int instrumentIndex);

    // Load a sample and configure a specific track's plugin
    juce::String loadSample (te::AudioTrack& track, const juce::File& sampleFile, int instrumentIndex);

    // Get the sample file loaded for a given instrument index
    juce::File getSampleFile (int instrumentIndex) const;

    // Preview a note on a specific track
    void playNote (te::AudioTrack& track, int midiNote);
    void stopNote (te::AudioTrack& track);

    // Get all loaded samples for serialization
    const std::map<int, juce::File>& getLoadedSamples() const { return loadedSamples; }
    void clearLoadedSamples() { loadedSamples.clear(); instrumentParams.clear(); sampleBanks.clear(); globalModStates.clear(); }

    // Instrument params
    InstrumentParams getParams (int instrumentIndex) const;
    void setParams (int instrumentIndex, const InstrumentParams& params);
    const std::map<int, InstrumentParams>& getAllParams() const { return instrumentParams; }
    void clearAllParams() { instrumentParams.clear(); }

    // Apply params to sampler plugin (no file I/O, just updates plugin state)
    juce::String applyParams (te::AudioTrack& track, int instrumentIndex);

    // Plugin chain management
    InstrumentEffectsPlugin* getOrCreateEffectsPlugin (te::AudioTrack& track, int instrumentIndex);
    void setupPluginChain (te::AudioTrack& track, int instrumentIndex);

    // Sample bank access
    std::shared_ptr<const SampleBank> getSampleBank (int instrumentIndex) const;

    // Global modulation state (shared across tracks for same instrument)
    GlobalModState* getOrCreateGlobalModState (int instrumentIndex);

private:
    std::map<int, juce::File> loadedSamples;
    std::map<int, InstrumentParams> instrumentParams;
    std::map<int, std::shared_ptr<SampleBank>> sampleBanks;
    std::map<int, std::unique_ptr<GlobalModState>> globalModStates;

    TrackerSamplerPlugin* getOrCreateTrackerSampler (te::AudioTrack& track);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleSampler)
};
