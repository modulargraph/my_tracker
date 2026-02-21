#include "TrackOutputPlugin.h"

const char* TrackOutputPlugin::xmlTypeName = "TrackOutput";

TrackOutputPlugin::TrackOutputPlugin (te::PluginCreationInfo info)
    : te::Plugin (info)
{
}

TrackOutputPlugin::~TrackOutputPlugin()
{
}

void TrackOutputPlugin::setMixState (const TrackMixState& s)
{
    const juce::SpinLock::ScopedLockType lock (mixStateLock);
    sharedMixState = s;
}

void TrackOutputPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate;

    double rampSeconds = 0.008;
    smoothedGainL.reset (sampleRate, rampSeconds);
    smoothedGainR.reset (sampleRate, rampSeconds);
}

void TrackOutputPlugin::deinitialise()
{
}

//==============================================================================
// Volume and Pan (from mixer state)
//==============================================================================

void TrackOutputPlugin::processVolumeAndPan (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    float gain;
    if (localMixState.volume <= -99.0)
        gain = 0.0f;
    else
        gain = juce::Decibels::decibelsToGain (static_cast<float> (localMixState.volume));

    float panNorm = (static_cast<float> (localMixState.pan) + 50.0f) / 100.0f;
    float targetLeftGain  = gain * std::cos (panNorm * juce::MathConstants<float>::halfPi);
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
// Sends (mixer-level delay/reverb)
//==============================================================================

void TrackOutputPlugin::processSends (const juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    if (sendBuffers == nullptr) return;

    if (localMixState.reverbSend > -99.0)
    {
        float reverbGain = juce::Decibels::decibelsToGain (static_cast<float> (localMixState.reverbSend));
        sendBuffers->addToReverb (buffer, startSample, numSamples, reverbGain);
    }

    if (localMixState.delaySend > -99.0)
    {
        float delayGain = juce::Decibels::decibelsToGain (static_cast<float> (localMixState.delaySend));
        sendBuffers->addToDelay (buffer, startSample, numSamples, delayGain);
    }
}

//==============================================================================
// Main processing
//==============================================================================

void TrackOutputPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr) return;

    {
        const juce::SpinLock::ScopedLockType lock (mixStateLock);
        localMixState = sharedMixState;
    }

    auto& buffer = *fc.destBuffer;
    int startSample = fc.bufferStartSample;
    int numSamples = fc.bufferNumSamples;

    // DSP chain: Pre-fader Sends -> Volume/Pan
    processSends (buffer, startSample, numSamples);
    processVolumeAndPan (buffer, startSample, numSamples);

    // Compute post-fader peak level for metering
    float peak = 0.0f;
    int numChannels = buffer.getNumChannels();
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto mag = buffer.getMagnitude (ch, startSample, numSamples);
        if (mag > peak) peak = mag;
    }
    float prev = peakLevel.load (std::memory_order_relaxed);
    if (peak > prev)
        peakLevel.store (peak, std::memory_order_relaxed);
}
