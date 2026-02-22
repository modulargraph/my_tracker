#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include "SendBuffers.h"
#include "SendEffectsParams.h"
#include "MixerState.h"

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

    // Mixer state pointer for send return and master processing
    void setMixerState (MixerState* state) { mixerStatePtr = state; }

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

    // Master peak metering
    float getMasterPeakLevel() const { return masterPeakLevel.load (std::memory_order_relaxed); }
    void resetMasterPeak() { masterPeakLevel.store (0.0f, std::memory_order_relaxed); }

private:
    SendBuffers* sendBuffers = nullptr;
    MixerState* mixerStatePtr = nullptr;

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
    juce::AudioBuffer<float> delayScratch;
    juce::AudioBuffer<float> reverbInputScratch;
    juce::AudioBuffer<float> reverbScratch;
    juce::AudioBuffer<float> delayReturnScratch;
    juce::AudioBuffer<float> reverbReturnScratch;

    // Send return EQ filters
    juce::dsp::IIR::Filter<float> delayReturnEqLowL, delayReturnEqLowR;
    juce::dsp::IIR::Filter<float> delayReturnEqMidL, delayReturnEqMidR;
    juce::dsp::IIR::Filter<float> delayReturnEqHighL, delayReturnEqHighR;
    juce::dsp::IIR::Filter<float> reverbReturnEqLowL, reverbReturnEqLowR;
    juce::dsp::IIR::Filter<float> reverbReturnEqMidL, reverbReturnEqMidR;
    juce::dsp::IIR::Filter<float> reverbReturnEqHighL, reverbReturnEqHighR;

    // Master EQ filters
    juce::dsp::IIR::Filter<float> masterEqLowL, masterEqLowR;
    juce::dsp::IIR::Filter<float> masterEqMidL, masterEqMidR;
    juce::dsp::IIR::Filter<float> masterEqHighL, masterEqHighR;

    // Master compressor state
    float masterCompEnvelope = 0.0f;

    // Master limiter state
    float masterLimiterEnvelope = 0.0f;

    // Master peak level
    std::atomic<float> masterPeakLevel { 0.0f };

    // Processing helpers
    void processDelay (const juce::AudioBuffer<float>& input,
                       juce::AudioBuffer<float>& output,
                       int startSample,
                       int numSamples);
    void processReverb (const juce::AudioBuffer<float>& input,
                        juce::AudioBuffer<float>& output,
                        int startSample,
                        int numSamples);
    int getDelayTimeSamples() const;

    // Send return processing
    void processSendReturnEQ (juce::AudioBuffer<float>& buffer, int numSamples,
                              const SendReturnState& state,
                              juce::dsp::IIR::Filter<float>& eqLowL, juce::dsp::IIR::Filter<float>& eqLowR,
                              juce::dsp::IIR::Filter<float>& eqMidL, juce::dsp::IIR::Filter<float>& eqMidR,
                              juce::dsp::IIR::Filter<float>& eqHighL, juce::dsp::IIR::Filter<float>& eqHighR);
    void applySendReturnVolumePan (juce::AudioBuffer<float>& buffer, int numSamples,
                                   const SendReturnState& state);

    // Master processing
    void processMasterEQ (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    void processMasterCompressor (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    void processMasterLimiter (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SendEffectsPlugin)
};
