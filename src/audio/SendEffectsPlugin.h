#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include "SendBuffers.h"
#include "SendEffectsParams.h"

namespace te = tracktion;

class SendEffectsPlugin : public te::Plugin
{
public:
    SendEffectsPlugin (te::PluginCreationInfo);
    ~SendEffectsPlugin() override;

    static const char* getPluginName()  { return "SendEffects"; }
    static const char* xmlTypeName;

    juce::String getName() const override               { return getPluginName(); }
    juce::String getPluginType() override               { return xmlTypeName; }
    bool takesMidiInput() override                      { return false; }
    bool takesAudioInput() override                     { return true; }
    bool isSynth() override                             { return false; }
    bool producesAudioWhenNoAudioInput() override       { return true; }
    int getNumOutputChannelsGivenInputs (int numInputChannels) override { return juce::jmin (numInputChannels, 2); }

    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override;
    void applyToBuffer (const te::PluginRenderContext&) override;

    juce::String getSelectableDescription() override    { return getName(); }
    bool needsConstantBufferSize() override             { return false; }

    // Shared send buffers (owned by SimpleSampler, set during setup)
    void setSendBuffers (SendBuffers* buffers) { sendBuffers = buffers; }

    // Global effect parameters (read from UI thread, consumed on audio thread)
    DelayParams delayParams;
    ReverbParams reverbParams;

private:
    SendBuffers* sendBuffers = nullptr;

    // Delay line
    static constexpr int kMaxDelaySamples = 192000; // ~4 seconds at 48kHz
    juce::AudioBuffer<float> delayLine;
    int delayWritePos = 0;
    juce::dsp::StateVariableTPTFilter<float> delayFilter;
    bool delayFilterInitialized = false;

    // Reverb
    juce::Reverb reverb;
    juce::AudioBuffer<float> preDelayBuffer;
    int preDelayWritePos = 0;
    int preDelayMaxSamples = 0;

    // Scratch buffers
    juce::AudioBuffer<float> delayScratch;
    juce::AudioBuffer<float> reverbScratch;

    // Processing helpers
    void processDelay (juce::AudioBuffer<float>& output, int startSample, int numSamples);
    void processReverb (juce::AudioBuffer<float>& output, int startSample, int numSamples);
    int getDelayTimeSamples() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SendEffectsPlugin)
};
