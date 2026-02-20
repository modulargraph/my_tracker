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
        const juce::SpinLock::ScopedLockType sl (lock);
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
        if (source.getNumChannels() <= 0) return;

        const juce::SpinLock::ScopedLockType sl (lock);
        if (startSample < 0 || numSamples <= 0) return;

        int requiredSamples = startSample + numSamples;
        int requiredChannels = juce::jmax (delayBuffer.getNumChannels(), source.getNumChannels());
        if (delayBuffer.getNumSamples() < requiredSamples
            || delayBuffer.getNumChannels() < requiredChannels)
        {
            delayBuffer.setSize (requiredChannels, requiredSamples, true, true, false);
        }

        int channels = juce::jmin (source.getNumChannels(), delayBuffer.getNumChannels());
        int srcAvail = juce::jmax (0, source.getNumSamples() - startSample);
        int dstAvail = juce::jmax (0, delayBuffer.getNumSamples() - startSample);
        int samples = juce::jmin (numSamples, juce::jmin (srcAvail, dstAvail));
        if (samples <= 0) return;

        for (int ch = 0; ch < channels; ++ch)
            delayBuffer.addFrom (ch, startSample, source, ch, startSample, samples, gain);
    }

    // Add audio to the reverb send buffer (called from each track's effects plugin).
    void addToReverb (const juce::AudioBuffer<float>& source, int startSample,
                      int numSamples, float gain)
    {
        if (gain <= 0.0f) return;
        if (source.getNumChannels() <= 0) return;

        const juce::SpinLock::ScopedLockType sl (lock);
        if (startSample < 0 || numSamples <= 0) return;

        int requiredSamples = startSample + numSamples;
        int requiredChannels = juce::jmax (reverbBuffer.getNumChannels(), source.getNumChannels());
        if (reverbBuffer.getNumSamples() < requiredSamples
            || reverbBuffer.getNumChannels() < requiredChannels)
        {
            reverbBuffer.setSize (requiredChannels, requiredSamples, true, true, false);
        }

        int channels = juce::jmin (source.getNumChannels(), reverbBuffer.getNumChannels());
        int srcAvail = juce::jmax (0, source.getNumSamples() - startSample);
        int dstAvail = juce::jmax (0, reverbBuffer.getNumSamples() - startSample);
        int samples = juce::jmin (numSamples, juce::jmin (srcAvail, dstAvail));
        if (samples <= 0) return;

        for (int ch = 0; ch < channels; ++ch)
            reverbBuffer.addFrom (ch, startSample, source, ch, startSample, samples, gain);
    }

    // Copy a block slice for processing and clear that slice in the shared buffers.
    // This avoids concurrent read/write on the same memory and keeps sub-block timing aligned.
    void consumeSlice (juce::AudioBuffer<float>& delayOut,
                       juce::AudioBuffer<float>& reverbOut,
                       int startSample,
                       int numSamples,
                       int numChannels)
    {
        delayOut.setSize (numChannels, numSamples, false, true, true);
        reverbOut.setSize (numChannels, numSamples, false, true, true);
        delayOut.clear();
        reverbOut.clear();

        if (numSamples <= 0)
            return;

        const juce::SpinLock::ScopedLockType sl (lock);

        int requiredSamples = juce::jmax (0, startSample) + numSamples;
        if (delayBuffer.getNumSamples() < requiredSamples
            || reverbBuffer.getNumSamples() < requiredSamples
            || delayBuffer.getNumChannels() < numChannels
            || reverbBuffer.getNumChannels() < numChannels)
        {
            delayBuffer.setSize (numChannels, requiredSamples, true, true, false);
            reverbBuffer.setSize (numChannels, requiredSamples, true, true, false);
        }

        int srcStart = juce::jmax (0, startSample);
        int maxDelaySamples = juce::jmax (0, delayBuffer.getNumSamples() - srcStart);
        int maxReverbSamples = juce::jmax (0, reverbBuffer.getNumSamples() - srcStart);
        int copySamples = juce::jmin (numSamples, juce::jmin (maxDelaySamples, maxReverbSamples));
        int channels = juce::jmin (numChannels, juce::jmin (delayBuffer.getNumChannels(), reverbBuffer.getNumChannels()));

        for (int ch = 0; ch < channels; ++ch)
        {
            delayOut.copyFrom (ch, 0, delayBuffer, ch, srcStart, copySamples);
            reverbOut.copyFrom (ch, 0, reverbBuffer, ch, srcStart, copySamples);
            delayBuffer.clear (ch, srcStart, copySamples);
            reverbBuffer.clear (ch, srcStart, copySamples);
        }
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
