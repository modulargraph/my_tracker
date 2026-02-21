#include "ToolbarComponent.h"

ToolbarComponent::ToolbarComponent (TrackerLookAndFeel& lnf)
    : lookAndFeel (lnf)
{
}

void ToolbarComponent::paint (juce::Graphics& g)
{
    auto bg = lookAndFeel.findColour (TrackerLookAndFeel::headerColourId);
    g.fillAll (bg);

    g.setFont (lookAndFeel.getMonoFont (13.0f));
    auto textCol = lookAndFeel.findColour (TrackerLookAndFeel::textColourId);

    int x = 8;

    // Arrangement panel toggle (left of pattern selector)
    arrangementToggleBounds = { x, 6, 24, 24 };
    g.setColour (arrangementOn ? juce::Colour (0xff5c8abf) : juce::Colour (0xff3a3a3a));
    g.fillRoundedRectangle (arrangementToggleBounds.toFloat(), 3.0f);
    g.setColour (arrangementOn ? juce::Colours::white : textCol);
    g.setFont (lookAndFeel.getMonoFont (11.0f));
    g.drawText ("ARR", arrangementToggleBounds, juce::Justification::centred);
    g.setFont (lookAndFeel.getMonoFont (13.0f));
    x += 28;

    // Pattern selector (scrollable)
    auto patStr = juce::String::formatted ("Pat:%02d/%02d", currentPattern, totalPatterns);
    patSelectorBounds = { x, 0, 80, kToolbarHeight };
    g.setColour (textCol);
    g.drawText (patStr, patSelectorBounds, juce::Justification::centredLeft);
    x += 82;

    // [+] button
    addPatBounds = { x, 6, 24, 24 };
    g.setColour (juce::Colour (0xff3a3a3a));
    g.fillRoundedRectangle (addPatBounds.toFloat(), 3.0f);
    g.setColour (textCol);
    g.drawText ("+", addPatBounds, juce::Justification::centred);
    x += 28;

    // [2x] duplicate button
    duplicatePatBounds = { x, 6, 30, 24 };
    g.setColour (juce::Colour (0xff3a3a3a));
    g.fillRoundedRectangle (duplicatePatBounds.toFloat(), 3.0f);
    g.setColour (textCol);
    g.setFont (lookAndFeel.getMonoFont (10.0f));
    g.drawText ("2x", duplicatePatBounds, juce::Justification::centred);
    g.setFont (lookAndFeel.getMonoFont (13.0f));
    x += 34;

    // [-] button
    removePatBounds = { x, 6, 24, 24 };
    g.setColour (juce::Colour (0xff3a3a3a));
    g.fillRoundedRectangle (removePatBounds.toFloat(), 3.0f);
    g.setColour (textCol);
    g.drawText ("-", removePatBounds, juce::Justification::centred);
    x += 28;

    // Pattern name
    patNameBounds = { x, 0, 100, kToolbarHeight };
    g.setColour (textCol.withAlpha (0.7f));
    g.drawText ("\"" + patternName + "\"", patNameBounds, juce::Justification::centredLeft);
    x += 104;

    // Separator
    g.setColour (juce::Colour (0xff444444));
    g.drawVerticalLine (x, 4.0f, static_cast<float> (kToolbarHeight - 4));
    x += 8;

    // Pattern length (draggable)
    auto lenStr = juce::String::formatted ("Len:%d", patternLength);
    lengthBounds = { x, 0, 60, kToolbarHeight };
    g.setColour (dragTarget == DragTarget::Length ? juce::Colour (0xff88aacc) : textCol);
    g.drawText (lenStr, lengthBounds, juce::Justification::centredLeft);
    x += 64;

    // Separator
    g.setColour (juce::Colour (0xff444444));
    g.drawVerticalLine (x, 4.0f, static_cast<float> (kToolbarHeight - 4));
    x += 8;

    // Instrument (draggable)
    auto instStr = juce::String::formatted ("Inst:%02X", instrument);
    instrumentBounds = { x, 0, 60, kToolbarHeight };
    g.setColour (dragTarget == DragTarget::Instrument
                     ? juce::Colour (0xff88aacc)
                     : lookAndFeel.findColour (TrackerLookAndFeel::instrumentColourId));
    g.drawText (instStr, instrumentBounds, juce::Justification::centredLeft);
    x += 64;

    // Sample name (if available)
    if (sampleName.isNotEmpty())
    {
        g.setColour (textCol.withAlpha (0.5f));
        g.setFont (lookAndFeel.getMonoFont (11.0f));
        auto truncName = sampleName.substring (0, 12);
        g.drawText (truncName, x, 0, 90, kToolbarHeight, juce::Justification::centredLeft);
        x += 90;
        g.setFont (lookAndFeel.getMonoFont (13.0f));
    }

    // Octave (draggable)
    auto octStr = juce::String::formatted ("Oct:%d", octave);
    octaveBounds = { x, 0, 50, kToolbarHeight };
    g.setColour (dragTarget == DragTarget::Octave ? juce::Colour (0xff88aacc) : textCol);
    g.drawText (octStr, octaveBounds, juce::Justification::centredLeft);
    x += 54;

    // Step (draggable)
    auto stepStr = juce::String::formatted ("Step:%d", step);
    stepBounds = { x, 0, 56, kToolbarHeight };
    g.setColour (dragTarget == DragTarget::Step ? juce::Colour (0xff88aacc) : textCol);
    g.drawText (stepStr, stepBounds, juce::Justification::centredLeft);
    x += 60;

    // Separator
    g.setColour (juce::Colour (0xff444444));
    g.drawVerticalLine (x, 4.0f, static_cast<float> (kToolbarHeight - 4));
    x += 8;

    // BPM (draggable)
    auto bpmStr = juce::String::formatted ("BPM:%.1f", bpm);
    bpmBounds = { x, 0, 80, kToolbarHeight };
    g.setColour (dragTarget == DragTarget::Bpm ? juce::Colour (0xff88aacc) : textCol);
    g.drawText (bpmStr, bpmBounds, juce::Justification::centredLeft);
    x += 84;

    // RPB (draggable)
    auto rpbStr = juce::String::formatted ("RPB:%d", rowsPerBeatVal);
    rpbBounds = { x, 0, 50, kToolbarHeight };
    g.setColour (dragTarget == DragTarget::Rpb ? juce::Colour (0xff88aacc) : textCol);
    g.drawText (rpbStr, rpbBounds, juce::Justification::centredLeft);
    x += 54;

    // Play state
    auto stateStr = playing ? "PLAYING" : "STOPPED";
    g.setColour (playing ? juce::Colour (0xff5cba5c) : juce::Colour (0xff888888));
    g.drawText (stateStr, x, 0, 70, kToolbarHeight, juce::Justification::centredLeft);
    x += 74;

    // Mode toggle (clickable)
    auto modeStr = songMode ? "SONG" : "PAT";
    modeBounds = { x, 0, 50, kToolbarHeight };
    g.setColour (songMode ? juce::Colour (0xffd4a843) : textCol);
    g.drawText (modeStr, modeBounds, juce::Justification::centredLeft);
    x += 50;

    // Follow toggle (Off / CTR / PGE)
    followBounds = { x, 6, 28, 24 };
    g.setColour (followModeVal > 0 ? juce::Colour (0xff5cba5c) : juce::Colour (0xff3a3a3a));
    g.fillRoundedRectangle (followBounds.toFloat(), 3.0f);
    g.setColour (followModeVal > 0 ? juce::Colours::white : textCol);
    g.setFont (lookAndFeel.getMonoFont (9.0f));
    auto folStr = followModeVal == 0 ? "FOL" : (followModeVal == 1 ? "CTR" : "PGE");
    g.drawText (folStr, followBounds, juce::Justification::centred);
    g.setFont (lookAndFeel.getMonoFont (13.0f));
    x += 32;

    // Metronome toggle
    metronomeBounds = { x, 6, 28, 24 };
    g.setColour (metronomeOn ? juce::Colour (0xffd4a843) : juce::Colour (0xff3a3a3a));
    g.fillRoundedRectangle (metronomeBounds.toFloat(), 3.0f);
    g.setColour (metronomeOn ? juce::Colours::white : textCol);
    g.setFont (lookAndFeel.getMonoFont (9.0f));
    g.drawText ("MET", metronomeBounds, juce::Justification::centred);
    g.setFont (lookAndFeel.getMonoFont (13.0f));
    x += 32;

    // FX reference button
    fxRefBounds = { x, 6, 24, 24 };
    g.setColour (juce::Colour (0xff3a3a3a));
    g.fillRoundedRectangle (fxRefBounds.toFloat(), 3.0f);
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::fxColourId));
    g.setFont (lookAndFeel.getMonoFont (9.0f));
    g.drawText ("FX", fxRefBounds, juce::Justification::centred);
    g.setFont (lookAndFeel.getMonoFont (13.0f));
    x += 28;

    // Automation panel toggle
    automationToggleBounds = { x, 6, 32, 24 };
    g.setColour (automationOn ? juce::Colour (0xff5c8abf) : juce::Colour (0xff3a3a3a));
    g.fillRoundedRectangle (automationToggleBounds.toFloat(), 3.0f);
    g.setColour (automationOn ? juce::Colours::white : textCol);
    g.setFont (lookAndFeel.getMonoFont (9.0f));
    g.drawText ("AUTO", automationToggleBounds, juce::Justification::centred);
    g.setFont (lookAndFeel.getMonoFont (13.0f));

    // Instrument panel toggle (right-aligned)
    instrumentToggleBounds = { getWidth() - 32, 6, 24, 24 };
    g.setColour (instrumentPanelOn ? juce::Colour (0xff5c8abf) : juce::Colour (0xff3a3a3a));
    g.fillRoundedRectangle (instrumentToggleBounds.toFloat(), 3.0f);
    g.setColour (instrumentPanelOn ? juce::Colours::white : textCol);
    g.setFont (lookAndFeel.getMonoFont (11.0f));
    g.drawText ("INS", instrumentToggleBounds, juce::Justification::centred);
    g.setFont (lookAndFeel.getMonoFont (13.0f));

    // Bottom border
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (kToolbarHeight - 1, 0.0f, static_cast<float> (getWidth()));
}

void ToolbarComponent::resized()
{
}

void ToolbarComponent::mouseDown (const juce::MouseEvent& event)
{
    auto pos = event.getPosition();

    if (arrangementToggleBounds.contains (pos) && onToggleArrangement)
    {
        onToggleArrangement();
        return;
    }
    if (instrumentToggleBounds.contains (pos) && onToggleInstrumentPanel)
    {
        onToggleInstrumentPanel();
        return;
    }
    if (addPatBounds.contains (pos) && onAddPattern)
    {
        onAddPattern();
        return;
    }
    if (duplicatePatBounds.contains (pos) && onDuplicatePattern)
    {
        onDuplicatePattern();
        return;
    }
    if (removePatBounds.contains (pos) && onRemovePattern)
    {
        onRemovePattern();
        return;
    }
    if (modeBounds.contains (pos) && onModeToggle)
    {
        onModeToggle();
        return;
    }
    if (followBounds.contains (pos) && onFollowToggle)
    {
        onFollowToggle();
        return;
    }
    if (metronomeBounds.contains (pos) && onMetronomeToggle)
    {
        onMetronomeToggle();
        return;
    }
    if (fxRefBounds.contains (pos) && onShowFxReference)
    {
        onShowFxReference();
        return;
    }
    if (automationToggleBounds.contains (pos) && onToggleAutomation)
    {
        onToggleAutomation();
        return;
    }

    // Start drag on draggable fields
    dragTarget = DragTarget::None;
    dragStartY = event.y;
    dragAccumulated = 0;

    if (lengthBounds.contains (pos))
        dragTarget = DragTarget::Length;
    else if (bpmBounds.contains (pos))
        dragTarget = DragTarget::Bpm;
    else if (stepBounds.contains (pos))
        dragTarget = DragTarget::Step;
    else if (octaveBounds.contains (pos))
        dragTarget = DragTarget::Octave;
    else if (instrumentBounds.contains (pos))
        dragTarget = DragTarget::Instrument;
    else if (rpbBounds.contains (pos))
        dragTarget = DragTarget::Rpb;

    if (dragTarget != DragTarget::None)
        repaint();
}

void ToolbarComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (dragTarget == DragTarget::None)
        return;

    int deltaY = dragStartY - event.y;  // up = positive
    int threshold = 4; // pixels per step

    int steps = (deltaY - dragAccumulated) / threshold;
    if (steps == 0) return;

    dragAccumulated += steps * threshold;

    switch (dragTarget)
    {
        case DragTarget::Length:
            if (onLengthDrag) onLengthDrag (steps);
            break;
        case DragTarget::Bpm:
            if (onBpmDrag) onBpmDrag (static_cast<double> (steps));
            break;
        case DragTarget::Step:
            if (onStepDrag) onStepDrag (steps);
            break;
        case DragTarget::Octave:
            if (onOctaveDrag) onOctaveDrag (steps);
            break;
        case DragTarget::Instrument:
            if (onInstrumentDrag) onInstrumentDrag (steps);
            break;
        case DragTarget::Rpb:
            if (onRpbDrag) onRpbDrag (steps);
            break;
        case DragTarget::None: break;
    }
}

void ToolbarComponent::mouseUp (const juce::MouseEvent&)
{
    if (dragTarget != DragTarget::None)
    {
        dragTarget = DragTarget::None;
        repaint();
    }
}

void ToolbarComponent::mouseDoubleClick (const juce::MouseEvent& event)
{
    auto pos = event.getPosition();

    if (lengthBounds.contains (pos) && onPatternLengthClick)
        onPatternLengthClick();
    else if (patNameBounds.contains (pos) && onPatternNameDoubleClick)
        onPatternNameDoubleClick();
}

void ToolbarComponent::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    auto pos = event.getPosition();
    if (patSelectorBounds.contains (pos))
    {
        if (wheel.deltaY > 0 && onNextPattern)
            onNextPattern();
        else if (wheel.deltaY < 0 && onPrevPattern)
            onPrevPattern();
    }
}

void ToolbarComponent::setPatternInfo (int current, int total, const juce::String& name)
{
    currentPattern = current;
    totalPatterns = total;
    patternName = name;
    repaint();
}

void ToolbarComponent::setPatternLength (int len)
{
    patternLength = len;
    repaint();
}

void ToolbarComponent::setInstrument (int inst)
{
    instrument = inst;
    repaint();
}

void ToolbarComponent::setOctave (int oct)
{
    octave = oct;
    repaint();
}

void ToolbarComponent::setEditStep (int s)
{
    step = s;
    repaint();
}

void ToolbarComponent::setBpm (double b)
{
    bpm = b;
    repaint();
}

void ToolbarComponent::setPlayState (bool p)
{
    playing = p;
    repaint();
}

void ToolbarComponent::setPlaybackMode (bool sm)
{
    songMode = sm;
    repaint();
}

void ToolbarComponent::setSampleName (const juce::String& name)
{
    sampleName = name;
    repaint();
}
