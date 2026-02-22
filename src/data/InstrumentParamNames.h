#pragma once

#include <JuceHeader.h>
#include "InstrumentParams.h"

/**
 * Enum-to-string conversion functions for InstrumentParams types.
 * Extracted from SampleEditorComponent for reuse.
 */
namespace InstrumentParamNames
{

inline juce::String getPlayModeName (InstrumentParams::PlayMode mode)
{
    switch (mode)
    {
        case InstrumentParams::PlayMode::OneShot:        return "1-Shot";
        case InstrumentParams::PlayMode::ForwardLoop:    return "Forward loop";
        case InstrumentParams::PlayMode::BackwardLoop:   return "Backward loop";
        case InstrumentParams::PlayMode::PingpongLoop:   return "Pingpong loop";
        case InstrumentParams::PlayMode::Slice:          return "Slice";
        case InstrumentParams::PlayMode::BeatSlice:      return "Beat Slice";
        case InstrumentParams::PlayMode::Granular:       return "Granular";
    }
    return "???";
}

inline juce::String getFilterTypeName (InstrumentParams::FilterType type)
{
    switch (type)
    {
        case InstrumentParams::FilterType::Disabled: return "Off";
        case InstrumentParams::FilterType::LowPass:  return "LowPass";
        case InstrumentParams::FilterType::HighPass:  return "HighPass";
        case InstrumentParams::FilterType::BandPass:  return "BandPass";
    }
    return "???";
}

inline juce::String getModTypeName (InstrumentParams::Modulation::Type type)
{
    switch (type)
    {
        case InstrumentParams::Modulation::Type::Off:       return "Off";
        case InstrumentParams::Modulation::Type::Envelope:  return "Envelope";
        case InstrumentParams::Modulation::Type::LFO:       return "LFO";
    }
    return "???";
}

inline juce::String getLfoShapeName (InstrumentParams::Modulation::LFOShape shape)
{
    switch (shape)
    {
        case InstrumentParams::Modulation::LFOShape::RevSaw:    return "Rev Saw";
        case InstrumentParams::Modulation::LFOShape::Saw:       return "Saw";
        case InstrumentParams::Modulation::LFOShape::Triangle:  return "Triangle";
        case InstrumentParams::Modulation::LFOShape::Square:    return "Square";
        case InstrumentParams::Modulation::LFOShape::Random:    return "Random";
    }
    return "???";
}

inline juce::String getModDestFullName (int dest)
{
    switch (static_cast<InstrumentParams::ModDest> (dest))
    {
        case InstrumentParams::ModDest::Volume:        return "Volume";
        case InstrumentParams::ModDest::Panning:       return "Panning";
        case InstrumentParams::ModDest::Cutoff:        return "Cutoff";
        case InstrumentParams::ModDest::GranularPos:   return "Granular Position";
        case InstrumentParams::ModDest::Finetune:      return "Finetune";
    }
    return "???";
}

inline juce::String getGranShapeName (InstrumentParams::GranShape shape)
{
    switch (shape)
    {
        case InstrumentParams::GranShape::Square:    return "Square";
        case InstrumentParams::GranShape::Triangle:  return "Triangle";
        case InstrumentParams::GranShape::Gauss:     return "Gauss";
    }
    return "???";
}

inline juce::String getGranLoopName (InstrumentParams::GranLoop loop)
{
    switch (loop)
    {
        case InstrumentParams::GranLoop::Forward:   return "Forward";
        case InstrumentParams::GranLoop::Reverse:   return "Reverse";
        case InstrumentParams::GranLoop::Pingpong:  return "Pingpong";
    }
    return "???";
}

inline juce::String formatLfoSpeed (int speed)
{
    if (speed == 1) return "1 step";
    return juce::String (speed) + " steps";
}

inline constexpr int kLfoSpeeds[] = {
    128, 96, 64, 48, 32, 24, 16, 12, 8, 6, 4, 3, 2, 1
};

inline constexpr int kNumLfoSpeeds = 14;

} // namespace InstrumentParamNames
