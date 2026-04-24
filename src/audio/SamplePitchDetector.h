#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace SamplePitchDetector
{

struct DetectionResult
{
    int midiNote = -1;
    double frequencyHz = 0.0;
    double confidence = 0.0;
    int tuneSemitones = 0;
    int finetuneCents = 0;
    juce::String noteName;
};

namespace detail
{

struct WindowPitch
{
    double midiNote = 0.0;
    double frequencyHz = 0.0;
    double clarity = 0.0;
};

inline double frequencyToMidiNote (double frequencyHz)
{
    return 69.0 + 12.0 * std::log2 (frequencyHz / 440.0);
}

inline juce::String midiNoteName (int midiNote)
{
    static constexpr const char* names[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    const int noteIndex = ((midiNote % 12) + 12) % 12;
    const int octave = midiNote / 12 - 1;
    return juce::String (names[noteIndex]) + juce::String (octave);
}

inline double median (std::vector<double> values)
{
    if (values.empty())
        return 0.0;

    const auto mid = values.begin() + static_cast<std::ptrdiff_t> (values.size() / 2);
    std::nth_element (values.begin(), mid, values.end());
    auto result = *mid;

    if ((values.size() & 1u) == 0)
    {
        const auto lower = std::max_element (values.begin(), mid);
        result = (*lower + result) * 0.5;
    }

    return result;
}

inline std::optional<WindowPitch> estimateWindowPitch (const std::vector<double>& mono,
                                                       int startSample,
                                                       int windowSize,
                                                       double sampleRate)
{
    static constexpr double kMinFrequencyHz = 40.0;
    static constexpr double kMaxFrequencyHz = 2000.0;
    static constexpr double kYinThreshold = 0.10;
    static constexpr double kMinClarity = 0.90;

    const int minLag = juce::jmax (2, static_cast<int> (std::floor (sampleRate / kMaxFrequencyHz)));
    const int maxLag = juce::jmin (windowSize / 2 - 2,
                                   static_cast<int> (std::ceil (sampleRate / kMinFrequencyHz)));

    if (minLag >= maxLag || startSample < 0 || startSample + windowSize > static_cast<int> (mono.size()))
        return std::nullopt;

    double mean = 0.0;
    for (int i = 0; i < windowSize; ++i)
        mean += mono[static_cast<size_t> (startSample + i)];
    mean /= static_cast<double> (windowSize);

    double energy = 0.0;
    for (int i = 0; i < windowSize; ++i)
    {
        const double x = mono[static_cast<size_t> (startSample + i)] - mean;
        energy += x * x;
    }

    if (std::sqrt (energy / static_cast<double> (windowSize)) < 1.0e-5)
        return std::nullopt;

    std::vector<double> difference (static_cast<size_t> (maxLag + 1), 0.0);
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        double sum = 0.0;
        const int count = windowSize - tau;
        for (int i = 0; i < count; ++i)
        {
            const double a = mono[static_cast<size_t> (startSample + i)] - mean;
            const double b = mono[static_cast<size_t> (startSample + i + tau)] - mean;
            const double d = a - b;
            sum += d * d;
        }
        difference[static_cast<size_t> (tau)] = sum;
    }

    std::vector<double> cmnd (static_cast<size_t> (maxLag + 1), 1.0);
    double runningSum = 0.0;
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        runningSum += difference[static_cast<size_t> (tau)];
        if (runningSum > 0.0)
            cmnd[static_cast<size_t> (tau)] = difference[static_cast<size_t> (tau)]
                                            * static_cast<double> (tau) / runningSum;
    }

    int tauEstimate = -1;
    for (int tau = minLag; tau <= maxLag; ++tau)
    {
        if (cmnd[static_cast<size_t> (tau)] < kYinThreshold)
        {
            while (tau + 1 <= maxLag
                   && cmnd[static_cast<size_t> (tau + 1)] < cmnd[static_cast<size_t> (tau)])
            {
                ++tau;
            }
            tauEstimate = tau;
            break;
        }
    }

    if (tauEstimate < 0)
    {
        auto begin = cmnd.begin() + minLag;
        auto end = cmnd.begin() + maxLag + 1;
        auto best = std::min_element (begin, end);
        if (best == end || *best >= kYinThreshold)
            return std::nullopt;

        tauEstimate = static_cast<int> (std::distance (cmnd.begin(), best));
    }

    const double clarity = 1.0 - cmnd[static_cast<size_t> (tauEstimate)];
    if (clarity < kMinClarity)
        return std::nullopt;

    double refinedTau = static_cast<double> (tauEstimate);
    if (tauEstimate > minLag && tauEstimate < maxLag)
    {
        const double left = cmnd[static_cast<size_t> (tauEstimate - 1)];
        const double centre = cmnd[static_cast<size_t> (tauEstimate)];
        const double right = cmnd[static_cast<size_t> (tauEstimate + 1)];
        const double denom = left - 2.0 * centre + right;
        if (std::abs (denom) > 1.0e-9)
        {
            const double offset = juce::jlimit (-1.0, 1.0, 0.5 * (left - right) / denom);
            refinedTau += offset;
        }
    }

    if (refinedTau <= 0.0)
        return std::nullopt;

    const double frequencyHz = sampleRate / refinedTau;
    return WindowPitch { frequencyToMidiNote (frequencyHz), frequencyHz, clarity };
}

} // namespace detail

inline std::optional<DetectionResult> detectPitch (const juce::AudioBuffer<float>& buffer,
                                                   double sampleRate)
{
    static constexpr double kMinSustainSeconds = 1.0;
    static constexpr double kMaxAnalysisSeconds = 8.0;
    static constexpr int kMaxPitchWindows = 12;

    const int channels = buffer.getNumChannels();
    const int totalSamples = buffer.getNumSamples();
    if (channels <= 0 || totalSamples < static_cast<int> (sampleRate * kMinSustainSeconds))
        return std::nullopt;

    const int analysisSamples = juce::jmin (
        totalSamples, static_cast<int> (std::round (sampleRate * kMaxAnalysisSeconds)));
    if (analysisSamples <= 0)
        return std::nullopt;

    std::vector<double> mono (static_cast<size_t> (analysisSamples), 0.0);
    for (int i = 0; i < analysisSamples; ++i)
    {
        double sample = 0.0;
        for (int ch = 0; ch < channels; ++ch)
            sample += static_cast<double> (buffer.getSample (ch, i));
        mono[static_cast<size_t> (i)] = sample / static_cast<double> (channels);
    }

    const int energyWindow = juce::jmax (128, static_cast<int> (std::round (sampleRate * 0.05)));
    const int energyHop = juce::jmax (64, energyWindow / 2);
    if (analysisSamples < energyWindow)
        return std::nullopt;

    const int numEnergyFrames = 1 + (analysisSamples - energyWindow) / energyHop;
    std::vector<double> rms (static_cast<size_t> (numEnergyFrames), 0.0);
    double maxRms = 0.0;

    for (int frame = 0; frame < numEnergyFrames; ++frame)
    {
        const int start = frame * energyHop;
        double sum = 0.0;
        for (int i = 0; i < energyWindow; ++i)
        {
            const double x = mono[static_cast<size_t> (start + i)];
            sum += x * x;
        }

        const double frameRms = std::sqrt (sum / static_cast<double> (energyWindow));
        rms[static_cast<size_t> (frame)] = frameRms;
        maxRms = juce::jmax (maxRms, frameRms);
    }

    if (maxRms < 1.0e-5)
        return std::nullopt;

    const double activeThreshold = maxRms * 0.18;
    int bestStartFrame = -1;
    int bestEndFrame = -1;
    int currentStartFrame = -1;

    for (int frame = 0; frame < numEnergyFrames; ++frame)
    {
        const bool active = rms[static_cast<size_t> (frame)] >= activeThreshold;
        if (active && currentStartFrame < 0)
            currentStartFrame = frame;

        const bool endsHere = ! active || frame == numEnergyFrames - 1;
        if (currentStartFrame >= 0 && endsHere)
        {
            const int currentEndFrame = active && frame == numEnergyFrames - 1 ? frame + 1 : frame;
            if (bestStartFrame < 0 || currentEndFrame - currentStartFrame > bestEndFrame - bestStartFrame)
            {
                bestStartFrame = currentStartFrame;
                bestEndFrame = currentEndFrame;
            }
            currentStartFrame = -1;
        }
    }

    if (bestStartFrame < 0 || bestEndFrame <= bestStartFrame)
        return std::nullopt;

    const int sustainedStart = bestStartFrame * energyHop;
    const int sustainedEnd = juce::jmin (analysisSamples, bestEndFrame * energyHop + energyWindow);
    const double sustainedSeconds = static_cast<double> (sustainedEnd - sustainedStart) / sampleRate;
    if (sustainedSeconds < kMinSustainSeconds)
        return std::nullopt;

    const int attackSkip = static_cast<int> (std::round (sampleRate * 0.10));
    const int pitchStart = juce::jmin (sustainedEnd, sustainedStart + attackSkip);
    const int pitchSamples = sustainedEnd - pitchStart;
    const int windowSize = juce::jlimit (2048, 8192, static_cast<int> (std::round (sampleRate * 0.10)));

    if (pitchSamples < windowSize)
        return std::nullopt;

    const int lastWindowStart = sustainedEnd - windowSize - 1;
    const int windowRange = juce::jmax (0, lastWindowStart - pitchStart);
    const int desiredWindows = juce::jlimit (
        1, kMaxPitchWindows, 1 + windowRange / juce::jmax (1, windowSize / 2));

    std::vector<detail::WindowPitch> estimates;
    estimates.reserve (static_cast<size_t> (desiredWindows));

    for (int w = 0; w < desiredWindows; ++w)
    {
        const int start = desiredWindows <= 1
                            ? pitchStart
                            : pitchStart + juce::roundToInt (
                                static_cast<double> (windowRange) * static_cast<double> (w)
                                / static_cast<double> (desiredWindows - 1));

        if (auto estimate = detail::estimateWindowPitch (mono, start, windowSize, sampleRate))
            estimates.push_back (*estimate);
    }

    const int minRequiredEstimates = juce::jmin (4, desiredWindows);
    if (static_cast<int> (estimates.size()) < minRequiredEstimates)
        return std::nullopt;

    std::vector<double> midiNotes;
    midiNotes.reserve (estimates.size());
    for (const auto& estimate : estimates)
        midiNotes.push_back (estimate.midiNote);

    const double medianMidi = detail::median (midiNotes);
    std::vector<detail::WindowPitch> inliers;
    inliers.reserve (estimates.size());
    for (const auto& estimate : estimates)
    {
        if (std::abs (estimate.midiNote - medianMidi) <= 0.25)
            inliers.push_back (estimate);
    }

    if (static_cast<int> (inliers.size()) < minRequiredEstimates)
        return std::nullopt;

    double meanMidi = 0.0;
    double meanFrequency = 0.0;
    double meanClarity = 0.0;
    for (const auto& estimate : inliers)
    {
        meanMidi += estimate.midiNote;
        meanFrequency += estimate.frequencyHz;
        meanClarity += estimate.clarity;
    }

    meanMidi /= static_cast<double> (inliers.size());
    meanFrequency /= static_cast<double> (inliers.size());
    meanClarity /= static_cast<double> (inliers.size());

    double variance = 0.0;
    for (const auto& estimate : inliers)
    {
        const double delta = estimate.midiNote - meanMidi;
        variance += delta * delta;
    }
    const double stdDev = std::sqrt (variance / static_cast<double> (inliers.size()));

    if (meanClarity < 0.93 || stdDev > 0.12)
        return std::nullopt;

    const double coverage = static_cast<double> (inliers.size()) / static_cast<double> (desiredWindows);
    const double stability = 1.0 - juce::jlimit (0.0, 1.0, stdDev / 0.25);
    const double confidence = meanClarity * coverage * stability;
    if (confidence < 0.85)
        return std::nullopt;

    const double correctionCents = (60.0 - meanMidi) * 100.0;
    int tuneSemitones = juce::jlimit (-24, 24,
                                      static_cast<int> (std::round (correctionCents / 100.0)));
    int finetuneCents = static_cast<int> (std::round (
        correctionCents - static_cast<double> (tuneSemitones) * 100.0));

    if (finetuneCents > 100 && tuneSemitones < 24)
    {
        ++tuneSemitones;
        finetuneCents -= 100;
    }
    else if (finetuneCents < -100 && tuneSemitones > -24)
    {
        --tuneSemitones;
        finetuneCents += 100;
    }

    if (finetuneCents < -100 || finetuneCents > 100)
        return std::nullopt;

    const int midiNote = juce::jlimit (0, 127, static_cast<int> (std::round (meanMidi)));
    return DetectionResult {
        midiNote,
        meanFrequency,
        confidence,
        tuneSemitones,
        finetuneCents,
        detail::midiNoteName (midiNote)
    };
}

} // namespace SamplePitchDetector
