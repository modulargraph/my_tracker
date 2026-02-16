#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include "InstrumentParams.h"
#include "TrackerSamplerPlugin.h"

namespace te = tracktion;

class InstrumentEffectsPlugin;

class SimpleSampler
{
public:
    SimpleSampler() = default;

    // Load a sample file into a track's TrackerSamplerPlugin
    juce::String loadSample (te::AudioTrack& track, const juce::File& sampleFile, int instrumentIndex);

    // Get the sample file loaded for a given instrument index
    juce::File getSampleFile (int instrumentIndex) const;

    // Preview a note on a specific track
    void playNote (te::AudioTrack& track, int midiNote);
    void stopNote (te::AudioTrack& track);

    // Get all loaded samples for serialization
    const std::map<int, juce::File>& getLoadedSamples() const { return loadedSamples; }
    void clearLoadedSamples() { loadedSamples.clear(); instrumentParams.clear(); sampleBanks.clear(); }

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

private:
    std::map<int, juce::File> loadedSamples;
    std::map<int, InstrumentParams> instrumentParams;
    std::map<int, std::shared_ptr<SampleBank>> sampleBanks;

    TrackerSamplerPlugin* getOrCreateTrackerSampler (te::AudioTrack& track);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleSampler)
};
