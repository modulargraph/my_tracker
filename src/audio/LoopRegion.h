#pragma once

#include "InstrumentParams.h"

/**
 * Helper for computing loop region boundaries from InstrumentParams.
 * Eliminates the 3x duplicate loop setup in TrackerSamplerPlugin.
 */
struct LoopRegion
{
    double loopStart;
    double loopEnd;
    double loopLen;

    static LoopRegion fromParams (const InstrumentParams& params, double totalSamples)
    {
        double regionStart = params.startPos * totalSamples;
        double regionEnd = params.endPos * totalSamples;
        double regionLen = regionEnd - regionStart;

        double ls = regionStart + params.loopStart * regionLen;
        double le = regionStart + params.loopEnd * regionLen;
        if (le <= ls) le = ls + 1.0;

        return { ls, le, le - ls };
    }

    double wrapPosition (double pos) const
    {
        double wrapped = std::fmod (pos - loopStart, loopLen);
        if (wrapped < 0.0)
            wrapped += loopLen;
        return loopStart + wrapped;
    }
};
