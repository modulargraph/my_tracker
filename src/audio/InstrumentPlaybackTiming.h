#pragma once

#include <algorithm>
#include <cstdint>

namespace InstrumentPlaybackTiming
{
    inline int decodeSignedTimingOffsetMs (int byteValue)
    {
        return static_cast<int> (static_cast<std::int8_t> (byteValue & 0xFF));
    }

    inline double applyTimingOffsetSeconds (double baseTimeSeconds, int offsetMs)
    {
        return std::max (0.0, baseTimeSeconds + static_cast<double> (offsetMs) * 0.001);
    }

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

    inline double chooseNoteEndSeconds (bool killMode,
                                        double rowEndSeconds,
                                        double nextTriggerSeconds,
                                        double segmentEndSeconds)
    {
        const double segmentClampedRowEnd = std::min (rowEndSeconds, segmentEndSeconds);

        if (killMode)
        {
            if (nextTriggerSeconds >= 0.0)
                return std::min (segmentClampedRowEnd, nextTriggerSeconds);

            return segmentClampedRowEnd;
        }

        if (nextTriggerSeconds >= 0.0)
            return std::min (nextTriggerSeconds, segmentEndSeconds);

        return segmentEndSeconds;
    }

    inline double ensureNoteEndAfterStartSeconds (double noteStartSeconds,
                                                  double noteEndSeconds,
                                                  double segmentEndSeconds)
    {
        if (noteEndSeconds > noteStartSeconds)
            return noteEndSeconds;

        return std::min (segmentEndSeconds, noteStartSeconds + 0.001);
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
