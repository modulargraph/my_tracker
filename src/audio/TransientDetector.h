#pragma once

#include <JuceHeader.h>
#include <vector>

/**
 * Transient detection via energy-envelope spectral flux.
 *
 * Computes a short-window RMS energy curve, takes the positive first
 * derivative (spectral flux), applies an adaptive threshold, and returns
 * the sample positions that exceed it as normalised 0-1 values.
 *
 * Header-only, following the same pattern as DspUtils.h.
 */
namespace TransientDetector
{

/**
 * Detect transient onset positions in a mono audio buffer.
 *
 * @param buffer        Mono audio data (only channel 0 is read).
 * @param sampleRate    Sample rate of the audio data.
 * @param sensitivity   0.0 = least sensitive (few onsets),
 *                      1.0 = most sensitive (many onsets).
 * @param rangeStart    Only return onsets after this normalised position (0-1).
 * @param rangeEnd      Only return onsets before this normalised position (0-1).
 * @return              Sorted vector of normalised sample positions (0.0 - 1.0).
 */
inline std::vector<double> detectTransients (const juce::AudioBuffer<float>& buffer,
                                             double sampleRate,
                                             double sensitivity = 0.5,
                                             double rangeStart  = 0.0,
                                             double rangeEnd    = 1.0)
{
    std::vector<double> result;

    auto numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return result;

    auto* data = buffer.getReadPointer (0);

    // --- Energy envelope with a ~5 ms window ---
    int windowSize = juce::jmax (64, static_cast<int> (sampleRate * 0.005));
    int hopSize    = windowSize / 2;
    int numFrames  = (numSamples - windowSize) / hopSize;

    if (numFrames <= 0)
        return result;

    std::vector<double> energy (static_cast<size_t> (numFrames), 0.0);
    double maxEnergy = 0.0;

    for (int f = 0; f < numFrames; ++f)
    {
        int offset = f * hopSize;
        double e = 0.0;

        for (int i = 0; i < windowSize; ++i)
        {
            double s = static_cast<double> (data[offset + i]);
            e += s * s;
        }

        e /= static_cast<double> (windowSize);
        energy[static_cast<size_t> (f)] = e;

        if (e > maxEnergy)
            maxEnergy = e;
    }

    if (maxEnergy <= 0.0)
        return result;

    // --- Normalise energy ---
    for (auto& e : energy)
        e /= maxEnergy;

    // --- Spectral flux (positive first-derivative only) ---
    std::vector<double> flux (energy.size(), 0.0);

    for (size_t i = 1; i < energy.size(); ++i)
    {
        double diff = energy[i] - energy[i - 1];
        flux[i] = juce::jmax (0.0, diff);
    }

    // --- Adaptive threshold ---
    double meanFlux = 0.0;
    for (auto f : flux)
        meanFlux += f;
    meanFlux /= static_cast<double> (flux.size());

    // sensitivity 1.0 -> low threshold (many onsets), 0.0 -> high threshold
    double threshold = meanFlux * (1.0 + (1.0 - sensitivity) * 8.0);

    // Minimum distance between onsets (~50 ms, in frames)
    int minDist = juce::jmax (1, static_cast<int> (sampleRate * 0.05) / hopSize);

    // --- Peak-pick above threshold ---
    int lastOnsetFrame = -minDist;

    for (int f = 1; f < numFrames - 1; ++f)
    {
        auto fi = static_cast<size_t> (f);

        if (flux[fi] > threshold
            && flux[fi] > flux[fi - 1]
            && flux[fi] >= flux[fi + 1]
            && (f - lastOnsetFrame) >= minDist)
        {
            double normPos = static_cast<double> (f * hopSize)
                           / static_cast<double> (numSamples);

            if (normPos > rangeStart && normPos < rangeEnd)
            {
                result.push_back (normPos);
                lastOnsetFrame = f;
            }
        }
    }

    return result;
}

} // namespace TransientDetector
