#pragma once

#include <JuceHeader.h>

/**
 * Shared DSP utilities used across audio plugins.
 * Consolidates duplicated EQ, compressor, and safety limiter code.
 */
namespace DspUtils
{

//==============================================================================
// 3-band EQ: low shelf 200Hz, parametric mid, high shelf 4kHz
//==============================================================================

inline void process3BandEQ (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                            double sampleRate,
                            double eqLowGain, double eqMidGain, double eqHighGain, double eqMidFreq,
                            juce::dsp::IIR::Filter<float>& eqLowL, juce::dsp::IIR::Filter<float>& eqLowR,
                            juce::dsp::IIR::Filter<float>& eqMidL, juce::dsp::IIR::Filter<float>& eqMidR,
                            juce::dsp::IIR::Filter<float>& eqHighL, juce::dsp::IIR::Filter<float>& eqHighR)
{
    bool hasEQ = eqLowGain != 0.0 || eqMidGain != 0.0 || eqHighGain != 0.0;
    if (! hasEQ) return;

    {
        float gain = (eqLowGain != 0.0)
                         ? juce::Decibels::decibelsToGain (static_cast<float> (eqLowGain))
                         : 1.0f;
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf (sampleRate, 200.0f, 0.707f, gain);
        eqLowL.coefficients = coeffs;
        eqLowR.coefficients = coeffs;
    }
    {
        float gain = (eqMidGain != 0.0)
                         ? juce::Decibels::decibelsToGain (static_cast<float> (eqMidGain))
                         : 1.0f;
        float freq = juce::jlimit (200.0f, 8000.0f, static_cast<float> (eqMidFreq));
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sampleRate, freq, 1.0f, gain);
        eqMidL.coefficients = coeffs;
        eqMidR.coefficients = coeffs;
    }
    {
        float gain = (eqHighGain != 0.0)
                         ? juce::Decibels::decibelsToGain (static_cast<float> (eqHighGain))
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
// Feed-forward compressor
//==============================================================================

inline void processCompressor (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                               double sampleRate, float& envelope,
                               double compThreshold, double compRatio,
                               double compAttack, double compRelease)
{
    if (compThreshold >= 0.0 && compRatio <= 1.0) return;

    float thresholdLinear = juce::Decibels::decibelsToGain (static_cast<float> (compThreshold));
    float ratio = static_cast<float> (juce::jmax (1.0, compRatio));

    float attackCoeff  = std::exp (-1.0f / (static_cast<float> (compAttack) * 0.001f * static_cast<float> (sampleRate)));
    float releaseCoeff = std::exp (-1.0f / (static_cast<float> (compRelease) * 0.001f * static_cast<float> (sampleRate)));

    int numChannels = buffer.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            peak = juce::jmax (peak, std::abs (buffer.getSample (ch, startSample + i)));

        if (peak > envelope)
            envelope = attackCoeff * envelope + (1.0f - attackCoeff) * peak;
        else
            envelope = releaseCoeff * envelope + (1.0f - releaseCoeff) * peak;

        float gain = 1.0f;
        if (envelope > thresholdLinear && thresholdLinear > 0.0f)
        {
            float overDB = juce::Decibels::gainToDecibels (envelope / thresholdLinear);
            float reductionDB = overDB * (1.0f - 1.0f / ratio);
            gain = juce::Decibels::decibelsToGain (-reductionDB);
        }

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer (ch)[startSample + i] *= gain;
    }
}

//==============================================================================
// Safety limiter: clamp and NaN/Inf protection
//==============================================================================

inline void applySafetyLimiter (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                                float limit = 4.0f)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer (ch, startSample);
        for (int i = 0; i < numSamples; ++i)
        {
            if (! std::isfinite (data[i]))
                data[i] = 0.0f;
            else
                data[i] = juce::jlimit (-limit, limit, data[i]);
        }
    }
}

//==============================================================================
// Filter parameter conversion (from InstrumentEffectsPlugin)
//==============================================================================

inline float cutoffPercentToHz (int percent)
{
    float p = juce::jlimit (0.0f, 100.0f, static_cast<float> (percent)) / 100.0f;
    return 20.0f * std::pow (1000.0f, p); // 20 * 1000^p -> 20Hz to 20kHz
}

inline float resonancePercentToQ (int percent)
{
    float p = juce::jlimit (0.0f, 100.0f, static_cast<float> (percent)) / 100.0f;
    return 0.5f + p * 4.5f; // 0.5 to 5.0 (capped for speaker safety)
}

//==============================================================================
// Initialize EQ filters to flat
//==============================================================================

inline void initFlatEQ (double sampleRate,
                        juce::dsp::IIR::Filter<float>& eqLowL, juce::dsp::IIR::Filter<float>& eqLowR,
                        juce::dsp::IIR::Filter<float>& eqMidL, juce::dsp::IIR::Filter<float>& eqMidR,
                        juce::dsp::IIR::Filter<float>& eqHighL, juce::dsp::IIR::Filter<float>& eqHighR)
{
    auto flatCoeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sampleRate, 1000.0f, 0.707f, 1.0f);
    eqLowL.coefficients = flatCoeffs;   eqLowR.coefficients = flatCoeffs;
    eqMidL.coefficients = flatCoeffs;    eqMidR.coefficients = flatCoeffs;
    eqHighL.coefficients = flatCoeffs;   eqHighR.coefficients = flatCoeffs;
    eqLowL.reset();  eqLowR.reset();
    eqMidL.reset();   eqMidR.reset();
    eqHighL.reset();  eqHighR.reset();
}

} // namespace DspUtils
