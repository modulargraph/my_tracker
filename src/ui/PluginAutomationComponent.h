#pragma once

#include <JuceHeader.h>
#include "PluginAutomationData.h"
#include "TrackerLookAndFeel.h"
#include "PatternData.h"
#include <set>

//==============================================================================
// Info about an automatable plugin target (for dropdown population)
//==============================================================================

struct AutomatablePluginInfo
{
    juce::String pluginId;       // Unique identifier (e.g. "inst:3" or insert identifier)
    juce::String displayName;    // Shown in dropdown
    int owningTrack = -1;        // Track that owns this plugin
    bool isInstrument = false;   // True if this is an instrument plugin

    // Parameter list for this plugin (populated when selected)
    struct ParamInfo
    {
        int index = 0;
        juce::String name;
        bool hasAutomation = false;  // True if this param has automation data in the current pattern
    };
    std::vector<ParamInfo> parameters;
};

//==============================================================================
// Bottom automation panel for drawing per-pattern plugin parameter curves
//==============================================================================

class PluginAutomationComponent : public juce::Component
{
public:
    static constexpr int kDefaultPanelHeight = 140;
    static constexpr int kMinPanelHeight = 80;
    static constexpr int kMaxPanelHeight = 400;
    static constexpr int kControlsWidth = 180;
    static constexpr int kDragHandleHeight = 5;

    // Kept for backward compat with MainComponent layout queries
    static constexpr int kPanelHeight = kDefaultPanelHeight;

    PluginAutomationComponent (TrackerLookAndFeel& lnf);
    ~PluginAutomationComponent() override = default;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& key) override;

    //==============================================================================
    // Data binding

    void setAutomationData (PatternAutomationData* data);
    void setPatternLength (int numRows);
    void setCurrentTrack (int trackIndex);
    void setBaseline (float baselineValue);
    void setAvailablePlugins (const std::vector<AutomatablePluginInfo>& plugins);
    juce::String getSelectedPluginId() const;
    int getSelectedParameterIndex() const;

    //==============================================================================
    // Resizable panel

    int getPanelHeight() const { return panelHeight; }
    void setPanelHeight (int h);

    //==============================================================================
    // Playback position

    void setPlaybackRow (int row);

    //==============================================================================
    // Snap to grid

    void setSnapToGrid (bool snap);
    bool getSnapToGrid() const { return snapToGrid; }

    //==============================================================================
    // Freehand draw mode

    void setDrawMode (bool freehand);
    bool getDrawMode() const { return drawMode; }

    //==============================================================================
    // Zoom

    void setZoomLevel (float zoom);
    float getZoomLevel() const { return zoomLevel; }

    //==============================================================================
    // Multi-lane overlay

    void setOverlayEnabled (bool enabled);
    bool getOverlayEnabled() const { return overlayEnabled; }

    //==============================================================================
    // Selection and clipboard

    void selectAll();
    void deleteSelected();
    void copySelected();
    void pasteFromClipboard();

    //==============================================================================
    // Undo / redo

    void undo();
    void redo();

    //==============================================================================
    // Recording

    void setRecording (bool recording);
    bool isRecording() const { return recordingEnabled; }
    void recordParameterValue (int row, float value);

    //==============================================================================
    // Navigate to a specific plugin/param (click-to-automate)

    void navigateToParam (const juce::String& pluginId, int paramIndex);

    //==============================================================================
    // Callbacks

    std::function<void()> onAutomationChanged;
    std::function<void (const juce::String& pluginId)> onPluginSelected;
    std::function<void (const juce::String& pluginId, int paramIndex)> onParameterSelected;
    std::function<void (int newHeight)> onPanelHeightChanged;
    std::function<float()> onGetCurrentParameterValue;

    //==============================================================================
    // Lane color palette

    static juce::Colour getLaneColour (int index);

private:
    TrackerLookAndFeel& lookAndFeel;

    // Data
    PatternAutomationData* automationData = nullptr;
    int patternLength = 64;
    int currentTrack = 0;
    float baseline = 0.5f;

    // UI controls
    juce::ComboBox pluginDropdown;
    juce::ComboBox parameterDropdown;
    juce::ComboBox curveTypeDropdown;
    juce::Label pluginLabel;
    juce::Label paramLabel;
    juce::TextButton snapButton { "Snap" };
    juce::TextButton drawButton { "Draw" };
    juce::TextButton recButton { "Rec" };
    juce::TextButton overlayButton { "Ovl" };

    // Available plugins and their parameter lists
    std::vector<AutomatablePluginInfo> availablePlugins;
    bool suppressSelectionCallbacks = false;

    // Panel height (resizable)
    int panelHeight = kDefaultPanelHeight;
    bool resizeDragging = false;
    int resizeDragStartY = 0;
    int resizeDragStartHeight = 0;

    // Graph area
    juce::Rectangle<int> getGraphBounds() const;

    // Point interaction
    int dragPointIndex = -1;
    bool isDragging = false;

    // Snap to grid
    bool snapToGrid = false;

    // Freehand draw mode
    bool drawMode = false;
    int lastDrawRow = -1;

    // Zoom
    float zoomLevel = 1.0f;
    float viewStartRow = 0.0f;
    void clampViewToPattern();

    // Playback position
    int playbackRow = -1;

    // Overlay (show all lanes for selected plugin)
    bool overlayEnabled = false;

    // Selection
    std::set<int> selectedPoints;
    bool isSelecting = false;
    juce::Point<float> selectionStart;
    juce::Point<float> selectionEnd;
    bool isMovingSelection = false;
    juce::Point<float> moveSelectionAnchor;

    // Clipboard
    struct ClipboardData
    {
        std::vector<AutomationPoint> points;
        int minRow = 0;
    };
    static ClipboardData clipboard;

    // Undo / redo
    struct UndoSnapshot
    {
        std::vector<AutomationPoint> points;
        juce::String pluginId;
        int parameterId = -1;
    };
    std::vector<UndoSnapshot> undoStack;
    std::vector<UndoSnapshot> redoStack;
    static constexpr int kMaxUndoSteps = 50;
    void pushUndoState();
    UndoSnapshot captureCurrentState() const;
    void restoreState (const UndoSnapshot& state);

    // Recording
    bool recordingEnabled = false;

    // Hover
    int hoverPointIndex = -1;
    juce::Point<float> hoverScreenPos;
    bool showHoverTooltip = false;

    // Convert between screen coords and data coords
    juce::Point<float> dataToScreen (float row, float value) const;
    juce::Point<float> screenToData (juce::Point<float> screenPos) const;
    int findPointNear (juce::Point<float> screenPos, float maxDist = 8.0f) const;
    int getSelectedPluginOwnerTrack() const;
    void notifyAndRepaint();

    // Get the current automation lane (or nullptr)
    AutomationLane* getCurrentLane();
    const AutomationLane* getCurrentLane() const;

    // Selection helpers
    juce::Rectangle<float> getSelectionRect() const;
    void updateSelectionFromRect();

    // Apply snap
    int snapRow (int row) const;

    void pluginSelectionChanged();
    void parameterSelectionChanged();
    void curveTypeChanged();
    void drawGrid (juce::Graphics& g, juce::Rectangle<int> bounds) const;
    void drawCurve (juce::Graphics& g, juce::Rectangle<int> bounds,
                    const AutomationLane* lane, juce::Colour colour, float alpha) const;
    void drawBaseline (juce::Graphics& g, juce::Rectangle<int> bounds) const;
    void drawPoints (juce::Graphics& g, juce::Rectangle<int> bounds) const;
    void drawPlaybackPosition (juce::Graphics& g, juce::Rectangle<int> bounds) const;
    void drawDragHandle (juce::Graphics& g) const;
    void drawHoverTooltip (juce::Graphics& g) const;
    void drawSelectionRect (juce::Graphics& g) const;
    void drawOverlayLanes (juce::Graphics& g, juce::Rectangle<int> bounds) const;

    // Default curve type for new points
    AutomationCurveType getSelectedCurveType() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginAutomationComponent)
};
