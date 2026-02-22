#include "SendEffectsPlugin.h"

const char* SendEffectsPlugin::xmlTypeName = "SendEffects";

SendEffectsPlugin::SendEffectsPlugin (te::PluginCreationInfo info)
    : te::Plugin (info)
{
}

SendEffectsPlugin::~SendEffectsPlugin()
{
}

void SendEffectsPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate;

    // Delay line (stereo circular buffer)
    delayLine.setSize (2, kMaxDelaySamples);
    delayLine.clear();
    delayWritePos = 0;

    // Delay feedback filter
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (info.blockSizeSamples);
    spec.numChannels = 1;
    delayFilter.prepare (spec);
    delayFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    delayFilter.setCutoffFrequency (8000.0f);
    delayFilterInitialized = true;

    // Reverb
    reverb.setSampleRate (sampleRate);

    // Pre-delay buffer for reverb (max 100ms)
    preDelayMaxSamples = static_cast<int> (sampleRate * 0.1);
    preDelayBuffer.setSize (2, preDelayMaxSamples);
    preDelayBuffer.clear();
    preDelayWritePos = 0;

    // Scratch buffers
    delayScratch.setSize (2, info.blockSizeSamples);
    reverbInputScratch.setSize (2, info.blockSizeSamples);
    reverbScratch.setSize (2, info.blockSizeSamples);
    delayReturnScratch.setSize (2, info.blockSizeSamples);
    reverbReturnScratch.setSize (2, info.blockSizeSamples);

    // Initialize EQ filters with flat coefficients
    auto flatCoeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sampleRate, 1000.0f, 0.707f, 1.0f);
    delayReturnEqLowL.coefficients = flatCoeffs;  delayReturnEqLowR.coefficients = flatCoeffs;
    delayReturnEqMidL.coefficients = flatCoeffs;   delayReturnEqMidR.coefficients = flatCoeffs;
    delayReturnEqHighL.coefficients = flatCoeffs;  delayReturnEqHighR.coefficients = flatCoeffs;
    reverbReturnEqLowL.coefficients = flatCoeffs;  reverbReturnEqLowR.coefficients = flatCoeffs;
    reverbReturnEqMidL.coefficients = flatCoeffs;   reverbReturnEqMidR.coefficients = flatCoeffs;
    reverbReturnEqHighL.coefficients = flatCoeffs;  reverbReturnEqHighR.coefficients = flatCoeffs;
    masterEqLowL.coefficients = flatCoeffs;  masterEqLowR.coefficients = flatCoeffs;
    masterEqMidL.coefficients = flatCoeffs;   masterEqMidR.coefficients = flatCoeffs;
    masterEqHighL.coefficients = flatCoeffs;  masterEqHighR.coefficients = flatCoeffs;

    masterCompEnvelope = 0.0f;
    masterLimiterEnvelope = 0.0f;
}

void SendEffectsPlugin::deinitialise()
{
    delayLine.clear();
    delayFilter.reset();
    delayFilterInitialized = false;
    reverb.reset();
    preDelayBuffer.clear();
    delayScratch.clear();
    reverbInputScratch.clear();
    reverbScratch.clear();
}

//==============================================================================
// Get delay time in samples based on current params and tempo
//==============================================================================

int SendEffectsPlugin::getDelayTimeSamples() const
{
    if (activeDelayParams.bpmSync)
    {
        // BPM-synced delay: division is the note denominator (4 = quarter, 8 = eighth, etc.)
        double bpm = edit.tempoSequence.getTempos()[0]->getBpm();
        if (bpm <= 0.0) bpm = 120.0;

        // Time for one beat (quarter note) in seconds
        double beatSeconds = 60.0 / bpm;

        // Sync division: 1=whole, 2=half, 4=quarter, 8=eighth, 16=sixteenth, 32=thirty-second
        double divisionSeconds = beatSeconds * (4.0 / static_cast<double> (juce::jmax (1, activeDelayParams.syncDivision)));

        // Dotted note: multiply by 1.5 (e.g., dotted quarter = quarter + eighth)
        if (activeDelayParams.dotted)
            divisionSeconds *= 1.5;

        int samples = static_cast<int> (divisionSeconds * sampleRate);
        return juce::jlimit (1, kMaxDelaySamples - 1, samples);
    }
    else
    {
        // Free time in ms
        int samples = static_cast<int> (activeDelayParams.time * sampleRate / 1000.0);
        return juce::jlimit (1, kMaxDelaySamples - 1, samples);
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

    float wet = static_cast<float> (activeDelayParams.wet) / 100.0f;
    float feedback = static_cast<float> (activeDelayParams.feedback) / 100.0f;
    int delaySamples = getDelayTimeSamples();

    // Ping-pong amount: 0% = normal stereo delay, 100% = full ping-pong
    float pingPong = static_cast<float> (activeDelayParams.stereoWidth) / 100.0f;

    // Setup filter if applicable
    if (delayFilterInitialized && activeDelayParams.filterType > 0)
    {
        float cutoffHz = 20.0f * std::pow (1000.0f, static_cast<float> (activeDelayParams.filterCutoff) / 100.0f);
        cutoffHz = juce::jmin (cutoffHz, static_cast<float> (sampleRate) * 0.4f);
        delayFilter.setCutoffFrequency (cutoffHz);

        if (activeDelayParams.filterType == 1)
            delayFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        else
            delayFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    }

    // Process delay with circular buffer
    int channels = juce::jmin (2, output.getNumChannels());

    for (int i = 0; i < numSamples; ++i)
    {
        // Read from delay line
        int readPos = delayWritePos - delaySamples;
        if (readPos < 0) readPos += kMaxDelaySamples;

        float delayedL = delayLine.getSample (0, readPos);
        float delayedR = (channels > 1) ? delayLine.getSample (1, readPos) : delayedL;

        // Apply filter to feedback signal
        if (delayFilterInitialized && activeDelayParams.filterType > 0)
        {
            float mono = (delayedL + delayedR) * 0.5f;
            float filtered = delayFilter.processSample (0, mono);
            delayedL = filtered + (delayedL - mono);
            delayedR = filtered + (delayedR - mono);
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

        // Soft clip feedback to prevent runaway
        finalWriteL = std::tanh (finalWriteL);
        finalWriteR = std::tanh (finalWriteR);

        delayLine.setSample (0, delayWritePos, finalWriteL);
        if (channels > 1)
            delayLine.setSample (1, delayWritePos, finalWriteR);

        delayWritePos = (delayWritePos + 1) % kMaxDelaySamples;

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

    float wet = static_cast<float> (activeReverbParams.wet) / 100.0f;
    if (wet <= 0.0f) return;

    // Configure juce::Reverb parameters
    juce::Reverb::Parameters rvParams;
    rvParams.roomSize   = static_cast<float> (activeReverbParams.roomSize) / 100.0f;
    rvParams.damping    = static_cast<float> (activeReverbParams.damping) / 100.0f;
    rvParams.wetLevel   = wet;
    rvParams.dryLevel   = 0.0f; // We only want the wet signal
    rvParams.width      = 1.0f;
    rvParams.freezeMode = 0.0f;

    // Map decay to room size blend (decay affects both roomSize and wet)
    float decayFactor = static_cast<float> (activeReverbParams.decay) / 100.0f;
    rvParams.roomSize = juce::jlimit (0.0f, 1.0f, rvParams.roomSize * (0.5f + decayFactor * 0.5f));

    reverb.setParameters (rvParams);

    // Pre-delay: read from a circular buffer offset by preDelay ms
    int preDelaySamples = static_cast<int> (activeReverbParams.preDelay * sampleRate / 1000.0);
    preDelaySamples = juce::jlimit (0, preDelayMaxSamples - 1, preDelaySamples);

    int channels = juce::jmin (2, output.getNumChannels());

    // Copy send buffer through pre-delay into scratch buffer
    reverbScratch.setSize (2, numSamples, false, false, true);
    reverbScratch.clear();

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

    // Copy params from pending (UI thread) to active (audio thread)
    {
        const juce::SpinLock::ScopedLockType lock (paramLock);
        activeDelayParams = pendingDelayParams;
        activeReverbParams = pendingReverbParams;
    }

    // Capture and clear this block slice atomically from shared send buffers.
    sendBuffers->consumeSlice (delayScratch, reverbInputScratch, startSample, numSamples, 2);

    // Process delay and reverb into separate scratch buffers for send return processing
    delayReturnScratch.setSize (2, numSamples, false, false, true);
    delayReturnScratch.clear();
    reverbReturnScratch.setSize (2, numSamples, false, false, true);
    reverbReturnScratch.clear();

    processDelay (delayScratch, delayReturnScratch, 0, numSamples);
    processReverb (reverbInputScratch, reverbReturnScratch, 0, numSamples);

    // Apply send return channel processing (EQ, volume, pan)
    if (mixerStatePtr != nullptr)
    {
        auto& delayReturn = mixerStatePtr->sendReturns[0];
        auto& reverbReturn = mixerStatePtr->sendReturns[1];

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

        // Master processing: EQ -> Compressor -> Limiter -> Volume/Pan
        processMasterEQ (buffer, startSample, numSamples);
        processMasterCompressor (buffer, startSample, numSamples);
        processMasterLimiter (buffer, startSample, numSamples);

        // Master volume and pan
        auto& master = mixerStatePtr->master;
        float masterGain;
        if (master.volume <= -99.0)
            masterGain = 0.0f;
        else
            masterGain = juce::Decibels::decibelsToGain (static_cast<float> (master.volume));

        float panNorm = (static_cast<float> (master.pan) + 50.0f) / 100.0f;
        float masterGainL = masterGain * std::cos (panNorm * juce::MathConstants<float>::halfPi);
        float masterGainR = masterGain * std::sin (panNorm * juce::MathConstants<float>::halfPi);

        if (buffer.getNumChannels() >= 2)
        {
            auto* left  = buffer.getWritePointer (0, startSample);
            auto* right = buffer.getWritePointer (1, startSample);
            for (int i = 0; i < numSamples; ++i)
            {
                left[i]  *= masterGainL;
                right[i] *= masterGainR;
            }
        }
        else if (buffer.getNumChannels() >= 1)
        {
            auto* data = buffer.getWritePointer (0, startSample);
            for (int i = 0; i < numSamples; ++i)
                data[i] *= masterGainL;
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
    else
    {
        // No mixer state: just add delay/reverb directly (legacy behavior)
        for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
        {
            buffer.addFrom (ch, startSample, delayReturnScratch, ch, 0, numSamples);
            buffer.addFrom (ch, startSample, reverbReturnScratch, ch, 0, numSamples);
        }
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

//==============================================================================
// Send return EQ processing
//==============================================================================

void SendEffectsPlugin::processSendReturnEQ (juce::AudioBuffer<float>& buffer, int numSamples,
                                              const SendReturnState& state,
                                              juce::dsp::IIR::Filter<float>& eqLowL,
                                              juce::dsp::IIR::Filter<float>& eqLowR,
                                              juce::dsp::IIR::Filter<float>& eqMidL,
                                              juce::dsp::IIR::Filter<float>& eqMidR,
                                              juce::dsp::IIR::Filter<float>& eqHighL,
                                              juce::dsp::IIR::Filter<float>& eqHighR)
{
    bool hasEQ = state.eqLowGain != 0.0 || state.eqMidGain != 0.0 || state.eqHighGain != 0.0;
    if (! hasEQ) return;

    {
        float gain = (state.eqLowGain != 0.0)
                         ? juce::Decibels::decibelsToGain (static_cast<float> (state.eqLowGain))
                         : 1.0f;
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf (sampleRate, 200.0f, 0.707f, gain);
        eqLowL.coefficients = coeffs;
        eqLowR.coefficients = coeffs;
    }
    {
        float gain = (state.eqMidGain != 0.0)
                         ? juce::Decibels::decibelsToGain (static_cast<float> (state.eqMidGain))
                         : 1.0f;
        float freq = juce::jlimit (200.0f, 8000.0f, static_cast<float> (state.eqMidFreq));
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sampleRate, freq, 1.0f, gain);
        eqMidL.coefficients = coeffs;
        eqMidR.coefficients = coeffs;
    }
    {
        float gain = (state.eqHighGain != 0.0)
                         ? juce::Decibels::decibelsToGain (static_cast<float> (state.eqHighGain))
                         : 1.0f;
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sampleRate, 4000.0f, 0.707f, gain);
        eqHighL.coefficients = coeffs;
        eqHighR.coefficients = coeffs;
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
                                                   const SendReturnState& state)
{
    float gain;
    if (state.volume <= -99.0)
        gain = 0.0f;
    else
        gain = juce::Decibels::decibelsToGain (static_cast<float> (state.volume));

    float panNorm = (static_cast<float> (state.pan) + 50.0f) / 100.0f;
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
// Master EQ
//==============================================================================

void SendEffectsPlugin::processMasterEQ (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    if (mixerStatePtr == nullptr) return;

    auto& master = mixerStatePtr->master;
    bool hasEQ = master.eqLowGain != 0.0 || master.eqMidGain != 0.0 || master.eqHighGain != 0.0;
    if (! hasEQ) return;

    {
        float gain = (master.eqLowGain != 0.0)
                         ? juce::Decibels::decibelsToGain (static_cast<float> (master.eqLowGain))
                         : 1.0f;
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf (sampleRate, 200.0f, 0.707f, gain);
        masterEqLowL.coefficients = coeffs;
        masterEqLowR.coefficients = coeffs;
    }
    {
        float gain = (master.eqMidGain != 0.0)
                         ? juce::Decibels::decibelsToGain (static_cast<float> (master.eqMidGain))
                         : 1.0f;
        float freq = juce::jlimit (200.0f, 8000.0f, static_cast<float> (master.eqMidFreq));
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sampleRate, freq, 1.0f, gain);
        masterEqMidL.coefficients = coeffs;
        masterEqMidR.coefficients = coeffs;
    }
    {
        float gain = (master.eqHighGain != 0.0)
                         ? juce::Decibels::decibelsToGain (static_cast<float> (master.eqHighGain))
                         : 1.0f;
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sampleRate, 4000.0f, 0.707f, gain);
        masterEqHighL.coefficients = coeffs;
        masterEqHighR.coefficients = coeffs;
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
    if (mixerStatePtr == nullptr) return;

    auto& master = mixerStatePtr->master;
    if (master.compThreshold >= 0.0 && master.compRatio <= 1.0) return;

    float thresholdLinear = juce::Decibels::decibelsToGain (static_cast<float> (master.compThreshold));
    float ratio = static_cast<float> (juce::jmax (1.0, master.compRatio));
    float attackCoeff  = std::exp (-1.0f / (static_cast<float> (master.compAttack) * 0.001f * static_cast<float> (sampleRate)));
    float releaseCoeff = std::exp (-1.0f / (static_cast<float> (master.compRelease) * 0.001f * static_cast<float> (sampleRate)));

    int numChannels = buffer.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            peak = juce::jmax (peak, std::abs (buffer.getSample (ch, startSample + i)));

        if (peak > masterCompEnvelope)
            masterCompEnvelope = attackCoeff * masterCompEnvelope + (1.0f - attackCoeff) * peak;
        else
            masterCompEnvelope = releaseCoeff * masterCompEnvelope + (1.0f - releaseCoeff) * peak;

        float gain = 1.0f;
        if (masterCompEnvelope > thresholdLinear && thresholdLinear > 0.0f)
        {
            float overDB = juce::Decibels::gainToDecibels (masterCompEnvelope / thresholdLinear);
            float reductionDB = overDB * (1.0f - 1.0f / ratio);
            gain = juce::Decibels::decibelsToGain (-reductionDB);
        }

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer (ch)[startSample + i] *= gain;
    }
}

//==============================================================================
// Master Limiter (brickwall)
//==============================================================================

void SendEffectsPlugin::processMasterLimiter (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    if (mixerStatePtr == nullptr) return;

    auto& master = mixerStatePtr->master;
    if (master.limiterThreshold >= 0.0) return;  // 0 dB = off

    float thresholdLinear = juce::Decibels::decibelsToGain (static_cast<float> (master.limiterThreshold));
    float releaseCoeff = std::exp (-1.0f / (static_cast<float> (master.limiterRelease) * 0.001f * static_cast<float> (sampleRate)));

    int numChannels = buffer.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            peak = juce::jmax (peak, std::abs (buffer.getSample (ch, startSample + i)));

        float targetGain = 1.0f;
        if (peak > thresholdLinear && thresholdLinear > 0.0f)
            targetGain = thresholdLinear / peak;

        // Fast attack (essentially instant), slow release
        if (targetGain < masterLimiterEnvelope)
            masterLimiterEnvelope = targetGain;
        else
            masterLimiterEnvelope = releaseCoeff * masterLimiterEnvelope + (1.0f - releaseCoeff) * targetGain;

        masterLimiterEnvelope = juce::jlimit (0.0f, 1.0f, masterLimiterEnvelope);

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer (ch)[startSample + i] *= masterLimiterEnvelope;
    }
}
