#pragma once

#include <JuceHeader.h>

namespace FormatUtils
{

inline juce::String formatDb (double db)
{
    if (db <= -99.0) return "-inf";
    return juce::String (db, 1) + " dB";
}

inline juce::String formatPercent (int v)
{
    return juce::String (v);
}

inline juce::String formatPan (int v)
{
    if (v == 0) return "C";
    if (v < 0)  return "L" + juce::String (-v);
    return "R" + juce::String (v);
}

inline juce::String formatSemitones (int v)
{
    if (v > 0) return "+" + juce::String (v);
    return juce::String (v);
}

inline juce::String formatCents (int v)
{
    if (v > 0) return "+" + juce::String (v);
    return juce::String (v);
}

inline juce::String formatSeconds (double s)
{
    return juce::String (s, 3) + "s";
}

inline juce::String formatPosSec (double normPos, double totalLen)
{
    return juce::String (normPos * totalLen, 3) + "s";
}

} // namespace FormatUtils
