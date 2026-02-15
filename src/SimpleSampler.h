#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include "InstrumentParams.h"

namespace te = tracktion;

class InstrumentEffectsPlugin;

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

    // Instrument params
    InstrumentParams getParams (int instrumentIndex) const;
    void setParams (int instrumentIndex, const InstrumentParams& params);
    const std::map<int, InstrumentParams>& getAllParams() const { return instrumentParams; }
    void clearAllParams() { instrumentParams.clear(); }

    // Apply params: process sample based on playback mode, reload into sampler
    juce::String applyParams (te::AudioTrack& track, int instrumentIndex);

    // Plugin chain management
    InstrumentEffectsPlugin* getOrCreateEffectsPlugin (te::AudioTrack& track, int instrumentIndex);
    void setupPluginChain (te::AudioTrack& track, int instrumentIndex);

private:
    std::map<int, juce::File> loadedSamples;
    std::map<int, InstrumentParams> instrumentParams;

    te::SamplerPlugin* getOrCreateSampler (te::AudioTrack& track);

    // Playback mode processing
    juce::String applyOneShotMode (te::AudioTrack& track, int instrumentIndex);
    juce::String applyForwardLoopMode (te::AudioTrack& track, int instrumentIndex);
    juce::String applyBackwardLoopMode (te::AudioTrack& track, int instrumentIndex);
    juce::String applyPingpongLoopMode (te::AudioTrack& track, int instrumentIndex);
    juce::String applySliceMode (te::AudioTrack& track, int instrumentIndex);
    juce::String applyBeatSliceMode (te::AudioTrack& track, int instrumentIndex);
    juce::String applyGranularMode (te::AudioTrack& track, int instrumentIndex);

    // Helper: read a sample file region into a buffer, optionally reversed
    struct SampleData
    {
        juce::AudioBuffer<float> buffer;
        double sampleRate = 44100.0;
        int numChannels = 1;
    };
    SampleData readSampleRegion (const juce::File& file, double startNorm, double endNorm, bool reverse);

    // Helper: write buffer to temp WAV and load into sampler
    juce::String writeTempAndLoad (te::AudioTrack& track, int instrumentIndex,
                                   const juce::AudioBuffer<float>& buffer, double sampleRate,
                                   int rootNote = 60, int lowNote = 0, int highNote = 127,
                                   bool openEnded = true);

    // Helper: write buffer to temp WAV file
    juce::File writeTempWav (int instrumentIndex, const juce::String& suffix,
                             const juce::AudioBuffer<float>& buffer, double sampleRate);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleSampler)
};
