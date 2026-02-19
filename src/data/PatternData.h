#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>

constexpr int kNumTracks = 16;

//==============================================================================
// FX command info for the dropdown reference
//==============================================================================

struct FxCommandInfo
{
    int command;            // 0x0 - 0xF
    juce::String format;   // "0xy", "8xx", etc.
    juce::String name;
    juce::String description;
};

inline const std::vector<FxCommandInfo>& getFxCommandList()
{
    static const std::vector<FxCommandInfo> commands = {
        { 0x0, "0xy", "Arpeggio",         "x=semi1, y=semi2" },
        { 0x1, "1xx", "Slide Up",         "xx=speed" },
        { 0x2, "2xx", "Slide Down",       "xx=speed" },
        { 0x3, "3xx", "Tone Portamento",  "xx=speed" },
        { 0x4, "4xy", "Vibrato",          "x=speed, y=depth" },
        { 0x5, "5xy", "Vol Slide+Porta",  "x=up, y=down" },
        { 0x6, "6xy", "Vol Slide+Vibrato","x=up, y=down" },
        { 0x7, "7xy", "Tremolo",          "x=speed, y=depth" },
        { 0x8, "8xx", "Set Panning",      "00=L, 80=C, FF=R" },
        { 0x9, "9xx", "Sample Offset",    "xx=offset" },
        { 0xA, "Axy", "Volume Slide",     "x=up, y=down" },
        { 0xB, "Bxx", "Position Jump (NYI)", "xx=position" },
        { 0xC, "Cxx", "Set Volume",       "xx=volume (00-7F)" },
        { 0xD, "Dxx", "Pattern Break (NYI)", "xx=row" },
        { 0xE, "Exy", "Mod Mode",         "x=dest, y=mode" },
        { 0xF, "Fxx", "Set Speed/Tempo",  "01-1F=speed, 20+=BPM" },
    };
    return commands;
}

//==============================================================================
// FX slot and Cell structures
//==============================================================================

struct FxSlot
{
    int fx = 0;           // Effect command (0 = none)
    int fxParam = 0;      // Effect parameter

    bool isEmpty() const { return fx == 0 && fxParam == 0; }
    void clear() { fx = 0; fxParam = 0; }
};

struct Cell
{
    int note = -1;        // MIDI note (-1 = empty, 0-127 = note)
    int instrument = -1;  // Instrument/sample index (-1 = none)
    int volume = -1;      // Volume (-1 = default, 0-127)
    std::vector<FxSlot> fxSlots; // At least 1 slot always present

    Cell() { fxSlots.push_back ({}); }

    // Backward compatibility: access first FX slot
    int getFx() const { return fxSlots.empty() ? 0 : fxSlots[0].fx; }
    int getFxParam() const { return fxSlots.empty() ? 0 : fxSlots[0].fxParam; }
    void setFx (int fx, int param)
    {
        if (fxSlots.empty()) fxSlots.push_back ({});
        fxSlots[0].fx = fx;
        fxSlots[0].fxParam = param;
    }

    // Access a specific FX lane
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
        for (const auto& slot : fxSlots)
            if (! slot.isEmpty())
                return false;
        return true;
    }

    bool hasNote() const { return note >= 0; }

    void clear()
    {
        note = -1; instrument = -1; volume = -1;
        fxSlots.clear();
        fxSlots.push_back ({}); // Keep at least one slot
    }
};

struct Pattern
{
    int numRows = 64;
    std::vector<std::array<Cell, kNumTracks>> rows;
    juce::String name;

    Pattern();
    explicit Pattern (int rowCount);

    Cell& getCell (int row, int track);
    const Cell& getCell (int row, int track) const;
    void setCell (int row, int track, const Cell& cell);
    void clear();
    void resize (int newNumRows);
};

class PatternData
{
public:
    PatternData();

    Pattern& getCurrentPattern();
    const Pattern& getCurrentPattern() const;

    Pattern& getPattern (int index);
    const Pattern& getPattern (int index) const;

    int getCurrentPatternIndex() const { return currentPattern; }
    void setCurrentPattern (int index);

    int getNumPatterns() const { return static_cast<int> (patterns.size()); }
    void addPattern();
    void addPattern (int numRows);
    void duplicatePattern (int index);
    void removePattern (int index);
    void clearAllPatterns();

    Cell& getCell (int row, int track);
    const Cell& getCell (int row, int track) const;
    void setCell (int row, int track, const Cell& cell);

private:
    std::vector<Pattern> patterns;
    int currentPattern = 0;
};
