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
        if (patternData.getNumPatterns() > 1)
        {
            int idx = patternData.getCurrentPatternIndex();
            patternData.removePattern (idx);
            switchToPattern (juce::jmin (idx, patternData.getNumPatterns() - 1));
        }
    };
    toolbar->onPatternLengthClick = [this] { showPatternLengthEditor(); };

    // Create arrangement panel (hidden by default)
    arrangementComponent = std::make_unique<ArrangementComponent> (arrangement, patternData, trackerLookAndFeel);
    addChildComponent (*arrangementComponent);
    arrangementComponent->onSwitchToPattern = [this] (int patIdx)
    {
        switchToPattern (patIdx);
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

    // Create the grid
    trackerGrid = std::make_unique<TrackerGrid> (patternData, trackerLookAndFeel);
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

    // Track header right-click
    trackerGrid->onTrackHeaderRightClick = [this] (int track, juce::Point<int> screenPos)
    {
        showTrackHeaderMenu (track, screenPos);
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

    // Register as key listener on the grid so we get events even when grid has focus
    trackerGrid->addKeyListener (this);
    trackerGrid->addKeyListener (commandManager.getKeyMappings());

    setSize (1280, 720);
    setWantsKeyboardFocus (true);
    trackerGrid->grabKeyboardFocus();
}

MainComponent::~MainComponent()
{
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
   #endif
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

    // Grid fills the rest
    trackerGrid->setBounds (r);
}

bool MainComponent::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    auto keyCode = key.getKeyCode();
    bool cmd = key.getModifiers().isCommandDown();

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

    // Ctrl/Cmd+Right/Left: next/prev pattern
    if (cmd && keyCode == juce::KeyPress::rightKey)
    {
        switchToPattern (patternData.getCurrentPatternIndex() + 1);
        return true;
    }
    if (cmd && keyCode == juce::KeyPress::leftKey)
    {
        switchToPattern (patternData.getCurrentPatternIndex() - 1);
        return true;
    }

    // Ctrl/Cmd+Shift+Right: add new pattern and switch to it
    if (cmd && key.getModifiers().isShiftDown() && keyCode == juce::KeyPress::rightKey)
    {
        patternData.addPattern (patternData.getCurrentPattern().numRows);
        switchToPattern (patternData.getNumPatterns() - 1);
        return true;
    }

    // Ctrl/Cmd+M: toggle mute
    if (cmd && key.getTextCharacter() == 'm')
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

    // Ctrl/Cmd+Shift+S: toggle solo (Cmd+S reserved for save later)
    // Use Ctrl+Shift+M for solo to avoid Cmd+S conflict
    if (cmd && key.getModifiers().isShiftDown() && key.getTextCharacter() == 'M')
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

    // Ctrl/Cmd+Up/Down: change instrument
    if (cmd && keyCode == juce::KeyPress::upKey)
    {
        int inst = trackerGrid->getCurrentInstrument() + 1;
        trackerGrid->setCurrentInstrument (juce::jlimit (0, 255, inst));
        updateStatusBar();
        updateToolbar();
        return true;
    }
    if (cmd && keyCode == juce::KeyPress::downKey)
    {
        int inst = trackerGrid->getCurrentInstrument() - 1;
        trackerGrid->setCurrentInstrument (juce::jlimit (0, 255, inst));
        updateStatusBar();
        updateToolbar();
        return true;
    }

    // F1: help overlay (only if not in octave select range - F1 is octave 0 in grid)
    // We handle F1 here to show help when not entering notes

    // F5: toggle arrangement panel
    if (keyCode == juce::KeyPress::F5Key)
    {
        toggleArrangementPanel();
        return true;
    }

    // F6: toggle PAT/SONG mode
    if (keyCode == juce::KeyPress::F6Key)
    {
        toggleSongMode();
        return true;
    }

    // F9/F10: decrease/increase BPM
    if (keyCode == juce::KeyPress::F9Key)
    {
        trackerEngine.setBpm (trackerEngine.getBpm() - 1.0);
        updateStatusBar();
        updateToolbar();
        return true;
    }
    if (keyCode == juce::KeyPress::F10Key)
    {
        trackerEngine.setBpm (trackerEngine.getBpm() + 1.0);
        updateStatusBar();
        updateToolbar();
        return true;
    }

    // F11/F12: decrease/increase edit step
    if (keyCode == juce::KeyPress::F11Key)
    {
        trackerGrid->setEditStep (juce::jmax (0, trackerGrid->getEditStep() - 1));
        updateStatusBar();
        updateToolbar();
        return true;
    }
    if (keyCode == juce::KeyPress::F12Key)
    {
        trackerGrid->setEditStep (juce::jmin (16, trackerGrid->getEditStep() + 1));
        updateStatusBar();
        updateToolbar();
        return true;
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
        default: return false;
    }
}

//==============================================================================
// MenuBarModel
//==============================================================================

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Edit" };
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
    return menu;
}

//==============================================================================

void MainComponent::timerCallback()
{
    if (trackerEngine.isPlaying())
    {
        int row = trackerEngine.getPlaybackRow (patternData.getCurrentPattern().numRows);
        trackerGrid->setPlaybackRow (row);
        trackerGrid->setPlaying (true);
    }
    else
    {
        trackerGrid->setPlaying (false);
    }
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

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }),
                        [this, track, t] (int result)
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
                        });
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

                              double bpm = 120.0;
                              int rpb = 4;
                              std::map<int, juce::File> samples;

                              auto error = ProjectSerializer::loadFromFile (file, patternData, bpm, rpb, samples);
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
                                                     trackerEngine.getSampler().getLoadedSamples());
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
                                                                          trackerEngine.getSampler().getLoadedSamples());
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
    help << "=== Tracker Adjust Keyboard Shortcuts ===\n\n";
    help << "NAVIGATION\n";
    help << "  Arrow keys      Navigate grid\n";
    help << "  Tab/Shift+Tab   Cycle sub-columns (Note/Inst/Vol/FX)\n";
    help << "  Page Up/Down    Jump 16 rows\n";
    help << "  Home/End        Jump to first/last row\n";
    help << "  Mouse wheel     Scroll vertically (Shift = horizontal)\n\n";
    help << "NOTE ENTRY\n";
    help << "  Z-M, Q-U keys   Enter notes (tracker keyboard layout)\n";
    help << "  F1-F8           Set octave 0-7\n";
    help << "  Backtick (`)    Note-off (===)\n";
    help << "  0-9, A-F        Hex entry for Inst/Vol/FX sub-columns\n";
    help << "  Delete/Bksp     Clear cell or sub-column\n\n";
    help << "PLAYBACK\n";
    help << "  Space           Play/Stop\n";
    help << "  F9/F10          Decrease/Increase BPM\n";
    help << "  F11/F12         Decrease/Increase edit step\n\n";
    help << "PATTERN\n";
    help << "  Cmd+Left/Right  Switch pattern\n";
    help << "  Cmd+Up/Down     Change instrument\n";
    help << "  Cmd+M           Toggle mute on track\n";
    help << "  Cmd+Shift+M     Toggle solo on track\n\n";
    help << "EDITING\n";
    help << "  Cmd+C/X/V       Copy/Cut/Paste\n";
    help << "  Cmd+Z           Undo\n";
    help << "  Cmd+Shift+Z     Redo\n";
    help << "  Shift+Arrow     Select region\n\n";
    help << "FILE\n";
    help << "  Cmd+N           New project\n";
    help << "  Cmd+O           Open project\n";
    help << "  Cmd+S           Save\n";
    help << "  Cmd+Shift+S     Save As\n";
    help << "  Cmd+Shift+O     Load sample\n\n";
    help << "ARRANGEMENT\n";
    help << "  F5              Toggle arrangement panel\n";
    help << "  F6              Toggle PAT/SONG mode\n";
    help << "  Drag audio files onto track headers to load samples\n";

    juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::InfoIcon, "Keyboard Shortcuts", help);
}

void MainComponent::toggleArrangementPanel()
{
    arrangementVisible = ! arrangementVisible;
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
    // Build a combined pattern from the arrangement entries and sync to edit
    // For simplicity, we sync the first pattern in the arrangement
    // A full implementation would concatenate all patterns
    if (arrangement.getNumEntries() > 0)
    {
        auto& entry = arrangement.getEntry (0);
        if (entry.patternIndex >= 0 && entry.patternIndex < patternData.getNumPatterns())
            trackerEngine.syncPatternToEdit (patternData.getPattern (entry.patternIndex));
    }
    else
    {
        // Fall back to current pattern
        trackerEngine.syncPatternToEdit (patternData.getCurrentPattern());
    }
}

void MainComponent::doCopy()
{
    auto& pat = patternData.getCurrentPattern();
    auto& clip = getClipboard();

    if (trackerGrid->hasSelection)
    {
        int minRow, maxRow, minTrack, maxTrack;
        trackerGrid->getSelectionBounds (minRow, maxRow, minTrack, maxTrack);
        clip.copyFromPattern (pat, minRow, maxRow, minTrack, maxTrack);
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
    int destTrack = trackerGrid->getCursorTrack();

    // Build undo records
    std::vector<MultiCellEditAction::CellRecord> records;
    for (int r = 0; r < clip.numRows; ++r)
    {
        int row = destRow + r;
        if (row >= pat.numRows) break;
        for (int t = 0; t < clip.numTracks; ++t)
        {
            int track = destTrack + t;
            if (track >= kNumTracks) break;
            MultiCellEditAction::CellRecord rec;
            rec.row = row;
            rec.track = track;
            rec.oldCell = pat.getCell (row, track);
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
        int minRow, maxRow, minTrack, maxTrack;
        trackerGrid->getSelectionBounds (minRow, maxRow, minTrack, maxTrack);

        std::vector<MultiCellEditAction::CellRecord> records;
        for (int r = minRow; r <= maxRow; ++r)
        {
            for (int t = minTrack; t <= maxTrack; ++t)
            {
                MultiCellEditAction::CellRecord rec;
                rec.row = r;
                rec.track = t;
                rec.oldCell = pat.getCell (r, t);
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
