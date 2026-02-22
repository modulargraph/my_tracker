#pragma once

#include <array>
#include <atomic>
#include <JuceHeader.h>
#include "InstrumentParams.h"

struct GlobalModState;

//==============================================================================
/** Consolidates per-note and global LFO / envelope modulation logic.

    InstrumentEffectsPlugin owns one of these and delegates all modulation
    computation to it.  The duplicated LFO-waveform switches that previously
    existed in computeLFO() and computeGlobalLFO() are now a single
    evaluateLfoWaveform() helper.
*/
class ModulationEngine
{
public:
    ModulationEngine() = default;

    //==========================================================================
    // State types (moved from InstrumentEffectsPlugin)

    struct LFOState
    {
        double phase = 0.0;
        float currentValue = 0.0f;
        float randomHoldValue = 0.0f;
        bool randomNeedsNew = true;
    };

    struct EnvState
    {
        enum class Stage { Idle, Attack, Decay, Sustain, Release };
        Stage stage = Stage::Idle;
        float level = 0.0f;
    };

    //==========================================================================
    // Context that the owning plugin sets each block

    void setSampleRate (double sr)        { sampleRate = sr; }
    void setRowsPerBeat (int rpb)         { rowsPerBeat = rpb; }
    void setTransportBeat (double beat)   { currentTransportBeat = beat; }
    void setGlobalModState (GlobalModState* s) { globalModState = s; }

    //==========================================================================
    // Core API

    /** Unified LFO waveform evaluation.  Returns a value in [-1, 1].
        Used by both per-note and global LFO paths. */
    static float evaluateLfoWaveform (float phase,
                                      InstrumentParams::Modulation::LFOShape shape,
                                      LFOState* state = nullptr);

    /** Per-note LFO: advances the phase and returns the scaled modulation value. */
    float computeLFO (LFOState& state,
                      const InstrumentParams::Modulation& mod,
                      double bpm, int numSamples);

    /** Per-note envelope: advances the ADSR and returns the scaled value. */
    float advanceEnvelope (EnvState& state,
                           const InstrumentParams::Modulation& mod,
                           int numSamples);

    /** Global (transport-synced) LFO.  Phase is derived from the transport
        beat position so every track reads the same value. */
    float computeGlobalLFO (const InstrumentParams::Modulation& mod,
                            double bpm);

    /** Read a global envelope value (written by advanceGlobalEnvelopes). */
    float readGlobalEnvelope (int destIndex,
                              const InstrumentParams::Modulation& mod);

    /** Advance all global envelopes once per audio block (deduplicated). */
    void advanceGlobalEnvelopes (const InstrumentParams& params,
                                 juce::int64 blockStartSample,
                                 int numSamples);

    /** Check whether a destination is using global mode, considering
        per-track overrides. */
    bool isModModeGlobal (int destIndex,
                          const InstrumentParams& params,
                          const std::array<int, InstrumentParams::kNumModDests>& modModeOverride) const;

    /** Combined per-note / global modulation value for a destination. */
    float getModulationValue (int destIndex,
                              const InstrumentParams& params,
                              double bpm, int numSamples,
                              const std::array<int, InstrumentParams::kNumModDests>& modModeOverride);

    //==========================================================================
    // Trigger / release helpers

    void triggerEnvelopes();
    void releaseEnvelopes();
    void resetState();

    //==========================================================================
    // Direct state access (for hard-cut, LFO phase reset, etc.)

    std::array<LFOState, InstrumentParams::kNumModDests>& getLfoStates()  { return lfoStates; }
    std::array<EnvState, InstrumentParams::kNumModDests>& getEnvStates()  { return envStates; }

    bool isNoteActive() const      { return noteActive; }
    void setNoteActive (bool v)    { noteActive = v; }

private:
    double sampleRate = 44100.0;
    int rowsPerBeat = 4;
    double currentTransportBeat = 0.0;
    GlobalModState* globalModState = nullptr;

    std::array<LFOState, InstrumentParams::kNumModDests> lfoStates {};
    std::array<EnvState, InstrumentParams::kNumModDests> envStates {};
    bool noteActive = false;
};
