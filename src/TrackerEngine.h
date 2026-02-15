#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include "PatternData.h"
#include "SimpleSampler.h"

namespace te = tracktion;

class TrackerEngine : private juce::ChangeListener
{
public:
    TrackerEngine();
    ~TrackerEngine() override;

    void initialise();

    // Pattern → Edit conversion
    void syncPatternToEdit (const Pattern& pattern);

    // Transport control
    void play();
    void stop();
    void togglePlayStop();
    bool isPlaying() const;

    // Returns current playback row (based on transport position)
    int getPlaybackRow (int numRows) const;

    // Tempo
    void setBpm (double bpm);
    double getBpm() const;

    // Rows per beat (default 4 = 16th notes)
    void setRowsPerBeat (int rpb) { rowsPerBeat = rpb; }
    int getRowsPerBeat() const { return rowsPerBeat; }

    // Sample loading
    juce::String loadSampleForTrack (int trackIndex, const juce::File& sampleFile);

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

    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackerEngine)
};
