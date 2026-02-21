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

    // Scratch buffer
    delayScratch.setSize (2, info.blockSizeSamples);
    reverbInputScratch.setSize (2, info.blockSizeSamples);
    reverbScratch.setSize (2, info.blockSizeSamples);
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

    // Process delay and reverb from the captured slice into output.
    processDelay (delayScratch, buffer, startSample, numSamples);
    processReverb (reverbInputScratch, buffer, startSample, numSamples);

    // Safety limiter
    static constexpr float kSafetyLimit = 1.5f;
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
