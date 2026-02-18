#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include "PatternData.h"
#include "SimpleSampler.h"
#include "InstrumentEffectsPlugin.h"
#include "MetronomePlugin.h"
#include "SendEffectsPlugin.h"
#include "SendEffectsParams.h"

namespace te = tracktion;

class TrackerEngine : private juce::ChangeListener,
                      private juce::Timer
{
public:
    TrackerEngine();
    ~TrackerEngine() override;

    void initialise();

    // Pattern → Edit conversion
    // releaseMode: per-track flag; true = note sustains until next note/OFF (release envelope plays)
    void syncPatternToEdit (const Pattern& pattern,
                            const std::array<bool, kNumTracks>& releaseMode = {});

    // Song-mode: concatenate multiple patterns with repeats into one long edit
    void syncArrangementToEdit (const std::vector<std::pair<const Pattern*, int>>& sequence, int rowsPerBeat,
                                const std::array<bool, kNumTracks>& releaseMode = {});

    // Transport control
    void play();
    void stop();
    void togglePlayStop();
    bool isPlaying() const;

    // Returns current playback row (based on transport position)
    int getPlaybackRow (int numRows) const;

    // Update the transport loop range for a new pattern length (call during playback)
    // If the playhead is past the new end, it resets to the beginning of the pattern.
    void updateLoopRangeForPatternLength (int numRows);

    // Re-apply instrument settings to all tracks currently using the given instrument
    // (call after loading a new sample while playback is active)
    void refreshTracksForInstrument (int instrumentIndex, const Pattern& pattern);

    // Returns current beat position (for song mode tracking)
    double getPlaybackBeatPosition() const;

    // Tempo
    void setBpm (double bpm);
    double getBpm() const;

    // Rows per beat (default 4 = 16th notes)
    void setRowsPerBeat (int rpb) { rowsPerBeat = rpb; }
    int getRowsPerBeat() const { return rowsPerBeat; }

    // Sample loading (instrument-only, no track coupling)
    juce::String loadSampleForInstrument (int instrumentIndex, const juce::File& sampleFile);

    // Ensure a track's plugin is configured for a specific instrument
    void ensureTrackHasInstrument (int trackIndex, int instrumentIndex);

    // Ensure correct instruments are loaded on each track based on pattern data
    void prepareTracksForPattern (const Pattern& pattern);

    // Query which instrument is currently loaded on a track
    int getTrackInstrument (int trackIndex) const;

    // Force re-load of instruments on next sync (call after loading a project)
    void invalidateTrackInstruments();

    // Preview a note on a track using a specific instrument (auto-stops after ~3s)
    void previewNote (int trackIndex, int instrumentIndex, int midiNote);

    // Preview an audio file from disk (for browser, plays on dedicated preview track)
    void previewAudioFile (const juce::File& file);

    // Preview an already-loaded instrument (plays note C-4 on dedicated preview track)
    void previewInstrument (int instrumentIndex);

    // Stop any active preview (file or note)
    void stopPreview();

    // Metronome
    void setMetronomeEnabled (bool enabled);
    bool isMetronomeEnabled() const;
    void setMetronomeVolume (float gainLinear);
    float getMetronomeVolume() const;

    // Get audio track
    te::AudioTrack* getTrack (int index);

    // Callback when transport state changes
    std::function<void()> onTransportChanged;

    te::Engine& getEngine() { return *engine; }
    SimpleSampler& getSampler() { return sampler; }

    // Send effects access
    SendEffectsPlugin* getSendEffectsPlugin() { return sendEffectsPlugin; }
    void setDelayParams (const DelayParams& params);
    void setReverbParams (const ReverbParams& params);
    DelayParams getDelayParams() const;
    ReverbParams getReverbParams() const;

private:
    std::unique_ptr<te::Engine> engine;
    std::unique_ptr<te::Edit> edit;
    SimpleSampler sampler;
    int rowsPerBeat = 4;
    std::array<int, kNumTracks> currentTrackInstrument {};

    // Preview, metronome, and send effects track indices
    static constexpr int kPreviewTrack = kNumTracks;
    static constexpr int kMetronomeTrack = kNumTracks + 1;
    static constexpr int kSendEffectsTrack = kNumTracks + 2;
    SendEffectsPlugin* sendEffectsPlugin = nullptr;
    void setupSendEffectsTrack();
    static constexpr int kPreviewDurationMs = 3000;
    int activePreviewTrack = -1;
    std::shared_ptr<SampleBank> previewBank;

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackerEngine)
};
