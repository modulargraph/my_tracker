#include "InstrumentEffectsPlugin.h"
#include "SimpleSampler.h"

const char* InstrumentEffectsPlugin::xmlTypeName = "InstrumentEffects";
std::atomic<uint64_t> InstrumentEffectsPlugin::blockCounter { 0 };

InstrumentEffectsPlugin::InstrumentEffectsPlugin (te::PluginCreationInfo info)
    : te::Plugin (info)
{
}

InstrumentEffectsPlugin::~InstrumentEffectsPlugin()
{
}

void InstrumentEffectsPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate;
    blockSize = info.blockSizeSamples;

    // Prepare filter
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (blockSize);
    spec.numChannels = 2;
    svfFilter.prepare (spec);
    filterInitialized = true;

    // Configure parameter smoothing (~8ms ramp)
    double rampSeconds = 0.008;
    smoothedGainL.reset (sampleRate, rampSeconds);
    smoothedGainR.reset (sampleRate, rampSeconds);
    smoothedCutoffHz.reset (sampleRate, rampSeconds);

    resetModulationState();
}

void InstrumentEffectsPlugin::deinitialise()
{
    svfFilter.reset();
    filterInitialized = false;
}

void InstrumentEffectsPlugin::resetModulationState()
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
    currentInstrument = -1;
    lastFilterType = InstrumentParams::FilterType::Disabled;
    overrides = TrackOverrides();
}

//==============================================================================
// Cutoff: 0-100% → 20Hz-20kHz (logarithmic)
//==============================================================================

float InstrumentEffectsPlugin::cutoffPercentToHz (int percent)
{
    float p = juce::jlimit (0.0f, 100.0f, static_cast<float> (percent)) / 100.0f;
    return 20.0f * std::pow (1000.0f, p); // 20 * 1000^p → 20Hz to 20kHz
}

float InstrumentEffectsPlugin::resonancePercentToQ (int percent)
{
    float p = juce::jlimit (0.0f, 100.0f, static_cast<float> (percent)) / 100.0f;
    return 0.5f + p * 4.5f; // 0.5 to 5.0 (capped for speaker safety)
}

//==============================================================================
// LFO computation
//==============================================================================

float InstrumentEffectsPlugin::computeLFO (LFOState& state, const InstrumentParams::Modulation& mod,
                                            double bpm, int numSamples)
{
    if (mod.type != InstrumentParams::Modulation::Type::LFO || mod.amount == 0)
        return 0.0f;

    // LFO Hz = bpm / 60.0 * rowsPerBeat / speedInSteps
    // Assume 4 rows per beat (standard tracker)
    double stepsPerBeat = 4.0;
    double speedInSteps = static_cast<double> (juce::jmax (1, mod.lfoSpeed));
    double lfoHz = (bpm / 60.0) * stepsPerBeat / speedInSteps;

    double phaseInc = lfoHz / sampleRate * static_cast<double> (numSamples);
    state.phase += phaseInc;
    if (state.phase >= 1.0)
    {
        state.phase -= std::floor (state.phase);
        state.randomNeedsNew = true;
    }

    float value = 0.0f;
    float p = static_cast<float> (state.phase);

    switch (mod.lfoShape)
    {
        case InstrumentParams::Modulation::LFOShape::RevSaw:
            value = 1.0f - 2.0f * p;
            break;
        case InstrumentParams::Modulation::LFOShape::Saw:
            value = -1.0f + 2.0f * p;
            break;
        case InstrumentParams::Modulation::LFOShape::Triangle:
            value = (p < 0.5f) ? (-1.0f + 4.0f * p) : (3.0f - 4.0f * p);
            break;
        case InstrumentParams::Modulation::LFOShape::Square:
            value = (p < 0.5f) ? 1.0f : -1.0f;
            break;
        case InstrumentParams::Modulation::LFOShape::Random:
            if (state.randomNeedsNew)
            {
                state.randomHoldValue = juce::Random::getSystemRandom().nextFloat() * 2.0f - 1.0f;
                state.randomNeedsNew = false;
            }
            value = state.randomHoldValue;
            break;
    }

    state.currentValue = value * (static_cast<float> (mod.amount) / 100.0f);
    return state.currentValue;
}

//==============================================================================
// Envelope computation
//==============================================================================

float InstrumentEffectsPlugin::advanceEnvelope (EnvState& state, const InstrumentParams::Modulation& mod,
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

void InstrumentEffectsPlugin::triggerEnvelopes()
{
    for (auto& env : envStates)
    {
        env.stage = EnvState::Stage::Attack;
        env.level = 0.0f;
    }
    noteActive = true;
}

void InstrumentEffectsPlugin::releaseEnvelopes()
{
    for (auto& env : envStates)
    {
        if (env.stage != EnvState::Stage::Idle)
            env.stage = EnvState::Stage::Release;
    }
    noteActive = false;
}

//==============================================================================
// Global modulation helpers
//==============================================================================

bool InstrumentEffectsPlugin::isModModeGlobal (int destIndex, const InstrumentParams& params) const
{
    if (destIndex < 0 || destIndex >= InstrumentParams::kNumModDests)
        return false;

    // Check per-track override first
    int ov = overrides.modModeOverride[static_cast<size_t> (destIndex)];
    if (ov >= 0)
        return ov == 1; // 0=PerNote, 1=Global

    // Fall back to instrument default
    return params.modulations[static_cast<size_t> (destIndex)].modMode
           == InstrumentParams::Modulation::ModMode::Global;
}

float InstrumentEffectsPlugin::computeGlobalLFO (const InstrumentParams::Modulation& mod)
{
    if (mod.type != InstrumentParams::Modulation::Type::LFO || mod.amount == 0)
        return 0.0f;

    // Deterministic from transport beat position
    double speedInSteps = static_cast<double> (juce::jmax (1, mod.lfoSpeed));
    double stepsPerBeat = static_cast<double> (rowsPerBeat);
    double phase = std::fmod (currentTransportBeat * stepsPerBeat / speedInSteps, 1.0);
    if (phase < 0.0) phase += 1.0;

    float p = static_cast<float> (phase);
    float value = 0.0f;

    switch (mod.lfoShape)
    {
        case InstrumentParams::Modulation::LFOShape::RevSaw:
            value = 1.0f - 2.0f * p;
            break;
        case InstrumentParams::Modulation::LFOShape::Saw:
            value = -1.0f + 2.0f * p;
            break;
        case InstrumentParams::Modulation::LFOShape::Triangle:
            value = (p < 0.5f) ? (-1.0f + 4.0f * p) : (3.0f - 4.0f * p);
            break;
        case InstrumentParams::Modulation::LFOShape::Square:
            value = (p < 0.5f) ? 1.0f : -1.0f;
            break;
        case InstrumentParams::Modulation::LFOShape::Random:
        {
            // Deterministic random: seed from quantized step index
            double stepsPerBeat2 = static_cast<double> (rowsPerBeat);
            int stepIndex = static_cast<int> (std::floor (currentTransportBeat * stepsPerBeat2 / speedInSteps));
            juce::Random rng (static_cast<juce::int64> (stepIndex * 12345 + 67890));
            value = rng.nextFloat() * 2.0f - 1.0f;
            break;
        }
    }

    return value * (static_cast<float> (mod.amount) / 100.0f);
}

float InstrumentEffectsPlugin::readGlobalEnvelope (int destIndex, const InstrumentParams::Modulation& mod)
{
    if (mod.type != InstrumentParams::Modulation::Type::Envelope || globalModState == nullptr)
        return 0.0f;

    float level = globalModState->envStates[static_cast<size_t> (destIndex)].level.load (std::memory_order_relaxed);
    return level * (static_cast<float> (mod.amount) / 100.0f);
}

void InstrumentEffectsPlugin::advanceGlobalEnvelopes (const InstrumentParams& params)
{
    if (globalModState == nullptr)
        return;

    // CAS on lastProcessedBlock to ensure only one plugin advances per block
    uint64_t currentBlock = blockCounter.load (std::memory_order_relaxed);
    uint64_t expected = globalModState->lastProcessedBlock.load (std::memory_order_relaxed);
    if (expected >= currentBlock)
        return; // Already processed this block

    if (! globalModState->lastProcessedBlock.compare_exchange_strong (
            expected, currentBlock, std::memory_order_relaxed))
        return; // Another plugin got there first

    double blockDuration = static_cast<double> (blockSize) / sampleRate;

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
                    stage = 2; // Decay
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
                    stage = 3; // Sustain
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
                    stage = 0; // Idle
                }
                break;
            }
        }

        es.stage.store (stage, std::memory_order_relaxed);
        es.level.store (level, std::memory_order_relaxed);
    }
}

//==============================================================================
// Get combined modulation for a destination
//==============================================================================

float InstrumentEffectsPlugin::getModulationValue (int destIndex, const InstrumentParams& params,
                                                    double bpm, int numSamples)
{
    if (destIndex < 0 || destIndex >= InstrumentParams::kNumModDests)
        return 0.0f;

    auto& mod = params.modulations[static_cast<size_t> (destIndex)];

    // Check if this destination should use global modulation
    if (isModModeGlobal (destIndex, params))
    {
        switch (mod.type)
        {
            case InstrumentParams::Modulation::Type::LFO:
                return computeGlobalLFO (mod);
            case InstrumentParams::Modulation::Type::Envelope:
                return readGlobalEnvelope (destIndex, mod);
            default:
                return 0.0f;
        }
    }

    // Per-note modulation (existing behavior)
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

//==============================================================================
// DSP processors
//==============================================================================

void InstrumentEffectsPlugin::processFilter (juce::AudioBuffer<float>& buffer, int startSample,
                                              int numSamples, const InstrumentParams& params, float cutoffMult)
{
    if (! filterInitialized)
        return;

    // Reset filter state when type changes (prevents pops from stale internal state)
    if (params.filterType != lastFilterType)
    {
        svfFilter.reset();
        lastFilterType = params.filterType;

        // Snap cutoff smoother to target to avoid sweep artifacts after reset
        int modCutoff = static_cast<int> (static_cast<float> (params.cutoff) * cutoffMult);
        modCutoff = juce::jlimit (0, 100, modCutoff);
        smoothedCutoffHz.setCurrentAndTargetValue (cutoffPercentToHz (modCutoff));
    }

    if (params.filterType == InstrumentParams::FilterType::Disabled)
        return;

    // Apply cutoff modulation as subtractive multiplier (never above set cutoff)
    int modCutoff = static_cast<int> (static_cast<float> (params.cutoff) * cutoffMult);
    modCutoff = juce::jlimit (0, 100, modCutoff);
    float targetFreqHz = cutoffPercentToHz (modCutoff);

    // Clamp frequency well below Nyquist to prevent SVF filter instability
    float maxFreqHz = static_cast<float> (sampleRate) * 0.4f;
    targetFreqHz = juce::jmin (targetFreqHz, maxFreqHz);

    float q = resonancePercentToQ (params.resonance);

    smoothedCutoffHz.setTargetValue (targetFreqHz);
    svfFilter.setResonance (q);

    switch (params.filterType)
    {
        case InstrumentParams::FilterType::LowPass:
            svfFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
            break;
        case InstrumentParams::FilterType::HighPass:
            svfFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
            break;
        case InstrumentParams::FilterType::BandPass:
            svfFilter.setType (juce::dsp::StateVariableTPTFilterType::bandpass);
            break;
        default:
            return;
    }

    // Process in sub-blocks of 32 samples, advancing smoothed cutoff between them
    constexpr int subBlockSize = 32;
    int samplesRemaining = numSamples;
    int offset = startSample;

    while (samplesRemaining > 0)
    {
        int chunkSize = juce::jmin (subBlockSize, samplesRemaining);

        svfFilter.setCutoffFrequency (smoothedCutoffHz.getNextValue());
        smoothedCutoffHz.skip (chunkSize - 1);

        auto block = juce::dsp::AudioBlock<float> (buffer)
                         .getSubBlock (static_cast<size_t> (offset),
                                       static_cast<size_t> (chunkSize));
        auto context = juce::dsp::ProcessContextReplacing<float> (block);
        svfFilter.process (context);

        offset += chunkSize;
        samplesRemaining -= chunkSize;
    }

    // NaN/Inf protection: if the filter produced bad values, clear them and reset
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer (ch, startSample);
        for (int i = 0; i < numSamples; ++i)
        {
            if (! std::isfinite (data[i]))
            {
                // Bad output detected — zero the rest and reset filter
                buffer.clear (startSample, numSamples);
                svfFilter.reset();
                return;
            }
        }
    }
}

void InstrumentEffectsPlugin::processOverdrive (juce::AudioBuffer<float>& buffer, int startSample,
                                                 int numSamples, int overdrive)
{
    if (overdrive <= 0) return;

    float gain = 1.0f + static_cast<float> (overdrive) * 0.29f; // 1.0 to ~30.0

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer (ch, startSample);
        for (int i = 0; i < numSamples; ++i)
            data[i] = std::tanh (gain * data[i]);
    }
}

void InstrumentEffectsPlugin::processBitDepth (juce::AudioBuffer<float>& buffer, int startSample,
                                                int numSamples, int bitDepth)
{
    if (bitDepth >= 16) return;

    float levels = std::pow (2.0f, static_cast<float> (bitDepth));

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer (ch, startSample);
        for (int i = 0; i < numSamples; ++i)
            data[i] = std::round (data[i] * levels) / levels;
    }
}

void InstrumentEffectsPlugin::processVolumeAndPan (juce::AudioBuffer<float>& buffer, int startSample,
                                                    int numSamples, const InstrumentParams& params,
                                                    float volumeGainMult, float panMod)
{
    // Volume: dB to linear, then apply subtractive modulation multiplier
    float baseGain;
    if (params.volume <= -99.0)
        baseGain = 0.0f;
    else
        baseGain = juce::Decibels::decibelsToGain (static_cast<float> (params.volume));

    float gain = baseGain * volumeGainMult;

    // Panning: use override if set (from 8xx effect), otherwise instrument panning
    float basePan;
    if (overrides.panningOverride >= 0)
    {
        // Map CC10 0-127 → -50..+50 (0=hard left, 64=center, 127=hard right)
        basePan = (static_cast<float> (overrides.panningOverride) / 127.0f) * 100.0f - 50.0f;
    }
    else
    {
        basePan = static_cast<float> (params.panning);
    }

    float effectivePan = basePan + panMod * 50.0f;
    effectivePan = juce::jlimit (-50.0f, 50.0f, effectivePan);

    float panNorm = (effectivePan + 50.0f) / 100.0f; // 0=left, 1=right
    float targetLeftGain = gain * std::cos (panNorm * juce::MathConstants<float>::halfPi);
    float targetRightGain = gain * std::sin (panNorm * juce::MathConstants<float>::halfPi);

    smoothedGainL.setTargetValue (targetLeftGain);
    smoothedGainR.setTargetValue (targetRightGain);

    if (buffer.getNumChannels() >= 2)
    {
        auto* left  = buffer.getWritePointer (0, startSample);
        auto* right = buffer.getWritePointer (1, startSample);
        for (int i = 0; i < numSamples; ++i)
        {
            left[i]  *= smoothedGainL.getNextValue();
            right[i] *= smoothedGainR.getNextValue();
        }
    }
    else if (buffer.getNumChannels() >= 1)
    {
        auto* data = buffer.getWritePointer (0, startSample);
        for (int i = 0; i < numSamples; ++i)
            data[i] *= smoothedGainL.getNextValue();
    }
}

//==============================================================================
// Main processing
//==============================================================================

void InstrumentEffectsPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr) return;

    auto& buffer = *fc.destBuffer;
    int startSample = fc.bufferStartSample;
    int numSamples = fc.bufferNumSamples;

    // Increment global block counter and compute transport beat
    blockCounter.fetch_add (1, std::memory_order_relaxed);
    currentTransportBeat = edit.tempoSequence.toBeats (fc.editTime.getStart()).inBeats();

    // Process MIDI to track current instrument and handle CCs/global notes
    if (fc.bufferForMidiMessages != nullptr)
    {
        if (fc.bufferForMidiMessages->isAllNotesOff)
        {
            releaseEnvelopes();

            // Global envelope: decrement note count, release if last note
            if (globalModState != nullptr)
            {
                int count = globalModState->activeNoteCount.fetch_sub (1, std::memory_order_relaxed) - 1;
                if (count <= 0)
                {
                    globalModState->activeNoteCount.store (0, std::memory_order_relaxed);
                    for (auto& es : globalModState->envStates)
                    {
                        int stage = es.stage.load (std::memory_order_relaxed);
                        if (stage != 0) // not Idle
                            es.stage.store (4, std::memory_order_relaxed); // Release
                    }
                }
            }
        }

        for (auto& m : *fc.bufferForMidiMessages)
        {
            if (m.isProgramChange())
            {
                // Multi-instrument support: update current instrument on program change
                currentInstrument = m.getProgramChangeNumber();
            }
            else if (m.isController())
            {
                int ccNum = m.getControllerNumber();
                int ccVal = m.getControllerValue();

                if (ccNum == 10) // Panning override (from 8xx effect)
                {
                    overrides.panningOverride = ccVal;
                }
                else if (ccNum == 85) // Mod mode override (from Exy effect)
                {
                    int dest = (ccVal >> 4) & 0xF;
                    int mode = ccVal & 0xF;

                    if (dest == 0xF) // F = all destinations
                    {
                        for (auto& ov : overrides.modModeOverride)
                            ov = mode;
                    }
                    else if (dest < InstrumentParams::kNumModDests)
                    {
                        overrides.modModeOverride[static_cast<size_t> (dest)] = mode;
                    }
                }
            }
            else if (m.isNoteOn())
            {
                triggerEnvelopes();
                for (auto& lfo : lfoStates)
                    lfo.phase = 0.0;

                // Global envelope: increment note count, trigger if first note
                if (globalModState != nullptr)
                {
                    int prevCount = globalModState->activeNoteCount.fetch_add (1, std::memory_order_relaxed);
                    if (prevCount <= 0)
                    {
                        // First note — trigger all global envelopes
                        for (auto& es : globalModState->envStates)
                        {
                            es.stage.store (1, std::memory_order_relaxed); // Attack
                            es.level.store (0.0f, std::memory_order_relaxed);
                        }
                    }
                }
            }
            else if (m.isNoteOff() || m.isAllNotesOff())
            {
                // Graceful release (OFF) — ADSR release stage plays
                releaseEnvelopes();

                // Global envelope: decrement note count, release if last note
                if (globalModState != nullptr)
                {
                    int count = globalModState->activeNoteCount.fetch_sub (1, std::memory_order_relaxed) - 1;
                    if (count <= 0)
                    {
                        globalModState->activeNoteCount.store (0, std::memory_order_relaxed);
                        for (auto& es : globalModState->envStates)
                        {
                            int stage = es.stage.load (std::memory_order_relaxed);
                            if (stage != 0) // not Idle
                                es.stage.store (4, std::memory_order_relaxed); // Release
                        }
                    }
                }
            }
            else if (m.isAllSoundOff())
            {
                // Hard cut (KILL) — immediate silence, no release tail
                for (auto& env : envStates)
                {
                    env.stage = EnvState::Stage::Idle;
                    env.level = 0.0f;
                }
                noteActive = false;

                // Global: hard reset
                if (globalModState != nullptr)
                {
                    globalModState->activeNoteCount.store (0, std::memory_order_relaxed);
                    for (auto& es : globalModState->envStates)
                    {
                        es.stage.store (0, std::memory_order_relaxed); // Idle
                        es.level.store (0.0f, std::memory_order_relaxed);
                    }
                }
            }
        }
    }

    // Look up current instrument params from sampler
    const InstrumentParams* params = nullptr;

    if (sampler != nullptr && currentInstrument >= 0)
    {
        auto& allParams = sampler->getAllParams();
        auto it = allParams.find (currentInstrument);
        if (it != allParams.end())
            params = &(it->second);
    }

    if (params == nullptr)
        return;

    // Advance global envelopes (once per block across all plugins sharing the state)
    advanceGlobalEnvelopes (*params);

    // Get tempo for LFO sync
    double bpm = edit.tempoSequence.getTempos()[0]->getBpm();

    // Compute modulation values for this block
    // Volume and Cutoff use subtractive modulation (never louder / never above set cutoff)
    // Pan uses additive modulation (swings both directions)

    // --- Volume: subtractive gain multiplier (0.0 = silence, 1.0 = configured volume) ---
    float volumeGainMult = 1.0f;
    {
        auto& volMod = params->modulations[static_cast<size_t> (InstrumentParams::ModDest::Volume)];
        float volAmount = static_cast<float> (volMod.amount) / 100.0f;
        float volScaled = getModulationValue (static_cast<int> (InstrumentParams::ModDest::Volume),
                                               *params, bpm, numSamples);

        if (volMod.type == InstrumentParams::Modulation::Type::Envelope)
            volumeGainMult = juce::jlimit (0.0f, 1.0f, 1.0f - volAmount + volScaled);
        else if (volMod.type == InstrumentParams::Modulation::Type::LFO)
            volumeGainMult = juce::jlimit (0.0f, 1.0f, 1.0f - volAmount * 0.5f + volScaled * 0.5f);
    }

    // --- Pan: additive (swing both directions) ---
    float panMod = getModulationValue (static_cast<int> (InstrumentParams::ModDest::Panning),
                                        *params, bpm, numSamples);

    // --- Cutoff: subtractive multiplier (0.0 = fully closed, 1.0 = set cutoff) ---
    float cutoffMult = 1.0f;
    {
        auto& cutMod = params->modulations[static_cast<size_t> (InstrumentParams::ModDest::Cutoff)];
        float cutAmount = static_cast<float> (cutMod.amount) / 100.0f;
        float cutScaled = getModulationValue (static_cast<int> (InstrumentParams::ModDest::Cutoff),
                                               *params, bpm, numSamples);

        if (cutMod.type == InstrumentParams::Modulation::Type::Envelope)
            cutoffMult = juce::jlimit (0.0f, 1.0f, 1.0f - cutAmount + cutScaled);
        else if (cutMod.type == InstrumentParams::Modulation::Type::LFO)
            cutoffMult = juce::jlimit (0.0f, 1.0f, 1.0f - cutAmount * 0.5f + cutScaled * 0.5f);
    }

    // Advance other modulators even if not directly used here
    getModulationValue (static_cast<int> (InstrumentParams::ModDest::GranularPos),
                        *params, bpm, numSamples);
    getModulationValue (static_cast<int> (InstrumentParams::ModDest::Finetune),
                        *params, bpm, numSamples);

    // DSP chain: Volume/Pan → Filter → Overdrive → BitDepth → Safety Limiter
    processVolumeAndPan (buffer, startSample, numSamples, *params, volumeGainMult, panMod);
    processFilter (buffer, startSample, numSamples, *params, cutoffMult);
    processOverdrive (buffer, startSample, numSamples, params->overdrive);
    processBitDepth (buffer, startSample, numSamples, params->bitDepth);

    // Safety limiter: hard clip to protect ears against any unexpected spikes
    static constexpr float kSafetyLimit = 1.5f; // ~3.5dB headroom max
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer (ch, startSample);
        for (int i = 0; i < numSamples; ++i)
        {
            if (! std::isfinite (data[i]))
                data[i] = 0.0f;
            else
                data[i] = juce::jlimit (-kSafetyLimit, kSafetyLimit, data[i]);
        }
    }

    // Send to delay/reverb buffers (post effects chain, post safety limiter)
    if (sendBuffers != nullptr)
    {
        // Reverb send: convert dB to linear gain
        if (params->reverbSend > -99.0)
        {
            float reverbGain = juce::Decibels::decibelsToGain (static_cast<float> (params->reverbSend));
            sendBuffers->addToReverb (buffer, startSample, numSamples, reverbGain);
        }

        // Delay send: convert dB to linear gain
        if (params->delaySend > -99.0)
        {
            float delayGain = juce::Decibels::decibelsToGain (static_cast<float> (params->delaySend));
            sendBuffers->addToDelay (buffer, startSample, numSamples, delayGain);
        }
    }
}

void InstrumentEffectsPlugin::setInstrumentIndex (int index)
{
    currentInstrument = index;
}
