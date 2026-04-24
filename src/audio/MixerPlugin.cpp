#include "MixerPlugin.h"
#include "DspUtils.h"

const char* MixerPlugin::xmlTypeName = "MixerChannel";

MixerPlugin::MixerPlugin (te::PluginCreationInfo info)
    : te::Plugin (info)
{
}

MixerPlugin::~MixerPlugin()
{
}

void MixerPlugin::setMixState (const TrackMixState& s)
{
    const juce::SpinLock::ScopedLockType lock (mixStateLock);
    sharedMixState = s;
}

void MixerPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate;

    double rampSeconds = 0.008;
    smoothedGainL.reset (sampleRate, rampSeconds);
    smoothedGainR.reset (sampleRate, rampSeconds);

    // Initialize EQ filters with flat coefficients
    auto flatCoeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sampleRate, 1000.0f, 0.707f, 1.0f);
    eqLowL.coefficients = flatCoeffs;
    eqLowR.coefficients = flatCoeffs;
    eqMidL.coefficients = flatCoeffs;
    eqMidR.coefficients = flatCoeffs;
    eqHighL.coefficients = flatCoeffs;
    eqHighR.coefficients = flatCoeffs;

    eqLowL.reset();
    eqLowR.reset();
    eqMidL.reset();
    eqMidR.reset();
    eqHighL.reset();
    eqHighR.reset();

    compEnvelope = 0.0f;
}

void MixerPlugin::deinitialise()
{
    eqLowL.reset();
    eqLowR.reset();
    eqMidL.reset();
    eqMidR.reset();
    eqHighL.reset();
    eqHighR.reset();
}

//==============================================================================
// EQ: 3-band (low shelf ~200Hz, parametric mid, high shelf ~4kHz)
//==============================================================================

void MixerPlugin::processEQ (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    DspUtils::process3BandEQ (buffer, startSample, numSamples, sampleRate,
                              localMixState.eqLowGain, localMixState.eqMidGain,
                              localMixState.eqHighGain, localMixState.eqMidFreq,
                              eqLowL, eqLowR, eqMidL, eqMidR, eqHighL, eqHighR);
}

//==============================================================================
// Compressor (simple feed-forward)
//==============================================================================

void MixerPlugin::processCompressor (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    DspUtils::processCompressor (buffer, startSample, numSamples, sampleRate, compEnvelope,
                                 localMixState.compThreshold, localMixState.compRatio,
                                 localMixState.compAttack, localMixState.compRelease);
}

//==============================================================================
// Volume and Pan (from mixer state)
//==============================================================================

void MixerPlugin::processVolumeAndPan (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
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

void MixerPlugin::processSends (const juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
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

void MixerPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr) return;

    // Copy UI-updated state to the audio-thread working copy.
    {
        const juce::SpinLock::ScopedLockType lock (mixStateLock);
        localMixState = sharedMixState;
    }

    auto& buffer = *fc.destBuffer;
    int startSample = fc.bufferStartSample;
    int numSamples = fc.bufferNumSamples;

    // DSP chain: EQ → Compressor → Volume/Pan → Post-fader Sends
    processEQ (buffer, startSample, numSamples);
    processCompressor (buffer, startSample, numSamples);
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
    // Decay: keep existing peak if it's higher (UI will decay it)
    float prev = peakLevel.load (std::memory_order_relaxed);
    if (peak > prev)
        peakLevel.store (peak, std::memory_order_relaxed);
}
