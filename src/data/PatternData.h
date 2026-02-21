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
    int command;            // 1-9
    char letter;            // B, P, T, G, Y, R, S, D, F
    juce::String format;    // "Bxx", "Pxx", etc.
    juce::String name;
    juce::String description;
};

inline const std::vector<FxCommandInfo>& getFxCommandList()
{
    static const std::vector<FxCommandInfo> commands = {
        { 1, 'B', "Bxx", "Direction",       "00=backward, 01=forward" },
        { 2, 'P', "Pxx", "Position",        "00-FF -> 0.0-1.0 of region" },
        { 3, 'T', "Txx", "Tune",            "signed semitones (two's complement)" },
        { 4, 'G', "Gxx", "Portamento",      "speed in steps (00=memory)" },
        { 5, 'Y', "Yxx", "Delay Send",      "00-FF send level" },
        { 6, 'R', "Rxx", "Reverb Send",     "00-FF send level" },
        { 7, 'S', "Sxy", "Slide Up",        "x semitones in y steps" },
        { 8, 'D', "Dxy", "Slide Down",      "x semitones in y steps" },
        { 9, 'F', "Fxx", "Tempo",           "BPM (master lane only)" },
    };
    return commands;
}

// Convert FX command letter to command number (0 = invalid)
inline int fxLetterToCommand (char letter)
{
    switch (letter)
    {
        case 'B': case 'b': return 1;
        case 'P': case 'p': return 2;
        case 'T': case 't': return 3;
        case 'G': case 'g': return 4;
        case 'Y': case 'y': return 5;
        case 'R': case 'r': return 6;
        case 'S': case 's': return 7;
        case 'D': case 'd': return 8;
        case 'F': case 'f': return 9;
        default: return 0;
    }
}

//==============================================================================
// FX slot and Cell structures
//==============================================================================

struct FxSlot
{
    int fx = 0;              // Numeric id derived from fxCommand (1..9)
    int fxParam = 0;         // Effect parameter byte
    char fxCommand = '\0';   // Symbolic command token ('\0' = empty)

    bool isSymbolic() const { return fxCommand != '\0'; }
    bool isEmpty() const { return fxCommand == '\0' && fxParam == 0; }
    int getCommand() const
    {
        return fxCommand != '\0' ? fxLetterToCommand (fxCommand) : 0;
    }
    char getCommandLetter() const
    {
        return fxCommand != '\0'
                   ? static_cast<char> (juce::CharacterFunctions::toUpperCase (fxCommand))
                   : '\0';
    }
    void setSymbolicCommand (char letter, int param)
    {
        auto upper = static_cast<char> (juce::CharacterFunctions::toUpperCase (letter));
        const auto command = fxLetterToCommand (upper);
        if (command <= 0)
        {
            clear();
            return;
        }

        fxCommand = upper;
        fx = command;
        fxParam = juce::jlimit (0, 255, param);
    }
    void clear()
    {
        fx = 0;
        fxParam = 0;
        fxCommand = '\0';
    }
};

struct Cell
{
    int note = -1;        // MIDI note (-1 = empty, 0-127 = note)
    int instrument = -1;  // Instrument/sample index (-1 = none)
    int volume = -1;      // Volume (-1 = default, 0-127)
    std::vector<FxSlot> fxSlots; // At least 1 slot always present

    Cell() { fxSlots.push_back ({}); }

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

    // Master lane FX: masterFxRows[row][lane]
    std::vector<std::vector<FxSlot>> masterFxRows;

    Pattern();
    explicit Pattern (int rowCount);

    Cell& getCell (int row, int track);
    const Cell& getCell (int row, int track) const;
    void setCell (int row, int track, const Cell& cell);
    void clear();
    void resize (int newNumRows);

    // Master lane access
    FxSlot& getMasterFxSlot (int row, int lane);
    const FxSlot& getMasterFxSlot (int row, int lane) const;
    void ensureMasterFxSlots (int laneCount);
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
