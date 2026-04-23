#pragma once

#include "InstrumentParams.h"
#include "SamplePlaybackLayout.h"

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
        double ls = SamplePlaybackLayout::getLoopStartNorm (params) * totalSamples;
        double le = SamplePlaybackLayout::getLoopEndNorm (params) * totalSamples;
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
