#include "MixerParamModel.h"
#include <JuceHeader.h>

namespace MixerParamModel
{

//==============================================================================
// EQ helpers (shared across Track, SendReturn, GroupBus, Master)
//==============================================================================

namespace
{

template <typename State>
double getEqParam (const State& s, int param)
{
    switch (param)
    {
        case 0:  return s.eqLowGain;
        case 1:  return s.eqMidGain;
        case 2:  return s.eqHighGain;
        case 3:  return s.eqMidFreq;
        default: return 0.0;
    }
}

template <typename State>
void setEqParam (State& s, int param, double value)
{
    switch (param)
    {
        case 0: s.eqLowGain  = juce::jlimit (-12.0, 12.0, value);   break;
        case 1: s.eqMidGain  = juce::jlimit (-12.0, 12.0, value);   break;
        case 2: s.eqHighGain = juce::jlimit (-12.0, 12.0, value);   break;
        case 3: s.eqMidFreq  = juce::jlimit (200.0, 8000.0, value); break;
        default: break;
    }
}

//==============================================================================
// Compressor helpers (shared across Track, GroupBus, Master)
//==============================================================================

template <typename State>
double getCompParam (const State& s, int param)
{
    switch (param)
    {
        case 0:  return s.compThreshold;
        case 1:  return s.compRatio;
        case 2:  return s.compAttack;
        case 3:  return s.compRelease;
        default: return 0.0;
    }
}

template <typename State>
void setCompParam (State& s, int param, double value)
{
    switch (param)
    {
        case 0: s.compThreshold = juce::jlimit (-60.0, 0.0, value);    break;
        case 1: s.compRatio     = juce::jlimit (1.0, 20.0, value);     break;
        case 2: s.compAttack    = juce::jlimit (0.1, 100.0, value);    break;
        case 3: s.compRelease   = juce::jlimit (10.0, 1000.0, value);  break;
        default: break;
    }
}

} // anonymous namespace

//==============================================================================
// getParamValue
//==============================================================================
double getParamValue (const MixerState& state, StripType stripType, int stripIndex, Section section, int paramIndex)
{
    switch (stripType)
    {
        case StripType::Track:
        {
            auto& s = state.tracks[static_cast<size_t> (stripIndex)];
            switch (section)
            {
                case Section::EQ:      return getEqParam (s, paramIndex);
                case Section::Comp:    return getCompParam (s, paramIndex);
                case Section::Inserts: return 0.0;
                case Section::Sends:
                    switch (paramIndex)
                    {
                        case 0:  return s.reverbSend;
                        case 1:  return s.delaySend;
                        default: return 0.0;
                    }
                case Section::Pan:     return static_cast<double> (s.pan);
                case Section::Volume:  return s.volume;
                case Section::Limiter: return 0.0;
            }
            break;
        }
        case StripType::DelayReturn:
        case StripType::ReverbReturn:
        {
            auto& sr = state.sendReturns[static_cast<size_t> (stripIndex)];
            switch (section)
            {
                case Section::EQ:     return getEqParam (sr, paramIndex);
                case Section::Pan:    return static_cast<double> (sr.pan);
                case Section::Volume: return sr.volume;
                default:              return 0.0;
            }
            break;
        }
        case StripType::GroupBus:
        {
            auto& gb = state.groupBuses[static_cast<size_t> (stripIndex)];
            switch (section)
            {
                case Section::EQ:     return getEqParam (gb, paramIndex);
                case Section::Comp:   return getCompParam (gb, paramIndex);
                case Section::Pan:    return static_cast<double> (gb.pan);
                case Section::Volume: return gb.volume;
                default:              return 0.0;
            }
            break;
        }
        case StripType::Master:
        {
            auto& m = state.master;
            switch (section)
            {
                case Section::EQ:      return getEqParam (m, paramIndex);
                case Section::Comp:    return getCompParam (m, paramIndex);
                case Section::Limiter:
                    switch (paramIndex)
                    {
                        case 0:  return m.limiterThreshold;
                        case 1:  return m.limiterRelease;
                        default: return 0.0;
                    }
                case Section::Inserts: return 0.0;
                case Section::Pan:     return static_cast<double> (m.pan);
                case Section::Volume:  return m.volume;
                default:               return 0.0;
            }
            break;
        }
    }
    return 0.0;
}

//==============================================================================
// setParamValue
//==============================================================================
void setParamValue (MixerState& state, StripType stripType, int stripIndex, Section section, int paramIndex, double value)
{
    switch (stripType)
    {
        case StripType::Track:
        {
            auto& s = state.tracks[static_cast<size_t> (stripIndex)];
            switch (section)
            {
                case Section::EQ:      setEqParam (s, paramIndex, value); break;
                case Section::Comp:    setCompParam (s, paramIndex, value); break;
                case Section::Inserts: break;
                case Section::Sends:
                    switch (paramIndex)
                    {
                        case 0: s.reverbSend = juce::jlimit (-100.0, 0.0, value); break;
                        case 1: s.delaySend  = juce::jlimit (-100.0, 0.0, value); break;
                        default: break;
                    }
                    break;
                case Section::Pan:
                    s.pan = juce::jlimit (-50, 50, static_cast<int> (value));
                    break;
                case Section::Volume:
                    s.volume = juce::jlimit (-100.0, 12.0, value);
                    break;
                case Section::Limiter: break;
            }
            break;
        }
        case StripType::DelayReturn:
        case StripType::ReverbReturn:
        {
            auto& sr = state.sendReturns[static_cast<size_t> (stripIndex)];
            switch (section)
            {
                case Section::EQ:
                    setEqParam (sr, paramIndex, value);
                    break;
                case Section::Pan:
                    sr.pan = juce::jlimit (-50, 50, static_cast<int> (value));
                    break;
                case Section::Volume:
                    sr.volume = juce::jlimit (-100.0, 12.0, value);
                    break;
                default: break;
            }
            break;
        }
        case StripType::GroupBus:
        {
            auto& gb = state.groupBuses[static_cast<size_t> (stripIndex)];
            switch (section)
            {
                case Section::EQ:   setEqParam (gb, paramIndex, value);   break;
                case Section::Comp: setCompParam (gb, paramIndex, value); break;
                case Section::Pan:
                    gb.pan = juce::jlimit (-50, 50, static_cast<int> (value));
                    break;
                case Section::Volume:
                    gb.volume = juce::jlimit (-100.0, 12.0, value);
                    break;
                default: break;
            }
            break;
        }
        case StripType::Master:
        {
            auto& m = state.master;
            switch (section)
            {
                case Section::EQ:   setEqParam (m, paramIndex, value);   break;
                case Section::Comp: setCompParam (m, paramIndex, value); break;
                case Section::Limiter:
                    switch (paramIndex)
                    {
                        case 0: m.limiterThreshold = juce::jlimit (-24.0, 0.0, value);  break;
                        case 1: m.limiterRelease   = juce::jlimit (1.0, 500.0, value);  break;
                        default: break;
                    }
                    break;
                case Section::Inserts: break;
                case Section::Pan:
                    m.pan = juce::jlimit (-50, 50, static_cast<int> (value));
                    break;
                case Section::Volume:
                    m.volume = juce::jlimit (-100.0, 12.0, value);
                    break;
                default: break;
            }
            break;
        }
    }
}

//==============================================================================
// getParamMin
//==============================================================================
double getParamMin (Section section, int paramIndex)
{
    switch (section)
    {
        case Section::EQ:      return paramIndex == 3 ? 200.0 : -12.0;
        case Section::Comp:
            switch (paramIndex)
            {
                case 0:  return -60.0;
                case 1:  return 1.0;
                case 2:  return 0.1;
                case 3:  return 10.0;
                default: return 0.0;
            }
        case Section::Limiter:
            switch (paramIndex)
            {
                case 0:  return -24.0;
                case 1:  return 1.0;
                default: return 0.0;
            }
        case Section::Inserts: return 0.0;
        case Section::Sends:   return -100.0;
        case Section::Pan:     return -50.0;
        case Section::Volume:  return -100.0;
    }
    return 0.0;
}

//==============================================================================
// getParamMax
//==============================================================================
double getParamMax (Section section, int paramIndex)
{
    switch (section)
    {
        case Section::EQ:      return paramIndex == 3 ? 8000.0 : 12.0;
        case Section::Comp:
            switch (paramIndex)
            {
                case 0:  return 0.0;
                case 1:  return 20.0;
                case 2:  return 100.0;
                case 3:  return 1000.0;
                default: return 1.0;
            }
        case Section::Limiter:
            switch (paramIndex)
            {
                case 0:  return 0.0;
                case 1:  return 500.0;
                default: return 1.0;
            }
        case Section::Inserts: return 1.0;
        case Section::Sends:   return 0.0;
        case Section::Pan:     return 50.0;
        case Section::Volume:  return 12.0;
    }
    return 1.0;
}

//==============================================================================
// getParamStep
//==============================================================================
double getParamStep (Section section, int paramIndex)
{
    switch (section)
    {
        case Section::EQ:      return paramIndex == 3 ? 50.0 : 0.5;
        case Section::Comp:
            switch (paramIndex)
            {
                case 0:  return 1.0;
                case 1:  return 0.5;
                case 2:  return 1.0;
                case 3:  return 10.0;
                default: return 0.1;
            }
        case Section::Limiter:
            switch (paramIndex)
            {
                case 0:  return 0.5;
                case 1:  return 5.0;
                default: return 0.1;
            }
        case Section::Inserts: return 1.0;
        case Section::Sends:   return 2.0;
        case Section::Pan:     return 1.0;
        case Section::Volume:  return 0.5;
    }
    return 1.0;
}

//==============================================================================
// getParamCountForSection
//==============================================================================
int getParamCountForSection (Section section, const MixerState& state, StripType stripType, int stripIndex)
{
    switch (section)
    {
        case Section::EQ:      return 4;  // Low, Mid, High, MidFreq
        case Section::Comp:    return 4;  // Threshold, Ratio, Attack, Release
        case Section::Limiter: return 2;  // Threshold, Release
        case Section::Inserts:
        {
            if (stripType == StripType::Master)
                return juce::jmax (1, static_cast<int> (state.masterInsertSlots.size()));
            if (stripType == StripType::Track)
            {
                auto& slots = state.insertSlots[static_cast<size_t> (stripIndex)];
                return juce::jmax (1, static_cast<int> (slots.size()));
            }
            return 1;
        }
        case Section::Sends:  return 2;  // Reverb, Delay
        case Section::Pan:    return 1;
        case Section::Volume: return 1;
    }
    return 1;
}

} // namespace MixerParamModel
