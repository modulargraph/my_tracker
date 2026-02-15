#include "TrackerGrid.h"

TrackerGrid::TrackerGrid (PatternData& patternData, TrackerLookAndFeel& lnf, TrackLayout& layout)
    : pattern (patternData), lookAndFeel (lnf), trackLayout (layout)
{
    setWantsKeyboardFocus (true);
}

//==============================================================================
// Layout
//==============================================================================

int TrackerGrid::getEffectiveHeaderHeight() const
{
    return kHeaderHeight + (trackLayout.hasGroups() ? kGroupHeaderHeight : 0);
}

int TrackerGrid::getVisibleRowCount() const
{
    return juce::jmax (1, (getHeight() - getEffectiveHeaderHeight()) / kRowHeight);
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

    // Use visual position of cursor track for horizontal scrolling
    int cursorVisual = trackLayout.physicalToVisual (cursorTrack);
    auto visibleTracks = getVisibleTrackCount();
    if (cursorVisual < horizontalScrollOffset)
        horizontalScrollOffset = cursorVisual;
    else if (cursorVisual >= horizontalScrollOffset + visibleTracks)
        horizontalScrollOffset = cursorVisual - visibleTracks + 1;
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

    if (trackLayout.hasGroups())
        drawGroupHeaders (g);
    drawHeaders (g);
    drawRowNumbers (g);
    drawCells (g);
    if (hasSelection)
        drawSelection (g);
    if (isDraggingBlock)
        drawDragPreview (g);
}

void TrackerGrid::drawHeaders (juce::Graphics& g)
{
    auto headerBg = lookAndFeel.findColour (TrackerLookAndFeel::headerColourId);
    auto textColour = lookAndFeel.findColour (TrackerLookAndFeel::textColourId);
    int headerY = trackLayout.hasGroups() ? kGroupHeaderHeight : 0;

    g.setColour (headerBg);
    g.fillRect (0, headerY, getWidth(), kHeaderHeight);

    g.setFont (lookAndFeel.getMonoFont (12.0f));

    int visibleTracks = getVisibleTrackCount();
    for (int i = 0; i < visibleTracks && (horizontalScrollOffset + i) < kNumTracks; ++i)
    {
        int physTrack = trackLayout.visualToPhysical (horizontalScrollOffset + i);
        int x = kRowNumberWidth + i * kCellWidth;

        // Mute/Solo indicators
        juce::String text;
        if (trackMuted[static_cast<size_t> (physTrack)])
            text = "M ";
        else if (trackSoloed[static_cast<size_t> (physTrack)])
            text = "S ";
        else
            text = "";

        auto& customName = trackLayout.getTrackName (physTrack);
        if (customName.isNotEmpty())
            text += customName;
        else if (trackHasSample[static_cast<size_t> (physTrack)])
            text += juce::String::formatted ("T%02d*", physTrack + 1);
        else
            text += juce::String::formatted ("T%02d", physTrack + 1);

        if (trackMuted[static_cast<size_t> (physTrack)])
            g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::muteColourId));
        else if (trackSoloed[static_cast<size_t> (physTrack)])
            g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::soloColourId));
        else
            g.setColour (textColour);

        g.drawText (text, x, headerY, kCellWidth, kHeaderHeight, juce::Justification::centred);
    }

    // Header bottom line
    int effectiveHeaderH = getEffectiveHeaderHeight();
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (effectiveHeaderH - 1, 0.0f, static_cast<float> (getWidth()));
}

void TrackerGrid::drawRowNumbers (juce::Graphics& g)
{
    auto textColour = lookAndFeel.findColour (TrackerLookAndFeel::textColourId);
    auto beatColour = lookAndFeel.findColour (TrackerLookAndFeel::beatMarkerColourId);
    auto& pat = pattern.getCurrentPattern();
    int effectiveHeaderH = getEffectiveHeaderHeight();

    g.setFont (lookAndFeel.getMonoFont (12.0f));

    int visibleRows = getVisibleRowCount();
    for (int i = 0; i < visibleRows; ++i)
    {
        int row = scrollOffset + i;
        if (row >= pat.numRows)
            break;

        int y = effectiveHeaderH + i * kRowHeight;

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
    int effectiveHeaderH = getEffectiveHeaderHeight();

    int visibleRows = getVisibleRowCount();
    int visibleTracks = getVisibleTrackCount();

    for (int i = 0; i < visibleRows; ++i)
    {
        int row = scrollOffset + i;
        if (row >= pat.numRows)
            break;

        int y = effectiveHeaderH + i * kRowHeight;

        // Bar marker line every 16th row
        if (row % 16 == 0 && row > 0)
        {
            g.setColour (juce::Colour (0xff444444));
            g.drawHorizontalLine (y, static_cast<float> (kRowNumberWidth),
                                  static_cast<float> (kRowNumberWidth + visibleTracks * kCellWidth));
        }

        for (int ti = 0; ti < visibleTracks && (horizontalScrollOffset + ti) < kNumTracks; ++ti)
        {
            int physTrack = trackLayout.visualToPhysical (horizontalScrollOffset + ti);
            int x = kRowNumberWidth + ti * kCellWidth;
            bool isCursor = (row == cursorRow && physTrack == cursorTrack);
            bool isCurrentRow = (row == cursorRow);
            bool isPlayRow = (row == playbackRow && isPlaying);

            drawCell (g, pat.getCell (row, physTrack), x, y, kCellWidth, isCursor, isCurrentRow, isPlayRow, physTrack);

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

    int minRow, maxRow, minViTrack, maxViTrack;
    getSelectionBounds (minRow, maxRow, minViTrack, maxViTrack);

    int effectiveHeaderH = getEffectiveHeaderHeight();
    int visibleTracks = getVisibleTrackCount();

    for (int row = minRow; row <= maxRow; ++row)
    {
        if (row < scrollOffset || row >= scrollOffset + getVisibleRowCount())
            continue;

        for (int vi = minViTrack; vi <= maxViTrack; ++vi)
        {
            int screenVi = vi - horizontalScrollOffset;
            if (screenVi < 0 || screenVi >= visibleTracks) continue;

            int x = kRowNumberWidth + screenVi * kCellWidth;
            int y = effectiveHeaderH + (row - scrollOffset) * kRowHeight;

            g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::selectionColourId));
            g.fillRect (x, y, kCellWidth, kRowHeight);
        }
    }
}

void TrackerGrid::drawGroupHeaders (juce::Graphics& g)
{
    auto groupBg = lookAndFeel.findColour (TrackerLookAndFeel::groupHeaderColourId);

    // Fill the group header row background
    g.setColour (groupBg);
    g.fillRect (0, 0, getWidth(), kGroupHeaderHeight);

    int visibleTracks = getVisibleTrackCount();

    // Pass 1: draw per-column background, blending colors of all groups that contain each track
    for (int vi = 0; vi < visibleTracks && (horizontalScrollOffset + vi) < kNumTracks; ++vi)
    {
        int physTrack = trackLayout.visualToPhysical (horizontalScrollOffset + vi);
        int x = kRowNumberWidth + vi * kCellWidth;

        // Collect colors from all groups this track belongs to
        float r = 0.0f, gr = 0.0f, b = 0.0f;
        int count = 0;
        for (int gi = 0; gi < trackLayout.getNumGroups(); ++gi)
        {
            auto& group = trackLayout.getGroup (gi);
            for (auto idx : group.trackIndices)
            {
                if (idx == physTrack)
                {
                    r += group.colour.getFloatRed();
                    gr += group.colour.getFloatGreen();
                    b += group.colour.getFloatBlue();
                    count++;
                    break;
                }
            }
        }

        if (count > 0)
        {
            auto blended = juce::Colour::fromFloatRGBA (r / count, gr / count, b / count, 0.4f);
            g.setColour (blended);
            g.fillRect (x, 0, kCellWidth, kGroupHeaderHeight);
        }
    }

    // Pass 2: draw group labels and borders
    for (int gi = 0; gi < trackLayout.getNumGroups(); ++gi)
    {
        auto& group = trackLayout.getGroup (gi);
        auto [firstVisual, lastVisual] = trackLayout.getGroupVisualRange (gi);

        int startCol = firstVisual - horizontalScrollOffset;
        int endCol = lastVisual - horizontalScrollOffset;

        if (endCol < 0 || startCol >= visibleTracks)
            continue;

        startCol = juce::jmax (0, startCol);
        endCol = juce::jmin (visibleTracks - 1, endCol);

        int x = kRowNumberWidth + startCol * kCellWidth;
        int w = (endCol - startCol + 1) * kCellWidth;

        // Draw group name
        g.setColour (group.colour.brighter (0.5f));
        g.setFont (lookAndFeel.getMonoFont (10.0f));
        g.drawText (group.name, x + 4, 0, w - 8, kGroupHeaderHeight, juce::Justification::centredLeft);

        // Draw left/right borders
        g.setColour (group.colour);
        g.drawVerticalLine (x, 0.0f, static_cast<float> (kGroupHeaderHeight));
        g.drawVerticalLine (x + w - 1, 0.0f, static_cast<float> (kGroupHeaderHeight));
    }

    // Bottom line of group header
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (kGroupHeaderHeight - 1, 0.0f, static_cast<float> (getWidth()));
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
    int effectiveHeaderH = getEffectiveHeaderHeight();
    if (my < effectiveHeaderH || mx < kRowNumberWidth)
        return false;

    int row = (my - effectiveHeaderH) / kRowHeight + scrollOffset;
    auto& pat = pattern.getCurrentPattern();
    if (row >= pat.numRows)
        return false;

    int trackPixel = mx - kRowNumberWidth;
    int trackVisual = trackPixel / kCellWidth;
    int visualIndex = trackVisual + horizontalScrollOffset;
    if (visualIndex >= kNumTracks)
        return false;

    outRow = row;
    outTrack = trackLayout.visualToPhysical (visualIndex);

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
    isDraggingSelection = false;
    isDraggingBlock = false;
    isDraggingHeader = false;
    isDraggingGroupBorder = false;
    isDraggingGroupAsWhole = false;
    dragGroupDragIndex = -1;
    dragHeaderVisualIndex = -1;
    dragGroupIndex = -1;
    dragMoveRow = -1;
    dragMoveTrack = -1;
    dragGrabRowOffset = 0;
    dragGrabTrackOffset = 0;

    int effectiveHeaderH = getEffectiveHeaderHeight();

    // Clicks on header area
    if (event.y < effectiveHeaderH && event.x >= kRowNumberWidth)
    {
        int trackPixel = event.x - kRowNumberWidth;
        int visualIndex = trackPixel / kCellWidth + horizontalScrollOffset;
        if (visualIndex >= kNumTracks) return;

        int physTrack = trackLayout.visualToPhysical (visualIndex);

        // Right-click → context menu
        if (event.mods.isPopupMenu())
        {
            if (onTrackHeaderRightClick)
                onTrackHeaderRightClick (physTrack, event.getScreenPosition());
            return;
        }

        // Check if clicking near a group border in the group header row
        if (trackLayout.hasGroups() && event.y < kGroupHeaderHeight)
        {
            constexpr int borderGrabZone = 6;
            int pixelInCell = trackPixel % kCellWidth;

            for (int gi = 0; gi < trackLayout.getNumGroups(); ++gi)
            {
                auto [firstVis, lastVis] = trackLayout.getGroupVisualRange (gi);
                // Check left border
                if (visualIndex == firstVis && pixelInCell < borderGrabZone)
                {
                    isDraggingGroupBorder = true;
                    dragGroupIndex = gi;
                    dragGroupRightEdge = false;
                    return;
                }
                // Check right border
                if (visualIndex == lastVis && (kCellWidth - pixelInCell) < borderGrabZone)
                {
                    isDraggingGroupBorder = true;
                    dragGroupIndex = gi;
                    dragGroupRightEdge = true;
                    return;
                }

                // Also detect clicks just outside the border (one pixel into adjacent column)
                if (visualIndex == firstVis - 1 && (kCellWidth - pixelInCell) < borderGrabZone)
                {
                    isDraggingGroupBorder = true;
                    dragGroupIndex = gi;
                    dragGroupRightEdge = false;
                    return;
                }
                if (visualIndex == lastVis + 1 && pixelInCell < borderGrabZone)
                {
                    isDraggingGroupBorder = true;
                    dragGroupIndex = gi;
                    dragGroupRightEdge = true;
                    return;
                }
            }
        }

        // Check if clicking on a group header band (not near border) to drag group
        if (trackLayout.hasGroups() && event.y < kGroupHeaderHeight)
        {
            int groupIdx = trackLayout.getGroupForTrack (physTrack);
            if (groupIdx >= 0)
            {
                // Drag entire group
                auto& pat = pattern.getCurrentPattern();

                // Select entire group columns (visual range)
                selStartRow = 0;
                selEndRow = pat.numRows - 1;
                auto [gFirst, gLast] = trackLayout.getGroupVisualRange (groupIdx);
                selStartTrack = gFirst;
                selEndTrack = gLast;
                hasSelection = true;
                cursorTrack = physTrack;
                cursorRow = 0;

                isDraggingHeader = true;
                isDraggingGroupAsWhole = true;
                dragGroupDragIndex = groupIdx;
                dragHeaderVisualIndex = visualIndex;

                repaint();
                if (onCursorMoved) onCursorMoved();
                return;
            }
        }

        // Shift-click on header → extend column selection (visual)
        if (event.mods.isShiftDown() && hasSelection)
        {
            auto& pat = pattern.getCurrentPattern();
            selEndTrack = visualIndex;
            selStartRow = 0;
            selEndRow = pat.numRows - 1;
            cursorTrack = physTrack;
            repaint();
            if (onCursorMoved) onCursorMoved();
            return;
        }

        // Left-click on header → select full column + start header drag (visual)
        auto& pat = pattern.getCurrentPattern();
        selStartRow = 0;
        selEndRow = pat.numRows - 1;
        selStartTrack = visualIndex;
        selEndTrack = visualIndex;
        hasSelection = true;
        cursorTrack = physTrack;
        cursorRow = 0;

        isDraggingHeader = true;
        dragHeaderVisualIndex = visualIndex;

        repaint();
        if (onCursorMoved) onCursorMoved();
        return;
    }

    // Click on row number area → select full row
    if (event.x < kRowNumberWidth && event.y >= effectiveHeaderH)
    {
        int clickedRow = (event.y - effectiveHeaderH) / kRowHeight + scrollOffset;
        auto& pat = pattern.getCurrentPattern();
        if (clickedRow >= 0 && clickedRow < pat.numRows)
        {
            if (event.mods.isShiftDown() && hasSelection)
            {
                // Extend existing row selection
                selEndRow = clickedRow;
            }
            else
            {
                selStartRow = clickedRow;
                selEndRow = clickedRow;
            }
            selStartTrack = 0;
            selEndTrack = kNumTracks - 1;
            hasSelection = true;
            cursorRow = clickedRow;
            cursorTrack = 0;
            isDraggingSelection = true;
            repaint();
            if (onCursorMoved) onCursorMoved();
        }
        return;
    }

    int row, track;
    SubColumn subCol;
    if (hitTestGrid (event.x, event.y, row, track, subCol))
    {
        int viTrack = trackLayout.physicalToVisual (track);

        // Right-click on grid cells
        if (event.mods.isPopupMenu())
        {
            if (onGridRightClick)
                onGridRightClick (track, event.getScreenPosition());
            return;
        }

        // Check if clicking inside an existing selection to initiate drag-move
        // Selection bounds are in visual space
        if (hasSelection && ! event.mods.isShiftDown())
        {
            int minRow, maxRow, minViTrack, maxViTrack;
            getSelectionBounds (minRow, maxRow, minViTrack, maxViTrack);
            if (row >= minRow && row <= maxRow && viTrack >= minViTrack && viTrack <= maxViTrack)
            {
                isDraggingBlock = true;
                dragMoveRow = row;
                dragMoveTrack = viTrack;
                dragGrabRowOffset = row - minRow;
                dragGrabTrackOffset = viTrack - minViTrack;
                return;
            }
        }

        if (event.mods.isShiftDown())
        {
            // Extend selection (visual space)
            if (! hasSelection)
            {
                selStartRow = cursorRow;
                selStartTrack = trackLayout.physicalToVisual (cursorTrack);
            }
            selEndRow = row;
            selEndTrack = viTrack;
            hasSelection = true;
        }
        else
        {
            // Start a new drag selection (visual space)
            clearSelection();
            selStartRow = row;
            selStartTrack = viTrack;
            selEndRow = row;
            selEndTrack = viTrack;
            isDraggingSelection = true;
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

void TrackerGrid::mouseDrag (const juce::MouseEvent& event)
{
    int row, track;
    SubColumn subCol;

    if (isDraggingGroupBorder)
    {
        if (dragGroupIndex < 0 || dragGroupIndex >= trackLayout.getNumGroups())
            return;

        int trackPixel = event.x - kRowNumberWidth;
        if (trackPixel < 0) return;

        int visualIndex = trackPixel / kCellWidth + horizontalScrollOffset;
        visualIndex = juce::jlimit (0, kNumTracks - 1, visualIndex);

        auto& group = trackLayout.getGroup (dragGroupIndex);
        auto [curFirst, curLast] = trackLayout.getGroupVisualRange (dragGroupIndex);

        if (dragGroupRightEdge)
        {
            // Extend or shrink right edge
            if (visualIndex > curLast)
            {
                // Add tracks from curLast+1 to visualIndex
                for (int v = curLast + 1; v <= visualIndex; ++v)
                {
                    int phys = trackLayout.visualToPhysical (v);
                    bool alreadyIn = false;
                    for (auto idx : group.trackIndices)
                        if (idx == phys) { alreadyIn = true; break; }
                    if (! alreadyIn)
                        group.trackIndices.push_back (phys);
                }
                repaint();
            }
            else if (visualIndex < curLast && visualIndex >= curFirst)
            {
                // Remove tracks from visualIndex+1 to curLast
                for (int v = curLast; v > visualIndex; --v)
                {
                    int phys = trackLayout.visualToPhysical (v);
                    group.trackIndices.erase (
                        std::remove (group.trackIndices.begin(), group.trackIndices.end(), phys),
                        group.trackIndices.end());
                }
                if (group.trackIndices.empty())
                    trackLayout.removeGroup (dragGroupIndex);
                repaint();
            }
        }
        else
        {
            // Extend or shrink left edge
            if (visualIndex < curFirst)
            {
                for (int v = curFirst - 1; v >= visualIndex; --v)
                {
                    int phys = trackLayout.visualToPhysical (v);
                    bool alreadyIn = false;
                    for (auto idx : group.trackIndices)
                        if (idx == phys) { alreadyIn = true; break; }
                    if (! alreadyIn)
                        group.trackIndices.insert (group.trackIndices.begin(), phys);
                }
                repaint();
            }
            else if (visualIndex > curFirst && visualIndex <= curLast)
            {
                for (int v = curFirst; v < visualIndex; ++v)
                {
                    int phys = trackLayout.visualToPhysical (v);
                    group.trackIndices.erase (
                        std::remove (group.trackIndices.begin(), group.trackIndices.end(), phys),
                        group.trackIndices.end());
                }
                if (group.trackIndices.empty())
                    trackLayout.removeGroup (dragGroupIndex);
                repaint();
            }
        }
    }
    else if (isDraggingHeader)
    {
        int trackPixel = event.x - kRowNumberWidth;
        if (trackPixel >= 0)
        {
            int visualIndex = trackPixel / kCellWidth + horizontalScrollOffset;
            visualIndex = juce::jlimit (0, kNumTracks - 1, visualIndex);

            if (isDraggingGroupAsWhole && dragGroupDragIndex >= 0
                && dragGroupDragIndex < trackLayout.getNumGroups())
            {
                // Move entire group
                auto [gFirst, gLast] = trackLayout.getGroupVisualRange (dragGroupDragIndex);
                int delta = visualIndex - dragHeaderVisualIndex;

                if (delta != 0)
                {
                    // Clamp delta so group stays in bounds
                    if (gFirst + delta < 0) delta = -gFirst;
                    if (gLast + delta >= kNumTracks) delta = kNumTracks - 1 - gLast;

                    if (delta != 0)
                    {
                        // Move group range by delta using moveVisualRange
                        int moveDir = (delta > 0) ? 1 : -1;
                        for (int step = 0; step < std::abs (delta); ++step)
                        {
                            auto [curFirst, curLast] = trackLayout.getGroupVisualRange (dragGroupDragIndex);
                            trackLayout.moveVisualRange (curFirst, curLast, moveDir);
                        }
                        dragHeaderVisualIndex = visualIndex;
                        repaint();
                    }
                }
            }
            else
            {
                // Single track header drag
                // If the dragged track is in a group, constrain to group bounds
                int physTrack = trackLayout.visualToPhysical (dragHeaderVisualIndex);
                int groupIdx = trackLayout.getGroupForTrack (physTrack);
                if (groupIdx >= 0)
                {
                    auto [gFirst, gLast] = trackLayout.getGroupVisualRange (groupIdx);
                    visualIndex = juce::jlimit (gFirst, gLast, visualIndex);
                }

                if (visualIndex != dragHeaderVisualIndex)
                {
                    trackLayout.swapTracks (dragHeaderVisualIndex, visualIndex);
                    dragHeaderVisualIndex = visualIndex;

                    // Update selection to follow the dragged track (visual space)
                    selStartTrack = visualIndex;
                    selEndTrack = visualIndex;
                    cursorTrack = trackLayout.visualToPhysical (visualIndex);

                    repaint();
                }
            }
        }
    }
    else if (isDraggingSelection)
    {
        auto& pat = pattern.getCurrentPattern();
        int effectiveHeaderH = getEffectiveHeaderHeight();
        int visibleRows = getVisibleRowCount();
        int visibleTracks = getVisibleTrackCount();

        if (hitTestGrid (event.x, event.y, row, track, subCol))
        {
            selEndRow = row;
            selEndTrack = trackLayout.physicalToVisual (track);
            cursorRow = row;
            cursorTrack = track;
        }
        else
        {
            // Auto-scroll when dragging past edges
            int trackPixel = event.x - kRowNumberWidth;
            int viFromPixel = trackPixel / kCellWidth + horizontalScrollOffset;
            int rowFromPixel = (event.y - effectiveHeaderH) / kRowHeight + scrollOffset;

            // Clamp to valid range
            viFromPixel = juce::jlimit (0, kNumTracks - 1, viFromPixel);
            rowFromPixel = juce::jlimit (0, pat.numRows - 1, rowFromPixel);

            selEndRow = rowFromPixel;
            selEndTrack = viFromPixel;
            cursorRow = rowFromPixel;
            cursorTrack = trackLayout.visualToPhysical (viFromPixel);

            // Scroll horizontally
            if (event.x > getWidth() - 10 && horizontalScrollOffset + visibleTracks < kNumTracks)
                horizontalScrollOffset++;
            else if (event.x < kRowNumberWidth + 10 && horizontalScrollOffset > 0)
                horizontalScrollOffset--;

            // Scroll vertically
            if (event.y > getHeight() - 10 && scrollOffset + visibleRows < pat.numRows)
                scrollOffset++;
            else if (event.y < effectiveHeaderH + 10 && scrollOffset > 0)
                scrollOffset--;
        }

        if (selStartRow != selEndRow || selStartTrack != selEndTrack)
            hasSelection = true;

        repaint();
    }
    else if (isDraggingBlock)
    {
        if (hitTestGrid (event.x, event.y, row, track, subCol))
        {
            dragMoveRow = row;
            dragMoveTrack = trackLayout.physicalToVisual (track);
            repaint();
        }
    }
}

void TrackerGrid::mouseUp (const juce::MouseEvent& event)
{
    if (isDraggingGroupBorder)
    {
        isDraggingGroupBorder = false;
        dragGroupIndex = -1;
        if (onPatternDataChanged) onPatternDataChanged();
        repaint();
        return;
    }

    if (isDraggingHeader)
    {
        // Header drag complete — layout already updated during drag
        if (onTrackHeaderDragged)
            onTrackHeaderDragged (-1, -1); // signal completion
        isDraggingHeader = false;
        dragHeaderVisualIndex = -1;
        if (onPatternDataChanged) onPatternDataChanged();
        repaint();
        return;
    }

    if (isDraggingBlock)
    {
        // Complete the drag-move: cut from old selection, paste at new position
        // Selection bounds and dragMoveTrack are in visual space
        int row, track;
        SubColumn subCol;
        if (hitTestGrid (event.x, event.y, row, track, subCol) && hasSelection)
        {
            int minRow, maxRow, minViTrack, maxViTrack;
            getSelectionBounds (minRow, maxRow, minViTrack, maxViTrack);

            // Use grab offset so the block anchors from the grab point (all visual)
            int destViTrack = dragMoveTrack - dragGrabTrackOffset;
            int destRow = dragMoveRow - dragGrabRowOffset;
            int rowOffset = destRow - minRow;
            int trackOffset = destViTrack - minViTrack;

            // Only proceed if there's actually a move
            if (rowOffset != 0 || trackOffset != 0)
            {
                auto& pat = pattern.getCurrentPattern();
                int selRows = maxRow - minRow + 1;
                int selTracks = maxViTrack - minViTrack + 1;

                // Copy the selected block (visual columns → physical)
                std::vector<std::vector<Cell>> buffer (static_cast<size_t> (selRows),
                    std::vector<Cell> (static_cast<size_t> (selTracks)));
                for (int r = 0; r < selRows; ++r)
                    for (int t = 0; t < selTracks; ++t)
                    {
                        int phys = trackLayout.visualToPhysical (minViTrack + t);
                        buffer[static_cast<size_t> (r)][static_cast<size_t> (t)] =
                            pat.getCell (minRow + r, phys);
                    }

                // Clear source area
                for (int r = minRow; r <= maxRow; ++r)
                    for (int vi = minViTrack; vi <= maxViTrack; ++vi)
                        pat.getCell (r, trackLayout.visualToPhysical (vi)).clear();

                // Paste at destination (visual columns → physical)
                for (int r = 0; r < selRows; ++r)
                {
                    int dr = destRow + r;
                    if (dr < 0 || dr >= pat.numRows) continue;
                    for (int t = 0; t < selTracks; ++t)
                    {
                        int dvi = destViTrack + t;
                        if (dvi < 0 || dvi >= kNumTracks) continue;
                        int dphys = trackLayout.visualToPhysical (dvi);
                        pat.getCell (dr, dphys) = buffer[static_cast<size_t> (r)][static_cast<size_t> (t)];
                    }
                }

                // Update selection to new position (visual space)
                selStartRow = destRow;
                selStartTrack = destViTrack;
                selEndRow = destRow + selRows - 1;
                selEndTrack = destViTrack + selTracks - 1;
                cursorRow = dragMoveRow;
                cursorTrack = trackLayout.visualToPhysical (dragMoveTrack);

                if (onPatternDataChanged) onPatternDataChanged();
            }
        }
    }

    isDraggingSelection = false;
    isDraggingBlock = false;
    dragMoveRow = -1;
    dragMoveTrack = -1;
    repaint();
}

void TrackerGrid::mouseDoubleClick (const juce::MouseEvent& event)
{
    int effectiveHeaderH = getEffectiveHeaderHeight();

    // Double-click on track header area → rename
    if (event.y < effectiveHeaderH && event.x >= kRowNumberWidth)
    {
        int trackPixel = event.x - kRowNumberWidth;
        int visualIndex = trackPixel / kCellWidth + horizontalScrollOffset;
        if (visualIndex < kNumTracks && onTrackHeaderDoubleClick)
            onTrackHeaderDoubleClick (trackLayout.visualToPhysical (visualIndex), event.getScreenPosition());
    }
}

void TrackerGrid::drawDragPreview (juce::Graphics& g)
{
    if (! isDraggingBlock || ! hasSelection || dragMoveRow < 0)
        return;

    int minRow, maxRow, minViTrack, maxViTrack;
    getSelectionBounds (minRow, maxRow, minViTrack, maxViTrack);

    // All in visual space — grab offset and dragMoveTrack are visual
    int rowOffset = (dragMoveRow - dragGrabRowOffset) - minRow;
    int trackOffset = (dragMoveTrack - dragGrabTrackOffset) - minViTrack;

    int effectiveHeaderH = getEffectiveHeaderHeight();
    int visibleTracks = getVisibleTrackCount();
    int selRows = maxRow - minRow + 1;
    int selTracks = maxViTrack - minViTrack + 1;

    for (int r = 0; r < selRows; ++r)
    {
        int destRow = minRow + rowOffset + r;
        if (destRow < scrollOffset || destRow >= scrollOffset + getVisibleRowCount())
            continue;

        for (int t = 0; t < selTracks; ++t)
        {
            int destVi = minViTrack + trackOffset + t;
            if (destVi < 0 || destVi >= kNumTracks) continue;

            int screenVi = destVi - horizontalScrollOffset;
            if (screenVi < 0 || screenVi >= visibleTracks) continue;

            int x = kRowNumberWidth + screenVi * kCellWidth;
            int y = effectiveHeaderH + (destRow - scrollOffset) * kRowHeight;

            g.setColour (juce::Colour (0x445588cc));
            g.fillRect (x, y, kCellWidth, kRowHeight);
            g.setColour (juce::Colour (0x885588cc));
            g.drawRect (x, y, kCellWidth, kRowHeight, 1);
        }
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

    int visualIndex = trackPixel / kCellWidth + horizontalScrollOffset;
    if (visualIndex >= kNumTracks) return;

    for (auto& f : files)
    {
        juce::File file (f);
        auto ext = file.getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".aiff" || ext == ".aif" || ext == ".flac" || ext == ".ogg" || ext == ".mp3")
        {
            int physTrack = trackLayout.visualToPhysical (visualIndex);
            if (onFileDroppedOnTrack)
                onFileDroppedOnTrack (physTrack, file);
            visualIndex++; // Next file goes to next visual track
            if (visualIndex >= kNumTracks) break;
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

    // Navigate in visual space for track delta
    int cursorVisual = trackLayout.physicalToVisual (cursorTrack);
    int newVisual = cursorVisual + trackDelta;

    // Wrap tracks in visual space
    if (newVisual < 0)
        newVisual = kNumTracks - 1;
    else if (newVisual >= kNumTracks)
        newVisual = 0;

    // Convert back to physical
    int newTrack = trackLayout.visualToPhysical (newVisual);

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
            selStartTrack = trackLayout.physicalToVisual (cursorTrack);
        }
        moveCursor (-1, 0);
        if (shift) { selEndRow = cursorRow; selEndTrack = trackLayout.physicalToVisual (cursorTrack); }
        else clearSelection();
        return true;
    }
    if (keyCode == juce::KeyPress::downKey)
    {
        if (shift && ! hasSelection)
        {
            hasSelection = true;
            selStartRow = cursorRow;
            selStartTrack = trackLayout.physicalToVisual (cursorTrack);
        }
        moveCursor (1, 0);
        if (shift) { selEndRow = cursorRow; selEndTrack = trackLayout.physicalToVisual (cursorTrack); }
        else clearSelection();
        return true;
    }
    if (keyCode == juce::KeyPress::leftKey)
    {
        if (shift && ! hasSelection)
        {
            hasSelection = true;
            selStartRow = cursorRow;
            selStartTrack = trackLayout.physicalToVisual (cursorTrack);
        }
        moveCursor (0, -1);
        if (shift) { selEndRow = cursorRow; selEndTrack = trackLayout.physicalToVisual (cursorTrack); }
        else clearSelection();
        return true;
    }
    if (keyCode == juce::KeyPress::rightKey)
    {
        if (shift && ! hasSelection)
        {
            hasSelection = true;
            selStartRow = cursorRow;
            selStartTrack = trackLayout.physicalToVisual (cursorTrack);
        }
        moveCursor (0, 1);
        if (shift) { selEndRow = cursorRow; selEndTrack = trackLayout.physicalToVisual (cursorTrack); }
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
        if (onPatternDataChanged) onPatternDataChanged();
        repaint();
        return true;
    }

    // Note-off with backtick
    if (key.getTextCharacter() == '`' && cursorSubColumn == SubColumn::Note)
    {
        Cell& cell = pattern.getCell (cursorRow, cursorTrack);
        cell.note = 255; // note-off marker
        cell.instrument = currentInstrument;
        if (onPatternDataChanged) onPatternDataChanged();
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
            if (onPatternDataChanged) onPatternDataChanged();

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
            if (onPatternDataChanged) onPatternDataChanged();
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
            if (onPatternDataChanged) onPatternDataChanged();
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
            if (onPatternDataChanged) onPatternDataChanged();
            repaint();
            return true;
        }
    }

    return false;
}
