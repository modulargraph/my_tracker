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

    // Pattern selector
    auto patStr = juce::String::formatted ("Pat:%02d/%02d", currentPattern, totalPatterns);
    g.setColour (textCol);
    g.drawText (patStr, x, 0, 80, kToolbarHeight, juce::Justification::centredLeft);
    x += 82;

    // [+] button
    addPatBounds = { x, 6, 24, 24 };
    g.setColour (juce::Colour (0xff3a3a3a));
    g.fillRoundedRectangle (addPatBounds.toFloat(), 3.0f);
    g.setColour (textCol);
    g.drawText ("+", addPatBounds, juce::Justification::centred);
    x += 28;

    // [-] button
    removePatBounds = { x, 6, 24, 24 };
    g.setColour (juce::Colour (0xff3a3a3a));
    g.fillRoundedRectangle (removePatBounds.toFloat(), 3.0f);
    g.setColour (textCol);
    g.drawText ("-", removePatBounds, juce::Justification::centred);
    x += 28;

    // Pattern name
    g.setColour (textCol.withAlpha (0.7f));
    g.drawText ("\"" + patternName + "\"", x, 0, 100, kToolbarHeight, juce::Justification::centredLeft);
    x += 104;

    // Separator
    g.setColour (juce::Colour (0xff444444));
    g.drawVerticalLine (x, 4.0f, static_cast<float> (kToolbarHeight - 4));
    x += 8;

    // Pattern length
    auto lenStr = juce::String::formatted ("Len:%d", patternLength);
    lengthBounds = { x, 0, 60, kToolbarHeight };
    g.setColour (textCol);
    g.drawText (lenStr, lengthBounds, juce::Justification::centredLeft);
    x += 64;

    // Separator
    g.setColour (juce::Colour (0xff444444));
    g.drawVerticalLine (x, 4.0f, static_cast<float> (kToolbarHeight - 4));
    x += 8;

    // Instrument
    auto instStr = juce::String::formatted ("Inst:%02X", instrument);
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::instrumentColourId));
    g.drawText (instStr, x, 0, 60, kToolbarHeight, juce::Justification::centredLeft);
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

    // Octave
    auto octStr = juce::String::formatted ("Oct:%d", octave);
    g.setColour (textCol);
    g.drawText (octStr, x, 0, 50, kToolbarHeight, juce::Justification::centredLeft);
    x += 54;

    // Step
    auto stepStr = juce::String::formatted ("Step:%d", step);
    g.setColour (textCol);
    g.drawText (stepStr, x, 0, 56, kToolbarHeight, juce::Justification::centredLeft);
    x += 60;

    // Separator
    g.setColour (juce::Colour (0xff444444));
    g.drawVerticalLine (x, 4.0f, static_cast<float> (kToolbarHeight - 4));
    x += 8;

    // BPM
    auto bpmStr = juce::String::formatted ("BPM:%.1f", bpm);
    g.setColour (textCol);
    g.drawText (bpmStr, x, 0, 80, kToolbarHeight, juce::Justification::centredLeft);
    x += 84;

    // Play state
    auto stateStr = playing ? "PLAYING" : "STOPPED";
    g.setColour (playing ? juce::Colour (0xff5cba5c) : juce::Colour (0xff888888));
    g.drawText (stateStr, x, 0, 70, kToolbarHeight, juce::Justification::centredLeft);
    x += 74;

    // Mode toggle
    auto modeStr = songMode ? "SONG" : "PAT";
    g.setColour (songMode ? juce::Colour (0xffd4a843) : textCol);
    g.drawText (modeStr, x, 0, 50, kToolbarHeight, juce::Justification::centredLeft);

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

    if (addPatBounds.contains (pos) && onAddPattern)
        onAddPattern();
    else if (removePatBounds.contains (pos) && onRemovePattern)
        onRemovePattern();
    else if (lengthBounds.contains (pos) && onPatternLengthClick)
        onPatternLengthClick();
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
