#include "WaveformView.h"
#include <cmath>

//==============================================================================
// Construction
//==============================================================================

WaveformView::WaveformView (TrackerLookAndFeel& lnf)
    : lookAndFeel (lnf)
{
    formatManager.registerBasicFormats();
    setInterceptsMouseClicks (false, false);
}

//==============================================================================
// Sample loading
//==============================================================================

void WaveformView::setSample (const juce::File& sampleFile)
{
    thumbnail.clear();
    if (sampleFile.existsAsFile())
        thumbnail.setSource (new juce::FileInputSource (sampleFile));
}

void WaveformView::clearSample()
{
    thumbnail.clear();
}

//==============================================================================
// State setters
//==============================================================================

void WaveformView::setParams (const InstrumentParams& params)
{
    currentParams = params;
    repaint();
}

void WaveformView::setViewRange (double start, double end)
{
    viewStart = start;
    viewEnd   = end;
    repaint();
}

void WaveformView::setPlaybackPosition (float normPos)
{
    if (currentPlaybackPos != normPos)
    {
        currentPlaybackPos = normPos;
        repaint();
    }
}

void WaveformView::setSelectedSliceIndex (int idx)
{
    selectedSliceIndex = idx;
    repaint();
}

//==============================================================================
// Coordinate helpers
//==============================================================================

juce::Rectangle<int> WaveformView::getWaveformArea() const
{
    auto area = getLocalBounds();
    area = area.withTrimmedBottom (kOverviewBarHeight + 2);
    return area.reduced (4, 4);
}

juce::Rectangle<int> WaveformView::getOverviewArea() const
{
    auto area = getLocalBounds();
    auto overviewArea = area.removeFromBottom (kOverviewBarHeight + 2);
    return overviewArea.reduced (4, 0).withTrimmedTop (2);
}

double WaveformView::pixelToNormPos (int pixelX, juce::Rectangle<int> waveArea,
                                     double vStart, double vEnd) const
{
    double frac = static_cast<double> (pixelX - waveArea.getX())
                  / static_cast<double> (juce::jmax (1, waveArea.getWidth()));
    frac = juce::jlimit (0.0, 1.0, frac);
    return vStart + frac * (vEnd - vStart);
}

int WaveformView::normPosToPixel (double normPos, juce::Rectangle<int> waveArea,
                                  double vStart, double vEnd) const
{
    double vWidth = vEnd - vStart;
    if (vWidth <= 0.0) vWidth = 1.0;
    double frac = (normPos - vStart) / vWidth;
    return waveArea.getX() + juce::roundToInt (frac * waveArea.getWidth());
}

//==============================================================================
// Paint
//==============================================================================

void WaveformView::paint (juce::Graphics& g)
{
    auto waveArea = getWaveformArea();
    auto overviewArea = getOverviewArea();

    drawWaveform (g, waveArea);
    drawWaveformMarkers (g, waveArea);
    drawOverviewBar (g, overviewArea);
}

//==============================================================================
// Drawing: Waveform
//==============================================================================

void WaveformView::drawWaveform (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto bg = lookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId);

    g.setColour (bg.brighter (0.06f));
    g.fillRect (area);

    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawRect (area, 1);

    // Center line
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId).withAlpha (0.4f));
    g.drawHorizontalLine (area.getCentreY(), static_cast<float> (area.getX()),
                          static_cast<float> (area.getRight()));

    double totalLen = thumbnail.getTotalLength();
    if (totalLen > 0.0)
    {
        // Shade outside start/end (in zoomed coordinates)
        int startPx = normPosToPixel (currentParams.startPos, area, viewStart, viewEnd);
        int endPx   = normPosToPixel (currentParams.endPos, area, viewStart, viewEnd);

        g.setColour (juce::Colour (0x40000000));
        if (startPx > area.getX())
            g.fillRect (area.getX(), area.getY(), startPx - area.getX(), area.getHeight());
        if (endPx < area.getRight())
            g.fillRect (endPx, area.getY(), area.getRight() - endPx, area.getHeight());

        // Draw the zoomed portion of the waveform
        double drawStart = viewStart * totalLen;
        double drawEnd   = viewEnd * totalLen;

        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::fxColourId).withAlpha (0.7f));
        thumbnail.drawChannels (g, area.reduced (1), drawStart, drawEnd, 1.0f);
    }
    else
    {
        g.setFont (lookAndFeel.getMonoFont (12.0f));
        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.25f));
        g.drawText ("No waveform data", area, juce::Justification::centred);
    }
}

//==============================================================================
// Drawing: Waveform markers
//==============================================================================

void WaveformView::drawWaveformMarkers (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (thumbnail.getTotalLength() <= 0.0) return;

    auto drawMarker = [&] (double normPos, juce::Colour colour, const juce::String& label,
                           bool highlighted = false, bool thick = false)
    {
        int x = normPosToPixel (normPos, area, viewStart, viewEnd);
        if (x < area.getX() - 2 || x > area.getRight() + 2) return;

        if (highlighted || thick)
        {
            g.setColour (colour.withAlpha (0.3f));
            g.fillRect (x - 2, area.getY(), 5, area.getHeight());
        }

        g.setColour (colour);
        g.drawVerticalLine (x, static_cast<float> (area.getY()),
                            static_cast<float> (area.getBottom()));
        if (thick)
            g.drawVerticalLine (x + 1, static_cast<float> (area.getY()),
                                static_cast<float> (area.getBottom()));

        g.setFont (lookAndFeel.getMonoFont (9.0f));
        g.drawText (label, x + 2, area.getY() + 2, 30, 12, juce::Justification::centredLeft);
    };

    auto startCol = juce::Colour (0xff44cc44);
    auto endCol   = juce::Colour (0xffcc4444);
    bool startHi = (hoveredMarker == MarkerType::Start || draggingMarker == MarkerType::Start);
    bool endHi   = (hoveredMarker == MarkerType::End   || draggingMarker == MarkerType::End);

    drawMarker (currentParams.startPos, startCol, "S", startHi, startHi);
    drawMarker (currentParams.endPos,   endCol,   "E", endHi,   endHi);

    auto mode = currentParams.playMode;
    if (mode == InstrumentParams::PlayMode::ForwardLoop
        || mode == InstrumentParams::PlayMode::BackwardLoop
        || mode == InstrumentParams::PlayMode::PingpongLoop)
    {
        auto loopCol = juce::Colour (0xff4488ff);
        bool lsHi = (hoveredMarker == MarkerType::LoopStart || draggingMarker == MarkerType::LoopStart);
        bool leHi = (hoveredMarker == MarkerType::LoopEnd   || draggingMarker == MarkerType::LoopEnd);
        drawMarker (currentParams.loopStart, loopCol, "LS", lsHi, lsHi);
        drawMarker (currentParams.loopEnd,   loopCol, "LE", leHi, leHi);
    }

    if (mode == InstrumentParams::PlayMode::Slice || mode == InstrumentParams::PlayMode::BeatSlice)
    {
        auto sliceCol = juce::Colour (0xffddcc44);
        for (int i = 0; i < static_cast<int> (currentParams.slicePoints.size()); ++i)
        {
            bool selected = (i == selectedSliceIndex);
            bool dragging = (draggingMarker == MarkerType::Slice && draggingSliceIndex == i);
            bool hi = selected || dragging
                      || (hoveredMarker == MarkerType::Slice && draggingSliceIndex == -1);
            auto col = selected ? sliceCol.brighter (0.3f) : sliceCol;
            drawMarker (currentParams.slicePoints[static_cast<size_t> (i)], col,
                        juce::String (i), hi, selected || dragging);
        }
    }

    if (mode == InstrumentParams::PlayMode::Granular)
    {
        bool gHi = (hoveredMarker == MarkerType::GranPos || draggingMarker == MarkerType::GranPos);
        drawMarker (currentParams.granularPosition, juce::Colour (0xffffaa44), "G", gHi, gHi);
    }

    // Draw playback cursor
    if (currentPlaybackPos >= 0.0f)
    {
        auto cursorCol = lookAndFeel.findColour (TrackerLookAndFeel::playbackCursorColourId).brighter (0.3f);
        int cx = normPosToPixel (static_cast<double> (currentPlaybackPos), area, viewStart, viewEnd);
        if (cx >= area.getX() && cx <= area.getRight())
        {
            g.setColour (cursorCol.withAlpha (0.15f));
            g.fillRect (cx - 3, area.getY(), 7, area.getHeight());
            g.setColour (cursorCol);
            g.drawVerticalLine (cx, static_cast<float> (area.getY()), static_cast<float> (area.getBottom()));
            g.drawVerticalLine (cx + 1, static_cast<float> (area.getY()), static_cast<float> (area.getBottom()));
        }
    }
}

//==============================================================================
// Drawing: Overview bar
//==============================================================================

void WaveformView::drawOverviewBar (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto bg = lookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId);
    auto gridCol = lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId);

    // Background
    g.setColour (bg.brighter (0.03f));
    g.fillRect (area);

    // Border
    g.setColour (gridCol);
    g.drawRect (area, 1);

    double totalLen = thumbnail.getTotalLength();
    if (totalLen <= 0.0) return;

    auto inner = area.reduced (1);

    // Draw full waveform thumbnail (small)
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::fxColourId).withAlpha (0.4f));
    thumbnail.drawChannels (g, inner, 0.0, totalLen, 0.6f);

    // Draw start/end shading
    int startPx = inner.getX() + juce::roundToInt (currentParams.startPos * inner.getWidth());
    int endPx   = inner.getX() + juce::roundToInt (currentParams.endPos * inner.getWidth());

    g.setColour (juce::Colour (0x30000000));
    if (startPx > inner.getX())
        g.fillRect (inner.getX(), inner.getY(), startPx - inner.getX(), inner.getHeight());
    if (endPx < inner.getRight())
        g.fillRect (endPx, inner.getY(), inner.getRight() - endPx, inner.getHeight());

    // Draw view rectangle (highlight showing current zoomed region)
    int viewStartPx = inner.getX() + juce::roundToInt (viewStart * inner.getWidth());
    int viewEndPx   = inner.getX() + juce::roundToInt (viewEnd * inner.getWidth());
    int viewW = juce::jmax (2, viewEndPx - viewStartPx);

    auto viewRect = juce::Rectangle<int> (viewStartPx, inner.getY(), viewW, inner.getHeight());

    // Semi-transparent fill for view area
    g.setColour (juce::Colour (0x20ffffff));
    g.fillRect (viewRect);

    // Border for view area
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.6f));
    g.drawRect (viewRect, 1);
}
