#pragma once

#include <array>
#include <atomic>
#include <map>
#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include "FxParamTransport.h"
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
    void setGlobalModState (GlobalModState* modState) { globalModState = modState; }
    void setGlobalModStates (const std::map<int, GlobalModState*>& states);
    void setRowsPerBeat (int rpb) { rowsPerBeat = rpb; }
    void setSendBuffers (SendBuffers* buffers) { sendBuffers = buffers; }
    void setOutputGainLinear (float gain) { outputGainLinear.store (juce::jlimit (0.0f, 1.0f, gain), std::memory_order_relaxed); }
    void setTrackSendGainLinear (float gain) { trackSendGainLinear.store (juce::jmax (0.0f, gain), std::memory_order_relaxed); }

    // Legacy callback hook; master-lane T tempo FX are handled by TrackerEngine.
    std::function<void (int)> onTempoChange;

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
        int volumeOverride = -1;   // -1 = no override, 0-127 from Cxx
        int delaySendOverride = -1;   // 0-255 (mapped to -100..0 dB)
        int reverbSendOverride = -1;  // 0-255 (mapped to -100..0 dB)
        int volumeFxRaw = -1;         // -1 = no override, 0-255 from Vxx
        int filterTypeOverride = -1;  // -1 = use instrument, otherwise InstrumentParams::FilterType ordinal
        int cutoffOverride = -1;      // -1 = use instrument cutoff, 0-100
        int overdriveOverride = -1;   // -1 = use instrument overdrive, 0-100
        int bitDepthOverride = -1;    // -1 = use instrument bit depth, 4-16
        std::array<int, InstrumentParams::kNumModDests> lfoSpeedOverride; // -1 = use instrument LFO speed
        std::array<int, InstrumentParams::kNumModDests> modModeOverride;  // -1 = use default

        TrackOverrides()
        {
            lfoSpeedOverride.fill (-1);
            modModeOverride.fill (-1);
        }
    };
    TrackOverrides overrides;

    // FX command state (per-track, updated via CC messages)
    struct FxState
    {
        // Legacy per-track arpeggio state; current Polyend Axx scheduling is resolved before MIDI emission.
        int arpParam = 0;         // x=high nibble, y=low nibble
        int arpPhase = 0;         // 0, 1, 2 cycling

        // Pitch slide (1xx, 2xx)
        float pitchSlide = 0.0f;  // accumulated pitch offset in semitones
        int slideUpSpeed = 0;
        int slideDownSpeed = 0;

        // Tone portamento (3xx)
        int portaSpeed = 0;
        int portaTarget = -1;     // target MIDI note
        float portaPitch = 0.0f;  // current pitch offset
        int portaSteps = 0;
        double portaRowsProgress = 0.0;
        float portaTargetOffset = 0.0f;

        // Vibrato (4xy)
        int vibratoSpeed = 0;
        int vibratoDepth = 0;
        double vibratoPhase = 0.0;

        // Tremolo (7xy)
        int tremoloSpeed = 0;
        int tremoloDepth = 0;
        double tremoloPhase = 0.0;

        // Volume slide (Axy, 5xy, 6xy)
        float volumeSlide = 0.0f; // accumulated volume offset (normalized 0-1)
        int volSlideUp = 0;
        int volSlideDown = 0;

        // Sample offset (9xx)
        int sampleOffset = 0;

        // Legacy speed/tempo cache.
        int lastSpeedTempo = 0;
        int trackerSpeed = 6; // ticks per row

        // 8-bit FX parameter transport helpers. CC#118 is a legacy fallback;
        // generated pattern FX uses per-value-controller high-bit CCs.
        std::array<int, 128> pendingParamHighBits {};
        int pendingParamHighBit = FxParamTransport::kNoPendingParamHighBit;

        // Current base MIDI note for pitch effects
        int currentNote = -1;

        // New symbolic command pitch state.
        float tuneOffset = 0.0f;
        float microTuneOffset = 0.0f;
        float stepSlideOffset = 0.0f;
        bool stepSlideActive = false;
        float stepSlideStart = 0.0f;
        float stepSlideTarget = 0.0f;
        int stepSlideSteps = 0;
        double stepSlideRowsProgress = 0.0;

        // Active flags for memory effects (cleared per row, re-set by CC)
        bool portaActive = false;
        bool vibratoActive = false;
        bool tremoloActive = false;
        double arpTickAccum = 0.0;

        FxState()
        {
            resetPendingParamHighBits();
        }

        void resetPendingParamHighBits()
        {
            pendingParamHighBits.fill (FxParamTransport::kNoPendingParamHighBit);
            pendingParamHighBit = FxParamTransport::kNoPendingParamHighBit;
        }

        void reset()
        {
            arpParam = 0; arpPhase = 0; arpTickAccum = 0.0;
            pitchSlide = 0.0f;
            slideUpSpeed = 0; slideDownSpeed = 0;
            portaSpeed = 0; portaTarget = -1; portaPitch = 0.0f; portaActive = false;
            vibratoSpeed = 0; vibratoDepth = 0; vibratoPhase = 0.0; vibratoActive = false;
            tremoloSpeed = 0; tremoloDepth = 0; tremoloPhase = 0.0; tremoloActive = false;
            volumeSlide = 0.0f; volSlideUp = 0; volSlideDown = 0;
            sampleOffset = 0; lastSpeedTempo = 0;
            trackerSpeed = 6;
            resetPendingParamHighBits();
            currentNote = -1;
            tuneOffset = 0.0f;
            microTuneOffset = 0.0f;
            stepSlideOffset = 0.0f;
            stepSlideActive = false;
            stepSlideStart = 0.0f;
            stepSlideTarget = 0.0f;
            stepSlideSteps = 0;
            stepSlideRowsProgress = 0.0;
            portaSteps = 0;
            portaRowsProgress = 0.0;
            portaTargetOffset = 0.0f;
        }
    };
    FxState fxState;

    // Global modulation support
    GlobalModState* globalModState = nullptr;
    juce::SpinLock globalStateLock;
    std::map<int, GlobalModState*> globalStatesByInstrument;
    double currentTransportBeat = 0.0;
    int rowsPerBeat = 4;
    int bankSelectMsb = 0;
    std::atomic<float> outputGainLinear { 1.0f };
    std::atomic<float> trackSendGainLinear { 1.0f };

    // Parameter smoothing
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedGainL { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedGainR { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedCutoffHz { 20000.0f };

    // Filter
    juce::dsp::StateVariableTPTFilter<float> svfFilter;
    bool filterInitialized = false;
    InstrumentParams::FilterType lastFilterType = InstrumentParams::FilterType::Disabled;
    double sampleRateReductionPhase = 1.0;
    std::array<float, 2> sampleRateReductionHeldSamples {};

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
    void processSampleRateReduction (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                                     double targetSampleRateHz);
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
    void advanceGlobalEnvelopes (const InstrumentParams& params, juce::int64 blockStartSample, int numSamples);
    bool isModModeGlobal (int destIndex, const InstrumentParams& params) const;

    void triggerEnvelopes();
    void releaseEnvelopes();
    void resetModulationState();

    void processFxCommands (int numSamples, float& pitchMod, float& fxVolumeMod);

    static float cutoffPercentToHz (int percent);
    static float resonancePercentToQ (int percent);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstrumentEffectsPlugin)
};
