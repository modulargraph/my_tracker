#pragma once

#include <JuceHeader.h>
#include "PatternData.h"
#include "TrackerGrid.h"
#include "TrackerEngine.h"
#include "TrackerLookAndFeel.h"
#include "ToolbarComponent.h"
#include "Clipboard.h"
#include "ProjectSerializer.h"
#include "Arrangement.h"
#include "ArrangementComponent.h"
#include "InstrumentPanel.h"
#include "SampleEditorComponent.h"
#include "TrackLayout.h"
#include "TabBarComponent.h"
#include "FileBrowserComponent.h"
#include "MixerComponent.h"
#include "MixerState.h"
#include "SendEffectsComponent.h"

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
        cmdToggleMetronome       = 0x1054
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
    bool arrangementVisible = false;
    bool instrumentPanelVisible = true;
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

    void timerCallback() override;
    void updateStatusBar();
    void updateToolbar();
    void loadSampleForCurrentTrack();
    void switchToPattern (int index);
    void showPatternLengthEditor();
    void showPatternNameEditor();
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
    void syncArrangementToEdit();
    void showHelpOverlay();
    void updateInstrumentPanel();
    std::array<bool, kNumTracks> getReleaseModes() const;
    void loadSampleForInstrument (int instrument);
    void clearSampleForInstrument (int instrument);
    void updateSampleEditorForCurrentInstrument();
    void updateTrackSampleMarkers();
    void cycleTab (int direction);
    void switchToTab (Tab tab);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
