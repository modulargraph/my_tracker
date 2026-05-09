#pragma once

#include <array>
#include <map>
#include <memory>
#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include "InstrumentParams.h"
#include "SamplePlaybackLayout.h"

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
    void setInstrumentIndex (int index)
    {
        instrumentIndex = juce::jlimit (0, 255, index);
        currentBankMsb = (instrumentIndex >> 7) & 0x7F;
        channelInstruments.fill (instrumentIndex);
        channelBankMsbs.fill (currentBankMsb);
    }
    void setPitchOffset (float semitones) { pitchOffset.store (semitones, std::memory_order_relaxed); }
    void setGranularPositionOffset (float regionOffset)
    {
        granularPositionOffset.store (juce::jlimit (-1.0f, 1.0f, regionOffset), std::memory_order_relaxed);
    }
    void setRowsPerBeat (int rpb) { rowsPerBeat = juce::jlimit (1, 16, rpb); }

    // Pre-load multiple banks for multi-instrument per track
    void preloadBanks (const std::map<int, std::shared_ptr<const SampleBank>>& banks)
    {
        const juce::SpinLock::ScopedLockType lock (bankLock);
        preloadedBanks = banks;
    }

    // Update a single bank in the preloaded set (e.g. after reloading a sample)
    void updateBank (int instrument, std::shared_ptr<const SampleBank> bank)
    {
        const juce::SpinLock::ScopedLockType lock (bankLock);
        if (bank != nullptr)
            preloadedBanks[instrument] = std::move (bank);
        else
            preloadedBanks.erase (instrument);
    }

    // Preview support (called from message thread, consumed on audio thread)
    void playNote (int note, float velocity);
    void playNotes (const std::vector<int>& notes, float velocity);
    void stopAllNotes();

    // Playback position for UI cursor (normalized 0-1, -1 = idle)
    float getPlaybackPosition() const { return playbackPosNorm.load (std::memory_order_relaxed); }

private:
    // Playback voice for tracker rows. MIDI channel is used internally to keep
    // note lanes independent for sample-track polyphony.
    struct Voice
    {
        enum class State { Idle, Playing, FadingOut };
        State state = State::Idle;

        std::shared_ptr<const SampleBank> bank;
        InstrumentParams params;

        double playbackPos = 0.0;
        int midiNote = 60;
        int midiChannel = 1;
        int instrumentIndex = -1;
        float velocity = 1.0f;

        int fadeOutRemaining = 0;
        static constexpr int kFadeOutSamples = 64;
        bool releaseTailActive = false;
        int releaseTailRemaining = 0;
        int releaseStopFadeRemaining = 0;

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
            bank.reset();
            params = {};
            playbackPos = 0.0;
            midiNote = 60;
            midiChannel = 1;
            instrumentIndex = -1;
            velocity = 1.0f;
            fadeOutRemaining = 0;
            releaseTailActive = false;
            releaseTailRemaining = 0;
            releaseStopFadeRemaining = 0;
            playingForward = true;
            inLoopPhase = false;
            sliceStart = sliceEnd = 0.0;
            grainStart = grainEnd = 0.0;
            grainPos = grainLength = 0;
        }
    };

    static constexpr int kMaxPlaybackVoices = 32;

    struct VoiceRetriggerSnapshot
    {
        bool valid = false;
        const SampleBank* bank = nullptr;
        int midiNote = -1;
        int midiChannel = 1;
        int instrumentIndex = -1;
        double playbackPos = 0.0;
        bool playingForward = true;
        bool inLoopPhase = false;
        double sliceStart = 0.0;
        double sliceEnd = 0.0;
        double grainStart = 0.0;
        double grainEnd = 0.0;
        int grainPos = 0;
        int grainLength = 0;
        float velocity = 1.0f;

        void capture (const Voice& voice)
        {
            valid = true;
            bank = voice.bank.get();
            midiNote = voice.midiNote;
            midiChannel = voice.midiChannel;
            instrumentIndex = voice.instrumentIndex;
            playbackPos = voice.playbackPos;
            playingForward = voice.playingForward;
            inLoopPhase = voice.inLoopPhase;
            sliceStart = voice.sliceStart;
            sliceEnd = voice.sliceEnd;
            grainStart = voice.grainStart;
            grainEnd = voice.grainEnd;
            grainPos = voice.grainPos;
            grainLength = voice.grainLength;
            velocity = voice.velocity;
        }

        bool matches (const Voice& voice) const
        {
            return valid
                && voice.state == Voice::State::Playing
                && voice.bank.get() == bank
                && voice.midiChannel == midiChannel
                && voice.instrumentIndex == instrumentIndex;
        }

        void restore (Voice& voice, int noteOffset, float velocityScale) const
        {
            if (! matches (voice))
                return;

            voice.midiNote = juce::jlimit (0, 127, midiNote + noteOffset);
            voice.velocity = juce::jlimit (0.0f, 1.0f, velocity * velocityScale);
            voice.playbackPos = playbackPos;
            voice.playingForward = playingForward;
            voice.inLoopPhase = inLoopPhase;
            voice.sliceStart = sliceStart;
            voice.sliceEnd = sliceEnd;
            voice.grainStart = grainStart;
            voice.grainEnd = grainEnd;
            voice.grainPos = grainPos;
            voice.grainLength = grainLength;
        }
    };

    struct RetriggerState
    {
        bool active = false;
        bool capturePending = false;
        int stepDenominator = 0;
        SamplePlaybackLayout::RollType rollType = SamplePlaybackLayout::RollType::Regular;
        int repeatIndex = 0;
        uint32_t randomSeed = 0x12345678u;
        double intervalSamples = 0.0;
        double samplesUntilNext = 0.0;
        std::array<VoiceRetriggerSnapshot, kMaxPlaybackVoices> snapshots {};

        void clear()
        {
            active = false;
            capturePending = false;
            stepDenominator = 0;
            rollType = SamplePlaybackLayout::RollType::Regular;
            repeatIndex = 0;
            randomSeed = 0x12345678u;
            intervalSamples = 0.0;
            samplesUntilNext = 0.0;
            for (auto& snapshot : snapshots)
                snapshot = {};
        }
    };

    std::array<Voice, kMaxPlaybackVoices> voices {};
    std::array<Voice, kMaxPlaybackVoices> fadeOutVoices {};
    static constexpr int kMaxPreviewVoices = 8;
    std::array<Voice, kMaxPreviewVoices> previewVoices {};
    std::array<Voice, kMaxPreviewVoices> previewFadeOutVoices {};

    // Thread-safe sample bank access
    juce::SpinLock bankLock;
    std::shared_ptr<const SampleBank> sharedBank;

    // Pre-loaded banks for multi-instrument per track (instrument index → bank)
    std::map<int, std::shared_ptr<const SampleBank>> preloadedBanks;

    // Params access (same pattern as InstrumentEffectsPlugin)
    SimpleSampler* samplerSource = nullptr;
    int instrumentIndex = -1;

    // Preview atomics (message thread writes, audio thread reads)
    std::array<std::atomic<int>, kMaxPreviewVoices> pendingPreviewNotes {};
    std::atomic<int> pendingPreviewNoteCount { 0 };
    std::atomic<float> previewVelocity { 0.0f };
    std::atomic<bool> previewStop { false };

    // FX pitch offset (set by InstrumentEffectsPlugin for slides/arpeggio/etc.)
    std::atomic<float> pitchOffset { 0.0f };
    std::atomic<float> granularPositionOffset { 0.0f };

    // Sample offset from 9xx effect (set via CC#9, consumed on next note-on)
    int pendingSampleOffset = -1;
    std::array<int, 128> pendingParamHighBits {};
    std::array<int, 16> channelInstruments {};
    std::array<int, 16> channelBankMsbs {};
    int legacyPendingParamHighBit = -1;
    int currentBankMsb = 0;
    int directionOverride = -1; // -1 = instrument default, 0 = backward, 1 = forward
    int sliceOverride = -1;     // -1 = instrument selectedSlice
    RetriggerState retriggerState;
    int rowsPerBeat = 4;

    // Audio thread state
    double outputSampleRate = 44100.0;
    juce::AudioBuffer<float> scratchBuffer;

    // Playback position for UI cursor (normalized 0-1, -1 = idle)
    std::atomic<float> playbackPosNorm { -1.0f };

    // Rendering
    void triggerNote (Voice& v, int note, float vel,
                      std::shared_ptr<const SampleBank> bank, const InstrumentParams& params);
    void renderVoice (Voice& v, juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    static void startFadeOut (Voice& source, Voice& fadeTarget);
    void releaseVoice (Voice& source, Voice& fadeTarget);
    float getReleaseTailGain (Voice& v);
    int getReleaseTailSamples (const InstrumentParams& params) const;
    static bool hasAudibleVolumeEnvelopeRelease (const InstrumentParams& params);

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
    void applyPositionCommandToVoice (Voice& v, int positionByte);
    void applySliceCommandToVoice (Voice& v, int sliceByte);
    void prepareGranularGrain (Voice& v, const SampleBank& bank, const InstrumentParams& params);
    void clearPendingParamHighBits();
    void clearRetriggerState();
    void requestRetriggerCapture (int denominator);
    void updateRetriggerIntervalSamples (double bpm);
    bool captureRetriggerAnchors();
    bool hasRetriggerAnchors() const;
    void applyRetrigger();
    void renderPlaybackVoices (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    void renderPlaybackVoicesWithRetrigger (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

    double getPitchRatio (int midiNote, const SampleBank& bank, const InstrumentParams& params) const;
    float interpolateSample (const SampleBank& bank, int channel, double pos) const;
    float getGranularEnvelope (const InstrumentParams& params, int pos, int length) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackerSamplerPlugin)
};
