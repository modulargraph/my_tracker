#include "SendEffectsComponent.h"
#include "FormatUtils.h"

using namespace FormatUtils;

SendEffectsComponent::SendEffectsComponent (TrackerLookAndFeel& lnf)
    : lookAndFeel (lnf)
{
    setWantsKeyboardFocus (true);
}

void SendEffectsComponent::setDelayParams (const DelayParams& params)
{
    delay = params;
    repaint();
}

void SendEffectsComponent::setReverbParams (const ReverbParams& params)
{
    reverb = params;
    repaint();
}

//==============================================================================
// Paint
//==============================================================================

void SendEffectsComponent::paint (juce::Graphics& g)
{
    auto bg = lookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId);
    g.fillAll (bg);

    // Outer border
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawRect (getLocalBounds(), 1);

    // Header
    drawHeader (g, { 0, 0, getWidth(), kHeaderHeight });

    // Bottom bar
    auto bottomBarArea = juce::Rectangle<int> (0, getHeight() - kBottomBarHeight,
                                                getWidth(), kBottomBarHeight);
    drawBottomBar (g, bottomBarArea);

    // Content area
    int contentTop = kHeaderHeight;
    int contentBottom = getHeight() - kBottomBarHeight;
    auto contentArea = juce::Rectangle<int> (0, contentTop, getWidth(), contentBottom - contentTop);

    // Split into two sections: DELAY (left) and REVERB (right)
    int halfWidth = (contentArea.getWidth() - kSectionGap) / 2;

    auto delayArea = contentArea.withWidth (halfWidth);
    auto reverbArea = contentArea.withX (contentArea.getX() + halfWidth + kSectionGap)
                                  .withWidth (contentArea.getWidth() - halfWidth - kSectionGap);

    drawDelaySection (g, delayArea, section == 0);
    drawReverbSection (g, reverbArea, section == 1);
}

void SendEffectsComponent::resized() {}

//==============================================================================
// Header
//==============================================================================

void SendEffectsComponent::drawHeader (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto headerBg = lookAndFeel.findColour (TrackerLookAndFeel::headerColourId);
    auto textCol = lookAndFeel.findColour (TrackerLookAndFeel::textColourId);
    auto gridCol = lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId);

    g.setColour (headerBg);
    g.fillRect (area);

    g.setColour (gridCol);
    g.drawHorizontalLine (area.getBottom() - 1, static_cast<float> (area.getX()),
                          static_cast<float> (area.getRight()));

    g.setFont (lookAndFeel.getMonoFont (12.0f));
    g.setColour (textCol);
    g.drawText ("SEND EFFECTS", area.reduced (8, 0), juce::Justification::centredLeft);
}

//==============================================================================
// Bottom bar
//==============================================================================

void SendEffectsComponent::drawBottomBar (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto headerBg = lookAndFeel.findColour (TrackerLookAndFeel::headerColourId);
    auto textCol = lookAndFeel.findColour (TrackerLookAndFeel::textColourId);
    auto gridCol = lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId);
    auto accentCol = lookAndFeel.findColour (TrackerLookAndFeel::fxColourId);

    g.setColour (headerBg);
    g.fillRect (area);

    g.setColour (gridCol);
    g.drawHorizontalLine (area.getY(), static_cast<float> (area.getX()),
                          static_cast<float> (area.getRight()));

    g.setFont (lookAndFeel.getMonoFont (11.0f));

    auto nameArea = area.reduced (8, 0).removeFromLeft (area.getWidth() / 3);
    auto valueArea = area.reduced (8, 0);

    g.setColour (textCol.withAlpha (0.5f));
    g.drawText (getColumnName(), nameArea, juce::Justification::centredLeft);

    g.setColour (accentCol);
    g.drawText (getColumnValue(), valueArea, juce::Justification::centred);
}

//==============================================================================
// Bar meter (same style as SampleEditorComponent)
//==============================================================================

void SendEffectsComponent::drawBarMeter (juce::Graphics& g, juce::Rectangle<int> area,
                                          float value01, bool focused, juce::Colour colour)
{
    auto bg = lookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId);
    auto gridCol = lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId);

    g.setColour (focused ? gridCol.brighter (0.4f) : gridCol);
    g.drawRect (area, 1);

    auto inner = area.reduced (6, 4);

    g.setColour (bg.brighter (0.04f));
    g.fillRect (inner);

    g.setColour (gridCol.withAlpha (0.6f));
    g.drawRect (inner, 1);

    value01 = juce::jlimit (0.0f, 1.0f, value01);
    int fillH = juce::roundToInt (value01 * static_cast<float> (inner.getHeight() - 2));

    if (fillH > 0)
    {
        auto fillRect = juce::Rectangle<int> (
            inner.getX() + 1,
            inner.getBottom() - 1 - fillH,
            inner.getWidth() - 2,
            fillH);

        g.setColour (colour.withAlpha (focused ? 0.85f : 0.5f));
        g.fillRect (fillRect);
    }
}

//==============================================================================
// List column (same style as SampleEditorComponent)
//==============================================================================

void SendEffectsComponent::drawListColumn (juce::Graphics& g, juce::Rectangle<int> area,
                                            const juce::StringArray& items, int selectedIndex,
                                            bool focused, juce::Colour colour)
{
    auto bg = lookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId);
    auto gridCol = lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId);
    auto textCol = lookAndFeel.findColour (TrackerLookAndFeel::textColourId);

    g.setColour (focused ? gridCol.brighter (0.4f) : gridCol);
    g.drawRect (area, 1);

    auto inner = area.reduced (1);
    int numItems = items.size();
    if (numItems == 0) return;

    int maxVisible = inner.getHeight() / kListItemHeight;
    int scrollOffset = 0;
    if (numItems > maxVisible && selectedIndex >= 0)
        scrollOffset = juce::jlimit (0, numItems - maxVisible, selectedIndex - maxVisible / 2);

    int visibleCount = juce::jmin (numItems - scrollOffset, maxVisible);

    g.setFont (lookAndFeel.getMonoFont (11.0f));

    for (int vi = 0; vi < visibleCount; ++vi)
    {
        int i = scrollOffset + vi;
        int y = inner.getY() + vi * kListItemHeight;
        auto itemRect = juce::Rectangle<int> (inner.getX(), y, inner.getWidth(), kListItemHeight);

        if (i == selectedIndex)
        {
            g.setColour (focused ? colour : colour.withAlpha (0.4f));
            g.fillRect (itemRect);
            g.setColour (focused ? bg : textCol);
        }
        else
        {
            g.setColour (textCol.withAlpha (focused ? 0.65f : 0.35f));
        }

        g.drawText (items[i], itemRect.reduced (6, 0), juce::Justification::centredLeft);
    }
}

//==============================================================================
// Delay section
//==============================================================================

void SendEffectsComponent::drawDelaySection (juce::Graphics& g, juce::Rectangle<int> area, bool sectionFocused)
{
    auto textCol = lookAndFeel.findColour (TrackerLookAndFeel::textColourId);
    auto blueCol = lookAndFeel.findColour (TrackerLookAndFeel::fxColourId);
    auto gridCol = lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId);

    // Section title
    auto titleArea = area.removeFromTop (20);
    g.setFont (lookAndFeel.getMonoFont (11.0f));
    g.setColour (sectionFocused ? blueCol : textCol.withAlpha (0.5f));
    g.drawText ("DELAY", titleArea.reduced (6, 0), juce::Justification::centredLeft);
    g.setColour (gridCol);
    g.drawHorizontalLine (titleArea.getBottom(), static_cast<float> (area.getX()),
                          static_cast<float> (area.getRight()));

    int colW = area.getWidth() / kDelayColumns;

    auto colRect = [&] (int c) -> juce::Rectangle<int>
    {
        int w = (c < kDelayColumns - 1) ? colW : (area.getWidth() - c * colW);
        return { area.getX() + c * colW, area.getY(), w, area.getHeight() };
    };

    bool sf = sectionFocused;

    // Col 0: Time (ms or sync display)
    if (delay.bpmSync)
    {
        // Show as sync division name
        float sync01 = 0.0f;
        if (delay.syncDivision <= 1) sync01 = 1.0f;
        else if (delay.syncDivision <= 2) sync01 = 0.85f;
        else if (delay.syncDivision <= 4) sync01 = 0.7f;
        else if (delay.syncDivision <= 8) sync01 = 0.5f;
        else if (delay.syncDivision <= 16) sync01 = 0.3f;
        else sync01 = 0.15f;
        drawBarMeter (g, colRect (0), sync01, sf && delayColumn == 0, blueCol);
    }
    else
    {
        float time01 = static_cast<float> (delay.time) / 2000.0f;
        drawBarMeter (g, colRect (0), time01, sf && delayColumn == 0, blueCol);
    }

    // Col 1: Sync Division list
    juce::StringArray syncItems = { "1 (Whole)", "2 (Half)", "4 (Quarter)", "8 (8th)", "16 (16th)", "32 (32nd)" };
    int syncIdx = 0;
    if (delay.syncDivision <= 1) syncIdx = 0;
    else if (delay.syncDivision <= 2) syncIdx = 1;
    else if (delay.syncDivision <= 4) syncIdx = 2;
    else if (delay.syncDivision <= 8) syncIdx = 3;
    else if (delay.syncDivision <= 16) syncIdx = 4;
    else syncIdx = 5;
    drawListColumn (g, colRect (1), syncItems, syncIdx, sf && delayColumn == 1, blueCol);

    // Col 2: BPM Sync toggle
    juce::StringArray syncToggle = { "Free", "Sync" };
    drawListColumn (g, colRect (2), syncToggle, delay.bpmSync ? 1 : 0, sf && delayColumn == 2, blueCol);

    // Col 3: Feedback bar
    float fb01 = static_cast<float> (delay.feedback) / 100.0f;
    drawBarMeter (g, colRect (3), fb01, sf && delayColumn == 3, blueCol);

    // Col 4: Filter type list
    juce::StringArray filterItems = { "Off", "LowPass", "HighPass" };
    drawListColumn (g, colRect (4), filterItems, delay.filterType, sf && delayColumn == 4, blueCol);

    // Col 5: Filter cutoff bar
    float cutoff01 = static_cast<float> (delay.filterCutoff) / 100.0f;
    drawBarMeter (g, colRect (5), cutoff01, sf && delayColumn == 5, blueCol);

    // Col 6: Wet bar
    float wet01 = static_cast<float> (delay.wet) / 100.0f;
    drawBarMeter (g, colRect (6), wet01, sf && delayColumn == 6, blueCol);

    // Col 7: Stereo Width bar
    float width01 = static_cast<float> (delay.stereoWidth) / 100.0f;
    drawBarMeter (g, colRect (7), width01, sf && delayColumn == 7, blueCol);

    // Column labels at the top of each bar
    g.setFont (lookAndFeel.getMonoFont (9.0f));
    const char* labels[] = { "TIME", "DIV", "SYNC", "FDBK", "FILT", "FREQ", "WET", "WIDTH" };
    for (int c = 0; c < kDelayColumns; ++c)
    {
        auto r = colRect (c);
        g.setColour ((sf && delayColumn == c) ? blueCol : textCol.withAlpha (0.4f));
        g.drawText (labels[c], r.getX(), area.getY() + 2, r.getWidth(), 12, juce::Justification::centred);
    }
}

//==============================================================================
// Reverb section
//==============================================================================

void SendEffectsComponent::drawReverbSection (juce::Graphics& g, juce::Rectangle<int> area, bool sectionFocused)
{
    auto textCol = lookAndFeel.findColour (TrackerLookAndFeel::textColourId);
    auto greenCol = lookAndFeel.findColour (TrackerLookAndFeel::volumeColourId);
    auto gridCol = lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId);

    // Section title
    auto titleArea = area.removeFromTop (20);
    g.setFont (lookAndFeel.getMonoFont (11.0f));
    g.setColour (sectionFocused ? greenCol : textCol.withAlpha (0.5f));
    g.drawText ("REVERB", titleArea.reduced (6, 0), juce::Justification::centredLeft);
    g.setColour (gridCol);
    g.drawHorizontalLine (titleArea.getBottom(), static_cast<float> (area.getX()),
                          static_cast<float> (area.getRight()));

    int colW = area.getWidth() / kReverbColumns;

    auto colRect = [&] (int c) -> juce::Rectangle<int>
    {
        int w = (c < kReverbColumns - 1) ? colW : (area.getWidth() - c * colW);
        return { area.getX() + c * colW, area.getY(), w, area.getHeight() };
    };

    bool sf = sectionFocused;

    // Col 0: Room Size bar
    float room01 = static_cast<float> (reverb.roomSize) / 100.0f;
    drawBarMeter (g, colRect (0), room01, sf && reverbColumn == 0, greenCol);

    // Col 1: Decay bar
    float decay01 = static_cast<float> (reverb.decay) / 100.0f;
    drawBarMeter (g, colRect (1), decay01, sf && reverbColumn == 1, greenCol);

    // Col 2: Damping bar
    float damp01 = static_cast<float> (reverb.damping) / 100.0f;
    drawBarMeter (g, colRect (2), damp01, sf && reverbColumn == 2, greenCol);

    // Col 3: Pre-Delay bar
    float pd01 = static_cast<float> (reverb.preDelay) / 100.0f;
    drawBarMeter (g, colRect (3), pd01, sf && reverbColumn == 3, greenCol);

    // Col 4: Wet bar
    float wet01 = static_cast<float> (reverb.wet) / 100.0f;
    drawBarMeter (g, colRect (4), wet01, sf && reverbColumn == 4, greenCol);

    // Column labels
    g.setFont (lookAndFeel.getMonoFont (9.0f));
    const char* labels[] = { "ROOM", "DECAY", "DAMP", "PREDL", "WET" };
    for (int c = 0; c < kReverbColumns; ++c)
    {
        auto r = colRect (c);
        g.setColour ((sf && reverbColumn == c) ? greenCol : textCol.withAlpha (0.4f));
        g.drawText (labels[c], r.getX(), area.getY() + 2, r.getWidth(), 12, juce::Justification::centred);
    }
}

//==============================================================================
// Column info for bottom bar
//==============================================================================

juce::String SendEffectsComponent::getColumnName() const
{
    if (section == 0)
    {
        const char* names[] = { "Time", "Sync Division", "BPM Sync", "Feedback",
                                "Filter", "Filter Cutoff", "Wet Level", "Stereo Width" };
        if (delayColumn >= 0 && delayColumn < kDelayColumns)
            return juce::String ("DELAY: ") + names[delayColumn];
    }
    else
    {
        const char* names[] = { "Room Size", "Decay", "Damping", "Pre-Delay", "Wet Level" };
        if (reverbColumn >= 0 && reverbColumn < kReverbColumns)
            return juce::String ("REVERB: ") + names[reverbColumn];
    }
    return {};
}

juce::String SendEffectsComponent::getColumnValue() const
{
    if (section == 0)
    {
        switch (delayColumn)
        {
            case 0:
                if (delay.bpmSync)
                {
                    if (delay.syncDivision <= 1) return "1/1 (Whole)";
                    if (delay.syncDivision <= 2) return "1/2 (Half)";
                    if (delay.syncDivision <= 4) return "1/4 (Quarter)";
                    if (delay.syncDivision <= 8) return "1/8 (8th)";
                    if (delay.syncDivision <= 16) return "1/16 (16th)";
                    return "1/32 (32nd)";
                }
                return juce::String (delay.time, 1) + " ms";
            case 1:
            {
                if (delay.syncDivision <= 1) return "1/1 (Whole)";
                if (delay.syncDivision <= 2) return "1/2 (Half)";
                if (delay.syncDivision <= 4) return "1/4 (Quarter)";
                if (delay.syncDivision <= 8) return "1/8 (8th)";
                if (delay.syncDivision <= 16) return "1/16 (16th)";
                return "1/32 (32nd)";
            }
            case 2: return delay.bpmSync ? "Sync" : "Free";
            case 3: return juce::String (delay.feedback, 0) + "%";
            case 4:
                if (delay.filterType == 0) return "Off";
                if (delay.filterType == 1) return "LowPass";
                return "HighPass";
            case 5: return juce::String (delay.filterCutoff, 0) + "%";
            case 6: return juce::String (delay.wet, 0) + "%";
            case 7: return juce::String (delay.stereoWidth, 0) + "%";
        }
    }
    else
    {
        switch (reverbColumn)
        {
            case 0: return juce::String (reverb.roomSize, 0) + "%";
            case 1: return juce::String (reverb.decay, 0) + "%";
            case 2: return juce::String (reverb.damping, 0) + "%";
            case 3: return juce::String (reverb.preDelay, 1) + " ms";
            case 4: return juce::String (reverb.wet, 0) + "%";
        }
    }
    return {};
}

//==============================================================================
// Keyboard navigation
//==============================================================================

bool SendEffectsComponent::keyPressed (const juce::KeyPress& key)
{
    auto keyCode = key.getKeyCode();
    bool shift = key.getModifiers().isShiftDown();

    // Tab / Shift+Tab: switch between DELAY and REVERB sections
    if (keyCode == juce::KeyPress::tabKey)
    {
        section = shift ? 0 : 1;
        repaint();
        return true;
    }

    // Left/Right: move between columns
    if (keyCode == juce::KeyPress::leftKey)
    {
        currentColumn() = juce::jmax (0, currentColumn() - 1);
        repaint();
        return true;
    }
    if (keyCode == juce::KeyPress::rightKey)
    {
        currentColumn() = juce::jmin (currentColumnCount() - 1, currentColumn() + 1);
        repaint();
        return true;
    }

    // Up/Down: adjust current value
    if (keyCode == juce::KeyPress::upKey)
    {
        adjustCurrentValue (1, shift, false);
        return true;
    }
    if (keyCode == juce::KeyPress::downKey)
    {
        adjustCurrentValue (-1, shift, false);
        return true;
    }

    // Page Up/Down: large adjustments
    if (keyCode == juce::KeyPress::pageUpKey)
    {
        adjustCurrentValue (1, false, true);
        return true;
    }
    if (keyCode == juce::KeyPress::pageDownKey)
    {
        adjustCurrentValue (-1, false, true);
        return true;
    }

    return false;
}

//==============================================================================
// Value adjustment
//==============================================================================

void SendEffectsComponent::adjustCurrentValue (int direction, bool fine, bool large)
{
    double step = fine ? 0.5 : (large ? 10.0 : 1.0);
    double delta = direction * step;

    if (section == 0) // DELAY
    {
        switch (delayColumn)
        {
            case 0: // Time (ms) or effective display
                if (! delay.bpmSync)
                    delay.time = juce::jlimit (1.0, 2000.0, delay.time + delta * 10.0);
                else
                {
                    // Adjust sync division via time column too
                    static const int divs[] = { 1, 2, 4, 8, 16, 32 };
                    int idx = 0;
                    for (int i = 0; i < 6; ++i)
                        if (delay.syncDivision >= divs[i]) idx = i;
                    idx = juce::jlimit (0, 5, idx + direction);
                    delay.syncDivision = divs[idx];
                }
                break;
            case 1: // Sync division
            {
                static const int divs[] = { 1, 2, 4, 8, 16, 32 };
                int idx = 0;
                for (int i = 0; i < 6; ++i)
                    if (delay.syncDivision >= divs[i]) idx = i;
                idx = juce::jlimit (0, 5, idx + direction);
                delay.syncDivision = divs[idx];
                break;
            }
            case 2: // BPM Sync toggle
                delay.bpmSync = ! delay.bpmSync;
                break;
            case 3: // Feedback
                delay.feedback = juce::jlimit (0.0, 100.0, delay.feedback + delta);
                break;
            case 4: // Filter type
                delay.filterType = juce::jlimit (0, 2, delay.filterType + direction);
                break;
            case 5: // Filter cutoff
                delay.filterCutoff = juce::jlimit (0.0, 100.0, delay.filterCutoff + delta);
                break;
            case 6: // Wet
                delay.wet = juce::jlimit (0.0, 100.0, delay.wet + delta);
                break;
            case 7: // Stereo width
                delay.stereoWidth = juce::jlimit (0.0, 100.0, delay.stereoWidth + delta);
                break;
        }
    }
    else // REVERB
    {
        switch (reverbColumn)
        {
            case 0: // Room size
                reverb.roomSize = juce::jlimit (0.0, 100.0, reverb.roomSize + delta);
                break;
            case 1: // Decay
                reverb.decay = juce::jlimit (0.0, 100.0, reverb.decay + delta);
                break;
            case 2: // Damping
                reverb.damping = juce::jlimit (0.0, 100.0, reverb.damping + delta);
                break;
            case 3: // Pre-delay
                reverb.preDelay = juce::jlimit (0.0, 100.0, reverb.preDelay + delta);
                break;
            case 4: // Wet
                reverb.wet = juce::jlimit (0.0, 100.0, reverb.wet + delta);
                break;
        }
    }

    notifyChanged();
    repaint();
}

void SendEffectsComponent::notifyChanged()
{
    if (onParamsChanged)
        onParamsChanged (delay, reverb);
}
