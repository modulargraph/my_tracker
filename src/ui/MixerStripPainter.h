#pragma once

#include <JuceHeader.h>
#include "TrackerLookAndFeel.h"
#include "MixerState.h"

//==============================================================================
// Free functions for painting mixer strip sections.
// Extracted from MixerComponent to keep painting logic reusable and separate
// from interaction / layout logic.
//==============================================================================
namespace MixerStripPainter
{
    //--------------------------------------------------------------------------
    // Generic painting primitives
    //--------------------------------------------------------------------------

    void paintVerticalBar (juce::Graphics& g, TrackerLookAndFeel& lnf,
                           juce::Rectangle<int> area, double value,
                           double minVal, double maxVal,
                           juce::Colour colour, bool bipolar = false);

    void paintHorizontalBar (juce::Graphics& g, TrackerLookAndFeel& lnf,
                             juce::Rectangle<int> area, double value,
                             double minVal, double maxVal,
                             juce::Colour colour, bool bipolar = false);

    void paintKnob (juce::Graphics& g, TrackerLookAndFeel& lnf,
                    juce::Rectangle<int> area, double value,
                    double minVal, double maxVal,
                    juce::Colour colour, const juce::String& label);

    //--------------------------------------------------------------------------
    // Section painters
    //--------------------------------------------------------------------------

    void paintGenericEqSection (juce::Graphics& g, TrackerLookAndFeel& lnf,
                                double eqLow, double eqMid, double eqHigh, double midFreq,
                                juce::Rectangle<int> bounds, bool isSelected, int selectedParam);

    void paintGenericCompSection (juce::Graphics& g, TrackerLookAndFeel& lnf,
                                  double threshold, double ratio, double attack, double release,
                                  juce::Rectangle<int> bounds, bool isSelected, int selectedParam);

    void paintGenericVolumeFader (juce::Graphics& g, TrackerLookAndFeel& lnf,
                                  double volume, juce::Rectangle<int> bounds,
                                  bool isSelected, float peakLinear = 0.0f);

    void paintGenericPanSection (juce::Graphics& g, TrackerLookAndFeel& lnf,
                                 int pan, juce::Rectangle<int> bounds, bool isSelected);

    void paintGenericMuteSolo (juce::Graphics& g, TrackerLookAndFeel& lnf,
                               bool muted, bool soloed, juce::Rectangle<int> bounds,
                               bool hasSolo = true);

    void paintLimiterSection (juce::Graphics& g, TrackerLookAndFeel& lnf,
                              double threshold, double release,
                              juce::Rectangle<int> bounds, bool isSelected, int selectedParam);

    /** Unified insert-slot painting used by both track and master strips. */
    void paintInsertSlots (juce::Graphics& g, TrackerLookAndFeel& lnf,
                           const std::vector<InsertSlotState>& slots,
                           int insertRowHeight, int insertAddButtonHeight,
                           juce::Rectangle<int> bounds,
                           bool isSelected, int selectedParam);

} // namespace MixerStripPainter
