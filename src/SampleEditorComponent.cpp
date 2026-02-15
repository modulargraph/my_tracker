#include "SampleEditorComponent.h"

SampleEditorComponent::SampleEditorComponent (TrackerLookAndFeel& lnf)
    : lookAndFeel (lnf)
{
    formatManager.registerBasicFormats();
    setWantsKeyboardFocus (true);
}

SampleEditorComponent::~SampleEditorComponent()
{
    stopTimer();
}

void SampleEditorComponent::open (int instrumentIndex, const juce::File& sampleFile, const InstrumentParams& params)
{
    currentInstrument = instrumentIndex;
    currentFile = sampleFile;
    currentParams = params;
    editorOpen = true;
    paramsDirty = false;
    focusedControl = FocusedControl::Start;

    // Load waveform thumbnail
    thumbnail.clear();
    if (sampleFile.existsAsFile())
        thumbnail.setSource (new juce::FileInputSource (sampleFile));

    setVisible (true);
    grabKeyboardFocus();
    repaint();
}

void SampleEditorComponent::close()
{
    // Flush any pending apply
    if (paramsDirty)
    {
        stopTimer();
        notifyParamsChanged();
        paramsDirty = false;
    }

    editorOpen = false;
    setVisible (false);
}

//==============================================================================
// Debounced apply: visual updates are instant, audio processing fires after 200ms idle
//==============================================================================

void SampleEditorComponent::scheduleApply()
{
    paramsDirty = true;
    startTimer (200);
    repaint();
}

void SampleEditorComponent::timerCallback()
{
    stopTimer();
    if (paramsDirty)
    {
        paramsDirty = false;
        notifyParamsChanged();
    }
}

void SampleEditorComponent::notifyParamsChanged()
{
    if (onParamsChanged)
        onParamsChanged (currentInstrument, currentParams);
}

//==============================================================================
// Paint
//==============================================================================

void SampleEditorComponent::paint (juce::Graphics& g)
{
    auto bg = lookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId);
    g.fillAll (bg);

    auto bounds = getLocalBounds();

    // Header
    auto headerArea = bounds.removeFromTop (kHeaderHeight);
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::headerColourId));
    g.fillRect (headerArea);

    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::textColourId));
    g.setFont (lookAndFeel.getMonoFont (13.0f));

    juce::String headerText = "Sample Editor";
    if (currentInstrument >= 0)
    {
        headerText += juce::String::formatted (" - %02X: ", currentInstrument);
        headerText += currentFile.getFileName();
    }
    g.drawText (headerText, headerArea.reduced (8, 0), juce::Justification::centredLeft);
    g.drawText ("[Esc]", headerArea.reduced (8, 0), juce::Justification::centredRight);

    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (headerArea.getBottom() - 1, 0.0f, static_cast<float> (getWidth()));

    // Controls area at bottom
    auto controlsArea = bounds.removeFromBottom (kControlsHeight);

    // Waveform fills the rest
    drawWaveform (g, bounds);
    drawEnvelopeOverlay (g, bounds);

    // Separator line
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (controlsArea.getY(), 0.0f, static_cast<float> (getWidth()));

    drawControls (g, controlsArea);
}

void SampleEditorComponent::resized()
{
}

//==============================================================================
// Waveform
//==============================================================================

void SampleEditorComponent::drawWaveform (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto waveArea = area.reduced (4);

    if (thumbnail.getTotalLength() <= 0.0)
    {
        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.3f));
        g.setFont (lookAndFeel.getMonoFont (14.0f));
        g.drawText ("No waveform data", waveArea, juce::Justification::centred);
        return;
    }

    auto totalLength = thumbnail.getTotalLength();

    // Shade regions outside start/end
    float startX = waveArea.getX() + static_cast<float> (currentParams.startPos) * waveArea.getWidth();
    float endX   = waveArea.getX() + static_cast<float> (currentParams.endPos) * waveArea.getWidth();

    g.setColour (juce::Colour (0x40000000));
    g.fillRect (static_cast<float> (waveArea.getX()), static_cast<float> (waveArea.getY()),
                startX - waveArea.getX(), static_cast<float> (waveArea.getHeight()));
    g.fillRect (endX, static_cast<float> (waveArea.getY()),
                static_cast<float> (waveArea.getRight()) - endX, static_cast<float> (waveArea.getHeight()));

    // Draw waveform
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::noteColourId).withAlpha (0.8f));
    thumbnail.drawChannels (g, waveArea, 0.0, totalLength, 1.0f);

    // Start marker (green)
    g.setColour (juce::Colour (0xff44cc44));
    g.drawVerticalLine (static_cast<int> (startX), static_cast<float> (waveArea.getY()),
                        static_cast<float> (waveArea.getBottom()));
    g.setFont (lookAndFeel.getMonoFont (10.0f));
    g.drawText ("S", static_cast<int> (startX) - 6, waveArea.getY(), 12, 14, juce::Justification::centred);

    // End marker (red)
    g.setColour (juce::Colour (0xffcc4444));
    g.drawVerticalLine (static_cast<int> (endX), static_cast<float> (waveArea.getY()),
                        static_cast<float> (waveArea.getBottom()));
    g.drawText ("E", static_cast<int> (endX) - 6, waveArea.getY(), 12, 14, juce::Justification::centred);
}

void SampleEditorComponent::drawEnvelopeOverlay (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (thumbnail.getTotalLength() <= 0.0) return;

    auto waveArea = area.reduced (4);
    float startX = waveArea.getX() + static_cast<float> (currentParams.startPos) * waveArea.getWidth();
    float endX   = waveArea.getX() + static_cast<float> (currentParams.endPos) * waveArea.getWidth();
    float activeWidth = endX - startX;
    if (activeWidth <= 0.0f) return;

    float bottom = static_cast<float> (waveArea.getBottom());
    float top    = static_cast<float> (waveArea.getY());
    float height = bottom - top;

    double totalDuration = thumbnail.getTotalLength() * (currentParams.endPos - currentParams.startPos);
    if (totalDuration <= 0.0) return;

    float attackFrac  = static_cast<float> (juce::jmin (currentParams.attackMs  * 0.001 / totalDuration, 1.0));
    float decayFrac   = static_cast<float> (juce::jmin (currentParams.decayMs   * 0.001 / totalDuration, 1.0));
    float releaseFrac = static_cast<float> (juce::jmin (currentParams.releaseMs * 0.001 / totalDuration, 1.0));
    float susLevel    = static_cast<float> (currentParams.sustainLevel);

    float attackEnd    = startX + activeWidth * attackFrac;
    float decayEnd     = attackEnd + activeWidth * decayFrac;
    float releaseStart = endX - activeWidth * releaseFrac;

    attackEnd    = juce::jmin (attackEnd, endX);
    decayEnd     = juce::jmin (decayEnd, endX);
    releaseStart = juce::jmax (releaseStart, decayEnd);

    juce::Path envPath;
    envPath.startNewSubPath (startX, bottom);
    envPath.lineTo (attackEnd, top);
    envPath.lineTo (decayEnd, top + height * (1.0f - susLevel));
    envPath.lineTo (releaseStart, top + height * (1.0f - susLevel));
    envPath.lineTo (endX, bottom);
    envPath.closeSubPath();

    g.setColour (juce::Colour (0x30ffaa44));
    g.fillPath (envPath);

    g.setColour (juce::Colour (0xbbffaa44));
    juce::Path envLine;
    envLine.startNewSubPath (startX, bottom);
    envLine.lineTo (attackEnd, top);
    envLine.lineTo (decayEnd, top + height * (1.0f - susLevel));
    envLine.lineTo (releaseStart, top + height * (1.0f - susLevel));
    envLine.lineTo (endX, bottom);
    g.strokePath (envLine, juce::PathStrokeType (2.0f));
}

//==============================================================================
// Controls drawing
//==============================================================================

juce::Rectangle<int> SampleEditorComponent::getControlsArea() const
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop (kHeaderHeight);
    return bounds.removeFromBottom (kControlsHeight).reduced (8, 4);
}

int SampleEditorComponent::getControlSlotWidth() const
{
    return getControlsArea().getWidth() / kNumControls;
}

SampleEditorComponent::FocusedControl SampleEditorComponent::hitTestControl (juce::Point<int> pos) const
{
    auto controlArea = getControlsArea();
    if (! controlArea.contains (pos))
        return focusedControl;

    int slotWidth = getControlSlotWidth();
    int relX = pos.x - controlArea.getX();
    int idx = juce::jlimit (0, kNumControls - 1, relX / slotWidth);
    return static_cast<FocusedControl> (idx);
}

void SampleEditorComponent::drawControls (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto controlArea = area.reduced (8, 4);

    struct ControlSlot
    {
        FocusedControl ctrl;
        juce::String name;
        juce::String value;
    };

    ControlSlot slots[] = {
        { FocusedControl::Start,   getControlName (FocusedControl::Start),   getControlValue (FocusedControl::Start) },
        { FocusedControl::End,     getControlName (FocusedControl::End),     getControlValue (FocusedControl::End) },
        { FocusedControl::Attack,  getControlName (FocusedControl::Attack),  getControlValue (FocusedControl::Attack) },
        { FocusedControl::Decay,   getControlName (FocusedControl::Decay),   getControlValue (FocusedControl::Decay) },
        { FocusedControl::Sustain, getControlName (FocusedControl::Sustain), getControlValue (FocusedControl::Sustain) },
        { FocusedControl::Release, getControlName (FocusedControl::Release), getControlValue (FocusedControl::Release) },
        { FocusedControl::Reverse, getControlName (FocusedControl::Reverse), getControlValue (FocusedControl::Reverse) },
    };

    int slotWidth = controlArea.getWidth() / kNumControls;
    int nameRowY = controlArea.getY() + 4;
    int valueRowY = controlArea.getY() + 34;

    // Section labels
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.4f));
    g.setFont (lookAndFeel.getMonoFont (10.0f));
    g.drawText ("POSITION", controlArea.getX(), controlArea.getY() - 2, slotWidth * 2, 14,
                juce::Justification::centredLeft);
    g.drawText ("ENVELOPE", controlArea.getX() + slotWidth * 2, controlArea.getY() - 2, slotWidth * 4, 14,
                juce::Justification::centredLeft);

    for (int i = 0; i < kNumControls; ++i)
    {
        int x = controlArea.getX() + i * slotWidth;
        bool isFocused = (slots[i].ctrl == focusedControl);

        if (isFocused)
        {
            g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::cursorCellColourId));
            g.fillRoundedRectangle (static_cast<float> (x), static_cast<float> (nameRowY - 2),
                                    static_cast<float> (slotWidth - 4), 58.0f, 4.0f);
        }

        // Control name
        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (isFocused ? 1.0f : 0.6f));
        g.setFont (lookAndFeel.getMonoFont (12.0f));
        g.drawText (slots[i].name, x + 2, nameRowY + 12, slotWidth - 8, 18,
                    juce::Justification::centred);

        // Control value
        juce::Colour valColour;
        if (i < 2)
            valColour = lookAndFeel.findColour (TrackerLookAndFeel::noteColourId);
        else if (i < 6)
            valColour = juce::Colour (0xffffaa44);
        else
            valColour = lookAndFeel.findColour (TrackerLookAndFeel::instrumentColourId);

        g.setColour (valColour.withAlpha (isFocused ? 1.0f : 0.7f));
        g.setFont (lookAndFeel.getMonoFont (14.0f));
        g.drawText (slots[i].value, x + 2, valueRowY, slotWidth - 8, 22,
                    juce::Justification::centred);

        // Separator lines between sections
        if (i == 2 || i == 6)
        {
            g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
            g.drawVerticalLine (x - 2, static_cast<float> (nameRowY),
                                static_cast<float> (valueRowY + 22));
        }
    }

    // Help text
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.35f));
    g.setFont (lookAndFeel.getMonoFont (10.0f));
    g.drawText ("Tab: next  Up/Down: adjust  Shift: fine  Cmd: large  R: reverse  Space: preview  Esc: close  Drag: mouse",
                controlArea.getX(), controlArea.getBottom() - 16, controlArea.getWidth(), 14,
                juce::Justification::centred);
}

//==============================================================================
// Control name/value helpers
//==============================================================================

juce::String SampleEditorComponent::getControlName (FocusedControl ctrl) const
{
    switch (ctrl)
    {
        case FocusedControl::Start:   return "Start";
        case FocusedControl::End:     return "End";
        case FocusedControl::Attack:  return "Atk";
        case FocusedControl::Decay:   return "Dec";
        case FocusedControl::Sustain: return "Sus";
        case FocusedControl::Release: return "Rel";
        case FocusedControl::Reverse: return "Rev";
        default: return "";
    }
}

juce::String SampleEditorComponent::getControlValue (FocusedControl ctrl) const
{
    switch (ctrl)
    {
        case FocusedControl::Start:   return juce::String (currentParams.startPos, 3);
        case FocusedControl::End:     return juce::String (currentParams.endPos, 3);
        case FocusedControl::Attack:  return juce::String (static_cast<int> (currentParams.attackMs)) + "ms";
        case FocusedControl::Decay:   return juce::String (static_cast<int> (currentParams.decayMs)) + "ms";
        case FocusedControl::Sustain: return juce::String (currentParams.sustainLevel, 2);
        case FocusedControl::Release: return juce::String (static_cast<int> (currentParams.releaseMs)) + "ms";
        case FocusedControl::Reverse: return currentParams.reversed ? "On" : "Off";
        default: return "";
    }
}

//==============================================================================
// Keyboard
//==============================================================================

bool SampleEditorComponent::keyPressed (const juce::KeyPress& key)
{
    auto keyCode = key.getKeyCode();
    bool shift = key.getModifiers().isShiftDown();
    bool cmd   = key.getModifiers().isCommandDown();

    if (keyCode == juce::KeyPress::escapeKey)
    {
        if (onCloseRequested)
            onCloseRequested();
        return true;
    }

    if (keyCode == juce::KeyPress::tabKey)
    {
        int idx = static_cast<int> (focusedControl);
        if (shift)
            idx = (idx + kNumControls - 1) % kNumControls;
        else
            idx = (idx + 1) % kNumControls;
        focusedControl = static_cast<FocusedControl> (idx);
        repaint();
        return true;
    }

    if (keyCode == juce::KeyPress::upKey || keyCode == juce::KeyPress::downKey)
    {
        int dir = (keyCode == juce::KeyPress::upKey) ? 1 : -1;
        adjustFocusedValue (dir, shift && ! cmd, cmd && ! shift);
        return true;
    }

    if ((key.getTextCharacter() == 'r' || key.getTextCharacter() == 'R') && ! cmd)
    {
        currentParams.reversed = ! currentParams.reversed;
        scheduleApply();
        return true;
    }

    if (keyCode == juce::KeyPress::spaceKey)
    {
        // Flush pending changes before preview so you hear the latest
        if (paramsDirty)
        {
            stopTimer();
            paramsDirty = false;
            notifyParamsChanged();
        }
        if (onPreviewRequested)
            onPreviewRequested (currentInstrument);
        return true;
    }

    return false;
}

//==============================================================================
// Mouse interaction
//==============================================================================

void SampleEditorComponent::mouseDown (const juce::MouseEvent& event)
{
    auto controlArea = getControlsArea();

    if (controlArea.contains (event.getPosition()))
    {
        auto ctrl = hitTestControl (event.getPosition());

        // Click on Reverse just toggles
        if (ctrl == FocusedControl::Reverse)
        {
            focusedControl = ctrl;
            currentParams.reversed = ! currentParams.reversed;
            scheduleApply();
            return;
        }

        focusedControl = ctrl;
        isDragging = true;
        dragStartY = static_cast<float> (event.y);
        dragStartParams = currentParams;
        repaint();
    }
}

void SampleEditorComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (! isDragging) return;

    float deltaY = dragStartY - static_cast<float> (event.y);  // up = positive

    // Sensitivity: shift for fine, cmd for large
    float sensitivity = 1.0f;
    if (event.mods.isShiftDown())
        sensitivity = 0.1f;
    else if (event.mods.isCommandDown())
        sensitivity = 5.0f;

    // Reset to drag start, then apply cumulative delta
    currentParams = dragStartParams;
    adjustControlByDelta (focusedControl, deltaY * sensitivity);
    scheduleApply();
}

void SampleEditorComponent::mouseUp (const juce::MouseEvent&)
{
    if (isDragging)
    {
        isDragging = false;
        // Flush immediately on release for snappy feel
        if (paramsDirty)
        {
            stopTimer();
            paramsDirty = false;
            notifyParamsChanged();
        }
    }
}

void SampleEditorComponent::adjustControlByDelta (FocusedControl ctrl, float pixelDelta)
{
    // Scale: 100 pixels of drag = full range traversal for normalized params
    switch (ctrl)
    {
        case FocusedControl::Start:
        {
            double delta = static_cast<double> (pixelDelta) / 200.0;
            currentParams.startPos = juce::jlimit (0.0, currentParams.endPos - 0.001,
                                                    dragStartParams.startPos + delta);
            break;
        }
        case FocusedControl::End:
        {
            double delta = static_cast<double> (pixelDelta) / 200.0;
            currentParams.endPos = juce::jlimit (currentParams.startPos + 0.001, 1.0,
                                                  dragStartParams.endPos + delta);
            break;
        }
        case FocusedControl::Attack:
        {
            double delta = static_cast<double> (pixelDelta) * 5.0;  // 1px = 5ms
            currentParams.attackMs = juce::jlimit (0.0, 5000.0, dragStartParams.attackMs + delta);
            break;
        }
        case FocusedControl::Decay:
        {
            double delta = static_cast<double> (pixelDelta) * 5.0;
            currentParams.decayMs = juce::jlimit (0.0, 5000.0, dragStartParams.decayMs + delta);
            break;
        }
        case FocusedControl::Sustain:
        {
            double delta = static_cast<double> (pixelDelta) / 100.0;
            currentParams.sustainLevel = juce::jlimit (0.0, 1.0, dragStartParams.sustainLevel + delta);
            break;
        }
        case FocusedControl::Release:
        {
            double delta = static_cast<double> (pixelDelta) * 5.0;
            currentParams.releaseMs = juce::jlimit (0.0, 5000.0, dragStartParams.releaseMs + delta);
            break;
        }
        case FocusedControl::Reverse:
            break;  // handled by click toggle
    }
}

//==============================================================================
// Keyboard value adjustment
//==============================================================================

void SampleEditorComponent::adjustFocusedValue (int direction, bool fine, bool large)
{
    switch (focusedControl)
    {
        case FocusedControl::Start:
        {
            double step = fine ? 0.001 : (large ? 0.1 : 0.01);
            currentParams.startPos = juce::jlimit (0.0, currentParams.endPos - 0.001,
                                                    currentParams.startPos + direction * step);
            break;
        }
        case FocusedControl::End:
        {
            double step = fine ? 0.001 : (large ? 0.1 : 0.01);
            currentParams.endPos = juce::jlimit (currentParams.startPos + 0.001, 1.0,
                                                  currentParams.endPos + direction * step);
            break;
        }
        case FocusedControl::Attack:
        {
            double step = fine ? 1.0 : (large ? 50.0 : 5.0);
            currentParams.attackMs = juce::jlimit (0.0, 5000.0, currentParams.attackMs + direction * step);
            break;
        }
        case FocusedControl::Decay:
        {
            double step = fine ? 1.0 : (large ? 50.0 : 5.0);
            currentParams.decayMs = juce::jlimit (0.0, 5000.0, currentParams.decayMs + direction * step);
            break;
        }
        case FocusedControl::Sustain:
        {
            double step = fine ? 0.01 : (large ? 0.25 : 0.05);
            currentParams.sustainLevel = juce::jlimit (0.0, 1.0, currentParams.sustainLevel + direction * step);
            break;
        }
        case FocusedControl::Release:
        {
            double step = fine ? 1.0 : (large ? 50.0 : 5.0);
            currentParams.releaseMs = juce::jlimit (0.0, 5000.0, currentParams.releaseMs + direction * step);
            break;
        }
        case FocusedControl::Reverse:
        {
            currentParams.reversed = ! currentParams.reversed;
            break;
        }
    }

    scheduleApply();
}
