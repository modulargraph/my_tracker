#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include "MixerState.h"
#include "SendBuffers.h"

namespace te = tracktion;

/**
 * TrackOutputPlugin handles Sends, Pan, Volume, and Peak metering for a single track.
 * This is the second half of the old MixerPlugin chain, split out so that
 * external insert plugins can be placed between the channel strip (EQ+Comp)
 * and the track output (Sends+Pan+Volume+Meter).
 *
 * Signal chain position:
 *   Sampler -> InstrumentEffects -> ChannelStrip -> [Insert Plugins] -> TrackOutput
 */
class TrackOutputPlugin : public te::Plugin
{
public:
    TrackOutputPlugin (te::PluginCreationInfo);
    ~TrackOutputPlugin() override;

    static const char* getPluginName()  { return "TrackOutput"; }
    static const char* xmlTypeName;

    juce::String getName() const override               { return getPluginName(); }
    juce::String getPluginType() override               { return xmlTypeName; }
    bool takesMidiInput() override                      { return false; }
    bool takesAudioInput() override                     { return true; }
    bool isSynth() override                             { return false; }
    bool producesAudioWhenNoAudioInput() override       { return false; }
    int getNumOutputChannelsGivenInputs (int numInputChannels) override { return juce::jmin (numInputChannels, 2); }

    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override;
    void applyToBuffer (const te::PluginRenderContext&) override;

    juce::String getSelectableDescription() override    { return getName(); }
    bool needsConstantBufferSize() override             { return false; }

    void setMixState (const TrackMixState& s);
    void setSendBuffers (SendBuffers* b) { sendBuffers = b; }

    // Peak level metering (audio thread writes, UI thread reads)
    float getPeakLevel() const { return peakLevel.load (std::memory_order_relaxed); }
    void resetPeak() { peakLevel.store (0.0f, std::memory_order_relaxed); }

private:
    juce::SpinLock mixStateLock;
    TrackMixState sharedMixState;
    TrackMixState localMixState;
    SendBuffers* sendBuffers = nullptr;

    // Smoothed gain
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedGainL { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedGainR { 1.0f };

    // Peak level (written on audio thread, read on UI thread)
    std::atomic<float> peakLevel { 0.0f };

    void processVolumeAndPan (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    void processSends (const juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackOutputPlugin)
};
