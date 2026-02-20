#include "MixerPlugin.h"

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
    bool hasEQ = localMixState.eqLowGain != 0.0 || localMixState.eqMidGain != 0.0 || localMixState.eqHighGain != 0.0;
    if (! hasEQ) return;

    // Always update coefficients (flat = gain 1.0 when disabled), so filters
    // reset properly when gain transitions back to zero
    {
        float gain = (localMixState.eqLowGain != 0.0)
                         ? juce::Decibels::decibelsToGain (static_cast<float> (localMixState.eqLowGain))
                         : 1.0f;
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf (sampleRate, 200.0f, 0.707f, gain);
        eqLowL.coefficients = coeffs;
        eqLowR.coefficients = coeffs;
    }
    {
        float gain = (localMixState.eqMidGain != 0.0)
                         ? juce::Decibels::decibelsToGain (static_cast<float> (localMixState.eqMidGain))
                         : 1.0f;
        float freq = juce::jlimit (200.0f, 8000.0f, static_cast<float> (localMixState.eqMidFreq));
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sampleRate, freq, 1.0f, gain);
        eqMidL.coefficients = coeffs;
        eqMidR.coefficients = coeffs;
    }
    {
        float gain = (localMixState.eqHighGain != 0.0)
                         ? juce::Decibels::decibelsToGain (static_cast<float> (localMixState.eqHighGain))
                         : 1.0f;
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sampleRate, 4000.0f, 0.707f, gain);
        eqHighL.coefficients = coeffs;
        eqHighR.coefficients = coeffs;
    }

    // Process all bands through the filters (flat coefficients are pass-through)
    if (buffer.getNumChannels() >= 2)
    {
        auto* left  = buffer.getWritePointer (0, startSample);
        auto* right = buffer.getWritePointer (1, startSample);

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
        auto* data = buffer.getWritePointer (0, startSample);
        for (int i = 0; i < numSamples; ++i)
        {
            data[i] = eqLowL.processSample (data[i]);
            data[i] = eqMidL.processSample (data[i]);
            data[i] = eqHighL.processSample (data[i]);
        }
    }
}

//==============================================================================
// Compressor (simple feed-forward)
//==============================================================================

void MixerPlugin::processCompressor (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{

    if (localMixState.compThreshold >= 0.0 && localMixState.compRatio <= 1.0) return; // Effectively off

    float thresholdLinear = juce::Decibels::decibelsToGain (static_cast<float> (localMixState.compThreshold));
    float ratio = static_cast<float> (juce::jmax (1.0, localMixState.compRatio));

    float attackCoeff  = std::exp (-1.0f / (static_cast<float> (localMixState.compAttack) * 0.001f * static_cast<float> (sampleRate)));
    float releaseCoeff = std::exp (-1.0f / (static_cast<float> (localMixState.compRelease) * 0.001f * static_cast<float> (sampleRate)));

    int numChannels = buffer.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        // Peak detection across channels
        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            peak = juce::jmax (peak, std::abs (buffer.getSample (ch, startSample + i)));

        // Envelope follower
        if (peak > compEnvelope)
            compEnvelope = attackCoeff * compEnvelope + (1.0f - attackCoeff) * peak;
        else
            compEnvelope = releaseCoeff * compEnvelope + (1.0f - releaseCoeff) * peak;

        // Gain computation
        float gain = 1.0f;
        if (compEnvelope > thresholdLinear && thresholdLinear > 0.0f)
        {
            float overDB = juce::Decibels::gainToDecibels (compEnvelope / thresholdLinear);
            float reductionDB = overDB * (1.0f - 1.0f / ratio);
            gain = juce::Decibels::decibelsToGain (-reductionDB);
        }

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer (ch)[startSample + i] *= gain;
    }
}

//==============================================================================
// Volume and Pan (from mixer state)
//==============================================================================

void MixerPlugin::processVolumeAndPan (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{


    // Volume: dB to linear
    float gain;
    if (localMixState.volume <= -99.0)
        gain = 0.0f;
    else
        gain = juce::Decibels::decibelsToGain (static_cast<float> (localMixState.volume));

    // Pan: -50 to +50 → 0.0 to 1.0
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

    // DSP chain: EQ → Compressor → Pre-fader Sends → Volume/Pan
    processEQ (buffer, startSample, numSamples);
    processCompressor (buffer, startSample, numSamples);
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
    // Decay: keep existing peak if it's higher (UI will decay it)
    float prev = peakLevel.load (std::memory_order_relaxed);
    if (peak > prev)
        peakLevel.store (peak, std::memory_order_relaxed);
}
