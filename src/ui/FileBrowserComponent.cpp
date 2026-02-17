#include "FileBrowserComponent.h"

SampleBrowserComponent::SampleBrowserComponent (TrackerLookAndFeel& lnf)
    : lookAndFeel (lnf)
{
    setWantsKeyboardFocus (true);
    currentDirectory = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    refreshFileList();
}

//==============================================================================
// Layout helpers
//==============================================================================

juce::Rectangle<int> SampleBrowserComponent::getFilePaneBounds() const
{
    auto r = getLocalBounds().withTrimmedBottom (kInfoBarHeight);
    return r.removeFromLeft (static_cast<int> (r.getWidth() * kFilePaneRatio));
}

juce::Rectangle<int> SampleBrowserComponent::getInstrumentPaneBounds() const
{
    auto r = getLocalBounds().withTrimmedBottom (kInfoBarHeight);
    r.removeFromLeft (static_cast<int> (r.getWidth() * kFilePaneRatio));
    return r;
}

juce::Rectangle<int> SampleBrowserComponent::getInfoBarBounds() const
{
    return getLocalBounds().removeFromBottom (kInfoBarHeight);
}

int SampleBrowserComponent::getFileVisibleRows() const
{
    auto bounds = getFilePaneBounds();
    return juce::jmax (1, (bounds.getHeight() - kHeaderHeight) / kRowHeight);
}

int SampleBrowserComponent::getInstrumentVisibleRows() const
{
    auto bounds = getInstrumentPaneBounds();
    return juce::jmax (1, (bounds.getHeight() - kHeaderHeight) / kRowHeight);
}

//==============================================================================
// File list management
//==============================================================================

bool SampleBrowserComponent::isAudioFile (const juce::File& f) const
{
    auto ext = f.getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".aiff" || ext == ".aif"
        || ext == ".flac" || ext == ".ogg" || ext == ".mp3";
}

juce::String SampleBrowserComponent::formatFileSize (int64_t bytes) const
{
    if (bytes < 1024)
        return juce::String (bytes) + "B";
    if (bytes < 1024 * 1024)
        return juce::String (bytes / 1024) + "KB";
    return juce::String::formatted ("%.1fMB", bytes / (1024.0 * 1024.0));
}

void SampleBrowserComponent::refreshFileList()
{
    fileEntries.clear();

    if (! currentDirectory.isRoot())
    {
        FileEntry parent;
        parent.name = "..";
        parent.file = currentDirectory.getParentDirectory();
        parent.isDirectory = true;
        parent.isParent = true;
        fileEntries.push_back (parent);
    }

    juce::Array<juce::File> children;
    currentDirectory.findChildFiles (children,
        juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles, false);

    // Sort: directories first, then alphabetical
    children.sort();
    std::stable_sort (children.begin(), children.end(),
        [] (const juce::File& a, const juce::File& b)
        {
            if (a.isDirectory() != b.isDirectory())
                return a.isDirectory();
            return false;
        });

    for (auto& child : children)
    {
        if (child.isDirectory() || isAudioFile (child))
        {
            FileEntry entry;
            entry.name = child.getFileName();
            entry.file = child;
            entry.isDirectory = child.isDirectory();

            if (! entry.isDirectory)
            {
                entry.sizeStr = formatFileSize (child.getSize());
                entry.formatStr = child.getFileExtension().toUpperCase().trimCharactersAtStart (".");
            }

            fileEntries.push_back (entry);
        }
    }

    fileSelection = 0;
    fileScrollOffset = 0;
    repaint();
}

void SampleBrowserComponent::setCurrentDirectory (const juce::File& dir)
{
    if (dir.isDirectory())
    {
        currentDirectory = dir;
        refreshFileList();

        if (onDirectoryChanged)
            onDirectoryChanged (dir);
    }
}

void SampleBrowserComponent::navigateInto (const juce::File& dir)
{
    setCurrentDirectory (dir);
}

void SampleBrowserComponent::loadSelectedFile()
{
    if (fileSelection < 0 || fileSelection >= static_cast<int> (fileEntries.size()))
        return;

    auto& entry = fileEntries[static_cast<size_t> (fileSelection)];

    if (entry.isDirectory)
    {
        navigateInto (entry.file);
        return;
    }

    // Load audio file into the selected instrument slot
    if (onLoadSample)
        onLoadSample (instrumentSelection, entry.file);
}

void SampleBrowserComponent::updateInstrumentSlots (const std::map<int, juce::File>& loadedSamples)
{
    for (auto& slot : instrumentSlots)
    {
        slot.sampleName.clear();
        slot.hasData = false;
    }

    for (auto& [index, file] : loadedSamples)
    {
        if (index >= 0 && index < 256)
        {
            instrumentSlots[static_cast<size_t> (index)].sampleName = file.getFileNameWithoutExtension();
            instrumentSlots[static_cast<size_t> (index)].hasData = true;
        }
    }

    repaint();
}

void SampleBrowserComponent::setSelectedInstrument (int inst)
{
    instrumentSelection = juce::jlimit (0, 255, inst);
    ensureInstrumentSelectionVisible();
    repaint();
}

//==============================================================================
// Paint
//==============================================================================

void SampleBrowserComponent::paint (juce::Graphics& g)
{
    auto bg = lookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId);
    g.fillAll (bg);

    paintFilePane (g, getFilePaneBounds());
    paintInstrumentPane (g, getInstrumentPaneBounds());
    paintInfoBar (g, getInfoBarBounds());

    // Divider between panes
    auto fileBounds = getFilePaneBounds();
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawVerticalLine (fileBounds.getRight(), 0.0f,
                        static_cast<float> (getHeight() - kInfoBarHeight));
}

void SampleBrowserComponent::paintFilePane (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    auto textCol = lookAndFeel.findColour (TrackerLookAndFeel::textColourId);
    auto accentCol = lookAndFeel.findColour (TrackerLookAndFeel::fxColourId);
    bool isActive = (activePane == Pane::Files);

    // Header
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::headerColourId));
    g.fillRect (bounds.getX(), bounds.getY(), bounds.getWidth(), kHeaderHeight);

    g.setFont (lookAndFeel.getMonoFont (11.0f));
    g.setColour (isActive ? textCol : textCol.withAlpha (0.5f));

    auto pathStr = currentDirectory.getFullPathName();
    if (pathStr.length() > 40)
        pathStr = "..." + pathStr.substring (pathStr.length() - 37);
    g.drawText ("FILES \xe2\x80\x94 " + pathStr,
                bounds.getX() + 6, bounds.getY(), bounds.getWidth() - 12, kHeaderHeight,
                juce::Justification::centredLeft);

    // Active pane indicator
    if (isActive)
    {
        g.setColour (accentCol);
        g.fillRect (bounds.getX(), bounds.getY() + kHeaderHeight - 2, bounds.getWidth(), 2);
    }

    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (bounds.getY() + kHeaderHeight - 1,
                          static_cast<float> (bounds.getX()),
                          static_cast<float> (bounds.getRight()));

    // File rows
    int visibleRows = getFileVisibleRows();
    g.setFont (lookAndFeel.getMonoFont (11.0f));

    for (int i = 0; i < visibleRows; ++i)
    {
        int idx = fileScrollOffset + i;
        if (idx >= static_cast<int> (fileEntries.size())) break;

        auto& entry = fileEntries[static_cast<size_t> (idx)];
        int y = bounds.getY() + kHeaderHeight + i * kRowHeight;

        // Selection highlight
        if (idx == fileSelection && isActive)
        {
            g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::cursorCellColourId));
            g.fillRect (bounds.getX(), y, bounds.getWidth(), kRowHeight);
        }
        else if (idx == fileSelection)
        {
            g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::cursorCellColourId).withAlpha (0.3f));
            g.fillRect (bounds.getX(), y, bounds.getWidth(), kRowHeight);
        }

        int textX = bounds.getX() + 6;

        if (entry.isDirectory)
        {
            // Directory indicator + name
            g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::instrumentColourId));
            g.drawText ("[D]", textX, y, 24, kRowHeight, juce::Justification::centredLeft);
            textX += 26;
            g.setColour (textCol);
            g.drawText (entry.name, textX, y, bounds.getWidth() - textX - 6, kRowHeight,
                        juce::Justification::centredLeft);
        }
        else
        {
            // Audio file: name + size + format
            g.setColour (textCol);
            int nameWidth = bounds.getWidth() - 120;
            auto displayName = entry.name;
            if (displayName.length() > 30)
                displayName = displayName.substring (0, 27) + "...";
            g.drawText (displayName, textX, y, nameWidth, kRowHeight,
                        juce::Justification::centredLeft);

            // Size
            g.setColour (textCol.withAlpha (0.5f));
            g.drawText (entry.sizeStr, bounds.getRight() - 110, y, 50, kRowHeight,
                        juce::Justification::centredRight);

            // Format
            g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::fxColourId).withAlpha (0.6f));
            g.drawText (entry.formatStr, bounds.getRight() - 54, y, 48, kRowHeight,
                        juce::Justification::centredRight);
        }

        // Row separator
        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId).withAlpha (0.3f));
        g.drawHorizontalLine (y + kRowHeight - 1,
                              static_cast<float> (bounds.getX()),
                              static_cast<float> (bounds.getRight()));
    }
}

void SampleBrowserComponent::paintInstrumentPane (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    auto textCol = lookAndFeel.findColour (TrackerLookAndFeel::textColourId);
    auto accentCol = lookAndFeel.findColour (TrackerLookAndFeel::fxColourId);
    bool isActive = (activePane == Pane::Instruments);

    // Header
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::headerColourId));
    g.fillRect (bounds.getX(), bounds.getY(), bounds.getWidth(), kHeaderHeight);

    g.setFont (lookAndFeel.getMonoFont (11.0f));
    g.setColour (isActive ? textCol : textCol.withAlpha (0.5f));
    g.drawText ("INSTRUMENTS", bounds.getX() + 6, bounds.getY(),
                bounds.getWidth() - 12, kHeaderHeight, juce::Justification::centredLeft);

    if (isActive)
    {
        g.setColour (accentCol);
        g.fillRect (bounds.getX(), bounds.getY() + kHeaderHeight - 2, bounds.getWidth(), 2);
    }

    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (bounds.getY() + kHeaderHeight - 1,
                          static_cast<float> (bounds.getX()),
                          static_cast<float> (bounds.getRight()));

    // Instrument slots
    int visibleRows = getInstrumentVisibleRows();
    g.setFont (lookAndFeel.getMonoFont (11.0f));

    for (int i = 0; i < visibleRows; ++i)
    {
        int idx = instrumentScrollOffset + i;
        if (idx >= 256) break;

        auto& slot = instrumentSlots[static_cast<size_t> (idx)];
        int y = bounds.getY() + kHeaderHeight + i * kRowHeight;

        // Selection highlight
        if (idx == instrumentSelection && isActive)
        {
            g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::cursorCellColourId));
            g.fillRect (bounds.getX(), y, bounds.getWidth(), kRowHeight);
        }
        else if (idx == instrumentSelection)
        {
            g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::cursorCellColourId).withAlpha (0.3f));
            g.fillRect (bounds.getX(), y, bounds.getWidth(), kRowHeight);
        }

        // Hex index
        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::instrumentColourId)
                         .withAlpha (slot.hasData ? 1.0f : 0.4f));
        g.drawText (juce::String::formatted ("%02X", idx),
                    bounds.getX() + 6, y, 22, kRowHeight, juce::Justification::centredLeft);

        // Sample name or empty
        if (slot.hasData)
        {
            g.setColour (textCol);
            auto truncName = slot.sampleName.substring (0, 20);
            g.drawText (truncName, bounds.getX() + 32, y, bounds.getWidth() - 38, kRowHeight,
                        juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (textCol.withAlpha (0.2f));
            g.drawText ("---", bounds.getX() + 32, y, bounds.getWidth() - 38, kRowHeight,
                        juce::Justification::centredLeft);
        }

        // Row separator
        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId).withAlpha (0.3f));
        g.drawHorizontalLine (y + kRowHeight - 1,
                              static_cast<float> (bounds.getX()),
                              static_cast<float> (bounds.getRight()));
    }
}

void SampleBrowserComponent::paintInfoBar (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::headerColourId).darker (0.1f));
    g.fillRect (bounds);

    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (bounds.getY(), 0.0f, static_cast<float> (getWidth()));

    g.setFont (lookAndFeel.getMonoFont (11.0f));
    auto textCol = lookAndFeel.findColour (TrackerLookAndFeel::textColourId);

    // Left: selected file info
    juce::String info;
    if (fileSelection >= 0 && fileSelection < static_cast<int> (fileEntries.size()))
    {
        auto& entry = fileEntries[static_cast<size_t> (fileSelection)];
        if (entry.isDirectory)
            info = entry.isParent ? "Parent directory" : "Directory: " + entry.name;
        else
            info = entry.name + "  " + entry.sizeStr + "  " + entry.formatStr;
    }

    g.setColour (textCol.withAlpha (0.7f));
    g.drawText (info, bounds.getX() + 8, bounds.getY(), bounds.getWidth() / 2, bounds.getHeight(),
                juce::Justification::centredLeft);

    // Center: hint
    juce::String hint;
    if (activePane == Pane::Files)
    {
        if (fileSelection >= 0 && fileSelection < static_cast<int> (fileEntries.size()))
        {
            auto& entry = fileEntries[static_cast<size_t> (fileSelection)];
            if (entry.isDirectory)
                hint = "Enter: Open folder";
            else
                hint = juce::String::formatted ("Enter: Load -> slot %02X", instrumentSelection);
        }
    }
    else
    {
        hint = juce::String::formatted ("Slot %02X selected", instrumentSelection);
    }

    int checkboxWidth = 120;
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::fxColourId).withAlpha (0.6f));
    g.drawText (hint, bounds.getX(), bounds.getY(),
                bounds.getWidth() - checkboxWidth - 12, bounds.getHeight(),
                juce::Justification::centredRight);

    // Right: auto-advance checkbox
    int cbX = bounds.getRight() - checkboxWidth - 4;
    int cbY = bounds.getY() + (bounds.getHeight() - 12) / 2;

    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawRect (cbX, cbY, 12, 12, 1);

    if (autoAdvance)
    {
        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::fxColourId));
        g.fillRect (cbX + 2, cbY + 2, 8, 8);
    }

    g.setColour (textCol.withAlpha (0.6f));
    g.drawText ("Auto-advance", cbX + 16, bounds.getY(), checkboxWidth - 16, bounds.getHeight(),
                juce::Justification::centredLeft);
}

//==============================================================================
// Keyboard
//==============================================================================

bool SampleBrowserComponent::keyPressed (const juce::KeyPress& key)
{
    auto keyCode = key.getKeyCode();

    // Left/Right: switch pane
    if (keyCode == juce::KeyPress::leftKey)
    {
        if (activePane != Pane::Files)
        {
            activePane = Pane::Files;
            triggerPreviewForSelection();
        }
        repaint();
        return true;
    }
    if (keyCode == juce::KeyPress::rightKey)
    {
        if (activePane != Pane::Instruments)
        {
            activePane = Pane::Instruments;
            triggerInstrumentPreview();
        }
        repaint();
        return true;
    }

    // Up/Down: navigate within active pane
    if (keyCode == juce::KeyPress::upKey)
    {
        if (activePane == Pane::Files)
        {
            fileSelection = juce::jmax (0, fileSelection - 1);
            ensureFileSelectionVisible();
            triggerPreviewForSelection();
        }
        else
        {
            instrumentSelection = juce::jmax (0, instrumentSelection - 1);
            ensureInstrumentSelectionVisible();
            triggerInstrumentPreview();
        }
        repaint();
        return true;
    }
    if (keyCode == juce::KeyPress::downKey)
    {
        if (activePane == Pane::Files)
        {
            fileSelection = juce::jmin (static_cast<int> (fileEntries.size()) - 1, fileSelection + 1);
            ensureFileSelectionVisible();
            triggerPreviewForSelection();
        }
        else
        {
            instrumentSelection = juce::jmin (255, instrumentSelection + 1);
            ensureInstrumentSelectionVisible();
            triggerInstrumentPreview();
        }
        repaint();
        return true;
    }

    // Page up/down
    if (keyCode == juce::KeyPress::pageUpKey)
    {
        if (activePane == Pane::Files)
        {
            fileSelection = juce::jmax (0, fileSelection - getFileVisibleRows());
            ensureFileSelectionVisible();
            triggerPreviewForSelection();
        }
        else
        {
            instrumentSelection = juce::jmax (0, instrumentSelection - getInstrumentVisibleRows());
            ensureInstrumentSelectionVisible();
            triggerInstrumentPreview();
        }
        repaint();
        return true;
    }
    if (keyCode == juce::KeyPress::pageDownKey)
    {
        if (activePane == Pane::Files)
        {
            fileSelection = juce::jmin (static_cast<int> (fileEntries.size()) - 1,
                                        fileSelection + getFileVisibleRows());
            ensureFileSelectionVisible();
            triggerPreviewForSelection();
        }
        else
        {
            instrumentSelection = juce::jmin (255, instrumentSelection + getInstrumentVisibleRows());
            ensureInstrumentSelectionVisible();
            triggerInstrumentPreview();
        }
        repaint();
        return true;
    }

    // Enter: open dir or load file
    if (keyCode == juce::KeyPress::returnKey)
    {
        if (activePane == Pane::Files)
        {
            if (onStopPreview)
                onStopPreview();
            loadSelectedFile();
        }
        return true;
    }

    // Backspace: parent directory
    if (keyCode == juce::KeyPress::backspaceKey)
    {
        if (onStopPreview)
            onStopPreview();
        if (! currentDirectory.isRoot())
            navigateInto (currentDirectory.getParentDirectory());
        return true;
    }

    return false;
}

//==============================================================================
// Mouse
//==============================================================================

void SampleBrowserComponent::mouseDown (const juce::MouseEvent& event)
{
    // Check auto-advance checkbox hit
    auto infoBounds = getInfoBarBounds();
    if (infoBounds.contains (event.getPosition()))
    {
        int checkboxWidth = 120;
        int cbX = infoBounds.getRight() - checkboxWidth - 4;
        int cbRight = infoBounds.getRight();
        if (event.x >= cbX && event.x <= cbRight)
        {
            autoAdvance = ! autoAdvance;
            repaint();
            return;
        }
    }

    auto fileBounds = getFilePaneBounds();
    auto instBounds = getInstrumentPaneBounds();

    if (fileBounds.contains (event.getPosition()))
    {
        activePane = Pane::Files;
        int row = (event.y - fileBounds.getY() - kHeaderHeight) / kRowHeight + fileScrollOffset;
        if (row >= 0 && row < static_cast<int> (fileEntries.size())
            && event.y > fileBounds.getY() + kHeaderHeight)
        {
            fileSelection = row;
            triggerPreviewForSelection();
        }
    }
    else if (instBounds.contains (event.getPosition()))
    {
        activePane = Pane::Instruments;
        int row = (event.y - instBounds.getY() - kHeaderHeight) / kRowHeight + instrumentScrollOffset;
        if (row >= 0 && row < 256 && event.y > instBounds.getY() + kHeaderHeight)
        {
            instrumentSelection = row;
            triggerInstrumentPreview();
        }
    }

    grabKeyboardFocus();
    repaint();
}

void SampleBrowserComponent::mouseDoubleClick (const juce::MouseEvent& event)
{
    auto fileBounds = getFilePaneBounds();
    if (fileBounds.contains (event.getPosition())
        && event.y > fileBounds.getY() + kHeaderHeight)
    {
        loadSelectedFile();
    }
}

void SampleBrowserComponent::mouseWheelMove (const juce::MouseEvent& event,
                                           const juce::MouseWheelDetails& wheel)
{
    int delta = (wheel.deltaY > 0) ? -3 : 3;

    auto fileBounds = getFilePaneBounds();
    auto instBounds = getInstrumentPaneBounds();

    if (fileBounds.contains (event.getPosition()))
    {
        int maxScroll = juce::jmax (0, static_cast<int> (fileEntries.size()) - getFileVisibleRows());
        fileScrollOffset = juce::jlimit (0, maxScroll, fileScrollOffset + delta);
    }
    else if (instBounds.contains (event.getPosition()))
    {
        int maxScroll = juce::jmax (0, 256 - getInstrumentVisibleRows());
        instrumentScrollOffset = juce::jlimit (0, maxScroll, instrumentScrollOffset + delta);
    }

    repaint();
}

//==============================================================================
// Scroll helpers
//==============================================================================

void SampleBrowserComponent::ensureFileSelectionVisible()
{
    int visibleRows = getFileVisibleRows();
    if (fileSelection < fileScrollOffset)
        fileScrollOffset = fileSelection;
    else if (fileSelection >= fileScrollOffset + visibleRows)
        fileScrollOffset = fileSelection - visibleRows + 1;
}

void SampleBrowserComponent::ensureInstrumentSelectionVisible()
{
    int visibleRows = getInstrumentVisibleRows();
    if (instrumentSelection < instrumentScrollOffset)
        instrumentScrollOffset = instrumentSelection;
    else if (instrumentSelection >= instrumentScrollOffset + visibleRows)
        instrumentScrollOffset = instrumentSelection - visibleRows + 1;
}

void SampleBrowserComponent::triggerPreviewForSelection()
{
    if (activePane != Pane::Files)
        return;

    if (fileSelection >= 0 && fileSelection < static_cast<int> (fileEntries.size()))
    {
        auto& entry = fileEntries[static_cast<size_t> (fileSelection)];
        if (! entry.isDirectory && onPreviewFile)
            onPreviewFile (entry.file);
        else if (entry.isDirectory && onStopPreview)
            onStopPreview();
    }
}

void SampleBrowserComponent::triggerInstrumentPreview()
{
    if (activePane != Pane::Instruments)
        return;

    if (instrumentSelection >= 0 && instrumentSelection < 256
        && instrumentSlots[static_cast<size_t> (instrumentSelection)].hasData
        && onPreviewInstrument)
    {
        onPreviewInstrument (instrumentSelection);
    }
}

void SampleBrowserComponent::advanceToNextEmptySlot()
{
    if (! autoAdvance)
        return;

    // Search forward from current selection for the next empty slot
    for (int i = instrumentSelection + 1; i < 256; ++i)
    {
        if (! instrumentSlots[static_cast<size_t> (i)].hasData)
        {
            instrumentSelection = i;
            ensureInstrumentSelectionVisible();
            repaint();
            return;
        }
    }

    // Wrap around from the beginning
    for (int i = 0; i < instrumentSelection; ++i)
    {
        if (! instrumentSlots[static_cast<size_t> (i)].hasData)
        {
            instrumentSelection = i;
            ensureInstrumentSelectionVisible();
            repaint();
            return;
        }
    }
}
