#include "ChannelStripPlugin.h"

const char* ChannelStripPlugin::xmlTypeName = "ChannelStrip";

ChannelStripPlugin::ChannelStripPlugin (te::PluginCreationInfo info)
    : te::Plugin (info)
{
}

ChannelStripPlugin::~ChannelStripPlugin()
{
}

void ChannelStripPlugin::setMixState (const TrackMixState& s)
{
    const juce::SpinLock::ScopedLockType lock (mixStateLock);
    sharedMixState = s;
}

void ChannelStripPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate;

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

void ChannelStripPlugin::deinitialise()
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

void ChannelStripPlugin::processEQ (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    bool hasEQ = localMixState.eqLowGain != 0.0 || localMixState.eqMidGain != 0.0 || localMixState.eqHighGain != 0.0;
    if (! hasEQ) return;

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

void ChannelStripPlugin::processCompressor (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    if (localMixState.compThreshold >= 0.0 && localMixState.compRatio <= 1.0) return;

    float thresholdLinear = juce::Decibels::decibelsToGain (static_cast<float> (localMixState.compThreshold));
    float ratio = static_cast<float> (juce::jmax (1.0, localMixState.compRatio));

    float attackCoeff  = std::exp (-1.0f / (static_cast<float> (localMixState.compAttack) * 0.001f * static_cast<float> (sampleRate)));
    float releaseCoeff = std::exp (-1.0f / (static_cast<float> (localMixState.compRelease) * 0.001f * static_cast<float> (sampleRate)));

    int numChannels = buffer.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            peak = juce::jmax (peak, std::abs (buffer.getSample (ch, startSample + i)));

        if (peak > compEnvelope)
            compEnvelope = attackCoeff * compEnvelope + (1.0f - attackCoeff) * peak;
        else
            compEnvelope = releaseCoeff * compEnvelope + (1.0f - releaseCoeff) * peak;

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
// Main processing
//==============================================================================

void ChannelStripPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr) return;

    {
        const juce::SpinLock::ScopedLockType lock (mixStateLock);
        localMixState = sharedMixState;
    }

    auto& buffer = *fc.destBuffer;
    int startSample = fc.bufferStartSample;
    int numSamples = fc.bufferNumSamples;

    // DSP chain: EQ -> Compressor
    processEQ (buffer, startSample, numSamples);
    processCompressor (buffer, startSample, numSamples);
}
