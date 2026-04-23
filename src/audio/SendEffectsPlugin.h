#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include <array>
#include <atomic>
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
    void setSendBuffers (SendBuffers* buffers);

    // Mixer state pointer for send return and master processing
    void setMixerState (MixerState* mixState);
    void refreshMixerStateSnapshot();

    // Thread-safe parameter setters (called from UI thread)
    void setDelayParams (const DelayParams& params);
    void setReverbParams (const ReverbParams& params);
    DelayParams getDelayParams() const;
    ReverbParams getReverbParams() const;
    void setTempoBpm (double bpm);

    // Master peak metering
    float getMasterPeakLevel() const { return masterPeakLevel.load (std::memory_order_relaxed); }
    void resetMasterPeak() { masterPeakLevel.store (0.0f, std::memory_order_relaxed); }

private:
    SendBuffers* sendBuffers = nullptr;
    MixerState* mixerStatePtr = nullptr;
    std::atomic<bool> hasMixerState { false };

    struct AtomicDelayParams
    {
        std::atomic<uint32_t> sequence { 0 };
        std::atomic<float> time { 250.0f };
        std::atomic<int> syncDivision { 4 };
        std::atomic<bool> bpmSync { true };
        std::atomic<bool> dotted { false };
        std::atomic<float> feedback { 40.0f };
        std::atomic<int> filterType { 0 };
        std::atomic<float> filterCutoff { 80.0f };
        std::atomic<float> wet { 50.0f };
        std::atomic<float> stereoWidth { 50.0f };

        void store (const DelayParams& params);
        bool loadConsistent (DelayParams& params) const;
        DelayParams loadRelaxed() const;
    };

    struct AtomicReverbParams
    {
        std::atomic<uint32_t> sequence { 0 };
        std::atomic<float> roomSize { 50.0f };
        std::atomic<float> decay { 50.0f };
        std::atomic<float> damping { 50.0f };
        std::atomic<float> preDelay { 10.0f };
        std::atomic<float> wet { 30.0f };

        void store (const ReverbParams& params);
        bool loadConsistent (ReverbParams& params) const;
        ReverbParams loadRelaxed() const;
    };

    AtomicDelayParams pendingDelayParams;
    AtomicReverbParams pendingReverbParams;
    DelayParams activeDelayParams;
    ReverbParams activeReverbParams;
    std::atomic<float> tempoBpm { 120.0f };

    struct AtomicSendReturnState
    {
        std::atomic<uint32_t> sequence { 0 };
        std::atomic<float> volume { 0.0f };
        std::atomic<int> pan { 0 };
        std::atomic<bool> muted { false };
        std::atomic<float> eqLowGain { 0.0f };
        std::atomic<float> eqMidGain { 0.0f };
        std::atomic<float> eqHighGain { 0.0f };
        std::atomic<float> eqMidFreq { 1000.0f };

        void store (const SendReturnState& state);
        bool loadConsistent (SendReturnState& state) const;
    };

    struct AtomicMasterMixState
    {
        std::atomic<uint32_t> sequence { 0 };
        std::atomic<float> volume { 0.0f };
        std::atomic<int> pan { 0 };
        std::atomic<float> eqLowGain { 0.0f };
        std::atomic<float> eqMidGain { 0.0f };
        std::atomic<float> eqHighGain { 0.0f };
        std::atomic<float> eqMidFreq { 1000.0f };
        std::atomic<float> compThreshold { 0.0f };
        std::atomic<float> compRatio { 1.0f };
        std::atomic<float> compAttack { 10.0f };
        std::atomic<float> compRelease { 100.0f };
        std::atomic<float> limiterThreshold { 0.0f };
        std::atomic<float> limiterRelease { 50.0f };

        void store (const MasterMixState& state);
        bool loadConsistent (MasterMixState& state) const;
    };

    std::array<AtomicSendReturnState, 2> pendingSendReturns;
    AtomicMasterMixState pendingMasterState;
    std::array<SendReturnState, 2> activeSendReturns {};
    MasterMixState activeMasterState;

    // Delay line
    static constexpr double kMaxDelaySeconds = 20.0;
    juce::AudioBuffer<float> delayLine;
    int delayWritePos = 0;
    juce::dsp::StateVariableTPTFilter<float> delayFilterL;
    juce::dsp::StateVariableTPTFilter<float> delayFilterR;
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
    int maxScratchSamples = 0;
    int sendBufferCapacitySamples = 0;

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
    void processSendEffectsChunk (juce::AudioBuffer<float>& buffer,
                                  int startSample,
                                  int numSamples,
                                  bool useMixerState);
    void configurePreparedBuffers (int blockSize);
    int getDelayLineSize() const noexcept { return delayLine.getNumSamples(); }

    // Send return processing
    void processSendReturnEQ (juce::AudioBuffer<float>& buffer, int numSamples,
                              const SendReturnState& sendState,
                              juce::dsp::IIR::Filter<float>& eqLowL, juce::dsp::IIR::Filter<float>& eqLowR,
                              juce::dsp::IIR::Filter<float>& eqMidL, juce::dsp::IIR::Filter<float>& eqMidR,
                              juce::dsp::IIR::Filter<float>& eqHighL, juce::dsp::IIR::Filter<float>& eqHighR);
    void applySendReturnVolumePan (juce::AudioBuffer<float>& buffer, int numSamples,
                                   const SendReturnState& sendState);

    // Master processing
    void processMasterEQ (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    void processMasterCompressor (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    void processMasterLimiter (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SendEffectsPlugin)
};
