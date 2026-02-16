#pragma once

#include <JuceHeader.h>

namespace NoteUtils
{

inline juce::String noteToString (int note)
{
    if (note < 0)
        return "---";
    if (note == 255) // note-off (OFF)
        return "===";
    if (note == 254) // note-kill (KILL)
        return "^^^";

    static const char* noteNames[] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };
    int octave = note / 12;
    int noteIndex = note % 12;
    return juce::String (noteNames[noteIndex]) + juce::String (octave);
}

inline int keyToNote (const juce::KeyPress& key, int currentOctave)
{
    if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown() || key.getModifiers().isAltDown())
        return -1;

    auto c = key.getTextCharacter();

    // Lower octave (current octave)
    int baseNote = currentOctave * 12;
    switch (c)
    {
        case 'z': return baseNote + 0;   // C
        case 's': return baseNote + 1;   // C#
        case 'x': return baseNote + 2;   // D
        case 'd': return baseNote + 3;   // D#
        case 'c': return baseNote + 4;   // E
        case 'v': return baseNote + 5;   // F
        case 'g': return baseNote + 6;   // F#
        case 'b': return baseNote + 7;   // G
        case 'h': return baseNote + 8;   // G#
        case 'n': return baseNote + 9;   // A
        case 'j': return baseNote + 10;  // A#
        case 'm': return baseNote + 11;  // B
        default: break;
    }

    // Upper octave (current octave + 1)
    int upperBase = (currentOctave + 1) * 12;
    switch (c)
    {
        case 'q': return upperBase + 0;   // C
        case '2': return upperBase + 1;   // C#
        case 'w': return upperBase + 2;   // D
        case '3': return upperBase + 3;   // D#
        case 'e': return upperBase + 4;   // E
        case 'r': return upperBase + 5;   // F
        case '5': return upperBase + 6;   // F#
        case 't': return upperBase + 7;   // G
        case '6': return upperBase + 8;   // G#
        case 'y': return upperBase + 9;   // A
        case '7': return upperBase + 10;  // A#
        case 'u': return upperBase + 11;  // B
        default: break;
    }

    return -1;
}

inline int hexCharToValue (juce::juce_wchar c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

} // namespace NoteUtils
