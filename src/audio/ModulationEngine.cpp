#include "ModulationEngine.h"
#include "SimpleSampler.h"   // GlobalModState
#include <cstdint>

//==============================================================================
// Unified LFO waveform evaluation
//==============================================================================

float ModulationEngine::evaluateLfoWaveform (float phase,
                                              InstrumentParams::Modulation::LFOShape shape,
                                              LFOState* state)
{
    float value = 0.0f;

    switch (shape)
    {
        case InstrumentParams::Modulation::LFOShape::RevSaw:
            value = 1.0f - 2.0f * phase;
            break;
        case InstrumentParams::Modulation::LFOShape::Saw:
            value = -1.0f + 2.0f * phase;
            break;
        case InstrumentParams::Modulation::LFOShape::Triangle:
            value = (phase < 0.5f) ? (-1.0f + 4.0f * phase) : (3.0f - 4.0f * phase);
            break;
        case InstrumentParams::Modulation::LFOShape::Square:
            value = (phase < 0.5f) ? 1.0f : -1.0f;
            break;
        case InstrumentParams::Modulation::LFOShape::Random:
            if (state != nullptr && state->randomNeedsNew)
            {
                state->randomHoldValue = juce::Random::getSystemRandom().nextFloat() * 2.0f - 1.0f;
                state->randomNeedsNew = false;
            }
            value = (state != nullptr) ? state->randomHoldValue : 0.0f;
            break;
    }

    return value;
}

//==============================================================================
// Per-note LFO
//==============================================================================

float ModulationEngine::computeLFO (LFOState& state,
                                     const InstrumentParams::Modulation& mod,
                                     double bpm, int numSamples)
{
    if (mod.type != InstrumentParams::Modulation::Type::LFO || mod.amount == 0)
        return 0.0f;

    double lfoHz;
    if (mod.lfoSpeedMode == InstrumentParams::Modulation::LFOSpeedMode::MS)
    {
        lfoHz = 1000.0 / static_cast<double> (juce::jmax (1, mod.lfoSpeedMs));
    }
    else
    {
        double stepsPerBeat = static_cast<double> (juce::jmax (1, rowsPerBeat));
        double speedInSteps = static_cast<double> (juce::jmax (1, mod.lfoSpeed));
        lfoHz = (bpm / 60.0) * stepsPerBeat / speedInSteps;
    }

    double phaseInc = lfoHz / sampleRate * static_cast<double> (numSamples);
    state.phase += phaseInc;
    if (state.phase >= 1.0)
    {
        state.phase -= std::floor (state.phase);
        state.randomNeedsNew = true;
    }

    float value = evaluateLfoWaveform (static_cast<float> (state.phase), mod.lfoShape, &state);

    state.currentValue = value * (static_cast<float> (mod.amount) / 100.0f);
    return state.currentValue;
}

//==============================================================================
// Per-note envelope
//==============================================================================

float ModulationEngine::advanceEnvelope (EnvState& state,
                                          const InstrumentParams::Modulation& mod,
                                          int numSamples)
{
    if (mod.type != InstrumentParams::Modulation::Type::Envelope)
        return 0.0f;

    double blockDuration = static_cast<double> (numSamples) / sampleRate;

    switch (state.stage)
    {
        case EnvState::Stage::Idle:
            state.level = 0.0f;
            break;

        case EnvState::Stage::Attack:
        {
            double attackTime = juce::jmax (0.001, mod.attackS);
            state.level += static_cast<float> (blockDuration / attackTime);
            if (state.level >= 1.0f)
            {
                state.level = 1.0f;
                state.stage = EnvState::Stage::Decay;
            }
            break;
        }

        case EnvState::Stage::Decay:
        {
            double decayTime = juce::jmax (0.001, mod.decayS);
            float susLevel = static_cast<float> (mod.sustain) / 100.0f;
            state.level -= static_cast<float> (blockDuration / decayTime) * (1.0f - susLevel);
            if (state.level <= susLevel)
            {
                state.level = susLevel;
                state.stage = EnvState::Stage::Sustain;
            }
            break;
        }

        case EnvState::Stage::Sustain:
            state.level = static_cast<float> (mod.sustain) / 100.0f;
            break;

        case EnvState::Stage::Release:
        {
            double releaseTime = juce::jmax (0.001, mod.releaseS);
            state.level -= static_cast<float> (blockDuration / releaseTime) * state.level;
            if (state.level < 0.001f)
            {
                state.level = 0.0f;
                state.stage = EnvState::Stage::Idle;
            }
            break;
        }
    }

    return state.level * (static_cast<float> (mod.amount) / 100.0f);
}

//==============================================================================
// Trigger / release helpers
//==============================================================================

void ModulationEngine::triggerEnvelopes()
{
    for (auto& env : envStates)
    {
        env.stage = EnvState::Stage::Attack;
        env.level = 0.0f;
    }
    noteActive = true;
}

void ModulationEngine::releaseEnvelopes()
{
    for (auto& env : envStates)
    {
        if (env.stage != EnvState::Stage::Idle)
            env.stage = EnvState::Stage::Release;
    }
    noteActive = false;
}

void ModulationEngine::resetState()
{
    for (auto& lfo : lfoStates)
    {
        lfo.phase = 0.0;
        lfo.currentValue = 0.0f;
        lfo.randomHoldValue = 0.0f;
        lfo.randomNeedsNew = true;
    }
    for (auto& env : envStates)
    {
        env.stage = EnvState::Stage::Idle;
        env.level = 0.0f;
    }
    noteActive = false;
}

//==============================================================================
// Global modulation helpers
//==============================================================================

bool ModulationEngine::isModModeGlobal (int destIndex,
                                         const InstrumentParams& params,
                                         const std::array<int, InstrumentParams::kNumModDests>& modModeOverride) const
{
    if (destIndex < 0 || destIndex >= InstrumentParams::kNumModDests)
        return false;

    int ov = modModeOverride[static_cast<size_t> (destIndex)];
    if (ov >= 0)
        return ov == 1;

    return params.modulations[static_cast<size_t> (destIndex)].modMode
           == InstrumentParams::Modulation::ModMode::Global;
}

float ModulationEngine::computeGlobalLFO (const InstrumentParams::Modulation& mod,
                                           double bpm)
{
    if (mod.type != InstrumentParams::Modulation::Type::LFO || mod.amount == 0)
        return 0.0f;

    double phase;
    double speedInSteps = static_cast<double> (juce::jmax (1, mod.lfoSpeed));

    if (mod.lfoSpeedMode == InstrumentParams::Modulation::LFOSpeedMode::MS)
    {
        double transportSeconds = currentTransportBeat * 60.0 / bpm;
        double periodSeconds = static_cast<double> (juce::jmax (1, mod.lfoSpeedMs)) / 1000.0;
        phase = std::fmod (transportSeconds / periodSeconds, 1.0);
    }
    else
    {
        double stepsPerBeat = static_cast<double> (rowsPerBeat);
        phase = std::fmod (currentTransportBeat * stepsPerBeat / speedInSteps, 1.0);
    }
    if (phase < 0.0) phase += 1.0;

    float p = static_cast<float> (phase);

    // For global Random, use deterministic seed from quantized step index
    if (mod.lfoShape == InstrumentParams::Modulation::LFOShape::Random)
    {
        int stepIndex;
        if (mod.lfoSpeedMode == InstrumentParams::Modulation::LFOSpeedMode::MS)
        {
            double transportSeconds = currentTransportBeat * 60.0 / bpm;
            double periodSeconds = static_cast<double> (juce::jmax (1, mod.lfoSpeedMs)) / 1000.0;
            stepIndex = static_cast<int> (std::floor (transportSeconds / periodSeconds));
        }
        else
        {
            double stepsPerBeat2 = static_cast<double> (rowsPerBeat);
            stepIndex = static_cast<int> (std::floor (currentTransportBeat * stepsPerBeat2 / speedInSteps));
        }
        juce::Random rng (static_cast<juce::int64> (stepIndex * 12345 + 67890));
        float value = rng.nextFloat() * 2.0f - 1.0f;
        return value * (static_cast<float> (mod.amount) / 100.0f);
    }

    float value = evaluateLfoWaveform (p, mod.lfoShape);
    return value * (static_cast<float> (mod.amount) / 100.0f);
}

float ModulationEngine::readGlobalEnvelope (int destIndex,
                                             const InstrumentParams::Modulation& mod)
{
    if (mod.type != InstrumentParams::Modulation::Type::Envelope || globalModState == nullptr)
        return 0.0f;

    float level = globalModState->envStates[static_cast<size_t> (destIndex)].level.load (std::memory_order_relaxed);
    return level * (static_cast<float> (mod.amount) / 100.0f);
}

void ModulationEngine::advanceGlobalEnvelopes (const InstrumentParams& params,
                                                juce::int64 blockStartSample,
                                                int numSamples)
{
    if (globalModState == nullptr || numSamples <= 0)
        return;

    const auto blockTag = static_cast<uint64_t> (juce::jmax<juce::int64> (0, blockStartSample));
    uint64_t expected = globalModState->lastProcessedBlock.load (std::memory_order_relaxed);
    if (expected == blockTag)
        return;

    if (! globalModState->lastProcessedBlock.compare_exchange_strong (
            expected, blockTag, std::memory_order_relaxed))
    {
        return;
    }

    const double blockDuration = static_cast<double> (numSamples) / sampleRate;

    for (int d = 0; d < InstrumentParams::kNumModDests; ++d)
    {
        auto& mod = params.modulations[static_cast<size_t> (d)];
        if (mod.type != InstrumentParams::Modulation::Type::Envelope)
            continue;
        if (mod.modMode != InstrumentParams::Modulation::ModMode::Global)
            continue;

        auto& es = globalModState->envStates[static_cast<size_t> (d)];
        int stage = es.stage.load (std::memory_order_relaxed);
        float level = es.level.load (std::memory_order_relaxed);

        switch (stage)
        {
            case 0: // Idle
                level = 0.0f;
                break;
            case 1: // Attack
            {
                double attackTime = juce::jmax (0.001, mod.attackS);
                level += static_cast<float> (blockDuration / attackTime);
                if (level >= 1.0f)
                {
                    level = 1.0f;
                    stage = 2;
                }
                break;
            }
            case 2: // Decay
            {
                double decayTime = juce::jmax (0.001, mod.decayS);
                float susLevel = static_cast<float> (mod.sustain) / 100.0f;
                level -= static_cast<float> (blockDuration / decayTime) * (1.0f - susLevel);
                if (level <= susLevel)
                {
                    level = susLevel;
                    stage = 3;
                }
                break;
            }
            case 3: // Sustain
                level = static_cast<float> (mod.sustain) / 100.0f;
                break;
            case 4: // Release
            {
                double releaseTime = juce::jmax (0.001, mod.releaseS);
                level -= static_cast<float> (blockDuration / releaseTime) * level;
                if (level < 0.001f)
                {
                    level = 0.0f;
                    stage = 0;
                }
                break;
            }
        }

        es.stage.store (stage, std::memory_order_relaxed);
        es.level.store (level, std::memory_order_relaxed);
    }
}

//==============================================================================
// Combined modulation value
//==============================================================================

float ModulationEngine::getModulationValue (int destIndex,
                                             const InstrumentParams& params,
                                             double bpm, int numSamples,
                                             const std::array<int, InstrumentParams::kNumModDests>& modModeOverride)
{
    if (destIndex < 0 || destIndex >= InstrumentParams::kNumModDests)
        return 0.0f;

    auto& mod = params.modulations[static_cast<size_t> (destIndex)];

    if (isModModeGlobal (destIndex, params, modModeOverride))
    {
        switch (mod.type)
        {
            case InstrumentParams::Modulation::Type::LFO:
                return computeGlobalLFO (mod, bpm);
            case InstrumentParams::Modulation::Type::Envelope:
                return readGlobalEnvelope (destIndex, mod);
            default:
                return 0.0f;
        }
    }

    switch (mod.type)
    {
        case InstrumentParams::Modulation::Type::LFO:
            return computeLFO (lfoStates[static_cast<size_t> (destIndex)], mod, bpm, numSamples);
        case InstrumentParams::Modulation::Type::Envelope:
            return advanceEnvelope (envStates[static_cast<size_t> (destIndex)], mod, numSamples);
        default:
            return 0.0f;
    }
}
