#pragma once

#include <JuceHeader.h>

namespace InstrumentRouting
{
constexpr int kMinInstrument = 0;
constexpr int kMaxInstrument = 255;

inline int clampInstrumentIndex (int instrument)
{
    return juce::jlimit (kMinInstrument, kMaxInstrument, instrument);
}

inline int getBankMsbForInstrument (int instrument)
{
    return (clampInstrumentIndex (instrument) >> 7) & 0x7F;
}

inline int getProgramForInstrument (int instrument)
{
    return clampInstrumentIndex (instrument) & 0x7F;
}

inline int decodeInstrumentFromBankAndProgram (int bankMsb, int program)
{
    const int bank = juce::jlimit (0, 127, bankMsb) & 0x7F;
    const int prog = juce::jlimit (0, 127, program) & 0x7F;
    return clampInstrumentIndex ((bank << 7) | prog);
}
}

