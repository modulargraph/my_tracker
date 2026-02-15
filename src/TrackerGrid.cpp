#include "TrackerGrid.h"

TrackerGrid::TrackerGrid (PatternData& patternData, TrackerLookAndFeel& lnf)
    : pattern (patternData), lookAndFeel (lnf)
{
    setWantsKeyboardFocus (true);
}

//==============================================================================
// Layout
//==============================================================================

int TrackerGrid::getVisibleRowCount() const
{
    return juce::jmax (1, (getHeight() - kHeaderHeight) / kRowHeight);
}

int TrackerGrid::getVisibleTrackCount() const
{
    return juce::jmax (1, (getWidth() - kRowNumberWidth) / kCellWidth);
}

void TrackerGrid::ensureCursorVisible()
{
    auto visibleRows = getVisibleRowCount();

    if (cursorRow < scrollOffset)
        scrollOffset = cursorRow;
    else if (cursorRow >= scrollOffset + visibleRows)
        scrollOffset = cursorRow - visibleRows + 1;

    auto visibleTracks = getVisibleTrackCount();
    if (cursorTrack < horizontalScrollOffset)
        horizontalScrollOffset = cursorTrack;
    else if (cursorTrack >= horizontalScrollOffset + visibleTracks)
        horizontalScrollOffset = cursorTrack - visibleTracks + 1;
}

void TrackerGrid::resized()
{
    ensureCursorVisible();
}

//==============================================================================
// Selection
//==============================================================================

void TrackerGrid::clearSelection()
{
    hasSelection = false;
    repaint();
}

void TrackerGrid::getSelectionBounds (int& minRow, int& maxRow, int& minTrack, int& maxTrack) const
{
    minRow = juce::jmin (selStartRow, selEndRow);
    maxRow = juce::jmax (selStartRow, selEndRow);
    minTrack = juce::jmin (selStartTrack, selEndTrack);
    maxTrack = juce::jmax (selStartTrack, selEndTrack);
}

//==============================================================================
// Paint
//==============================================================================

void TrackerGrid::paint (juce::Graphics& g)
{
    auto bgColour = lookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId);
    g.fillAll (bgColour);

    drawHeaders (g);
    drawRowNumbers (g);
    drawCells (g);
    if (hasSelection)
        drawSelection (g);
}

void TrackerGrid::drawHeaders (juce::Graphics& g)
{
    auto headerBg = lookAndFeel.findColour (TrackerLookAndFeel::headerColourId);
    auto textColour = lookAndFeel.findColour (TrackerLookAndFeel::textColourId);

    g.setColour (headerBg);
    g.fillRect (0, 0, getWidth(), kHeaderHeight);

    g.setFont (lookAndFeel.getMonoFont (12.0f));

    int visibleTracks = getVisibleTrackCount();
    for (int i = 0; i < visibleTracks && (horizontalScrollOffset + i) < kNumTracks; ++i)
    {
        int track = horizontalScrollOffset + i;
        int x = kRowNumberWidth + i * kCellWidth;

        // Mute/Solo indicators
        juce::String text;
        if (trackMuted[static_cast<size_t> (track)])
            text = "M ";
        else if (trackSoloed[static_cast<size_t> (track)])
            text = "S ";
        else
            text = "";

        if (trackHasSample[static_cast<size_t> (track)])
            text += juce::String::formatted ("T%02d*", track + 1);
        else
            text += juce::String::formatted ("T%02d", track + 1);

        if (trackMuted[static_cast<size_t> (track)])
            g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::muteColourId));
        else if (trackSoloed[static_cast<size_t> (track)])
            g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::soloColourId));
        else
            g.setColour (textColour);

        g.drawText (text, x, 0, kCellWidth, kHeaderHeight, juce::Justification::centred);
    }

    // Header bottom line
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (kHeaderHeight - 1, 0.0f, static_cast<float> (getWidth()));
}

void TrackerGrid::drawRowNumbers (juce::Graphics& g)
{
    auto textColour = lookAndFeel.findColour (TrackerLookAndFeel::textColourId);
    auto beatColour = lookAndFeel.findColour (TrackerLookAndFeel::beatMarkerColourId);
    auto& pat = pattern.getCurrentPattern();

    g.setFont (lookAndFeel.getMonoFont (12.0f));

    int visibleRows = getVisibleRowCount();
    for (int i = 0; i < visibleRows; ++i)
    {
        int row = scrollOffset + i;
        if (row >= pat.numRows)
            break;

        int y = kHeaderHeight + i * kRowHeight;

        // Beat marker background on every 4th row
        if (row % 4 == 0)
        {
            g.setColour (beatColour);
            g.fillRect (0, y, kRowNumberWidth, kRowHeight);
        }

        // More prominent bar marker every 16th row
        if (row % 16 == 0)
        {
            g.setColour (juce::Colour (0xff2a2a2a));
            g.fillRect (0, y, kRowNumberWidth, kRowHeight);
        }

        g.setColour (textColour.withAlpha (row % 4 == 0 ? 1.0f : 0.6f));
        g.drawText (juce::String::formatted ("%02X", row), 2, y, kRowNumberWidth - 4, kRowHeight,
                    juce::Justification::centredRight);
    }
}

void TrackerGrid::drawCells (juce::Graphics& g)
{
    auto& pat = pattern.getCurrentPattern();
    auto gridColour = lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId);

    int visibleRows = getVisibleRowCount();
    int visibleTracks = getVisibleTrackCount();

    for (int i = 0; i < visibleRows; ++i)
    {
        int row = scrollOffset + i;
        if (row >= pat.numRows)
            break;

        int y = kHeaderHeight + i * kRowHeight;

        // Bar marker line every 16th row
        if (row % 16 == 0 && row > 0)
        {
            g.setColour (juce::Colour (0xff444444));
            g.drawHorizontalLine (y, static_cast<float> (kRowNumberWidth),
                                  static_cast<float> (kRowNumberWidth + visibleTracks * kCellWidth));
        }

        for (int ti = 0; ti < visibleTracks && (horizontalScrollOffset + ti) < kNumTracks; ++ti)
        {
            int track = horizontalScrollOffset + ti;
            int x = kRowNumberWidth + ti * kCellWidth;
            bool isCursor = (row == cursorRow && track == cursorTrack);
            bool isCurrentRow = (row == cursorRow);
            bool isPlayRow = (row == playbackRow && isPlaying);

            drawCell (g, pat.getCell (row, track), x, y, kCellWidth, isCursor, isCurrentRow, isPlayRow, track);

            // Vertical grid line
            g.setColour (gridColour);
            g.drawVerticalLine (x, static_cast<float> (y), static_cast<float> (y + kRowHeight));
        }

        // Horizontal grid line
        g.setColour (gridColour);
        g.drawHorizontalLine (y + kRowHeight - 1, static_cast<float> (kRowNumberWidth),
                              static_cast<float> (kRowNumberWidth + visibleTracks * kCellWidth));
    }
}

void TrackerGrid::drawCell (juce::Graphics& g, const Cell& cell, int x, int y, int width,
                            bool isCursor, bool isCurrentRow, bool isPlaybackRow, int /*track*/)
{
    // Background
    if (isCursor)
        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::cursorCellColourId));
    else if (isPlaybackRow)
        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::playbackCursorColourId));
    else if (isCurrentRow)
        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::cursorRowColourId));
    else
        g.setColour (juce::Colours::transparentBlack);

    if (isCursor || isCurrentRow || isPlaybackRow)
        g.fillRect (x, y, width, kRowHeight);

    // Draw sub-columns with distinct colors
    g.setFont (lookAndFeel.getMonoFont (12.0f));

    int textX = x + kCellPadding;

    // Note sub-column
    juce::String noteStr = cell.isEmpty() ? "---" : noteToString (cell.note);
    auto noteColour = isCursor ? juce::Colours::white : lookAndFeel.findColour (TrackerLookAndFeel::noteColourId);

    // Highlight active sub-column on cursor cell
    if (isCursor && cursorSubColumn == SubColumn::Note)
    {
        g.setColour (juce::Colour (0xff3a5a7a));
        g.fillRect (textX - 1, y, kNoteWidth + 2, kRowHeight);
    }
    g.setColour (noteColour);
    g.drawText (noteStr, textX, y, kNoteWidth, kRowHeight, juce::Justification::centredLeft);
    textX += kNoteWidth;

    // Space
    g.setColour (isCursor ? juce::Colours::white.withAlpha (0.3f) : lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.3f));
    g.drawText (" ", textX, y, 4, kRowHeight, juce::Justification::centredLeft);
    textX += 4;

    // Instrument sub-column
    juce::String instStr = cell.instrument >= 0 ? juce::String::formatted ("%02X", cell.instrument) : "..";
    auto instColour = isCursor ? juce::Colours::white : lookAndFeel.findColour (TrackerLookAndFeel::instrumentColourId);
    if (isCursor && cursorSubColumn == SubColumn::Instrument)
    {
        g.setColour (juce::Colour (0xff3a5a7a));
        g.fillRect (textX - 1, y, kInstWidth + 2, kRowHeight);
    }
    g.setColour (instColour);
    g.drawText (instStr, textX, y, kInstWidth, kRowHeight, juce::Justification::centredLeft);
    textX += kInstWidth;

    // Space
    textX += 4;

    // Volume sub-column
    juce::String volStr = cell.volume >= 0 ? juce::String::formatted ("%02X", cell.volume) : "..";
    auto volColour = isCursor ? juce::Colours::white : lookAndFeel.findColour (TrackerLookAndFeel::volumeColourId);
    if (isCursor && cursorSubColumn == SubColumn::Volume)
    {
        g.setColour (juce::Colour (0xff3a5a7a));
        g.fillRect (textX - 1, y, kVolWidth + 2, kRowHeight);
    }
    g.setColour (volColour);
    g.drawText (volStr, textX, y, kVolWidth, kRowHeight, juce::Justification::centredLeft);
    textX += kVolWidth;

    // Space
    textX += 4;

    // FX sub-column
    juce::String fxStr = cell.fx > 0 ? juce::String::formatted ("%X%02X", cell.fx, cell.fxParam) : "...";
    auto fxColour = isCursor ? juce::Colours::white : lookAndFeel.findColour (TrackerLookAndFeel::fxColourId);
    if (isCursor && cursorSubColumn == SubColumn::FX)
    {
        g.setColour (juce::Colour (0xff3a5a7a));
        g.fillRect (textX - 1, y, kFxWidth + 2, kRowHeight);
    }
    g.setColour (fxColour);
    g.drawText (fxStr, textX, y, kFxWidth, kRowHeight, juce::Justification::centredLeft);
}

void TrackerGrid::drawSelection (juce::Graphics& g)
{
    if (! hasSelection) return;

    int minRow, maxRow, minTrack, maxTrack;
    getSelectionBounds (minRow, maxRow, minTrack, maxTrack);

    int visibleTracks = getVisibleTrackCount();

    for (int row = minRow; row <= maxRow; ++row)
    {
        if (row < scrollOffset || row >= scrollOffset + getVisibleRowCount())
            continue;

        for (int track = minTrack; track <= maxTrack; ++track)
        {
            int ti = track - horizontalScrollOffset;
            if (ti < 0 || ti >= visibleTracks) continue;

            int x = kRowNumberWidth + ti * kCellWidth;
            int y = kHeaderHeight + (row - scrollOffset) * kRowHeight;

            g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::selectionColourId));
            g.fillRect (x, y, kCellWidth, kRowHeight);
        }
    }
}

//==============================================================================
// Note helpers
//==============================================================================

juce::String TrackerGrid::noteToString (int note)
{
    if (note < 0)
        return "---";
    if (note == 255) // note-off
        return "===";

    static const char* noteNames[] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };
    int octave = note / 12;
    int noteIndex = note % 12;
    return juce::String (noteNames[noteIndex]) + juce::String (octave);
}

int TrackerGrid::keyToNote (const juce::KeyPress& key) const
{
    // Don't trigger notes if modifier keys (other than shift) are pressed
    if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown() || key.getModifiers().isAltDown())
        return -1;

    auto c = key.getTextCharacter();

    // Lower octave (current octave)
    int baseNote = currentOctave * 12;
    switch (c)
    {
        case 'z': return baseNote + 0;   // C
        case 's': return baseNote + 1;   // C#
        case 'x': return baseNote + 2;   // D
        case 'd': return baseNote + 3;   // D#
        case 'c': return baseNote + 4;   // E
        case 'v': return baseNote + 5;   // F
        case 'g': return baseNote + 6;   // F#
        case 'b': return baseNote + 7;   // G
        case 'h': return baseNote + 8;   // G#
        case 'n': return baseNote + 9;   // A
        case 'j': return baseNote + 10;  // A#
        case 'm': return baseNote + 11;  // B
        default: break;
    }

    // Upper octave (current octave + 1)
    int upperBase = (currentOctave + 1) * 12;
    switch (c)
    {
        case 'q': return upperBase + 0;   // C
        case '2': return upperBase + 1;   // C#
        case 'w': return upperBase + 2;   // D
        case '3': return upperBase + 3;   // D#
        case 'e': return upperBase + 4;   // E
        case 'r': return upperBase + 5;   // F
        case '5': return upperBase + 6;   // F#
        case 't': return upperBase + 7;   // G
        case '6': return upperBase + 8;   // G#
        case 'y': return upperBase + 9;   // A
        case '7': return upperBase + 10;  // A#
        case 'u': return upperBase + 11;  // B
        default: break;
    }

    return -1;
}

int TrackerGrid::hexCharToValue (juce::juce_wchar c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

//==============================================================================
// Mouse
//==============================================================================

bool TrackerGrid::hitTestGrid (int mx, int my, int& outRow, int& outTrack, SubColumn& outSubCol) const
{
    if (my < kHeaderHeight || mx < kRowNumberWidth)
        return false;

    int row = (my - kHeaderHeight) / kRowHeight + scrollOffset;
    auto& pat = pattern.getCurrentPattern();
    if (row >= pat.numRows)
        return false;

    int trackPixel = mx - kRowNumberWidth;
    int trackVisual = trackPixel / kCellWidth;
    int track = trackVisual + horizontalScrollOffset;
    if (track >= kNumTracks)
        return false;

    outRow = row;
    outTrack = track;

    // Determine sub-column within cell
    int cellOffset = trackPixel - trackVisual * kCellWidth - kCellPadding;
    if (cellOffset < kNoteWidth)
        outSubCol = SubColumn::Note;
    else if (cellOffset < kNoteWidth + 4 + kInstWidth)
        outSubCol = SubColumn::Instrument;
    else if (cellOffset < kNoteWidth + 4 + kInstWidth + 4 + kVolWidth)
        outSubCol = SubColumn::Volume;
    else
        outSubCol = SubColumn::FX;

    return true;
}

void TrackerGrid::mouseDown (const juce::MouseEvent& event)
{
    grabKeyboardFocus();

    // Right-click on header
    if (event.mods.isPopupMenu() && event.y < kHeaderHeight)
    {
        int trackPixel = event.x - kRowNumberWidth;
        if (trackPixel >= 0)
        {
            int track = trackPixel / kCellWidth + horizontalScrollOffset;
            if (track < kNumTracks && onTrackHeaderRightClick)
                onTrackHeaderRightClick (track, event.getScreenPosition());
        }
        return;
    }

    int row, track;
    SubColumn subCol;
    if (hitTestGrid (event.x, event.y, row, track, subCol))
    {
        if (event.mods.isShiftDown())
        {
            // Extend selection
            if (! hasSelection)
            {
                selStartRow = cursorRow;
                selStartTrack = cursorTrack;
            }
            selEndRow = row;
            selEndTrack = track;
            hasSelection = true;
        }
        else
        {
            clearSelection();
        }

        cursorRow = row;
        cursorTrack = track;
        cursorSubColumn = subCol;
        hexDigitCount = 0;
        hexAccumulator = 0;
        ensureCursorVisible();
        repaint();

        if (onCursorMoved)
            onCursorMoved();
    }
}

bool TrackerGrid::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".aiff" || ext == ".aif" || ext == ".flac" || ext == ".ogg" || ext == ".mp3")
            return true;
    }
    return false;
}

void TrackerGrid::filesDropped (const juce::StringArray& files, int x, int /*y*/)
{
    // Determine which track was dropped on
    int trackPixel = x - kRowNumberWidth;
    if (trackPixel < 0) return;

    int track = trackPixel / kCellWidth + horizontalScrollOffset;
    if (track >= kNumTracks) return;

    for (auto& f : files)
    {
        juce::File file (f);
        auto ext = file.getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".aiff" || ext == ".aif" || ext == ".flac" || ext == ".ogg" || ext == ".mp3")
        {
            if (onFileDroppedOnTrack)
                onFileDroppedOnTrack (track, file);
            track++; // Next file goes to next track
            if (track >= kNumTracks) break;
        }
    }
}

void TrackerGrid::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    int delta = static_cast<int> (wheel.deltaY * -10.0f);

    if (event.mods.isShiftDown())
    {
        // Horizontal scroll
        horizontalScrollOffset = juce::jlimit (0, juce::jmax (0, kNumTracks - getVisibleTrackCount()),
                                                horizontalScrollOffset + delta);
    }
    else
    {
        auto& pat = pattern.getCurrentPattern();
        scrollOffset = juce::jlimit (0, juce::jmax (0, pat.numRows - getVisibleRowCount()),
                                     scrollOffset + delta);
    }
    repaint();
}

//==============================================================================
// Cursor & navigation
//==============================================================================

void TrackerGrid::setCursorPosition (int row, int track)
{
    auto& pat = pattern.getCurrentPattern();
    cursorRow = juce::jlimit (0, pat.numRows - 1, row);
    cursorTrack = juce::jlimit (0, kNumTracks - 1, track);
    hexDigitCount = 0;
    hexAccumulator = 0;
    ensureCursorVisible();
    repaint();

    if (onCursorMoved)
        onCursorMoved();
}

void TrackerGrid::moveCursor (int rowDelta, int trackDelta)
{
    auto& pat = pattern.getCurrentPattern();
    int newRow = cursorRow + rowDelta;
    int newTrack = cursorTrack + trackDelta;

    // Wrap tracks
    if (newTrack < 0)
        newTrack = kNumTracks - 1;
    else if (newTrack >= kNumTracks)
        newTrack = 0;

    // Clamp rows
    newRow = juce::jlimit (0, pat.numRows - 1, newRow);

    setCursorPosition (newRow, newTrack);
}

void TrackerGrid::setPlaybackRow (int row)
{
    playbackRow = row;
    repaint();
}

void TrackerGrid::setPlaying (bool playing)
{
    isPlaying = playing;
    if (! playing)
        playbackRow = -1;
    repaint();
}

//==============================================================================
// Keyboard handling
//==============================================================================

bool TrackerGrid::keyPressed (const juce::KeyPress& key)
{
    auto keyCode = key.getKeyCode();
    bool shift = key.getModifiers().isShiftDown();

    // Navigation
    if (keyCode == juce::KeyPress::upKey)
    {
        if (shift && ! hasSelection)
        {
            hasSelection = true;
            selStartRow = cursorRow;
            selStartTrack = cursorTrack;
        }
        moveCursor (-1, 0);
        if (shift) { selEndRow = cursorRow; selEndTrack = cursorTrack; }
        else clearSelection();
        return true;
    }
    if (keyCode == juce::KeyPress::downKey)
    {
        if (shift && ! hasSelection)
        {
            hasSelection = true;
            selStartRow = cursorRow;
            selStartTrack = cursorTrack;
        }
        moveCursor (1, 0);
        if (shift) { selEndRow = cursorRow; selEndTrack = cursorTrack; }
        else clearSelection();
        return true;
    }
    if (keyCode == juce::KeyPress::leftKey)
    {
        if (shift && ! hasSelection)
        {
            hasSelection = true;
            selStartRow = cursorRow;
            selStartTrack = cursorTrack;
        }
        moveCursor (0, -1);
        if (shift) { selEndRow = cursorRow; selEndTrack = cursorTrack; }
        else clearSelection();
        return true;
    }
    if (keyCode == juce::KeyPress::rightKey)
    {
        if (shift && ! hasSelection)
        {
            hasSelection = true;
            selStartRow = cursorRow;
            selStartTrack = cursorTrack;
        }
        moveCursor (0, 1);
        if (shift) { selEndRow = cursorRow; selEndTrack = cursorTrack; }
        else clearSelection();
        return true;
    }
    if (keyCode == juce::KeyPress::pageUpKey)
    {
        moveCursor (-16, 0);
        clearSelection();
        return true;
    }
    if (keyCode == juce::KeyPress::pageDownKey)
    {
        moveCursor (16, 0);
        clearSelection();
        return true;
    }
    if (keyCode == juce::KeyPress::homeKey)
    {
        setCursorPosition (0, cursorTrack);
        clearSelection();
        return true;
    }
    if (keyCode == juce::KeyPress::endKey)
    {
        setCursorPosition (pattern.getCurrentPattern().numRows - 1, cursorTrack);
        clearSelection();
        return true;
    }

    // Tab: cycle through sub-columns, then to next track
    if (keyCode == juce::KeyPress::tabKey)
    {
        hexDigitCount = 0;
        hexAccumulator = 0;
        if (shift)
        {
            // Reverse: FX → Vol → Inst → Note → prev track's FX
            if (cursorSubColumn == SubColumn::Note)
            {
                cursorSubColumn = SubColumn::FX;
                moveCursor (0, -1);
            }
            else if (cursorSubColumn == SubColumn::Instrument)
                cursorSubColumn = SubColumn::Note;
            else if (cursorSubColumn == SubColumn::Volume)
                cursorSubColumn = SubColumn::Instrument;
            else if (cursorSubColumn == SubColumn::FX)
                cursorSubColumn = SubColumn::Volume;
        }
        else
        {
            // Forward: Note → Inst → Vol → FX → next track's Note
            if (cursorSubColumn == SubColumn::Note)
                cursorSubColumn = SubColumn::Instrument;
            else if (cursorSubColumn == SubColumn::Instrument)
                cursorSubColumn = SubColumn::Volume;
            else if (cursorSubColumn == SubColumn::Volume)
                cursorSubColumn = SubColumn::FX;
            else if (cursorSubColumn == SubColumn::FX)
            {
                cursorSubColumn = SubColumn::Note;
                moveCursor (0, 1);
            }
        }
        repaint();
        if (onCursorMoved) onCursorMoved();
        return true;
    }

    // Delete cell
    if (keyCode == juce::KeyPress::deleteKey || keyCode == juce::KeyPress::backspaceKey)
    {
        Cell& cell = pattern.getCell (cursorRow, cursorTrack);
        switch (cursorSubColumn)
        {
            case SubColumn::Note:       cell.clear(); break;
            case SubColumn::Instrument: cell.instrument = -1; break;
            case SubColumn::Volume:     cell.volume = -1; break;
            case SubColumn::FX:         cell.fx = 0; cell.fxParam = 0; break;
        }
        hexDigitCount = 0;
        hexAccumulator = 0;
        repaint();
        return true;
    }

    // Note-off with backtick
    if (key.getTextCharacter() == '`' && cursorSubColumn == SubColumn::Note)
    {
        Cell& cell = pattern.getCell (cursorRow, cursorTrack);
        cell.note = 255; // note-off marker
        cell.instrument = currentInstrument;
        moveCursor (editStep, 0);
        repaint();
        return true;
    }

    // Octave change with F-keys: F1-F8 set octave 0-7
    if (keyCode >= juce::KeyPress::F1Key && keyCode <= juce::KeyPress::F8Key)
    {
        setOctave (keyCode - juce::KeyPress::F1Key);
        if (onCursorMoved) onCursorMoved();
        return true;
    }

    // Sub-column specific editing
    if (cursorSubColumn == SubColumn::Note)
    {
        // Note entry
        int note = keyToNote (key);
        if (note >= 0 && note <= 127)
        {
            Cell& cell = pattern.getCell (cursorRow, cursorTrack);
            cell.note = note;
            cell.instrument = currentInstrument;
            if (cell.volume < 0)
                cell.volume = 127;

            if (onNoteEntered)
                onNoteEntered (note, currentInstrument);

            moveCursor (editStep, 0);
            repaint();
            return true;
        }
    }
    else if (cursorSubColumn == SubColumn::Instrument)
    {
        int hexVal = hexCharToValue (key.getTextCharacter());
        if (hexVal >= 0)
        {
            Cell& cell = pattern.getCell (cursorRow, cursorTrack);
            if (hexDigitCount == 0)
            {
                hexAccumulator = hexVal;
                hexDigitCount = 1;
                cell.instrument = hexAccumulator;
            }
            else
            {
                hexAccumulator = (hexAccumulator << 4) | hexVal;
                cell.instrument = hexAccumulator & 0xFF;
                hexDigitCount = 0;
                hexAccumulator = 0;
                moveCursor (editStep, 0);
            }
            repaint();
            return true;
        }
    }
    else if (cursorSubColumn == SubColumn::Volume)
    {
        int hexVal = hexCharToValue (key.getTextCharacter());
        if (hexVal >= 0)
        {
            Cell& cell = pattern.getCell (cursorRow, cursorTrack);
            if (hexDigitCount == 0)
            {
                hexAccumulator = hexVal;
                hexDigitCount = 1;
                cell.volume = hexAccumulator;
            }
            else
            {
                hexAccumulator = (hexAccumulator << 4) | hexVal;
                cell.volume = juce::jlimit (0, 127, hexAccumulator);
                hexDigitCount = 0;
                hexAccumulator = 0;
                moveCursor (editStep, 0);
            }
            repaint();
            return true;
        }
    }
    else if (cursorSubColumn == SubColumn::FX)
    {
        int hexVal = hexCharToValue (key.getTextCharacter());
        if (hexVal >= 0)
        {
            Cell& cell = pattern.getCell (cursorRow, cursorTrack);
            if (hexDigitCount == 0)
            {
                // First digit = effect command
                cell.fx = hexVal;
                cell.fxParam = 0;
                hexAccumulator = 0;
                hexDigitCount = 1;
            }
            else if (hexDigitCount == 1)
            {
                hexAccumulator = hexVal;
                hexDigitCount = 2;
                cell.fxParam = hexAccumulator;
            }
            else
            {
                hexAccumulator = (hexAccumulator << 4) | hexVal;
                cell.fxParam = hexAccumulator & 0xFF;
                hexDigitCount = 0;
                hexAccumulator = 0;
                moveCursor (editStep, 0);
            }
            repaint();
            return true;
        }
    }

    return false;
}
