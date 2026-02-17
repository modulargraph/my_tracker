#pragma once

#include <atomic>
#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

class MetronomePlugin : public te::Plugin
{
public:
    MetronomePlugin (te::PluginCreationInfo);
    ~MetronomePlugin() override;

    static const char* getPluginName()  { return "Metronome"; }
    static const char* xmlTypeName;

    juce::String getName() const override               { return getPluginName(); }
    juce::String getPluginType() override               { return xmlTypeName; }
    bool takesMidiInput() override                      { return false; }
    bool takesAudioInput() override                     { return false; }
    bool isSynth() override                             { return true; }
    bool producesAudioWhenNoAudioInput() override       { return true; }
    int getNumOutputChannelsGivenInputs (int) override  { return 2; }

    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override;
    void applyToBuffer (const te::PluginRenderContext&) override;

    juce::String getSelectableDescription() override    { return getName(); }
    bool needsConstantBufferSize() override             { return false; }

    // --- Message-thread API ---
    void setEnabled (bool enabled);
    bool isEnabled() const;
    void setVolume (float gainLinear); // 0.0 to 1.0
    float getVolume() const;
    void setAccentEnabled (bool accent);
    bool isAccentEnabled() const;

private:
    std::atomic<bool> metronomeEnabled { false };
    std::atomic<float> volume { 0.7f };
    std::atomic<bool> accentEnabled { true };

    double lastBeatPosition = -1.0;
    int clickSamplesRemaining = 0;
    float clickFrequency = 800.0f;
    float clickPhase = 0.0f;
    float clickGain = 0.0f;

    double outputSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetronomePlugin)
};
