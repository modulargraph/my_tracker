#pragma once

#include <JuceHeader.h>

// Thread-safe shared accumulation buffers for delay and reverb sends.
// Each InstrumentEffectsPlugin adds its post-processed audio (scaled by send amount)
// into these buffers. The SendEffectsPlugin reads and processes them each block.

struct SendBuffers
{
    juce::AudioBuffer<float> delayBuffer;
    juce::AudioBuffer<float> reverbBuffer;
    juce::SpinLock lock;

    // Prepare buffers for the expected block size and channel count.
    void prepare (int numSamples, int numChannels)
    {
        delayBuffer.setSize (numChannels, numSamples, false, true, false);
        reverbBuffer.setSize (numChannels, numSamples, false, true, false);
        delayBuffer.clear();
        reverbBuffer.clear();
    }

    // Add audio to the delay send buffer (called from each track's effects plugin).
    void addToDelay (const juce::AudioBuffer<float>& source, int startSample,
                     int numSamples, float gain)
    {
        if (gain <= 0.0f) return;

        const juce::SpinLock::ScopedLockType sl (lock);
        int channels = juce::jmin (source.getNumChannels(), delayBuffer.getNumChannels());
        int samples = juce::jmin (numSamples, delayBuffer.getNumSamples());

        for (int ch = 0; ch < channels; ++ch)
            delayBuffer.addFrom (ch, 0, source, ch, startSample, samples, gain);
    }

    // Add audio to the reverb send buffer (called from each track's effects plugin).
    void addToReverb (const juce::AudioBuffer<float>& source, int startSample,
                      int numSamples, float gain)
    {
        if (gain <= 0.0f) return;

        const juce::SpinLock::ScopedLockType sl (lock);
        int channels = juce::jmin (source.getNumChannels(), reverbBuffer.getNumChannels());
        int samples = juce::jmin (numSamples, reverbBuffer.getNumSamples());

        for (int ch = 0; ch < channels; ++ch)
            reverbBuffer.addFrom (ch, 0, source, ch, startSample, samples, gain);
    }

    // Clear both buffers (called at the start of each block by SendEffectsPlugin
    // after it has read them).
    void clear()
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        delayBuffer.clear();
        reverbBuffer.clear();
    }
};
