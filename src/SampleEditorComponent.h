#pragma once

#include <JuceHeader.h>
#include "InstrumentParams.h"
#include "TrackerLookAndFeel.h"

class SampleEditorComponent : public juce::Component,
                               private juce::Timer
{
public:
    SampleEditorComponent (TrackerLookAndFeel& lnf);
    ~SampleEditorComponent() override;

    void open (int instrumentIndex, const juce::File& sampleFile, const InstrumentParams& params);
    void close();

    bool isOpen() const { return editorOpen; }
    int getInstrument() const { return currentInstrument; }
    InstrumentParams getParams() const { return currentParams; }

    // Callbacks
    std::function<void (int instrument, const InstrumentParams& params)> onParamsChanged;
    std::function<void (int instrument)> onPreviewRequested;
    std::function<void()> onCloseRequested;

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;

private:
    TrackerLookAndFeel& lookAndFeel;
    bool editorOpen = false;
    int currentInstrument = -1;
    juce::File currentFile;
    InstrumentParams currentParams;

    // Waveform display
    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache { 1 };
    juce::AudioThumbnail thumbnail { 512, formatManager, thumbnailCache };

    // Controls
    enum class FocusedControl { Start, End, Attack, Decay, Sustain, Release, Reverse };
    FocusedControl focusedControl = FocusedControl::Start;
    static constexpr int kNumControls = 7;

    static constexpr int kHeaderHeight = 28;
    static constexpr int kControlsHeight = 100;

    // Mouse drag state
    bool isDragging = false;
    float dragStartY = 0.0f;
    InstrumentParams dragStartParams;

    // Debounced audio processing
    bool paramsDirty = false;
    void timerCallback() override;
    void scheduleApply();

    void adjustFocusedValue (int direction, bool fine, bool large);
    void adjustControlByDelta (FocusedControl ctrl, float pixelDelta);
    void notifyParamsChanged();
    void drawWaveform (juce::Graphics& g, juce::Rectangle<int> area);
    void drawEnvelopeOverlay (juce::Graphics& g, juce::Rectangle<int> area);
    void drawControls (juce::Graphics& g, juce::Rectangle<int> area);

    juce::String getControlName (FocusedControl ctrl) const;
    juce::String getControlValue (FocusedControl ctrl) const;
    juce::Rectangle<int> getControlsArea() const;
    int getControlSlotWidth() const;
    FocusedControl hitTestControl (juce::Point<int> pos) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleEditorComponent)
};
