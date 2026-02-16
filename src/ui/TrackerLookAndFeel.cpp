#include "TrackerLookAndFeel.h"

TrackerLookAndFeel::TrackerLookAndFeel()
{
    // Set the dark colour scheme
    setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (0xff1a1a1a));
    setColour (juce::Label::textColourId, juce::Colour (0xffcccccc));

    // Custom tracker colours
    setColour (backgroundColourId,     juce::Colour (0xff1a1a1a));
    setColour (gridLineColourId,       juce::Colour (0xff333333));
    setColour (textColourId,           juce::Colour (0xffcccccc));
    setColour (cursorRowColourId,      juce::Colour (0xff1e3a5a));
    setColour (cursorCellColourId,     juce::Colour (0xff2a4a6a));
    setColour (beatMarkerColourId,     juce::Colour (0xff222222));
    setColour (headerColourId,         juce::Colour (0xff252525));
    setColour (playbackCursorColourId, juce::Colour (0xff4a6a2a));
    setColour (noteColourId,           juce::Colour (0xffcccccc));  // light gray
    setColour (instrumentColourId,     juce::Colour (0xffd4a843));  // amber
    setColour (volumeColourId,         juce::Colour (0xff5cba5c));  // green
    setColour (fxColourId,             juce::Colour (0xff5c8abf));  // blue
    setColour (selectionColourId,      juce::Colour (0x44ffffff));  // translucent white
    setColour (muteColourId,           juce::Colour (0xffcc4444));  // red
    setColour (soloColourId,           juce::Colour (0xffd4d444));  // yellow
    setColour (groupHeaderColourId,    juce::Colour (0xff383848));  // subtle dark
}

juce::Font TrackerLookAndFeel::getMonoFont (float height) const
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
}
