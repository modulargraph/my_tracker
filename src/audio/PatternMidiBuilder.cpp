#include "PatternMidiBuilder.h"
#include "Pattern.h"
#include "ChannelStripPlugin.h"
#include "TrackOutputPlugin.h"
#include "FxParamTransport.h"
#include <cmath>

namespace PatternMidiBuilder
{

namespace
{
void appendMidiOutAssignment (juce::MidiMessageSequence& midiSeq,
                              int laneIndex,
                              int value,
                              double eventTime,
                              int channel,
                              const std::array<InstrumentParams::MidiOutAssignment,
                                               InstrumentParams::kNumMidiOutLanes>* assignments)
{
    if (laneIndex < 0 || laneIndex >= InstrumentParams::kNumMidiOutLanes)
        return;

    const auto assignment = assignments != nullptr
                                ? (*assignments)[static_cast<size_t> (laneIndex)]
                                : InstrumentParams::makeDefaultMidiOutAssignment (laneIndex);
    const int clampedValue = juce::jlimit (0, 127, value);
    const int number = juce::jlimit (0, 127, assignment.number);

    switch (assignment.type)
    {
        case InstrumentParams::MidiOutMessageType::ControlChange:
            midiSeq.addEvent (juce::MidiMessage::controllerEvent (channel, number, clampedValue), eventTime);
            break;
        case InstrumentParams::MidiOutMessageType::ProgramChange:
            midiSeq.addEvent (juce::MidiMessage::programChange (channel, clampedValue), eventTime);
            break;
        case InstrumentParams::MidiOutMessageType::ChannelPressure:
            midiSeq.addEvent (juce::MidiMessage::channelPressureChange (channel, clampedValue), eventTime);
            break;
        case InstrumentParams::MidiOutMessageType::PolyPressure:
            midiSeq.addEvent (juce::MidiMessage::aftertouchChange (channel, number, clampedValue), eventTime);
            break;
    }
}
} // namespace

int getRowTempoCommand (const Pattern& pattern, int row)
{
    if (row < 0 || row >= pattern.numRows)
        return -1;

    int tempoPercent = -1;
    int laneCount = row < static_cast<int> (pattern.masterFxRows.size())
                        ? static_cast<int> (pattern.masterFxRows[static_cast<size_t> (row)].size())
                        : 0;

    for (int lane = 0; lane < laneCount; ++lane)
    {
        const auto& slot = pattern.getMasterFxSlot (row, lane);
        if (slot.getCommandLetter() == 'T')
            tempoPercent = decodeTempoPercentFxParam (slot.fxParam);
    }

    return tempoPercent;
}

int getMidiOutCcForCommandLetter (char commandLetter)
{
    const int laneIndex = getMidiOutLaneIndexForCommandLetter (commandLetter);
    return laneIndex >= 0 ? InstrumentParams::getDefaultMidiOutNumber (laneIndex) : -1;
}

int getMidiOutLaneIndexForCommandLetter (char commandLetter)
{
    return commandLetter >= 'a' && commandLetter <= 'f' ? commandLetter - 'a' : -1;
}

void appendSymbolicTrackFx (juce::MidiMessageSequence& midiSeq,
                            const FxSlot& slot,
                            double ccTime,
                            int midiOutChannel,
                            const std::array<InstrumentParams::MidiOutAssignment,
                                             InstrumentParams::kNumMidiOutLanes>* midiOutAssignments)
{
    midiOutChannel = juce::jlimit (1, 16, midiOutChannel);

    switch (slot.getCommandLetter())
    {
        case '!':
            midiSeq.addEvent (juce::MidiMessage::controllerEvent (1, kCcFxNoteReset, 0), ccTime);
            break;
        case 'r':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcSamplerDirection, slot.fxParam, ccTime);
            break;
        case 'p':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcSamplerPosition, slot.fxParam, ccTime);
            break;
        case 'U':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxTune, slot.fxParam, ccTime);
            break;
        case 'M':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxMicroTune, slot.fxParam, ccTime);
            break;
        case 'G':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxPortaSteps, slot.fxParam, ccTime);
            break;
        case 'P':
            midiSeq.addEvent (juce::MidiMessage::controllerEvent (
                                  1, 10, juce::jlimit (0, 127, static_cast<int> (std::lround (slot.fxParam * 127.0 / 100.0)))),
                              ccTime);
            break;
        case 's':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxDelaySend, slot.fxParam, ccTime);
            break;
        case 't':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxReverbSend, slot.fxParam, ccTime);
            break;
        case 'F':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxSlideUp, slot.fxParam, ccTime);
            break;
        case 'J':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxSlideDown, slot.fxParam, ccTime);
            break;
        case 'V':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxVolume, slot.fxParam, ccTime);
            break;
        case 'S':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcSamplerSlice, slot.fxParam, ccTime);
            break;
        case 'R':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcSamplerRetrigger, slot.fxParam, ccTime);
            break;
        case 'D':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxOverdrive, slot.fxParam, ccTime);
            break;
        case 'L':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxLowPass, slot.fxParam, ccTime);
            break;
        case 'B':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxBandPass, slot.fxParam, ccTime);
            break;
        case 'H':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxHighPass, slot.fxParam, ccTime);
            break;
        case 'E':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxBitDepth, slot.fxParam, ccTime);
            break;
        case 'g':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxVolumeLfoRate, slot.fxParam, ccTime);
            break;
        case 'h':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxPanLfoRate, slot.fxParam, ccTime);
            break;
        case 'j':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxFilterLfoRate, slot.fxParam, ccTime);
            break;
        case 'k':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxPositionLfoRate, slot.fxParam, ccTime);
            break;
        case 'l':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxFinetuneLfoRate, slot.fxParam, ccTime);
            break;
        case 'a':
        case 'b':
        case 'c':
        case 'd':
        case 'e':
        case 'f':
            appendMidiOutAssignment (midiSeq,
                                     getMidiOutLaneIndexForCommandLetter (slot.getCommandLetter()),
                                     slot.fxParam,
                                     ccTime,
                                     midiOutChannel,
                                     midiOutAssignments);
            break;
        case 'm':
            // Note timing is handled by the pattern scheduler, not by MIDI CC state.
            break;
        case 'x':
            // Random FX value is resolved before FX are emitted.
            break;
        case 'X':
            // Plugin-instrument modulation triggers are handled from the UI row clock.
            break;
        case 'T':
            // Tempo is handled via master lane tempo points.
            break;
        default:
            break;
    }
}

te::Plugin* findInsertPluginForSlot (te::AudioTrack& track, int slotIndex)
{
    if (slotIndex < 0)
        return nullptr;

    bool inInsertZone = false;
    int insertIdx = 0;

    auto& pluginList = track.pluginList;
    for (int i = 0; i < pluginList.size(); ++i)
    {
        auto* plugin = pluginList[i];
        if (dynamic_cast<ChannelStripPlugin*> (plugin) != nullptr)
        {
            inInsertZone = true;
            continue;
        }

        if (! inInsertZone)
            continue;

        if (dynamic_cast<TrackOutputPlugin*> (plugin) != nullptr)
            break;

        if (dynamic_cast<te::ExternalPlugin*> (plugin) == nullptr)
            continue;

        if (insertIdx == slotIndex)
            return plugin;

        ++insertIdx;
    }

    return nullptr;
}

} // namespace PatternMidiBuilder
