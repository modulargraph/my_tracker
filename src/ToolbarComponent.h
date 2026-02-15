#pragma once

#include <JuceHeader.h>
#include "TrackerLookAndFeel.h"

class ToolbarComponent : public juce::Component
{
public:
    ToolbarComponent (TrackerLookAndFeel& lnf);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;

    // Display state
    void setPatternInfo (int current, int total, const juce::String& name);
    void setPatternLength (int len);
    void setInstrument (int inst);
    void setOctave (int oct);
    void setEditStep (int step);
    void setBpm (double bpm);
    void setPlayState (bool playing);
    void setPlaybackMode (bool songMode);
    void setSampleName (const juce::String& name);

    // Callbacks
    std::function<void()> onAddPattern;
    std::function<void()> onRemovePattern;
    std::function<void()> onPatternLengthClick;

    static constexpr int kToolbarHeight = 36;

private:
    TrackerLookAndFeel& lookAndFeel;

    int currentPattern = 0;
    int totalPatterns = 1;
    juce::String patternName = "Pattern";
    int patternLength = 64;
    int instrument = 0;
    int octave = 4;
    int step = 1;
    double bpm = 120.0;
    bool playing = false;
    bool songMode = false;
    juce::String sampleName;

    // Button hit areas
    juce::Rectangle<int> addPatBounds, removePatBounds, lengthBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToolbarComponent)
};
