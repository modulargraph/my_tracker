#pragma once

#include <JuceHeader.h>

namespace FxParamTransport
{
constexpr int kParamHighBitCc = 118;

inline int clampToByte (int value)
{
    return juce::jlimit (0, 255, value);
}

inline void appendByteAsControllers (juce::MidiMessageSequence& sequence,
                                     int midiChannel,
                                     int valueController,
                                     int byteValue,
                                     double valueTimeSeconds)
{
    const int byte = clampToByte (byteValue);
    const int highBit = (byte >> 7) & 0x1;
    const int lowBits = byte & 0x7F;
    const double highBitTime = valueTimeSeconds;

    sequence.addEvent (juce::MidiMessage::controllerEvent (midiChannel, kParamHighBitCc, highBit), highBitTime);
    sequence.addEvent (juce::MidiMessage::controllerEvent (midiChannel, valueController, lowBits), valueTimeSeconds);
}

inline int consumeByteFromController (int lowBitsValue, int& pendingHighBit)
{
    const int byte = ((pendingHighBit & 0x1) << 7) | (lowBitsValue & 0x7F);
    pendingHighBit = 0;
    return byte;
}
}
