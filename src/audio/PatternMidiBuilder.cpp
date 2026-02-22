#include "PatternMidiBuilder.h"
#include "ChannelStripPlugin.h"
#include "TrackOutputPlugin.h"
#include "FxParamTransport.h"

namespace PatternMidiBuilder
{

int getRowTempoCommand (const Pattern& pattern, int row)
{
    if (row < 0 || row >= pattern.numRows)
        return -1;

    int bpm = -1;
    int laneCount = row < static_cast<int> (pattern.masterFxRows.size())
                        ? static_cast<int> (pattern.masterFxRows[static_cast<size_t> (row)].size())
                        : 0;

    for (int lane = 0; lane < laneCount; ++lane)
    {
        const auto& slot = pattern.getMasterFxSlot (row, lane);
        if (slot.getCommandLetter() == 'F')
            bpm = juce::jlimit (20, 300, slot.fxParam);
    }

    return bpm;
}

void appendSymbolicTrackFx (juce::MidiMessageSequence& midiSeq, const FxSlot& slot, double ccTime)
{
    switch (slot.getCommandLetter())
    {
        case 'B':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcSamplerDirection, slot.fxParam, ccTime);
            break;
        case 'P':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcSamplerPosition, slot.fxParam, ccTime);
            break;
        case 'T':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxTune, slot.fxParam, ccTime);
            break;
        case 'G':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxPortaSteps, slot.fxParam, ccTime);
            break;
        case 'Y':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxDelaySend, slot.fxParam, ccTime);
            break;
        case 'R':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxReverbSend, slot.fxParam, ccTime);
            break;
        case 'S':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxSlideUp, slot.fxParam, ccTime);
            break;
        case 'D':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxSlideDown, slot.fxParam, ccTime);
            break;
        case 'V':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxVolume, slot.fxParam, ccTime);
            break;
        case 'F':
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
