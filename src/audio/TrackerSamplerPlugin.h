#pragma once

#include <map>
#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include "InstrumentParams.h"

namespace te = tracktion;

class SimpleSampler;

// Holds the entire sample data in memory for lock-free audio thread access
struct SampleBank
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 44100.0;
    int numChannels = 1;
    juce::int64 totalSamples = 0;
    juce::File sourceFile;
};

class TrackerSamplerPlugin : public te::Plugin
{
public:
    TrackerSamplerPlugin (te::PluginCreationInfo);
    ~TrackerSamplerPlugin() override;

    static const char* getPluginName()  { return "TrackerSampler"; }
    static const char* xmlTypeName;

    juce::String getName() const override               { return getPluginName(); }
    juce::String getPluginType() override               { return xmlTypeName; }
    bool takesMidiInput() override                      { return true; }
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
    void setSampleBank (std::shared_ptr<const SampleBank> bank);
    void setSamplerSource (SimpleSampler* s) { samplerSource = s; }
    void setInstrumentIndex (int index) { instrumentIndex = index; }

    // Pre-load multiple banks for multi-instrument per track
    void preloadBanks (const std::map<int, std::shared_ptr<const SampleBank>>& banks)
    {
        preloadedBanks = banks;
    }

    // Preview support (called from message thread, consumed on audio thread)
    void playNote (int note, float velocity);
    void stopAllNotes();

private:
    // Monophonic voice for tracker-style playback
    struct Voice
    {
        enum class State { Idle, Playing, FadingOut };
        State state = State::Idle;

        double playbackPos = 0.0;
        int midiNote = 60;
        float velocity = 1.0f;

        int fadeOutRemaining = 0;
        static constexpr int kFadeOutSamples = 64;

        bool playingForward = true;
        bool inLoopPhase = false;

        // Slice mode boundaries (in samples)
        double sliceStart = 0.0;
        double sliceEnd = 0.0;

        // Granular mode state
        double grainStart = 0.0;
        double grainEnd = 0.0;
        int grainPos = 0;
        int grainLength = 0;

        void reset()
        {
            state = State::Idle;
            playbackPos = 0.0;
            midiNote = 60;
            velocity = 1.0f;
            fadeOutRemaining = 0;
            playingForward = true;
            inLoopPhase = false;
            sliceStart = sliceEnd = 0.0;
            grainStart = grainEnd = 0.0;
            grainPos = grainLength = 0;
        }
    };

    Voice voice;
    Voice fadeOutVoice;

    // Thread-safe sample bank access
    juce::SpinLock bankLock;
    std::shared_ptr<const SampleBank> sharedBank;

    // Pre-loaded banks for multi-instrument per track (instrument index → bank)
    std::map<int, std::shared_ptr<const SampleBank>> preloadedBanks;

    // Params access (same pattern as InstrumentEffectsPlugin)
    SimpleSampler* samplerSource = nullptr;
    int instrumentIndex = -1;

    // Preview atomics (message thread writes, audio thread reads)
    std::atomic<int> previewNote { -1 };
    std::atomic<float> previewVelocity { 0.0f };
    std::atomic<bool> previewStop { false };

    // Audio thread state
    double outputSampleRate = 44100.0;
    juce::AudioBuffer<float> scratchBuffer;

    // Rendering
    void triggerNote (Voice& v, int note, float vel,
                      const SampleBank& bank, const InstrumentParams& params);
    void renderVoice (Voice& v, juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                      const SampleBank& bank, const InstrumentParams& params);

    void renderOneShot (Voice& v, juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                        const SampleBank& bank, const InstrumentParams& params);
    void renderForwardLoop (Voice& v, juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                            const SampleBank& bank, const InstrumentParams& params);
    void renderBackwardLoop (Voice& v, juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                             const SampleBank& bank, const InstrumentParams& params);
    void renderPingpongLoop (Voice& v, juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                             const SampleBank& bank, const InstrumentParams& params);
    void renderSlice (Voice& v, juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                      const SampleBank& bank, const InstrumentParams& params);
    void renderGranular (Voice& v, juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                         const SampleBank& bank, const InstrumentParams& params);

    double getPitchRatio (int midiNote, const SampleBank& bank, const InstrumentParams& params) const;
    float interpolateSample (const SampleBank& bank, int channel, double pos) const;
    float getGranularEnvelope (const InstrumentParams& params, int pos, int length) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackerSamplerPlugin)
};
