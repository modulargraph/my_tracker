#pragma once

#include <JuceHeader.h>
#include "TrackerLookAndFeel.h"
#include "MixerState.h"
#include "TrackLayout.h"

class MixerComponent : public juce::Component,
                       private juce::Timer
{
public:
    MixerComponent (TrackerLookAndFeel& lnf, MixerState& state, TrackLayout& layout);

    void paint (juce::Graphics& g) override;
    bool keyPressed (const juce::KeyPress& key) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    // Update mute/solo state from engine
    void setTrackMuteState (int track, bool muted);
    void setTrackSoloState (int track, bool soloed);

    // Callbacks
    std::function<void (int track, bool muted)> onMuteChanged;
    std::function<void (int track, bool soloed)> onSoloChanged;
    std::function<void()> onMixStateChanged;

    int getSelectedTrack() const { return selectedTrack; }

    // Peak level metering
    void setPeakLevelCallback (std::function<float (int)> cb) { peakLevelCallback = std::move (cb); }
    void startMetering() { startTimerHz (30); }
    void stopMetering() { stopTimer(); }

private:
    void timerCallback() override;
    TrackerLookAndFeel& lookAndFeel;
    MixerState& mixerState;
    TrackLayout& trackLayout;

    int selectedTrack = 0;      // visual track index

    // Parameter navigation within a strip
    enum class Section { EQ, Comp, Sends, Pan, Volume };
    Section currentSection = Section::Volume;
    int currentParam = 0;       // param index within section

    // Peak level metering
    std::array<float, kNumTracks> trackPeakLevels {};
    std::function<float (int)> peakLevelCallback;

    // Horizontal scroll
    int scrollOffset = 0;

    // Mouse drag state
    bool dragging = false;
    int dragTrack = -1;
    Section dragSection = Section::Volume;
    int dragParam = -1;
    int dragStartY = 0;
    double dragStartValue = 0.0;

    // Layout constants
    static constexpr int kStripWidth = 104;
    static constexpr int kStripGap = 1;
    static constexpr int kHeaderHeight = 31;
    static constexpr int kEqSectionHeight = 104;
    static constexpr int kCompSectionHeight = 104;
    static constexpr int kSendsSectionHeight = 57;
    static constexpr int kPanSectionHeight = 36;
    static constexpr int kMuteSoloHeight = 31;
    static constexpr int kSectionLabelHeight = 18;

    // Computed layout
    int getStripX (int visualTrack) const;
    int getVisibleStripCount() const;
    juce::Rectangle<int> getStripBounds (int visualTrack) const;

    // Paint helpers
    void paintStrip (juce::Graphics& g, int visualTrack, juce::Rectangle<int> bounds);
    void paintHeader (juce::Graphics& g, int physTrack, int visualTrack, juce::Rectangle<int> bounds);
    void paintEqSection (juce::Graphics& g, const TrackMixState& state, juce::Rectangle<int> bounds,
                         bool isSelected, int selectedParam);
    void paintCompSection (juce::Graphics& g, const TrackMixState& state, juce::Rectangle<int> bounds,
                           bool isSelected, int selectedParam);
    void paintSendsSection (juce::Graphics& g, const TrackMixState& state, juce::Rectangle<int> bounds,
                            bool isSelected, int selectedParam);
    void paintPanSection (juce::Graphics& g, const TrackMixState& state, juce::Rectangle<int> bounds,
                          bool isSelected);
    void paintVolumeFader (juce::Graphics& g, const TrackMixState& state, juce::Rectangle<int> bounds,
                           bool isSelected, float peakLinear = 0.0f);
    void paintMuteSolo (juce::Graphics& g, const TrackMixState& state, juce::Rectangle<int> bounds,
                        int physTrack);

    // Value painting helpers
    void paintVerticalBar (juce::Graphics& g, juce::Rectangle<int> area, double value, double minVal,
                           double maxVal, juce::Colour colour, bool bipolar = false);
    void paintHorizontalBar (juce::Graphics& g, juce::Rectangle<int> area, double value, double minVal,
                             double maxVal, juce::Colour colour, bool bipolar = false);
    void paintKnob (juce::Graphics& g, juce::Rectangle<int> area, double value, double minVal,
                    double maxVal, juce::Colour colour, const juce::String& label);

    // Interaction
    void adjustCurrentParam (double delta);
    int getParamCountForSection (Section section) const;
    void nextSection();
    void prevSection();
    void ensureTrackVisible();

    // Hit testing
    struct HitResult
    {
        int visualTrack = -1;
        Section section = Section::Volume;
        int param = -1;
        bool hitMute = false;
        bool hitSolo = false;
    };
    HitResult hitTestStrip (juce::Point<int> pos) const;

    double getParamValue (int physTrack, Section section, int param) const;
    void setParamValue (int physTrack, Section section, int param, double value);
    double getParamMin (Section section, int param) const;
    double getParamMax (Section section, int param) const;
    double getParamStep (Section section, int param) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerComponent)
};
