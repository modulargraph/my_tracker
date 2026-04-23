#pragma once

#include <JuceHeader.h>
#include <functional>
#include <map>
#include <memory>
#include "TrackerConstants.h"
#include "SimpleSampler.h"
#include "MixerState.h"
#include "InstrumentSlotInfo.h"

namespace te = tracktion;

class PluginCatalogService;
class SendEffectsPlugin;
struct Pattern;
struct PatternAutomationData;
struct DelayParams;
struct ReverbParams;

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
    void setRowsPerBeat (int rpb);
    int getRowsPerBeat() const { return rowsPerBeat; }

    // Sample loading (instrument-only, no track coupling)
    juce::String loadSampleForInstrument (int instrumentIndex, const juce::File& sampleFile);
    void clearSampleForInstrument (int instrumentIndex);

    // Ensure a track's plugin is configured for a specific instrument
    void ensureTrackHasInstrument (int trackIndex, int instrumentIndex);

    // Ensure correct instruments are loaded on each track based on pattern data
    void prepareTracksForPattern (const Pattern& pattern);

    // Query which instrument is currently loaded on a track
    int getTrackInstrument (int trackIndex) const;

    // Force re-load of instruments on next sync (call after loading a project)
    void invalidateTrackInstruments();

    // Preview a note on a track using a specific instrument
    // autoStop: when true (default), stops after kPreviewDurationMs; when false, plays until stopPreview()
    void previewNote (int trackIndex, int instrumentIndex, int midiNote, bool autoStop = true);

    // Get the normalized playback position (0-1) of the preview voice, or -1 if idle
    float getPreviewPlaybackPosition() const;

    // Preview an audio file from disk (for browser, plays on dedicated preview track)
    void previewAudioFile (const juce::File& file);

    // Preview an already-loaded instrument (plays note C-4 on dedicated preview track)
    void previewInstrument (int instrumentIndex);

    // Stop any active preview (file or note)
    void stopPreview();
    void setPreviewVolume (float gainLinear);
    float getPreviewVolume() const { return previewVolume; }

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
    PluginCatalogService& getPluginCatalog() { return *pluginCatalog; }

    // Send effects access
    SendEffectsPlugin* getSendEffectsPlugin() { return sendEffectsPlugin; }
    void setDelayParams (const DelayParams& params);
    void setReverbParams (const ReverbParams& params);
    DelayParams getDelayParams() const;
    ReverbParams getReverbParams() const;

    // Mixer DSP: set a pointer to the MixerState for per-track processing
    void setMixerState (MixerState* state);
    void refreshMixerPlugins();
    void rebuildMixerPluginChains();

    // Insert plugin management
    bool addInsertPlugin (int trackIndex, const juce::PluginDescription& desc);
    void removeInsertPlugin (int trackIndex, int slotIndex);
    void setInsertBypassed (int trackIndex, int slotIndex, bool bypassed);
    te::Plugin* getInsertPlugin (int trackIndex, int slotIndex);
    void rebuildInsertChain (int trackIndex);
    bool addMasterInsertPlugin (const juce::PluginDescription& desc);
    void removeMasterInsertPlugin (int slotIndex);
    void setMasterInsertBypassed (int slotIndex, bool bypassed);
    te::Plugin* getMasterInsertPlugin (int slotIndex);
    void rebuildMasterInsertChain();
    void snapshotInsertPluginStates();
    void snapshotPluginInstrumentStates();

    // Plugin editor window management
    void openPluginEditor (int trackIndex, int slotIndex);
    void closePluginEditor (int trackIndex, int slotIndex);
    void openMasterPluginEditor (int slotIndex);
    void closeMasterPluginEditor (int slotIndex);

    // Callback when inserts change (for UI refresh)
    std::function<void()> onInsertStateChanged;

    // Peak level metering (read from audio thread, consumed by UI)
    float getTrackPeakLevel (int trackIndex) const;
    void decayTrackPeaks();

    //==============================================================================
    // Plugin instrument slot management (Phase 4)
    //==============================================================================

    /** Get the instrument slot info for a given instrument index. */
    const InstrumentSlotInfo& getInstrumentSlotInfo (int instrumentIndex) const;

    /** Set a plugin instrument for a slot: assigns the plugin description and owner track. */
    bool setPluginInstrument (int instrumentIndex, const juce::PluginDescription& desc, int ownerTrack);

    /** Clear a plugin instrument slot (reverts to sample mode). */
    void clearPluginInstrument (int instrumentIndex);

    /** Check if a given instrument index is a plugin instrument. */
    bool isPluginInstrument (int instrumentIndex) const;

    /** Get the owner track for a plugin instrument (-1 if sample or unassigned). */
    int getPluginInstrumentOwnerTrack (int instrumentIndex) const;

    /** Get all instrument slot infos (for serialization). */
    const std::map<int, InstrumentSlotInfo>& getAllInstrumentSlotInfos() const { return instrumentSlotInfos; }

    /** Set instrument slot infos (for deserialization). */
    void setInstrumentSlotInfos (const std::map<int, InstrumentSlotInfo>& infos);

    /** Determine the content mode of a track based on assigned instruments. */
    TrackContentMode getTrackContentMode (int trackIndex) const;

    /**
     * Validate whether a note entry is allowed for the given instrument on the given track.
     * Returns empty string if allowed, or an error message if blocked.
     */
    juce::String validateNoteEntry (int instrumentIndex, int trackIndex) const;

    /** Get the Tracktion plugin instance for a plugin instrument (nullptr if not loaded). */
    te::Plugin* getPluginInstrumentInstance (int instrumentIndex);

    /** Open the editor window for a plugin instrument. */
    void openPluginInstrumentEditor (int instrumentIndex);

    /** Close the editor window for a plugin instrument. */
    void closePluginInstrumentEditor (int instrumentIndex);

    /** Callback for status messages (set by MainComponent). */
    std::function<void (const juce::String& message, bool isError, int timeoutMs)> onStatusMessage;

    /** Callback for click-to-automate: navigates to the automation lane for a parameter. */
    std::function<void (const juce::String& pluginId, int paramIndex)> onNavigateToAutomation;

    /** Callback when a plugin instrument is cleared (for automation cleanup). */
    std::function<void (const juce::String& pluginId)> onPluginInstrumentCleared;

    //==============================================================================
    // Plugin automation (Phase 5)
    //==============================================================================

    /** Apply automation data from a pattern to plugin parameter automation curves.
     *  This is called during syncPatternToEdit to map modulation points to plugin parameters. */
    void applyPatternAutomation (const PatternAutomationData& automationData,
                                 int patternLength, int rowsPerBeat);

    /** Apply automation values for a specific playback row (used by live playback updates).
     *  The optional exclusion is used while recording a live VST parameter so
     *  playback automation does not fight the user's current gesture. */
    void applyAutomationForPlaybackRow (const PatternAutomationData& automationData,
                                        int row,
                                        const juce::String& excludedPluginId = {},
                                        int excludedParamIndex = -1);

    /** Reset all plugin parameters modified by automation to their baseline values.
     *  Called when switching patterns or stopping playback. */
    void resetAutomationParameters();

    /** Resolve a plugin ID string to an AudioPluginInstance (public for automation panel). */
    juce::AudioPluginInstance* resolvePluginInstance (const juce::String& pluginId);

    /** Returns true when a parameter change matches the most recent value written
     *  by tracker automation and should not be treated as a manual VST UI gesture. */
    bool shouldSuppressPluginAutoLearnChange (const juce::String& pluginId,
                                              int paramIndex,
                                              float currentValue) const;

private:
    std::unique_ptr<te::Engine> engine;
    std::unique_ptr<te::Edit> edit;
    SimpleSampler sampler;
    std::unique_ptr<PluginCatalogService> pluginCatalog;
    int rowsPerBeat = 4;
    std::array<int, kNumTracks + 3> currentTrackInstrument {};

    // Preview, metronome, and send effects track indices
    static constexpr int kPreviewTrack = kNumTracks;
    static constexpr int kMetronomeTrack = kNumTracks + 1;
    static constexpr int kSendEffectsTrack = kNumTracks + 2;
    SendEffectsPlugin* sendEffectsPlugin = nullptr;
    MixerState* mixerStatePtr = nullptr;
    void setupSendEffectsTrack();
    void setupMixerPlugins();
    void setupChannelStripAndOutput (int trackIndex);
    void openExternalPluginEditor (te::Plugin* plugin, const juce::String& key);

    // Plugin editor windows (keyed by "track:slot")
    std::map<juce::String, std::unique_ptr<juce::DocumentWindow>> pluginEditorWindows;
    void closePluginEditorsForTrack (int trackIndex);
    void refreshTransportLoopRangeFromClip();
    static constexpr int kPreviewDurationMs = 30000;
    static constexpr int kPluginPreviewDurationMs = 500;
    int activePreviewTrack = -1;
    std::shared_ptr<SampleBank> previewBank;
    float previewVolume = 1.0f;

    // Plugin instrument preview state
    int previewPluginNote = -1;
    int previewPluginInstrument = -1;
    int previewPluginTrack = -1;
    bool stopPluginPreview();

    void prepareTracksForInstrumentUsage (const std::array<std::vector<int>, kNumTracks>& instrumentsByTrack);
    void rebuildTempoSequenceFromPatternMasterLane (const Pattern& pattern);

    // Plugin instrument slot state
    std::map<int, InstrumentSlotInfo> instrumentSlotInfos;
    // Loaded plugin instrument instances (keyed by instrument index)
    std::map<int, te::Plugin::Ptr> pluginInstrumentInstances;
    // Plugin instrument editor windows (keyed by instrument index)
    std::map<int, std::unique_ptr<juce::DocumentWindow>> pluginInstrumentEditorWindows;
    void clearPluginInstrumentInternal (int instrumentIndex, bool notifyAutomation);
    void unloadAllPluginInstruments (bool notifyAutomation);

    // Automation state tracking
    struct AutomatedParam
    {
        juce::String pluginId;
        int paramIndex = -1;
        float baselineValue = 0.0f;
    };
    struct AutomatedParamWrite
    {
        juce::String pluginId;
        int paramIndex = -1;
        float value = 0.0f;
    };
    std::vector<AutomatedParam> lastAutomatedParams;
    std::vector<AutomatedParamWrite> recentAutomatedParamWrites;
    AutomatedParam* findAutomatedParam (const juce::String& pluginId, int paramIndex);
    const AutomatedParam* findAutomatedParam (const juce::String& pluginId, int paramIndex) const;
    void forgetAutomatedParamsForPlugin (const juce::String& pluginId);
    void forgetAutomatedParamsForInsertTrack (int trackIndex);
    const AutomatedParamWrite* findRecentAutomatedParamWrite (const juce::String& pluginId, int paramIndex) const;
    void rememberAutomatedParamWrite (const juce::String& pluginId, int paramIndex, float value);

    // Ensure the plugin instrument is loaded on its owner track
    void ensurePluginInstrumentLoaded (int instrumentIndex);
    void removePluginInstrumentFromTrack (int instrumentIndex);
    void rebuildTempoSequenceFromArrangementMasterLane (const std::vector<std::pair<const Pattern*, int>>& sequence, int rpb);

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackerEngine)
};
