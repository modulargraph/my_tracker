#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include "InstrumentParams.h"

namespace te = tracktion;

// SimpleSampler manages loading samples into Tracktion's SamplerPlugin instances.
// Each track gets one SamplerPlugin with a sample mapped to a range of MIDI notes.
class SimpleSampler
{
public:
    SimpleSampler() = default;

    // Load a sample file into a track's SamplerPlugin
    juce::String loadSample (te::AudioTrack& track, const juce::File& sampleFile, int instrumentIndex);

    // Get the sample file loaded for a given instrument index
    juce::File getSampleFile (int instrumentIndex) const;

    // Preview a note on a specific track
    void playNote (te::AudioTrack& track, int midiNote);
    void stopNote (te::AudioTrack& track);

    // Get all loaded samples for serialization
    const std::map<int, juce::File>& getLoadedSamples() const { return loadedSamples; }
    void clearLoadedSamples() { loadedSamples.clear(); instrumentParams.clear(); }

    // Instrument params (ADSR, start/end, reverse)
    InstrumentParams getParams (int instrumentIndex) const;
    void setParams (int instrumentIndex, const InstrumentParams& params);
    const std::map<int, InstrumentParams>& getAllParams() const { return instrumentParams; }
    void clearAllParams() { instrumentParams.clear(); }

    // Apply params: read original file, process (crop/reverse/ADSR), write temp WAV, reload
    juce::String applyParams (te::AudioTrack& track, int instrumentIndex);

private:
    std::map<int, juce::File> loadedSamples;
    std::map<int, InstrumentParams> instrumentParams;

    te::SamplerPlugin* getOrCreateSampler (te::AudioTrack& track);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleSampler)
};
