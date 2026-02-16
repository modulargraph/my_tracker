#pragma once

#include <JuceHeader.h>

class TrackerLookAndFeel : public juce::LookAndFeel_V4
{
public:
    TrackerLookAndFeel();

    // Colour IDs specific to the tracker
    enum ColourIds
    {
        backgroundColourId      = 0x2000001,
        gridLineColourId        = 0x2000002,
        textColourId            = 0x2000003,
        cursorRowColourId       = 0x2000004,
        cursorCellColourId      = 0x2000005,
        beatMarkerColourId      = 0x2000006,
        headerColourId          = 0x2000007,
        playbackCursorColourId  = 0x2000008,
        noteColourId            = 0x2000009,
        instrumentColourId      = 0x200000A,
        volumeColourId          = 0x200000B,
        fxColourId              = 0x200000C,
        selectionColourId       = 0x200000D,
        muteColourId            = 0x200000E,
        soloColourId            = 0x200000F,
        groupHeaderColourId     = 0x2000010
    };

    juce::Font getMonoFont (float height) const;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackerLookAndFeel)
};
