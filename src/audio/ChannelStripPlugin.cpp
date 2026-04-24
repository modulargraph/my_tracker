#include "ChannelStripPlugin.h"
#include "DspUtils.h"

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
    DspUtils::process3BandEQ (buffer, startSample, numSamples, sampleRate,
                              localMixState.eqLowGain, localMixState.eqMidGain,
                              localMixState.eqHighGain, localMixState.eqMidFreq,
                              eqLowL, eqLowR, eqMidL, eqMidR, eqHighL, eqHighR);
}

//==============================================================================
// Compressor (simple feed-forward)
//==============================================================================

void ChannelStripPlugin::processCompressor (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    DspUtils::processCompressor (buffer, startSample, numSamples, sampleRate, compEnvelope,
                                 localMixState.compThreshold, localMixState.compRatio,
                                 localMixState.compAttack, localMixState.compRelease);
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
