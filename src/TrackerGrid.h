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
    SubColumn getCursorSubColumn() const { return cursorSubColumn; }
    void setCursorPosition (int row, int track);

    // Playback cursor
    void setPlaybackRow (int row);
    int getPlaybackRow() const { return playbackRow; }
    void setPlaying (bool playing);

    // Edit step (rows to advance after note entry)
    void setEditStep (int step) { editStep = step; }
    int getEditStep() const { return editStep; }

    // Current octave for note entry
    void setOctave (int oct) { currentOctave = juce::jlimit (0, 9, oct); }
    int getOctave() const { return currentOctave; }

    // Current instrument for note entry
    void setCurrentInstrument (int inst) { currentInstrument = inst; }
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

    // Layout constants (public for toolbar/status)
    static constexpr int kRowNumberWidth = 30;
    static constexpr int kHeaderHeight = 22;
    static constexpr int kRowHeight = 18;
    static constexpr int kCellWidth = 90;

    // Sub-column widths within a cell
    static constexpr int kNoteWidth = 28;
    static constexpr int kInstWidth = 18;
    static constexpr int kVolWidth = 18;
    static constexpr int kFxWidth = 22;
    static constexpr int kCellPadding = 4;
    static constexpr int kGroupHeaderHeight = 16;

private:
    PatternData& pattern;
    TrackerLookAndFeel& lookAndFeel;
    TrackLayout& trackLayout;

    int cursorRow = 0;
    int cursorTrack = 0;
    SubColumn cursorSubColumn = SubColumn::Note;
    int playbackRow = -1;
    bool isPlaying = false;
    int editStep = 1;
    int currentOctave = 4;
    int currentInstrument = 0;

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
    int dragMoveRow = -1;
    int dragMoveTrack = -1;
    int dragGrabRowOffset = 0;   // offset from selection top-left to grab point
    int dragGrabTrackOffset = 0;

    // Rendering helper for drag-move preview
    void drawDragPreview (juce::Graphics& g);

    // Scrolling
    int scrollOffset = 0;
    int horizontalScrollOffset = 0;
    int getVisibleRowCount() const;
    int getVisibleTrackCount() const;
    void ensureCursorVisible();

    // Rendering helpers
    void drawHeaders (juce::Graphics& g);
    void drawRowNumbers (juce::Graphics& g);
    void drawCells (juce::Graphics& g);
    void drawCell (juce::Graphics& g, const Cell& cell, int x, int y, int width,
                   bool isCursor, bool isCurrentRow, bool isPlaybackRow, int track);
    void drawSelection (juce::Graphics& g);
    void drawGroupHeaders (juce::Graphics& g);
    int getEffectiveHeaderHeight() const;

    // Note name helper
    static juce::String noteToString (int note);

    // Keyboard mapping for note entry
    int keyToNote (const juce::KeyPress& key) const;

    // Hex character helper
    static int hexCharToValue (juce::juce_wchar c);

    // Navigation
    void moveCursor (int rowDelta, int trackDelta);

    // Hit test: convert pixel position to grid coordinates
    bool hitTestGrid (int x, int y, int& outRow, int& outTrack, SubColumn& outSubCol) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackerGrid)
};
