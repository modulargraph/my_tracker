#pragma once

struct DelayParams
{
    double time = 250.0;        // ms (when bpmSync is false)
    int syncDivision = 4;       // quarter note = 4, eighth = 8, sixteenth = 16, etc.
    bool bpmSync = true;
    bool dotted = false;        // 1.5x multiplier on sync division
    double feedback = 40.0;     // 0-100%
    int filterType = 0;         // 0=off, 1=LP, 2=HP
    double filterCutoff = 80.0; // 0-100%
    double wet = 50.0;          // 0-100%
    double stereoWidth = 50.0;  // 0-100%
};

struct ReverbParams
{
    double roomSize = 50.0;     // 0-100%
    double decay = 50.0;        // 0-100%
    double damping = 50.0;      // 0-100%
    double preDelay = 10.0;     // ms (0-100)
    double wet = 30.0;          // 0-100%
};
