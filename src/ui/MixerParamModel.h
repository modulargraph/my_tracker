#pragma once

#include "MixerState.h"
#include "MixerHitTest.h"

//==============================================================================
// MixerParamModel: pure-function access to mixer parameter values by
// strip type, strip index, section, and param index.  Extracted from
// MixerComponent so that other subsystems can query or modify mixer
// parameters without depending on the UI component.
//==============================================================================
namespace MixerParamModel
{
    // Re-export the shared enum types for convenience
    using Section   = MixerSection;
    using StripType = MixerStripType;

    //--------------------------------------------------------------------------
    // Value access
    double getParamValue (const MixerState& state, StripType stripType, int stripIndex, Section section, int paramIndex);
    void   setParamValue (MixerState& state, StripType stripType, int stripIndex, Section section, int paramIndex, double value);

    //--------------------------------------------------------------------------
    // Range / step metadata (independent of strip type)
    double getParamMin  (Section section, int paramIndex);
    double getParamMax  (Section section, int paramIndex);
    double getParamStep (Section section, int paramIndex);

    //--------------------------------------------------------------------------
    // Number of adjustable parameters in a given section.
    // For the Inserts section, pass the MixerState so slot counts can be read.
    int getParamCountForSection (Section section, const MixerState& state, StripType stripType, int stripIndex);
}
