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

    inline double chooseNoteEndBeat (bool /*killMode*/,
                                     double /*startBeat*/,
                                     double /*rowLengthBeats*/,
                                     double releaseModeEndBeat,
                                     double segmentEndBeat)
    {
        // Tracker kill/release does not change note duration. Notes sustain
        // until the next trigger or segment end; kill mode only changes how
        // sample tracks are cut at that handoff.
        return std::min (releaseModeEndBeat, segmentEndBeat);
    }

    inline double chooseNoteEndSeconds (bool /*killMode*/,
                                        double /*rowEndSeconds*/,
                                        double nextTriggerSeconds,
                                        double segmentEndSeconds)
    {
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

    inline bool shouldSendHardCutAtNoteHandoff (bool killMode,
                                                bool pluginInstrumentTrack,
                                                bool nextTriggerIsNormalNote)
    {
        return killMode && ! pluginInstrumentTrack && nextTriggerIsNormalNote;
    }

    inline double getHandoffEventTime (double noteEndSeconds)
    {
        return std::max (0.0, noteEndSeconds - 0.00002);
    }

    inline double getHardCutEventTime (double noteEndSeconds)
    {
        return getHandoffEventTime (noteEndSeconds);
    }
}
