#include <regex>
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

    toolbar->onToggleAutomation = [this]
    {
        automationPanelVisible = ! automationPanelVisible;
        toolbar->setAutomationPanelVisible (automationPanelVisible);
        if (automationPanelVisible)
            refreshAutomationPanel();
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
    instrumentPanel->onSetPluginInstrumentRequested = [this] (int inst)
    {
        // Show plugin picker from scanned instruments list
        auto instruments = trackerEngine.getPluginCatalog().getInstruments();
        if (instruments.isEmpty())
        {
            setTemporaryStatus ("No plugin instruments found. Scan for plugins first.", true, 3000);
            return;
        }

        // Sort by manufacturer, then by name within each manufacturer
        std::sort (instruments.begin(), instruments.end(),
                   [] (const juce::PluginDescription& a, const juce::PluginDescription& b)
                   {
                       int cmp = a.manufacturerName.compareIgnoreCase (b.manufacturerName);
                       if (cmp != 0) return cmp < 0;
                       return a.name.compareIgnoreCase (b.name) < 0;
                   });

        // Build menu with manufacturer submenus
        juce::PopupMenu menu;
        juce::String currentMfr;
        juce::PopupMenu currentSubMenu;
        std::vector<int> subMenuStartIndices;  // first index in each submenu

        for (int i = 0; i < instruments.size(); ++i)
        {
            auto& desc = instruments.getReference (i);
            auto mfr = desc.manufacturerName.isEmpty() ? juce::String ("Unknown") : desc.manufacturerName;

            if (mfr != currentMfr)
            {
                if (currentMfr.isNotEmpty())
                    menu.addSubMenu (currentMfr, currentSubMenu);
                currentSubMenu = juce::PopupMenu();
                currentMfr = mfr;
            }
            currentSubMenu.addItem (i + 1, desc.name + " (" + desc.pluginFormatName + ")");
        }
        if (currentMfr.isNotEmpty())
            menu.addSubMenu (currentMfr, currentSubMenu);

        int cursorTrack = trackerGrid->getCursorTrack();
        if (cursorTrack >= kNumTracks)
            cursorTrack = 0;

        menu.showMenuAsync (juce::PopupMenu::Options(),
                            [this, inst, cursorTrack, instruments] (int result)
                            {
                                if (result > 0 && result <= instruments.size())
                                {
                                    auto& desc = instruments.getReference (result - 1);

                                    // Clear stale automation for this instrument slot (param indices change with new plugin)
                                    auto pluginId = "inst:" + juce::String (inst);
                                    for (int p = 0; p < patternData.getNumPatterns(); ++p)
                                        patternData.getPattern (p).automationData.removeAllLanesForPlugin (pluginId);

                                    trackerEngine.setPluginInstrument (inst, desc, cursorTrack);
                                    invalidateAutomationPluginCache (cursorTrack);
                                    updateInstrumentPanel();
                                    if (automationPanelVisible)
                                        refreshAutomationPanel();
                                    markDirty();
                                    setTemporaryStatus ("Plugin instrument slot "
                                                        + juce::String::formatted ("%02X", inst)
                                                        + " set to " + desc.name
                                                        + " (owner track " + juce::String (cursorTrack + 1) + ")",
                                                        false, 3000);
                                }
                            });
    };
    instrumentPanel->onClearPluginInstrumentRequested = [this] (int inst)
    {
        trackerEngine.clearPluginInstrument (inst);
        invalidateAutomationPluginCache();
        updateInstrumentPanel();
        if (automationPanelVisible)
            refreshAutomationPanel();
        markDirty();
    };
    instrumentPanel->onOpenPluginEditorRequested = [this] (int inst)
    {
        trackerEngine.openPluginInstrumentEditor (inst);
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
    sampleEditor->onOpenPluginEditorRequested = [this] (int inst)
    {
        trackerEngine.openPluginInstrumentEditor (inst);
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

    // Insert plugin callbacks
    mixerComponent->onAddInsertClicked = [this] (int track)
    {
        auto effects = trackerEngine.getPluginCatalog().getEffects();
        if (effects.isEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::InfoIcon,
                "No Plugins", "No effect plugins found. Scan for plugins in Audio Plugin Settings first.");
            return;
        }

        // Sort by manufacturer, then by name within each manufacturer
        std::sort (effects.begin(), effects.end(),
                   [] (const juce::PluginDescription& a, const juce::PluginDescription& b)
                   {
                       int cmp = a.manufacturerName.compareIgnoreCase (b.manufacturerName);
                       if (cmp != 0) return cmp < 0;
                       return a.name.compareIgnoreCase (b.name) < 0;
                   });

        // Build menu with manufacturer submenus
        juce::PopupMenu menu;
        juce::String currentMfr;
        juce::PopupMenu currentSubMenu;

        for (int i = 0; i < effects.size(); ++i)
        {
            auto mfr = effects[i].manufacturerName.isEmpty() ? juce::String ("Unknown") : effects[i].manufacturerName;

            if (mfr != currentMfr)
            {
                if (currentMfr.isNotEmpty())
                    menu.addSubMenu (currentMfr, currentSubMenu);
                currentSubMenu = juce::PopupMenu();
                currentMfr = mfr;
            }
            currentSubMenu.addItem (i + 1, effects[i].name + " (" + effects[i].pluginFormatName + ")");
        }
        if (currentMfr.isNotEmpty())
            menu.addSubMenu (currentMfr, currentSubMenu);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (mixerComponent.get()),
            [this, track, effects] (int result)
            {
                if (result > 0)
                {
                    auto desc = effects[result - 1];
                    if (trackerEngine.addInsertPlugin (track, desc))
                    {
                        invalidateAutomationPluginCache (track);
                        mixerComponent->repaint();
                        if (automationPanelVisible)
                            refreshAutomationPanel();
                        markDirty();
                    }
                }
            });
    };

    mixerComponent->onRemoveInsertClicked = [this] (int track, int slotIndex)
    {
        trackerEngine.removeInsertPlugin (track, slotIndex);
        invalidateAutomationPluginCache (track);
        mixerComponent->repaint();
        if (automationPanelVisible)
            refreshAutomationPanel();
        markDirty();
    };

    mixerComponent->onInsertBypassToggled = [this] (int track, int slotIndex, bool bypassed)
    {
        trackerEngine.setInsertBypassed (track, slotIndex, bypassed);
        mixerComponent->repaint();
        markDirty();
    };

    mixerComponent->onOpenInsertEditor = [this] (int track, int slotIndex)
    {
        trackerEngine.openPluginEditor (track, slotIndex);
    };

    // Callback from engine when insert state changes (e.g. after addInsertPlugin modifies the state model)
    trackerEngine.onInsertStateChanged = [this]
    {
        invalidateAutomationPluginCache();
        mixerComponent->repaint();
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

    // Create automation panel (bottom panel in tracker tab)
    automationPanel = std::make_unique<PluginAutomationComponent> (trackerLookAndFeel);
    addChildComponent (*automationPanel);
    automationPanel->onAutomationChanged = [this]
    {
        if (trackerEngine.isPlaying())
        {
            if (songMode)
                syncArrangementToEdit();
            else
                trackerEngine.syncPatternToEdit (patternData.getCurrentPattern(), getReleaseModes());
        }
        markDirty();
    };
    automationPanel->onPluginSelected = [] (const juce::String& /*pluginId*/)
    {
        // Selection should not trigger plugin-list repopulation.
    };
    automationPanel->onParameterSelected = [] (const juce::String& pluginId, int paramIndex)
    {
        // Update baseline from current parameter value
        juce::ignoreUnused (pluginId, paramIndex);
        // Baseline is set when parameters are populated
    };
    automationPanel->onPanelHeightChanged = [this] (int /*newHeight*/)
    {
        resized();
    };
    automationPanel->onGetCurrentParameterValue = [this]() -> float
    {
        auto pluginId = automationPanel->getSelectedPluginId();
        int paramIdx = automationPanel->getSelectedParameterIndex();
        if (pluginId.isEmpty() || paramIdx < 0)
            return 0.5f;

        if (auto* audioPlugin = trackerEngine.resolvePluginInstance (pluginId))
        {
            // tryEnter to avoid deadlocking with the audio thread.
            auto& lock = audioPlugin->getCallbackLock();
            if (lock.tryEnter())
            {
                auto& params = audioPlugin->getParameters();
                float val = 0.5f;
                if (paramIdx >= 0 && paramIdx < params.size())
                    val = params[paramIdx]->getValue();
                lock.exit();
                return val;
            }
        }
        return 0.5f;
    };

    // Create the grid
    trackerGrid = std::make_unique<TrackerGrid> (patternData, trackerLookAndFeel, trackLayout);
    trackerGrid->setRowsPerBeat (trackerEngine.getRowsPerBeat());
    trackerGrid->setUndoManager (&undoManager);
    addAndMakeVisible (*trackerGrid);

    // Note entry validation callback (ownership/track mode check)
    trackerGrid->onValidateNoteEntry = [this] (int instrumentIndex, int trackIndex) -> juce::String
    {
        auto error = trackerEngine.validateNoteEntry (instrumentIndex, trackIndex);
        if (error.isNotEmpty())
            setTemporaryStatus (error, true, 3000);
        return error;
    };

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
        // Sync instrument from cell under cursor (standard tracker behaviour)
        auto& pat = patternData.getCurrentPattern();
        int row = trackerGrid->getCursorRow();
        int track = trackerGrid->getCursorTrack();
        if (! trackerGrid->isCursorInMasterLane() && row >= 0 && row < pat.numRows
            && track >= 0 && track < kNumTracks)
        {
            auto cell = pat.getCell (row, track);
            auto slot = cell.getNoteLane (trackerGrid->getCursorNoteLane());
            if (slot.instrument >= 0)
                trackerGrid->setCurrentInstrument (slot.instrument);
        }

        updateStatusBar();
        updateToolbar();
        instrumentPanel->setSelectedInstrument (trackerGrid->getCurrentInstrument());
        if (automationPanelVisible)
            refreshAutomationPanel (false);
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

    // Status message callback (for ownership violations etc.)
    trackerEngine.onStatusMessage = [this] (const juce::String& message, bool isError, int timeoutMs)
    {
        setTemporaryStatus (message, isError, timeoutMs);
    };

    trackerEngine.onNavigateToAutomation = [this] (const juce::String& pluginId, int paramIndex)
    {
        navigateToAutomationParam (pluginId, paramIndex);
    };

    trackerEngine.onPluginInstrumentCleared = [this] (const juce::String& pluginId)
    {
        // Remove automation lanes for this plugin from all patterns
        for (int p = 0; p < patternData.getNumPatterns(); ++p)
            patternData.getPattern (p).automationData.removeAllLanesForPlugin (pluginId);

        // Refresh automation panel if visible
        if (automationPanelVisible)
            refreshAutomationPanel();
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
    automationPanel->addKeyListener (commandManager.getKeyMappings());
    arrangementComponent->addKeyListener (commandManager.getKeyMappings());

    setSize (1280, 720);
    setWantsKeyboardFocus (true);
    trackerGrid->grabKeyboardFocus();
}

MainComponent::~MainComponent()
{
    // Prevent any late engine callbacks from touching a partially-destroyed UI.
    trackerEngine.onTransportChanged = nullptr;
    trackerEngine.onStatusMessage = nullptr;
    trackerEngine.onNavigateToAutomation = nullptr;
    trackerEngine.onPluginInstrumentCleared = nullptr;
    trackerEngine.onInsertStateChanged = nullptr;

   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
   #endif
    arrangementComponent->removeKeyListener (commandManager.getKeyMappings());
    automationPanel->removeKeyListener (commandManager.getKeyMappings());
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
    automationPanel->setVisible (false);
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

            // Automation panel (bottom, above status bar)
            if (automationPanelVisible)
            {
                automationPanel->setBounds (r.removeFromBottom (automationPanel->getPanelHeight()));
                automationPanel->setVisible (true);
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

    // Cmd+Shift+B: toggle automation panel
    if (cmd && shift && textChar == 'B')
    {
        automationPanelVisible = ! automationPanelVisible;
        toolbar->setAutomationPanelVisible (automationPanelVisible);
        if (automationPanelVisible)
            refreshAutomationPanel();
        resized();
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
    commands.add (cmdAudioPluginSettings);
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
        case cmdAudioPluginSettings:
            result.setInfo ("Audio & Plugin Settings...", "Configure audio output and plugin scan paths", "File", 0);
            result.addDefaultKeypress (',', juce::ModifierKeys::commandModifier);
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
        case cmdAudioPluginSettings:
            showAudioPluginSettings();
            return true;
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
        menu.addSeparator();
        menu.addCommandItem (&commandManager, cmdAudioPluginSettings);
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
        int playPatternIndex = -1;

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
                playPatternIndex = info.patternIndex;
            }
        }
        else
        {
            // Pattern mode: simple row from beat position
            playRow = trackerEngine.getPlaybackRow (patternData.getCurrentPattern().numRows);
            playPatternIndex = patternData.getCurrentPatternIndex();
        }

        if (playPatternIndex >= 0 && playPatternIndex < patternData.getNumPatterns() && playRow >= 0)
        {
            const auto& automationData = patternData.getPattern (playPatternIndex).automationData;
            trackerEngine.applyAutomationForPlaybackRow (automationData, playRow);
        }

        trackerGrid->setPlaybackRow (playRow);
        trackerGrid->setPlaying (true);

        // Update automation panel playback position and recording
        if (automationPanelVisible && automationPanel != nullptr)
        {
            automationPanel->setPlaybackRow (playRow);

            // Automation recording: poll current parameter value and record it
            if (automationPanel->isRecording() && playRow >= 0)
            {
                if (automationPanel->onGetCurrentParameterValue)
                {
                    float currentValue = automationPanel->onGetCurrentParameterValue();
                    automationPanel->recordParameterValue (playRow, currentValue);
                }
            }
        }

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
        if (automationPanelVisible && automationPanel != nullptr)
            automationPanel->setPlaybackRow (-1);
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
    // Check if a temporary status message is active
    if (temporaryStatusMessage.isNotEmpty())
    {
        auto now = juce::Time::getMillisecondCounter();
        if (now < temporaryStatusExpiry)
        {
            statusLabel.setText (temporaryStatusMessage, juce::dontSendNotification);
            statusLabel.setColour (juce::Label::textColourId,
                                   temporaryStatusIsError ? juce::Colour (0xffff4444)
                                                          : juce::Colour (0xffffcc44));
            octaveLabel.setText ("Oct:" + juce::String (trackerGrid->getOctave()),
                                 juce::dontSendNotification);
            bpmLabel.setText ("BPM:" + juce::String (trackerEngine.getBpm(), 1),
                              juce::dontSendNotification);
            return;
        }
        // Expired -- clear it
        temporaryStatusMessage.clear();
        statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xffcccccc));
    }

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

void MainComponent::setTemporaryStatus (const juce::String& message, bool isError, int timeoutMs)
{
    temporaryStatusMessage = message;
    temporaryStatusIsError = isError;
    temporaryStatusExpiry = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);
    updateStatusBar();
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

    toolbar->setAutomationPanelVisible (automationPanelVisible);

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
    if (automationPanelVisible)
        refreshAutomationPanel();
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

    // Note lanes
    menu.addSeparator();
    int noteLanes = trackLayout.getTrackNoteLaneCount (track);
    menu.addItem (22, "Add Note Lane (" + juce::String (noteLanes) + " -> " + juce::String (noteLanes + 1) + ")", noteLanes < 8);
    menu.addItem (23, "Remove Note Lane (" + juce::String (noteLanes) + " -> " + juce::String (noteLanes - 1) + ")", noteLanes > 1);

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
                            else if (result == 22)
                            {
                                performUndoableTrackLayoutChange ([this, track]
                                {
                                    trackLayout.addNoteLane (track);
                                });
                            }
                            else if (result == 23)
                            {
                                performUndoableTrackLayoutChange ([this, track]
                                {
                                    trackLayout.removeNoteLane (track);
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
    trackerEngine.setInstrumentSlotInfos ({});
    mixerState.reset();
    trackerEngine.refreshMixerPlugins();
    invalidateAutomationPluginCache();
    undoManager.clearUndoHistory();
    currentProjectFile = juce::File();
    isDirty = false;
    updateWindowTitle();
    updateStatusBar();
    updateToolbar();
    updateInstrumentPanel();
    fileBrowser->updateInstrumentSlots (trackerEngine.getSampler().getLoadedSamples());
    trackerGrid->repaint();
    if (automationPanelVisible)
        refreshAutomationPanel();
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
                              std::map<int, InstrumentSlotInfo> loadedPluginSlots;
                              auto error = ProjectSerializer::loadFromFile (file, patternData, bpm, rpb, samples, instParams, arrangement, trackLayout, mixerState, loadedDelay, loadedReverb, &loadedFollowMode, &browserDir, &loadedPluginSlots);
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

                              // Restore plugin instrument slots
                              trackerEngine.setInstrumentSlotInfos (loadedPluginSlots);

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
                              invalidateAutomationPluginCache();

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
                              if (automationPanelVisible)
                                  refreshAutomationPanel();

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
        trackerEngine.snapshotInsertPluginStates();
        trackerEngine.snapshotPluginInstrumentStates();
        auto& slotInfos = trackerEngine.getAllInstrumentSlotInfos();
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
                                                     fileBrowser->getCurrentDirectory().getFullPathName(),
                                                     &slotInfos);
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
                              trackerEngine.snapshotInsertPluginStates();
                              trackerEngine.snapshotPluginInstrumentStates();
                              auto& slotInfos = trackerEngine.getAllInstrumentSlotInfos();
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
                                                                          fileBrowser->getCurrentDirectory().getFullPathName(),
                                                                          &slotInfos);
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
    auto& pluginSlotInfos = trackerEngine.getAllInstrumentSlotInfos();

    instrumentPanel->updateSampleInfo (loadedSamples);
    instrumentPanel->updatePluginInfo (pluginSlotInfos);
    instrumentPanel->setSelectedInstrument (trackerGrid->getCurrentInstrument());

    // Keep file browser in sync with plugin instrument state
    fileBrowser->updatePluginSlots (pluginSlotInfos);

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

    // Check if this is a plugin instrument
    if (trackerEngine.isPluginInstrument (inst))
    {
        auto& info = trackerEngine.getInstrumentSlotInfo (inst);
        sampleEditor->setPluginInstrument (inst, info.pluginDescription.name, info.ownerTrack);
        return;
    }

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

    // Refresh automation panel when returning to tracker tab
    if (tab == Tab::Tracker && automationPanelVisible)
        refreshAutomationPanel();

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

void MainComponent::showAudioPluginSettings()
{
    auto* content = new AudioPluginSettingsComponent (trackerEngine.getEngine(),
                                                      trackerEngine.getPluginCatalog(),
                                                      trackerLookAndFeel);

    // Load persisted scan paths, or use defaults if none saved
    auto savedPaths = ProjectSerializer::loadGlobalPluginScanPaths();
    if (savedPaths.isEmpty())
        savedPaths = PluginCatalogService::getDefaultScanPaths();

    content->setScanPaths (savedPaths);

    // Persist scan paths when they change
    content->onScanPathsChanged = [] (const juce::StringArray& paths)
    {
        ProjectSerializer::saveGlobalPluginScanPaths (paths);
    };

    content->setSize (AudioPluginSettingsComponent::kPreferredWidth,
                      AudioPluginSettingsComponent::kPreferredHeight);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (content);
    opts.dialogTitle = "Audio & Plugin Settings";
    opts.dialogBackgroundColour = juce::Colour (0xff1e1e2e);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = false;
    opts.resizable = true;
    opts.launchAsync();
}

void MainComponent::refreshAutomationPanel (bool forcePopulate)
{
    if (automationPanel == nullptr)
        return;

    auto& pat = patternData.getCurrentPattern();
    automationPanel->setAutomationData (&pat.automationData);
    automationPanel->setPatternLength (pat.numRows);

    int curTrack = trackerGrid->getCursorTrack();
    automationPanel->setCurrentTrack (curTrack);

    // Only re-enumerate plugin parameters when the track actually changes
    // (or when explicitly forced).
    if (forcePopulate || curTrack != lastAutomationPopulateTrack)
    {
        // Save selection for the track we're leaving
        if (lastAutomationPopulateTrack >= 0)
            saveAutomationSelection();
        lastAutomationPopulateTrack = curTrack;
        populateAutomationPlugins();
        restoreAutomationSelection (curTrack);
    }
    else
    {
        automationPanel->repaint();
    }
}

void MainComponent::invalidateAutomationPluginCache (int trackIndex)
{
    if (trackIndex < 0)
        automationPluginCache.clear();
    else
        automationPluginCache.erase (trackIndex);
    lastAutomationPopulateTrack = -1;
}

void MainComponent::saveAutomationSelection()
{
    if (automationPanel == nullptr || lastAutomationPopulateTrack < 0)
        return;
    auto pluginId = automationPanel->getSelectedPluginId();
    auto paramIdx = automationPanel->getSelectedParameterIndex();
    if (pluginId.isNotEmpty())
        automationSelectionPerTrack[lastAutomationPopulateTrack] = { pluginId, paramIdx };
}

void MainComponent::restoreAutomationSelection (int trackIndex)
{
    if (automationPanel == nullptr)
        return;
    auto it = automationSelectionPerTrack.find (trackIndex);
    if (it != automationSelectionPerTrack.end())
        automationPanel->navigateToParam (it->second.first, it->second.second);
}

void MainComponent::populateAutomationPlugins()
{
    if (automationPanel == nullptr)
        return;

    int cursorTrack = trackerGrid->getCursorTrack();
    if (cursorTrack >= kNumTracks)
        cursorTrack = 0;

    // Return cached result if available — avoids expensive getName
    // on every parameter each time the user switches to this track.
    // Refresh hasAutomation flags from current pattern since automation
    // data may have changed since the cache was built.
    auto cacheIt = automationPluginCache.find (cursorTrack);
    if (cacheIt != automationPluginCache.end())
    {
        auto& cachedPlugins = cacheIt->second;
        const auto& cachedAutoData = patternData.getCurrentPattern().automationData;
        for (auto& plugin : cachedPlugins)
            for (auto& param : plugin.parameters)
                param.hasAutomation = cachedAutoData.findLane (plugin.pluginId, param.index) != nullptr;
        automationPanel->setAvailablePlugins (cachedPlugins);
        return;
    }

    // Get current automation data so we can mark already-automated parameters.
    auto& pat = patternData.getCurrentPattern();
    const auto& automationData = pat.automationData;

    // Helper: collect parameters from a JUCE AudioPluginInstance.
    // getName() is safe without the callback lock — it just reads stored strings,
    // not audio-thread state.  Removing tryEnter from this path eliminates the
    // main source of latency (lock contention with the always-live audio graph).
    auto collectParams = [&] (juce::AudioPluginInstance* audioPlugin,
                              const juce::String& pluginId,
                              AutomatablePluginInfo& pluginInfo)
    {
        if (audioPlugin == nullptr)
            return;

        auto& params = audioPlugin->getParameters();
        pluginInfo.parameters.reserve (static_cast<size_t> (params.size()));
        for (int pi = 0; pi < params.size(); ++pi)
        {
            AutomatablePluginInfo::ParamInfo paramInfo;
            paramInfo.index = pi;
            paramInfo.name = params[pi]->getName (40);
            paramInfo.hasAutomation = automationData.findLane (pluginId, pi) != nullptr;
            pluginInfo.parameters.push_back (paramInfo);
        }
    };

    std::vector<AutomatablePluginInfo> plugins;

    // 1. Add instrument plugins owned by the current track
    for (const auto& [instIdx, info] : trackerEngine.getAllInstrumentSlotInfos())
    {
        if (info.isPlugin() && info.ownerTrack == cursorTrack)
        {
            AutomatablePluginInfo pluginInfo;
            pluginInfo.pluginId = "inst:" + juce::String (instIdx);
            pluginInfo.displayName = info.pluginDescription.name + " (Inst " + juce::String (instIdx) + ")";
            pluginInfo.owningTrack = cursorTrack;
            pluginInfo.isInstrument = true;

            if (auto* pluginInstance = trackerEngine.getPluginInstrumentInstance (instIdx))
            {
                if (auto* external = dynamic_cast<te::ExternalPlugin*> (pluginInstance))
                    collectParams (external->getAudioPluginInstance(), pluginInfo.pluginId, pluginInfo);
            }

            if (! pluginInfo.parameters.empty())
                plugins.push_back (std::move (pluginInfo));
        }
    }

    // 2. Add insert plugins on the current track
    if (trackerEngine.getTrack (cursorTrack) != nullptr)
    {
        auto& insertSlots = mixerState.insertSlots[static_cast<size_t> (cursorTrack)];
        for (int si = 0; si < static_cast<int> (insertSlots.size()); ++si)
        {
            auto& slot = insertSlots[static_cast<size_t> (si)];
            if (slot.isEmpty())
                continue;

            AutomatablePluginInfo pluginInfo;
            pluginInfo.pluginId = "insert:" + juce::String (cursorTrack) + ":" + juce::String (si);
            pluginInfo.displayName = slot.pluginName + " (Insert " + juce::String (si + 1) + ")";
            pluginInfo.owningTrack = cursorTrack;
            pluginInfo.isInstrument = false;

            if (auto* plugin = trackerEngine.getInsertPlugin (cursorTrack, si))
            {
                if (auto* external = dynamic_cast<te::ExternalPlugin*> (plugin))
                    collectParams (external->getAudioPluginInstance(), pluginInfo.pluginId, pluginInfo);
            }

            if (! pluginInfo.parameters.empty())
                plugins.push_back (std::move (pluginInfo));
        }
    }

    // Sort parameters: already-automated first (marked with *), then by
    // relevance tier (common automation targets), then alphabetical.
    // Uses fast containsIgnoreCase instead of regex for speed.
    auto getParamPriority = [] (const juce::String& name) -> int
    {
        auto lo = name.toLowerCase();
        if (lo.contains ("macro") || lo.contains ("mmod"))                       return 0;
        if (lo.contains ("cutoff") || (lo.contains ("filter") && lo.contains ("freq"))) return 1;
        if (lo.contains ("reson") || lo.contains ("emphasis"))                   return 2;
        if (lo.contains ("volume") || lo.contains ("gain") || lo.contains ("level")
            || lo.contains ("amplitude") || lo.contains ("output"))              return 3;
        if (lo.contains ("mix") || lo.contains ("dry") || lo.contains ("wet")
            || lo.contains ("blend"))                                            return 4;
        if (lo.contains ("pan") || lo.contains ("balance") || lo.contains ("width")
            || lo.contains ("stereo") || lo.contains ("spread"))                 return 5;
        if (lo.contains ("lfo") && (lo.contains ("rate") || lo.contains ("speed"))) return 6;
        if (lo.contains ("attack") || lo.contains ("decay") || lo.contains ("sustain")
            || lo.contains ("release") || lo.contains ("adsr"))                  return 7;
        if (lo.contains ("pitch") || lo.contains ("tune") || lo.contains ("detune")
            || lo.contains ("semi") || lo.contains ("coarse") || lo.contains ("fine")
            || lo.contains ("transpose") || lo.contains ("cent"))                return 8;
        if (lo.contains ("drive") || lo.contains ("distort") || lo.contains ("saturat")
            || lo.contains ("overdrive"))                                        return 9;
        if (lo.contains ("feedback") || (lo.contains ("delay") && lo.contains ("time"))) return 10;
        if (lo.contains ("reverb") || lo.contains ("room") || lo.contains ("damping")) return 11;
        if (lo.contains ("chorus") || lo.contains ("flanger") || lo.contains ("phaser")) return 12;
        return 99;
    };

    for (auto& pluginInfo : plugins)
    {
        std::stable_sort (pluginInfo.parameters.begin(), pluginInfo.parameters.end(),
                          [&] (const AutomatablePluginInfo::ParamInfo& a,
                               const AutomatablePluginInfo::ParamInfo& b)
                          {
                              // Already-automated params always come first
                              if (a.hasAutomation != b.hasAutomation)
                                  return a.hasAutomation;
                              int pa = getParamPriority (a.name);
                              int pb = getParamPriority (b.name);
                              if (pa != pb) return pa < pb;
                              return a.name.compareIgnoreCase (b.name) < 0;
                          });
    }

    // Cache the result so subsequent visits to this track are instant.
    automationPluginCache[cursorTrack] = plugins;
    automationPanel->setAvailablePlugins (plugins);
}

void MainComponent::navigateToAutomationParam (const juce::String& pluginId, int paramIndex)
{
    // Show automation panel if hidden
    if (! automationPanelVisible)
    {
        automationPanelVisible = true;
        toolbar->setAutomationPanelVisible (true);
        if (automationPanel != nullptr)
        {
            auto& pat = patternData.getCurrentPattern();
            automationPanel->setAutomationData (&pat.automationData);
            automationPanel->setPatternLength (pat.numRows);
            automationPanel->setCurrentTrack (trackerGrid->getCursorTrack());

            // Lightweight population for auto-learn navigation: avoid expensive
            // full plugin scans and parameter-name queries while a plugin UI is active.
            std::vector<AutomatablePluginInfo> plugins;
            AutomatablePluginInfo pluginInfo;
            pluginInfo.pluginId = pluginId;
            pluginInfo.owningTrack = trackerGrid->getCursorTrack();

            if (pluginId.startsWith ("inst:"))
            {
                int instIdx = pluginId.substring (5).getIntValue();
                const auto& slotInfo = trackerEngine.getInstrumentSlotInfo (instIdx);
                pluginInfo.owningTrack = slotInfo.ownerTrack;
                pluginInfo.isInstrument = true;
                auto name = slotInfo.pluginDescription.name;
                if (name.isEmpty())
                    name = "Instrument";
                pluginInfo.displayName = name + " (Inst " + juce::String (instIdx) + ")";
            }
            else if (pluginId.startsWith ("insert:"))
            {
                auto parts = juce::StringArray::fromTokens (pluginId.substring (7), ":", "");
                int trackIdx = parts.size() > 0 ? parts[0].getIntValue() : trackerGrid->getCursorTrack();
                int slotIdx = parts.size() > 1 ? parts[1].getIntValue() : 0;
                pluginInfo.owningTrack = trackIdx;
                pluginInfo.isInstrument = false;

                juce::String name = "Insert";
                if (trackIdx >= 0 && trackIdx < static_cast<int> (mixerState.insertSlots.size()))
                {
                    auto& slots = mixerState.insertSlots[static_cast<size_t> (trackIdx)];
                    if (slotIdx >= 0 && slotIdx < static_cast<int> (slots.size())
                        && slots[static_cast<size_t> (slotIdx)].pluginName.isNotEmpty())
                    {
                        name = slots[static_cast<size_t> (slotIdx)].pluginName;
                    }
                }
                pluginInfo.displayName = name + " (Insert " + juce::String (slotIdx + 1) + ")";
            }
            else
            {
                pluginInfo.displayName = "Plugin";
            }

            if (paramIndex >= 0)
            {
                AutomatablePluginInfo::ParamInfo paramInfo;
                paramInfo.index = paramIndex;

                // Get the real parameter name from the plugin instance.
                // getName() is safe without the callback lock.
                juce::String realName;
                auto* resolvedPlugin = trackerEngine.resolvePluginInstance (pluginId);
                if (resolvedPlugin != nullptr)
                {
                    auto& params = resolvedPlugin->getParameters();
                    if (paramIndex < params.size() && params[paramIndex] != nullptr)
                        realName = params[paramIndex]->getName (40);
                }
                paramInfo.name = realName.isNotEmpty() ? realName
                                                       : ("Param " + juce::String (paramIndex));
                pluginInfo.parameters.push_back (paramInfo);
            }

            // Invalidate cache for this track so the next full populate
            // picks up the newly automated parameter correctly.
            invalidateAutomationPluginCache (pluginInfo.owningTrack);

            if (! pluginInfo.parameters.empty())
            {
                plugins.push_back (std::move (pluginInfo));
                automationPanel->setAvailablePlugins (plugins);
            }
            else
            {
                populateAutomationPlugins();
            }
        }
        resized();
    }

    // Navigate to the specified param
    if (automationPanel != nullptr)
        automationPanel->navigateToParam (pluginId, paramIndex);
}
