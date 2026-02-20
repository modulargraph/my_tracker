#pragma once

#include <atomic>
#include <map>
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
    void setGlobalModStates (const std::map<int, GlobalModState*>& states);
    void setRowsPerBeat (int rpb) { rowsPerBeat = rpb; }
    void setSendBuffers (SendBuffers* buffers) { sendBuffers = buffers; }

    // Callback for Fxx (Set Speed/Tempo) — called on audio thread
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
        std::array<int, InstrumentParams::kNumModDests> modModeOverride;  // -1 = use default

        TrackOverrides() { modModeOverride.fill (-1); }
    };
    TrackOverrides overrides;

    // FX command state (per-track, updated via CC messages)
    struct FxState
    {
        // Arpeggio (0xy): cycle base, +x, +y semitones
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

        // Set Speed/Tempo (Fxx)
        int lastSpeedTempo = 0;
        int trackerSpeed = 6; // ticks per row

        // 8-bit FX parameter transport helper (high bit from CC#118)
        int pendingParamHighBit = 0;

        // Current base MIDI note for pitch effects
        int currentNote = -1;

        // Active flags for memory effects (cleared per row, re-set by CC)
        bool portaActive = false;
        bool vibratoActive = false;
        bool tremoloActive = false;
        double arpTickAccum = 0.0;

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
            pendingParamHighBit = 0;
            currentNote = -1;
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
