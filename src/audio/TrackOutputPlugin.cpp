#include "TrackOutputPlugin.h"
#include "DspUtils.h"

const char* TrackOutputPlugin::xmlTypeName = "TrackOutput";

TrackOutputPlugin::TrackOutputPlugin (te::PluginCreationInfo info)
    : te::Plugin (info)
{
}

TrackOutputPlugin::~TrackOutputPlugin()
{
}

void TrackOutputPlugin::AtomicOutputState::store (const TrackMixState& state)
{
    sequence.fetch_add (1, std::memory_order_release);
    volume.store (static_cast<float> (juce::jlimit (-100.0, 12.0, state.volume)), std::memory_order_relaxed);
    pan.store (juce::jlimit (-50, 50, state.pan), std::memory_order_relaxed);
    reverbSend.store (static_cast<float> (juce::jlimit (-100.0, 0.0, state.reverbSend)), std::memory_order_relaxed);
    delaySend.store (static_cast<float> (juce::jlimit (-100.0, 0.0, state.delaySend)), std::memory_order_relaxed);
    sequence.fetch_add (1, std::memory_order_release);
}

bool TrackOutputPlugin::AtomicOutputState::loadConsistent (TrackMixState& state) const
{
    const auto before = sequence.load (std::memory_order_acquire);
    if ((before & 1u) != 0u)
        return false;

    TrackMixState snapshot = state;
    snapshot.volume = volume.load (std::memory_order_relaxed);
    snapshot.pan = pan.load (std::memory_order_relaxed);
    snapshot.reverbSend = reverbSend.load (std::memory_order_relaxed);
    snapshot.delaySend = delaySend.load (std::memory_order_relaxed);

    const auto after = sequence.load (std::memory_order_acquire);
    if (before != after || (after & 1u) != 0u)
        return false;

    state = snapshot;
    return true;
}

void TrackOutputPlugin::setMixState (const TrackMixState& s)
{
    sharedMixState.store (s);
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
    const auto gains = DspUtils::getEqualPowerPanGains (localMixState.volume, localMixState.pan);

    smoothedGainL.setTargetValue (gains.left);
    smoothedGainR.setTargetValue (gains.right);

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

    sharedMixState.loadConsistent (localMixState);

    auto& buffer = *fc.destBuffer;
    int startSample = fc.bufferStartSample;
    int numSamples = fc.bufferNumSamples;

    // DSP chain: Volume/Pan -> Post-fader Sends
    processVolumeAndPan (buffer, startSample, numSamples);
    processSends (buffer, startSample, numSamples);

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
