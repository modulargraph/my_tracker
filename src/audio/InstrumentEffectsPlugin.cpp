#include "InstrumentEffectsPlugin.h"
#include "SimpleSampler.h"

const char* InstrumentEffectsPlugin::xmlTypeName = "InstrumentEffects";

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
    return 0.5f + p * 9.5f; // 0.5 to 10.0
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
// Get combined modulation for a destination
//==============================================================================

float InstrumentEffectsPlugin::getModulationValue (int destIndex, const InstrumentParams& params,
                                                    double bpm, int numSamples)
{
    if (destIndex < 0 || destIndex >= InstrumentParams::kNumModDests)
        return 0.0f;

    auto& mod = params.modulations[static_cast<size_t> (destIndex)];

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
                                              int numSamples, const InstrumentParams& params, float cutoffMod)
{
    if (params.filterType == InstrumentParams::FilterType::Disabled || ! filterInitialized)
        return;

    // Apply cutoff modulation (-1..+1 maps to roughly +/- 50% of cutoff range)
    int modCutoff = juce::jlimit (0, 100, params.cutoff + static_cast<int> (cutoffMod * 50.0f));
    float targetFreqHz = cutoffPercentToHz (modCutoff);
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
                                                    float volumeMod, float panMod)
{
    // Volume: dB to linear, with modulation (-1..+1 maps to +/- 24dB)
    double effectiveVolume = params.volume + static_cast<double> (volumeMod) * 24.0;
    float gain;
    if (effectiveVolume <= -99.0)
        gain = 0.0f;
    else
        gain = juce::Decibels::decibelsToGain (static_cast<float> (effectiveVolume));

    // Panning: -50 to +50, with modulation (-1..+1 maps to +/- 50)
    float effectivePan = static_cast<float> (params.panning) + panMod * 50.0f;
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

    // Process MIDI to track current instrument
    if (fc.bufferForMidiMessages != nullptr)
    {
        if (fc.bufferForMidiMessages->isAllNotesOff)
        {
            releaseEnvelopes();
        }

        for (auto& m : *fc.bufferForMidiMessages)
        {
            if (m.isProgramChange())
            {
                // Multi-instrument support: update current instrument on program change
                currentInstrument = m.getProgramChangeNumber();
            }
            else if (m.isNoteOn())
            {
                triggerEnvelopes();
                for (auto& lfo : lfoStates)
                    lfo.phase = 0.0;
            }
            else if (m.isNoteOff() || m.isAllNotesOff())
            {
                // Graceful release (OFF) — ADSR release stage plays
                releaseEnvelopes();
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

    // Get tempo for LFO sync
    double bpm = edit.tempoSequence.getTempos()[0]->getBpm();

    // Compute modulation values for this block
    float volumeMod = getModulationValue (static_cast<int> (InstrumentParams::ModDest::Volume),
                                           *params, bpm, numSamples);
    float panMod = getModulationValue (static_cast<int> (InstrumentParams::ModDest::Panning),
                                        *params, bpm, numSamples);
    float cutoffMod = getModulationValue (static_cast<int> (InstrumentParams::ModDest::Cutoff),
                                           *params, bpm, numSamples);

    // Advance other modulators even if not directly used here
    getModulationValue (static_cast<int> (InstrumentParams::ModDest::GranularPos),
                        *params, bpm, numSamples);
    getModulationValue (static_cast<int> (InstrumentParams::ModDest::Finetune),
                        *params, bpm, numSamples);

    // DSP chain: Volume/Pan → Filter → Overdrive → BitDepth
    processVolumeAndPan (buffer, startSample, numSamples, *params, volumeMod, panMod);
    processFilter (buffer, startSample, numSamples, *params, cutoffMod);
    processOverdrive (buffer, startSample, numSamples, params->overdrive);
    processBitDepth (buffer, startSample, numSamples, params->bitDepth);
}

void InstrumentEffectsPlugin::setInstrumentIndex (int index)
{
    currentInstrument = index;
}
