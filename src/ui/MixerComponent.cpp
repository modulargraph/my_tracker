#include "MixerComponent.h"

MixerComponent::MixerComponent (TrackerLookAndFeel& lnf, MixerState& state, TrackLayout& layout)
    : lookAndFeel (lnf), mixerState (state), trackLayout (layout)
{
    setWantsKeyboardFocus (true);
    trackPeakLevels.fill (0.0f);
}

//==============================================================================
// Metering timer
//==============================================================================

void MixerComponent::timerCallback()
{
    bool needsRepaint = false;
    constexpr float decayRate = 0.85f;  // peak decay per timer tick (~30Hz)

    for (int t = 0; t < kNumTracks; ++t)
    {
        float newPeak = 0.0f;
        if (peakLevelCallback)
            newPeak = peakLevelCallback (t);

        // Use the max of the new peak and the decayed old peak
        float decayed = trackPeakLevels[static_cast<size_t> (t)] * decayRate;
        float level = juce::jmax (newPeak, decayed);

        if (level < 0.001f) level = 0.0f;

        if (std::abs (level - trackPeakLevels[static_cast<size_t> (t)]) > 0.0001f)
        {
            trackPeakLevels[static_cast<size_t> (t)] = level;
            needsRepaint = true;
        }
    }

    if (needsRepaint)
        repaint();
}

//==============================================================================
// Layout helpers
//==============================================================================

int MixerComponent::getStripX (int visualTrack) const
{
    return (visualTrack - scrollOffset) * (kStripWidth + kStripGap);
}

int MixerComponent::getVisibleStripCount() const
{
    return juce::jmax (1, getWidth() / (kStripWidth + kStripGap));
}

juce::Rectangle<int> MixerComponent::getStripBounds (int visualTrack) const
{
    int x = getStripX (visualTrack);
    return { x, 0, kStripWidth, getHeight() };
}

int MixerComponent::getInsertsSectionHeight (int physTrack) const
{
    auto& slots = mixerState.insertSlots[static_cast<size_t> (physTrack)];
    int numSlots = static_cast<int> (slots.size());
    // Always show the + button, plus one row per existing insert
    return numSlots * kInsertRowHeight + kInsertAddButtonHeight;
}

//==============================================================================
// Main paint
//==============================================================================

void MixerComponent::paint (juce::Graphics& g)
{
    auto bg = lookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId);
    g.fillAll (bg);

    int visibleCount = getVisibleStripCount();

    for (int vi = scrollOffset; vi < juce::jmin (scrollOffset + visibleCount + 1, kNumTracks); ++vi)
    {
        auto bounds = getStripBounds (vi);
        if (bounds.getRight() < 0 || bounds.getX() > getWidth())
            continue;

        paintStrip (g, vi, bounds);
    }

    // Draw scroll indicators if needed
    if (scrollOffset > 0)
    {
        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::fxColourId).withAlpha (0.6f));
        g.setFont (lookAndFeel.getMonoFont (13.0f));
        g.drawText ("<", 0, getHeight() / 2 - 10, 12, 20, juce::Justification::centred);
    }
    if (scrollOffset + visibleCount < kNumTracks)
    {
        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::fxColourId).withAlpha (0.6f));
        g.setFont (lookAndFeel.getMonoFont (13.0f));
        g.drawText (">", getWidth() - 12, getHeight() / 2 - 10, 12, 20, juce::Justification::centred);
    }
}

//==============================================================================
// Strip painting
//==============================================================================

void MixerComponent::paintStrip (juce::Graphics& g, int visualTrack, juce::Rectangle<int> bounds)
{
    int physTrack = trackLayout.visualToPhysical (visualTrack);
    auto& state = mixerState.tracks[static_cast<size_t> (physTrack)];
    bool isSelected = (visualTrack == selectedTrack);

    // Strip background
    auto stripBg = lookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId).brighter (0.04f);
    if (isSelected)
        stripBg = stripBg.brighter (0.06f);
    g.setColour (stripBg);
    g.fillRect (bounds);

    // Strip border
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawVerticalLine (bounds.getRight(), 0.0f, static_cast<float> (getHeight()));

    // Layout sections top to bottom
    auto r = bounds;

    // Header
    auto headerArea = r.removeFromTop (kHeaderHeight);
    paintHeader (g, physTrack, visualTrack, headerArea);

    // EQ section
    auto eqLabelArea = r.removeFromTop (kSectionLabelHeight);
    g.setFont (lookAndFeel.getMonoFont (12.0f));
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::fxColourId).withAlpha (0.6f));
    g.drawText ("EQ", eqLabelArea, juce::Justification::centred);

    auto eqArea = r.removeFromTop (kEqSectionHeight);
    paintEqSection (g, state, eqArea, isSelected,
                    (isSelected && currentSection == Section::EQ) ? currentParam : -1);

    // Compressor section
    auto compLabelArea = r.removeFromTop (kSectionLabelHeight);
    g.setFont (lookAndFeel.getMonoFont (12.0f));
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::fxColourId).withAlpha (0.6f));
    g.drawText ("COMP", compLabelArea, juce::Justification::centred);

    auto compArea = r.removeFromTop (kCompSectionHeight);
    paintCompSection (g, state, compArea, isSelected,
                      (isSelected && currentSection == Section::Comp) ? currentParam : -1);

    // Inserts section (between Comp and Send)
    int insertHeight = getInsertsSectionHeight (physTrack);
    if (insertHeight > 0)
    {
        auto insertLabelArea = r.removeFromTop (kSectionLabelHeight);
        g.setFont (lookAndFeel.getMonoFont (12.0f));
        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::fxColourId).withAlpha (0.6f));
        g.drawText ("INSERTS", insertLabelArea, juce::Justification::centred);

        auto insertsArea = r.removeFromTop (insertHeight);
        paintInsertsSection (g, physTrack, insertsArea, isSelected,
                             (isSelected && currentSection == Section::Inserts) ? currentParam : -1);
    }

    // Sends section
    auto sendsLabelArea = r.removeFromTop (kSectionLabelHeight);
    g.setFont (lookAndFeel.getMonoFont (12.0f));
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::fxColourId).withAlpha (0.6f));
    g.drawText ("SEND", sendsLabelArea, juce::Justification::centred);

    auto sendsArea = r.removeFromTop (kSendsSectionHeight);
    paintSendsSection (g, state, sendsArea, isSelected,
                       (isSelected && currentSection == Section::Sends) ? currentParam : -1);

    // Separator
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (r.getY(), static_cast<float> (bounds.getX()),
                          static_cast<float> (bounds.getRight()));
    r.removeFromTop (1);

    // Pan section
    auto panArea = r.removeFromTop (kPanSectionHeight);
    paintPanSection (g, state, panArea, isSelected && currentSection == Section::Pan);

    // Separator
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (r.getY(), static_cast<float> (bounds.getX()),
                          static_cast<float> (bounds.getRight()));
    r.removeFromTop (1);

    // Mute/Solo buttons
    auto muteSoloArea = r.removeFromBottom (kMuteSoloHeight);
    paintMuteSolo (g, state, muteSoloArea, physTrack);

    // Volume fader fills the rest
    float peakLevel = trackPeakLevels[static_cast<size_t> (physTrack)];
    paintVolumeFader (g, state, r, isSelected && currentSection == Section::Volume, peakLevel);
}

void MixerComponent::paintHeader (juce::Graphics& g, int physTrack, int /*visualTrack*/, juce::Rectangle<int> bounds)
{
    // Check if track belongs to a group
    int groupIdx = trackLayout.getGroupForTrack (physTrack);
    if (groupIdx >= 0)
    {
        auto& group = trackLayout.getGroup (groupIdx);
        g.setColour (group.colour.withAlpha (0.3f));
    }
    else
    {
        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::headerColourId));
    }
    g.fillRect (bounds);

    // Track name or number
    auto name = trackLayout.getTrackName (physTrack);
    if (name.isEmpty())
        name = juce::String::formatted ("T%02d", physTrack + 1);

    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::textColourId));
    g.setFont (lookAndFeel.getMonoFont (14.0f));
    g.drawText (name, bounds.reduced (4, 0), juce::Justification::centred);

    // Bottom line
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (bounds.getBottom() - 1, static_cast<float> (bounds.getX()),
                          static_cast<float> (bounds.getRight()));
}

//==============================================================================
// EQ Section: 3 vertical bars for Low/Mid/High + frequency readout
//==============================================================================

void MixerComponent::paintEqSection (juce::Graphics& g, const TrackMixState& state,
                                      juce::Rectangle<int> bounds, bool /*isSelected*/, int selectedParam)
{
    auto inner = bounds.reduced (4, 2);

    // Draw 3 EQ bars side by side
    int barWidth = (inner.getWidth() - 8) / 3;
    auto volumeCol = lookAndFeel.findColour (TrackerLookAndFeel::volumeColourId);
    auto selCol = lookAndFeel.findColour (TrackerLookAndFeel::fxColourId);

    struct EqBand { const char* label; double value; int idx; };
    EqBand bands[] = {
        { "L", state.eqLowGain,  0 },
        { "M", state.eqMidGain,  1 },
        { "H", state.eqHighGain, 2 }
    };

    for (int i = 0; i < 3; ++i)
    {
        int x = inner.getX() + i * (barWidth + 4);
        auto barArea = juce::Rectangle<int> (x, inner.getY(), barWidth, inner.getHeight() - 18);

        bool paramSelected = (selectedParam == i);
        auto col = paramSelected ? selCol : volumeCol;
        paintVerticalBar (g, barArea, bands[i].value, -12.0, 12.0, col, true);

        // Label + value
        g.setFont (lookAndFeel.getMonoFont (10.0f));
        g.setColour (paramSelected ? selCol : lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.6f));
        juce::String valStr = (bands[i].value >= 0.0 ? "+" : "") + juce::String (bands[i].value, 1);
        g.drawText (bands[i].label + juce::String (" ") + valStr, x, barArea.getBottom() + 1, barWidth, 16, juce::Justification::centred);
    }

    // Mid frequency readout (param index 3)
    if (selectedParam == 3)
    {
        g.setFont (lookAndFeel.getMonoFont (10.0f));
        g.setColour (selCol);
        juce::String freqStr = juce::String (static_cast<int> (state.eqMidFreq)) + "Hz";
        g.drawText (freqStr, inner.getX(), inner.getBottom() - 12, inner.getWidth(), 10,
                    juce::Justification::centred);
    }
}

//==============================================================================
// Compressor Section: 4 small knobs (Thr, Rat, Att, Rel)
//==============================================================================

void MixerComponent::paintCompSection (juce::Graphics& g, const TrackMixState& state,
                                        juce::Rectangle<int> bounds, bool /*isSelected*/, int selectedParam)
{
    auto inner = bounds.reduced (2, 2);
    auto selCol = lookAndFeel.findColour (TrackerLookAndFeel::fxColourId);
    auto textCol = lookAndFeel.findColour (TrackerLookAndFeel::textColourId);

    int knobSize = (inner.getWidth() - 6) / 2;
    int knobH = (inner.getHeight() - 2) / 2;

    struct CompParam { const char* label; double value; double minV; double maxV; int idx; };
    CompParam params[] = {
        { "THR", state.compThreshold, -60.0, 0.0, 0 },
        { "RAT", state.compRatio,     1.0,   20.0, 1 },
        { "ATT", state.compAttack,    0.1,   100.0, 2 },
        { "REL", state.compRelease,   10.0,  1000.0, 3 }
    };

    for (int i = 0; i < 4; ++i)
    {
        int col = i % 2;
        int row = i / 2;
        int x = inner.getX() + col * (knobSize + 3);
        int y = inner.getY() + row * knobH;

        auto area = juce::Rectangle<int> (x, y, knobSize, knobH);
        bool sel = (selectedParam == i);
        auto colour = sel ? selCol : textCol.withAlpha (0.5f);

        // Build value text for the knob label
        juce::String valueStr;
        switch (i)
        {
            case 0: valueStr = juce::String (static_cast<int> (params[i].value)) + "dB"; break;  // Threshold
            case 1: valueStr = juce::String (params[i].value, 1) + ":1"; break;                   // Ratio
            case 2: valueStr = juce::String (params[i].value, 1) + "ms"; break;                   // Attack
            case 3: valueStr = juce::String (static_cast<int> (params[i].value)) + "ms"; break;    // Release
        }

        paintKnob (g, area, params[i].value, params[i].minV, params[i].maxV, colour, valueStr);
    }
}

//==============================================================================
// Inserts Section: rows for insert plugins + add button
//==============================================================================

void MixerComponent::paintInsertsSection (juce::Graphics& g, int physTrack,
                                           juce::Rectangle<int> bounds, bool isSelected, int selectedParam)
{
    auto inner = bounds.reduced (2, 1);
    auto selCol = lookAndFeel.findColour (TrackerLookAndFeel::fxColourId);
    auto textCol = lookAndFeel.findColour (TrackerLookAndFeel::textColourId);
    auto bgCol = lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId).brighter (0.05f);

    auto& slots = mixerState.insertSlots[static_cast<size_t> (physTrack)];
    int numSlots = static_cast<int> (slots.size());

    // Draw each insert row
    for (int i = 0; i < numSlots; ++i)
    {
        auto& slot = slots[static_cast<size_t> (i)];
        auto rowArea = juce::Rectangle<int> (inner.getX(), inner.getY() + i * kInsertRowHeight,
                                              inner.getWidth(), kInsertRowHeight);
        bool isSel = isSelected && (selectedParam == i);

        // Row background
        g.setColour (isSel ? bgCol.brighter (0.1f) : bgCol);
        g.fillRect (rowArea);

        // Bypass indicator (small dot)
        auto bypassArea = rowArea.removeFromLeft (14);
        g.setColour (slot.bypassed ? textCol.withAlpha (0.2f) : juce::Colour (0xff33aa55));
        g.fillEllipse (bypassArea.getCentreX() - 3.0f, bypassArea.getCentreY() - 3.0f, 6.0f, 6.0f);

        // Remove button (X) on the right
        auto removeArea = rowArea.removeFromRight (16);
        g.setFont (lookAndFeel.getMonoFont (10.0f));
        g.setColour (textCol.withAlpha (0.5f));
        g.drawText ("x", removeArea, juce::Justification::centred);

        // Plugin name (truncated)
        g.setFont (lookAndFeel.getMonoFont (9.0f));
        g.setColour (isSel ? selCol : textCol.withAlpha (slot.bypassed ? 0.3f : 0.7f));
        auto nameText = slot.pluginName;
        if (nameText.length() > 10)
            nameText = nameText.substring (0, 9) + "~";
        g.drawText (nameText, rowArea.reduced (1, 0), juce::Justification::centredLeft);

        // Bottom border
        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
        g.drawHorizontalLine (inner.getY() + (i + 1) * kInsertRowHeight - 1,
                              static_cast<float> (inner.getX()),
                              static_cast<float> (inner.getRight()));
    }

    // "+" add button at bottom
    int addY = inner.getY() + numSlots * kInsertRowHeight;
    auto addArea = juce::Rectangle<int> (inner.getX(), addY, inner.getWidth(), kInsertAddButtonHeight);

    bool canAdd = numSlots < kMaxInsertSlots;
    g.setColour (canAdd ? selCol.withAlpha (0.3f) : bgCol);
    g.fillRect (addArea);

    g.setFont (lookAndFeel.getMonoFont (12.0f));
    g.setColour (canAdd ? selCol : textCol.withAlpha (0.2f));
    g.drawText ("+", addArea, juce::Justification::centred);
}

//==============================================================================
// Sends Section: 2 horizontal faders (Reverb, Delay)
//==============================================================================

void MixerComponent::paintSendsSection (juce::Graphics& g, const TrackMixState& state,
                                         juce::Rectangle<int> bounds, bool /*isSelected*/, int selectedParam)
{
    auto inner = bounds.reduced (4, 2);
    auto selCol = lookAndFeel.findColour (TrackerLookAndFeel::fxColourId);
    auto sendCol = lookAndFeel.findColour (TrackerLookAndFeel::instrumentColourId);

    int rowH = inner.getHeight() / 2;

    struct SendParam { const char* label; double value; int idx; };
    SendParam sends[] = {
        { "RVB", state.reverbSend, 0 },
        { "DLY", state.delaySend,  1 }
    };

    for (int i = 0; i < 2; ++i)
    {
        int y = inner.getY() + i * rowH;
        bool sel = (selectedParam == i);

        // Label with value
        juce::String labelText = sends[i].label;
        if (sends[i].value <= -99.0)
            labelText += " off";
        else
            labelText += juce::String (" ") + juce::String (static_cast<int> (sends[i].value));

        g.setFont (lookAndFeel.getMonoFont (9.0f));
        g.setColour (sel ? selCol : lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.5f));
        g.drawText (labelText, inner.getX(), y, 40, rowH, juce::Justification::centredLeft);

        auto barArea = juce::Rectangle<int> (inner.getX() + 40, y + 3, inner.getWidth() - 42, rowH - 6);
        auto col = sel ? selCol : sendCol;
        paintHorizontalBar (g, barArea, sends[i].value, -100.0, 0.0, col);
    }
}

//==============================================================================
// Pan Section: horizontal bar, center-zero
//==============================================================================

void MixerComponent::paintPanSection (juce::Graphics& g, const TrackMixState& state,
                                       juce::Rectangle<int> bounds, bool isSelected)
{
    auto inner = bounds.reduced (4, 3);
    auto selCol = lookAndFeel.findColour (TrackerLookAndFeel::fxColourId);
    auto panCol = lookAndFeel.findColour (TrackerLookAndFeel::instrumentColourId);

    juce::String panLabel;
    if (state.pan == 0)
        panLabel = "PAN C";
    else if (state.pan < 0)
        panLabel = "PAN L" + juce::String (-state.pan);
    else
        panLabel = "PAN R" + juce::String (state.pan);

    g.setFont (lookAndFeel.getMonoFont (9.0f));
    g.setColour (isSelected ? selCol : lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.5f));
    g.drawText (panLabel, inner.getX(), inner.getY(), 44, inner.getHeight(), juce::Justification::centredLeft);

    auto barArea = juce::Rectangle<int> (inner.getX() + 44, inner.getY() + 2,
                                          inner.getWidth() - 46, inner.getHeight() - 4);
    paintHorizontalBar (g, barArea, static_cast<double> (state.pan), -50.0, 50.0,
                        isSelected ? selCol : panCol, true);
}

//==============================================================================
// Volume Fader: large vertical bar
//==============================================================================

void MixerComponent::paintVolumeFader (juce::Graphics& g, const TrackMixState& state,
                                        juce::Rectangle<int> bounds, bool isSelected,
                                        float peakLinear)
{
    auto inner = bounds.reduced (6, 4);
    auto selCol = lookAndFeel.findColour (TrackerLookAndFeel::fxColourId);
    auto volCol = lookAndFeel.findColour (TrackerLookAndFeel::volumeColourId);

    // Volume value text at top
    g.setFont (lookAndFeel.getMonoFont (12.0f));
    g.setColour (isSelected ? selCol : lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.7f));

    juce::String volText;
    if (state.volume <= -99.0)
        volText = "-inf";
    else
        volText = juce::String (state.volume, 1) + "dB";
    g.drawText (volText, inner.getX(), inner.getY(), inner.getWidth(), 12, juce::Justification::centred);

    // Fader bar
    auto faderArea = inner.withTrimmedTop (14).withTrimmedBottom (2);

    // Background track
    auto trackArea = faderArea.reduced (faderArea.getWidth() / 2 - 6, 0);
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId).brighter (0.1f));
    g.fillRect (trackArea);

    // ── Peak level meter (behind fader) ──
    if (peakLinear > 0.0f)
    {
        // Convert linear peak to dB, then to normalized position on the fader
        float peakDb = juce::Decibels::gainToDecibels (peakLinear, -100.0f);
        double peakNorm = (static_cast<double> (peakDb) - (-100.0)) / (12.0 - (-100.0));
        peakNorm = juce::jlimit (0.0, 1.0, peakNorm);
        int meterHeight = static_cast<int> (peakNorm * static_cast<double> (faderArea.getHeight()));

        // Draw meter with gradient: green → yellow → red
        auto meterArea = juce::Rectangle<int> (
            faderArea.getX() + 1, faderArea.getBottom() - meterHeight,
            faderArea.getWidth() - 2, meterHeight);

        juce::Colour meterCol;
        if (peakDb > 0.0f)
            meterCol = juce::Colour (0xffcc3333);       // red (clipping)
        else if (peakDb > -6.0f)
            meterCol = juce::Colour (0xffccaa33);       // yellow (hot)
        else
            meterCol = juce::Colour (0xff33aa55);       // green (normal)

        g.setColour (meterCol.withAlpha (0.25f));
        g.fillRect (meterArea);

        // Thin bright line at peak position
        g.setColour (meterCol.withAlpha (0.6f));
        g.drawHorizontalLine (faderArea.getBottom() - meterHeight,
                              static_cast<float> (faderArea.getX() + 1),
                              static_cast<float> (faderArea.getRight() - 1));
    }

    // dB scale markings
    g.setFont (lookAndFeel.getMonoFont (9.0f));
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.3f));
    double markings[] = { 12.0, 6.0, 0.0, -6.0, -12.0, -24.0, -48.0 };
    for (auto dB : markings)
    {
        double norm = (dB - (-100.0)) / (12.0 - (-100.0));
        int y = faderArea.getBottom() - static_cast<int> (norm * static_cast<double> (faderArea.getHeight()));
        g.drawHorizontalLine (y, static_cast<float> (faderArea.getX()),
                              static_cast<float> (faderArea.getX() + 3));
        g.drawHorizontalLine (y, static_cast<float> (faderArea.getRight() - 3),
                              static_cast<float> (faderArea.getRight()));
    }

    // Filled portion (volume fader)
    double norm = (state.volume - (-100.0)) / (12.0 - (-100.0));
    norm = juce::jlimit (0.0, 1.0, norm);
    int fillHeight = static_cast<int> (norm * static_cast<double> (faderArea.getHeight()));

    auto fillCol = isSelected ? selCol : volCol;
    g.setColour (fillCol.withAlpha (0.7f));
    g.fillRect (trackArea.getX(), faderArea.getBottom() - fillHeight,
                trackArea.getWidth(), fillHeight);

    // Fader handle
    int handleY = faderArea.getBottom() - fillHeight;
    g.setColour (fillCol);
    g.fillRect (faderArea.getX(), handleY - 2, faderArea.getWidth(), 4);
}

//==============================================================================
// Mute/Solo buttons
//==============================================================================

void MixerComponent::paintMuteSolo (juce::Graphics& g, const TrackMixState& state,
                                     juce::Rectangle<int> bounds, int /*physTrack*/)
{
    int halfW = bounds.getWidth() / 2;

    // Mute button
    auto muteArea = juce::Rectangle<int> (bounds.getX() + 2, bounds.getY() + 2,
                                           halfW - 3, bounds.getHeight() - 4);
    auto muteCol = lookAndFeel.findColour (TrackerLookAndFeel::muteColourId);
    g.setColour (state.muted ? muteCol : muteCol.withAlpha (0.15f));
    g.fillRoundedRectangle (muteArea.toFloat(), 2.0f);
    g.setColour (state.muted ? juce::Colours::white : lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.4f));
    g.setFont (lookAndFeel.getMonoFont (13.0f));
    g.drawText ("M", muteArea, juce::Justification::centred);

    // Solo button
    auto soloArea = juce::Rectangle<int> (bounds.getX() + halfW + 1, bounds.getY() + 2,
                                           halfW - 3, bounds.getHeight() - 4);
    auto soloCol = lookAndFeel.findColour (TrackerLookAndFeel::soloColourId);
    g.setColour (state.soloed ? soloCol : soloCol.withAlpha (0.15f));
    g.fillRoundedRectangle (soloArea.toFloat(), 2.0f);
    g.setColour (state.soloed ? juce::Colours::black : lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.4f));
    g.setFont (lookAndFeel.getMonoFont (13.0f));
    g.drawText ("S", soloArea, juce::Justification::centred);

    // Top separator
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (bounds.getY(), static_cast<float> (bounds.getX()),
                          static_cast<float> (bounds.getRight()));
}

//==============================================================================
// Generic bar/knob painting
//==============================================================================

void MixerComponent::paintVerticalBar (juce::Graphics& g, juce::Rectangle<int> area,
                                        double value, double minVal, double maxVal,
                                        juce::Colour colour, bool bipolar)
{
    // Background
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId).brighter (0.05f));
    g.fillRect (area);

    double range = maxVal - minVal;
    if (range <= 0.0) return;

    double norm = (value - minVal) / range;
    norm = juce::jlimit (0.0, 1.0, norm);

    if (bipolar)
    {
        // Draw from center
        double centerNorm = (0.0 - minVal) / range;
        int centerY = area.getBottom() - static_cast<int> (centerNorm * static_cast<double> (area.getHeight()));

        int valueY = area.getBottom() - static_cast<int> (norm * static_cast<double> (area.getHeight()));

        g.setColour (colour.withAlpha (0.6f));
        if (valueY < centerY)
            g.fillRect (area.getX() + 1, valueY, area.getWidth() - 2, centerY - valueY);
        else
            g.fillRect (area.getX() + 1, centerY, area.getWidth() - 2, valueY - centerY);

        // Center line
        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.2f));
        g.drawHorizontalLine (centerY, static_cast<float> (area.getX()),
                              static_cast<float> (area.getRight()));
    }
    else
    {
        int fillH = static_cast<int> (norm * static_cast<double> (area.getHeight()));
        g.setColour (colour.withAlpha (0.6f));
        g.fillRect (area.getX() + 1, area.getBottom() - fillH, area.getWidth() - 2, fillH);
    }

    // Border
    g.setColour (colour.withAlpha (0.3f));
    g.drawRect (area, 1);
}

void MixerComponent::paintHorizontalBar (juce::Graphics& g, juce::Rectangle<int> area,
                                          double value, double minVal, double maxVal,
                                          juce::Colour colour, bool bipolar)
{
    // Background
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId).brighter (0.05f));
    g.fillRect (area);

    double range = maxVal - minVal;
    if (range <= 0.0) return;

    double norm = (value - minVal) / range;
    norm = juce::jlimit (0.0, 1.0, norm);

    if (bipolar)
    {
        double centerNorm = (0.0 - minVal) / range;
        int centerX = area.getX() + static_cast<int> (centerNorm * static_cast<double> (area.getWidth()));
        int valueX = area.getX() + static_cast<int> (norm * static_cast<double> (area.getWidth()));

        g.setColour (colour.withAlpha (0.6f));
        if (valueX > centerX)
            g.fillRect (centerX, area.getY() + 1, valueX - centerX, area.getHeight() - 2);
        else
            g.fillRect (valueX, area.getY() + 1, centerX - valueX, area.getHeight() - 2);

        // Center line
        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.2f));
        g.drawVerticalLine (centerX, static_cast<float> (area.getY()),
                            static_cast<float> (area.getBottom()));
    }
    else
    {
        int fillW = static_cast<int> (norm * static_cast<double> (area.getWidth()));
        g.setColour (colour.withAlpha (0.6f));
        g.fillRect (area.getX(), area.getY() + 1, fillW, area.getHeight() - 2);
    }

    // Border
    g.setColour (colour.withAlpha (0.3f));
    g.drawRect (area, 1);
}

void MixerComponent::paintKnob (juce::Graphics& g, juce::Rectangle<int> area,
                                  double value, double minVal, double maxVal,
                                  juce::Colour colour, const juce::String& label)
{
    // Simple arc-style knob
    auto inner = area.reduced (2, 1);
    int knobDiam = juce::jmin (inner.getWidth(), inner.getHeight() - 12);
    if (knobDiam < 8) return;

    auto knobArea = juce::Rectangle<int> (inner.getCentreX() - knobDiam / 2,
                                           inner.getY() + 1, knobDiam, knobDiam);

    // Background ring
    float cx = static_cast<float> (knobArea.getCentreX());
    float cy = static_cast<float> (knobArea.getCentreY());
    float radius = static_cast<float> (knobDiam) * 0.4f;

    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId).brighter (0.1f));
    juce::Path bgArc;
    bgArc.addCentredArc (cx, cy, radius, radius, 0.0f,
                          juce::MathConstants<float>::pi * 0.75f,
                          juce::MathConstants<float>::pi * 2.25f, true);
    g.strokePath (bgArc, juce::PathStrokeType (2.0f));

    // Value arc
    double norm = (maxVal > minVal) ? juce::jlimit (0.0, 1.0, (value - minVal) / (maxVal - minVal)) : 0.0;
    float startAngle = juce::MathConstants<float>::pi * 0.75f;
    float endAngle = startAngle + static_cast<float> (norm) * juce::MathConstants<float>::pi * 1.5f;

    g.setColour (colour);
    juce::Path valArc;
    valArc.addCentredArc (cx, cy, radius, radius, 0.0f, startAngle, endAngle, true);
    g.strokePath (valArc, juce::PathStrokeType (2.5f));

    // Dot indicator
    float dotX = cx + radius * std::cos (endAngle - juce::MathConstants<float>::halfPi);
    float dotY = cy + radius * std::sin (endAngle - juce::MathConstants<float>::halfPi);
    g.fillEllipse (dotX - 1.5f, dotY - 1.5f, 3.0f, 3.0f);

    // Label
    g.setFont (lookAndFeel.getMonoFont (9.0f));
    g.setColour (colour.withAlpha (0.8f));
    g.drawText (label, inner.getX(), knobArea.getBottom() + 1, inner.getWidth(),
                inner.getBottom() - knobArea.getBottom() - 1, juce::Justification::centredTop);
}

//==============================================================================
// Hit testing
//==============================================================================

MixerComponent::HitResult MixerComponent::hitTestStrip (juce::Point<int> pos) const
{
    HitResult result;

    // Determine which strip
    int vi = scrollOffset + pos.x / (kStripWidth + kStripGap);
    if (vi < 0 || vi >= kNumTracks)
        return result;

    result.visualTrack = vi;
    auto bounds = getStripBounds (vi);

    int relY = pos.y;
    int y = 0;

    // Header
    y += kHeaderHeight;
    if (relY < y)
        return result;

    // EQ label + section
    y += kSectionLabelHeight;
    int eqStart = y;
    y += kEqSectionHeight;
    if (relY < y)
    {
        result.section = Section::EQ;
        int relEqY = relY - eqStart;

        // Bottom readout area controls mid frequency (param 3).
        if (relEqY >= (kEqSectionHeight - 18))
        {
            result.param = 3;
            return result;
        }

        // Upper area controls the 3 EQ gain bands.
        int relX = pos.x - bounds.getX();
        int barWidth = (bounds.getWidth() - 16) / 3;
        result.param = juce::jlimit (0, 2, (relX - 4) / (barWidth + 4));
        return result;
    }

    // Comp label + section
    y += kSectionLabelHeight;
    int compStart = y;
    y += kCompSectionHeight;
    if (relY < y)
    {
        result.section = Section::Comp;
        int relX = pos.x - bounds.getX();
        int relCY = relY - compStart;
        int col = (relX < bounds.getWidth() / 2) ? 0 : 1;
        int row = (relCY < kCompSectionHeight / 2) ? 0 : 1;
        result.param = row * 2 + col;
        return result;
    }

    // Inserts label + section (dynamic height)
    {
        int physTrack = trackLayout.visualToPhysical (vi);
        int insertHeight = getInsertsSectionHeight (physTrack);
        if (insertHeight > 0)
        {
            y += kSectionLabelHeight; // "INSERTS" label
            int insertsStart = y;
            y += insertHeight;
            if (relY < y)
            {
                result.section = Section::Inserts;
                int relInsertY = relY - insertsStart;
                auto& slots = mixerState.insertSlots[static_cast<size_t> (physTrack)];
                int numSlots = static_cast<int> (slots.size());

                // Check if hitting an insert row
                int slotIdx = relInsertY / kInsertRowHeight;
                if (slotIdx < numSlots)
                {
                    result.hitInsertSlot = slotIdx;
                    result.param = slotIdx;

                    // Check sub-areas within the row
                    int relX = pos.x - bounds.getX();
                    int innerLeft = bounds.getX() + 2;
                    int innerRight = bounds.getRight() - 2;
                    juce::ignoreUnused (innerLeft);

                    if (relX < 16)
                    {
                        // Bypass dot area
                        result.hitInsertBypass = true;
                    }
                    else if (relX > innerRight - bounds.getX() - 18)
                    {
                        // Remove (x) area
                        result.hitInsertRemove = true;
                    }
                    else
                    {
                        // Name area -> open editor
                        result.hitInsertOpen = true;
                    }
                }
                else
                {
                    // + button
                    result.hitInsertAdd = true;
                    result.param = -1;
                }
                return result;
            }
        }
    }

    // Sends label + section
    y += kSectionLabelHeight;
    int sendsStart = y;
    y += kSendsSectionHeight;
    if (relY < y)
    {
        result.section = Section::Sends;
        result.param = (relY - sendsStart < kSendsSectionHeight / 2) ? 0 : 1;
        return result;
    }

    y += 1; // separator

    // Pan
    y += kPanSectionHeight;
    if (relY < y)
    {
        result.section = Section::Pan;
        result.param = 0;
        return result;
    }

    y += 1; // separator

    // Check mute/solo at bottom
    int muteSoloTop = getHeight() - kMuteSoloHeight;
    if (relY >= muteSoloTop)
    {
        int relX = pos.x - bounds.getX();
        if (relX < bounds.getWidth() / 2)
            result.hitMute = true;
        else
            result.hitSolo = true;
        return result;
    }

    // Volume (everything between pan and mute/solo)
    result.section = Section::Volume;
    result.param = 0;
    return result;
}

//==============================================================================
// Parameter access
//==============================================================================

double MixerComponent::getParamValue (int physTrack, Section section, int param) const
{
    auto& state = mixerState.tracks[static_cast<size_t> (physTrack)];

    switch (section)
    {
        case Section::EQ:
            switch (param)
            {
                case 0: return state.eqLowGain;
                case 1: return state.eqMidGain;
                case 2: return state.eqHighGain;
                case 3: return state.eqMidFreq;
                default: return 0.0;
            }
        case Section::Comp:
            switch (param)
            {
                case 0: return state.compThreshold;
                case 1: return state.compRatio;
                case 2: return state.compAttack;
                case 3: return state.compRelease;
                default: return 0.0;
            }
        case Section::Inserts:
            return 0.0;  // Inserts don't have traditional params
        case Section::Sends:
            switch (param)
            {
                case 0: return state.reverbSend;
                case 1: return state.delaySend;
                default: return 0.0;
            }
        case Section::Pan:
            return static_cast<double> (state.pan);
        case Section::Volume:
            return state.volume;
    }
    return 0.0;
}

void MixerComponent::setParamValue (int physTrack, Section section, int param, double value)
{
    auto& state = mixerState.tracks[static_cast<size_t> (physTrack)];

    switch (section)
    {
        case Section::EQ:
            switch (param)
            {
                case 0: state.eqLowGain  = juce::jlimit (-12.0, 12.0, value); break;
                case 1: state.eqMidGain  = juce::jlimit (-12.0, 12.0, value); break;
                case 2: state.eqHighGain = juce::jlimit (-12.0, 12.0, value); break;
                case 3: state.eqMidFreq  = juce::jlimit (200.0, 8000.0, value); break;
                default: break;
            }
            break;
        case Section::Comp:
            switch (param)
            {
                case 0: state.compThreshold = juce::jlimit (-60.0, 0.0, value); break;
                case 1: state.compRatio     = juce::jlimit (1.0, 20.0, value); break;
                case 2: state.compAttack    = juce::jlimit (0.1, 100.0, value); break;
                case 3: state.compRelease   = juce::jlimit (10.0, 1000.0, value); break;
                default: break;
            }
            break;
        case Section::Inserts:
            break;  // Inserts don't have traditional params
        case Section::Sends:
            switch (param)
            {
                case 0: state.reverbSend = juce::jlimit (-100.0, 0.0, value); break;
                case 1: state.delaySend  = juce::jlimit (-100.0, 0.0, value); break;
                default: break;
            }
            break;
        case Section::Pan:
            state.pan = juce::jlimit (-50, 50, static_cast<int> (value));
            break;
        case Section::Volume:
            state.volume = juce::jlimit (-100.0, 12.0, value);
            break;
    }

    if (onMixStateChanged)
        onMixStateChanged();
}

double MixerComponent::getParamMin (Section section, int param) const
{
    switch (section)
    {
        case Section::EQ:      return param == 3 ? 200.0 : -12.0;
        case Section::Comp:
            switch (param)
            {
                case 0: return -60.0;
                case 1: return 1.0;
                case 2: return 0.1;
                case 3: return 10.0;
                default: return 0.0;
            }
        case Section::Inserts: return 0.0;
        case Section::Sends:   return -100.0;
        case Section::Pan:     return -50.0;
        case Section::Volume:  return -100.0;
    }
    return 0.0;
}

double MixerComponent::getParamMax (Section section, int param) const
{
    switch (section)
    {
        case Section::EQ:      return param == 3 ? 8000.0 : 12.0;
        case Section::Comp:
            switch (param)
            {
                case 0: return 0.0;
                case 1: return 20.0;
                case 2: return 100.0;
                case 3: return 1000.0;
                default: return 1.0;
            }
        case Section::Inserts: return 1.0;
        case Section::Sends:   return 0.0;
        case Section::Pan:     return 50.0;
        case Section::Volume:  return 12.0;
    }
    return 1.0;
}

double MixerComponent::getParamStep (Section section, int param) const
{
    switch (section)
    {
        case Section::EQ:      return param == 3 ? 50.0 : 0.5;
        case Section::Comp:
            switch (param)
            {
                case 0: return 1.0;
                case 1: return 0.5;
                case 2: return 1.0;
                case 3: return 10.0;
                default: return 0.1;
            }
        case Section::Inserts: return 1.0;
        case Section::Sends:   return 2.0;
        case Section::Pan:     return 1.0;
        case Section::Volume:  return 0.5;
    }
    return 1.0;
}

int MixerComponent::getParamCountForSection (Section section) const
{
    switch (section)
    {
        case Section::EQ:      return 4;  // Low, Mid, High, MidFreq
        case Section::Comp:    return 4;  // Threshold, Ratio, Attack, Release
        case Section::Inserts:
        {
            int physTrack = trackLayout.visualToPhysical (selectedTrack);
            auto& slots = mixerState.insertSlots[static_cast<size_t> (physTrack)];
            return juce::jmax (1, static_cast<int> (slots.size()));
        }
        case Section::Sends:   return 2;  // Reverb, Delay
        case Section::Pan:     return 1;
        case Section::Volume:  return 1;
    }
    return 1;
}

//==============================================================================
// Keyboard handling
//==============================================================================

bool MixerComponent::keyPressed (const juce::KeyPress& key)
{
    auto keyCode = key.getKeyCode();
    bool shift = key.getModifiers().isShiftDown();

    // Up/Down: navigate params/sections within strip (vertical layout)
    if (keyCode == juce::KeyPress::upKey && ! shift)
    {
        if (currentParam > 0)
            currentParam--;
        else
            prevSection();
        repaint();
        return true;
    }
    if (keyCode == juce::KeyPress::downKey && ! shift)
    {
        if (currentParam < getParamCountForSection (currentSection) - 1)
            currentParam++;
        else
            nextSection();
        repaint();
        return true;
    }

    // Left/Right: adjust value
    if (keyCode == juce::KeyPress::rightKey)
    {
        adjustCurrentParam (shift ? 5.0 : 1.0);
        repaint();
        return true;
    }
    if (keyCode == juce::KeyPress::leftKey)
    {
        adjustCurrentParam (shift ? -5.0 : -1.0);
        repaint();
        return true;
    }

    // Tab/Shift+Tab: select track
    if (keyCode == juce::KeyPress::tabKey)
    {
        if (shift)
        {
            if (selectedTrack > 0)
                selectedTrack--;
        }
        else
        {
            if (selectedTrack < kNumTracks - 1)
                selectedTrack++;
        }
        ensureTrackVisible();
        repaint();
        return true;
    }

    // M: toggle mute
    if (key.getTextCharacter() == 'm' || key.getTextCharacter() == 'M')
    {
        int physTrack = trackLayout.visualToPhysical (selectedTrack);
        auto& state = mixerState.tracks[static_cast<size_t> (physTrack)];
        state.muted = ! state.muted;
        if (onMuteChanged)
            onMuteChanged (physTrack, state.muted);
        repaint();
        return true;
    }

    // S: toggle solo
    if (key.getTextCharacter() == 's' || key.getTextCharacter() == 'S')
    {
        int physTrack = trackLayout.visualToPhysical (selectedTrack);
        auto& state = mixerState.tracks[static_cast<size_t> (physTrack)];
        state.soloed = ! state.soloed;
        if (onSoloChanged)
            onSoloChanged (physTrack, state.soloed);
        repaint();
        return true;
    }

    return false;
}

//==============================================================================
// Mouse handling
//==============================================================================

void MixerComponent::mouseDown (const juce::MouseEvent& event)
{
    auto hit = hitTestStrip (event.getPosition());
    if (hit.visualTrack < 0)
        return;

    // Handle mute/solo clicks
    if (hit.hitMute)
    {
        int physTrack = trackLayout.visualToPhysical (hit.visualTrack);
        auto& state = mixerState.tracks[static_cast<size_t> (physTrack)];
        state.muted = ! state.muted;
        if (onMuteChanged)
            onMuteChanged (physTrack, state.muted);
        repaint();
        return;
    }
    if (hit.hitSolo)
    {
        int physTrack = trackLayout.visualToPhysical (hit.visualTrack);
        auto& state = mixerState.tracks[static_cast<size_t> (physTrack)];
        state.soloed = ! state.soloed;
        if (onSoloChanged)
            onSoloChanged (physTrack, state.soloed);
        repaint();
        return;
    }

    // Handle insert-specific clicks
    if (hit.section == Section::Inserts)
    {
        int physTrack = trackLayout.visualToPhysical (hit.visualTrack);

        if (hit.hitInsertAdd)
        {
            if (onAddInsertClicked)
                onAddInsertClicked (physTrack);
            repaint();
            return;
        }
        if (hit.hitInsertRemove && hit.hitInsertSlot >= 0)
        {
            if (onRemoveInsertClicked)
                onRemoveInsertClicked (physTrack, hit.hitInsertSlot);
            repaint();
            return;
        }
        if (hit.hitInsertBypass && hit.hitInsertSlot >= 0)
        {
            auto& slots = mixerState.insertSlots[static_cast<size_t> (physTrack)];
            if (hit.hitInsertSlot < static_cast<int> (slots.size()))
            {
                bool newState = ! slots[static_cast<size_t> (hit.hitInsertSlot)].bypassed;
                if (onInsertBypassToggled)
                    onInsertBypassToggled (physTrack, hit.hitInsertSlot, newState);
            }
            repaint();
            return;
        }
        if (hit.hitInsertOpen && hit.hitInsertSlot >= 0)
        {
            if (onOpenInsertEditor)
                onOpenInsertEditor (physTrack, hit.hitInsertSlot);
            repaint();
            return;
        }

        // Fallthrough: select insert section
        selectedTrack = hit.visualTrack;
        currentSection = Section::Inserts;
        if (hit.param >= 0)
            currentParam = hit.param;
        repaint();
        return;
    }

    // Select track and param
    selectedTrack = hit.visualTrack;
    currentSection = hit.section;
    if (hit.param >= 0)
        currentParam = hit.param;

    // Don't start drag for Inserts section
    if (hit.section == Section::Inserts)
    {
        repaint();
        return;
    }

    // Start drag
    dragging = true;
    dragTrack = hit.visualTrack;
    dragSection = hit.section;
    dragParam = (hit.param >= 0) ? hit.param : 0;
    dragStartY = event.getPosition().y;

    int physTrack = trackLayout.visualToPhysical (dragTrack);
    dragStartValue = getParamValue (physTrack, dragSection, dragParam);

    repaint();
}

void MixerComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (! dragging || dragTrack < 0)
        return;

    int physTrack = trackLayout.visualToPhysical (dragTrack);
    double minVal = getParamMin (dragSection, dragParam);
    double maxVal = getParamMax (dragSection, dragParam);
    double range = maxVal - minVal;

    // Vertical drag: up = increase, down = decrease
    double pixelRange = 200.0; // full range over 200 pixels
    double delta = static_cast<double> (dragStartY - event.getPosition().y) / pixelRange * range;

    double newValue = dragStartValue + delta;
    newValue = juce::jlimit (minVal, maxVal, newValue);

    setParamValue (physTrack, dragSection, dragParam, newValue);
    repaint();
}

void MixerComponent::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    dragTrack = -1;
}

void MixerComponent::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    auto hit = hitTestStrip (event.getPosition());
    if (hit.visualTrack < 0)
    {
        // Horizontal scroll if no strip hit
        scrollOffset = juce::jlimit (0, juce::jmax (0, kNumTracks - getVisibleStripCount()),
                                      scrollOffset - static_cast<int> (wheel.deltaY * 3.0f));
        repaint();
        return;
    }

    // Adjust parameter under cursor
    selectedTrack = hit.visualTrack;
    currentSection = hit.section;
    if (hit.param >= 0)
        currentParam = hit.param;

    int physTrack = trackLayout.visualToPhysical (hit.visualTrack);
    double step = getParamStep (hit.section, (hit.param >= 0) ? hit.param : 0);
    double delta = (wheel.deltaY > 0.0f ? 1.0 : -1.0) * step;

    int paramIdx = (hit.param >= 0) ? hit.param : 0;
    double current = getParamValue (physTrack, hit.section, paramIdx);
    double minVal = getParamMin (hit.section, paramIdx);
    double maxVal = getParamMax (hit.section, paramIdx);
    double newVal = juce::jlimit (minVal, maxVal, current + delta);

    setParamValue (physTrack, hit.section, paramIdx, newVal);
    repaint();
}

//==============================================================================
// Navigation helpers
//==============================================================================

void MixerComponent::adjustCurrentParam (double direction)
{
    int physTrack = trackLayout.visualToPhysical (selectedTrack);
    double step = getParamStep (currentSection, currentParam) * direction;
    double current = getParamValue (physTrack, currentSection, currentParam);
    double minVal = getParamMin (currentSection, currentParam);
    double maxVal = getParamMax (currentSection, currentParam);
    double newVal = juce::jlimit (minVal, maxVal, current + step);
    setParamValue (physTrack, currentSection, currentParam, newVal);
}

void MixerComponent::nextSection()
{
    switch (currentSection)
    {
        case Section::EQ:      currentSection = Section::Comp;    break;
        case Section::Comp:    currentSection = Section::Inserts; break;
        case Section::Inserts: currentSection = Section::Sends;   break;
        case Section::Sends:   currentSection = Section::Pan;     break;
        case Section::Pan:     currentSection = Section::Volume;  break;
        case Section::Volume:  currentSection = Section::EQ;      break;
    }
    currentParam = 0;
}

void MixerComponent::prevSection()
{
    switch (currentSection)
    {
        case Section::EQ:      currentSection = Section::Volume;  break;
        case Section::Comp:    currentSection = Section::EQ;      break;
        case Section::Inserts: currentSection = Section::Comp;    break;
        case Section::Sends:   currentSection = Section::Inserts; break;
        case Section::Pan:     currentSection = Section::Sends;   break;
        case Section::Volume:  currentSection = Section::Pan;     break;
    }
    currentParam = getParamCountForSection (currentSection) - 1;
}

void MixerComponent::ensureTrackVisible()
{
    int visCount = getVisibleStripCount();
    if (selectedTrack < scrollOffset)
        scrollOffset = selectedTrack;
    else if (selectedTrack >= scrollOffset + visCount)
        scrollOffset = selectedTrack - visCount + 1;
    scrollOffset = juce::jlimit (0, juce::jmax (0, kNumTracks - visCount), scrollOffset);
}

//==============================================================================
// External state updates
//==============================================================================

void MixerComponent::setTrackMuteState (int track, bool muted)
{
    if (track >= 0 && track < kNumTracks)
    {
        mixerState.tracks[static_cast<size_t> (track)].muted = muted;
        repaint();
    }
}

void MixerComponent::setTrackSoloState (int track, bool soloed)
{
    if (track >= 0 && track < kNumTracks)
    {
        mixerState.tracks[static_cast<size_t> (track)].soloed = soloed;
        repaint();
    }
}
