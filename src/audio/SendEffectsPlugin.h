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

    // Thread-safe parameter setters (called from UI thread)
    void setDelayParams (const DelayParams& params)
    {
        const juce::SpinLock::ScopedLockType lock (paramLock);
        pendingDelayParams = params;
    }
    void setReverbParams (const ReverbParams& params)
    {
        const juce::SpinLock::ScopedLockType lock (paramLock);
        pendingReverbParams = params;
    }
    DelayParams getDelayParams() const
    {
        const juce::SpinLock::ScopedLockType lock (paramLock);
        return pendingDelayParams;
    }
    ReverbParams getReverbParams() const
    {
        const juce::SpinLock::ScopedLockType lock (paramLock);
        return pendingReverbParams;
    }

private:
    SendBuffers* sendBuffers = nullptr;

    // Thread-safe param exchange: UI writes pending, audio copies to active
    mutable juce::SpinLock paramLock;
    DelayParams pendingDelayParams;
    ReverbParams pendingReverbParams;
    DelayParams activeDelayParams;
    ReverbParams activeReverbParams;

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

    // Scratch buffer
    juce::AudioBuffer<float> reverbScratch;

    // Processing helpers
    void processDelay (juce::AudioBuffer<float>& output, int startSample, int numSamples);
    void processReverb (juce::AudioBuffer<float>& output, int startSample, int numSamples);
    int getDelayTimeSamples() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SendEffectsPlugin)
};
