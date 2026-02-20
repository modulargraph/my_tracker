#pragma once

#include <array>
#include <vector>

struct InstrumentParams
{
    // === General ===
    double volume       = 0.0;     // dB, -inf to +24.0 (-100 = -inf)
    int    panning      = 0;       // -50 to +50
    int    tune         = 0;       // -24 to +24 semitones
    int    finetune     = 0;       // -100 to +100 cents

    // === Filter ===
    enum class FilterType { Disabled, LowPass, HighPass, BandPass };
    FilterType filterType = FilterType::Disabled;
    int cutoff      = 100;         // 0-100 (percentage mapped to Hz)
    int resonance   = 0;           // 0-100

    // === Effects ===
    int    overdrive   = 0;        // 0-100
    int    bitDepth    = 16;       // 4-16
    double reverbSend  = -100.0;   // dB, -inf (as -100) to 0
    double delaySend   = -100.0;   // dB, -inf (as -100) to 0

    // === Sample Position ===
    double startPos    = 0.0;      // 0.0-1.0 normalized
    double endPos      = 1.0;
    double loopStart   = 0.0;      // for loop modes
    double loopEnd     = 1.0;

    // === Playback Mode ===
    enum class PlayMode { OneShot, ForwardLoop, BackwardLoop, PingpongLoop,
                          Slice, BeatSlice, Granular };
    PlayMode playMode = PlayMode::OneShot;

    bool reversed = false;

    // === Granular params ===
    double granularPosition = 0.0; // absolute normalized sample position (0.0-1.0)
    int    granularLength   = 500; // 1-1000 ms
    enum class GranShape { Square, Triangle, Gauss };
    GranShape granularShape = GranShape::Triangle;
    enum class GranLoop { Forward, Reverse, Pingpong };
    GranLoop granularLoop = GranLoop::Forward;

    // === Slice data ===
    std::vector<double> slicePoints; // absolute normalized sample positions, sorted

    // === Modulation (per destination) ===
    enum class ModDest { Volume, Panning, Cutoff, GranularPos, Finetune };
    static constexpr int kNumModDests = 5;

    struct Modulation
    {
        enum class Type { Off, Envelope, LFO };
        Type type = Type::Off;

        // LFO
        enum class LFOShape { RevSaw, Saw, Triangle, Square, Random };
        LFOShape lfoShape = LFOShape::Triangle;
        int lfoSpeed      = 24;   // step-based speed value
        int amount        = 0;    // 0-100 (starts at 0 for safety)

        // Envelope
        double attackS  = 0.020;  // seconds (0-10)
        double decayS   = 0.030;
        int    sustain  = 100;    // 0-100
        double releaseS = 0.050;

        // Modulation mode: PerNote resets per note, Global is shared across tracks
        enum class ModMode { PerNote, Global };
        ModMode modMode = ModMode::PerNote;

        bool isDefault() const
        {
            return type == Type::Off
                && lfoShape == LFOShape::Triangle
                && lfoSpeed == 24
                && amount == 0
                && attackS == 0.020
                && decayS == 0.030
                && sustain == 100
                && releaseS == 0.050
                && modMode == ModMode::PerNote;
        }
    };

    std::array<Modulation, kNumModDests> modulations {};

    // === Legacy compatibility ===
    // Old params mapped during load: attackMs/decayMs/sustainLevel/releaseMs

    bool isDefault() const
    {
        if (volume != 0.0 || panning != 0 || tune != 0 || finetune != 0)
            return false;
        if (filterType != FilterType::Disabled || cutoff != 100 || resonance != 0)
            return false;
        if (overdrive != 0 || bitDepth != 16)
            return false;
        if (reverbSend != -100.0 || delaySend != -100.0)
            return false;
        if (startPos != 0.0 || endPos != 1.0)
            return false;
        if (loopStart != 0.0 || loopEnd != 1.0)
            return false;
        if (playMode != PlayMode::OneShot || reversed)
            return false;
        if (granularPosition != 0.0 || granularLength != 500)
            return false;
        if (granularShape != GranShape::Triangle || granularLoop != GranLoop::Forward)
            return false;
        if (! slicePoints.empty())
            return false;
        for (auto& mod : modulations)
            if (! mod.isDefault())
                return false;
        return true;
    }
};
