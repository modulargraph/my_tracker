#pragma once

#include <array>
#include <JuceHeader.h>

namespace FxParamTransport
{
constexpr int kParamHighBitCc = 118;
constexpr int kNoPendingParamHighBit = -1;
constexpr int kDedicatedParamHighBitCcBase = 11;
constexpr int kDedicatedParamValueCcMin = 31;
constexpr int kDedicatedParamValueCcMax = 41;

inline bool usesDedicatedHighBitController (int valueController)
{
    return valueController >= kDedicatedParamValueCcMin
        && valueController <= kDedicatedParamValueCcMax;
}

inline int getHighBitControllerForValueController (int valueController)
{
    if (usesDedicatedHighBitController (valueController))
        return kDedicatedParamHighBitCcBase + valueController - kDedicatedParamValueCcMin;

    return kParamHighBitCc;
}

inline int getValueControllerForHighBitController (int highBitController)
{
    const int offset = highBitController - kDedicatedParamHighBitCcBase;
    if (offset < 0 || offset > kDedicatedParamValueCcMax - kDedicatedParamValueCcMin)
        return -1;

    return kDedicatedParamValueCcMin + offset;
}

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
    const int highBitController = getHighBitControllerForValueController (valueController);
    const double highBitTime = valueTimeSeconds;

    sequence.addEvent (juce::MidiMessage::controllerEvent (midiChannel, highBitController, highBit), highBitTime);
    sequence.addEvent (juce::MidiMessage::controllerEvent (midiChannel, valueController, lowBits), valueTimeSeconds);
}

inline int consumeByteFromController (int lowBitsValue, int& pendingHighBit)
{
    const int highBit = pendingHighBit == kNoPendingParamHighBit ? 0 : (pendingHighBit & 0x1);
    const int byte = (highBit << 7) | (lowBitsValue & 0x7F);
    pendingHighBit = 0;
    return byte;
}

inline int consumeByteFromController (int valueController,
                                      int lowBitsValue,
                                      std::array<int, 128>& pendingHighBitsByController,
                                      int& legacyPendingHighBit)
{
    int highBit = 0;

    if (valueController >= 0 && valueController < static_cast<int> (pendingHighBitsByController.size())
        && pendingHighBitsByController[static_cast<size_t> (valueController)] != kNoPendingParamHighBit)
    {
        highBit = pendingHighBitsByController[static_cast<size_t> (valueController)] & 0x1;
        pendingHighBitsByController[static_cast<size_t> (valueController)] = kNoPendingParamHighBit;
    }
    else if (legacyPendingHighBit != kNoPendingParamHighBit)
    {
        highBit = legacyPendingHighBit & 0x1;
    }

    legacyPendingHighBit = kNoPendingParamHighBit;
    return ((highBit & 0x1) << 7) | (lowBitsValue & 0x7F);
}
}
