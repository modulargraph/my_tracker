#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include "InstrumentParams.h"

struct FxSlot;
struct Pattern;

namespace te = tracktion;

// Helpers for converting pattern FX data into MIDI CC messages
// and for locating insert-chain plugins on audio tracks.
namespace PatternMidiBuilder
{
    // CC numbers for pattern FX commands
    static constexpr int kCcFxTune        = 31;
    static constexpr int kCcFxPortaSteps  = 32;
    static constexpr int kCcFxSlideUp     = 33;
    static constexpr int kCcFxSlideDown   = 34;
    static constexpr int kCcFxDelaySend   = 35;
    static constexpr int kCcFxReverbSend  = 36;
    static constexpr int kCcSamplerDirection = 37;
    static constexpr int kCcSamplerPosition  = 38;
    static constexpr int kCcFxNoteReset   = 39;
    static constexpr int kCcFxVolume      = 40;
    static constexpr int kCcSamplerSlice  = 41;
    static constexpr int kCcSamplerRetrigger = 42;
    static constexpr int kCcFxMicroTune   = 43;
    static constexpr int kCcFxOverdrive   = 44;
    static constexpr int kCcFxLowPass     = 45;
    static constexpr int kCcFxBandPass    = 46;
    static constexpr int kCcFxHighPass    = 47;
    static constexpr int kCcFxBitDepth    = 48;
    static constexpr int kCcFxVolumeLfoRate = 49;
    static constexpr int kCcFxPanLfoRate = 50;
    static constexpr int kCcFxFilterLfoRate = 51;
    static constexpr int kCcFxPositionLfoRate = 52;
    static constexpr int kCcFxFinetuneLfoRate = 53;
    static constexpr int kCcMidiOutA      = 20;
    static constexpr int kCcMidiOutB      = 21;
    static constexpr int kCcMidiOutC      = 22;
    static constexpr int kCcMidiOutD      = 23;
    static constexpr int kCcMidiOutE      = 24;
    static constexpr int kCcMidiOutF      = 25;

    // Polyend exposes six per-instrument MIDI Out lanes named a-f.
    // Defaults remain fixed CC20-25 for older projects.
    int getMidiOutCcForCommandLetter (char commandLetter);
    int getMidiOutLaneIndexForCommandLetter (char commandLetter);

    // Extract Polyend tempo ('T') percent from a pattern row's master lane.
    // Returns 0 for TSTP, 10..400 for tempo percent, or -1 if no tempo command is present.
    int getRowTempoCommand (const Pattern& pattern, int row);

    // Convert a single FX slot into MIDI CC messages appended to a sequence.
    void appendSymbolicTrackFx (juce::MidiMessageSequence& midiSeq,
                                const FxSlot& slot,
                                double ccTime,
                                int midiOutChannel = 1,
                                const std::array<InstrumentParams::MidiOutAssignment,
                                                 InstrumentParams::kNumMidiOutLanes>* midiOutAssignments = nullptr);

    // Find an insert plugin on a track by slot index (plugins between ChannelStrip
    // and TrackOutput, counting only ExternalPlugin instances).
    // Returns nullptr if not found or out of range.
    te::Plugin* findInsertPluginForSlot (te::AudioTrack& track, int slotIndex);
}
