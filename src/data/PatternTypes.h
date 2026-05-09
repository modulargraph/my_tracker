#pragma once

#include <cstdint>
#include <vector>
#include <juce_core/juce_core.h>

struct FxCommandInfo
{
    int command;
    char letter;
    juce::String format;
    juce::String name;
    juce::String description;
};

inline const std::vector<FxCommandInfo>& getFxCommandList()
{
    static const std::vector<FxCommandInfo> commands = {
        { 0,  '-', "--",  "None",             "clear this FX slot" },
        { 1,  '!', "!xx", "Off",              "curtail previous memory FX" },
        { 2,  'V', "Vxx", "Volume",           "per-step velocity/volume" },
        { 3,  'P', "Pxx", "Panning",          "00-64 left..right" },
        { 4,  'M', "Mxx", "Micro Tune",       "00-198 -> -99..+99 cents" },
        { 5,  'G', "Gxx", "Glide",            "speed in tracker steps" },
        { 6,  'T', "Txx", "Tempo",            "percent tempo change; 00=stop marker" },
        { 7,  'I', "Ixx", "Swing",            "timing swing amount" },
        { 8,  'm', "mxx", "Micro Move",       "signed milliseconds" },
        { 9,  'q', "qxx", "Gate Length",      "shorten note gate" },
        { 10, 'C', "Cxx", "Chance",           "step probability" },
        { 11, 'R', "Rxx", "Roll",             "00-10 R, 11-21 Rv, 22-32 RV, 33-43 Rn, 44-54 RN, 55-65 RR" },
        { 12, 'A', "Axx", "Arp",              "Polyend arp direction/timing; use MIDI Chord slot for intervals" },
        { 13, 'n', "nxx", "Random Note",      "random note range" },
        { 14, 'i', "ixx", "Random Instrument","random instrument range" },
        { 15, 'x', "xxx", "Random FX Value",  "randomize adjacent FX value" },
        { 37, 'S', "Sxx", "Slice",            "01-based slice index for Slice/Beat Slice mode" },
        { 16, 'v', "vxx", "Random Volume",    "random velocity range" },
        { 17, 'r', "rxx", "Reverse Sample",   "00=backward, 01=forward" },
        { 18, 'p', "pxx", "Position",         "00-FF -> 0.0-1.0 of region" },
        { 19, 'g', "gxx", "Volume LFO Rate",  "volume LFO speed" },
        { 20, 'h', "hxx", "Panning LFO Rate", "panning LFO speed" },
        { 21, 'j', "jxx", "Filter LFO Rate",  "filter LFO speed" },
        { 22, 'k', "kxx", "Position LFO Rate","position LFO speed" },
        { 23, 'l', "lxx", "Finetune LFO Rate","finetune LFO speed" },
        { 24, 'D', "Dxx", "Overdrive",        "00-FF drive amount" },
        { 25, 'L', "Lxx", "Low Pass Filter",  "00-FF cutoff" },
        { 26, 'B', "Bxx", "Band Pass Filter", "00-FF cutoff" },
        { 27, 'H', "Hxx", "High Pass Filter", "00-FF cutoff" },
        { 28, 's', "sxx", "Delay Send",       "00-FF send level" },
        { 29, 't', "txx", "Reverb Send",      "00-FF send level" },
        { 30, 'E', "Exx", "Bit Depth",        "00-FF -> 4..16 bits" },
        { 31, 'U', "Uxx", "Tuning",           "signed semitones" },
        { 32, 'a', "axx", "MIDI Out A",       "MIDI CC lane A" },
        { 32, 'b', "bxx", "MIDI Out B",       "MIDI CC lane B" },
        { 32, 'c', "cxx", "MIDI Out C",       "MIDI CC lane C" },
        { 32, 'd', "dxx", "MIDI Out D",       "MIDI CC lane D" },
        { 32, 'e', "exx", "MIDI Out E",       "MIDI CC lane E" },
        { 32, 'f', "fxx", "MIDI Out F",       "MIDI CC lane F" },
        { 33, 'F', "Fxy", "Slide Up",         "x semitones in y steps" },
        { 34, 'J', "Jxy", "Slide Down",       "x semitones in y steps" },
        { 35, '0', "0xxx", "MIDI Chord",      "per-step MIDI chord intervals" },
        { 36, 'X', "Xxy", "Plugin Mod",       "VC: trigger plugin step envelopes" },
    };
    return commands;
}

inline int fxLetterToCommand (char letter)
{
    switch (letter)
    {
        case '!': return 1;
        case 'V': return 2;
        case 'P': return 3;
        case 'M': return 4;
        case 'G': return 5;
        case 'T': return 6;
        case 'I': return 7;
        case 'm': return 8;
        case 'q': return 9;
        case 'C': return 10;
        case 'R': return 11;
        case 'A': return 12;
        case 'n': return 13;
        case 'i': return 14;
        case 'x': return 15;
        case 'S': return 37;
        case 'v': return 16;
        case 'r': return 17;
        case 'p': return 18;
        case 'g': return 19;
        case 'h': return 20;
        case 'j': return 21;
        case 'k': return 22;
        case 'l': return 23;
        case 'D': return 24;
        case 'L': return 25;
        case 'B': return 26;
        case 'H': return 27;
        case 's': return 28;
        case 't': return 29;
        case 'E': return 30;
        case 'U': return 31;
        case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': return 32;
        case 'F': return 33;
        case 'J': return 34;
        case '0': return 35;
        case 'X': return 36;
        default: return 0;
    }
}

inline char migrateLegacyFxCommandLetter (char letter)
{
    switch (letter)
    {
        case 'B': return 'r'; // legacy Direction
        case 'P': return 'p'; // legacy Position
        case 'T': return 'U'; // legacy Tune
        case 'Y': return 's'; // legacy Delay Send
        case 'R': return 't'; // legacy Reverb Send
        case 'S': return 'F'; // legacy Slide Up
        case 'D': return 'J'; // legacy Slide Down
        case 'F': return 'T'; // legacy Tempo
        case 'L': return 'S'; // legacy Slice
        case 'N': return 'm'; // legacy Note Nudge
        case 'M': return 'X'; // legacy Plugin Mod
        case 'Q': return 'R'; // legacy Retrigger
        case 'f': return 'x'; // VC v11 Random FX, now Polyend x to free MIDI Out F
        default: return letter;
    }
}

inline char migrateFxCommandLetterForProjectVersion (char letter, int projectVersion)
{
    if (projectVersion <= 10)
        return migrateLegacyFxCommandLetter (letter);

    if (projectVersion <= 11 && letter == 'f')
        return 'x';

    return letter;
}

inline int migrateFxParamForProjectVersion (char migratedLetter, int param, int projectVersion)
{
    if (migratedLetter == 'S' && projectVersion <= 11)
        return juce::jlimit (0, 255, param + 1);

    return param;
}

inline int getMaxFxParamForCommandLetter (char letter)
{
    switch (letter)
    {
        case '0': return 0x0FFF;
        case 'T': return 400;
        case 'P':
        case 'q':
        case 'C':
        case 'n':
        case 'v':
            return 100;
        case 'M': return 198;
        case 'g':
        case 'h':
        case 'j':
        case 'k':
        case 'l':
            return 128;
        case 'a':
        case 'b':
        case 'c':
        case 'd':
        case 'e':
        case 'f':
            return 127;
        default: return 255;
    }
}

inline int clampFxParamForCommandLetter (char letter, int param)
{
    return juce::jlimit (0, getMaxFxParamForCommandLetter (letter), param);
}

inline int decodeTempoPercentFxParam (int param)
{
    if (param <= 0)
        return 0;

    return juce::jlimit (10, 400, param);
}

struct NoteSlot
{
    int note = -1;
    int instrument = -1;
    int volume = -1;

    bool isEmpty() const { return note < 0 && instrument < 0 && volume < 0; }
    bool hasNote() const { return note >= 0; }
    void clear() { note = -1; instrument = -1; volume = -1; }
};

struct FxSlot
{
    int fx = 0;
    int fxParam = 0;
    char fxCommand = '\0';

    bool isSymbolic() const { return fxCommand != '\0'; }
    bool isEmpty() const { return fxCommand == '\0' && fxParam == 0; }
    int getCommand() const
    {
        return fxCommand != '\0' ? fxLetterToCommand (fxCommand) : 0;
    }

    char getCommandLetter() const
    {
        return fxCommand;
    }

    void setSymbolicCommand (char letter, int param)
    {
        const auto command = fxLetterToCommand (letter);
        if (command <= 0)
        {
            clear();
            return;
        }

        fxCommand = letter;
        fx = command;
        fxParam = clampFxParamForCommandLetter (letter, param);
    }

    void setParam (int param)
    {
        fxParam = clampFxParamForCommandLetter (fxCommand, param);
    }

    void clear()
    {
        fx = 0;
        fxParam = 0;
        fxCommand = '\0';
    }
};

inline juce::String formatSignedFxDisplay (char letter, int value)
{
    return value < 0 ? juce::String::formatted ("%c%d", letter, value)
                     : juce::String::formatted ("%c %d", letter, value);
}

inline juce::String formatUnsignedFxDisplay (char letter, int value)
{
    return juce::String::formatted ("%c%d", letter, value);
}

inline juce::String formatFxSlotForDisplay (const FxSlot& slot)
{
    if (slot.isEmpty())
        return "...";

    const auto letter = slot.getCommandLetter();
    if (letter == '\0')
        return "...";

    switch (letter)
    {
        case '!':
            return "!OFF";
        case 'P':
            return formatSignedFxDisplay (letter, juce::jlimit (0, 100, slot.fxParam) - 50);
        case 'M':
            return formatSignedFxDisplay (letter, juce::jlimit (0, 198, slot.fxParam) - 99);
        case 'm':
            return formatSignedFxDisplay (letter, static_cast<int> (static_cast<std::int8_t> (slot.fxParam & 0xFF)));
        case 'T':
            return slot.fxParam <= 0 ? "TSTP" : formatUnsignedFxDisplay (letter, decodeTempoPercentFxParam (slot.fxParam));
        case 'I':
        case 'q':
        case 'C':
        case 'n':
        case 'v':
        case 'G':
        case 'S':
        case 'g':
        case 'h':
        case 'j':
        case 'k':
        case 'l':
        case 'a':
        case 'b':
        case 'c':
        case 'd':
        case 'e':
        case 'f':
            return formatUnsignedFxDisplay (letter, slot.fxParam);
        case '0':
            return juce::String::formatted ("%c%03X", letter, slot.fxParam & 0x0FFF);
        default:
            return juce::String::formatted ("%c%02X", letter, slot.fxParam & 0xFF);
    }
}

struct Cell
{
    int note = -1;
    int instrument = -1;
    int volume = -1;
    std::vector<NoteSlot> extraNoteLanes;
    std::vector<FxSlot> fxSlots;

    Cell() { fxSlots.push_back ({}); }

    NoteSlot getNoteLane (int laneIndex) const
    {
        if (laneIndex == 0)
            return { note, instrument, volume };

        const auto idx = laneIndex - 1;
        if (idx < 0 || idx >= static_cast<int> (extraNoteLanes.size()))
            return {};

        return extraNoteLanes[static_cast<size_t> (idx)];
    }

    void setNoteLane (int laneIndex, const NoteSlot& slot)
    {
        if (laneIndex == 0)
        {
            note = slot.note;
            instrument = slot.instrument;
            volume = slot.volume;
            return;
        }

        const auto idx = laneIndex - 1;
        while (static_cast<int> (extraNoteLanes.size()) <= idx)
            extraNoteLanes.push_back ({});

        extraNoteLanes[static_cast<size_t> (idx)] = slot;
    }

    int getNumNoteLanes() const { return 1 + static_cast<int> (extraNoteLanes.size()); }

    void ensureNoteLanes (int count)
    {
        if (count > 1)
            while (static_cast<int> (extraNoteLanes.size()) < count - 1)
                extraNoteLanes.push_back ({});
    }

    FxSlot& getFxSlot (int index)
    {
        while (static_cast<int> (fxSlots.size()) <= index)
            fxSlots.push_back ({});

        return fxSlots[static_cast<size_t> (index)];
    }

    const FxSlot& getFxSlot (int index) const
    {
        static const FxSlot emptySlot {};
        if (index < 0 || index >= static_cast<int> (fxSlots.size()))
            return emptySlot;

        return fxSlots[static_cast<size_t> (index)];
    }

    int getNumFxSlots() const { return static_cast<int> (fxSlots.size()); }

    void ensureFxSlots (int count)
    {
        while (static_cast<int> (fxSlots.size()) < count)
            fxSlots.push_back ({});
    }

    bool isEmpty() const
    {
        if (note >= 0 || instrument >= 0 || volume >= 0)
            return false;

        for (const auto& lane : extraNoteLanes)
            if (! lane.isEmpty())
                return false;

        for (const auto& slot : fxSlots)
            if (! slot.isEmpty())
                return false;

        return true;
    }

    bool hasNote() const { return note >= 0; }

    void clear()
    {
        note = -1;
        instrument = -1;
        volume = -1;
        extraNoteLanes.clear();
        fxSlots.clear();
        fxSlots.push_back ({});
    }
};

inline void setFxSlotFromSerializedCommand (FxSlot& slot, char letter, int param, int projectVersion)
{
    const auto migratedLetter = migrateFxCommandLetterForProjectVersion (letter, projectVersion);
    slot.setSymbolicCommand (migratedLetter,
                             migrateFxParamForProjectVersion (migratedLetter, param, projectVersion));
}
