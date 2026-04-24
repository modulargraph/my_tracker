#pragma once

#include <array>
#include <JuceHeader.h>
#include "MixerState.h"

// Thread-safe accumulation buffers for grouped track routing. Track outputs add
// post-fader audio here, then the master processor consumes each group bus slice.
struct GroupRoutingBuffers
{
    std::array<juce::AudioBuffer<float>, kMaxGroupBuses> groupBuffers;
    juce::SpinLock lock;

    void prepare (int numSamples, int numChannels)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (auto& buffer : groupBuffers)
        {
            buffer.setSize (numChannels, numSamples, false, true, false);
            buffer.clear();
        }
    }

    void addToGroup (int groupIndex,
                     const juce::AudioBuffer<float>& source,
                     int startSample,
                     int numSamples)
    {
        if (groupIndex < 0 || groupIndex >= kMaxGroupBuses)
            return;
        if (source.getNumChannels() <= 0 || startSample < 0 || numSamples <= 0)
            return;

        const juce::SpinLock::ScopedLockType sl (lock);

        auto& groupBuffer = groupBuffers[static_cast<size_t> (groupIndex)];
        const int requiredSamples = startSample + numSamples;
        const int requiredChannels = juce::jmax (groupBuffer.getNumChannels(), source.getNumChannels());
        if (groupBuffer.getNumSamples() < requiredSamples
            || groupBuffer.getNumChannels() < requiredChannels)
        {
            groupBuffer.setSize (requiredChannels, requiredSamples, true, true, false);
        }

        const int channels = juce::jmin (source.getNumChannels(), groupBuffer.getNumChannels());
        const int srcAvail = juce::jmax (0, source.getNumSamples() - startSample);
        const int dstAvail = juce::jmax (0, groupBuffer.getNumSamples() - startSample);
        const int samples = juce::jmin (numSamples, juce::jmin (srcAvail, dstAvail));
        if (samples <= 0)
            return;

        for (int ch = 0; ch < channels; ++ch)
            groupBuffer.addFrom (ch, startSample, source, ch, startSample, samples);
    }

    void consumeGroupSlice (int groupIndex,
                            juce::AudioBuffer<float>& groupOut,
                            int startSample,
                            int numSamples,
                            int numChannels)
    {
        groupOut.setSize (numChannels, numSamples, false, true, true);
        groupOut.clear();

        if (groupIndex < 0 || groupIndex >= kMaxGroupBuses || numSamples <= 0)
            return;

        const juce::SpinLock::ScopedLockType sl (lock);

        auto& groupBuffer = groupBuffers[static_cast<size_t> (groupIndex)];
        const int requiredSamples = juce::jmax (0, startSample) + numSamples;
        if (groupBuffer.getNumSamples() < requiredSamples
            || groupBuffer.getNumChannels() < numChannels)
        {
            groupBuffer.setSize (numChannels, requiredSamples, true, true, false);
        }

        const int srcStart = juce::jmax (0, startSample);
        const int maxSamples = juce::jmax (0, groupBuffer.getNumSamples() - srcStart);
        const int copySamples = juce::jmin (numSamples, maxSamples);
        const int channels = juce::jmin (numChannels, groupBuffer.getNumChannels());

        for (int ch = 0; ch < channels; ++ch)
        {
            groupOut.copyFrom (ch, 0, groupBuffer, ch, srcStart, copySamples);
            groupBuffer.clear (ch, srcStart, copySamples);
        }
    }

    void clear()
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (auto& buffer : groupBuffers)
            buffer.clear();
    }
};
