#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <memory>

// Real-time-safe shared accumulation buffers for delay and reverb sends.
// prepare() must be called from setup code before audio starts. Audio callbacks
// only perform bounded atomic add/exchange operations; they never lock or grow.
struct SendBuffers
{
    void prepare (int numSamples, int numChannels)
    {
        const int newSamples = juce::jmax (0, numSamples);
        const int newChannels = juce::jmax (0, numChannels);
        const size_t total = static_cast<size_t> (newSamples) * static_cast<size_t> (newChannels);

        if (newSamples != capacitySamples || newChannels != capacityChannels)
        {
            delayData = total > 0 ? std::make_unique<std::atomic<float>[]> (total) : nullptr;
            reverbData = total > 0 ? std::make_unique<std::atomic<float>[]> (total) : nullptr;
            capacitySamples = newSamples;
            capacityChannels = newChannels;
        }

        clear();
    }

    int getCapacitySamples() const noexcept { return capacitySamples; }
    int getCapacityChannels() const noexcept { return capacityChannels; }

    void addToDelay (const juce::AudioBuffer<float>& source, int startSample,
                     int numSamples, float gain)
    {
        addToBuffer (delayData.get(), source, startSample, numSamples, gain);
    }

    void addToReverb (const juce::AudioBuffer<float>& source, int startSample,
                      int numSamples, float gain)
    {
        addToBuffer (reverbData.get(), source, startSample, numSamples, gain);
    }

    void consumeSliceIntoPrepared (juce::AudioBuffer<float>& delayOut,
                                   juce::AudioBuffer<float>& reverbOut,
                                   int startSample,
                                   int numSamples,
                                   int numChannels)
    {
        const int outChannels = juce::jmin (numChannels,
                                            juce::jmin (delayOut.getNumChannels(),
                                                        reverbOut.getNumChannels()));
        const int outSamples = juce::jmin (numSamples,
                                           juce::jmin (delayOut.getNumSamples(),
                                                       reverbOut.getNumSamples()));

        if (outChannels <= 0 || outSamples <= 0)
            return;

        for (int ch = 0; ch < outChannels; ++ch)
        {
            delayOut.clear (ch, 0, outSamples);
            reverbOut.clear (ch, 0, outSamples);
        }

        if (! isPrepared() || startSample < 0)
            return;

        if (startSample >= capacitySamples)
            return;

        const int copySamples = juce::jmin (outSamples, capacitySamples - startSample);
        const int channels = juce::jmin (outChannels, capacityChannels);

        for (int ch = 0; ch < channels; ++ch)
        {
            auto* delayDst = delayOut.getWritePointer (ch);
            auto* reverbDst = reverbOut.getWritePointer (ch);
            const int baseIndex = ch * capacitySamples + startSample;

            for (int i = 0; i < copySamples; ++i)
            {
                delayDst[i] = delayData[static_cast<size_t> (baseIndex + i)].exchange (0.0f, std::memory_order_acq_rel);
                reverbDst[i] = reverbData[static_cast<size_t> (baseIndex + i)].exchange (0.0f, std::memory_order_acq_rel);
            }
        }
    }

    void consumeSlice (juce::AudioBuffer<float>& delayOut,
                       juce::AudioBuffer<float>& reverbOut,
                       int startSample,
                       int numSamples,
                       int numChannels)
    {
        delayOut.setSize (juce::jmax (0, numChannels), juce::jmax (0, numSamples), false, true, true);
        reverbOut.setSize (juce::jmax (0, numChannels), juce::jmax (0, numSamples), false, true, true);
        consumeSliceIntoPrepared (delayOut, reverbOut, startSample, numSamples, numChannels);
    }

    void clear()
    {
        if (! isPrepared())
            return;

        const size_t total = static_cast<size_t> (capacitySamples) * static_cast<size_t> (capacityChannels);
        for (size_t i = 0; i < total; ++i)
        {
            delayData[i].store (0.0f, std::memory_order_relaxed);
            reverbData[i].store (0.0f, std::memory_order_relaxed);
        }
    }

private:
    std::unique_ptr<std::atomic<float>[]> delayData;
    std::unique_ptr<std::atomic<float>[]> reverbData;
    int capacitySamples = 0;
    int capacityChannels = 0;

    bool isPrepared() const noexcept
    {
        return delayData != nullptr && reverbData != nullptr
            && capacitySamples > 0 && capacityChannels > 0;
    }

    static void atomicAdd (std::atomic<float>& target, float value)
    {
        if (value == 0.0f || ! std::isfinite (value))
            return;

        auto current = target.load (std::memory_order_relaxed);
        while (! target.compare_exchange_weak (current, current + value,
                                               std::memory_order_relaxed,
                                               std::memory_order_relaxed))
        {
        }
    }

    void addToBuffer (std::atomic<float>* target,
                      const juce::AudioBuffer<float>& source,
                      int startSample,
                      int numSamples,
                      float gain)
    {
        if (target == nullptr || gain <= 0.0f || ! std::isfinite (gain) || startSample < 0 || numSamples <= 0)
            return;

        if (source.getNumChannels() <= 0 || startSample >= source.getNumSamples())
            return;

        if (startSample >= capacitySamples)
            return;

        const int samples = juce::jmin (numSamples,
                                        juce::jmin (source.getNumSamples() - startSample,
                                                    capacitySamples - startSample));
        const int channels = juce::jmin (source.getNumChannels(), capacityChannels);

        if (samples <= 0 || channels <= 0)
            return;

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto* src = source.getReadPointer (ch, startSample);
            const int baseIndex = ch * capacitySamples + startSample;

            for (int i = 0; i < samples; ++i)
                atomicAdd (target[static_cast<size_t> (baseIndex + i)], src[i] * gain);
        }
    }
};
