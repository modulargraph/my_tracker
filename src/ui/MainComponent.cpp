#include "MainComponent.h"

MainComponent::MainComponent()
{
    setLookAndFeel (&trackerLookAndFeel);

    // Initialise the engine
    trackerEngine.initialise();
    trackerEngine.setMixerState (&mixerState);

    // Create tab bar
    tabBar = std::make_unique<TabBarComponent> (trackerLookAndFeel);
    addAndMakeVisible (*tabBar);
    tabBar->onTabChanged = [this] (Tab tab) { switchToTab (tab); };

    // Create toolbar
    toolbar = std::make_unique<ToolbarComponent> (trackerLookAndFeel);
    addAndMakeVisible (*toolbar);

    toolbar->onAddPattern = [this]
    {
        patternData.addPattern (patternData.getCurrentPattern().numRows);
        switchToPattern (patternData.getNumPatterns() - 1);
        markDirty();
    };
    toolbar->onDuplicatePattern = [this]
    {
        int idx = patternData.getCurrentPatternIndex();
        patternData.duplicatePattern (idx);
        arrangement.remapAfterPatternInserted (idx + 1);
        switchToPattern (idx + 1);
        if (trackerEngine.isPlaying() && songMode)
            syncArrangementToEdit();
        markDirty();
    };
    toolbar->onRemovePattern = [this]
    {
        int idx = patternData.getCurrentPatternIndex();
        auto& pat = patternData.getCurrentPattern();

        // Only delete when the current pattern is empty.
        // Otherwise this behaves like "previous pattern".
        bool hasData = false;
        for (int r = 0; r < pat.numRows && ! hasData; ++r)
            for (int t = 0; t < kNumTracks && ! hasData; ++t)
                if (! pat.getCell (r, t).isEmpty())
                    hasData = true;
        for (int r = 0; r < pat.numRows && ! hasData; ++r)
            for (int lane = 0; lane < trackLayout.getMasterFxLaneCount() && ! hasData; ++lane)
                if (! pat.getMasterFxSlot (r, lane).isEmpty())
                    hasData = true;

        if (hasData)
        {
            if (idx > 0)
                switchToPattern (idx - 1);
            return;
        }

        if (patternData.getNumPatterns() <= 1)
            return;

        removePatternAndRepairArrangement (idx);
        if (idx > 0)
            switchToPattern (idx - 1);
        else
        {
            switchToPattern (0);
        }
        markDirty();
    };
    toolbar->onNextPattern = [this]
    {
        int idx = patternData.getCurrentPatternIndex();
        if (idx + 1 >= patternData.getNumPatterns())
        {
            // At end — create a new pattern
            patternData.addPattern (patternData.getCurrentPattern().numRows);
            markDirty();
        }
        switchToPattern (idx + 1);
    };
    toolbar->onPrevPattern = [this]
    {
        int idx = patternData.getCurrentPatternIndex();
        if (idx > 0)
        {
            // If on last pattern and it's empty, remove it
            if (idx == patternData.getNumPatterns() - 1)
            {
                auto& pat = patternData.getCurrentPattern();
                bool hasData = false;
                for (int r = 0; r < pat.numRows && ! hasData; ++r)
                    for (int t = 0; t < kNumTracks && ! hasData; ++t)
                        if (! pat.getCell (r, t).isEmpty())
                            hasData = true;
                for (int r = 0; r < pat.numRows && ! hasData; ++r)
                    for (int lane = 0; lane < trackLayout.getMasterFxLaneCount() && ! hasData; ++lane)
                        if (! pat.getMasterFxSlot (r, lane).isEmpty())
                            hasData = true;
                if (! hasData)
                {
                    removePatternAndRepairArrangement (idx);
                    markDirty();
                }
            }
            switchToPattern (idx - 1);
        }
    };
    toolbar->onPatternLengthClick = [this] { showPatternLengthEditor(); };

    toolbar->onLengthDrag = [this] (int delta)
    {
        if (patternData.getNumPatterns() == 0) return;
        auto& pat = patternData.getCurrentPattern();
        int newLen = juce::jlimit (1, 256, pat.numRows + delta);
        pat.resize (newLen);
        trackerGrid->setCursorPosition (
            juce::jmin (trackerGrid->getCursorRow(), newLen - 1),
            trackerGrid->getCursorTrack());

        if (trackerEngine.isPlaying())
        {
            if (songMode)
                syncArrangementToEdit();
            else
            {
                trackerEngine.syncPatternToEdit (pat, getReleaseModes());
                trackerEngine.updateLoopRangeForPatternLength (pat.numRows);
            }
        }

        updateToolbar();
        markDirty();
    };

    toolbar->onBpmDrag = [this] (double delta)
    {
        trackerEngine.setBpm (juce::jlimit (20.0, 999.0, trackerEngine.getBpm() + delta));
        updateStatusBar();
        updateToolbar();
        markDirty();
    };

    toolbar->onRpbDrag = [this] (int delta)
    {
        int rpb = juce::jlimit (1, 16, trackerEngine.getRowsPerBeat() + delta);
        trackerEngine.setRowsPerBeat (rpb);
        trackerGrid->setRowsPerBeat (rpb);

        if (trackerEngine.isPlaying())
            resyncPlaybackForCurrentMode();

        updateToolbar();
        markDirty();
    };

    toolbar->onStepDrag = [this] (int delta)
    {
        trackerGrid->setEditStep (juce::jlimit (0, 16, trackerGrid->getEditStep() + delta));
        updateStatusBar();
        updateToolbar();
    };

    toolbar->onOctaveDrag = [this] (int delta)
    {
        int oct = juce::jlimit (0, 9, trackerGrid->getOctave() + delta);
        trackerGrid->setOctave (oct);
        sampleEditor->setOctave (oct);
        updateStatusBar();
        updateToolbar();
    };

    toolbar->onModeToggle = [this]
    {
        toggleSongMode();
    };

    toolbar->onPatternNameDoubleClick = [this]
    {
        showPatternNameEditor();
    };

    toolbar->onToggleArrangement = [this]
    {
        toggleArrangementPanel();
        toolbar->setArrangementVisible (arrangementVisible);
    };

    toolbar->onToggleInstrumentPanel = [this]
    {
        instrumentPanelVisible = ! instrumentPanelVisible;
        toolbar->setInstrumentPanelVisible (instrumentPanelVisible);
        resized();
    };

    toolbar->onInstrumentDrag = [this] (int delta)
    {
        int inst = juce::jlimit (0, 255, trackerGrid->getCurrentInstrument() + delta);
        trackerGrid->setCurrentInstrument (inst);
        instrumentPanel->setSelectedInstrument (inst);
        updateStatusBar();
        updateToolbar();
    };

    toolbar->onFollowToggle = [this]
    {
        // Cycle: Off → Center → Page → Off
        if (followMode == FollowMode::Off)
            followMode = FollowMode::Center;
        else if (followMode == FollowMode::Center)
            followMode = FollowMode::Page;
        else
            followMode = FollowMode::Off;
        toolbar->setFollowMode (static_cast<int> (followMode));
        markDirty();
    };

    toolbar->onMetronomeToggle = [this]
    {
        bool enabled = ! trackerEngine.isMetronomeEnabled();
        trackerEngine.setMetronomeEnabled (enabled);
        toolbar->setMetronomeEnabled (enabled);
    };

    toolbar->onShowFxReference = [this]
    {
        auto mousePos = juce::Desktop::getInstance().getMousePosition();
        trackerGrid->showFxCommandPopupAt (mousePos);
    };

    previewVolumeLabel.setText ("Preview", juce::dontSendNotification);
    previewVolumeLabel.setJustificationType (juce::Justification::centredRight);
    previewVolumeLabel.setColour (juce::Label::textColourId,
                                  trackerLookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.7f));
    addAndMakeVisible (previewVolumeLabel);

    previewVolumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    previewVolumeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    previewVolumeSlider.setRange (0.0, 1.0, 0.01);
    previewVolumeSlider.setValue (trackerEngine.getPreviewVolume(), juce::dontSendNotification);
    previewVolumeSlider.onValueChange = [this]
    {
        trackerEngine.setPreviewVolume (static_cast<float> (previewVolumeSlider.getValue()));
    };
    addAndMakeVisible (previewVolumeSlider);

    // Create arrangement panel (hidden by default)
    arrangementComponent = std::make_unique<ArrangementComponent> (arrangement, patternData, trackerLookAndFeel);
    addChildComponent (*arrangementComponent);
    arrangementComponent->onSwitchToPattern = [this] (int patIdx)
    {
        switchToPattern (patIdx);
    };
    arrangementComponent->onAddEntryRequested = [this]
    {
        int patIdx = patternData.getCurrentPatternIndex();
        int pos = (arrangementComponent->getSelectedEntry() >= 0)
                      ? arrangementComponent->getSelectedEntry() + 1
                      : arrangement.getNumEntries();
        arrangement.insertEntry (pos, patIdx);
        arrangementComponent->setSelectedEntry (pos);

        if (trackerEngine.isPlaying() && songMode)
            syncArrangementToEdit();

        markDirty();
    };
    arrangementComponent->onArrangementChanged = [this]
    {
        if (trackerEngine.isPlaying() && songMode)
            syncArrangementToEdit();

        markDirty();
    };

    // Create instrument panel (right side, visible by default)
    instrumentPanel = std::make_unique<InstrumentPanel> (trackerLookAndFeel);
    addAndMakeVisible (*instrumentPanel);
    instrumentPanel->onLoadSampleRequested = [this] (int inst)
    {
        loadSampleForInstrument (inst);
    };
    instrumentPanel->onClearSampleRequested = [this] (int inst)
    {
        clearSampleForInstrument (inst);
    };
    instrumentPanel->onEditSampleRequested = [this] (int inst)
    {
        trackerGrid->setCurrentInstrument (inst);
        instrumentPanel->setSelectedInstrument (inst);
        switchToTab (Tab::InstrumentEdit);
    };
    instrumentPanel->onInstrumentSelected = [this] (int inst)
    {
        trackerGrid->setCurrentInstrument (inst);
        updateStatusBar();
        updateToolbar();
        // Refresh editor if on an edit/type tab
        if (activeTab == Tab::InstrumentEdit || activeTab == Tab::InstrumentType)
            updateSampleEditorForCurrentInstrument();
    };

    // Create sample editor (always present, shown in edit/type tabs)
    sampleEditor = std::make_unique<SampleEditorComponent> (trackerLookAndFeel);
    addAndMakeVisible (*sampleEditor);

    sampleEditor->onParamsChanged = [this] (int inst, const InstrumentParams& params)
    {
        trackerEngine.getSampler().setParams (inst, params);

        // Apply to all tracks that currently use this instrument
        bool applied = false;
        for (int t = 0; t < kNumTracks; ++t)
        {
            if (trackerEngine.getTrackInstrument (t) == inst)
            {
                auto* track = trackerEngine.getTrack (t);
                if (track != nullptr)
                {
                    trackerEngine.getSampler().applyParams (*track, inst);
                    applied = true;
                }
            }
        }

        // Fallback: apply to the instrument's home track (before first playback sync)
        if (! applied && inst >= 0 && inst < kNumTracks)
        {
            auto* track = trackerEngine.getTrack (inst);
            if (track != nullptr)
                trackerEngine.getSampler().applyParams (*track, inst);
        }
        markDirty();
    };
    sampleEditor->onRealtimeParamsChanged = [this] (int inst, const InstrumentParams& params)
    {
        // Lightweight path: update params map only — InstrumentEffectsPlugin reads
        // from the params map each audio block, so no applyParams() needed
        trackerEngine.getSampler().setParams (inst, params);
        markDirty();
    };
    sampleEditor->onPreviewRequested = [this] (int inst, int note)
    {
        // Preview through dedicated preview track (no auto-stop; key release stops it).
        int previewTrack = trackerGrid->isCursorInMasterLane() ? 0 : trackerGrid->getCursorTrack();
        trackerEngine.previewNote (previewTrack, inst, note, false);
    };
    sampleEditor->onPreviewStopped = [this]()
    {
        trackerEngine.stopPreview();
    };
    sampleEditor->onGetPreviewPosition = [this]() -> float
    {
        return trackerEngine.getPreviewPlaybackPosition();
    };

    // Create mixer component (hidden by default)
    mixerComponent = std::make_unique<MixerComponent> (trackerLookAndFeel, mixerState, trackLayout);
    addChildComponent (*mixerComponent);

    mixerComponent->onMuteChanged = [this] (int track, bool muted)
    {
        auto* t = trackerEngine.getTrack (track);
        if (t != nullptr)
        {
            t->setMute (muted);
            updateMuteSoloState();
        }
        markDirty();
    };
    mixerComponent->onSoloChanged = [this] (int track, bool soloed)
    {
        auto* t = trackerEngine.getTrack (track);
        if (t != nullptr)
        {
            t->setSolo (soloed);
            updateMuteSoloState();
        }
        markDirty();
    };
    mixerComponent->onMixStateChanged = [this]
    {
        trackerEngine.refreshMixerPlugins();
        markDirty();
    };

    // Wire peak level metering from engine to mixer UI
    mixerComponent->setPeakLevelCallback ([this] (int track) -> float
    {
        return trackerEngine.getTrackPeakLevel (track);
    });
    mixerComponent->startMetering();

    // Create file browser (hidden by default)
    fileBrowser = std::make_unique<SampleBrowserComponent> (trackerLookAndFeel);
    addChildComponent (*fileBrowser);

    // Restore last browser directory from global prefs
    {
        auto savedDir = ProjectSerializer::loadGlobalBrowserDir();
        if (savedDir.isNotEmpty())
        {
            juce::File dir (savedDir);
            if (dir.isDirectory())
                fileBrowser->setCurrentDirectory (dir);
        }
    }

    fileBrowser->onDirectoryChanged = [] (const juce::File& dir)
    {
        ProjectSerializer::saveGlobalBrowserDir (dir.getFullPathName());
    };
    fileBrowser->onLoadSample = [this] (int instrument, const juce::File& file)
    {
        auto error = trackerEngine.loadSampleForInstrument (instrument, file);
        if (error.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Load Error", error);
        else
        {
            if (trackerEngine.isPlaying())
            {
                if (songMode)
                    syncArrangementToEdit();
                else
                    trackerEngine.refreshTracksForInstrument (instrument, patternData.getCurrentPattern());
            }

            // Auto-select the loaded instrument in the tracker and instrument panel
            trackerGrid->setCurrentInstrument (instrument);
            instrumentPanel->setSelectedInstrument (instrument);

            trackerGrid->repaint();
            updateToolbar();
            updateStatusBar();
            updateInstrumentPanel();
            fileBrowser->updateInstrumentSlots (trackerEngine.getSampler().getLoadedSamples());
            fileBrowser->advanceToNextEmptySlot();
            markDirty();
        }
    };
    fileBrowser->onPreviewFile = [this] (const juce::File& file)
    {
        trackerEngine.previewAudioFile (file);
    };
    fileBrowser->onPreviewInstrument = [this] (int instrumentIndex)
    {
        trackerEngine.previewInstrument (instrumentIndex);
    };
    fileBrowser->onStopPreview = [this]()
    {
        trackerEngine.stopPreview();
    };

    // Create send effects component
    sendEffectsComponent = std::make_unique<SendEffectsComponent> (trackerLookAndFeel);
    addChildComponent (*sendEffectsComponent);

    sendEffectsComponent->setDelayParams (trackerEngine.getDelayParams());
    sendEffectsComponent->setReverbParams (trackerEngine.getReverbParams());

    sendEffectsComponent->onParamsChanged = [this] (const DelayParams& dp, const ReverbParams& rp)
    {
        trackerEngine.setDelayParams (dp);
        trackerEngine.setReverbParams (rp);
        markDirty();
    };

    // Create the grid
    trackerGrid = std::make_unique<TrackerGrid> (patternData, trackerLookAndFeel, trackLayout);
    trackerGrid->setRowsPerBeat (trackerEngine.getRowsPerBeat());
    trackerGrid->setUndoManager (&undoManager);
    addAndMakeVisible (*trackerGrid);

    // Note preview callback
    trackerGrid->onNoteEntered = [this] (int note, int instrument)
    {
        int previewTrack = trackerGrid->isCursorInMasterLane() ? 0 : trackerGrid->getCursorTrack();
        trackerEngine.previewNote (previewTrack, instrument, note);
        markDirty();
    };

    // Cursor moved callback
    trackerGrid->onCursorMoved = [this]
    {
        updateStatusBar();
        updateToolbar();
        instrumentPanel->setSelectedInstrument (trackerGrid->getCurrentInstrument());
    };

    // Pattern data changed — re-sync during playback
    trackerGrid->onPatternDataChanged = [this]
    {
        if (trackerEngine.isPlaying())
        {
            if (songMode)
                syncArrangementToEdit();
            else
                trackerEngine.syncPatternToEdit (patternData.getCurrentPattern(), getReleaseModes());
        }
        markDirty();
        commandManager.commandStatusChanged();
    };

    // Track header right-click
    trackerGrid->onTrackHeaderRightClick = [this] (int track, juce::Point<int> screenPos)
    {
        showTrackHeaderMenu (track, screenPos);
    };

    // Grid right-click (context menu on cells)
    trackerGrid->onGridRightClick = [this] (int track, juce::Point<int> screenPos)
    {
        showTrackHeaderMenu (track, screenPos);
    };

    // Double-click on track header to rename
    trackerGrid->onTrackHeaderDoubleClick = [this] (int track, juce::Point<int> /*screenPos*/)
    {
        showRenameTrackDialog (track);
    };

    // Header drag-drop reorder complete
    trackerGrid->onTrackHeaderDragged = [this] (int, int)
    {
        markDirty();
    };

    // File drop on track
    trackerGrid->onFileDroppedOnTrack = [this] (int track, const juce::File& file)
    {
        int inst = resolveInstrumentForTrackDrop (track);
        trackerGrid->setCurrentInstrument (inst);
        instrumentPanel->setSelectedInstrument (inst);

        auto error = trackerEngine.loadSampleForInstrument (inst, file);
        if (error.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Load Error", error);
        else
        {
            if (trackerEngine.isPlaying())
            {
                if (songMode)
                    syncArrangementToEdit();
                else
                    trackerEngine.refreshTracksForInstrument (inst, patternData.getCurrentPattern());
            }

            trackerGrid->repaint();
            updateToolbar();
            updateInstrumentPanel();
            markDirty();
        }
    };

    trackerGrid->onNoteModeToggled = [this] (int /*track*/)
    {
        markDirty();
        if (trackerEngine.isPlaying() && ! songMode)
        {
            trackerEngine.syncPatternToEdit (patternData.getCurrentPattern(), getReleaseModes());
        }
        else if (trackerEngine.isPlaying() && songMode)
        {
            syncArrangementToEdit();
        }
    };

    // Transport change callback
    trackerEngine.onTransportChanged = [this]
    {
        updateStatusBar();
        updateToolbar();
    };

    // Status bar
    addAndMakeVisible (statusLabel);
    statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xffcccccc));
    statusLabel.setFont (trackerLookAndFeel.getMonoFont (12.0f));

    addAndMakeVisible (octaveLabel);
    octaveLabel.setColour (juce::Label::textColourId, juce::Colour (0xffcccccc));
    octaveLabel.setFont (trackerLookAndFeel.getMonoFont (12.0f));

    addAndMakeVisible (bpmLabel);
    bpmLabel.setColour (juce::Label::textColourId, juce::Colour (0xffcccccc));
    bpmLabel.setFont (trackerLookAndFeel.getMonoFont (12.0f));

    updateStatusBar();
    updateToolbar();

    // Set up application command manager for Cmd shortcuts (macOS needs this)
    commandManager.registerAllCommandsForTarget (this);
    commandManager.setFirstCommandTarget (this);
    addKeyListener (commandManager.getKeyMappings());

    // Register as mac menu bar so Cmd+O goes through the native menu system
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (this);
   #endif

    // Playback cursor update timer
    startTimerHz (30);

    // Register as key listener on the grid, sample editor, file browser, mixer, and effects
    trackerGrid->addKeyListener (this);
    trackerGrid->addKeyListener (commandManager.getKeyMappings());
    sampleEditor->addKeyListener (this);
    sampleEditor->addKeyListener (commandManager.getKeyMappings());
    fileBrowser->addKeyListener (this);
    fileBrowser->addKeyListener (commandManager.getKeyMappings());
    mixerComponent->addKeyListener (this);
    mixerComponent->addKeyListener (commandManager.getKeyMappings());
    sendEffectsComponent->addKeyListener (this);
    sendEffectsComponent->addKeyListener (commandManager.getKeyMappings());
    instrumentPanel->addKeyListener (commandManager.getKeyMappings());
    arrangementComponent->addKeyListener (commandManager.getKeyMappings());

    setSize (1280, 720);
    setWantsKeyboardFocus (true);
    trackerGrid->grabKeyboardFocus();
}

MainComponent::~MainComponent()
{
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
   #endif
    arrangementComponent->removeKeyListener (commandManager.getKeyMappings());
    instrumentPanel->removeKeyListener (commandManager.getKeyMappings());
    sendEffectsComponent->removeKeyListener (commandManager.getKeyMappings());
    mixerComponent->removeKeyListener (this);
    mixerComponent->removeKeyListener (commandManager.getKeyMappings());
    sendEffectsComponent->removeKeyListener (this);
    fileBrowser->removeKeyListener (commandManager.getKeyMappings());
    fileBrowser->removeKeyListener (this);
    sampleEditor->removeKeyListener (commandManager.getKeyMappings());
    sampleEditor->removeKeyListener (this);
    trackerGrid->removeKeyListener (commandManager.getKeyMappings());
    trackerGrid->removeKeyListener (this);
    setLookAndFeel (nullptr);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (trackerLookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId));
}

void MainComponent::resized()
{
    auto r = getLocalBounds();

    // Tab bar at top
    tabBar->setBounds (r.removeFromTop (TabBarComponent::kTabBarHeight));

    // Toolbar below tab bar
    toolbar->setBounds (r.removeFromTop (ToolbarComponent::kToolbarHeight));
    auto toolbarBounds = toolbar->getBounds();
    constexpr int kPreviewSliderWidth = 96;
    constexpr int kPreviewLabelWidth = 56;
    int previewY = toolbarBounds.getY() + 8;
    int sliderRight = toolbarBounds.getRight() - 44; // keep clear of INS toggle
    previewVolumeSlider.setBounds (sliderRight - kPreviewSliderWidth, previewY, kPreviewSliderWidth, 20);
    previewVolumeLabel.setBounds (previewVolumeSlider.getX() - kPreviewLabelWidth - 4, previewY, kPreviewLabelWidth, 20);

    // Status bar at bottom
    auto statusBar = r.removeFromBottom (24);
    statusLabel.setBounds (statusBar.removeFromLeft (statusBar.getWidth() / 2));

    auto rightStatus = statusBar;
    octaveLabel.setBounds (rightStatus.removeFromLeft (rightStatus.getWidth() / 2));
    bpmLabel.setBounds (rightStatus);

    // Hide everything first
    arrangementComponent->setVisible (false);
    instrumentPanel->setVisible (false);
    trackerGrid->setVisible (false);
    sampleEditor->setVisible (false);
    fileBrowser->setVisible (false);
    mixerComponent->setVisible (false);
    sendEffectsComponent->setVisible (false);

    switch (activeTab)
    {
        case Tab::Tracker:
        {
            // Arrangement panel (left side)
            if (arrangementVisible)
            {
                arrangementComponent->setBounds (r.removeFromLeft (ArrangementComponent::kPanelWidth));
                arrangementComponent->setVisible (true);
            }

            // Instrument panel (right side)
            if (instrumentPanelVisible)
            {
                instrumentPanel->setBounds (r.removeFromRight (InstrumentPanel::kPanelWidth));
                instrumentPanel->setVisible (true);
            }

            // Grid fills the rest
            trackerGrid->setBounds (r);
            trackerGrid->setVisible (true);
            break;
        }
        case Tab::InstrumentEdit:
        {
            // Instrument panel (right side, optional)
            if (instrumentPanelVisible)
            {
                instrumentPanel->setBounds (r.removeFromRight (InstrumentPanel::kPanelWidth));
                instrumentPanel->setVisible (true);
            }

            sampleEditor->setDisplayMode (SampleEditorComponent::DisplayMode::InstrumentEdit);
            sampleEditor->setBounds (r);
            sampleEditor->setVisible (true);
            break;
        }
        case Tab::InstrumentType:
        {
            // Instrument panel (right side, optional)
            if (instrumentPanelVisible)
            {
                instrumentPanel->setBounds (r.removeFromRight (InstrumentPanel::kPanelWidth));
                instrumentPanel->setVisible (true);
            }

            sampleEditor->setDisplayMode (SampleEditorComponent::DisplayMode::InstrumentType);
            sampleEditor->setBounds (r);
            sampleEditor->setVisible (true);
            break;
        }
        case Tab::Mixer:
        {
            mixerComponent->setBounds (r);
            mixerComponent->setVisible (true);
            break;
        }
        case Tab::Effects:
        {
            sendEffectsComponent->setBounds (r);
            sendEffectsComponent->setVisible (true);
            break;
        }
        case Tab::Browser:
        {
            fileBrowser->setBounds (r);
            fileBrowser->setVisible (true);
            break;
        }
    }
}

bool MainComponent::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    auto keyCode = key.getKeyCode();
    bool cmd = key.getModifiers().isCommandDown();
    bool shift = key.getModifiers().isShiftDown();
    auto textChar = key.getTextCharacter();
    bool alt = key.getModifiers().isAltDown();

    // Option+Left/Right: cycle through top-level tabs
    if (alt && ! cmd && keyCode == juce::KeyPress::rightKey)
    {
        cycleTab (1);
        return true;
    }
    if (alt && ! cmd && keyCode == juce::KeyPress::leftKey)
    {
        cycleTab (-1);
        return true;
    }

    // F1-F6: switch tabs — but on the Tracker tab with no modifiers, let F-keys
    // pass through to TrackerGrid (which uses F1-F8 for octave setting)
    bool onTrackerTab = (tabBar != nullptr && tabBar->getActiveTab() == Tab::Tracker);
    if (!onTrackerTab || shift || cmd || alt)
    {
        if (keyCode == juce::KeyPress::F1Key) { switchToTab (Tab::Tracker); return true; }
        if (keyCode == juce::KeyPress::F2Key) { switchToTab (Tab::InstrumentEdit); return true; }
        if (keyCode == juce::KeyPress::F3Key) { switchToTab (Tab::InstrumentType); return true; }
        if (keyCode == juce::KeyPress::F4Key) { switchToTab (Tab::Mixer); return true; }
        if (keyCode == juce::KeyPress::F5Key) { switchToTab (Tab::Effects); return true; }
        if (keyCode == juce::KeyPress::F6Key) { switchToTab (Tab::Browser); return true; }
    }

    // Escape in non-Tracker tabs: return to Tracker
    if (keyCode == juce::KeyPress::escapeKey && activeTab != Tab::Tracker)
    {
        switchToTab (Tab::Tracker);
        return true;
    }

    // When on non-Tracker tabs, only handle global shortcuts (Space, Cmd+S, etc.)
    if (activeTab != Tab::Tracker)
    {
        // Space: toggle play/stop (global) -- but not when sample editor has focus (it uses Space for preview)
        if (keyCode == juce::KeyPress::spaceKey
            && activeTab != Tab::InstrumentEdit && activeTab != Tab::InstrumentType)
        {
            if (! trackerEngine.isPlaying())
            {
                if (songMode)
                    syncArrangementToEdit();
                else
                    trackerEngine.syncPatternToEdit (patternData.getCurrentPattern(), getReleaseModes());
            }
            trackerEngine.togglePlayStop();
            updateStatusBar();
            updateToolbar();
            return true;
        }
        // Let Cmd shortcuts fall through to ApplicationCommandTarget
        return false;
    }

    // Space: toggle play/stop
    if (keyCode == juce::KeyPress::spaceKey)
    {
        if (! trackerEngine.isPlaying())
        {
            if (songMode)
                syncArrangementToEdit();
            else
                trackerEngine.syncPatternToEdit (patternData.getCurrentPattern(), getReleaseModes());
        }

        trackerEngine.togglePlayStop();
        updateStatusBar();
        updateToolbar();
        return true;
    }

    // Cmd+Shift+Right/Left: next/prev pattern (Cmd+Left/Right is now octave transpose)
    if (cmd && shift && keyCode == juce::KeyPress::rightKey)
    {
        if (alt)
        {
            // Cmd+Shift+Alt+Right: add new pattern and switch to it
            patternData.addPattern (patternData.getCurrentPattern().numRows);
            switchToPattern (patternData.getNumPatterns() - 1);
        }
        else
        {
            switchToPattern (patternData.getCurrentPatternIndex() + 1);
        }
        return true;
    }
    if (cmd && shift && keyCode == juce::KeyPress::leftKey)
    {
        switchToPattern (patternData.getCurrentPatternIndex() - 1);
        return true;
    }

    // Cmd+M: toggle mute
    if (cmd && ! shift && textChar == 'm')
    {
        int track = trackerGrid->getCursorTrack();
        if (track >= kNumTracks)
            return true;
        auto* t = trackerEngine.getTrack (track);
        if (t != nullptr)
        {
            t->setMute (! t->isMuted (false));
            updateMuteSoloState();
            markDirty();
        }
        return true;
    }

    // Cmd+Shift+M: toggle solo
    if (cmd && shift && textChar == 'M')
    {
        int track = trackerGrid->getCursorTrack();
        if (track >= kNumTracks)
            return true;
        auto* t = trackerEngine.getTrack (track);
        if (t != nullptr)
        {
            t->setSolo (! t->isSolo (false));
            updateMuteSoloState();
            markDirty();
        }
        return true;
    }

    // Cmd+Up/Down: change instrument
    if (cmd && keyCode == juce::KeyPress::upKey)
    {
        int inst = juce::jlimit (0, 255, trackerGrid->getCurrentInstrument() - 1);
        trackerGrid->setCurrentInstrument (inst);
        updateStatusBar();
        updateToolbar();
        instrumentPanel->setSelectedInstrument (inst);
        return true;
    }
    if (cmd && keyCode == juce::KeyPress::downKey)
    {
        int inst = juce::jlimit (0, 255, trackerGrid->getCurrentInstrument() + 1);
        trackerGrid->setCurrentInstrument (inst);
        updateStatusBar();
        updateToolbar();
        instrumentPanel->setSelectedInstrument (inst);
        return true;
    }

    // Cmd+Shift+Up/Down: change keyboard octave
    if (cmd && shift && keyCode == juce::KeyPress::upKey)
    {
        int oct = juce::jmin (9, trackerGrid->getOctave() + 1);
        trackerGrid->setOctave (oct);
        sampleEditor->setOctave (oct);
        updateStatusBar();
        updateToolbar();
        return true;
    }
    if (cmd && shift && keyCode == juce::KeyPress::downKey)
    {
        int oct = juce::jmax (0, trackerGrid->getOctave() - 1);
        trackerGrid->setOctave (oct);
        sampleEditor->setOctave (oct);
        updateStatusBar();
        updateToolbar();
        return true;
    }

    // Cmd+1 through Cmd+8: set octave 0-7 (MacBook-friendly alternative to F1-F8)
    if (cmd && ! shift && (keyCode >= '1' && keyCode <= '8'))
    {
        trackerGrid->setOctave (keyCode - '1');
        sampleEditor->setOctave (keyCode - '1');
        updateStatusBar();
        updateToolbar();
        return true;
    }

    // Cmd+[ / Cmd+]: decrease/increase BPM (MacBook-friendly alternative to F9/F10)
    // NOTE: use keyCode, not textChar — macOS zeroes textChar when Cmd is held for punctuation keys
    if (cmd && ! shift && keyCode == '[')
    {
        trackerEngine.setBpm (juce::jlimit (20.0, 999.0, trackerEngine.getBpm() - 1.0));
        updateStatusBar();
        updateToolbar();
        markDirty();
        return true;
    }
    if (cmd && ! shift && keyCode == ']')
    {
        trackerEngine.setBpm (juce::jlimit (20.0, 999.0, trackerEngine.getBpm() + 1.0));
        updateStatusBar();
        updateToolbar();
        markDirty();
        return true;
    }

    // Cmd+- / Cmd+=: decrease/increase edit step (MacBook-friendly alternative to F11/F12)
    if (cmd && ! shift && keyCode == '-')
    {
        trackerGrid->setEditStep (juce::jmax (0, trackerGrid->getEditStep() - 1));
        updateStatusBar();
        updateToolbar();
        return true;
    }
    if (cmd && ! shift && keyCode == '=')
    {
        trackerGrid->setEditStep (juce::jmin (16, trackerGrid->getEditStep() + 1));
        updateStatusBar();
        updateToolbar();
        return true;
    }

    // F-key alternatives (still work if user holds Fn)
    if (keyCode == juce::KeyPress::F7Key)  { toggleArrangementPanel(); return true; }
    if (keyCode == juce::KeyPress::F8Key)  { toggleSongMode(); return true; }

    if (keyCode == juce::KeyPress::F9Key)
    {
        trackerEngine.setBpm (juce::jlimit (20.0, 999.0, trackerEngine.getBpm() - 1.0));
        updateStatusBar();
        updateToolbar();
        markDirty();
        return true;
    }
    if (keyCode == juce::KeyPress::F10Key)
    {
        trackerEngine.setBpm (juce::jlimit (20.0, 999.0, trackerEngine.getBpm() + 1.0));
        updateStatusBar();
        updateToolbar();
        markDirty();
        return true;
    }
    if (keyCode == juce::KeyPress::F11Key)
    {
        trackerGrid->setEditStep (juce::jmax (0, trackerGrid->getEditStep() - 1));
        updateStatusBar(); updateToolbar(); return true;
    }
    if (keyCode == juce::KeyPress::F12Key)
    {
        trackerGrid->setEditStep (juce::jmin (16, trackerGrid->getEditStep() + 1));
        updateStatusBar(); updateToolbar(); return true;
    }

    return false;
}

//==============================================================================
// ApplicationCommandTarget
//==============================================================================

void MainComponent::getAllCommands (juce::Array<juce::CommandID>& commands)
{
    commands.add (loadSample);
    commands.add (nextPattern);
    commands.add (prevPattern);
    commands.add (addPattern);
    commands.add (muteTrack);
    commands.add (soloTrack);
    commands.add (cmdCopy);
    commands.add (cmdPaste);
    commands.add (cmdCut);
    commands.add (cmdUndo);
    commands.add (cmdRedo);
    commands.add (cmdNewProject);
    commands.add (cmdOpen);
    commands.add (cmdSave);
    commands.add (cmdSaveAs);
    commands.add (cmdShowHelp);
    commands.add (cmdToggleArrangement);
    commands.add (cmdToggleSongMode);
    commands.add (cmdToggleInstrumentPanel);
    commands.add (cmdToggleMetronome);
}

void MainComponent::getCommandInfo (juce::CommandID commandID, juce::ApplicationCommandInfo& result)
{
    switch (commandID)
    {
        case loadSample:
            result.setInfo ("Load Sample", "Load a sample for the current track", "File", 0);
            result.addDefaultKeypress ('O', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            break;
        case nextPattern:
            result.setInfo ("Next Pattern", "Switch to next pattern", "Pattern", 0);
            break;
        case prevPattern:
            result.setInfo ("Previous Pattern", "Switch to previous pattern", "Pattern", 0);
            break;
        case addPattern:
            result.setInfo ("Add Pattern", "Add a new pattern", "Pattern", 0);
            break;
        case muteTrack:
            result.setInfo ("Mute Track", "Toggle mute on current track", "Track", 0);
            break;
        case soloTrack:
            result.setInfo ("Solo Track", "Toggle solo on current track", "Track", 0);
            break;
        case cmdCopy:
            result.setInfo ("Copy", "Copy selection", "Edit", 0);
            result.addDefaultKeypress ('C', juce::ModifierKeys::commandModifier);
            break;
        case cmdPaste:
            result.setInfo ("Paste", "Paste at cursor", "Edit", 0);
            result.addDefaultKeypress ('V', juce::ModifierKeys::commandModifier);
            break;
        case cmdCut:
            result.setInfo ("Cut", "Cut selection", "Edit", 0);
            result.addDefaultKeypress ('X', juce::ModifierKeys::commandModifier);
            break;
        case cmdUndo:
            result.setInfo ("Undo", "Undo last action", "Edit", 0);
            result.addDefaultKeypress ('Z', juce::ModifierKeys::commandModifier);
            result.setActive (undoManager.canUndo());
            break;
        case cmdRedo:
            result.setInfo ("Redo", "Redo last undone action", "Edit", 0);
            result.addDefaultKeypress ('Z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            result.addDefaultKeypress ('Y', juce::ModifierKeys::commandModifier);
            result.setActive (undoManager.canRedo());
            break;
        case cmdNewProject:
            result.setInfo ("New Project", "Create a new project", "File", 0);
            result.addDefaultKeypress ('N', juce::ModifierKeys::commandModifier);
            break;
        case cmdOpen:
            result.setInfo ("Open Project...", "Open a project file", "File", 0);
            result.addDefaultKeypress ('O', juce::ModifierKeys::commandModifier);
            break;
        case cmdSave:
            result.setInfo ("Save", "Save current project", "File", 0);
            result.addDefaultKeypress ('S', juce::ModifierKeys::commandModifier);
            result.setActive (true);
            break;
        case cmdSaveAs:
            result.setInfo ("Save As...", "Save project to a new file", "File", 0);
            result.addDefaultKeypress ('S', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            result.setActive (true);
            break;
        case cmdShowHelp:
            result.setInfo ("Keyboard Shortcuts", "Show all keyboard shortcuts", "Help", 0);
            result.addDefaultKeypress ('/', juce::ModifierKeys::commandModifier);
            break;
        case cmdToggleArrangement:
            result.setInfo ("Toggle Arrangement", "Show/hide arrangement panel", "View", 0);
            result.addDefaultKeypress ('A', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            break;
        case cmdToggleSongMode:
            result.setInfo ("Toggle Song Mode", "Switch between PAT and SONG playback", "View", 0);
            result.addDefaultKeypress ('P', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            break;
        case cmdToggleInstrumentPanel:
            result.setInfo ("Toggle Instruments", "Show/hide instrument panel", "View", 0);
            result.addDefaultKeypress ('I', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            break;
        case cmdToggleMetronome:
            result.setInfo ("Toggle Metronome", "Toggle the metronome on/off", "View", 0);
            result.addDefaultKeypress ('K', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            break;
        default: break;
    }
}

bool MainComponent::perform (const InvocationInfo& info)
{
    switch (info.commandID)
    {
        case loadSample:
            loadSampleForCurrentTrack();
            return true;
        case nextPattern:
            switchToPattern (patternData.getCurrentPatternIndex() + 1);
            return true;
        case prevPattern:
            switchToPattern (patternData.getCurrentPatternIndex() - 1);
            return true;
        case addPattern:
            patternData.addPattern (patternData.getCurrentPattern().numRows);
            switchToPattern (patternData.getNumPatterns() - 1);
            markDirty();
            return true;
        case muteTrack:
        {
            int track = trackerGrid->getCursorTrack();
            if (track >= kNumTracks)
                return true;
            auto* t = trackerEngine.getTrack (track);
            if (t) { t->setMute (! t->isMuted (false)); updateMuteSoloState(); markDirty(); }
            return true;
        }
        case soloTrack:
        {
            int track = trackerGrid->getCursorTrack();
            if (track >= kNumTracks)
                return true;
            auto* t = trackerEngine.getTrack (track);
            if (t) { t->setSolo (! t->isSolo (false)); updateMuteSoloState(); markDirty(); }
            return true;
        }
        case cmdCopy:
            doCopy();
            return true;
        case cmdPaste:
            doPaste();
            return true;
        case cmdCut:
            doCut();
            return true;
        case cmdUndo:
            if (undoManager.undo())
            {
                if (trackerGrid->onPatternDataChanged)
                    trackerGrid->onPatternDataChanged();
                trackerGrid->repaint();
                commandManager.commandStatusChanged();
            }
            return true;
        case cmdRedo:
            if (undoManager.redo())
            {
                if (trackerGrid->onPatternDataChanged)
                    trackerGrid->onPatternDataChanged();
                trackerGrid->repaint();
                commandManager.commandStatusChanged();
            }
            return true;
        case cmdNewProject:
            newProject();
            return true;
        case cmdOpen:
            openProject();
            return true;
        case cmdSave:
            saveProject();
            return true;
        case cmdSaveAs:
            saveProjectAs();
            return true;
        case cmdShowHelp:
            showHelpOverlay();
            return true;
        case cmdToggleArrangement:
            toggleArrangementPanel();
            return true;
        case cmdToggleSongMode:
            toggleSongMode();
            return true;
        case cmdToggleInstrumentPanel:
            instrumentPanelVisible = ! instrumentPanelVisible;
            toolbar->setInstrumentPanelVisible (instrumentPanelVisible);
            resized();
            return true;
        case cmdToggleMetronome:
        {
            bool enabled = ! trackerEngine.isMetronomeEnabled();
            trackerEngine.setMetronomeEnabled (enabled);
            toolbar->setMetronomeEnabled (enabled);
            return true;
        }
        default: return false;
    }
}

//==============================================================================
// MenuBarModel
//==============================================================================

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Edit", "View", "Help" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int menuIndex, const juce::String&)
{
    juce::PopupMenu menu;
    if (menuIndex == 0)
    {
        menu.addCommandItem (&commandManager, cmdNewProject);
        menu.addCommandItem (&commandManager, cmdOpen);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, cmdSave);
        menu.addCommandItem (&commandManager, cmdSaveAs);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, loadSample);
    }
    else if (menuIndex == 1)
    {
        menu.addCommandItem (&commandManager, cmdUndo);
        menu.addCommandItem (&commandManager, cmdRedo);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, cmdCopy);
        menu.addCommandItem (&commandManager, cmdCut);
        menu.addCommandItem (&commandManager, cmdPaste);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, muteTrack);
        menu.addCommandItem (&commandManager, soloTrack);
    }
    else if (menuIndex == 2)
    {
        menu.addCommandItem (&commandManager, cmdToggleArrangement);
        menu.addCommandItem (&commandManager, cmdToggleInstrumentPanel);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, cmdToggleSongMode);
        menu.addCommandItem (&commandManager, cmdToggleMetronome);
    }
    else if (menuIndex == 3)
    {
        menu.addCommandItem (&commandManager, cmdShowHelp);
    }
    return menu;
}

//==============================================================================

void MainComponent::timerCallback()
{
    if (trackerEngine.isPlaying())
    {
        int playRow = -1;

        if (songMode && arrangement.getNumEntries() > 0)
        {
            // Song mode: compute which pattern/row from the beat position
            double beatPos = trackerEngine.getPlaybackBeatPosition();
            auto info = getArrangementPlaybackPosition (beatPos);

            if (info.entryIndex >= 0)
            {
                // Auto-switch pattern if needed
                if (info.patternIndex != patternData.getCurrentPatternIndex())
                    switchToPattern (info.patternIndex);

                // Highlight current entry in arrangement panel
                if (arrangementVisible)
                    arrangementComponent->setPlayingEntry (info.entryIndex);

                playRow = info.rowInPattern;
            }
        }
        else
        {
            // Pattern mode: simple row from beat position
            playRow = trackerEngine.getPlaybackRow (patternData.getCurrentPattern().numRows);
        }

        trackerGrid->setPlaybackRow (playRow);
        trackerGrid->setPlaying (true);

        // Follow mode
        if (followMode != FollowMode::Off && playRow >= 0)
        {
            int visibleRows = trackerGrid->getVisibleRowCount();

            if (followMode == FollowMode::Center)
            {
                // Keep playback row centered
                trackerGrid->setScrollOffset (playRow - visibleRows / 2);
            }
            else if (followMode == FollowMode::Page)
            {
                // Page-style: scroll when playback is near the bottom
                int scrollOff = trackerGrid->getScrollOffset();
                int margin = juce::jmax (4, visibleRows / 6);
                if (playRow >= scrollOff + visibleRows - margin)
                    trackerGrid->setScrollOffset (playRow - margin);
                else if (playRow < scrollOff)
                    trackerGrid->setScrollOffset (playRow - margin);
            }
        }
    }
    else
    {
        trackerGrid->setPlaying (false);
        if (arrangementVisible)
            arrangementComponent->setPlayingEntry (-1);
    }
}

MainComponent::ArrangementPlaybackInfo MainComponent::getArrangementPlaybackPosition (double beatPos) const
{
    ArrangementPlaybackInfo info;
    if (beatPos < 0.0) return info;

    int rpb = trackerEngine.getRowsPerBeat();
    double accBeats = 0.0;

    for (int i = 0; i < arrangement.getNumEntries(); ++i)
    {
        auto& entry = arrangement.getEntry (i);
        if (entry.patternIndex < 0 || entry.patternIndex >= patternData.getNumPatterns())
            continue;

        auto& pat = patternData.getPattern (entry.patternIndex);
        double patBeats = static_cast<double> (pat.numRows) / static_cast<double> (rpb);
        double entryBeats = patBeats * entry.repeats;

        if (beatPos < accBeats + entryBeats)
        {
            // We're in this entry
            info.entryIndex = i;
            info.patternIndex = entry.patternIndex;
            double beatsIntoEntry = beatPos - accBeats;
            // Handle repeats: get position within a single pattern
            double beatsIntoPattern = std::fmod (beatsIntoEntry, patBeats);
            info.rowInPattern = static_cast<int> (beatsIntoPattern * static_cast<double> (rpb));
            info.rowInPattern = juce::jlimit (0, pat.numRows - 1, info.rowInPattern);
            return info;
        }

        accBeats += entryBeats;
    }

    return info; // past the end
}

void MainComponent::updateStatusBar()
{
    auto playState = trackerEngine.isPlaying() ? "PLAYING" : "STOPPED";
    auto row = juce::String::formatted ("%02X", trackerGrid->getCursorRow());
    auto track = trackerGrid->isCursorInMasterLane()
                     ? juce::String ("MASTER")
                     : juce::String::formatted ("%02d", trackerGrid->getCursorTrack() + 1);

    const char* subColNames[] = { "Note", "Inst", "Vol", "FX" };
    auto subCol = subColNames[static_cast<int> (trackerGrid->getCursorSubColumn())];

    statusLabel.setText (juce::String (playState) + "  Row:" + row + "  Track:" + track
                             + " [" + subCol + "]"
                             + "  Step:" + juce::String (trackerGrid->getEditStep()),
                         juce::dontSendNotification);

    octaveLabel.setText ("Oct:" + juce::String (trackerGrid->getOctave()),
                         juce::dontSendNotification);

    bpmLabel.setText ("BPM:" + juce::String (trackerEngine.getBpm(), 1),
                      juce::dontSendNotification);
}

void MainComponent::updateToolbar()
{
    auto& pat = patternData.getCurrentPattern();
    toolbar->setPatternInfo (patternData.getCurrentPatternIndex() + 1, patternData.getNumPatterns(), pat.name);
    toolbar->setPatternLength (pat.numRows);
    toolbar->setInstrument (trackerGrid->getCurrentInstrument());
    toolbar->setOctave (trackerGrid->getOctave());
    toolbar->setEditStep (trackerGrid->getEditStep());
    toolbar->setBpm (trackerEngine.getBpm());
    toolbar->setRowsPerBeat (trackerEngine.getRowsPerBeat());
    toolbar->setPlayState (trackerEngine.isPlaying());
    toolbar->setPlaybackMode (songMode);

    // Show sample name for current instrument
    auto sampleFile = trackerEngine.getSampler().getSampleFile (trackerGrid->getCurrentInstrument());
    toolbar->setSampleName (sampleFile.existsAsFile() ? sampleFile.getFileNameWithoutExtension() : "");
}

void MainComponent::loadSampleForCurrentTrack()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        "Load Sample",
        juce::File::getSpecialLocation (juce::File::userHomeDirectory),
        "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (file.existsAsFile())
                              {
                                  int inst = trackerGrid->getCurrentInstrument();
                                  auto error = trackerEngine.loadSampleForInstrument (inst, file);
                                  if (error.isNotEmpty())
                                      juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                                              "Load Error", error);
                                  else
                                  {
                                      if (trackerEngine.isPlaying())
                                      {
                                          if (songMode)
                                              syncArrangementToEdit();
                                          else
                                              trackerEngine.refreshTracksForInstrument (inst, patternData.getCurrentPattern());
                                      }

                                      trackerGrid->repaint();
                                      updateToolbar();
                                      updateInstrumentPanel();
                                      fileBrowser->updateInstrumentSlots (trackerEngine.getSampler().getLoadedSamples());
                                      markDirty();
                                  }
                              }
                          });
}

void MainComponent::switchToPattern (int index)
{
    index = juce::jlimit (0, patternData.getNumPatterns() - 1, index);
    patternData.setCurrentPattern (index);

    // Clear any selection from the previous pattern
    trackerGrid->clearSelection();

    // Clamp cursor row to new pattern length
    auto& pat = patternData.getCurrentPattern();
    trackerGrid->setCursorPosition (
        juce::jmin (trackerGrid->getCursorRow(), pat.numRows - 1),
        trackerGrid->getCursorTrack());

    // Re-sync edit if playing in pattern mode (not song mode)
    if (trackerEngine.isPlaying() && ! songMode)
        trackerEngine.syncPatternToEdit (pat, getReleaseModes());

    trackerGrid->repaint();
    updateTrackSampleMarkers();
    updateStatusBar();
    updateToolbar();
}

void MainComponent::showPatternLengthEditor()
{
    auto* aw = new juce::AlertWindow ("Pattern Length", "Enter new pattern length (1-256):", juce::AlertWindow::NoIcon);
    aw->addTextEditor ("length", juce::String (patternData.getCurrentPattern().numRows));
    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw] (int result)
    {
        if (result == 1)
        {
            int newLen = aw->getTextEditorContents ("length").getIntValue();
            newLen = juce::jlimit (1, 256, newLen);
            auto& pat = patternData.getCurrentPattern();
            pat.resize (newLen);
            trackerGrid->setCursorPosition (
                juce::jmin (trackerGrid->getCursorRow(), newLen - 1),
                trackerGrid->getCursorTrack());

            // Re-sync edit while playing.
            if (trackerEngine.isPlaying())
            {
                if (songMode)
                    syncArrangementToEdit();
                else
                {
                    trackerEngine.syncPatternToEdit (pat, getReleaseModes());
                    trackerEngine.updateLoopRangeForPatternLength (pat.numRows);
                }
            }

            trackerGrid->repaint();
            updateToolbar();
            markDirty();
        }
        delete aw;
    }), true);
}

void MainComponent::showPatternNameEditor()
{
    auto& pat = patternData.getCurrentPattern();
    auto* aw = new juce::AlertWindow ("Pattern Name", "Enter a name for this pattern:", juce::AlertWindow::NoIcon);
    aw->addTextEditor ("name", pat.name);
    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw] (int result)
    {
        if (result == 1)
        {
            patternData.getCurrentPattern().name = aw->getTextEditorContents ("name");
            updateToolbar();
            markDirty();
        }
        delete aw;
    }), true);
}

void MainComponent::showTrackHeaderMenu (int track, juce::Point<int> screenPos)
{
    juce::PopupMenu menu;

    auto* t = trackerEngine.getTrack (track);
    if (t != nullptr)
    {
        bool muted = t->isMuted (false);
        bool soloed = t->isSolo (false);

        menu.addItem (1, muted ? "Unmute" : "Mute");
        menu.addItem (2, soloed ? "Unsolo" : "Solo");
        menu.addSeparator();
    }
    menu.addItem (3, "Load Sample...");
    menu.addItem (4, "Rename Track...");
    menu.addSeparator();

    // Selection bounds are in visual space; get visual range
    int rangeStart, rangeEnd;
    if (trackerGrid->hasSelection)
    {
        int minRow, maxRow, minViTrack, maxViTrack;
        trackerGrid->getSelectionBounds (minRow, maxRow, minViTrack, maxViTrack);
        rangeStart = minViTrack;
        rangeEnd = maxViTrack;
    }
    else
    {
        rangeStart = trackLayout.physicalToVisual (track);
        rangeEnd = rangeStart;
    }

    menu.addItem (10, "Move Track Left", rangeStart > 0);
    menu.addItem (11, "Move Track Right", rangeEnd < kNumTracks - 1);

    // Group selected tracks (if selection spans multiple tracks)
    if (trackerGrid->hasSelection)
    {
        int minRow, maxRow, minTrack, maxTrack;
        trackerGrid->getSelectionBounds (minRow, maxRow, minTrack, maxTrack);
        if (minTrack != maxTrack)
            menu.addItem (12, "Group Selected Tracks...");
    }

    // FX lanes
    menu.addSeparator();
    int fxLanes = trackLayout.getTrackFxLaneCount (track);
    menu.addItem (20, "Add FX Lane (" + juce::String (fxLanes) + " -> " + juce::String (fxLanes + 1) + ")", fxLanes < 8);
    menu.addItem (21, "Remove FX Lane (" + juce::String (fxLanes) + " -> " + juce::String (fxLanes - 1) + ")", fxLanes > 1);

    int groupIdx = trackLayout.getGroupForTrack (track);
    if (groupIdx >= 0)
    {
        menu.addSeparator();
        menu.addItem (13, "Remove from Group");
        menu.addItem (14, "Dissolve Group");
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }),
                        [this, track, t, rangeStart, rangeEnd, groupIdx] (int result)
                        {
                            if (result == 1 && t)
                            {
                                t->setMute (! t->isMuted (false));
                                updateMuteSoloState();
                                markDirty();
                            }
                            else if (result == 2 && t)
                            {
                                t->setSolo (! t->isSolo (false));
                                updateMuteSoloState();
                                markDirty();
                            }
                            else if (result == 3)
                            {
                                trackerGrid->setCursorPosition (trackerGrid->getCursorRow(), track);
                                loadSampleForCurrentTrack();
                            }
                            else if (result == 4)
                            {
                                showRenameTrackDialog (track);
                            }
                            else if (result == 10)
                            {
                                performUndoableTrackLayoutChange ([this, rangeStart, rangeEnd]
                                {
                                    trackLayout.moveVisualRange (rangeStart, rangeEnd, -1);
                                });
                            }
                            else if (result == 11)
                            {
                                performUndoableTrackLayoutChange ([this, rangeStart, rangeEnd]
                                {
                                    trackLayout.moveVisualRange (rangeStart, rangeEnd, +1);
                                });
                            }
                            else if (result == 12)
                            {
                                // Group selected tracks — prompt for name
                                auto* aw = new juce::AlertWindow ("Group Tracks", "Enter a name for this group:", juce::AlertWindow::NoIcon);
                                aw->addTextEditor ("name", "Group");
                                aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
                                aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

                                aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw, rangeStart, rangeEnd] (int res)
                                {
                                    if (res == 1)
                                    {
                                        auto name = aw->getTextEditorContents ("name");
                                        if (name.isEmpty()) name = "Group";
                                        performUndoableTrackLayoutChange ([this, name, rangeStart, rangeEnd]
                                        {
                                            trackLayout.createGroup (name, rangeStart, rangeEnd);
                                        });
                                    }
                                    delete aw;
                                }), true);
                            }
                            else if (result == 13 && groupIdx >= 0)
                            {
                                performUndoableTrackLayoutChange ([this, track]
                                {
                                    int currentGroupIdx = trackLayout.getGroupForTrack (track);
                                    if (currentGroupIdx < 0 || currentGroupIdx >= trackLayout.getNumGroups())
                                        return;

                                    // Remove this track from its group.
                                    auto& group = trackLayout.getGroup (currentGroupIdx);
                                    group.trackIndices.erase (
                                        std::remove (group.trackIndices.begin(), group.trackIndices.end(), track),
                                        group.trackIndices.end());
                                    if (group.trackIndices.empty())
                                        trackLayout.removeGroup (currentGroupIdx);
                                });
                            }
                            else if (result == 14 && groupIdx >= 0)
                            {
                                performUndoableTrackLayoutChange ([this, groupIdx]
                                {
                                    if (groupIdx >= 0 && groupIdx < trackLayout.getNumGroups())
                                        trackLayout.removeGroup (groupIdx);
                                });
                            }
                            else if (result == 20)
                            {
                                performUndoableTrackLayoutChange ([this, track]
                                {
                                    trackLayout.addFxLane (track);
                                });
                            }
                            else if (result == 21)
                            {
                                performUndoableTrackLayoutChange ([this, track]
                                {
                                    trackLayout.removeFxLane (track);
                                });
                            }
                        });
}

void MainComponent::showRenameTrackDialog (int track)
{
    auto currentName = trackLayout.getTrackName (track);
    auto defaultText = currentName.isNotEmpty() ? currentName
                                                 : juce::String::formatted ("T%02d", track + 1);

    auto* aw = new juce::AlertWindow ("Rename Track",
        "Enter a name for Track " + juce::String (track + 1) + ":",
        juce::AlertWindow::NoIcon);
    aw->addTextEditor ("name", defaultText);
    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw, track] (int result)
    {
        if (result == 1)
        {
            auto name = aw->getTextEditorContents ("name").trim();
            // If name matches default "T##" pattern, clear it
            if (name == juce::String::formatted ("T%02d", track + 1))
                name.clear();
            trackLayout.setTrackName (track, name);
            markDirty();
            trackerGrid->repaint();
        }
        delete aw;
    }), true);
}

void MainComponent::performUndoableTrackLayoutChange (const std::function<void()>& changeFn)
{
    auto before = trackLayout.createSnapshot();
    changeFn();
    auto after = trackLayout.createSnapshot();

    if (TrackLayout::snapshotsEqual (before, after))
    {
        trackerGrid->repaint();
        return;
    }

    undoManager.perform (new TrackLayoutEditAction (trackLayout, std::move (before), std::move (after)));

    trackerGrid->setCursorPosition (trackerGrid->getCursorRow(), trackerGrid->getCursorTrack());
    trackerGrid->repaint();
    if (trackerGrid->onPatternDataChanged)
        trackerGrid->onPatternDataChanged();
}

void MainComponent::markDirty()
{
    isDirty = true;
    updateWindowTitle();
}

void MainComponent::updateWindowTitle()
{
    auto name = currentProjectFile.existsAsFile() ? currentProjectFile.getFileName() : "Untitled";
    auto title = "Tracker Adjust - " + name + (isDirty ? " *" : "");
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
        window->setName (title);
}

bool MainComponent::confirmDiscardChanges()
{
    if (! isDirty) return true;
    return juce::AlertWindow::showOkCancelBox (juce::AlertWindow::QuestionIcon,
                                                "Unsaved Changes",
                                                "You have unsaved changes. Discard them?",
                                                "Discard", "Cancel");
}

void MainComponent::newProject()
{
    if (! confirmDiscardChanges()) return;

    trackerEngine.stop();
    patternData.clearAllPatterns();
    arrangement.clear();
    trackLayout.resetToDefault();
    trackerEngine.getSampler().clearLoadedSamples();
    arrangementComponent->setSelectedEntry (-1);
    trackerGrid->setCursorPosition (0, 0);
    trackerGrid->clearSelection();
    for (int i = 0; i < kNumTracks; ++i)
    {
        trackerGrid->trackMuted[static_cast<size_t> (i)] = false;
        trackerGrid->trackSoloed[static_cast<size_t> (i)] = false;
    }
    trackerEngine.setBpm (120.0);
    trackerEngine.setRowsPerBeat (4);
    trackerGrid->setRowsPerBeat (trackerEngine.getRowsPerBeat());
    trackerEngine.invalidateTrackInstruments();
    mixerState.reset();
    undoManager.clearUndoHistory();
    currentProjectFile = juce::File();
    isDirty = false;
    updateWindowTitle();
    updateStatusBar();
    updateToolbar();
    updateInstrumentPanel();
    fileBrowser->updateInstrumentSlots (trackerEngine.getSampler().getLoadedSamples());
    trackerGrid->repaint();
}

void MainComponent::openProject()
{
    if (! confirmDiscardChanges()) return;

    auto chooser = std::make_shared<juce::FileChooser> (
        "Open Project",
        juce::File::getSpecialLocation (juce::File::userHomeDirectory),
        "*.tkadj");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (! file.existsAsFile()) return;

                              trackerEngine.stop();
                              arrangement.clear();

                              double bpm = 120.0;
                              int rpb = 4;
                              std::map<int, juce::File> samples;
                              std::map<int, InstrumentParams> instParams;

                              DelayParams loadedDelay;
                              ReverbParams loadedReverb;
                              int loadedFollowMode = 0;
                              juce::String browserDir;
                              auto error = ProjectSerializer::loadFromFile (file, patternData, bpm, rpb, samples, instParams, arrangement, trackLayout, mixerState, loadedDelay, loadedReverb, &loadedFollowMode, &browserDir);
                              if (error.isNotEmpty())
                              {
                                  juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                                          "Load Error", error);
                                  return;
                              }

                              trackerEngine.setBpm (bpm);
                              trackerEngine.setRowsPerBeat (rpb);
                              trackerGrid->setRowsPerBeat (trackerEngine.getRowsPerBeat());

                              // Reload samples
                              trackerEngine.getSampler().clearLoadedSamples();

                              for (auto& [index, sampleFile] : samples)
                                  trackerEngine.loadSampleForInstrument (index, sampleFile);

                              // Restore instrument params
                              for (auto& [index, params] : instParams)
                                  trackerEngine.getSampler().setParams (index, params);

                              // Restore send effects params
                              trackerEngine.setDelayParams (loadedDelay);
                              trackerEngine.setReverbParams (loadedReverb);

                              // Restore follow mode
                              followMode = static_cast<FollowMode> (juce::jlimit (0, 2, loadedFollowMode));
                              toolbar->setFollowMode (static_cast<int> (followMode));

                              // Restore mute/solo from mixer state
                              for (int i = 0; i < kNumTracks; ++i)
                              {
                                  auto* t = trackerEngine.getTrack (i);
                                  if (t != nullptr)
                                  {
                                      t->setMute (mixerState.tracks[static_cast<size_t> (i)].muted);
                                      t->setSolo (mixerState.tracks[static_cast<size_t> (i)].soloed);
                                  }
                              }

                              // Refresh mixer plugins with loaded state
                              trackerEngine.refreshMixerPlugins();

                              // Invalidate track instrument cache so next sync re-loads correctly
                              trackerEngine.invalidateTrackInstruments();

                              arrangementComponent->setSelectedEntry (arrangement.getNumEntries() > 0 ? 0 : -1);

                              trackerGrid->setCursorPosition (0, 0);
                              trackerGrid->clearSelection();
                              undoManager.clearUndoHistory();
                              currentProjectFile = file;
                              isDirty = false;
                              updateWindowTitle();
                              updateStatusBar();
                              updateToolbar();
                              updateInstrumentPanel();
                              fileBrowser->updateInstrumentSlots (trackerEngine.getSampler().getLoadedSamples());

                              // Restore browser directory from project
                              if (browserDir.isNotEmpty())
                              {
                                  juce::File dir (browserDir);
                                  if (dir.isDirectory())
                                      fileBrowser->setCurrentDirectory (dir);
                              }

                              trackerGrid->repaint();
                          });
}

void MainComponent::saveProject()
{
    if (currentProjectFile.existsAsFile())
    {
        auto error = ProjectSerializer::saveToFile (currentProjectFile, patternData,
                                                     trackerEngine.getBpm(),
                                                     trackerEngine.getRowsPerBeat(),
                                                     trackerEngine.getSampler().getLoadedSamples(),
                                                     trackerEngine.getSampler().getAllParams(),
                                                     arrangement,
                                                     trackLayout,
                                                     mixerState,
                                                     trackerEngine.getDelayParams(),
                                                     trackerEngine.getReverbParams(),
                                                     static_cast<int> (followMode),
                                                     fileBrowser->getCurrentDirectory().getFullPathName());
        if (error.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Save Error", error);
        else
        {
            isDirty = false;
            updateWindowTitle();
        }
    }
    else
    {
        saveProjectAs();
    }
}

void MainComponent::saveProjectAs()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        "Save Project As",
        currentProjectFile.existsAsFile() ? currentProjectFile.getParentDirectory()
                                           : juce::File::getSpecialLocation (juce::File::userHomeDirectory),
        "*.tkadj");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (file == juce::File()) return;

                              auto f = file.withFileExtension ("tkadj");
                              auto error = ProjectSerializer::saveToFile (f, patternData,
                                                                          trackerEngine.getBpm(),
                                                                          trackerEngine.getRowsPerBeat(),
                                                                          trackerEngine.getSampler().getLoadedSamples(),
                                                                          trackerEngine.getSampler().getAllParams(),
                                                                          arrangement,
                                                                          trackLayout,
                                                                          mixerState,
                                                                          trackerEngine.getDelayParams(),
                                                                          trackerEngine.getReverbParams(),
                                                                          static_cast<int> (followMode),
                                                                          fileBrowser->getCurrentDirectory().getFullPathName());
                              if (error.isNotEmpty())
                                  juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                                          "Save Error", error);
                              else
                              {
                                  currentProjectFile = f;
                                  isDirty = false;
                                  updateWindowTitle();
                              }
                          });
}

void MainComponent::showHelpOverlay()
{
    struct HelpComponent : public juce::Component
    {
        struct Section
        {
            juce::String title;
            juce::StringArray shortcuts;
        };

        std::array<std::vector<Section>, 3> columns;

        HelpComponent()
        {
            // Column 1: Navigation + Notes
            columns[0] = {
                { "TRACKER NAVIGATION", {
                    "Arrow keys        Navigate grid",
                    "Tab / Shift+Tab   Cycle sub-columns",
                    "Fn+Up / Fn+Down   Page Up / Down",
                    "Fn+Left / Right   First / Last row",
                    "Mouse wheel       Scroll (Shift=horiz)" }},
                { "NOTE ENTRY", {
                    "Z-M, Q-U keys    Enter notes",
                    "Cmd+1 to Cmd+8   Set octave 0-7",
                    "= (note col)     Note-off (OFF)",
                    "- (note col)     Note-kill (KILL)",
                    "FX: letter+2 hex  (e.g. T0C, P80)",
                    "/ or ? (FX col)   FX command list",
                    "Backspace         Clear cell" }},
                { "PLAYBACK", {
                    "Space             Play / Stop",
                    "Cmd+[ / Cmd+]     BPM down / up",
                    "Cmd+- / Cmd+=     Step down / up" }}
            };

            // Column 2: Pattern + Editing + File
            columns[1] = {
                { "PATTERN & TRACKS", {
                    "Cmd+Left/Right    Switch pattern",
                    "Cmd+Shift+Right   Add new pattern",
                    "Cmd+Down/Up       Instrument +/-",
                    "Cmd+M             Mute track",
                    "Cmd+Shift+M       Solo track" }},
                { "EDITING", {
                    "Cmd+C / X / V     Copy / Cut / Paste",
                    "Cmd+Z             Undo",
                    "Cmd+Shift+Z / Y   Redo",
                    "Shift+Arrow       Select region" }},
                { "FILE", {
                    "Cmd+N             New project",
                    "Cmd+O             Open project",
                    "Cmd+S             Save",
                    "Cmd+Shift+S       Save As",
                    "Cmd+Shift+O       Load sample" }}
            };

            // Column 3: Tabs + Mixer + Browser + View
            columns[2] = {
                { "TABS", {
                    "Shift+F1          Tracker",
                    "Shift+F2          Inst Edit",
                    "Shift+F3          Inst Type",
                    "Shift+F4          Mixer",
                    "Shift+F5          Effects",
                    "Shift+F6          Browser",
                    "Escape            Return to Tracker",
                    "` (in edit tabs)  Params / Mod" }},
                { "MIXER", {
                    "Left / Right      Navigate params",
                    "Up / Down         Adjust value",
                    "Shift+Up/Down     Large adjust",
                    "Tab / Shift+Tab   Switch track",
                    "M / S             Mute / Solo" }},
                { "VIEW", {
                    "Cmd+Shift+A       Arrangement",
                    "Cmd+Shift+I       Instruments",
                    "Cmd+Shift+P       PAT / SONG mode",
                    "Cmd+Shift+K       Metronome",
                    "Cmd+/             Show this help" }}
            };
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff1e1e2e));

            auto area = getLocalBounds().reduced (16);
            int colWidth = area.getWidth() / 3;
            auto font = juce::Font (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain);
            auto titleFont = juce::Font (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold);

            for (int c = 0; c < 3; ++c)
            {
                auto colArea = area.removeFromLeft (colWidth);
                if (c < 2)
                    colArea.removeFromRight (8); // gap between columns

                int y = colArea.getY();

                for (auto& section : columns[static_cast<size_t> (c)])
                {
                    g.setFont (titleFont);
                    g.setColour (juce::Colour (0xffcba6f7));
                    g.drawText (section.title, colArea.getX(), y, colArea.getWidth(), 18,
                                juce::Justification::centredLeft);
                    y += 20;

                    g.setFont (font);
                    g.setColour (juce::Colour (0xffcdd6f4));
                    for (auto& shortcut : section.shortcuts)
                    {
                        g.drawText ("  " + shortcut, colArea.getX(), y, colArea.getWidth(), 16,
                                    juce::Justification::centredLeft);
                        y += 16;
                    }
                    y += 10; // gap between sections
                }
            }

            // Footer
            g.setFont (font);
            g.setColour (juce::Colour (0xff6c7086));
            g.drawText ("Drag audio files onto track headers to load samples.",
                        getLocalBounds().reduced (16).removeFromBottom (20),
                        juce::Justification::centred);
        }
    };

    auto* content = new HelpComponent();
    content->setSize (720, 480);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (content);
    opts.dialogTitle = "Keyboard Shortcuts";
    opts.dialogBackgroundColour = juce::Colour (0xff1e1e2e);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = false;
    opts.resizable = false;
    opts.launchAsync();
}

void MainComponent::toggleArrangementPanel()
{
    arrangementVisible = ! arrangementVisible;
    toolbar->setArrangementVisible (arrangementVisible);
    resized();

    // Restore keyboard focus to the active content component
    if (! arrangementVisible && activeTab == Tab::Tracker)
        trackerGrid->grabKeyboardFocus();
}

void MainComponent::toggleSongMode()
{
    songMode = ! songMode;
    toolbar->setPlaybackMode (songMode);

    if (trackerEngine.isPlaying())
        resyncPlaybackForCurrentMode();

    updateToolbar();
    markDirty();
}

void MainComponent::removePatternAndRepairArrangement (int index)
{
    patternData.removePattern (index);
    arrangement.remapAfterPatternRemoved (index, patternData.getNumPatterns());

    if (arrangementComponent != nullptr && arrangementComponent->getSelectedEntry() >= arrangement.getNumEntries())
        arrangementComponent->setSelectedEntry (arrangement.getNumEntries() - 1);

    if (trackerEngine.isPlaying() && songMode)
        syncArrangementToEdit();
}

int MainComponent::resolveInstrumentForTrackDrop (int track) const
{
    track = juce::jlimit (0, kNumTracks - 1, track);

    int trackInst = trackerEngine.getTrackInstrument (track);
    if (trackInst >= 0)
        return juce::jlimit (0, 255, trackInst);

    auto& pat = patternData.getCurrentPattern();
    for (int row = 0; row < pat.numRows; ++row)
    {
        int inst = pat.getCell (row, track).instrument;
        if (inst >= 0)
            return juce::jlimit (0, 255, inst);
    }

    return juce::jlimit (0, 255, track);
}

void MainComponent::resyncPlaybackForCurrentMode()
{
    if (! trackerEngine.isPlaying())
        return;

    if (songMode)
    {
        syncArrangementToEdit();
    }
    else
    {
        auto& pat = patternData.getCurrentPattern();
        trackerEngine.syncPatternToEdit (pat, getReleaseModes());
        trackerEngine.updateLoopRangeForPatternLength (pat.numRows);
    }
}

void MainComponent::syncArrangementToEdit()
{
    if (arrangement.getNumEntries() == 0)
    {
        // Fall back to current pattern
        trackerEngine.syncPatternToEdit (patternData.getCurrentPattern(), getReleaseModes());
        return;
    }

    // Build sequence of (pattern*, repeats) pairs
    std::vector<std::pair<const Pattern*, int>> sequence;
    for (auto& entry : arrangement.getEntries())
    {
        if (entry.patternIndex >= 0 && entry.patternIndex < patternData.getNumPatterns())
            sequence.emplace_back (&patternData.getPattern (entry.patternIndex), entry.repeats);
    }

    if (sequence.empty())
    {
        trackerEngine.syncPatternToEdit (patternData.getCurrentPattern(), getReleaseModes());
        return;
    }

    trackerEngine.syncArrangementToEdit (sequence, trackerEngine.getRowsPerBeat(), getReleaseModes());
}

void MainComponent::doCopy()
{
    auto& pat = patternData.getCurrentPattern();
    auto& clip = getClipboard();

    if (trackerGrid->hasSelection)
    {
        // Selection bounds are in visual space — copy visual columns
        int minRow, maxRow, minViTrack, maxViTrack;
        trackerGrid->getSelectionBounds (minRow, maxRow, minViTrack, maxViTrack);
        int copyStartVi = juce::jmax (0, minViTrack);
        int copyEndVi = juce::jmin (kNumTracks - 1, maxViTrack);
        if (copyStartVi > copyEndVi)
        {
            clip.numRows = 0;
            clip.numTracks = 0;
            clip.cells.clear();
            return;
        }
        clip.numRows = maxRow - minRow + 1;
        clip.numTracks = copyEndVi - copyStartVi + 1;
        clip.cells.resize (static_cast<size_t> (clip.numRows));
        for (int r = 0; r < clip.numRows; ++r)
        {
            clip.cells[static_cast<size_t> (r)].resize (static_cast<size_t> (clip.numTracks));
            for (int t = 0; t < clip.numTracks; ++t)
            {
                int phys = trackLayout.visualToPhysical (copyStartVi + t);
                clip.cells[static_cast<size_t> (r)][static_cast<size_t> (t)] =
                    pat.getCell (minRow + r, phys);
            }
        }
    }
    else
    {
        if (trackerGrid->isCursorInMasterLane())
        {
            clip.numRows = 0;
            clip.numTracks = 0;
            clip.cells.clear();
            return;
        }

        // Copy single cell at cursor
        clip.copyFromPattern (pat, trackerGrid->getCursorRow(), trackerGrid->getCursorRow(),
                              trackerGrid->getCursorTrack(), trackerGrid->getCursorTrack());
    }
}

void MainComponent::doPaste()
{
    auto& clip = getClipboard();
    if (clip.isEmpty()) return;
    if (trackerGrid->isCursorInMasterLane()) return;

    auto& pat = patternData.getCurrentPattern();
    int destRow = trackerGrid->getCursorRow();
    int destViTrack = trackLayout.physicalToVisual (trackerGrid->getCursorTrack());

    // Build undo records — paste to visual columns
    std::vector<MultiCellEditAction::CellRecord> records;
    for (int r = 0; r < clip.numRows; ++r)
    {
        int row = destRow + r;
        if (row >= pat.numRows) break;
        for (int t = 0; t < clip.numTracks; ++t)
        {
            int vi = destViTrack + t;
            if (vi >= kNumTracks) break;
            int phys = trackLayout.visualToPhysical (vi);
            MultiCellEditAction::CellRecord rec;
            rec.row = row;
            rec.track = phys;
            rec.oldCell = pat.getCell (row, phys);
            rec.newCell = clip.cells[static_cast<size_t> (r)][static_cast<size_t> (t)];
            records.push_back (rec);
        }
    }

    if (! records.empty())
    {
        undoManager.perform (new MultiCellEditAction (patternData, patternData.getCurrentPatternIndex(), std::move (records)));
        if (trackerGrid->onPatternDataChanged)
            trackerGrid->onPatternDataChanged();
        commandManager.commandStatusChanged();
    }
    trackerGrid->repaint();
}

void MainComponent::doCut()
{
    doCopy();

    auto& pat = patternData.getCurrentPattern();
    int patIdx = patternData.getCurrentPatternIndex();
    std::vector<MultiCellEditAction::CellRecord> cellRecords;
    std::vector<MultiCellEditAction::MasterFxRecord> masterFxRecords;

    auto sameFx = [] (const FxSlot& a, const FxSlot& b)
    {
        return a.fx == b.fx && a.fxParam == b.fxParam && a.fxCommand == b.fxCommand;
    };

    auto sameCell = [&sameFx] (const Cell& a, const Cell& b)
    {
        if (a.note != b.note || a.instrument != b.instrument || a.volume != b.volume)
            return false;
        if (a.fxSlots.size() != b.fxSlots.size())
            return false;
        for (size_t i = 0; i < a.fxSlots.size(); ++i)
            if (! sameFx (a.fxSlots[i], b.fxSlots[i]))
                return false;
        return true;
    };

    if (trackerGrid->hasSelection)
    {
        // Selection bounds are in visual space
        int minRow, maxRow, minViTrack, maxViTrack;
        trackerGrid->getSelectionBounds (minRow, maxRow, minViTrack, maxViTrack);

        for (int r = minRow; r <= maxRow; ++r)
        {
            for (int vi = minViTrack; vi <= maxViTrack; ++vi)
            {
                if (vi >= kNumTracks)
                {
                    for (int lane = 0; lane < trackLayout.getMasterFxLaneCount(); ++lane)
                    {
                        auto oldSlot = pat.getMasterFxSlot (r, lane);
                        auto newSlot = oldSlot;
                        newSlot.clear();
                        if (! sameFx (oldSlot, newSlot))
                            masterFxRecords.push_back ({ r, lane, oldSlot, newSlot });
                    }
                }
                else
                {
                    int phys = trackLayout.visualToPhysical (vi);
                    MultiCellEditAction::CellRecord rec;
                    rec.row = r;
                    rec.track = phys;
                    rec.oldCell = pat.getCell (r, phys);
                    rec.newCell = Cell{}; // cleared
                    if (! sameCell (rec.oldCell, rec.newCell))
                        cellRecords.push_back (rec);
                }
            }
        }
    }
    else
    {
        int r = trackerGrid->getCursorRow();
        int t = trackerGrid->getCursorTrack();
        if (t >= kNumTracks)
        {
            int lane = trackerGrid->getCursorFxLane();
            auto oldSlot = pat.getMasterFxSlot (r, lane);
            auto newSlot = oldSlot;
            newSlot.clear();
            if (! sameFx (oldSlot, newSlot))
                masterFxRecords.push_back ({ r, lane, oldSlot, newSlot });
        }
        else
        {
            MultiCellEditAction::CellRecord rec;
            rec.row = r;
            rec.track = t;
            rec.oldCell = pat.getCell (r, t);
            rec.newCell = Cell{};
            if (! sameCell (rec.oldCell, rec.newCell))
                cellRecords.push_back (rec);
        }
    }

    if (! cellRecords.empty() || ! masterFxRecords.empty())
    {
        undoManager.perform (new MultiCellEditAction (patternData, patIdx,
                                                      std::move (cellRecords),
                                                      std::move (masterFxRecords)));
        if (trackerGrid->onPatternDataChanged)
            trackerGrid->onPatternDataChanged();
        commandManager.commandStatusChanged();
    }

    if (trackerGrid->hasSelection)
        trackerGrid->clearSelection();

    trackerGrid->repaint();
}

void MainComponent::updateInstrumentPanel()
{
    auto loadedSamples = trackerEngine.getSampler().getLoadedSamples();
    instrumentPanel->updateSampleInfo (loadedSamples);
    instrumentPanel->setSelectedInstrument (trackerGrid->getCurrentInstrument());

    updateTrackSampleMarkers();
}

void MainComponent::updateTrackSampleMarkers()
{
    auto loadedSamples = trackerEngine.getSampler().getLoadedSamples();
    for (int i = 0; i < kNumTracks; ++i)
    {
        bool hasSample = false;
        int trackInst = trackerEngine.getTrackInstrument (i);
        if (trackInst >= 0)
            hasSample = loadedSamples.find (trackInst) != loadedSamples.end();
        else
            hasSample = loadedSamples.find (i) != loadedSamples.end();
        trackerGrid->trackHasSample[static_cast<size_t> (i)] = hasSample;
    }
    trackerGrid->repaint();
}

void MainComponent::loadSampleForInstrument (int instrument)
{
    auto chooser = std::make_shared<juce::FileChooser> (
        "Load Sample for Instrument " + juce::String::formatted ("%02X", instrument),
        juce::File::getSpecialLocation (juce::File::userHomeDirectory),
        "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser, instrument] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (file.existsAsFile())
                              {
                                  auto error = trackerEngine.loadSampleForInstrument (instrument, file);
                                  if (error.isNotEmpty())
                                      juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                                              "Load Error", error);
                                  else
                                  {
                                      if (trackerEngine.isPlaying())
                                      {
                                          if (songMode)
                                              syncArrangementToEdit();
                                          else
                                              trackerEngine.refreshTracksForInstrument (instrument, patternData.getCurrentPattern());
                                      }

                                      trackerGrid->repaint();
                                      updateToolbar();
                                      updateInstrumentPanel();
                                      fileBrowser->updateInstrumentSlots (trackerEngine.getSampler().getLoadedSamples());
                                      markDirty();
                                  }
                              }
                          });
}

void MainComponent::clearSampleForInstrument (int instrument)
{
    trackerEngine.clearSampleForInstrument (instrument);
    trackerEngine.invalidateTrackInstruments();

    if (trackerEngine.isPlaying())
    {
        if (songMode)
            syncArrangementToEdit();
        else
            trackerEngine.syncPatternToEdit (patternData.getCurrentPattern(), getReleaseModes());
    }

    if (trackerGrid->getCurrentInstrument() == instrument)
        updateSampleEditorForCurrentInstrument();

    updateToolbar();
    updateInstrumentPanel();
    fileBrowser->updateInstrumentSlots (trackerEngine.getSampler().getLoadedSamples());
    markDirty();
}

void MainComponent::updateSampleEditorForCurrentInstrument()
{
    int inst = trackerGrid->getCurrentInstrument();
    auto sampleFile = trackerEngine.getSampler().getSampleFile (inst);
    auto params = trackerEngine.getSampler().getParams (inst);

    if (sampleFile.existsAsFile())
        sampleEditor->setInstrument (inst, sampleFile, params);
    else
        sampleEditor->setInstrument (inst, juce::File(), params);
}

std::array<bool, kNumTracks> MainComponent::getReleaseModes() const
{
    std::array<bool, kNumTracks> modes {};
    for (int i = 0; i < kNumTracks; ++i)
        modes[static_cast<size_t> (i)] = (trackLayout.getTrackNoteMode (i) == NoteMode::Release);
    return modes;
}

void MainComponent::cycleTab (int direction)
{
    static constexpr Tab allTabs[] = {
        Tab::Tracker, Tab::InstrumentEdit, Tab::InstrumentType,
        Tab::Mixer, Tab::Effects, Tab::Browser
    };
    static constexpr int numTabs = 6;

    int current = 0;
    for (int i = 0; i < numTabs; ++i)
        if (allTabs[i] == activeTab) { current = i; break; }

    int next = ((current + direction) % numTabs + numTabs) % numTabs;
    switchToTab (allTabs[next]);
}

void MainComponent::switchToTab (Tab tab)
{
    if (activeTab == tab) return;

    // Stop file preview when leaving browser tab
    if (activeTab == Tab::Browser)
        trackerEngine.stopPreview();

    activeTab = tab;
    tabBar->setActiveTab (tab);

    // Refresh browser data when switching to it
    if (tab == Tab::Browser)
    {
        fileBrowser->updateInstrumentSlots (trackerEngine.getSampler().getLoadedSamples());
        fileBrowser->setSelectedInstrument (trackerGrid->getCurrentInstrument());
    }

    // Update instrument panel and editor when switching to edit/type tabs
    if (tab == Tab::InstrumentEdit || tab == Tab::InstrumentType)
    {
        updateInstrumentPanel();
        updateSampleEditorForCurrentInstrument();
        sampleEditor->setOctave (trackerGrid->getOctave());
    }

    // Sync mute/solo state when switching to mixer
    if (tab == Tab::Mixer)
    {
        for (int i = 0; i < kNumTracks; ++i)
        {
            auto* t = trackerEngine.getTrack (i);
            if (t != nullptr)
            {
                mixerComponent->setTrackMuteState (i, t->isMuted (false));
                mixerComponent->setTrackSoloState (i, t->isSolo (false));
            }
        }
    }

    resized();

    // Refresh effects params when switching to effects tab
    if (tab == Tab::Effects)
    {
        sendEffectsComponent->setDelayParams (trackerEngine.getDelayParams());
        sendEffectsComponent->setReverbParams (trackerEngine.getReverbParams());
    }

    // Focus the right component
    switch (tab)
    {
        case Tab::Tracker:
            trackerGrid->grabKeyboardFocus();
            break;
        case Tab::InstrumentEdit:
        case Tab::InstrumentType:
            sampleEditor->grabKeyboardFocus();
            break;
        case Tab::Mixer:
            mixerComponent->grabKeyboardFocus();
            break;
        case Tab::Effects:
            sendEffectsComponent->grabKeyboardFocus();
            break;
        case Tab::Browser:
            fileBrowser->grabKeyboardFocus();
            break;
    }
}

void MainComponent::updateMuteSoloState()
{
    for (int i = 0; i < kNumTracks; ++i)
    {
        auto* t = trackerEngine.getTrack (i);
        if (t != nullptr)
        {
            bool muted = t->isMuted (false);
            bool soloed = t->isSolo (false);
            trackerGrid->trackMuted[static_cast<size_t> (i)] = muted;
            trackerGrid->trackSoloed[static_cast<size_t> (i)] = soloed;
            mixerComponent->setTrackMuteState (i, muted);
            mixerComponent->setTrackSoloState (i, soloed);
            mixerState.tracks[static_cast<size_t> (i)].muted = muted;
            mixerState.tracks[static_cast<size_t> (i)].soloed = soloed;
        }
    }
    trackerGrid->repaint();
    if (activeTab == Tab::Mixer)
        mixerComponent->repaint();
}
