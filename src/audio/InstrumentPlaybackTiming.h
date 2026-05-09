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

    inline int clampPercentValue (int value)
    {
        return std::clamp (value, 0, 100);
    }

    inline double applyGateLengthSeconds (double noteStartSeconds,
                                          double rowEndSeconds,
                                          double noteEndSeconds,
                                          double segmentEndSeconds,
                                          int gatePercent)
    {
        gatePercent = clampPercentValue (gatePercent);

        const double availableRowSeconds = std::max (0.001, rowEndSeconds - noteStartSeconds);
        const double gateDurationSeconds = std::max (0.001,
                                                     availableRowSeconds * static_cast<double> (gatePercent) / 100.0);
        const double gatedEnd = std::min (noteEndSeconds, noteStartSeconds + gateDurationSeconds);
        return ensureNoteEndAfterStartSeconds (noteStartSeconds, gatedEnd, segmentEndSeconds);
    }

    inline int clampSwingPercent (int value)
    {
        return std::clamp (value, 25, 75);
    }

    inline double getSwingOffsetSeconds (int rowIndex, double rowDurationSeconds, int swingPercent)
    {
        if ((rowIndex & 1) == 0)
            return 0.0;

        const int swing = clampSwingPercent (swingPercent);
        const double safeRowDuration = std::max (0.0, rowDurationSeconds);
        return (static_cast<double> (swing - 50) / 50.0) * safeRowDuration * 0.5;
    }

    inline double applySwingOffsetSeconds (double baseTimeSeconds,
                                           int rowIndex,
                                           double rowDurationSeconds,
                                           int swingPercent)
    {
        return std::max (0.0, baseTimeSeconds + getSwingOffsetSeconds (rowIndex, rowDurationSeconds, swingPercent));
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
