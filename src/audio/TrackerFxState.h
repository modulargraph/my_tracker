#pragma once

#include <array>
#include "InstrumentParams.h"

// Per-track overrides (set via effect commands, only accessed on audio thread)
struct TrackOverrides
{
    int panningOverride = -1;  // -1 = no override, 0-127 = CC10 value (64=center)
    int volumeOverride = -1;   // -1 = no override, 0-127 from Cxx
    int delaySendOverride = -1;   // 0-255 (mapped to -100..0 dB)
    int reverbSendOverride = -1;  // 0-255 (mapped to -100..0 dB)
    int volumeFxRaw = -1;         // -1 = no override, 0-255 from Vxx
    std::array<int, InstrumentParams::kNumModDests> modModeOverride;  // -1 = use default

    TrackOverrides() { modModeOverride.fill (-1); }
};

// FX command state (per-track, updated via CC messages)
struct TrackerFxState
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

    // Set Speed/Tempo (Fxx)
    int lastSpeedTempo = 0;
    int trackerSpeed = 6; // ticks per row

    // 8-bit FX parameter transport helper (high bit from CC#118)
    int pendingParamHighBit = 0;

    // Current base MIDI note for pitch effects
    int currentNote = -1;

    // New symbolic command pitch state.
    float tuneOffset = 0.0f;
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
        tuneOffset = 0.0f;
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
