#include "MixerPlugin.h"

const char* MixerPlugin::xmlTypeName = "MixerChannel";

MixerPlugin::MixerPlugin (te::PluginCreationInfo info)
    : te::Plugin (info)
{
}

MixerPlugin::~MixerPlugin()
{
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
    if (mixState == nullptr) return;

    bool hasEQ = mixState->eqLowGain != 0.0 || mixState->eqMidGain != 0.0 || mixState->eqHighGain != 0.0;
    if (! hasEQ) return;

    // Low shelf at 200 Hz
    if (mixState->eqLowGain != 0.0)
    {
        float gain = juce::Decibels::decibelsToGain (static_cast<float> (mixState->eqLowGain));
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf (sampleRate, 200.0f, 0.707f, gain);
        eqLowL.coefficients = coeffs;
        eqLowR.coefficients = coeffs;
    }

    // Parametric mid
    if (mixState->eqMidGain != 0.0)
    {
        float gain = juce::Decibels::decibelsToGain (static_cast<float> (mixState->eqMidGain));
        float freq = juce::jlimit (200.0f, 8000.0f, static_cast<float> (mixState->eqMidFreq));
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sampleRate, freq, 1.0f, gain);
        eqMidL.coefficients = coeffs;
        eqMidR.coefficients = coeffs;
    }

    // High shelf at 4 kHz
    if (mixState->eqHighGain != 0.0)
    {
        float gain = juce::Decibels::decibelsToGain (static_cast<float> (mixState->eqHighGain));
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sampleRate, 4000.0f, 0.707f, gain);
        eqHighL.coefficients = coeffs;
        eqHighR.coefficients = coeffs;
    }

    // Process each sample
    if (buffer.getNumChannels() >= 2)
    {
        auto* left  = buffer.getWritePointer (0, startSample);
        auto* right = buffer.getWritePointer (1, startSample);

        for (int i = 0; i < numSamples; ++i)
        {
            if (mixState->eqLowGain != 0.0)
            {
                left[i]  = eqLowL.processSample (left[i]);
                right[i] = eqLowR.processSample (right[i]);
            }
            if (mixState->eqMidGain != 0.0)
            {
                left[i]  = eqMidL.processSample (left[i]);
                right[i] = eqMidR.processSample (right[i]);
            }
            if (mixState->eqHighGain != 0.0)
            {
                left[i]  = eqHighL.processSample (left[i]);
                right[i] = eqHighR.processSample (right[i]);
            }
        }
    }
    else if (buffer.getNumChannels() >= 1)
    {
        auto* data = buffer.getWritePointer (0, startSample);
        for (int i = 0; i < numSamples; ++i)
        {
            if (mixState->eqLowGain != 0.0)  data[i] = eqLowL.processSample (data[i]);
            if (mixState->eqMidGain != 0.0)   data[i] = eqMidL.processSample (data[i]);
            if (mixState->eqHighGain != 0.0)  data[i] = eqHighL.processSample (data[i]);
        }
    }
}

//==============================================================================
// Compressor (simple feed-forward)
//==============================================================================

void MixerPlugin::processCompressor (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    if (mixState == nullptr) return;
    if (mixState->compThreshold >= 0.0 && mixState->compRatio <= 1.0) return; // Effectively off

    float thresholdLinear = juce::Decibels::decibelsToGain (static_cast<float> (mixState->compThreshold));
    float ratio = static_cast<float> (juce::jmax (1.0, mixState->compRatio));

    float attackCoeff  = std::exp (-1.0f / (static_cast<float> (mixState->compAttack) * 0.001f * static_cast<float> (sampleRate)));
    float releaseCoeff = std::exp (-1.0f / (static_cast<float> (mixState->compRelease) * 0.001f * static_cast<float> (sampleRate)));

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
    if (mixState == nullptr) return;

    // Volume: dB to linear
    float gain;
    if (mixState->volume <= -99.0)
        gain = 0.0f;
    else
        gain = juce::Decibels::decibelsToGain (static_cast<float> (mixState->volume));

    // Pan: -50 to +50 → 0.0 to 1.0
    float panNorm = (static_cast<float> (mixState->pan) + 50.0f) / 100.0f;
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
    if (mixState == nullptr || sendBuffers == nullptr) return;

    if (mixState->reverbSend > -99.0)
    {
        float reverbGain = juce::Decibels::decibelsToGain (static_cast<float> (mixState->reverbSend));
        sendBuffers->addToReverb (buffer, startSample, numSamples, reverbGain);
    }

    if (mixState->delaySend > -99.0)
    {
        float delayGain = juce::Decibels::decibelsToGain (static_cast<float> (mixState->delaySend));
        sendBuffers->addToDelay (buffer, startSample, numSamples, delayGain);
    }
}

//==============================================================================
// Main processing
//==============================================================================

void MixerPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || mixState == nullptr) return;

    auto& buffer = *fc.destBuffer;
    int startSample = fc.bufferStartSample;
    int numSamples = fc.bufferNumSamples;

    // DSP chain: EQ → Compressor → Volume/Pan → Sends
    processEQ (buffer, startSample, numSamples);
    processCompressor (buffer, startSample, numSamples);
    processSends (buffer, startSample, numSamples);  // Pre-fader sends
    processVolumeAndPan (buffer, startSample, numSamples);
}
