#pragma once

#include <JuceHeader.h>
#include "SendEffectsParams.h"
#include "TrackerLookAndFeel.h"

class SendEffectsComponent : public juce::Component
{
public:
    SendEffectsComponent (TrackerLookAndFeel& lnf);
    ~SendEffectsComponent() override = default;

    // Set/get current parameters
    void setDelayParams (const DelayParams& params);
    void setReverbParams (const ReverbParams& params);
    DelayParams getDelayParams() const { return delay; }
    ReverbParams getReverbParams() const { return reverb; }

    // Callback when any parameter changes
    std::function<void (const DelayParams&, const ReverbParams&)> onParamsChanged;

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

private:
    TrackerLookAndFeel& lookAndFeel;
    DelayParams delay;
    ReverbParams reverb;

    // Focus: 0 = delay section, 1 = reverb section
    int section = 0;

    // Column within each section
    int delayColumn = 0;
    int reverbColumn = 0;

    // Column counts
    static constexpr int kDelayColumns = 8;  // Time, Sync Div, BPM Sync, Feedback, Filter, Cutoff, Wet, Width
    static constexpr int kReverbColumns = 5; // Room Size, Decay, Damping, Pre-Delay, Wet

    // Layout
    static constexpr int kHeaderHeight = 26;
    static constexpr int kBottomBarHeight = 40;
    static constexpr int kSectionGap = 4;
    static constexpr int kListItemHeight = 22;

    // Navigation
    int& currentColumn() { return section == 0 ? delayColumn : reverbColumn; }
    int currentColumnCount() const { return section == 0 ? kDelayColumns : kReverbColumns; }

    // Value adjustment
    void adjustCurrentValue (int direction, bool fine, bool large);
    bool isBarColumn() const;
    void setCurrentValueFromNorm (float norm);

    // Drawing helpers
    void drawHeader (juce::Graphics& g, juce::Rectangle<int> area);
    void drawBottomBar (juce::Graphics& g, juce::Rectangle<int> area);
    void drawBarMeter (juce::Graphics& g, juce::Rectangle<int> area,
                       float value01, bool focused, juce::Colour colour);
    void drawListColumn (juce::Graphics& g, juce::Rectangle<int> area,
                         const juce::StringArray& items, int selectedIndex,
                         bool focused, juce::Colour colour);

    // Section drawing
    void drawDelaySection (juce::Graphics& g, juce::Rectangle<int> area, bool sectionFocused);
    void drawReverbSection (juce::Graphics& g, juce::Rectangle<int> area, bool sectionFocused);

    // Info
    juce::String getColumnName() const;
    juce::String getColumnValue() const;

    void notifyChanged();

    // Mouse drag state
    bool mouseDragging = false;
    int mouseDragStartY = 0;
    int mouseDragAccumulated = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SendEffectsComponent)
};
