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

    // Display modes (set by MainComponent based on active tab)
    enum class DisplayMode { InstrumentEdit, InstrumentType };
    enum class EditSubTab { Parameters, Modulation };

    void setDisplayMode (DisplayMode mode);
    DisplayMode getDisplayMode() const { return displayMode; }

    void setEditSubTab (EditSubTab tab);
    EditSubTab getEditSubTab() const { return editSubTab; }

    // Instrument management
    void setInstrument (int instrumentIndex, const juce::File& sampleFile, const InstrumentParams& params);
    void setPluginInstrument (int instrumentIndex, const juce::String& pluginName, int ownerTrack);
    void clearInstrument();

    int getInstrument() const { return currentInstrument; }
    InstrumentParams getParams() const { return currentParams; }
    bool isShowingPluginInstrument() const { return showingPlugin; }

    // Octave for keyboard note preview
    void setOctave (int oct) { currentOctave = juce::jlimit (0, 9, oct); }
    int getOctave() const { return currentOctave; }

    // Callbacks
    std::function<void (int instrument, const InstrumentParams& params)> onParamsChanged;
    std::function<void (int instrument, const InstrumentParams& params)> onRealtimeParamsChanged;
    std::function<void (int instrument, int note)> onPreviewRequested;
    std::function<void()> onPreviewStopped;
    std::function<float()> onGetPreviewPosition;
    std::function<void (int instrument)> onOpenPluginEditorRequested;

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    bool keyStateChanged (bool isKeyDown) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void mouseMove (const juce::MouseEvent& event) override;
    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

private:
    TrackerLookAndFeel& lookAndFeel;
    DisplayMode displayMode = DisplayMode::InstrumentEdit;
    EditSubTab editSubTab = EditSubTab::Parameters;
    int currentInstrument = -1;
    juce::File currentFile;
    InstrumentParams currentParams;
    InstrumentParams lastCommittedParams;

    // Plugin instrument display state
    bool showingPlugin = false;
    juce::String pluginInstrumentName;
    int pluginOwnerTrack = -1;

    void drawPluginInstrumentPage (juce::Graphics& g, juce::Rectangle<int> area);

    // Waveform display
    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache { 1 };
    juce::AudioThumbnail thumbnail { 512, formatManager, thumbnailCache };

    // Column-based focus (per mode/sub-tab)
    int parametersColumn = 0;
    int modColumn = 0;
    int modDestIndex = 0;
    int playbackColumn = 0;

    // Octave for keyboard note preview
    int currentOctave = 4;

    // Layout constants
    static constexpr int kHeaderHeight = 26;
    static constexpr int kBottomBarHeight = 40;
    static constexpr int kListItemHeight = 22;
    static constexpr int kSubTabWidth = 80;
    static constexpr int kOverviewBarHeight = 20;

    // LFO speed presets
    static const int kLfoSpeeds[];
    static constexpr int kNumLfoSpeeds = 14;

    // Drag state (for bar/list column drags)
    bool isDragging = false;
    float dragStartY = 0.0f;
    InstrumentParams dragStartParams;
    int dragStartModDestIndex = 0;

    // ── Waveform zoom ──
    double viewStart = 0.0;  // normalized 0-1 left edge of zoomed view
    double viewEnd   = 1.0;  // normalized 0-1 right edge of zoomed view

    // ── Waveform marker dragging ──
    enum class MarkerType { None, Start, End, LoopStart, LoopEnd, GranPos, Slice };
    MarkerType draggingMarker = MarkerType::None;
    int draggingSliceIndex = -1;         // which slice point is being dragged
    bool isWaveformDragging = false;     // true when dragging a marker on waveform
    float waveformDragStartX = 0.0f;

    // ── Slice selection ──
    int selectedSliceIndex = -1;         // currently selected slice in Slice modes

    // ── Hover state for cursor feedback ──
    MarkerType hoveredMarker = MarkerType::None;

    // ── Waveform panning ──
    bool isPanning = false;
    double panStartViewStart = 0.0;
    double panStartViewEnd = 0.0;
    float panStartX = 0.0f;

    // ── Auto-slice sensitivity ──
    double autoSliceSensitivity = 0.5;   // 0.0 - 1.0

    // Preview state (for hold-to-preview and cursor)
    bool previewActive = false;
    int previewKeyCode = -1;
    float currentPlaybackPos = -1.0f;

    // Debounced apply
    bool paramsDirty = false;
    void constrainPlaybackMarkersToRegion();
    void timerCallback() override;
    void scheduleApply();
    void notifyParamsChanged();
    bool isRealtimeOnlyChange (const InstrumentParams& oldP, const InstrumentParams& newP) const;

    // Focus helpers
    int getFocusedColumn() const;
    void setFocusedColumn (int col);
    int getColumnCount() const;

    // Value adjustment
    void adjustCurrentValue (int direction, bool fine, bool large);
    void adjustCurrentValueByDelta (double normDelta);
    bool isCurrentColumnDiscrete() const;

    // Drawing helpers
    void drawHeader (juce::Graphics& g, juce::Rectangle<int> area);
    void drawBottomBar (juce::Graphics& g, juce::Rectangle<int> area);
    void drawListColumn (juce::Graphics& g, juce::Rectangle<int> area,
                         const juce::StringArray& items, int selectedIndex,
                         bool focused, juce::Colour colour);
    void drawBarMeter (juce::Graphics& g, juce::Rectangle<int> area,
                       float value01, bool focused, juce::Colour colour);

    // Page drawing
    void drawParametersPage (juce::Graphics& g, juce::Rectangle<int> area);
    void drawModulationPage (juce::Graphics& g, juce::Rectangle<int> area);
    void drawPlaybackPage (juce::Graphics& g, juce::Rectangle<int> area);
    void drawWaveform (juce::Graphics& g, juce::Rectangle<int> area);
    void drawWaveformMarkers (juce::Graphics& g, juce::Rectangle<int> area);
    void drawOverviewBar (juce::Graphics& g, juce::Rectangle<int> area);
    void drawSubTabBar (juce::Graphics& g, juce::Rectangle<int> area);

    // Bottom bar content
    juce::String getColumnName (int col) const;
    juce::String getColumnValue (int col) const;

    // Note preview
    int keyToNote (const juce::KeyPress& key) const;

    // ── Waveform coordinate helpers ──
    juce::Rectangle<int> getWaveformArea() const;
    double pixelToNormPos (int pixelX, juce::Rectangle<int> waveArea) const;
    int normPosToPixel (double normPos, juce::Rectangle<int> waveArea) const;
    MarkerType hitTestMarker (int pixelX, juce::Rectangle<int> waveArea) const;
    int hitTestSlice (int pixelX, juce::Rectangle<int> waveArea) const;

    // ── Zoom helpers ──
    void zoomAroundPoint (double zoomFactor, double normPos);
    void scrollView (double deltaNorm);

    // ── Slice operations ──
    void addSliceAtPosition (double normPos);
    void removeSlice (int sliceIdx);
    void generateEqualSlices (int numSlices);
    void autoSlice();

    // String helpers
    juce::String getPlayModeName (InstrumentParams::PlayMode mode) const;
    juce::String getFilterTypeName (InstrumentParams::FilterType type) const;
    juce::String getModTypeName (InstrumentParams::Modulation::Type type) const;
    juce::String getLfoShapeName (InstrumentParams::Modulation::LFOShape shape) const;
    juce::String getModDestFullName (int dest) const;
    juce::String getGranShapeName (InstrumentParams::GranShape shape) const;
    juce::String getGranLoopName (InstrumentParams::GranLoop loop) const;
    juce::String formatLfoSpeed (int speed) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleEditorComponent)
};
