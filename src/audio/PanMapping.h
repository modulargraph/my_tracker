#pragma once

#include <algorithm>

namespace PanMapping
{

// Map tracker-style CC10 values (0-127) to UI pan range (-50..+50) with exact center at 64.
inline float cc10ToPan (int ccValue)
{
    const int clamped = std::clamp (ccValue, 0, 127);

    if (clamped == 64)
        return 0.0f;

    if (clamped < 64)
        return (static_cast<float> (clamped) / 64.0f) * 50.0f - 50.0f;

    return (static_cast<float> (clamped - 64) / 63.0f) * 50.0f;
}

} // namespace PanMapping
