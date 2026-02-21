#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include "MixerState.h"

namespace te = tracktion;

/**
 * ChannelStripPlugin handles EQ and Compressor processing for a single track.
 * This is the first half of the old MixerPlugin chain, split out so that
 * external insert plugins can be placed between the channel strip (EQ+Comp)
 * and the track output (Sends+Pan+Volume+Meter).
 *
 * Signal chain position:
 *   Sampler -> InstrumentEffects -> ChannelStrip -> [Insert Plugins] -> TrackOutput
 */
class ChannelStripPlugin : public te::Plugin
{
public:
    ChannelStripPlugin (te::PluginCreationInfo);
    ~ChannelStripPlugin() override;

    static const char* getPluginName()  { return "ChannelStrip"; }
    static const char* xmlTypeName;

    juce::String getName() const override               { return getPluginName(); }
    juce::String getPluginType() override               { return xmlTypeName; }
    bool takesMidiInput() override                      { return false; }
    bool takesAudioInput() override                     { return true; }
    bool isSynth() override                             { return false; }
    bool producesAudioWhenNoAudioInput() override       { return false; }
    int getNumOutputChannelsGivenInputs (int numInputChannels) override { return juce::jmin (numInputChannels, 2); }

    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override;
    void applyToBuffer (const te::PluginRenderContext&) override;

    juce::String getSelectableDescription() override    { return getName(); }
    bool needsConstantBufferSize() override             { return false; }

    void setMixState (const TrackMixState& s);

private:
    juce::SpinLock mixStateLock;
    TrackMixState sharedMixState;
    TrackMixState localMixState;

    // EQ filters (3-band)
    juce::dsp::IIR::Filter<float> eqLowL, eqLowR;
    juce::dsp::IIR::Filter<float> eqMidL, eqMidR;
    juce::dsp::IIR::Filter<float> eqHighL, eqHighR;

    // Compressor state
    float compEnvelope = 0.0f;

    void processEQ (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    void processCompressor (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStripPlugin)
};
