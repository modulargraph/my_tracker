#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include "PatternData.h"
#include "SimpleSampler.h"
#include "InstrumentEffectsPlugin.h"

namespace te = tracktion;

class TrackerEngine : private juce::ChangeListener
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

    // Returns current beat position (for song mode tracking)
    double getPlaybackBeatPosition() const;

    // Tempo
    void setBpm (double bpm);
    double getBpm() const;

    // Rows per beat (default 4 = 16th notes)
    void setRowsPerBeat (int rpb) { rowsPerBeat = rpb; }
    int getRowsPerBeat() const { return rowsPerBeat; }

    // Sample loading
    juce::String loadSampleForTrack (int trackIndex, const juce::File& sampleFile);

    // Ensure correct instruments are loaded on each track based on pattern data
    void prepareTracksForPattern (const Pattern& pattern);

    // Query which instrument is currently loaded on a track
    int getTrackInstrument (int trackIndex) const;

    // Force re-load of instruments on next sync (call after loading a project)
    void invalidateTrackInstruments();

    // Preview a note on a track
    void previewNote (int trackIndex, int midiNote);

    // Get audio track
    te::AudioTrack* getTrack (int index);

    // Callback when transport state changes
    std::function<void()> onTransportChanged;

    te::Engine& getEngine() { return *engine; }
    SimpleSampler& getSampler() { return sampler; }

private:
    std::unique_ptr<te::Engine> engine;
    std::unique_ptr<te::Edit> edit;
    SimpleSampler sampler;
    int rowsPerBeat = 4;
    std::array<int, kNumTracks> currentTrackInstrument {};

    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackerEngine)
};
