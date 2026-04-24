#include "SendEffectsPlugin.h"
#include "DspUtils.h"

const char* SendEffectsPlugin::xmlTypeName = "SendEffects";

namespace
{
constexpr int kMinimumSendBufferSamples = 8192;

float percentToUnit (double value)
{
    return juce::jlimit (0.0f, 1.0f, static_cast<float> (value) / 100.0f);
}

float softLimitDelaySample (float value)
{
    if (! std::isfinite (value))
        return 0.0f;

    static constexpr float kLinearLimit = 2.0f;
    if (std::abs (value) <= kLinearLimit)
        return value;

    return kLinearLimit * std::tanh (value / kLinearLimit);
}

template <typename Filter>
void assignBiquadCoefficients (Filter& filter, const std::array<float, 6>& coeffs)
{
    *filter.coefficients = coeffs;
}

template <typename FilterL, typename FilterR>
void assignStereoBiquadCoefficients (FilterL& left, FilterR& right, const std::array<float, 6>& coeffs)
{
    assignBiquadCoefficients (left, coeffs);
    assignBiquadCoefficients (right, coeffs);
}
} // namespace

SendEffectsPlugin::SendEffectsPlugin (te::PluginCreationInfo info)
    : te::Plugin (info)
{
}

SendEffectsPlugin::~SendEffectsPlugin()
{
}

void SendEffectsPlugin::AtomicDelayParams::store (const DelayParams& params)
{
    sequence.fetch_add (1, std::memory_order_release);
    time.store (static_cast<float> (juce::jlimit (1.0, 2000.0, params.time)), std::memory_order_relaxed);
    syncDivision.store (juce::jlimit (1, 32, params.syncDivision), std::memory_order_relaxed);
    bpmSync.store (params.bpmSync, std::memory_order_relaxed);
    dotted.store (params.dotted, std::memory_order_relaxed);
    feedback.store (static_cast<float> (juce::jlimit (0.0, 100.0, params.feedback)), std::memory_order_relaxed);
    filterType.store (juce::jlimit (0, 2, params.filterType), std::memory_order_relaxed);
    filterCutoff.store (static_cast<float> (juce::jlimit (0.0, 100.0, params.filterCutoff)), std::memory_order_relaxed);
    wet.store (static_cast<float> (juce::jlimit (0.0, 100.0, params.wet)), std::memory_order_relaxed);
    stereoWidth.store (static_cast<float> (juce::jlimit (0.0, 100.0, params.stereoWidth)), std::memory_order_relaxed);
    sequence.fetch_add (1, std::memory_order_release);
}

bool SendEffectsPlugin::AtomicDelayParams::loadConsistent (DelayParams& params) const
{
    const auto before = sequence.load (std::memory_order_acquire);
    if ((before & 1u) != 0u)
        return false;

    DelayParams snapshot;
    snapshot.time = time.load (std::memory_order_relaxed);
    snapshot.syncDivision = syncDivision.load (std::memory_order_relaxed);
    snapshot.bpmSync = bpmSync.load (std::memory_order_relaxed);
    snapshot.dotted = dotted.load (std::memory_order_relaxed);
    snapshot.feedback = feedback.load (std::memory_order_relaxed);
    snapshot.filterType = filterType.load (std::memory_order_relaxed);
    snapshot.filterCutoff = filterCutoff.load (std::memory_order_relaxed);
    snapshot.wet = wet.load (std::memory_order_relaxed);
    snapshot.stereoWidth = stereoWidth.load (std::memory_order_relaxed);

    const auto after = sequence.load (std::memory_order_acquire);
    if (before != after || (after & 1u) != 0u)
        return false;

    params = snapshot;
    return true;
}

DelayParams SendEffectsPlugin::AtomicDelayParams::loadRelaxed() const
{
    DelayParams params;
    params.time = time.load (std::memory_order_relaxed);
    params.syncDivision = syncDivision.load (std::memory_order_relaxed);
    params.bpmSync = bpmSync.load (std::memory_order_relaxed);
    params.dotted = dotted.load (std::memory_order_relaxed);
    params.feedback = feedback.load (std::memory_order_relaxed);
    params.filterType = filterType.load (std::memory_order_relaxed);
    params.filterCutoff = filterCutoff.load (std::memory_order_relaxed);
    params.wet = wet.load (std::memory_order_relaxed);
    params.stereoWidth = stereoWidth.load (std::memory_order_relaxed);
    return params;
}

void SendEffectsPlugin::AtomicReverbParams::store (const ReverbParams& params)
{
    sequence.fetch_add (1, std::memory_order_release);
    roomSize.store (static_cast<float> (juce::jlimit (0.0, 100.0, params.roomSize)), std::memory_order_relaxed);
    decay.store (static_cast<float> (juce::jlimit (0.0, 100.0, params.decay)), std::memory_order_relaxed);
    damping.store (static_cast<float> (juce::jlimit (0.0, 100.0, params.damping)), std::memory_order_relaxed);
    preDelay.store (static_cast<float> (juce::jlimit (0.0, 100.0, params.preDelay)), std::memory_order_relaxed);
    wet.store (static_cast<float> (juce::jlimit (0.0, 100.0, params.wet)), std::memory_order_relaxed);
    sequence.fetch_add (1, std::memory_order_release);
}

bool SendEffectsPlugin::AtomicReverbParams::loadConsistent (ReverbParams& params) const
{
    const auto before = sequence.load (std::memory_order_acquire);
    if ((before & 1u) != 0u)
        return false;

    ReverbParams snapshot;
    snapshot.roomSize = roomSize.load (std::memory_order_relaxed);
    snapshot.decay = decay.load (std::memory_order_relaxed);
    snapshot.damping = damping.load (std::memory_order_relaxed);
    snapshot.preDelay = preDelay.load (std::memory_order_relaxed);
    snapshot.wet = wet.load (std::memory_order_relaxed);

    const auto after = sequence.load (std::memory_order_acquire);
    if (before != after || (after & 1u) != 0u)
        return false;

    params = snapshot;
    return true;
}

ReverbParams SendEffectsPlugin::AtomicReverbParams::loadRelaxed() const
{
    ReverbParams params;
    params.roomSize = roomSize.load (std::memory_order_relaxed);
    params.decay = decay.load (std::memory_order_relaxed);
    params.damping = damping.load (std::memory_order_relaxed);
    params.preDelay = preDelay.load (std::memory_order_relaxed);
    params.wet = wet.load (std::memory_order_relaxed);
    return params;
}

void SendEffectsPlugin::AtomicSendReturnState::store (const SendReturnState& state)
{
    sequence.fetch_add (1, std::memory_order_release);
    volume.store (static_cast<float> (juce::jlimit (-100.0, 12.0, state.volume)), std::memory_order_relaxed);
    pan.store (juce::jlimit (-50, 50, state.pan), std::memory_order_relaxed);
    muted.store (state.muted, std::memory_order_relaxed);
    eqLowGain.store (static_cast<float> (juce::jlimit (-12.0, 12.0, state.eqLowGain)), std::memory_order_relaxed);
    eqMidGain.store (static_cast<float> (juce::jlimit (-12.0, 12.0, state.eqMidGain)), std::memory_order_relaxed);
    eqHighGain.store (static_cast<float> (juce::jlimit (-12.0, 12.0, state.eqHighGain)), std::memory_order_relaxed);
    eqMidFreq.store (static_cast<float> (juce::jlimit (200.0, 8000.0, state.eqMidFreq)), std::memory_order_relaxed);
    sequence.fetch_add (1, std::memory_order_release);
}

bool SendEffectsPlugin::AtomicSendReturnState::loadConsistent (SendReturnState& state) const
{
    const auto before = sequence.load (std::memory_order_acquire);
    if ((before & 1u) != 0u)
        return false;

    SendReturnState snapshot;
    snapshot.volume = volume.load (std::memory_order_relaxed);
    snapshot.pan = pan.load (std::memory_order_relaxed);
    snapshot.muted = muted.load (std::memory_order_relaxed);
    snapshot.eqLowGain = eqLowGain.load (std::memory_order_relaxed);
    snapshot.eqMidGain = eqMidGain.load (std::memory_order_relaxed);
    snapshot.eqHighGain = eqHighGain.load (std::memory_order_relaxed);
    snapshot.eqMidFreq = eqMidFreq.load (std::memory_order_relaxed);

    const auto after = sequence.load (std::memory_order_acquire);
    if (before != after || (after & 1u) != 0u)
        return false;

    state = snapshot;
    return true;
}

void SendEffectsPlugin::AtomicMasterMixState::store (const MasterMixState& state)
{
    sequence.fetch_add (1, std::memory_order_release);
    volume.store (static_cast<float> (juce::jlimit (-100.0, 12.0, state.volume)), std::memory_order_relaxed);
    pan.store (juce::jlimit (-50, 50, state.pan), std::memory_order_relaxed);
    eqLowGain.store (static_cast<float> (juce::jlimit (-12.0, 12.0, state.eqLowGain)), std::memory_order_relaxed);
    eqMidGain.store (static_cast<float> (juce::jlimit (-12.0, 12.0, state.eqMidGain)), std::memory_order_relaxed);
    eqHighGain.store (static_cast<float> (juce::jlimit (-12.0, 12.0, state.eqHighGain)), std::memory_order_relaxed);
    eqMidFreq.store (static_cast<float> (juce::jlimit (200.0, 8000.0, state.eqMidFreq)), std::memory_order_relaxed);
    compThreshold.store (static_cast<float> (juce::jlimit (-60.0, 0.0, state.compThreshold)), std::memory_order_relaxed);
    compRatio.store (static_cast<float> (juce::jlimit (1.0, 20.0, state.compRatio)), std::memory_order_relaxed);
    compAttack.store (static_cast<float> (juce::jlimit (0.1, 100.0, state.compAttack)), std::memory_order_relaxed);
    compRelease.store (static_cast<float> (juce::jlimit (10.0, 1000.0, state.compRelease)), std::memory_order_relaxed);
    limiterThreshold.store (static_cast<float> (juce::jlimit (-24.0, 0.0, state.limiterThreshold)), std::memory_order_relaxed);
    limiterRelease.store (static_cast<float> (juce::jlimit (1.0, 500.0, state.limiterRelease)), std::memory_order_relaxed);
    sequence.fetch_add (1, std::memory_order_release);
}

bool SendEffectsPlugin::AtomicMasterMixState::loadConsistent (MasterMixState& state) const
{
    const auto before = sequence.load (std::memory_order_acquire);
    if ((before & 1u) != 0u)
        return false;

    MasterMixState snapshot;
    snapshot.volume = volume.load (std::memory_order_relaxed);
    snapshot.pan = pan.load (std::memory_order_relaxed);
    snapshot.eqLowGain = eqLowGain.load (std::memory_order_relaxed);
    snapshot.eqMidGain = eqMidGain.load (std::memory_order_relaxed);
    snapshot.eqHighGain = eqHighGain.load (std::memory_order_relaxed);
    snapshot.eqMidFreq = eqMidFreq.load (std::memory_order_relaxed);
    snapshot.compThreshold = compThreshold.load (std::memory_order_relaxed);
    snapshot.compRatio = compRatio.load (std::memory_order_relaxed);
    snapshot.compAttack = compAttack.load (std::memory_order_relaxed);
    snapshot.compRelease = compRelease.load (std::memory_order_relaxed);
    snapshot.limiterThreshold = limiterThreshold.load (std::memory_order_relaxed);
    snapshot.limiterRelease = limiterRelease.load (std::memory_order_relaxed);

    const auto after = sequence.load (std::memory_order_acquire);
    if (before != after || (after & 1u) != 0u)
        return false;

    state = snapshot;
    return true;
}

void SendEffectsPlugin::setSendBuffers (SendBuffers* buffers)
{
    sendBuffers = buffers;
    if (sendBuffers != nullptr && sendBufferCapacitySamples > 0)
        sendBuffers->prepare (sendBufferCapacitySamples, 2);
}

void SendEffectsPlugin::setMixerState (MixerState* mixState)
{
    mixerStatePtr = mixState;
    if (mixerStatePtr != nullptr)
    {
        refreshMixerStateSnapshot();
        hasMixerState.store (true, std::memory_order_release);
    }
    else
    {
        hasMixerState.store (false, std::memory_order_release);
        pendingSendReturns[0].store (SendReturnState {});
        pendingSendReturns[1].store (SendReturnState {});
        pendingMasterState.store (MasterMixState {});
    }
}

void SendEffectsPlugin::refreshMixerStateSnapshot()
{
    if (mixerStatePtr == nullptr)
        return;

    pendingSendReturns[0].store (mixerStatePtr->sendReturns[0]);
    pendingSendReturns[1].store (mixerStatePtr->sendReturns[1]);
    pendingMasterState.store (mixerStatePtr->master);
}

void SendEffectsPlugin::setDelayParams (const DelayParams& params)
{
    pendingDelayParams.store (params);
}

void SendEffectsPlugin::setReverbParams (const ReverbParams& params)
{
    pendingReverbParams.store (params);
}

DelayParams SendEffectsPlugin::getDelayParams() const
{
    return pendingDelayParams.loadRelaxed();
}

ReverbParams SendEffectsPlugin::getReverbParams() const
{
    return pendingReverbParams.loadRelaxed();
}

void SendEffectsPlugin::setTempoBpm (double bpm)
{
    tempoBpm.store (static_cast<float> (juce::jlimit (20.0, 999.0, bpm)), std::memory_order_relaxed);
}

void SendEffectsPlugin::configurePreparedBuffers (int blockSize)
{
    maxScratchSamples = juce::jmax (1, blockSize);
    sendBufferCapacitySamples = juce::jmax (kMinimumSendBufferSamples, maxScratchSamples * 2);

    delayScratch.setSize (2, maxScratchSamples);
    reverbInputScratch.setSize (2, maxScratchSamples);
    reverbScratch.setSize (2, maxScratchSamples);
    delayReturnScratch.setSize (2, maxScratchSamples);
    reverbReturnScratch.setSize (2, maxScratchSamples);

    delayScratch.clear();
    reverbInputScratch.clear();
    reverbScratch.clear();
    delayReturnScratch.clear();
    reverbReturnScratch.clear();

    if (sendBuffers != nullptr)
        sendBuffers->prepare (sendBufferCapacitySamples, 2);
}

void SendEffectsPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate;

    // Delay line (stereo circular buffer)
    const int maxDelaySamples = juce::jmax (1, static_cast<int> (std::ceil (sampleRate * kMaxDelaySeconds)) + 1);
    delayLine.setSize (2, maxDelaySamples);
    delayLine.clear();
    delayWritePos = 0;

    // Delay feedback filter
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (juce::jmax (1, info.blockSizeSamples));
    spec.numChannels = 1;
    delayFilterL.prepare (spec);
    delayFilterR.prepare (spec);
    delayFilterL.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    delayFilterR.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    delayFilterL.setCutoffFrequency (8000.0f);
    delayFilterR.setCutoffFrequency (8000.0f);
    delayFilterInitialized = true;

    // Reverb
    reverb.setSampleRate (sampleRate);

    // Pre-delay buffer for reverb (max 100ms)
    preDelayMaxSamples = juce::jmax (1, static_cast<int> (std::ceil (sampleRate * 0.1)) + 1);
    preDelayBuffer.setSize (2, preDelayMaxSamples);
    preDelayBuffer.clear();
    preDelayWritePos = 0;

    configurePreparedBuffers (info.blockSizeSamples);

    // Initialize EQ filters with flat coefficients
    auto flatCoeffs = juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter (sampleRate, 1000.0f, 0.707f, 1.0f);
    assignStereoBiquadCoefficients (delayReturnEqLowL, delayReturnEqLowR, flatCoeffs);
    assignStereoBiquadCoefficients (delayReturnEqMidL, delayReturnEqMidR, flatCoeffs);
    assignStereoBiquadCoefficients (delayReturnEqHighL, delayReturnEqHighR, flatCoeffs);
    assignStereoBiquadCoefficients (reverbReturnEqLowL, reverbReturnEqLowR, flatCoeffs);
    assignStereoBiquadCoefficients (reverbReturnEqMidL, reverbReturnEqMidR, flatCoeffs);
    assignStereoBiquadCoefficients (reverbReturnEqHighL, reverbReturnEqHighR, flatCoeffs);
    assignStereoBiquadCoefficients (masterEqLowL, masterEqLowR, flatCoeffs);
    assignStereoBiquadCoefficients (masterEqMidL, masterEqMidR, flatCoeffs);
    assignStereoBiquadCoefficients (masterEqHighL, masterEqHighR, flatCoeffs);

    for (int i = 0; i < kMaxGroupBuses; ++i)
    {
        DspUtils::initFlatEQ (sampleRate,
                              groupEqLowL[static_cast<size_t> (i)],
                              groupEqLowR[static_cast<size_t> (i)],
                              groupEqMidL[static_cast<size_t> (i)],
                              groupEqMidR[static_cast<size_t> (i)],
                              groupEqHighL[static_cast<size_t> (i)],
                              groupEqHighR[static_cast<size_t> (i)]);
        groupCompEnvelopes[static_cast<size_t> (i)] = 0.0f;
    }

    masterCompEnvelope = 0.0f;
    masterLimiterEnvelope = 1.0f;
}

void SendEffectsPlugin::deinitialise()
{
    delayLine.clear();
    delayFilterL.reset();
    delayFilterR.reset();
    delayFilterInitialized = false;
    reverb.reset();
    preDelayBuffer.clear();
    delayScratch.clear();
    reverbInputScratch.clear();
    reverbScratch.clear();
    for (auto& buffer : groupScratch)
        buffer.clear();
}

//==============================================================================
// Get delay time in samples based on current params and tempo
//==============================================================================

int SendEffectsPlugin::getDelayTimeSamples() const
{
    if (activeDelayParams.bpmSync)
    {
        // BPM-synced delay: division is the note denominator (4 = quarter, 8 = eighth, etc.)
        double bpm = static_cast<double> (tempoBpm.load (std::memory_order_relaxed));
        if (bpm <= 0.0) bpm = 120.0;

        // Time for one beat (quarter note) in seconds
        double beatSeconds = 60.0 / bpm;

        // Sync division: 1=whole, 2=half, 4=quarter, 8=eighth, 16=sixteenth, 32=thirty-second
        double divisionSeconds = beatSeconds * (4.0 / static_cast<double> (juce::jmax (1, activeDelayParams.syncDivision)));

        // Dotted note: multiply by 1.5 (e.g., dotted quarter = quarter + eighth)
        if (activeDelayParams.dotted)
            divisionSeconds *= 1.5;

        int samples = static_cast<int> (divisionSeconds * sampleRate);
        return juce::jlimit (1, getDelayLineSize() - 1, samples);
    }
    else
    {
        // Free time in ms
        int samples = static_cast<int> (activeDelayParams.time * sampleRate / 1000.0);
        return juce::jlimit (1, getDelayLineSize() - 1, samples);
    }
}

//==============================================================================
// Process delay
//==============================================================================

void SendEffectsPlugin::processDelay (const juce::AudioBuffer<float>& input,
                                      juce::AudioBuffer<float>& output,
                                      int startSample,
                                      int numSamples)
{
    if (numSamples <= 0) return;

    float wet = percentToUnit (activeDelayParams.wet);
    float feedback = juce::jlimit (0.0f, 0.98f, percentToUnit (activeDelayParams.feedback));
    int delaySamples = getDelayTimeSamples();

    // Ping-pong amount: 0% = normal stereo delay, 100% = full ping-pong
    float pingPong = percentToUnit (activeDelayParams.stereoWidth);

    // Setup filter if applicable
    if (delayFilterInitialized && activeDelayParams.filterType > 0)
    {
        float cutoffHz = 20.0f * std::pow (1000.0f, static_cast<float> (activeDelayParams.filterCutoff) / 100.0f);
        cutoffHz = juce::jmin (cutoffHz, static_cast<float> (sampleRate) * 0.4f);
        delayFilterL.setCutoffFrequency (cutoffHz);
        delayFilterR.setCutoffFrequency (cutoffHz);

        if (activeDelayParams.filterType == 1)
        {
            delayFilterL.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
            delayFilterR.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        }
        else
        {
            delayFilterL.setType (juce::dsp::StateVariableTPTFilterType::highpass);
            delayFilterR.setType (juce::dsp::StateVariableTPTFilterType::highpass);
        }
    }

    // Process delay with circular buffer
    int channels = juce::jmin (2, output.getNumChannels());

    for (int i = 0; i < numSamples; ++i)
    {
        // Read from delay line
        int readPos = delayWritePos - delaySamples;
        if (readPos < 0) readPos += getDelayLineSize();

        float delayedL = delayLine.getSample (0, readPos);
        float delayedR = (channels > 1) ? delayLine.getSample (1, readPos) : delayedL;

        // Apply filter to feedback signal
        if (delayFilterInitialized && activeDelayParams.filterType > 0)
        {
            delayedL = delayFilterL.processSample (0, delayedL);
            delayedR = delayFilterR.processSample (0, delayedR);
        }

        // Get input from captured send slice
        float inputL = input.getSample (0, juce::jmin (i, input.getNumSamples() - 1));
        float inputR = (channels > 1 && input.getNumChannels() > 1)
                            ? input.getSample (1, juce::jmin (i, input.getNumSamples() - 1))
                            : inputL;

        // Standard stereo delay: each channel feeds back into itself
        float stdWriteL = inputL + delayedL * feedback;
        float stdWriteR = inputR + delayedR * feedback;

        // Ping-pong delay: cross-feed (L output feeds R input and vice versa)
        float ppWriteL = inputL + delayedR * feedback;
        float ppWriteR = inputR + delayedL * feedback;

        // Blend between standard and ping-pong based on pingPong amount
        float finalWriteL = stdWriteL + (ppWriteL - stdWriteL) * pingPong;
        float finalWriteR = stdWriteR + (ppWriteR - stdWriteR) * pingPong;

        // Keep pathological feedback or send spikes finite without colouring normal levels.
        finalWriteL = softLimitDelaySample (finalWriteL);
        finalWriteR = softLimitDelaySample (finalWriteR);

        delayLine.setSample (0, delayWritePos, finalWriteL);
        if (channels > 1)
            delayLine.setSample (1, delayWritePos, finalWriteR);

        delayWritePos = (delayWritePos + 1) % getDelayLineSize();

        // Add wet signal to output
        if (channels > 0)
            output.addSample (0, startSample + i, delayedL * wet);
        if (channels > 1)
            output.addSample (1, startSample + i, delayedR * wet);
    }
}

//==============================================================================
// Process reverb
//==============================================================================

void SendEffectsPlugin::processReverb (const juce::AudioBuffer<float>& input,
                                       juce::AudioBuffer<float>& output,
                                       int startSample,
                                       int numSamples)
{
    if (numSamples <= 0) return;

    float wet = percentToUnit (activeReverbParams.wet);
    if (wet <= 0.0f) return;

    // Configure juce::Reverb parameters
    juce::Reverb::Parameters rvParams;
    rvParams.roomSize   = percentToUnit (activeReverbParams.roomSize);
    rvParams.damping    = percentToUnit (activeReverbParams.damping);
    rvParams.wetLevel   = wet;
    rvParams.dryLevel   = 0.0f; // We only want the wet signal
    rvParams.width      = 1.0f;
    rvParams.freezeMode = 0.0f;

    // Map decay to room size blend (decay affects both roomSize and wet)
    float decayFactor = percentToUnit (activeReverbParams.decay);
    rvParams.roomSize = juce::jlimit (0.0f, 1.0f, rvParams.roomSize * (0.5f + decayFactor * 0.5f));

    reverb.setParameters (rvParams);

    // Pre-delay: read from a circular buffer offset by preDelay ms
    int preDelaySamples = static_cast<int> (activeReverbParams.preDelay * sampleRate / 1000.0);
    preDelaySamples = juce::jlimit (0, preDelayMaxSamples - 1, preDelaySamples);

    int channels = juce::jmin (2, output.getNumChannels());

    // Copy send buffer through pre-delay into scratch buffer
    for (int ch = 0; ch < reverbScratch.getNumChannels(); ++ch)
        reverbScratch.clear (ch, 0, juce::jmin (numSamples, reverbScratch.getNumSamples()));

    for (int i = 0; i < numSamples; ++i)
    {
        // Write current input into pre-delay buffer
        float inL = input.getSample (0, juce::jmin (i, input.getNumSamples() - 1));
        float inR = (channels > 1 && input.getNumChannels() > 1)
                        ? input.getSample (1, juce::jmin (i, input.getNumSamples() - 1))
                        : inL;

        preDelayBuffer.setSample (0, preDelayWritePos, inL);
        if (preDelayBuffer.getNumChannels() > 1)
            preDelayBuffer.setSample (1, preDelayWritePos, inR);

        // Read from pre-delay buffer
        int readPos = preDelayWritePos - preDelaySamples;
        if (readPos < 0) readPos += preDelayMaxSamples;

        reverbScratch.setSample (0, i, preDelayBuffer.getSample (0, readPos));
        if (channels > 1 && preDelayBuffer.getNumChannels() > 1)
            reverbScratch.setSample (1, i, preDelayBuffer.getSample (1, readPos));
        else if (channels > 1)
            reverbScratch.setSample (1, i, preDelayBuffer.getSample (0, readPos));

        preDelayWritePos = (preDelayWritePos + 1) % preDelayMaxSamples;
    }

    // Process reverb in-place on the scratch buffer
    if (channels >= 2)
    {
        reverb.processStereo (reverbScratch.getWritePointer (0),
                              reverbScratch.getWritePointer (1),
                              numSamples);
    }
    else
    {
        reverb.processMono (reverbScratch.getWritePointer (0), numSamples);
    }

    // Add processed reverb to output
    for (int ch = 0; ch < channels; ++ch)
        output.addFrom (ch, startSample, reverbScratch, ch, 0, numSamples);
}

//==============================================================================
// Main processing
//==============================================================================

void SendEffectsPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || sendBuffers == nullptr)
        return;

    auto& buffer = *fc.destBuffer;
    int startSample = fc.bufferStartSample;
    int numSamples = fc.bufferNumSamples;

    if (numSamples <= 0)
        return;

    pendingDelayParams.loadConsistent (activeDelayParams);
    pendingReverbParams.loadConsistent (activeReverbParams);
    const bool useMixerState = hasMixerState.load (std::memory_order_acquire);
    if (useMixerState)
    {
        pendingSendReturns[0].loadConsistent (activeSendReturns[0]);
        pendingSendReturns[1].loadConsistent (activeSendReturns[1]);
        pendingMasterState.loadConsistent (activeMasterState);
    }

    const int chunkCapacity = juce::jmax (1, maxScratchSamples);
    int offset = 0;
    while (offset < numSamples)
    {
        const int chunkSamples = juce::jmin (chunkCapacity, numSamples - offset);
        processSendEffectsChunk (buffer, startSample + offset, chunkSamples, useMixerState);
        offset += chunkSamples;
    }

    if (useMixerState)
    {
        processGroupBuses (buffer, startSample, numSamples);

        // Master processing: EQ -> Compressor -> Limiter -> Volume/Pan
        processMasterEQ (buffer, startSample, numSamples);
        processMasterCompressor (buffer, startSample, numSamples);
        processMasterLimiter (buffer, startSample, numSamples);

        // Master volume and pan
        auto& master = activeMasterState;
        const auto masterGains = DspUtils::getEqualPowerPanGains (master.volume, master.pan);

        if (buffer.getNumChannels() >= 2)
        {
            auto* left  = buffer.getWritePointer (0, startSample);
            auto* right = buffer.getWritePointer (1, startSample);
            for (int i = 0; i < numSamples; ++i)
            {
                left[i]  *= masterGains.left;
                right[i] *= masterGains.right;
            }
        }
        else if (buffer.getNumChannels() >= 1)
        {
            auto* data = buffer.getWritePointer (0, startSample);
            for (int i = 0; i < numSamples; ++i)
                data[i] *= masterGains.left;
        }

        // Master peak metering
        float peak = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto mag = buffer.getMagnitude (ch, startSample, numSamples);
            if (mag > peak) peak = mag;
        }
        float prev = masterPeakLevel.load (std::memory_order_relaxed);
        if (peak > prev)
            masterPeakLevel.store (peak, std::memory_order_relaxed);
    }

    // Safety limiter
    static constexpr float kSafetyLimit = 4.0f;
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
}

void SendEffectsPlugin::processSendEffectsChunk (juce::AudioBuffer<float>& buffer,
                                                 int startSample,
                                                 int numSamples,
                                                 bool useMixerState)
{
    if (numSamples <= 0)
        return;

    sendBuffers->consumeSliceIntoPrepared (delayScratch, reverbInputScratch, startSample, numSamples, 2);

    for (int ch = 0; ch < delayReturnScratch.getNumChannels(); ++ch)
    {
        delayReturnScratch.clear (ch, 0, numSamples);
        reverbReturnScratch.clear (ch, 0, numSamples);
    }

    processDelay (delayScratch, delayReturnScratch, 0, numSamples);
    processReverb (reverbInputScratch, reverbReturnScratch, 0, numSamples);

    // Apply send return channel processing (EQ, volume, pan)
    if (useMixerState)
    {
        auto& delayReturn = activeSendReturns[0];
        auto& reverbReturn = activeSendReturns[1];

        if (! delayReturn.muted)
        {
            processSendReturnEQ (delayReturnScratch, numSamples, delayReturn,
                                 delayReturnEqLowL, delayReturnEqLowR,
                                 delayReturnEqMidL, delayReturnEqMidR,
                                 delayReturnEqHighL, delayReturnEqHighR);
            applySendReturnVolumePan (delayReturnScratch, numSamples, delayReturn);

            for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
                buffer.addFrom (ch, startSample, delayReturnScratch, ch, 0, numSamples);
        }

        if (! reverbReturn.muted)
        {
            processSendReturnEQ (reverbReturnScratch, numSamples, reverbReturn,
                                 reverbReturnEqLowL, reverbReturnEqLowR,
                                 reverbReturnEqMidL, reverbReturnEqMidR,
                                 reverbReturnEqHighL, reverbReturnEqHighR);
            applySendReturnVolumePan (reverbReturnScratch, numSamples, reverbReturn);

            for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
                buffer.addFrom (ch, startSample, reverbReturnScratch, ch, 0, numSamples);
        }
    }
    else
    {
        // No mixer state: just add delay/reverb directly (legacy behavior)
        for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
        {
            buffer.addFrom (ch, startSample, delayReturnScratch, ch, 0, numSamples);
            buffer.addFrom (ch, startSample, reverbReturnScratch, ch, 0, numSamples);
        }
    }
}

//==============================================================================
// Group bus processing
//==============================================================================

void SendEffectsPlugin::processGroupBuses (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    if (mixerStatePtr == nullptr || groupRoutingBuffers == nullptr)
        return;

    const int numChannels = juce::jmin (2, buffer.getNumChannels());
    if (numChannels <= 0)
        return;

    bool anyGroupSoloed = false;
    for (const auto& groupState : mixerStatePtr->groupBuses)
    {
        if (groupState.soloed)
        {
            anyGroupSoloed = true;
            break;
        }
    }

    for (int groupIndex = 0; groupIndex < kMaxGroupBuses; ++groupIndex)
    {
        auto& scratch = groupScratch[static_cast<size_t> (groupIndex)];
        groupRoutingBuffers->consumeGroupSlice (groupIndex, scratch, startSample, numSamples, numChannels);

        const auto& groupState = mixerStatePtr->groupBuses[static_cast<size_t> (groupIndex)];
        if (groupState.muted || (anyGroupSoloed && ! groupState.soloed))
            continue;

        DspUtils::process3BandEQ (scratch, 0, numSamples, sampleRate,
                                  groupState.eqLowGain,
                                  groupState.eqMidGain,
                                  groupState.eqHighGain,
                                  groupState.eqMidFreq,
                                  groupEqLowL[static_cast<size_t> (groupIndex)],
                                  groupEqLowR[static_cast<size_t> (groupIndex)],
                                  groupEqMidL[static_cast<size_t> (groupIndex)],
                                  groupEqMidR[static_cast<size_t> (groupIndex)],
                                  groupEqHighL[static_cast<size_t> (groupIndex)],
                                  groupEqHighR[static_cast<size_t> (groupIndex)]);

        DspUtils::processCompressor (scratch, 0, numSamples, sampleRate,
                                     groupCompEnvelopes[static_cast<size_t> (groupIndex)],
                                     groupState.compThreshold,
                                     groupState.compRatio,
                                     groupState.compAttack,
                                     groupState.compRelease);

        applyGroupBusVolumePan (scratch, numSamples, groupState);

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.addFrom (ch, startSample, scratch, ch, 0, numSamples);
    }
}

void SendEffectsPlugin::applyGroupBusVolumePan (juce::AudioBuffer<float>& buffer, int numSamples,
                                                const GroupBusState& groupState)
{
    float gain;
    if (groupState.volume <= -99.0)
        gain = 0.0f;
    else
        gain = juce::Decibels::decibelsToGain (static_cast<float> (groupState.volume));

    float panNorm = (static_cast<float> (groupState.pan) + 50.0f) / 100.0f;
    float gainL = gain * std::cos (panNorm * juce::MathConstants<float>::halfPi);
    float gainR = gain * std::sin (panNorm * juce::MathConstants<float>::halfPi);

    if (buffer.getNumChannels() >= 2)
    {
        auto* left  = buffer.getWritePointer (0);
        auto* right = buffer.getWritePointer (1);
        for (int i = 0; i < numSamples; ++i)
        {
            left[i]  *= gainL;
            right[i] *= gainR;
        }
    }
    else if (buffer.getNumChannels() >= 1)
    {
        auto* data = buffer.getWritePointer (0);
        for (int i = 0; i < numSamples; ++i)
            data[i] *= gainL;
    }
}

//==============================================================================
// Send return EQ processing
//==============================================================================

void SendEffectsPlugin::processSendReturnEQ (juce::AudioBuffer<float>& buffer, int numSamples,
                                             const SendReturnState& sendState,
                                             juce::dsp::IIR::Filter<float>& eqLowL,
                                             juce::dsp::IIR::Filter<float>& eqLowR,
                                             juce::dsp::IIR::Filter<float>& eqMidL,
                                             juce::dsp::IIR::Filter<float>& eqMidR,
                                             juce::dsp::IIR::Filter<float>& eqHighL,
                                             juce::dsp::IIR::Filter<float>& eqHighR)
{
    bool hasEQ = sendState.eqLowGain != 0.0 || sendState.eqMidGain != 0.0 || sendState.eqHighGain != 0.0;
    if (! hasEQ) return;

    {
        float gain = (sendState.eqLowGain != 0.0)
                         ? juce::Decibels::decibelsToGain (static_cast<float> (sendState.eqLowGain))
                         : 1.0f;
        auto coeffs = juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf (sampleRate, 200.0f, 0.707f, gain);
        assignStereoBiquadCoefficients (eqLowL, eqLowR, coeffs);
    }
    {
        float gain = (sendState.eqMidGain != 0.0)
                         ? juce::Decibels::decibelsToGain (static_cast<float> (sendState.eqMidGain))
                         : 1.0f;
        float freq = juce::jlimit (200.0f, 8000.0f, static_cast<float> (sendState.eqMidFreq));
        auto coeffs = juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter (sampleRate, freq, 1.0f, gain);
        assignStereoBiquadCoefficients (eqMidL, eqMidR, coeffs);
    }
    {
        float gain = (sendState.eqHighGain != 0.0)
                         ? juce::Decibels::decibelsToGain (static_cast<float> (sendState.eqHighGain))
                         : 1.0f;
        auto coeffs = juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (sampleRate, 4000.0f, 0.707f, gain);
        assignStereoBiquadCoefficients (eqHighL, eqHighR, coeffs);
    }

    if (buffer.getNumChannels() >= 2)
    {
        auto* left  = buffer.getWritePointer (0);
        auto* right = buffer.getWritePointer (1);
        for (int i = 0; i < numSamples; ++i)
        {
            left[i]  = eqLowL.processSample (left[i]);
            right[i] = eqLowR.processSample (right[i]);
            left[i]  = eqMidL.processSample (left[i]);
            right[i] = eqMidR.processSample (right[i]);
            left[i]  = eqHighL.processSample (left[i]);
            right[i] = eqHighR.processSample (right[i]);
        }
    }
    else if (buffer.getNumChannels() >= 1)
    {
        auto* data = buffer.getWritePointer (0);
        for (int i = 0; i < numSamples; ++i)
        {
            data[i] = eqLowL.processSample (data[i]);
            data[i] = eqMidL.processSample (data[i]);
            data[i] = eqHighL.processSample (data[i]);
        }
    }
}

void SendEffectsPlugin::applySendReturnVolumePan (juce::AudioBuffer<float>& buffer, int numSamples,
                                                  const SendReturnState& sendState)
{
    const auto gains = DspUtils::getEqualPowerPanGains (sendState.volume, sendState.pan);

    if (buffer.getNumChannels() >= 2)
    {
        auto* left  = buffer.getWritePointer (0);
        auto* right = buffer.getWritePointer (1);
        for (int i = 0; i < numSamples; ++i)
        {
            left[i]  *= gains.left;
            right[i] *= gains.right;
        }
    }
    else if (buffer.getNumChannels() >= 1)
    {
        auto* data = buffer.getWritePointer (0);
        for (int i = 0; i < numSamples; ++i)
            data[i] *= gains.left;
    }
}

//==============================================================================
// Master EQ
//==============================================================================

void SendEffectsPlugin::processMasterEQ (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    auto& master = activeMasterState;
    bool hasEQ = master.eqLowGain != 0.0 || master.eqMidGain != 0.0 || master.eqHighGain != 0.0;
    if (! hasEQ) return;

    {
        float gain = (master.eqLowGain != 0.0)
                         ? juce::Decibels::decibelsToGain (static_cast<float> (master.eqLowGain))
                         : 1.0f;
        auto coeffs = juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf (sampleRate, 200.0f, 0.707f, gain);
        assignStereoBiquadCoefficients (masterEqLowL, masterEqLowR, coeffs);
    }
    {
        float gain = (master.eqMidGain != 0.0)
                         ? juce::Decibels::decibelsToGain (static_cast<float> (master.eqMidGain))
                         : 1.0f;
        float freq = juce::jlimit (200.0f, 8000.0f, static_cast<float> (master.eqMidFreq));
        auto coeffs = juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter (sampleRate, freq, 1.0f, gain);
        assignStereoBiquadCoefficients (masterEqMidL, masterEqMidR, coeffs);
    }
    {
        float gain = (master.eqHighGain != 0.0)
                         ? juce::Decibels::decibelsToGain (static_cast<float> (master.eqHighGain))
                         : 1.0f;
        auto coeffs = juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (sampleRate, 4000.0f, 0.707f, gain);
        assignStereoBiquadCoefficients (masterEqHighL, masterEqHighR, coeffs);
    }

    if (buffer.getNumChannels() >= 2)
    {
        auto* left  = buffer.getWritePointer (0, startSample);
        auto* right = buffer.getWritePointer (1, startSample);
        for (int i = 0; i < numSamples; ++i)
        {
            left[i]  = masterEqLowL.processSample (left[i]);
            right[i] = masterEqLowR.processSample (right[i]);
            left[i]  = masterEqMidL.processSample (left[i]);
            right[i] = masterEqMidR.processSample (right[i]);
            left[i]  = masterEqHighL.processSample (left[i]);
            right[i] = masterEqHighR.processSample (right[i]);
        }
    }
}

//==============================================================================
// Master Compressor
//==============================================================================

void SendEffectsPlugin::processMasterCompressor (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    auto& master = activeMasterState;
    DspUtils::processCompressor (buffer, startSample, numSamples, sampleRate, masterCompEnvelope,
                                 master.compThreshold, master.compRatio,
                                 master.compAttack, master.compRelease);
}

//==============================================================================
// Master Limiter (brickwall)
//==============================================================================

void SendEffectsPlugin::processMasterLimiter (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    auto& master = activeMasterState;
    DspUtils::processPeakLimiter (buffer, startSample, numSamples, sampleRate, masterLimiterEnvelope,
                                  master.limiterThreshold, master.limiterRelease);
}
