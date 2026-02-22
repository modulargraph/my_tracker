#pragma once

#include <JuceHeader.h>
#include "SimpleSampler.h"

class TrackerEngine;

/**
 * Manages note/sample/audio-file preview playback for TrackerEngine.
 *
 * Owns the preview-related state (active track, plugin note, volume, bank)
 * and the auto-stop timer.  All heavy lifting still goes through TrackerEngine
 * accessors so transport, sampler, and plugin infrastructure remain in one place.
 */
class PreviewManager : public juce::Timer
{
public:
    explicit PreviewManager (TrackerEngine& engine);

    /** Preview a note on the dedicated preview track using a specific instrument.
     *  autoStop: when true (default), stops after kPreviewDurationMs;
     *            when false, plays until stopPreview() is called. */
    void previewNote (int trackIndex, int instrumentIndex, int midiNote, bool autoStop = true);

    /** Get the normalized playback position (0-1) of the preview voice, or -1 if idle. */
    float getPreviewPlaybackPosition() const;

    /** Preview an audio file from disk (for browser, plays on dedicated preview track). */
    void previewAudioFile (const juce::File& file);

    /** Preview an already-loaded instrument (plays note C-4 on dedicated preview track). */
    void previewInstrument (int instrumentIndex);

    /** Stop any active preview (file or note). */
    void stopPreview();

    /** Stop an active plugin instrument preview only (sends note-off).
     *  Called by TrackerEngine::stop() before halting transport. */
    bool stopPluginPreview();

    /** Set/get preview volume (linear gain 0-1). */
    void setPreviewVolume (float gainLinear);
    float getPreviewVolume() const { return previewVolume; }

    /** Timer callback for auto-stop. */
    void timerCallback() override;

    // State accessors
    int getActivePreviewTrack() const { return activePreviewTrack; }
    int getPreviewPluginNote() const { return previewPluginNote; }
    int getPreviewPluginInstrument() const { return previewPluginInstrument; }
    int getPreviewPluginTrack() const { return previewPluginTrack; }

    static constexpr int kPreviewDurationMs = 30000;
    static constexpr int kPluginPreviewDurationMs = 500;

private:

    TrackerEngine& engine;
    int activePreviewTrack = -1;
    std::shared_ptr<SampleBank> previewBank;
    float previewVolume = 1.0f;

    // Plugin instrument preview state
    int previewPluginNote = -1;
    int previewPluginInstrument = -1;
    int previewPluginTrack = -1;
};
