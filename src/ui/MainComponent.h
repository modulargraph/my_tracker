#pragma once

#include <JuceHeader.h>
#include "PatternData.h"
#include "PatternTypes.h"
#include "TrackerEngine.h"
#include "TrackerLookAndFeel.h"
#include "Arrangement.h"
#include "TrackLayout.h"
#include "MixerState.h"
#include "TabBarComponent.h"
#include "PluginAutomationComponent.h"

class ArrangementComponent;
class InstrumentPanel;
class MixerComponent;
class PluginAutomationComponent;
class SampleBrowserComponent;
class SampleEditorComponent;
class SendEffectsComponent;
class ToolbarComponent;
class TrackerGrid;

struct MidiGeneratorSettings
{
    int keyRoot = 0;
    int scale = 0;
    int progression = 0;
    int outputMode = 0;
    int chordStyle = 0;
    int chordSet = 0;
    int chordRhythm = 0;
    int bassPattern = 0;
    int bars = 8;
    int transpose = 0;
    int randomAmount = 20;
    bool startAtCursor = false;
};

class MainComponent : public juce::Component,
                      public juce::KeyListener,
                      public juce::ApplicationCommandTarget,
                      public juce::MenuBarModel,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    // KeyListener override
    bool keyPressed (const juce::KeyPress& key, juce::Component* originatingComponent) override;
    using juce::Component::keyPressed;

    // ApplicationCommandTarget
    ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>& commands) override;
    void getCommandInfo (juce::CommandID commandID, juce::ApplicationCommandInfo& result) override;
    bool perform (const InvocationInfo& info) override;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int menuIndex, const juce::String& menuName) override;
    void menuItemSelected (int /*menuItemID*/, int /*topLevelMenuIndex*/) override {}

    juce::ApplicationCommandManager commandManager;

    enum CommandIDs
    {
        loadSample      = 0x1001,
        nextPattern     = 0x1010,
        prevPattern     = 0x1011,
        addPattern      = 0x1012,
        muteTrack       = 0x1020,
        soloTrack       = 0x1021,
        cmdCopy         = 0x1030,
        cmdPaste        = 0x1031,
        cmdCut          = 0x1032,
        cmdUndo         = 0x1033,
        cmdRedo         = 0x1034,
        cmdNewProject   = 0x1040,
        cmdOpen         = 0x1041,
        cmdSave         = 0x1042,
        cmdSaveAs       = 0x1043,
        cmdShowHelp     = 0x1050,
        cmdToggleArrangement = 0x1051,
        cmdToggleSongMode    = 0x1052,
        cmdToggleInstrumentPanel = 0x1053,
        cmdToggleMetronome       = 0x1054,
        cmdToggleVelocityLanes   = 0x1055,
        cmdToggleAudioUnitEquivalents = 0x1056,
        cmdAudioPluginSettings   = 0x1060
    };

    // Access for serialization
    PatternData& getPatternData() { return patternData; }
    TrackerEngine& getTrackerEngine() { return trackerEngine; }
    TrackerGrid& getTrackerGrid() { return *trackerGrid; }
    bool confirmDiscardChanges();

private:
    TrackerLookAndFeel trackerLookAndFeel;
    TrackLayout trackLayout;
    PatternData patternData;
    TrackerEngine trackerEngine;
    std::unique_ptr<TabBarComponent> tabBar;
    Tab activeTab = Tab::Tracker;
    std::unique_ptr<ToolbarComponent> toolbar;
    std::unique_ptr<TrackerGrid> trackerGrid;
    juce::UndoManager undoManager;
    Arrangement arrangement;
    std::unique_ptr<ArrangementComponent> arrangementComponent;
    std::unique_ptr<InstrumentPanel> instrumentPanel;
    std::unique_ptr<SampleEditorComponent> sampleEditor;
    std::unique_ptr<SampleBrowserComponent> fileBrowser;
    std::unique_ptr<MixerComponent> mixerComponent;
    MixerState mixerState;
    std::unique_ptr<SendEffectsComponent> sendEffectsComponent;
    std::unique_ptr<PluginAutomationComponent> automationPanel;
    bool automationPanelVisible = false;
    MidiGeneratorSettings lastMidiGeneratorSettings;
    bool chordEntryEnabled = false;
    int lastAutomationPopulateTrack = -1;
    int lastPluginModTriggerRowSerial = -1;
    double lastPluginModTriggerBeat = -1.0;
    std::map<int, std::vector<AutomatablePluginInfo>> automationPluginCache;
    std::map<int, std::pair<juce::String, int>> automationSelectionPerTrack;
    std::map<int, juce::String> detectedSamplePitchLabels;
    void invalidateAutomationPluginCache (int trackIndex = -1);
    void saveAutomationSelection();
    void restoreAutomationSelection (int trackIndex);
    bool arrangementVisible = false;
    bool instrumentPanelVisible = true;
    bool showAudioUnitEquivalents = false;
    bool songMode = false;
    enum class FollowMode { Off, Center, Page };
    FollowMode followMode = FollowMode::Off;

    // Song mode arrangement playback tracking
    struct ArrangementPlaybackInfo
    {
        int entryIndex = -1;
        int patternIndex = -1;
        int rowInPattern = -1;
    };
    ArrangementPlaybackInfo getArrangementPlaybackPosition (double beatPos) const;

    // Status bar info
    juce::Label statusLabel;
    juce::Label octaveLabel;
    juce::Label bpmLabel;
    juce::Label previewVolumeLabel;
    juce::Slider previewVolumeSlider;
    juce::Label uiScaleLabel;
    juce::Slider uiScaleSlider;
    int uiScalePercent = 100;

    // Temporary status message system (Phase 4)
    juce::String temporaryStatusMessage;
    bool temporaryStatusIsError = false;
    juce::uint32 temporaryStatusExpiry = 0; // millisecond timestamp when message expires
    void setTemporaryStatus (const juce::String& message, bool isError = false, int timeoutMs = 3000);

    void timerCallback() override;
    void updateStatusBar();
    void updateToolbar();
    void setUiScalePercent (int scalePercent, bool persist);
    void updateUiScaleLabel();
    void loadSampleForCurrentTrack();
    void switchToPattern (int index);
    void showPatternLengthEditor();
    void showPatternNameEditor();
    void showMidiGeneratorDialog (int targetTrack = -1);
    void applyGeneratedMidiToTrack (int targetTrack, const MidiGeneratorSettings& settings);
    bool enterChordFromKeyboardNote (int rootNote, int row, int targetTrack, int startNoteLane, int instrument);
    void setChordEntryEnabled (bool enabled);
    void cycleChordEntrySet();
    juce::String getChordEntryToolbarLabel() const;
    juce::String getChordEntryStatusText() const;
    void transposeNotesInRange (int startRow, int endRow, int startVisualTrack, int endVisualTrack, int semitones);
    void transposeSelectedNotes (int semitones);
    void wiggleTrackVelocities (int track, int amount);
    void showTrackHeaderMenu (int track, juce::Point<int> screenPos);
    void showRenameTrackDialog (int track);
    void performUndoableTrackLayoutChange (const std::function<void()>& changeFn);
    void removePatternAndRepairArrangement (int index);
    int resolveInstrumentForTrackDrop (int track) const;
    void resyncPlaybackForCurrentMode();
    void updateMuteSoloState();
    void doCopy();
    void doPaste();
    void doCut();

    // Save/Load
    juce::File currentProjectFile;
    bool isDirty = false;
    void markDirty();
    void updateWindowTitle();
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    void toggleArrangementPanel();
    void toggleSongMode();
    void toggleVelocityLanes();
    void setVelocityLanesVisible (bool visible, bool persist);
    void toggleAudioUnitEquivalents();
    void setAudioUnitEquivalentsVisible (bool visible, bool persist);
    void syncArrangementToEdit();
    void showHelpOverlay();
    void updateInstrumentPanel();
    std::array<bool, kNumTracks> getReleaseModes() const;
    void applyInstrumentParamsToPlayback (int instrument, const InstrumentParams& params);
    juce::String loadSampleAndMaybeDetectPitch (int instrument, const juce::File& file);
    void maybeAutoDetectSamplePitch (int instrument);
    void syncDetectedPitchLabelsToBrowser();
    void loadSampleForInstrument (int instrument);
    void clearSampleForInstrument (int instrument);
    void updateSampleEditorForCurrentInstrument();
    void updateTrackSampleMarkers();
    void cycleTab (int direction);
    void switchToTab (Tab tab);
    void focusContentForTab (Tab tab);
    void showAudioPluginSettings();
    void refreshAutomationPanel (bool forcePopulate = true);
    void populateAutomationPlugins();
    void navigateToAutomationParam (const juce::String& pluginId, int paramIndex);
    void applyAutomationAtPlaybackPosition (int playPatternIndex, int playRow);
    void applyPluginModulationAtPlaybackPosition (int playPatternIndex,
                                                  int playRow,
                                                  double beatPosition,
                                                  const juce::String& excludedPluginId = {},
                                                  int excludedParamIndex = -1);
    int resolvePluginInstrumentForTrackRow (int trackIndex, const NoteSlot& noteSlot) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
