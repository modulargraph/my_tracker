#include "MainComponent.h"

MainComponent::MainComponent()
{
    setLookAndFeel (&trackerLookAndFeel);

    // Initialise the engine
    trackerEngine.initialise();

    // Create toolbar
    toolbar = std::make_unique<ToolbarComponent> (trackerLookAndFeel);
    addAndMakeVisible (*toolbar);

    toolbar->onAddPattern = [this]
    {
        patternData.addPattern (patternData.getCurrentPattern().numRows);
        switchToPattern (patternData.getNumPatterns() - 1);
    };
    toolbar->onRemovePattern = [this]
    {
        if (patternData.getNumPatterns() <= 1)
            return;

        int idx = patternData.getCurrentPatternIndex();
        auto& pat = patternData.getCurrentPattern();

        // Check if pattern has any data
        bool hasData = false;
        for (int r = 0; r < pat.numRows && ! hasData; ++r)
            for (int t = 0; t < kNumTracks && ! hasData; ++t)
                if (! pat.getCell (r, t).isEmpty())
                    hasData = true;

        auto doRemove = [this, idx]
        {
            patternData.removePattern (idx);
            switchToPattern (juce::jmin (idx, patternData.getNumPatterns() - 1));
            markDirty();
        };

        if (hasData)
        {
            juce::AlertWindow::showOkCancelBox (
                juce::AlertWindow::WarningIcon,
                "Delete Pattern",
                "This pattern contains data. Are you sure you want to delete it?",
                "Delete", "Cancel", nullptr,
                juce::ModalCallbackFunction::create ([doRemove] (int result)
                {
                    if (result == 1)
                        doRemove();
                }));
        }
        else
        {
            doRemove();
        }
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
                if (! hasData)
                {
                    patternData.removePattern (idx);
                    markDirty();
                }
            }
            switchToPattern (idx - 1);
        }
    };
    toolbar->onPatternLengthClick = [this] { showPatternLengthEditor(); };

    toolbar->onLengthDrag = [this] (int delta)
    {
        auto& pat = patternData.getCurrentPattern();
        int newLen = juce::jlimit (1, 256, pat.numRows + delta);
        pat.resize (newLen);
        trackerGrid->setCursorPosition (
            juce::jmin (trackerGrid->getCursorRow(), newLen - 1),
            trackerGrid->getCursorTrack());
        updateToolbar();
        markDirty();
    };

    toolbar->onBpmDrag = [this] (double delta)
    {
        trackerEngine.setBpm (juce::jlimit (20.0, 999.0, trackerEngine.getBpm() + delta));
        updateStatusBar();
        updateToolbar();
    };

    toolbar->onStepDrag = [this] (int delta)
    {
        trackerGrid->setEditStep (juce::jlimit (0, 16, trackerGrid->getEditStep() + delta));
        updateStatusBar();
        updateToolbar();
    };

    toolbar->onOctaveDrag = [this] (int delta)
    {
        trackerGrid->setOctave (juce::jlimit (0, 9, trackerGrid->getOctave() + delta));
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
    };

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
        markDirty();
    };

    // Create instrument panel (right side, visible by default)
    instrumentPanel = std::make_unique<InstrumentPanel> (trackerLookAndFeel);
    addAndMakeVisible (*instrumentPanel);
    instrumentPanel->onInstrumentSelected = [this] (int inst)
    {
        trackerGrid->setCurrentInstrument (inst);
        updateStatusBar();
        updateToolbar();
    };
    instrumentPanel->onLoadSampleRequested = [this] (int inst)
    {
        loadSampleForInstrument (inst);
    };
    instrumentPanel->onEditSampleRequested = [this] (int inst)
    {
        openSampleEditor (inst);
    };

    // Create sample editor (hidden by default)
    sampleEditor = std::make_unique<SampleEditorComponent> (trackerLookAndFeel);
    addChildComponent (*sampleEditor);

    sampleEditor->onParamsChanged = [this] (int inst, const InstrumentParams& params)
    {
        trackerEngine.getSampler().setParams (inst, params);
        auto* track = trackerEngine.getTrack (inst);
        if (track != nullptr)
            trackerEngine.getSampler().applyParams (*track, inst);
        markDirty();
    };
    sampleEditor->onPreviewRequested = [this] (int inst)
    {
        trackerEngine.previewNote (inst, 60);
    };
    sampleEditor->onCloseRequested = [this]
    {
        closeSampleEditor();
    };

    // Create the grid
    trackerGrid = std::make_unique<TrackerGrid> (patternData, trackerLookAndFeel, trackLayout);
    addAndMakeVisible (*trackerGrid);

    // Note preview callback
    trackerGrid->onNoteEntered = [this] (int note, int /*instrument*/)
    {
        trackerEngine.previewNote (trackerGrid->getCursorTrack(), note);
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
                trackerEngine.syncPatternToEdit (patternData.getCurrentPattern());
        }
        markDirty();
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
        auto error = trackerEngine.loadSampleForTrack (track, file);
        if (error.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Load Error", error);
        else
        {
            trackerGrid->trackHasSample[static_cast<size_t> (track)] = true;
            trackerGrid->repaint();
            updateToolbar();
            updateInstrumentPanel();
            markDirty();
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
    addKeyListener (commandManager.getKeyMappings());

    // Register as mac menu bar so Cmd+O goes through the native menu system
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (this);
   #endif

    // Playback cursor update timer
    startTimerHz (30);

    // Register as key listener on the grid and sample editor
    trackerGrid->addKeyListener (this);
    trackerGrid->addKeyListener (commandManager.getKeyMappings());
    sampleEditor->addKeyListener (this);

    setSize (1280, 720);
    setWantsKeyboardFocus (true);
    trackerGrid->grabKeyboardFocus();
}

MainComponent::~MainComponent()
{
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
   #endif
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

    // Toolbar at top
    toolbar->setBounds (r.removeFromTop (ToolbarComponent::kToolbarHeight));

    // Status bar at bottom
    auto statusBar = r.removeFromBottom (24);
    statusLabel.setBounds (statusBar.removeFromLeft (statusBar.getWidth() / 2));

    auto rightStatus = statusBar;
    octaveLabel.setBounds (rightStatus.removeFromLeft (rightStatus.getWidth() / 2));
    bpmLabel.setBounds (rightStatus);

    // Arrangement panel (left side)
    if (arrangementVisible)
    {
        arrangementComponent->setBounds (r.removeFromLeft (ArrangementComponent::kPanelWidth));
        arrangementComponent->setVisible (true);
    }
    else
    {
        arrangementComponent->setVisible (false);
    }

    // Instrument panel (right side)
    if (instrumentPanelVisible)
    {
        instrumentPanel->setBounds (r.removeFromRight (InstrumentPanel::kPanelWidth));
        instrumentPanel->setVisible (true);
    }
    else
    {
        instrumentPanel->setVisible (false);
    }

    // Grid or sample editor fills the rest
    if (sampleEditorVisible)
    {
        sampleEditor->setBounds (r);
        sampleEditor->setVisible (true);
        trackerGrid->setVisible (false);
    }
    else
    {
        trackerGrid->setBounds (r);
        trackerGrid->setVisible (true);
        sampleEditor->setVisible (false);
    }
}

bool MainComponent::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    auto keyCode = key.getKeyCode();
    bool cmd = key.getModifiers().isCommandDown();
    bool shift = key.getModifiers().isShiftDown();
    auto textChar = key.getTextCharacter();

    // When sample editor is open, let it handle keys (except Cmd shortcuts)
    if (sampleEditorVisible)
    {
        if (keyCode == juce::KeyPress::escapeKey)
        {
            closeSampleEditor();
            return true;
        }
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
                trackerEngine.syncPatternToEdit (patternData.getCurrentPattern());
        }

        trackerEngine.togglePlayStop();
        updateStatusBar();
        updateToolbar();
        return true;
    }

    // Cmd+Right/Left: next/prev pattern
    if (cmd && keyCode == juce::KeyPress::rightKey)
    {
        if (shift)
        {
            // Cmd+Shift+Right: add new pattern and switch to it
            patternData.addPattern (patternData.getCurrentPattern().numRows);
            switchToPattern (patternData.getNumPatterns() - 1);
        }
        else
        {
            switchToPattern (patternData.getCurrentPatternIndex() + 1);
        }
        return true;
    }
    if (cmd && keyCode == juce::KeyPress::leftKey)
    {
        switchToPattern (patternData.getCurrentPatternIndex() - 1);
        return true;
    }

    // Cmd+M: toggle mute
    if (cmd && ! shift && textChar == 'm')
    {
        int track = trackerGrid->getCursorTrack();
        auto* t = trackerEngine.getTrack (track);
        if (t != nullptr)
        {
            t->setMute (! t->isMuted (false));
            updateMuteSoloState();
        }
        return true;
    }

    // Cmd+Shift+M: toggle solo
    if (cmd && shift && textChar == 'M')
    {
        int track = trackerGrid->getCursorTrack();
        auto* t = trackerEngine.getTrack (track);
        if (t != nullptr)
        {
            t->setSolo (! t->isSolo (false));
            updateMuteSoloState();
        }
        return true;
    }

    // Cmd+Up/Down: change instrument
    if (cmd && keyCode == juce::KeyPress::upKey)
    {
        int inst = trackerGrid->getCurrentInstrument() + 1;
        trackerGrid->setCurrentInstrument (juce::jlimit (0, 255, inst));
        updateStatusBar();
        updateToolbar();
        instrumentPanel->setSelectedInstrument (inst);
        return true;
    }
    if (cmd && keyCode == juce::KeyPress::downKey)
    {
        int inst = trackerGrid->getCurrentInstrument() - 1;
        trackerGrid->setCurrentInstrument (juce::jlimit (0, 255, inst));
        updateStatusBar();
        updateToolbar();
        instrumentPanel->setSelectedInstrument (inst);
        return true;
    }

    // Cmd+1 through Cmd+8: set octave 0-7 (MacBook-friendly alternative to F1-F8)
    if (cmd && ! shift && textChar >= '1' && textChar <= '8')
    {
        trackerGrid->setOctave (textChar - '1');
        updateStatusBar();
        updateToolbar();
        return true;
    }

    // Cmd+[ / Cmd+]: decrease/increase BPM (MacBook-friendly alternative to F9/F10)
    if (cmd && ! shift && textChar == '[')
    {
        trackerEngine.setBpm (trackerEngine.getBpm() - 1.0);
        updateStatusBar();
        updateToolbar();
        return true;
    }
    if (cmd && ! shift && textChar == ']')
    {
        trackerEngine.setBpm (trackerEngine.getBpm() + 1.0);
        updateStatusBar();
        updateToolbar();
        return true;
    }

    // Cmd+- / Cmd+=: decrease/increase edit step (MacBook-friendly alternative to F11/F12)
    if (cmd && ! shift && textChar == '-')
    {
        trackerGrid->setEditStep (juce::jmax (0, trackerGrid->getEditStep() - 1));
        updateStatusBar();
        updateToolbar();
        return true;
    }
    if (cmd && ! shift && textChar == '=')
    {
        trackerGrid->setEditStep (juce::jmin (16, trackerGrid->getEditStep() + 1));
        updateStatusBar();
        updateToolbar();
        return true;
    }

    // F-key alternatives (still work if user holds Fn)
    if (keyCode == juce::KeyPress::F5Key)  { toggleArrangementPanel(); return true; }
    if (keyCode == juce::KeyPress::F6Key)  { toggleSongMode(); return true; }

    if (keyCode == juce::KeyPress::F9Key)
    {
        trackerEngine.setBpm (trackerEngine.getBpm() - 1.0);
        updateStatusBar(); updateToolbar(); return true;
    }
    if (keyCode == juce::KeyPress::F10Key)
    {
        trackerEngine.setBpm (trackerEngine.getBpm() + 1.0);
        updateStatusBar(); updateToolbar(); return true;
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
            break;
        case cmdRedo:
            result.setInfo ("Redo", "Redo last undone action", "Edit", 0);
            result.addDefaultKeypress ('Z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
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
            break;
        case cmdSaveAs:
            result.setInfo ("Save As...", "Save project to a new file", "File", 0);
            result.addDefaultKeypress ('S', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
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
            return true;
        case muteTrack:
        {
            int track = trackerGrid->getCursorTrack();
            auto* t = trackerEngine.getTrack (track);
            if (t) { t->setMute (! t->isMuted (false)); updateMuteSoloState(); }
            return true;
        }
        case soloTrack:
        {
            int track = trackerGrid->getCursorTrack();
            auto* t = trackerEngine.getTrack (track);
            if (t) { t->setSolo (! t->isSolo (false)); updateMuteSoloState(); }
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
            undoManager.undo();
            trackerGrid->repaint();
            return true;
        case cmdRedo:
            undoManager.redo();
            trackerGrid->repaint();
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
    auto track = juce::String::formatted ("%02d", trackerGrid->getCursorTrack() + 1);

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
    toolbar->setPatternInfo (patternData.getCurrentPatternIndex(), patternData.getNumPatterns(), pat.name);
    toolbar->setPatternLength (pat.numRows);
    toolbar->setInstrument (trackerGrid->getCurrentInstrument());
    toolbar->setOctave (trackerGrid->getOctave());
    toolbar->setEditStep (trackerGrid->getEditStep());
    toolbar->setBpm (trackerEngine.getBpm());
    toolbar->setPlayState (trackerEngine.isPlaying());
    toolbar->setPlaybackMode (songMode);

    // Show sample name for current track
    auto sampleFile = trackerEngine.getSampler().getSampleFile (trackerGrid->getCursorTrack());
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
                                  int trackIdx = trackerGrid->getCursorTrack();
                                  auto error = trackerEngine.loadSampleForTrack (trackIdx, file);
                                  if (error.isNotEmpty())
                                      juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                                              "Load Error", error);
                                  else
                                  {
                                      trackerGrid->trackHasSample[static_cast<size_t> (trackIdx)] = true;
                                      trackerGrid->repaint();
                                      updateToolbar();
                                      updateInstrumentPanel();
                                  }
                              }
                          });
}

void MainComponent::switchToPattern (int index)
{
    index = juce::jlimit (0, patternData.getNumPatterns() - 1, index);
    patternData.setCurrentPattern (index);
    trackerGrid->setCursorPosition (0, trackerGrid->getCursorTrack());
    trackerGrid->repaint();
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
            patternData.getCurrentPattern().resize (newLen);
            trackerGrid->setCursorPosition (
                juce::jmin (trackerGrid->getCursorRow(), newLen - 1),
                trackerGrid->getCursorTrack());
            trackerGrid->repaint();
            updateToolbar();
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

    int groupIdx = trackLayout.getGroupForTrack (track);
    if (groupIdx >= 0)
    {
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
                            }
                            else if (result == 2 && t)
                            {
                                t->setSolo (! t->isSolo (false));
                                updateMuteSoloState();
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
                                trackLayout.moveVisualRange (rangeStart, rangeEnd, -1);
                                markDirty();
                                trackerGrid->repaint();
                            }
                            else if (result == 11)
                            {
                                trackLayout.moveVisualRange (rangeStart, rangeEnd, +1);
                                markDirty();
                                trackerGrid->repaint();
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
                                        trackLayout.createGroup (name, rangeStart, rangeEnd);
                                        markDirty();
                                        trackerGrid->repaint();
                                    }
                                    delete aw;
                                }), true);
                            }
                            else if (result == 13 && groupIdx >= 0)
                            {
                                // Remove this track from its group
                                auto& group = trackLayout.getGroup (groupIdx);
                                group.trackIndices.erase (
                                    std::remove (group.trackIndices.begin(), group.trackIndices.end(), track),
                                    group.trackIndices.end());
                                if (group.trackIndices.empty())
                                    trackLayout.removeGroup (groupIdx);
                                markDirty();
                                trackerGrid->repaint();
                            }
                            else if (result == 14 && groupIdx >= 0)
                            {
                                trackLayout.removeGroup (groupIdx);
                                markDirty();
                                trackerGrid->repaint();
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
    patternData.addPattern (64);
    arrangement.clear();
    trackLayout.resetToDefault();
    arrangementComponent->setSelectedEntry (-1);
    trackerGrid->setCursorPosition (0, 0);
    trackerGrid->clearSelection();
    for (int i = 0; i < kNumTracks; ++i)
    {
        trackerGrid->trackMuted[static_cast<size_t> (i)] = false;
        trackerGrid->trackSoloed[static_cast<size_t> (i)] = false;
        trackerGrid->trackHasSample[static_cast<size_t> (i)] = false;
    }
    trackerEngine.setBpm (120.0);
    undoManager.clearUndoHistory();
    currentProjectFile = juce::File();
    isDirty = false;
    updateWindowTitle();
    updateStatusBar();
    updateToolbar();
    updateInstrumentPanel();
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

                              auto error = ProjectSerializer::loadFromFile (file, patternData, bpm, rpb, samples, instParams, arrangement, trackLayout);
                              if (error.isNotEmpty())
                              {
                                  juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                                          "Load Error", error);
                                  return;
                              }

                              trackerEngine.setBpm (bpm);
                              trackerEngine.setRowsPerBeat (rpb);

                              // Reload samples
                              trackerEngine.getSampler().clearLoadedSamples();
                              for (int i = 0; i < kNumTracks; ++i)
                                  trackerGrid->trackHasSample[static_cast<size_t> (i)] = false;

                              for (auto& [index, sampleFile] : samples)
                              {
                                  if (index < kNumTracks)
                                  {
                                      trackerEngine.loadSampleForTrack (index, sampleFile);
                                      trackerGrid->trackHasSample[static_cast<size_t> (index)] = true;
                                  }
                              }

                              // Restore instrument params and apply processing
                              for (auto& [index, params] : instParams)
                              {
                                  trackerEngine.getSampler().setParams (index, params);
                                  if (! params.isDefault())
                                  {
                                      auto* track = trackerEngine.getTrack (index);
                                      if (track != nullptr)
                                          trackerEngine.getSampler().applyParams (*track, index);
                                  }
                              }

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
                                                     trackLayout);
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
                                                                          trackLayout);
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
    juce::String help;
    help << "=== Keyboard Shortcuts ===\n\n";
    help << "NAVIGATION\n";
    help << "  Arrow keys        Navigate grid\n";
    help << "  Tab / Shift+Tab   Cycle sub-columns (Note/Inst/Vol/FX)\n";
    help << "  Fn+Up / Fn+Down   Jump 16 rows (Page Up/Down)\n";
    help << "  Fn+Left / Fn+Right  First/last row (Home/End)\n";
    help << "  Mouse wheel       Scroll (Shift = horizontal)\n\n";
    help << "NOTE ENTRY\n";
    help << "  Z-M, Q-U keys    Enter notes (tracker layout)\n";
    help << "  Cmd+1 to Cmd+8   Set octave 0-7\n";
    help << "  Backtick (`)      Note-off (===)\n";
    help << "  0-9, A-F          Hex entry (Inst/Vol/FX columns)\n";
    help << "  Backspace         Clear cell or sub-column\n\n";
    help << "PLAYBACK\n";
    help << "  Space             Play / Stop\n";
    help << "  Cmd+[ / Cmd+]     Decrease / Increase BPM\n";
    help << "  Cmd+- / Cmd+=     Decrease / Increase edit step\n\n";
    help << "PATTERN & TRACKS\n";
    help << "  Cmd+Left/Right    Switch pattern\n";
    help << "  Cmd+Shift+Right   Add new pattern\n";
    help << "  Cmd+Up/Down       Change instrument\n";
    help << "  Cmd+M             Mute track\n";
    help << "  Cmd+Shift+M       Solo track\n\n";
    help << "EDITING\n";
    help << "  Cmd+C / X / V     Copy / Cut / Paste\n";
    help << "  Cmd+Z             Undo\n";
    help << "  Cmd+Shift+Z       Redo\n";
    help << "  Shift+Arrow       Select region\n\n";
    help << "FILE\n";
    help << "  Cmd+N             New project\n";
    help << "  Cmd+O             Open project\n";
    help << "  Cmd+S             Save\n";
    help << "  Cmd+Shift+S       Save As\n";
    help << "  Cmd+Shift+O       Load sample\n\n";
    help << "VIEW\n";
    help << "  Cmd+Shift+A       Toggle arrangement panel\n";
    help << "  Cmd+Shift+I       Toggle instrument panel\n";
    help << "  Cmd+Shift+P       Toggle PAT / SONG mode\n";
    help << "  Cmd+/             Show this help\n\n";
    help << "Drag audio files onto track headers to load samples.\n";

    juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::InfoIcon, "Keyboard Shortcuts", help);
}

void MainComponent::toggleArrangementPanel()
{
    arrangementVisible = ! arrangementVisible;
    toolbar->setArrangementVisible (arrangementVisible);
    resized();
}

void MainComponent::toggleSongMode()
{
    songMode = ! songMode;
    toolbar->setPlaybackMode (songMode);
    updateToolbar();
}

void MainComponent::syncArrangementToEdit()
{
    if (arrangement.getNumEntries() == 0)
    {
        // Fall back to current pattern
        trackerEngine.syncPatternToEdit (patternData.getCurrentPattern());
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
        trackerEngine.syncPatternToEdit (patternData.getCurrentPattern());
        return;
    }

    trackerEngine.syncArrangementToEdit (sequence, trackerEngine.getRowsPerBeat());
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
        clip.numRows = maxRow - minRow + 1;
        clip.numTracks = maxViTrack - minViTrack + 1;
        clip.cells.resize (static_cast<size_t> (clip.numRows));
        for (int r = 0; r < clip.numRows; ++r)
        {
            clip.cells[static_cast<size_t> (r)].resize (static_cast<size_t> (clip.numTracks));
            for (int t = 0; t < clip.numTracks; ++t)
            {
                int phys = trackLayout.visualToPhysical (minViTrack + t);
                clip.cells[static_cast<size_t> (r)][static_cast<size_t> (t)] =
                    pat.getCell (minRow + r, phys);
            }
        }
    }
    else
    {
        // Copy single cell at cursor
        clip.copyFromPattern (pat, trackerGrid->getCursorRow(), trackerGrid->getCursorRow(),
                              trackerGrid->getCursorTrack(), trackerGrid->getCursorTrack());
    }
}

void MainComponent::doPaste()
{
    auto& clip = getClipboard();
    if (clip.isEmpty()) return;

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

    undoManager.perform (new MultiCellEditAction (pat, std::move (records)));
    trackerGrid->repaint();
}

void MainComponent::doCut()
{
    doCopy();

    auto& pat = patternData.getCurrentPattern();

    if (trackerGrid->hasSelection)
    {
        // Selection bounds are in visual space
        int minRow, maxRow, minViTrack, maxViTrack;
        trackerGrid->getSelectionBounds (minRow, maxRow, minViTrack, maxViTrack);

        std::vector<MultiCellEditAction::CellRecord> records;
        for (int r = minRow; r <= maxRow; ++r)
        {
            for (int vi = minViTrack; vi <= maxViTrack; ++vi)
            {
                int phys = trackLayout.visualToPhysical (vi);
                MultiCellEditAction::CellRecord rec;
                rec.row = r;
                rec.track = phys;
                rec.oldCell = pat.getCell (r, phys);
                rec.newCell = Cell{}; // cleared
                records.push_back (rec);
            }
        }
        undoManager.perform (new MultiCellEditAction (pat, std::move (records)));
        trackerGrid->clearSelection();
    }
    else
    {
        int r = trackerGrid->getCursorRow();
        int t = trackerGrid->getCursorTrack();
        Cell empty;
        undoManager.perform (new CellEditAction (pat, r, t, empty));
    }

    trackerGrid->repaint();
}

void MainComponent::updateInstrumentPanel()
{
    instrumentPanel->updateSampleInfo (trackerEngine.getSampler().getLoadedSamples());
    instrumentPanel->setSelectedInstrument (trackerGrid->getCurrentInstrument());
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
                                  auto error = trackerEngine.loadSampleForTrack (instrument, file);
                                  if (error.isNotEmpty())
                                      juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                                              "Load Error", error);
                                  else
                                  {
                                      if (instrument < kNumTracks)
                                          trackerGrid->trackHasSample[static_cast<size_t> (instrument)] = true;
                                      trackerGrid->repaint();
                                      updateToolbar();
                                      updateInstrumentPanel();
                                      markDirty();
                                  }
                              }
                          });
}

void MainComponent::openSampleEditor (int instrument)
{
    auto sampleFile = trackerEngine.getSampler().getSampleFile (instrument);
    if (! sampleFile.existsAsFile())
        return;

    auto params = trackerEngine.getSampler().getParams (instrument);
    sampleEditor->open (instrument, sampleFile, params);
    sampleEditorVisible = true;
    resized();
    sampleEditor->grabKeyboardFocus();
}

void MainComponent::closeSampleEditor()
{
    sampleEditor->close();
    sampleEditorVisible = false;
    resized();
    trackerGrid->grabKeyboardFocus();
}

void MainComponent::updateMuteSoloState()
{
    for (int i = 0; i < kNumTracks; ++i)
    {
        auto* t = trackerEngine.getTrack (i);
        if (t != nullptr)
        {
            trackerGrid->trackMuted[static_cast<size_t> (i)] = t->isMuted (false);
            trackerGrid->trackSoloed[static_cast<size_t> (i)] = t->isSolo (false);
        }
    }
    trackerGrid->repaint();
}
