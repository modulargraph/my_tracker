#include "PluginAutomationComponent.h"

PluginAutomationComponent::PluginAutomationComponent (TrackerLookAndFeel& lnf)
    : lookAndFeel (lnf)
{
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
}

void PluginAutomationComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // Background
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId).brighter (0.05f));
    g.fillRect (bounds);

    // Top border
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (0, 0.0f, static_cast<float> (bounds.getWidth()));

    // Graph area
    auto graphBounds = getGraphBounds();
    if (graphBounds.isEmpty())
        return;

    drawGrid (g, graphBounds);
    drawBaseline (g, graphBounds);
    drawCurve (g, graphBounds);
    drawPoints (g, graphBounds);

    // Label: "AUTOMATION" in top-left of graph area
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.3f));
    g.setFont (lookAndFeel.getMonoFont (10.0f));
    g.drawText ("AUTOMATION", graphBounds.getX() + 4, graphBounds.getY() + 2, 100, 12,
                juce::Justification::centredLeft);
}

void PluginAutomationComponent::resized()
{
    auto bounds = getLocalBounds().reduced (4);

    // Controls on the left
    auto controlArea = bounds.removeFromLeft (kControlsWidth);
    controlArea.removeFromTop (2);

    pluginLabel.setBounds (controlArea.removeFromTop (16));
    pluginDropdown.setBounds (controlArea.removeFromTop (22).reduced (0, 1));
    controlArea.removeFromTop (4);
    paramLabel.setBounds (controlArea.removeFromTop (16));
    parameterDropdown.setBounds (controlArea.removeFromTop (22).reduced (0, 1));
}

juce::Rectangle<int> PluginAutomationComponent::getGraphBounds() const
{
    auto bounds = getLocalBounds().reduced (4);
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
    repaint();
}

void PluginAutomationComponent::setPatternLength (int numRows)
{
    patternLength = juce::jmax (1, numRows);
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

    auto previousId = getSelectedPluginId();
    availablePlugins = plugins;

    pluginDropdown.clear (juce::dontSendNotification);

    for (int i = 0; i < static_cast<int> (availablePlugins.size()); ++i)
        pluginDropdown.addItem (availablePlugins[static_cast<size_t> (i)].displayName, i + 1);

    // Try to re-select the previous plugin
    if (previousId.isNotEmpty())
    {
        for (int i = 0; i < static_cast<int> (availablePlugins.size()); ++i)
        {
            if (availablePlugins[static_cast<size_t> (i)].pluginId == previousId)
            {
                pluginDropdown.setSelectedId (i + 1, juce::dontSendNotification);
                pluginSelectionChanged();
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
// Plugin/parameter selection
//==============================================================================

void PluginAutomationComponent::pluginSelectionChanged()
{
    auto pluginId = getSelectedPluginId();

    parameterDropdown.clear (juce::dontSendNotification);

    if (! suppressSelectionCallbacks && pluginId.isNotEmpty() && onPluginSelected)
        onPluginSelected (pluginId);

    // Populate parameter dropdown from the selected plugin's params
    int idx = pluginDropdown.getSelectedId() - 1;
    if (idx >= 0 && idx < static_cast<int> (availablePlugins.size()))
    {
        auto& params = availablePlugins[static_cast<size_t> (idx)].parameters;
        for (int i = 0; i < static_cast<int> (params.size()); ++i)
            parameterDropdown.addItem (params[static_cast<size_t> (i)].name, i + 1);

        if (! params.empty())
        {
            parameterDropdown.setSelectedId (1, juce::dontSendNotification);
            parameterSelectionChanged();
        }
    }

    repaint();
}

void PluginAutomationComponent::parameterSelectionChanged()
{
    auto pluginId = getSelectedPluginId();
    int paramIdx = getSelectedParameterIndex();

    if (pluginId.isNotEmpty() && paramIdx >= 0 && onParameterSelected)
        onParameterSelected (pluginId, paramIdx);

    repaint();
}

//==============================================================================
// Coordinate conversion
//==============================================================================

juce::Point<float> PluginAutomationComponent::dataToScreen (float row, float value) const
{
    auto gb = getGraphBounds().toFloat();
    float x = gb.getX() + (row / static_cast<float> (patternLength)) * gb.getWidth();
    float y = gb.getBottom() - value * gb.getHeight();
    return { x, y };
}

juce::Point<float> PluginAutomationComponent::screenToData (juce::Point<float> screenPos) const
{
    auto gb = getGraphBounds().toFloat();
    float row = ((screenPos.x - gb.getX()) / gb.getWidth()) * static_cast<float> (patternLength);
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
// Mouse interaction
//==============================================================================

void PluginAutomationComponent::mouseDown (const juce::MouseEvent& e)
{
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
                lane->points.erase (lane->points.begin() + nearIdx);
                if (lane->isEmpty())
                    automationData->removeLane (pluginId, paramIdx);

                if (onAutomationChanged)
                    onAutomationChanged();
                repaint();
            }
        }
        return;
    }

    // Left-click: grab existing point or create new one
    auto* lane = getCurrentLane();
    int nearIdx = lane != nullptr ? findPointNear (screenPos, 10.0f) : -1;

    if (nearIdx >= 0)
    {
        // Start dragging existing point
        dragPointIndex = nearIdx;
        isDragging = true;
    }
    else
    {
        // Create new point
        auto data = screenToData (screenPos);
        int row = juce::roundToInt (data.x);
        float value = data.y;

        // Get the owning track from the selected plugin info
        int ownerTrack = currentTrack;
        int plugIdx = pluginDropdown.getSelectedId() - 1;
        if (plugIdx >= 0 && plugIdx < static_cast<int> (availablePlugins.size()))
            ownerTrack = availablePlugins[static_cast<size_t> (plugIdx)].owningTrack;

        auto& laneRef = automationData->getOrCreateLane (pluginId, paramIdx, ownerTrack);
        laneRef.setPoint (row, value);

        // Find the index of the newly created point for dragging
        for (int i = 0; i < static_cast<int> (laneRef.points.size()); ++i)
        {
            if (laneRef.points[static_cast<size_t> (i)].row == row)
            {
                dragPointIndex = i;
                isDragging = true;
                break;
            }
        }

        if (onAutomationChanged)
            onAutomationChanged();
        repaint();
    }
}

void PluginAutomationComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (! isDragging || dragPointIndex < 0)
        return;

    auto* lane = getCurrentLane();
    if (lane == nullptr || dragPointIndex >= static_cast<int> (lane->points.size()))
    {
        isDragging = false;
        return;
    }

    auto data = screenToData (e.getPosition().toFloat());
    int row = juce::roundToInt (data.x);
    float value = data.y;

    auto& point = lane->points[static_cast<size_t> (dragPointIndex)];
    point.row = juce::jlimit (0, patternLength - 1, row);
    point.value = juce::jlimit (0.0f, 1.0f, value);

    // Re-sort and find the new index
    int targetRow = point.row;
    float targetVal = point.value;
    lane->sortPoints();

    for (int i = 0; i < static_cast<int> (lane->points.size()); ++i)
    {
        if (lane->points[static_cast<size_t> (i)].row == targetRow
            && std::abs (lane->points[static_cast<size_t> (i)].value - targetVal) < 1.0e-6f)
        {
            dragPointIndex = i;
            break;
        }
    }

    if (onAutomationChanged)
        onAutomationChanged();
    repaint();
}

void PluginAutomationComponent::mouseUp (const juce::MouseEvent&)
{
    isDragging = false;
    dragPointIndex = -1;
}

//==============================================================================
// Drawing helpers
//==============================================================================

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

    // Vertical grid lines: every 4 or 8 rows depending on pattern length
    int step = patternLength <= 32 ? 4 : 8;
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId).withAlpha (0.15f));
    for (int row = step; row < patternLength; row += step)
    {
        float x = fb.getX() + (static_cast<float> (row) / static_cast<float> (patternLength)) * fb.getWidth();
        g.drawVerticalLine (static_cast<int> (x), fb.getY(), fb.getBottom());
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

void PluginAutomationComponent::drawCurve (juce::Graphics& g, juce::Rectangle<int> bounds) const
{
    auto* lane = getCurrentLane();
    if (lane == nullptr || lane->points.empty())
        return;

    juce::Path path;
    bool started = false;

    // Draw from row 0 to patternLength
    for (int row = 0; row < patternLength; ++row)
    {
        float value = lane->getValueAtRow (static_cast<float> (row));
        auto sp = dataToScreen (static_cast<float> (row), value);

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

    // Final point at end
    float finalValue = lane->getValueAtRow (static_cast<float> (patternLength - 1));
    auto finalSp = dataToScreen (static_cast<float> (patternLength - 1), finalValue);
    path.lineTo (finalSp);

    g.setColour (juce::Colour (0xff44aaff).withAlpha (0.8f));
    g.strokePath (path, juce::PathStrokeType (1.5f));

    // Fill under curve (subtle)
    juce::Path fillPath (path);
    auto fb = bounds.toFloat();
    fillPath.lineTo (finalSp.x, fb.getBottom());
    fillPath.lineTo (fb.getX(), fb.getBottom());
    fillPath.closeSubPath();

    g.setColour (juce::Colour (0xff44aaff).withAlpha (0.08f));
    g.fillPath (fillPath);
}

void PluginAutomationComponent::drawPoints (juce::Graphics& g, juce::Rectangle<int> /*bounds*/) const
{
    auto* lane = getCurrentLane();
    if (lane == nullptr)
        return;

    for (int i = 0; i < static_cast<int> (lane->points.size()); ++i)
    {
        auto& p = lane->points[static_cast<size_t> (i)];
        auto sp = dataToScreen (static_cast<float> (p.row), p.value);

        bool isActive = (isDragging && dragPointIndex == i);
        float radius = isActive ? 5.0f : 4.0f;

        // Outer circle
        g.setColour (isActive ? juce::Colour (0xffffcc44) : juce::Colour (0xff44aaff));
        g.fillEllipse (sp.x - radius, sp.y - radius, radius * 2.0f, radius * 2.0f);

        // Inner dot
        g.setColour (juce::Colour (0xffffffff));
        g.fillEllipse (sp.x - 2.0f, sp.y - 2.0f, 4.0f, 4.0f);
    }
}
