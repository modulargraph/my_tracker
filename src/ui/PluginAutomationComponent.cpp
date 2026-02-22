#include "PluginAutomationComponent.h"

// Static clipboard shared across instances
PluginAutomationComponent::ClipboardData PluginAutomationComponent::clipboard;

//==============================================================================
// Lane colour palette
//==============================================================================

static constexpr juce::uint32 kLaneColours[] = {
    0xff44aaff, // blue (primary)
    0xffff6644, // orange
    0xff44ff88, // green
    0xffff44aa, // pink
    0xffaaff44, // lime
    0xff44ffff, // cyan
    0xffffaa44, // amber
    0xffaa44ff  // purple
};

juce::Colour PluginAutomationComponent::getLaneColour (int index)
{
    return juce::Colour (kLaneColours[static_cast<size_t> (index) % 8]);
}

//==============================================================================
// Constructor
//==============================================================================

PluginAutomationComponent::PluginAutomationComponent (TrackerLookAndFeel& lnf)
    : lookAndFeel (lnf)
{
    setWantsKeyboardFocus (true);

    pluginLabel.setText ("Plugin:", juce::dontSendNotification);
    pluginLabel.setColour (juce::Label::textColourId,
                           lnf.findColour (TrackerLookAndFeel::textColourId));
    pluginLabel.setFont (lnf.getMonoFont (11.0f));
    addAndMakeVisible (pluginLabel);

    paramLabel.setText ("Param:", juce::dontSendNotification);
    paramLabel.setColour (juce::Label::textColourId,
                          lnf.findColour (TrackerLookAndFeel::textColourId));
    paramLabel.setFont (lnf.getMonoFont (11.0f));
    addAndMakeVisible (paramLabel);

    pluginDropdown.setTextWhenNothingSelected ("(none)");
    pluginDropdown.onChange = [this] { pluginSelectionChanged(); };
    addAndMakeVisible (pluginDropdown);

    parameterDropdown.setTextWhenNothingSelected ("(none)");
    parameterDropdown.onChange = [this] { parameterSelectionChanged(); };
    addAndMakeVisible (parameterDropdown);

    // Curve type dropdown
    curveTypeDropdown.addItem ("Linear", 1);
    curveTypeDropdown.addItem ("Step", 2);
    curveTypeDropdown.addItem ("Smooth", 3);
    curveTypeDropdown.addItem ("S-Curve", 4);
    curveTypeDropdown.setSelectedId (1, juce::dontSendNotification);
    curveTypeDropdown.onChange = [this] { curveTypeChanged(); };
    addAndMakeVisible (curveTypeDropdown);

    // Toggle buttons
    auto buttonColour = lnf.findColour (TrackerLookAndFeel::textColourId);
    auto setupButton = [&] (juce::TextButton& btn)
    {
        btn.setColour (juce::TextButton::textColourOffId, buttonColour);
        btn.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1e1e2e));
        btn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff44aaff));
        btn.setClickingTogglesState (true);
        addAndMakeVisible (btn);
    };

    setupButton (snapButton);
    setupButton (drawButton);
    setupButton (overlayButton);

    recButton.setColour (juce::TextButton::textColourOffId, buttonColour);
    recButton.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1e1e2e));
    recButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffff4444));
    recButton.setClickingTogglesState (true);
    addAndMakeVisible (recButton);

    snapButton.onClick = [this] { snapToGrid = snapButton.getToggleState(); repaint(); };
    drawButton.onClick = [this] { drawMode = drawButton.getToggleState(); repaint(); };
    recButton.onClick = [this] { recordingEnabled = recButton.getToggleState(); repaint(); };
    overlayButton.onClick = [this] { overlayEnabled = overlayButton.getToggleState(); repaint(); };
}

//==============================================================================
// Paint
//==============================================================================

void PluginAutomationComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // Draw resize handle at top
    drawDragHandle (g);

    // Background
    auto bodyBounds = bounds.withTrimmedTop (kDragHandleHeight);
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId).brighter (0.05f));
    g.fillRect (bodyBounds);

    // Top border
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (kDragHandleHeight, 0.0f, static_cast<float> (bounds.getWidth()));

    // Graph area
    auto graphBounds = getGraphBounds();
    if (graphBounds.isEmpty())
        return;

    drawGrid (g, graphBounds);
    drawBaseline (g, graphBounds);

    // Overlay lanes (other params from same plugin, drawn first so active lane is on top)
    if (overlayEnabled)
        drawOverlayLanes (g, graphBounds);

    drawCurve (g, graphBounds, getCurrentLane(), getLaneColour (0), 0.8f);
    drawPoints (g, graphBounds);
    drawPlaybackPosition (g, graphBounds);
    drawSelectionRect (g);
    drawHoverTooltip (g);

    // Label: "AUTOMATION" in top-left of graph area
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.3f));
    g.setFont (lookAndFeel.getMonoFont (10.0f));
    g.drawText ("AUTOMATION", graphBounds.getX() + 4, graphBounds.getY() + 2, 100, 12,
                juce::Justification::centredLeft);

    // Zoom indicator
    if (zoomLevel > 1.01f)
    {
        auto zoomText = juce::String (zoomLevel, 1) + "x";
        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.4f));
        g.drawText (zoomText, graphBounds.getRight() - 40, graphBounds.getY() + 2, 36, 12,
                    juce::Justification::centredRight);
    }
}

//==============================================================================
// Resized
//==============================================================================

void PluginAutomationComponent::resized()
{
    auto bounds = getLocalBounds().withTrimmedTop (kDragHandleHeight).reduced (4);

    // Controls on the left
    auto controlArea = bounds.removeFromLeft (kControlsWidth);
    controlArea.removeFromTop (2);

    pluginLabel.setBounds (controlArea.removeFromTop (14));
    pluginDropdown.setBounds (controlArea.removeFromTop (20).reduced (0, 1));
    controlArea.removeFromTop (2);
    paramLabel.setBounds (controlArea.removeFromTop (14));
    parameterDropdown.setBounds (controlArea.removeFromTop (20).reduced (0, 1));
    controlArea.removeFromTop (2);

    // Toggle buttons row
    auto buttonRow = controlArea.removeFromTop (20);
    int btnW = (buttonRow.getWidth() - 6) / 4;
    snapButton.setBounds (buttonRow.removeFromLeft (btnW));
    buttonRow.removeFromLeft (2);
    drawButton.setBounds (buttonRow.removeFromLeft (btnW));
    buttonRow.removeFromLeft (2);
    recButton.setBounds (buttonRow.removeFromLeft (btnW));
    buttonRow.removeFromLeft (2);
    overlayButton.setBounds (buttonRow);

    controlArea.removeFromTop (2);

    // Curve type dropdown
    curveTypeDropdown.setBounds (controlArea.removeFromTop (20).reduced (0, 1));
}

juce::Rectangle<int> PluginAutomationComponent::getGraphBounds() const
{
    auto bounds = getLocalBounds().withTrimmedTop (kDragHandleHeight).reduced (4);
    bounds.removeFromLeft (kControlsWidth + 4);
    bounds.removeFromTop (2);
    bounds.removeFromBottom (2);
    return bounds;
}

//==============================================================================
// Data binding
//==============================================================================

void PluginAutomationComponent::setAutomationData (PatternAutomationData* data)
{
    automationData = data;
    undoStack.clear();
    redoStack.clear();
    selectedPoints.clear();
    repaint();
}

void PluginAutomationComponent::setPatternLength (int numRows)
{
    patternLength = juce::jmax (1, numRows);
    clampViewToPattern();
    repaint();
}

void PluginAutomationComponent::setCurrentTrack (int trackIndex)
{
    currentTrack = trackIndex;
}

void PluginAutomationComponent::setBaseline (float baselineValue)
{
    baseline = juce::jlimit (0.0f, 1.0f, baselineValue);
    repaint();
}

void PluginAutomationComponent::setAvailablePlugins (const std::vector<AutomatablePluginInfo>& plugins)
{
    juce::ScopedValueSetter<bool> suppressCallbacks (suppressSelectionCallbacks, true);

    auto previousPluginId = getSelectedPluginId();
    int previousParamIdx = getSelectedParameterIndex();
    availablePlugins = plugins;

    pluginDropdown.clear (juce::dontSendNotification);

    for (int i = 0; i < static_cast<int> (availablePlugins.size()); ++i)
        pluginDropdown.addItem (availablePlugins[static_cast<size_t> (i)].displayName, i + 1);

    // Try to re-select the previous plugin (and parameter)
    if (previousPluginId.isNotEmpty())
    {
        for (int i = 0; i < static_cast<int> (availablePlugins.size()); ++i)
        {
            if (availablePlugins[static_cast<size_t> (i)].pluginId == previousPluginId)
            {
                pluginDropdown.setSelectedId (i + 1, juce::dontSendNotification);

                // Rebuild parameter dropdown, then restore the previous parameter
                parameterDropdown.clear (juce::dontSendNotification);
                auto& params = availablePlugins[static_cast<size_t> (i)].parameters;
                for (int pi = 0; pi < static_cast<int> (params.size()); ++pi)
                {
                    auto& p = params[static_cast<size_t> (pi)];
                    auto displayName = p.hasAutomation ? ("* " + p.name) : p.name;
                    parameterDropdown.addItem (displayName, pi + 1);
                }

                // Restore previous parameter if it still exists
                bool restored = false;
                if (previousParamIdx >= 0)
                {
                    for (int pi = 0; pi < static_cast<int> (params.size()); ++pi)
                    {
                        if (params[static_cast<size_t> (pi)].index == previousParamIdx)
                        {
                            parameterDropdown.setSelectedId (pi + 1, juce::dontSendNotification);
                            parameterSelectionChanged();
                            restored = true;
                            break;
                        }
                    }
                }

                if (! restored && ! params.empty())
                {
                    parameterDropdown.setSelectedId (1, juce::dontSendNotification);
                    parameterSelectionChanged();
                }

                repaint();
                return;
            }
        }
    }

    // Auto-select first if nothing matched
    if (! availablePlugins.empty())
    {
        pluginDropdown.setSelectedId (1, juce::dontSendNotification);
        pluginSelectionChanged();
    }
    else
    {
        parameterDropdown.clear (juce::dontSendNotification);
        repaint();
    }
}

juce::String PluginAutomationComponent::getSelectedPluginId() const
{
    int idx = pluginDropdown.getSelectedId() - 1;
    if (idx >= 0 && idx < static_cast<int> (availablePlugins.size()))
        return availablePlugins[static_cast<size_t> (idx)].pluginId;
    return {};
}

int PluginAutomationComponent::getSelectedParameterIndex() const
{
    int idx = pluginDropdown.getSelectedId() - 1;
    if (idx < 0 || idx >= static_cast<int> (availablePlugins.size()))
        return -1;

    int paramIdx = parameterDropdown.getSelectedId() - 1;
    auto& params = availablePlugins[static_cast<size_t> (idx)].parameters;
    if (paramIdx >= 0 && paramIdx < static_cast<int> (params.size()))
        return params[static_cast<size_t> (paramIdx)].index;

    return -1;
}

//==============================================================================
// Resizable panel
//==============================================================================

void PluginAutomationComponent::setPanelHeight (int h)
{
    panelHeight = juce::jlimit (kMinPanelHeight, kMaxPanelHeight, h);
}

//==============================================================================
// Playback position
//==============================================================================

void PluginAutomationComponent::setPlaybackRow (int row)
{
    if (playbackRow != row)
    {
        playbackRow = row;
        repaint();
    }
}

//==============================================================================
// Snap / Draw mode
//==============================================================================

void PluginAutomationComponent::setSnapToGrid (bool snap)
{
    snapToGrid = snap;
    snapButton.setToggleState (snap, juce::dontSendNotification);
}

void PluginAutomationComponent::setDrawMode (bool freehand)
{
    drawMode = freehand;
    drawButton.setToggleState (freehand, juce::dontSendNotification);
}

//==============================================================================
// Zoom
//==============================================================================

void PluginAutomationComponent::setZoomLevel (float zoom)
{
    zoomLevel = juce::jlimit (1.0f, 16.0f, zoom);
    clampViewToPattern();
    repaint();
}

void PluginAutomationComponent::clampViewToPattern()
{
    float visibleRange = static_cast<float> (patternLength) / zoomLevel;
    float maxStart = static_cast<float> (patternLength) - visibleRange;
    viewStartRow = juce::jlimit (0.0f, juce::jmax (0.0f, maxStart), viewStartRow);
}

//==============================================================================
// Multi-lane overlay
//==============================================================================

void PluginAutomationComponent::setOverlayEnabled (bool enabled)
{
    overlayEnabled = enabled;
    overlayButton.setToggleState (enabled, juce::dontSendNotification);
    repaint();
}

//==============================================================================
// Plugin/parameter selection
//==============================================================================

void PluginAutomationComponent::pluginSelectionChanged()
{
    auto pluginId = getSelectedPluginId();

    parameterDropdown.clear (juce::dontSendNotification);

    if (! suppressSelectionCallbacks && pluginId.isNotEmpty() && onPluginSelected)
        onPluginSelected (pluginId);

    int idx = pluginDropdown.getSelectedId() - 1;
    if (idx >= 0 && idx < static_cast<int> (availablePlugins.size()))
    {
        auto& params = availablePlugins[static_cast<size_t> (idx)].parameters;
        for (int i = 0; i < static_cast<int> (params.size()); ++i)
        {
            auto& p = params[static_cast<size_t> (i)];
            // Mark automated params with * prefix so the user can identify them
            auto displayName = p.hasAutomation ? ("* " + p.name) : p.name;
            parameterDropdown.addItem (displayName, i + 1);
        }

        if (! params.empty())
        {
            parameterDropdown.setSelectedId (1, juce::dontSendNotification);
            parameterSelectionChanged();
        }
    }

    selectedPoints.clear();
    undoStack.clear();
    redoStack.clear();
    repaint();
}

void PluginAutomationComponent::parameterSelectionChanged()
{
    auto pluginId = getSelectedPluginId();
    int paramIdx = getSelectedParameterIndex();

    if (pluginId.isNotEmpty() && paramIdx >= 0 && onParameterSelected)
        onParameterSelected (pluginId, paramIdx);

    selectedPoints.clear();
    undoStack.clear();
    redoStack.clear();
    repaint();
}

void PluginAutomationComponent::curveTypeChanged()
{
    // Apply the selected curve type to all selected points
    auto* lane = getCurrentLane();
    if (lane == nullptr || selectedPoints.empty())
        return;

    pushUndoState();
    auto ct = getSelectedCurveType();

    for (int idx : selectedPoints)
    {
        if (idx >= 0 && idx < static_cast<int> (lane->points.size()))
            lane->points[static_cast<size_t> (idx)].curveType = ct;
    }

    if (onAutomationChanged)
        onAutomationChanged();
    repaint();
}

AutomationCurveType PluginAutomationComponent::getSelectedCurveType() const
{
    switch (curveTypeDropdown.getSelectedId())
    {
        case 2:  return AutomationCurveType::Step;
        case 3:  return AutomationCurveType::Smooth;
        case 4:  return AutomationCurveType::SCurve;
        default: return AutomationCurveType::Linear;
    }
}

//==============================================================================
// Navigate to param (click-to-automate)
//==============================================================================

void PluginAutomationComponent::navigateToParam (const juce::String& pluginId, int paramIndex)
{
    // Find and select the plugin
    for (int i = 0; i < static_cast<int> (availablePlugins.size()); ++i)
    {
        if (availablePlugins[static_cast<size_t> (i)].pluginId == pluginId)
        {
            pluginDropdown.setSelectedId (i + 1, juce::sendNotification);

            // Now find and select the parameter
            auto& params = availablePlugins[static_cast<size_t> (i)].parameters;
            for (int j = 0; j < static_cast<int> (params.size()); ++j)
            {
                if (params[static_cast<size_t> (j)].index == paramIndex)
                {
                    parameterDropdown.setSelectedId (j + 1, juce::sendNotification);
                    return;
                }
            }
            return;
        }
    }
}

//==============================================================================
// Coordinate conversion
//==============================================================================

juce::Point<float> PluginAutomationComponent::dataToScreen (float row, float value) const
{
    auto gb = getGraphBounds().toFloat();
    float visibleRange = static_cast<float> (patternLength) / zoomLevel;
    float x = gb.getX() + ((row - viewStartRow) / visibleRange) * gb.getWidth();
    float y = gb.getBottom() - value * gb.getHeight();
    return { x, y };
}

juce::Point<float> PluginAutomationComponent::screenToData (juce::Point<float> screenPos) const
{
    auto gb = getGraphBounds().toFloat();
    float visibleRange = static_cast<float> (patternLength) / zoomLevel;
    float row = viewStartRow + ((screenPos.x - gb.getX()) / gb.getWidth()) * visibleRange;
    float value = 1.0f - (screenPos.y - gb.getY()) / gb.getHeight();
    return { juce::jlimit (0.0f, static_cast<float> (patternLength - 1), row),
             juce::jlimit (0.0f, 1.0f, value) };
}

int PluginAutomationComponent::findPointNear (juce::Point<float> screenPos, float maxDist) const
{
    auto* lane = getCurrentLane();
    if (lane == nullptr)
        return -1;

    float bestDist = maxDist + 1.0f;
    int bestIdx = -1;

    for (int i = 0; i < static_cast<int> (lane->points.size()); ++i)
    {
        auto& p = lane->points[static_cast<size_t> (i)];
        auto sp = dataToScreen (static_cast<float> (p.row), p.value);
        float dist = screenPos.getDistanceFrom (sp);
        if (dist < bestDist)
        {
            bestDist = dist;
            bestIdx = i;
        }
    }

    return bestDist <= maxDist ? bestIdx : -1;
}

AutomationLane* PluginAutomationComponent::getCurrentLane()
{
    if (automationData == nullptr)
        return nullptr;

    auto pluginId = getSelectedPluginId();
    int paramIdx = getSelectedParameterIndex();
    if (pluginId.isEmpty() || paramIdx < 0)
        return nullptr;

    return automationData->findLane (pluginId, paramIdx);
}

const AutomationLane* PluginAutomationComponent::getCurrentLane() const
{
    if (automationData == nullptr)
        return nullptr;

    auto pluginId = getSelectedPluginId();
    int paramIdx = getSelectedParameterIndex();
    if (pluginId.isEmpty() || paramIdx < 0)
        return nullptr;

    return automationData->findLane (pluginId, paramIdx);
}

//==============================================================================
// Snap helper
//==============================================================================

int PluginAutomationComponent::snapRow (int row) const
{
    if (! snapToGrid)
        return row;

    int step = patternLength <= 32 ? 4 : 8;
    int snapped = ((row + step / 2) / step) * step;
    return juce::jlimit (0, patternLength - 1, snapped);
}

//==============================================================================
// Selection helpers
//==============================================================================

juce::Rectangle<float> PluginAutomationComponent::getSelectionRect() const
{
    float x1 = juce::jmin (selectionStart.x, selectionEnd.x);
    float y1 = juce::jmin (selectionStart.y, selectionEnd.y);
    float x2 = juce::jmax (selectionStart.x, selectionEnd.x);
    float y2 = juce::jmax (selectionStart.y, selectionEnd.y);
    return { x1, y1, x2 - x1, y2 - y1 };
}

void PluginAutomationComponent::updateSelectionFromRect()
{
    selectedPoints.clear();
    auto* lane = getCurrentLane();
    if (lane == nullptr)
        return;

    auto rect = getSelectionRect();

    for (int i = 0; i < static_cast<int> (lane->points.size()); ++i)
    {
        auto& p = lane->points[static_cast<size_t> (i)];
        auto sp = dataToScreen (static_cast<float> (p.row), p.value);
        if (rect.contains (sp))
            selectedPoints.insert (i);
    }
}

//==============================================================================
// Undo / Redo
//==============================================================================

PluginAutomationComponent::UndoSnapshot PluginAutomationComponent::captureCurrentState() const
{
    UndoSnapshot snap;
    snap.pluginId = getSelectedPluginId();
    snap.parameterId = getSelectedParameterIndex();

    auto* lane = getCurrentLane();
    if (lane != nullptr)
        snap.points = lane->points;

    return snap;
}

void PluginAutomationComponent::pushUndoState()
{
    undoStack.push_back (captureCurrentState());
    if (static_cast<int> (undoStack.size()) > kMaxUndoSteps)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void PluginAutomationComponent::restoreState (const UndoSnapshot& state)
{
    if (automationData == nullptr)
        return;

    if (state.pluginId.isEmpty() || state.parameterId < 0)
        return;

    auto* lane = automationData->findLane (state.pluginId, state.parameterId);
    if (lane != nullptr)
    {
        lane->points = state.points;
        if (lane->isEmpty())
            automationData->removeLane (state.pluginId, state.parameterId);
    }
    else if (! state.points.empty())
    {
        // Re-create the lane
        int ownerTrack = currentTrack;
        auto& newLane = automationData->getOrCreateLane (state.pluginId, state.parameterId, ownerTrack);
        newLane.points = state.points;
    }

    selectedPoints.clear();
    if (onAutomationChanged)
        onAutomationChanged();
    repaint();
}

void PluginAutomationComponent::undo()
{
    if (undoStack.empty())
        return;

    redoStack.push_back (captureCurrentState());
    restoreState (undoStack.back());
    undoStack.pop_back();
}

void PluginAutomationComponent::redo()
{
    if (redoStack.empty())
        return;

    undoStack.push_back (captureCurrentState());
    restoreState (redoStack.back());
    redoStack.pop_back();
}

//==============================================================================
// Selection operations
//==============================================================================

void PluginAutomationComponent::selectAll()
{
    auto* lane = getCurrentLane();
    if (lane == nullptr)
        return;

    selectedPoints.clear();
    for (int i = 0; i < static_cast<int> (lane->points.size()); ++i)
        selectedPoints.insert (i);
    repaint();
}

void PluginAutomationComponent::deleteSelected()
{
    auto* lane = getCurrentLane();
    if (lane == nullptr || selectedPoints.empty())
        return;

    pushUndoState();

    // Delete in reverse order to preserve indices
    std::vector<int> sorted (selectedPoints.begin(), selectedPoints.end());
    std::sort (sorted.rbegin(), sorted.rend());

    for (int idx : sorted)
    {
        if (idx >= 0 && idx < static_cast<int> (lane->points.size()))
            lane->points.erase (lane->points.begin() + idx);
    }

    auto pluginId = getSelectedPluginId();
    int paramIdx = getSelectedParameterIndex();
    if (lane->isEmpty() && pluginId.isNotEmpty() && paramIdx >= 0)
        automationData->removeLane (pluginId, paramIdx);

    selectedPoints.clear();
    if (onAutomationChanged)
        onAutomationChanged();
    repaint();
}

void PluginAutomationComponent::copySelected()
{
    auto* lane = getCurrentLane();
    if (lane == nullptr || selectedPoints.empty())
        return;

    clipboard.points.clear();
    clipboard.minRow = INT_MAX;

    for (int idx : selectedPoints)
    {
        if (idx >= 0 && idx < static_cast<int> (lane->points.size()))
        {
            clipboard.points.push_back (lane->points[static_cast<size_t> (idx)]);
            clipboard.minRow = juce::jmin (clipboard.minRow, lane->points[static_cast<size_t> (idx)].row);
        }
    }
}

void PluginAutomationComponent::pasteFromClipboard()
{
    if (clipboard.points.empty() || automationData == nullptr)
        return;

    auto pluginId = getSelectedPluginId();
    int paramIdx = getSelectedParameterIndex();
    if (pluginId.isEmpty() || paramIdx < 0)
        return;

    pushUndoState();

    int ownerTrack = currentTrack;
    int plugIdx = pluginDropdown.getSelectedId() - 1;
    if (plugIdx >= 0 && plugIdx < static_cast<int> (availablePlugins.size()))
        ownerTrack = availablePlugins[static_cast<size_t> (plugIdx)].owningTrack;

    auto& lane = automationData->getOrCreateLane (pluginId, paramIdx, ownerTrack);

    // Paste at a row offset so the first point starts at row 0
    // (or at a logical paste position - we'll paste relative to the current view start)
    int pasteOffset = static_cast<int> (viewStartRow) - clipboard.minRow;

    selectedPoints.clear();
    for (auto pt : clipboard.points)
    {
        pt.row = juce::jlimit (0, patternLength - 1, pt.row + pasteOffset);
        lane.setPoint (pt.row, pt.value, pt.curveType);
    }
    lane.sortPoints();

    // Select the pasted points
    for (auto& pt : clipboard.points)
    {
        int pastedRow = juce::jlimit (0, patternLength - 1, pt.row + pasteOffset);
        for (int i = 0; i < static_cast<int> (lane.points.size()); ++i)
        {
            if (lane.points[static_cast<size_t> (i)].row == pastedRow)
            {
                selectedPoints.insert (i);
                break;
            }
        }
    }

    if (onAutomationChanged)
        onAutomationChanged();
    repaint();
}

//==============================================================================
// Recording
//==============================================================================

void PluginAutomationComponent::setRecording (bool recording)
{
    recordingEnabled = recording;
    recButton.setToggleState (recording, juce::dontSendNotification);
}

void PluginAutomationComponent::recordParameterValue (int row, float value)
{
    if (! recordingEnabled || automationData == nullptr)
        return;

    auto pluginId = getSelectedPluginId();
    int paramIdx = getSelectedParameterIndex();
    if (pluginId.isEmpty() || paramIdx < 0)
        return;

    int ownerTrack = currentTrack;
    int plugIdx = pluginDropdown.getSelectedId() - 1;
    if (plugIdx >= 0 && plugIdx < static_cast<int> (availablePlugins.size()))
        ownerTrack = availablePlugins[static_cast<size_t> (plugIdx)].owningTrack;

    auto& lane = automationData->getOrCreateLane (pluginId, paramIdx, ownerTrack);
    lane.setPoint (juce::jlimit (0, patternLength - 1, row), value, getSelectedCurveType());

    if (onAutomationChanged)
        onAutomationChanged();
    repaint();
}

//==============================================================================
// Mouse interaction
//==============================================================================

void PluginAutomationComponent::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    // Check resize handle
    if (e.getPosition().y < kDragHandleHeight)
    {
        resizeDragging = true;
        resizeDragStartY = e.getScreenY();
        resizeDragStartHeight = panelHeight;
        return;
    }

    if (automationData == nullptr)
        return;

    auto graphBounds = getGraphBounds();
    if (! graphBounds.contains (e.getPosition()))
        return;

    auto pluginId = getSelectedPluginId();
    int paramIdx = getSelectedParameterIndex();
    if (pluginId.isEmpty() || paramIdx < 0)
        return;

    auto screenPos = e.getPosition().toFloat();

    // Right-click: delete nearest point
    if (e.mods.isRightButtonDown())
    {
        auto* lane = getCurrentLane();
        if (lane != nullptr)
        {
            int nearIdx = findPointNear (screenPos, 12.0f);
            if (nearIdx >= 0)
            {
                pushUndoState();
                lane->points.erase (lane->points.begin() + nearIdx);
                selectedPoints.erase (nearIdx);
                if (lane->isEmpty())
                    automationData->removeLane (pluginId, paramIdx);

                if (onAutomationChanged)
                    onAutomationChanged();
                repaint();
            }
        }
        return;
    }

    auto* lane = getCurrentLane();
    int nearIdx = lane != nullptr ? findPointNear (screenPos, 10.0f) : -1;

    // Freehand draw mode
    if (drawMode)
    {
        pushUndoState();
        auto data = screenToData (screenPos);
        int row = snapRow (juce::roundToInt (data.x));
        float value = data.y;

        int ownerTrack = currentTrack;
        int plugIdx = pluginDropdown.getSelectedId() - 1;
        if (plugIdx >= 0 && plugIdx < static_cast<int> (availablePlugins.size()))
            ownerTrack = availablePlugins[static_cast<size_t> (plugIdx)].owningTrack;

        auto& laneRef = automationData->getOrCreateLane (pluginId, paramIdx, ownerTrack);
        laneRef.setPoint (row, value, getSelectedCurveType());
        lastDrawRow = row;
        isDragging = true;

        if (onAutomationChanged)
            onAutomationChanged();
        repaint();
        return;
    }

    // Check if clicking on a selected point to start moving selection
    if (nearIdx >= 0 && selectedPoints.count (nearIdx) > 0)
    {
        isMovingSelection = true;
        moveSelectionAnchor = screenPos;
        isDragging = true;
        pushUndoState();
        return;
    }

    // Shift+click to toggle point selection
    if (e.mods.isShiftDown() && nearIdx >= 0)
    {
        if (selectedPoints.count (nearIdx) > 0)
            selectedPoints.erase (nearIdx);
        else
            selectedPoints.insert (nearIdx);
        repaint();
        return;
    }

    if (nearIdx >= 0)
    {
        // Start dragging existing point
        if (! e.mods.isShiftDown())
        {
            selectedPoints.clear();
            selectedPoints.insert (nearIdx);
        }
        dragPointIndex = nearIdx;
        isDragging = true;
        pushUndoState();
    }
    else
    {
        // Start rubber-band selection or create a new point
        if (e.mods.isShiftDown() || e.mods.isCommandDown())
        {
            // Rubber-band selection
            isSelecting = true;
            selectionStart = screenPos;
            selectionEnd = screenPos;
        }
        else
        {
            // Create new point
            selectedPoints.clear();
            pushUndoState();

            auto data = screenToData (screenPos);
            int row = snapRow (juce::roundToInt (data.x));
            float value = data.y;

            int ownerTrack = currentTrack;
            int plugIdx = pluginDropdown.getSelectedId() - 1;
            if (plugIdx >= 0 && plugIdx < static_cast<int> (availablePlugins.size()))
                ownerTrack = availablePlugins[static_cast<size_t> (plugIdx)].owningTrack;

            auto& laneRef = automationData->getOrCreateLane (pluginId, paramIdx, ownerTrack);
            laneRef.setPoint (row, value, getSelectedCurveType());

            for (int i = 0; i < static_cast<int> (laneRef.points.size()); ++i)
            {
                if (laneRef.points[static_cast<size_t> (i)].row == row)
                {
                    dragPointIndex = i;
                    isDragging = true;
                    selectedPoints.insert (i);
                    break;
                }
            }

            if (onAutomationChanged)
                onAutomationChanged();
            repaint();
        }
    }
}

void PluginAutomationComponent::mouseDrag (const juce::MouseEvent& e)
{
    // Resize handle drag
    if (resizeDragging)
    {
        int delta = resizeDragStartY - e.getScreenY();
        int newHeight = juce::jlimit (kMinPanelHeight, kMaxPanelHeight, resizeDragStartHeight + delta);
        if (newHeight != panelHeight)
        {
            panelHeight = newHeight;
            if (onPanelHeightChanged)
                onPanelHeightChanged (panelHeight);
        }
        return;
    }

    // Rubber-band selection
    if (isSelecting)
    {
        selectionEnd = e.getPosition().toFloat();
        updateSelectionFromRect();
        repaint();
        return;
    }

    // Freehand draw
    if (drawMode && isDragging)
    {
        auto screenPos = e.getPosition().toFloat();
        auto data = screenToData (screenPos);
        int row = snapRow (juce::roundToInt (data.x));
        float value = data.y;

        if (row != lastDrawRow)
        {
            auto pluginId = getSelectedPluginId();
            int paramIdx = getSelectedParameterIndex();
            auto* lane = getCurrentLane();
            if (lane != nullptr)
            {
                // Fill in intermediate rows
                int minR = juce::jmin (lastDrawRow, row);
                int maxR = juce::jmax (lastDrawRow, row);
                for (int r = minR; r <= maxR; ++r)
                {
                    float t = (lastDrawRow == row) ? 0.0f
                        : static_cast<float> (r - lastDrawRow) / static_cast<float> (row - lastDrawRow);
                    float interpValue = value; // simplified: just use current value
                    juce::ignoreUnused (t);
                    lane->setPoint (juce::jlimit (0, patternLength - 1, r), interpValue, getSelectedCurveType());
                }
                lastDrawRow = row;

                if (onAutomationChanged)
                    onAutomationChanged();
                repaint();
            }
        }
        return;
    }

    // Moving selection
    if (isMovingSelection)
    {
        auto* lane = getCurrentLane();
        if (lane == nullptr)
            return;

        auto screenPos = e.getPosition().toFloat();
        auto anchorData = screenToData (moveSelectionAnchor);
        auto currentData = screenToData (screenPos);

        int rowDelta = juce::roundToInt (currentData.x - anchorData.x);
        float valueDelta = currentData.y - anchorData.y;

        if (rowDelta == 0 && std::abs (valueDelta) < 0.001f)
            return;

        // Apply delta to selected points
        for (int idx : selectedPoints)
        {
            if (idx >= 0 && idx < static_cast<int> (lane->points.size()))
            {
                auto& p = lane->points[static_cast<size_t> (idx)];
                p.row = juce::jlimit (0, patternLength - 1, p.row + rowDelta);
                p.value = juce::jlimit (0.0f, 1.0f, p.value + valueDelta);
            }
        }

        lane->sortPoints();
        moveSelectionAnchor = screenPos;

        // Re-discover selected point indices after sort
        // (This is a simplification; in practice we'd track by identity)

        if (onAutomationChanged)
            onAutomationChanged();
        repaint();
        return;
    }

    // Normal single-point drag
    if (! isDragging || dragPointIndex < 0)
        return;

    auto* lane = getCurrentLane();
    if (lane == nullptr || dragPointIndex >= static_cast<int> (lane->points.size()))
    {
        isDragging = false;
        return;
    }

    auto data = screenToData (e.getPosition().toFloat());
    int row = snapRow (juce::roundToInt (data.x));
    float value = data.y;

    auto& point = lane->points[static_cast<size_t> (dragPointIndex)];
    point.row = juce::jlimit (0, patternLength - 1, row);
    point.value = juce::jlimit (0.0f, 1.0f, value);

    int targetRow = point.row;
    float targetVal = point.value;
    lane->sortPoints();

    for (int i = 0; i < static_cast<int> (lane->points.size()); ++i)
    {
        if (lane->points[static_cast<size_t> (i)].row == targetRow
            && std::abs (lane->points[static_cast<size_t> (i)].value - targetVal) < 1.0e-6f)
        {
            dragPointIndex = i;
            selectedPoints.clear();
            selectedPoints.insert (i);
            break;
        }
    }

    if (onAutomationChanged)
        onAutomationChanged();
    repaint();
}

void PluginAutomationComponent::mouseUp (const juce::MouseEvent&)
{
    if (isSelecting)
    {
        isSelecting = false;
        repaint();
    }

    resizeDragging = false;
    isDragging = false;
    isMovingSelection = false;
    dragPointIndex = -1;
    lastDrawRow = -1;
}

void PluginAutomationComponent::mouseMove (const juce::MouseEvent& e)
{
    auto graphBounds = getGraphBounds();

    // Change cursor for resize handle
    if (e.getPosition().y < kDragHandleHeight)
    {
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }
    else if (graphBounds.contains (e.getPosition()))
    {
        setMouseCursor (drawMode ? juce::MouseCursor::CrosshairCursor
                                 : juce::MouseCursor::NormalCursor);

        // Hover tooltip
        auto screenPos = e.getPosition().toFloat();
        int nearIdx = findPointNear (screenPos, 12.0f);

        if (nearIdx >= 0)
        {
            hoverPointIndex = nearIdx;
            hoverScreenPos = screenPos;
            showHoverTooltip = true;
        }
        else
        {
            // Show data coordinates even when not near a point
            hoverPointIndex = -1;
            hoverScreenPos = screenPos;
            showHoverTooltip = true;
        }
        repaint();
    }
    else
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
        if (showHoverTooltip)
        {
            showHoverTooltip = false;
            repaint();
        }
    }
}

void PluginAutomationComponent::mouseWheelMove (const juce::MouseEvent& e,
                                                  const juce::MouseWheelDetails& wheel)
{
    auto graphBounds = getGraphBounds();
    if (! graphBounds.contains (e.getPosition()))
        return;

    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        // Zoom
        float zoomDelta = wheel.deltaY > 0 ? 1.2f : (1.0f / 1.2f);
        float newZoom = juce::jlimit (1.0f, 16.0f, zoomLevel * zoomDelta);

        // Zoom centered on mouse position
        auto mouseData = screenToData (e.getPosition().toFloat());
        zoomLevel = newZoom;

        float visibleRange = static_cast<float> (patternLength) / zoomLevel;
        float mouseRowFraction = (e.getPosition().toFloat().x - graphBounds.toFloat().getX()) / graphBounds.toFloat().getWidth();
        viewStartRow = mouseData.x - mouseRowFraction * visibleRange;
        clampViewToPattern();
        repaint();
    }
    else
    {
        // Horizontal scroll
        float scrollAmount = wheel.deltaY * (static_cast<float> (patternLength) / zoomLevel) * 0.1f;
        viewStartRow -= scrollAmount;
        clampViewToPattern();
        repaint();
    }
}

void PluginAutomationComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto graphBounds = getGraphBounds();
    if (! graphBounds.contains (e.getPosition()))
        return;

    // Double-click on a point to cycle its curve type
    auto screenPos = e.getPosition().toFloat();
    auto* lane = getCurrentLane();
    if (lane == nullptr)
        return;

    int nearIdx = findPointNear (screenPos, 10.0f);
    if (nearIdx >= 0)
    {
        pushUndoState();
        auto& pt = lane->points[static_cast<size_t> (nearIdx)];
        int ct = static_cast<int> (pt.curveType);
        ct = (ct + 1) % 4;
        pt.curveType = static_cast<AutomationCurveType> (ct);

        if (onAutomationChanged)
            onAutomationChanged();
        repaint();
    }
}

//==============================================================================
// Keyboard
//==============================================================================

bool PluginAutomationComponent::keyPressed (const juce::KeyPress& key)
{
    bool cmd = key.getModifiers().isCommandDown();

    // Delete selected points
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        deleteSelected();
        return true;
    }

    // Ctrl+A: select all
    if (cmd && key.getKeyCode() == 'A')
    {
        selectAll();
        return true;
    }

    // Ctrl+C: copy
    if (cmd && key.getKeyCode() == 'C')
    {
        copySelected();
        return true;
    }

    // Ctrl+V: paste
    if (cmd && key.getKeyCode() == 'V')
    {
        pasteFromClipboard();
        return true;
    }

    // Ctrl+X: cut
    if (cmd && key.getKeyCode() == 'X')
    {
        copySelected();
        deleteSelected();
        return true;
    }

    // Ctrl+Z: undo
    if (cmd && ! key.getModifiers().isShiftDown() && key.getKeyCode() == 'Z')
    {
        undo();
        return true;
    }

    // Ctrl+Shift+Z or Ctrl+Y: redo
    if ((cmd && key.getModifiers().isShiftDown() && key.getKeyCode() == 'Z')
        || (cmd && key.getKeyCode() == 'Y'))
    {
        redo();
        return true;
    }

    return false;
}

//==============================================================================
// Drawing helpers
//==============================================================================

void PluginAutomationComponent::drawDragHandle (juce::Graphics& g) const
{
    auto handleBounds = getLocalBounds().removeFromTop (kDragHandleHeight);
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId).brighter (0.15f));
    g.fillRect (handleBounds);

    // Draw grip dots
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.3f));
    int cx = handleBounds.getCentreX();
    int cy = handleBounds.getCentreY();
    for (int i = -2; i <= 2; ++i)
        g.fillEllipse (static_cast<float> (cx + i * 8 - 1), static_cast<float> (cy - 1), 3.0f, 3.0f);
}

void PluginAutomationComponent::drawGrid (juce::Graphics& g, juce::Rectangle<int> bounds) const
{
    // Graph background
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId).darker (0.1f));
    g.fillRect (bounds);

    // Border
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId).withAlpha (0.5f));
    g.drawRect (bounds);

    // Horizontal grid lines (0.25, 0.5, 0.75)
    auto fb = bounds.toFloat();
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId).withAlpha (0.2f));
    for (float v = 0.25f; v <= 0.75f; v += 0.25f)
    {
        float y = fb.getBottom() - v * fb.getHeight();
        g.drawHorizontalLine (static_cast<int> (y), fb.getX(), fb.getRight());
    }

    // Vertical grid lines with zoom awareness
    int step = patternLength <= 32 ? 4 : 8;
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId).withAlpha (0.15f));
    float visibleRange = static_cast<float> (patternLength) / zoomLevel;
    int startRow = juce::jmax (0, static_cast<int> (viewStartRow) - 1);
    int endRow = juce::jmin (patternLength, static_cast<int> (viewStartRow + visibleRange) + 1);

    for (int row = (startRow / step) * step; row <= endRow; row += step)
    {
        if (row <= 0)
            continue;
        auto sp = dataToScreen (static_cast<float> (row), 0.0f);
        if (sp.x >= fb.getX() && sp.x <= fb.getRight())
            g.drawVerticalLine (static_cast<int> (sp.x), fb.getY(), fb.getBottom());
    }
}

void PluginAutomationComponent::drawBaseline (juce::Graphics& g, juce::Rectangle<int> bounds) const
{
    auto fb = bounds.toFloat();
    float y = fb.getBottom() - baseline * fb.getHeight();

    g.setColour (juce::Colour (0xff888844).withAlpha (0.5f));
    const float dashLengths[] = { 4.0f, 3.0f };
    g.drawDashedLine (juce::Line<float> (fb.getX(), y, fb.getRight(), y),
                      dashLengths, 2, 1.0f);
}

void PluginAutomationComponent::drawCurve (juce::Graphics& g, juce::Rectangle<int> bounds,
                                            const AutomationLane* lane, juce::Colour colour, float alpha) const
{
    if (lane == nullptr || lane->points.empty())
        return;

    juce::Path path;
    bool started = false;

    float visibleRange = static_cast<float> (patternLength) / zoomLevel;
    float startR = viewStartRow;
    float endR = viewStartRow + visibleRange;

    // Draw with sub-row resolution for smooth curves
    float step = juce::jmax (0.25f, visibleRange / static_cast<float> (bounds.getWidth()));

    for (float r = startR; r <= endR; r += step)
    {
        float value = lane->getValueAtRow (r);
        auto sp = dataToScreen (r, value);

        if (! started)
        {
            path.startNewSubPath (sp);
            started = true;
        }
        else
        {
            path.lineTo (sp);
        }
    }

    // Final point at visible end
    float finalValue = lane->getValueAtRow (juce::jmin (endR, static_cast<float> (patternLength - 1)));
    auto finalSp = dataToScreen (juce::jmin (endR, static_cast<float> (patternLength - 1)), finalValue);
    if (started)
        path.lineTo (finalSp);

    g.setColour (colour.withAlpha (alpha));
    g.strokePath (path, juce::PathStrokeType (1.5f));

    // Fill under curve (subtle)
    if (started)
    {
        juce::Path fillPath (path);
        auto fb = bounds.toFloat();
        fillPath.lineTo (finalSp.x, fb.getBottom());
        auto firstSp = dataToScreen (startR, lane->getValueAtRow (startR));
        fillPath.lineTo (firstSp.x, fb.getBottom());
        fillPath.closeSubPath();

        g.setColour (colour.withAlpha (alpha * 0.1f));
        g.fillPath (fillPath);
    }
}

void PluginAutomationComponent::drawPoints (juce::Graphics& g, juce::Rectangle<int> /*bounds*/) const
{
    auto* lane = getCurrentLane();
    if (lane == nullptr)
        return;

    auto gb = getGraphBounds().toFloat();

    for (int i = 0; i < static_cast<int> (lane->points.size()); ++i)
    {
        auto& p = lane->points[static_cast<size_t> (i)];
        auto sp = dataToScreen (static_cast<float> (p.row), p.value);

        // Skip points outside visible area
        if (sp.x < gb.getX() - 10.0f || sp.x > gb.getRight() + 10.0f)
            continue;

        bool isActive = (isDragging && dragPointIndex == i);
        bool isSelected = selectedPoints.count (i) > 0;
        float radius = isActive ? 5.0f : (isSelected ? 4.5f : 4.0f);

        // Colour based on curve type
        juce::Colour ptColour;
        if (isActive)
            ptColour = juce::Colour (0xffffcc44);
        else if (isSelected)
            ptColour = juce::Colour (0xffffffff);
        else
        {
            switch (p.curveType)
            {
                case AutomationCurveType::Step:   ptColour = juce::Colour (0xffff8844); break;
                case AutomationCurveType::Smooth:  ptColour = juce::Colour (0xff44ff88); break;
                case AutomationCurveType::SCurve:  ptColour = juce::Colour (0xffff44aa); break;
                default:                           ptColour = juce::Colour (0xff44aaff); break;
            }
        }

        // Selection highlight ring
        if (isSelected && ! isActive)
        {
            g.setColour (juce::Colour (0xffffffff).withAlpha (0.4f));
            g.drawEllipse (sp.x - radius - 2.0f, sp.y - radius - 2.0f,
                           (radius + 2.0f) * 2.0f, (radius + 2.0f) * 2.0f, 1.0f);
        }

        // Outer circle
        g.setColour (ptColour);
        g.fillEllipse (sp.x - radius, sp.y - radius, radius * 2.0f, radius * 2.0f);

        // Inner dot
        g.setColour (juce::Colour (0xffffffff));
        g.fillEllipse (sp.x - 2.0f, sp.y - 2.0f, 4.0f, 4.0f);

        // Curve type indicator for Step (small square)
        if (p.curveType == AutomationCurveType::Step)
        {
            g.setColour (ptColour.darker (0.3f));
            g.fillRect (sp.x - 1.5f, sp.y + radius + 1.0f, 3.0f, 3.0f);
        }
    }
}

void PluginAutomationComponent::drawPlaybackPosition (juce::Graphics& g, juce::Rectangle<int> bounds) const
{
    if (playbackRow < 0)
        return;

    auto sp = dataToScreen (static_cast<float> (playbackRow), 0.0f);
    auto fb = bounds.toFloat();

    if (sp.x < fb.getX() || sp.x > fb.getRight())
        return;

    g.setColour (juce::Colour (0xffffffff).withAlpha (0.6f));
    g.drawVerticalLine (static_cast<int> (sp.x), fb.getY(), fb.getBottom());
}

void PluginAutomationComponent::drawHoverTooltip (juce::Graphics& g) const
{
    if (! showHoverTooltip)
        return;

    auto gb = getGraphBounds().toFloat();
    auto data = screenToData (hoverScreenPos);

    juce::String text;
    if (hoverPointIndex >= 0)
    {
        auto* lane = getCurrentLane();
        if (lane != nullptr && hoverPointIndex < static_cast<int> (lane->points.size()))
        {
            auto& p = lane->points[static_cast<size_t> (hoverPointIndex)];
            text = "Row " + juce::String (p.row) + " : " + juce::String (p.value, 3);
        }
    }
    else
    {
        text = "Row " + juce::String (juce::roundToInt (data.x)) + " : " + juce::String (data.y, 3);
    }

    if (text.isEmpty())
        return;

    g.setFont (lookAndFeel.getMonoFont (10.0f));
    int textWidth = static_cast<int> (g.getCurrentFont().getStringWidthFloat (text)) + 8;
    int textHeight = 14;

    float tooltipX = hoverScreenPos.x + 12.0f;
    float tooltipY = hoverScreenPos.y - textHeight - 4.0f;

    // Keep tooltip in graph bounds
    if (tooltipX + textWidth > gb.getRight())
        tooltipX = hoverScreenPos.x - textWidth - 4.0f;
    if (tooltipY < gb.getY())
        tooltipY = hoverScreenPos.y + 8.0f;

    auto tooltipRect = juce::Rectangle<float> (tooltipX, tooltipY,
                                                static_cast<float> (textWidth),
                                                static_cast<float> (textHeight));

    g.setColour (juce::Colour (0xdd1e1e2e));
    g.fillRoundedRectangle (tooltipRect, 3.0f);
    g.setColour (juce::Colour (0x88ffffff));
    g.drawRoundedRectangle (tooltipRect, 3.0f, 0.5f);

    g.setColour (juce::Colour (0xffffffff));
    g.drawText (text, tooltipRect.toNearestInt(), juce::Justification::centred);
}

void PluginAutomationComponent::drawSelectionRect (juce::Graphics& g) const
{
    if (! isSelecting)
        return;

    auto rect = getSelectionRect();
    g.setColour (juce::Colour (0x2244aaff));
    g.fillRect (rect);
    g.setColour (juce::Colour (0x8844aaff));
    g.drawRect (rect, 1.0f);
}

void PluginAutomationComponent::drawOverlayLanes (juce::Graphics& g, juce::Rectangle<int> bounds) const
{
    if (automationData == nullptr)
        return;

    auto pluginId = getSelectedPluginId();
    if (pluginId.isEmpty())
        return;

    int activeParamId = getSelectedParameterIndex();
    auto allLanes = automationData->findLanesForPlugin (pluginId);
    int colourIndex = 1; // Start at 1 since 0 is used for the active lane

    for (auto* lane : allLanes)
    {
        if (lane->parameterId == activeParamId)
            continue; // Skip active lane; it's drawn separately

        auto colour = getLaneColour (colourIndex);
        drawCurve (g, bounds, lane, colour, 0.35f);

        // Draw a small colour legend entry
        if (colourIndex <= 8)
        {
            // Find param name for this lane
            juce::String paramName = "Param " + juce::String (lane->parameterId);
            int plugIdx = pluginDropdown.getSelectedId() - 1;
            if (plugIdx >= 0 && plugIdx < static_cast<int> (availablePlugins.size()))
            {
                for (auto& pInfo : availablePlugins[static_cast<size_t> (plugIdx)].parameters)
                {
                    if (pInfo.index == lane->parameterId)
                    {
                        paramName = pInfo.name;
                        break;
                    }
                }
            }

            int legendY = bounds.getY() + 14 + (colourIndex - 1) * 12;
            g.setColour (colour.withAlpha (0.5f));
            g.fillRect (bounds.getX() + 4, legendY, 8, 8);
            g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.4f));
            g.setFont (lookAndFeel.getMonoFont (9.0f));
            g.drawText (paramName, bounds.getX() + 16, legendY - 1, 120, 10,
                        juce::Justification::centredLeft);
        }

        ++colourIndex;
    }
}
