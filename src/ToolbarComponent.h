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
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void mouseDoubleClick (const juce::MouseEvent& event) override;
    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

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

    // Panel toggle state
    void setArrangementVisible (bool v) { arrangementOn = v; repaint(); }
    void setInstrumentPanelVisible (bool v) { instrumentPanelOn = v; repaint(); }
    // 0 = off, 1 = center, 2 = page
    void setFollowMode (int mode) { followModeVal = mode; repaint(); }

    // Callbacks
    std::function<void()> onAddPattern;
    std::function<void()> onRemovePattern;
    std::function<void()> onPatternLengthClick;
    std::function<void (int delta)> onLengthDrag;
    std::function<void (double delta)> onBpmDrag;
    std::function<void (int delta)> onStepDrag;
    std::function<void (int delta)> onOctaveDrag;
    std::function<void()> onModeToggle;
    std::function<void()> onPatternNameDoubleClick;
    std::function<void()> onToggleArrangement;
    std::function<void()> onToggleInstrumentPanel;
    std::function<void()> onNextPattern;
    std::function<void()> onPrevPattern;
    std::function<void (int delta)> onInstrumentDrag;
    std::function<void()> onFollowToggle;

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
    bool arrangementOn = false;
    bool instrumentPanelOn = true;
    int followModeVal = 0; // 0=off, 1=center, 2=page

    // Hit areas
    juce::Rectangle<int> addPatBounds, removePatBounds;
    juce::Rectangle<int> lengthBounds, bpmBounds, stepBounds, octaveBounds, modeBounds, patNameBounds;
    juce::Rectangle<int> arrangementToggleBounds, instrumentToggleBounds, patSelectorBounds;
    juce::Rectangle<int> instrumentBounds, followBounds;

    // Drag state
    enum class DragTarget { None, Length, Bpm, Step, Octave, Instrument };
    DragTarget dragTarget = DragTarget::None;
    int dragStartY = 0;
    int dragAccumulated = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToolbarComponent)
};
