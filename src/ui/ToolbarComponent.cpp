#include "ToolbarComponent.h"

namespace
{
juce::Colour getToolbarButtonColour (TrackerLookAndFeel& lookAndFeel)
{
    auto header = lookAndFeel.findColour (TrackerLookAndFeel::headerColourId);
    return header.getPerceivedBrightness() >= 0.5f ? header.darker (0.08f)
                                                   : header.brighter (0.10f);
}
}

ToolbarComponent::ToolbarComponent (TrackerLookAndFeel& lnf)
    : lookAndFeel (lnf)
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
}

void ToolbarComponent::setChordEntryState (bool enabled, const juce::String& chordSetLabel,
                                           const juce::String& rootLabel, const juce::String& scaleLabel)
{
    chordEntryOn = enabled;
    chordEntryLabel = chordSetLabel.isNotEmpty() ? chordSetLabel : "CHD";
    chordRootLabel = rootLabel.isNotEmpty() ? rootLabel : "C";
    chordScaleLabel = scaleLabel.isNotEmpty() ? scaleLabel : "MAJ";
    repaint();
}

void ToolbarComponent::paint (juce::Graphics& g)
{
    auto bg = lookAndFeel.findColour (TrackerLookAndFeel::headerColourId);
    g.fillAll (bg);

    g.setFont (lookAndFeel.getMonoFont (13.0f));
    auto textCol = lookAndFeel.findColour (TrackerLookAndFeel::textColourId);
    auto gridCol = lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId);
    auto buttonCol = getToolbarButtonColour (lookAndFeel);
    auto accentCol = lookAndFeel.findColour (TrackerLookAndFeel::fxColourId);
    auto playCol = lookAndFeel.findColour (TrackerLookAndFeel::volumeColourId);
    auto emphasisCol = lookAndFeel.findColour (TrackerLookAndFeel::instrumentColourId);

    int x = 8;

    // Arrangement panel toggle (left of pattern selector)
    arrangementToggleBounds = { x, 6, 24, 24 };
    g.setColour (arrangementOn ? accentCol : buttonCol);
    g.fillRoundedRectangle (arrangementToggleBounds.toFloat(), 3.0f);
    g.setColour (arrangementOn ? accentCol.contrasting() : textCol);
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
    g.setColour (buttonCol);
    g.fillRoundedRectangle (addPatBounds.toFloat(), 3.0f);
    g.setColour (textCol);
    g.drawText ("+", addPatBounds, juce::Justification::centred);
    x += 28;

    // [2x] duplicate button
    duplicatePatBounds = { x, 6, 30, 24 };
    g.setColour (buttonCol);
    g.fillRoundedRectangle (duplicatePatBounds.toFloat(), 3.0f);
    g.setColour (textCol);
    g.setFont (lookAndFeel.getMonoFont (10.0f));
    g.drawText ("2x", duplicatePatBounds, juce::Justification::centred);
    g.setFont (lookAndFeel.getMonoFont (13.0f));
    x += 34;

    // [-] button
    removePatBounds = { x, 6, 24, 24 };
    g.setColour (buttonCol);
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
    g.setColour (gridCol);
    g.drawVerticalLine (x, 4.0f, static_cast<float> (kToolbarHeight - 4));
    x += 8;

    // Pattern length (draggable)
    auto lenStr = juce::String::formatted ("Len:%d", patternLength);
    lengthBounds = { x, 0, 60, kToolbarHeight };
    g.setColour (dragTarget == DragTarget::Length ? accentCol : textCol);
    g.drawText (lenStr, lengthBounds, juce::Justification::centredLeft);
    x += 64;

    // Separator
    g.setColour (gridCol);
    g.drawVerticalLine (x, 4.0f, static_cast<float> (kToolbarHeight - 4));
    x += 8;

    // Instrument (draggable)
    auto instStr = juce::String::formatted ("Inst:%02X", instrument);
    instrumentBounds = { x, 0, 60, kToolbarHeight };
    g.setColour (dragTarget == DragTarget::Instrument
                     ? accentCol
                     : lookAndFeel.getInstrumentColour (instrument).brighter (0.18f));
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
    g.setColour (dragTarget == DragTarget::Octave ? accentCol : textCol);
    g.drawText (octStr, octaveBounds, juce::Justification::centredLeft);
    x += 54;

    // Chord root (draggable / scrollable text)
    auto rootStr = "Key:" + chordRootLabel;
    chordRootBounds = { x, 0, 54, kToolbarHeight };
    g.setColour (dragTarget == DragTarget::ChordRoot
                     ? accentCol
                     : lookAndFeel.findColour (TrackerLookAndFeel::noteColourId));
    g.drawText (rootStr, chordRootBounds, juce::Justification::centredLeft);
    x += 58;

    // Chord scale (draggable / scrollable text)
    auto scaleStr = "Scale:" + chordScaleLabel;
    chordScaleBounds = { x, 0, 76, kToolbarHeight };
    g.setColour (dragTarget == DragTarget::ChordScale
                     ? accentCol
                     : lookAndFeel.findColour (TrackerLookAndFeel::noteColourId));
    g.drawText (scaleStr, chordScaleBounds, juce::Justification::centredLeft);
    x += 80;

    // Step (draggable)
    auto stepStr = juce::String::formatted ("Step:%d", step);
    stepBounds = { x, 0, 56, kToolbarHeight };
    g.setColour (dragTarget == DragTarget::Step ? accentCol : textCol);
    g.drawText (stepStr, stepBounds, juce::Justification::centredLeft);
    x += 60;

    // Separator
    g.setColour (gridCol);
    g.drawVerticalLine (x, 4.0f, static_cast<float> (kToolbarHeight - 4));
    x += 8;

    // BPM (draggable)
    auto bpmStr = juce::String::formatted ("BPM:%.1f", bpm);
    bpmBounds = { x, 0, 80, kToolbarHeight };
    g.setColour (dragTarget == DragTarget::Bpm ? accentCol : textCol);
    g.drawText (bpmStr, bpmBounds, juce::Justification::centredLeft);
    x += 84;

    // RPB (draggable)
    auto rpbStr = juce::String::formatted ("RPB:%d", rowsPerBeatVal);
    rpbBounds = { x, 0, 50, kToolbarHeight };
    g.setColour (dragTarget == DragTarget::Rpb ? accentCol : textCol);
    g.drawText (rpbStr, rpbBounds, juce::Justification::centredLeft);
    x += 54;

    // Play state
    auto stateStr = playing ? "PLAYING" : "STOPPED";
    g.setColour (playing ? playCol : textCol.withAlpha (0.55f));
    g.drawText (stateStr, x, 0, 70, kToolbarHeight, juce::Justification::centredLeft);
    x += 74;

    // Mode toggle (clickable)
    auto modeStr = songMode ? "SONG" : "PAT";
    modeBounds = { x, 0, 50, kToolbarHeight };
    g.setColour (songMode ? emphasisCol : textCol);
    g.drawText (modeStr, modeBounds, juce::Justification::centredLeft);
    x += 50;

    // Follow toggle (Off / CTR / PGE)
    followBounds = { x, 6, 28, 24 };
    g.setColour (followModeVal > 0 ? playCol : buttonCol);
    g.fillRoundedRectangle (followBounds.toFloat(), 3.0f);
    g.setColour (followModeVal > 0 ? playCol.contrasting() : textCol);
    g.setFont (lookAndFeel.getMonoFont (9.0f));
    auto folStr = followModeVal == 0 ? "FOL" : (followModeVal == 1 ? "CTR" : "PGE");
    g.drawText (folStr, followBounds, juce::Justification::centred);
    g.setFont (lookAndFeel.getMonoFont (13.0f));
    x += 32;

    // Metronome toggle
    metronomeBounds = { x, 6, 28, 24 };
    g.setColour (metronomeOn ? emphasisCol : buttonCol);
    g.fillRoundedRectangle (metronomeBounds.toFloat(), 3.0f);
    g.setColour (metronomeOn ? emphasisCol.contrasting() : textCol);
    g.setFont (lookAndFeel.getMonoFont (9.0f));
    g.drawText ("MET", metronomeBounds, juce::Justification::centred);
    g.setFont (lookAndFeel.getMonoFont (13.0f));
    x += 32;

    // FX reference button
    fxRefBounds = { x, 6, 24, 24 };
    g.setColour (buttonCol);
    g.fillRoundedRectangle (fxRefBounds.toFloat(), 3.0f);
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::fxColourId));
    g.setFont (lookAndFeel.getMonoFont (9.0f));
    g.drawText ("FX", fxRefBounds, juce::Justification::centred);
    g.setFont (lookAndFeel.getMonoFont (13.0f));
    x += 28;

    // Chord entry toggle
    chordEntryBounds = { x, 6, 34, 24 };
    g.setColour (chordEntryOn ? lookAndFeel.findColour (TrackerLookAndFeel::noteColourId).withAlpha (0.42f)
                              : buttonCol);
    g.fillRoundedRectangle (chordEntryBounds.toFloat(), 3.0f);
    g.setColour (chordEntryOn ? lookAndFeel.findColour (TrackerLookAndFeel::noteColourId).contrasting()
                              : lookAndFeel.findColour (TrackerLookAndFeel::noteColourId));
    g.setFont (lookAndFeel.getMonoFont (9.0f));
    g.drawText ("CHD", chordEntryBounds, juce::Justification::centred);
    g.setFont (lookAndFeel.getMonoFont (13.0f));
    x += 38;

    // Chord set selector
    chordSetBounds = { x, 6, 38, 24 };
    g.setColour (chordEntryOn ? lookAndFeel.findColour (TrackerLookAndFeel::noteColourId).withAlpha (0.28f)
                              : buttonCol);
    g.fillRoundedRectangle (chordSetBounds.toFloat(), 3.0f);
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::noteColourId));
    g.setFont (lookAndFeel.getMonoFont (9.0f));
    g.drawText (chordEntryLabel, chordSetBounds, juce::Justification::centred);
    g.setFont (lookAndFeel.getMonoFont (13.0f));
    x += 42;

    // Instrument panel toggle (right-aligned)
    instrumentToggleBounds = { getWidth() - 32, 6, 24, 24 };
    g.setColour (instrumentPanelOn ? accentCol : buttonCol);
    g.fillRoundedRectangle (instrumentToggleBounds.toFloat(), 3.0f);
    g.setColour (instrumentPanelOn ? accentCol.contrasting() : textCol);
    g.setFont (lookAndFeel.getMonoFont (11.0f));
    g.drawText ("INS", instrumentToggleBounds, juce::Justification::centred);
    g.setFont (lookAndFeel.getMonoFont (13.0f));

    // Bottom border
    g.setColour (gridCol);
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
    if (chordRootBounds.contains (pos) && event.mods.isPopupMenu())
    {
        if (onShowChordRootMenu)
            onShowChordRootMenu (event.getScreenPosition());
        return;
    }
    if (chordScaleBounds.contains (pos) && event.mods.isPopupMenu())
    {
        if (onShowChordScaleMenu)
            onShowChordScaleMenu (event.getScreenPosition());
        return;
    }
    if (chordSetBounds.contains (pos))
    {
        if (onCycleChordSet)
            onCycleChordSet();
        return;
    }
    if (chordEntryBounds.contains (pos))
    {
        if (event.mods.isPopupMenu())
        {
            if (onCycleChordSet)
                onCycleChordSet();
        }
        else if (onToggleChordEntry)
        {
            onToggleChordEntry();
        }
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
    else if (chordRootBounds.contains (pos))
        dragTarget = DragTarget::ChordRoot;
    else if (chordScaleBounds.contains (pos))
        dragTarget = DragTarget::ChordScale;

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
        case DragTarget::ChordRoot:
            if (onChordRootDrag) onChordRootDrag (steps);
            break;
        case DragTarget::ChordScale:
            if (onChordScaleDrag) onChordScaleDrag (steps);
            break;
        case DragTarget::None: break;
    }
}

void ToolbarComponent::mouseUp (const juce::MouseEvent&)
{
    if (dragTarget != DragTarget::None)
    {
        const auto completedTarget = dragTarget;
        dragTarget = DragTarget::None;
        repaint();

        if (completedTarget == DragTarget::Bpm && onBpmDragEnd)
            onBpmDragEnd();
    }
}

void ToolbarComponent::mouseDoubleClick (const juce::MouseEvent& event)
{
    auto pos = event.getPosition();

    if (patSelectorBounds.contains (pos) && onPatternSelectorDoubleClick)
        onPatternSelectorDoubleClick();
    else if (lengthBounds.contains (pos) && onPatternLengthClick)
        onPatternLengthClick();
    else if (instrumentBounds.contains (pos) && onInstrumentDoubleClick)
        onInstrumentDoubleClick();
    else if (octaveBounds.contains (pos) && onOctaveDoubleClick)
        onOctaveDoubleClick();
    else if (chordRootBounds.contains (pos) && onChordRootDoubleClick)
        onChordRootDoubleClick();
    else if (chordScaleBounds.contains (pos) && onChordScaleDoubleClick)
        onChordScaleDoubleClick();
    else if (stepBounds.contains (pos) && onStepDoubleClick)
        onStepDoubleClick();
    else if (bpmBounds.contains (pos) && onBpmDoubleClick)
        onBpmDoubleClick();
    else if (rpbBounds.contains (pos) && onRpbDoubleClick)
        onRpbDoubleClick();
    else if (patNameBounds.contains (pos) && onPatternNameDoubleClick)
        onPatternNameDoubleClick();
}

void ToolbarComponent::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    auto pos = event.getPosition();
    if (wheel.deltaY == 0.0f)
        return;

    const int steps = wheel.deltaY > 0.0f ? 1 : -1;

    if (patSelectorBounds.contains (pos))
    {
        if (steps > 0 && onNextPattern)
            onNextPattern();
        else if (steps < 0 && onPrevPattern)
            onPrevPattern();
        return;
    }

    if (lengthBounds.contains (pos) && onLengthDrag)
        onLengthDrag (steps);
    else if (instrumentBounds.contains (pos) && onInstrumentDrag)
        onInstrumentDrag (steps);
    else if (octaveBounds.contains (pos) && onOctaveDrag)
        onOctaveDrag (steps);
    else if (chordRootBounds.contains (pos) && onChordRootDrag)
        onChordRootDrag (steps);
    else if (chordScaleBounds.contains (pos) && onChordScaleDrag)
        onChordScaleDrag (steps);
    else if (stepBounds.contains (pos) && onStepDrag)
        onStepDrag (steps);
    else if (bpmBounds.contains (pos) && onBpmDrag)
    {
        onBpmDrag (static_cast<double> (steps));
        if (onBpmDragEnd)
            onBpmDragEnd();
    }
    else if (rpbBounds.contains (pos) && onRpbDrag)
        onRpbDrag (steps);
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
