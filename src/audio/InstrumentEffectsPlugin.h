#pragma once

#include <atomic>
#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include "InstrumentParams.h"
#include "SendBuffers.h"

namespace te = tracktion;

class SimpleSampler;
struct GlobalModState;

class InstrumentEffectsPlugin : public te::Plugin
{
public:
    InstrumentEffectsPlugin (te::PluginCreationInfo);
    ~InstrumentEffectsPlugin() override;

    static const char* getPluginName()  { return "InstrumentEffects"; }
    static const char* xmlTypeName;

    juce::String getName() const override               { return getPluginName(); }
    juce::String getPluginType() override               { return xmlTypeName; }
    bool takesMidiInput() override                      { return true; }
    bool takesAudioInput() override                     { return true; }
    bool isSynth() override                             { return false; }
    bool producesAudioWhenNoAudioInput() override       { return false; }
    int getNumOutputChannelsGivenInputs (int numInputChannels) override { return juce::jmin (numInputChannels, 2); }

    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override;
    void applyToBuffer (const te::PluginRenderContext&) override;

    juce::String getSelectableDescription() override    { return getName(); }
    bool needsConstantBufferSize() override             { return false; }

    void setSamplerSource (SimpleSampler* s) { sampler = s; }
    void setInstrumentIndex (int index);
    void setGlobalModState (GlobalModState* state) { globalModState = state; }
    void setRowsPerBeat (int rpb) { rowsPerBeat = rpb; }
    void setSendBuffers (SendBuffers* buffers) { sendBuffers = buffers; }

private:
    SimpleSampler* sampler = nullptr;
    SendBuffers* sendBuffers = nullptr;
    int blockSize = 512;

    // Current instrument state
    int currentInstrument = -1;

    // Per-track overrides (set via effect commands, only accessed on audio thread)
    struct TrackOverrides
    {
        int panningOverride = -1;  // -1 = no override, 0-127 = CC10 value (64=center)
        std::array<int, InstrumentParams::kNumModDests> modModeOverride;  // -1 = use default

        TrackOverrides() { modModeOverride.fill (-1); }
    };
    TrackOverrides overrides;

    // Global modulation support
    GlobalModState* globalModState = nullptr;
    double currentTransportBeat = 0.0;
    int rowsPerBeat = 4;
    static std::atomic<uint64_t> blockCounter;

    // Parameter smoothing
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedGainL { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedGainR { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedCutoffHz { 20000.0f };

    // Filter
    juce::dsp::StateVariableTPTFilter<float> svfFilter;
    bool filterInitialized = false;
    InstrumentParams::FilterType lastFilterType = InstrumentParams::FilterType::Disabled;

    // LFO state per destination
    struct LFOState
    {
        double phase = 0.0;
        float currentValue = 0.0f;
        float randomHoldValue = 0.0f;
        bool randomNeedsNew = true;
    };
    std::array<LFOState, InstrumentParams::kNumModDests> lfoStates {};

    // Envelope state per destination
    struct EnvState
    {
        enum class Stage { Idle, Attack, Decay, Sustain, Release };
        Stage stage = Stage::Idle;
        float level = 0.0f;
    };
    std::array<EnvState, InstrumentParams::kNumModDests> envStates {};

    bool noteActive = false;

    // DSP helpers
    void processFilter (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                        const InstrumentParams& params, float cutoffMod);
    void processOverdrive (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                           int overdrive);
    void processBitDepth (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                          int bitDepth);
    void processVolumeAndPan (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                              const InstrumentParams& params, float volumeMod, float panMod);

    // Modulation (per-note)
    float computeLFO (LFOState& state, const InstrumentParams::Modulation& mod, double bpm, int numSamples);
    float advanceEnvelope (EnvState& state, const InstrumentParams::Modulation& mod, int numSamples);
    float getModulationValue (int destIndex, const InstrumentParams& params, double bpm, int numSamples);

    // Global modulation
    float computeGlobalLFO (const InstrumentParams::Modulation& mod);
    float readGlobalEnvelope (int destIndex, const InstrumentParams::Modulation& mod);
    void advanceGlobalEnvelopes (const InstrumentParams& params);
    bool isModModeGlobal (int destIndex, const InstrumentParams& params) const;

    void triggerEnvelopes();
    void releaseEnvelopes();
    void resetModulationState();

    static float cutoffPercentToHz (int percent);
    static float resonancePercentToQ (int percent);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstrumentEffectsPlugin)
};
