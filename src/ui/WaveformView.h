#pragma once

#include <JuceHeader.h>
#include "InstrumentParams.h"
#include "TrackerLookAndFeel.h"

/**
 * Standalone waveform display component.
 *
 * Owns the AudioThumbnail and renders the waveform, markers, and
 * overview bar.  It does NOT capture mouse events -- the parent is
 * responsible for all interaction logic and simply pushes state
 * updates into this component.
 */
class WaveformView : public juce::Component
{
public:
    WaveformView (TrackerLookAndFeel& lnf);
    ~WaveformView() override = default;

    // ── Sample loading ──
    void setSample (const juce::File& sampleFile);
    void clearSample();
    double getTotalLength() const { return thumbnail.getTotalLength(); }

    // ── State pushed by the parent every frame ──
    void setParams (const InstrumentParams& params);
    void setViewRange (double start, double end);
    void setPlaybackPosition (float normPos);
    void setSelectedSliceIndex (int idx);

    // ── Marker highlight state ──
    enum class MarkerType { None, Start, End, LoopStart, LoopEnd, GranPos, Slice };
    void setHoveredMarker (MarkerType m)  { hoveredMarker = m;  repaint(); }
    void setDraggingMarker (MarkerType m) { draggingMarker = m; repaint(); }
    void setDraggingSliceIndex (int idx)  { draggingSliceIndex = idx; }

    // ── Coordinate helpers (used by parent for hit-testing & conversion) ──
    juce::Rectangle<int> getWaveformArea() const;
    juce::Rectangle<int> getOverviewArea() const;
    double pixelToNormPos (int pixelX, juce::Rectangle<int> waveArea, double viewStart, double viewEnd) const;
    int    normPosToPixel (double normPos, juce::Rectangle<int> waveArea, double viewStart, double viewEnd) const;

    // ── Component overrides ──
    void paint (juce::Graphics& g) override;
    void resized() override {}

private:
    TrackerLookAndFeel& lookAndFeel;

    // Waveform display
    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache { 1 };
    juce::AudioThumbnail thumbnail { 512, formatManager, thumbnailCache };

    // Snapshot of state from the parent (read-only in paint)
    InstrumentParams currentParams;
    double viewStart = 0.0;
    double viewEnd   = 1.0;
    float  currentPlaybackPos = -1.0f;
    int    selectedSliceIndex = -1;

    // Marker highlight state (pushed by parent)
    MarkerType hoveredMarker  = MarkerType::None;
    MarkerType draggingMarker = MarkerType::None;
    int draggingSliceIndex    = -1;

    // Layout
    static constexpr int kOverviewBarHeight = 20;

    // ── Drawing helpers ──
    void drawWaveform (juce::Graphics& g, juce::Rectangle<int> area);
    void drawWaveformMarkers (juce::Graphics& g, juce::Rectangle<int> area);
    void drawOverviewBar (juce::Graphics& g, juce::Rectangle<int> area);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
};
