#pragma once

struct InstrumentParams
{
    double startPos     = 0.0;    // 0.0-1.0 normalized
    double endPos       = 1.0;    // 0.0-1.0 normalized
    double attackMs     = 5.0;    // milliseconds
    double decayMs      = 50.0;   // milliseconds
    double sustainLevel = 1.0;    // 0.0-1.0
    double releaseMs    = 50.0;   // milliseconds
    bool   reversed     = false;

    bool isDefault() const
    {
        return startPos == 0.0
            && endPos == 1.0
            && attackMs == 5.0
            && decayMs == 50.0
            && sustainLevel == 1.0
            && releaseMs == 50.0
            && ! reversed;
    }
};
