#pragma once

#include <JuceHeader.h>
#include "PatternData.h"
#include "TrackerLookAndFeel.h"
#include "TrackLayout.h"

enum class SubColumn { Note, Instrument, Volume, FX };

class TrackerGrid : public juce::Component,
                    public juce::FileDragAndDropTarget
{
public:
    static constexpr int kMasterLaneTrack = kNumTracks;

    TrackerGrid (PatternData& patternData, TrackerLookAndFeel& lnf, TrackLayout& layout);

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void mouseDoubleClick (const juce::MouseEvent& event) override;
    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    // Cursor control
    int getCursorRow() const { return cursorRow; }
    int getCursorTrack() const { return cursorTrack; }
    bool isCursorInMasterLane() const { return cursorTrack == kMasterLaneTrack; }
    SubColumn getCursorSubColumn() const { return cursorSubColumn; }
    void setCursorPosition (int row, int track);

    // Playback cursor
    void setPlaybackRow (int row);
    int getPlaybackRow() const { return playbackRow; }
    void setPlaying (bool playing);

    // Scroll access (for follow mode)
    int getScrollOffset() const { return scrollOffset; }
    void setScrollOffset (int offset);
    int getVisibleRowCount() const;
    void setRowsPerBeat (int rpb) { rowsPerBeat = juce::jlimit (1, 16, rpb); repaint(); }
    int getRowsPerBeat() const { return rowsPerBeat; }

    // Edit step (rows to advance after note entry)
    void setEditStep (int step) { editStep = step; }
    int getEditStep() const { return editStep; }

    // Current octave for note entry
    void setOctave (int oct) { currentOctave = juce::jlimit (0, 9, oct); }
    int getOctave() const { return currentOctave; }

    // Current instrument for note entry
    void setCurrentInstrument (int inst) { currentInstrument = juce::jlimit (0, 255, inst); }
    int getCurrentInstrument() const { return currentInstrument; }

    // Selection
    bool hasSelection = false;
    int selStartRow = 0, selStartTrack = 0;
    int selEndRow = 0, selEndTrack = 0;
    void clearSelection();
    void getSelectionBounds (int& minRow, int& maxRow, int& minTrack, int& maxTrack) const;

    // Mute/Solo display
    std::array<bool, kNumTracks> trackMuted {};
    std::array<bool, kNumTracks> trackSoloed {};
    std::array<bool, kNumTracks> trackHasSample {};

    // Callback for when a note is entered (for preview)
    std::function<void (int note, int instrument)> onNoteEntered;
    // Callback for any pattern data change (note entry, hex edit, delete, etc.)
    std::function<void()> onPatternDataChanged;
    // Callback for status bar updates
    std::function<void()> onCursorMoved;
    // Callback for right-click on track header
    std::function<void (int track, juce::Point<int> screenPos)> onTrackHeaderRightClick;
    // Callback for right-click on grid cells (for context menu)
    std::function<void (int track, juce::Point<int> screenPos)> onGridRightClick;
    // Callback for double-click on track header (for renaming)
    std::function<void (int track, juce::Point<int> screenPos)> onTrackHeaderDoubleClick;
    // Callback for drag-drop reorder of track header
    std::function<void (int fromVisual, int toVisual)> onTrackHeaderDragged;
    // Callback for file drop on track header
    std::function<void (int track, const juce::File& file)> onFileDroppedOnTrack;
    // Callback for note mode toggle (K/R) on track header
    std::function<void (int track)> onNoteModeToggled;
    // Callback for validating note entry (returns empty string if allowed, error message if blocked)
    std::function<juce::String (int instrumentIndex, int trackIndex)> onValidateNoteEntry;

    // Layout constants (public for toolbar/status)
    static constexpr int kRowNumberWidth = 30;
    static constexpr int kHeaderHeight = 22;
    static constexpr int kRowHeight = 18;

    // Sub-column widths within a cell
    static constexpr int kNoteWidth = 28;
    static constexpr int kInstWidth = 18;
    static constexpr int kVolWidth = 18;
    static constexpr int kFxWidth = 26;  // Increased from 22 for proper 3-char display
    static constexpr int kCellPadding = 2;
    static constexpr int kSubColSpace = 2; // Space between sub-columns (was 4)
    static constexpr int kGroupHeaderHeight = 16;

    // Width of one note lane (Note + space + Inst + space + Vol + space)
    static constexpr int kNoteLaneWidth = kNoteWidth + kSubColSpace + kInstWidth
                                        + kSubColSpace + kVolWidth + kSubColSpace;

    // Base cell width (1 note lane, 1 FX lane): padding + NoteLane + FX
    static constexpr int kBaseCellWidth = kCellPadding + kNoteLaneWidth + kFxWidth;

    // Compute cell width for a track with the given number of note and FX lanes
    static int getCellWidth (int fxLaneCount, int noteLaneCount = 1)
    {
        return kCellPadding
             + noteLaneCount * kNoteLaneWidth
             + fxLaneCount * kFxWidth + (fxLaneCount - 1) * kSubColSpace;
    }

    // Current cursor note lane index (which note lane the cursor is in)
    int getCursorNoteLane() const { return cursorNoteLane; }

    // Current cursor FX lane index (which FX lane the cursor is in)
    int getCursorFxLane() const { return cursorFxLane; }

    // FX command dropdown
    void showFxCommandPopup();
    void showFxCommandPopupAt (juce::Point<int> screenPos);

    // Undo manager for undoable edits (delete, drag-move)
    void setUndoManager (juce::UndoManager* um) { undoManager = um; }

private:
    juce::UndoManager* undoManager = nullptr;
    PatternData& pattern;
    TrackerLookAndFeel& lookAndFeel;
    TrackLayout& trackLayout;

    int cursorRow = 0;
    int cursorTrack = 0;
    SubColumn cursorSubColumn = SubColumn::Note;
    int cursorNoteLane = 0; // Which note lane the cursor is in when SubColumn::Note/Instrument/Volume
    int cursorFxLane = 0;  // Which FX lane the cursor is in when SubColumn::FX
    int playbackRow = -1;
    bool isPlaying = false;
    int editStep = 0;
    int currentOctave = 4;
    int currentInstrument = 0;
    int rowsPerBeat = 4;

    // Hex entry state for multi-digit input
    int hexDigitCount = 0;
    int hexAccumulator = 0;

    // Drag selection / drag-move state
    bool isDraggingSelection = false;
    bool isDraggingBlock = false;
    bool isDraggingHeader = false;
    bool isDraggingGroupBorder = false;
    bool isDraggingGroupAsWhole = false;
    int dragGroupDragIndex = -1; // group index being dragged as whole
    int dragHeaderVisualIndex = -1;
    int dragGroupIndex = -1;
    bool dragGroupRightEdge = false; // true = right edge, false = left edge
    bool layoutDragSnapshotValid = false;
    TrackLayout::Snapshot layoutDragStartSnapshot;
    int dragMoveRow = -1;
    int dragMoveTrack = -1;
    int dragGrabRowOffset = 0;   // offset from selection top-left to grab point
    int dragGrabTrackOffset = 0;

    // Rendering helper for drag-move preview
    void drawDragPreview (juce::Graphics& g);

    // Scrolling
    int scrollOffset = 0;
    int horizontalScrollOffset = 0;
    int getVisibleTrackCount() const;
    int getTotalVisualColumns() const { return kNumTracks + 1; }
    bool isMasterVisualColumn (int visualIndex) const { return visualIndex == kNumTracks; }
    int visualToTrackIndex (int visualIndex) const;
    int trackToVisualIndex (int trackIndex) const;
    bool isMasterTrack (int trackIndex) const { return trackIndex == kMasterLaneTrack; }
    void ensureCursorVisible();

    // Rendering helpers
    void drawHeaders (juce::Graphics& g);
    void drawRowNumbers (juce::Graphics& g);
    void drawCells (juce::Graphics& g);
    void drawCell (juce::Graphics& g, const Cell& cell, int x, int y, int width,
                   bool isCursor, bool isCurrentRow, bool isPlaybackRow, int track, int fxLaneCount);
    void drawMasterCell (juce::Graphics& g, const Pattern& pat, int row, int x, int y, int width,
                         bool isCursor, bool isCurrentRow, bool isPlaybackRow);
    void drawSelection (juce::Graphics& g);
    void drawGroupHeaders (juce::Graphics& g);
    void fillCellBackground (juce::Graphics& g, int x, int y, int width,
                             bool isCursor, bool isCurrentRow, bool isPlaybackRow) const;
    void drawCursorSubColumnHighlight (juce::Graphics& g, int x, int y, int width) const;
    int getEffectiveHeaderHeight() const;

    // Note name helper
    static juce::String noteToString (int note);

    // Keyboard mapping for note entry
    int keyToNote (const juce::KeyPress& key) const;

    // Hex character helper
    static int hexCharToValue (juce::juce_wchar c);

    // FX popup helpers
    juce::Rectangle<int> getFxPopupTargetRect() const;
    bool applyFxCommandAtCursor (char commandLetter);
    void showFxCommandPopupWithOptions (const juce::PopupMenu::Options& options);

    // Navigation
    void moveCursor (int rowDelta, int trackDelta);

    // Hit test: convert pixel position to grid coordinates
    bool hitTestGrid (int x, int y, int& outRow, int& outTrack, SubColumn& outSubCol) const;
    bool hitTestGrid (int x, int y, int& outRow, int& outTrack, SubColumn& outSubCol, int& outFxLane) const;
    bool hitTestGrid (int x, int y, int& outRow, int& outTrack, SubColumn& outSubCol, int& outFxLane, int& outNoteLane) const;

    // Variable-width track layout helpers
    int getTrackXOffset (int visualIndex) const;  // pixel X for a visual track index
    int getTrackWidth (int visualIndex) const;     // pixel width for a visual track
    int visualTrackAtPixel (int pixelX) const;     // visual track index at pixel X (relative to row number area)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackerGrid)
};
