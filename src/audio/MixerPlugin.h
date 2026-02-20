#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include "MixerState.h"
#include "SendBuffers.h"

namespace te = tracktion;

class MixerPlugin : public te::Plugin
{
public:
    MixerPlugin (te::PluginCreationInfo);
    ~MixerPlugin() override;

    static const char* getPluginName()  { return "MixerChannel"; }
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
    void setSendBuffers (SendBuffers* b) { sendBuffers = b; }

private:
    juce::SpinLock mixStateLock;
    TrackMixState sharedMixState;
    TrackMixState localMixState;
    SendBuffers* sendBuffers = nullptr;

    // EQ filters (3-band)
    juce::dsp::IIR::Filter<float> eqLowL, eqLowR;
    juce::dsp::IIR::Filter<float> eqMidL, eqMidR;
    juce::dsp::IIR::Filter<float> eqHighL, eqHighR;

    // Compressor state
    float compEnvelope = 0.0f;

    // Smoothed gain
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedGainL { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedGainR { 1.0f };

    void processEQ (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    void processCompressor (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    void processVolumeAndPan (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    void processSends (const juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerPlugin)
};
