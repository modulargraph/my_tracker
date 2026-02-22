#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include "PatternData.h"

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

    // Extract tempo ('F') command value from a pattern row's master lane.
    // Returns the BPM value (clamped 20..300) or -1 if no tempo command is present.
    int getRowTempoCommand (const Pattern& pattern, int row);

    // Convert a single FX slot into MIDI CC messages appended to a sequence.
    void appendSymbolicTrackFx (juce::MidiMessageSequence& midiSeq,
                                const FxSlot& slot, double ccTime);

    // Find an insert plugin on a track by slot index (plugins between ChannelStrip
    // and TrackOutput, counting only ExternalPlugin instances).
    // Returns nullptr if not found or out of range.
    te::Plugin* findInsertPluginForSlot (te::AudioTrack& track, int slotIndex);
}
