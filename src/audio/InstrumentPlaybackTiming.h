#pragma once

#include <algorithm>

namespace InstrumentPlaybackTiming
{
    inline double getKillModeEndBeat (double startBeat, double rowLengthBeats, double segmentEndBeat)
    {
        return std::min (startBeat + rowLengthBeats, segmentEndBeat);
    }

    inline double chooseNoteEndBeat (bool killMode,
                                     double startBeat,
                                     double rowLengthBeats,
                                     double releaseModeEndBeat,
                                     double segmentEndBeat)
    {
        if (killMode)
            return getKillModeEndBeat (startBeat, rowLengthBeats, segmentEndBeat);

        return std::min (releaseModeEndBeat, segmentEndBeat);
    }

    inline bool shouldSendHardCutAtEnd (bool killMode, bool pluginInstrumentTrack)
    {
        return killMode && ! pluginInstrumentTrack;
    }

    inline double getHardCutEventTime (double noteEndSeconds)
    {
        return std::max (0.0, noteEndSeconds - 0.00002);
    }
}
