#include "MixerComponent.h"

MixerComponent::MixerComponent (TrackerLookAndFeel& lnf, MixerState& state, TrackLayout& layout)
    : lookAndFeel (lnf), mixerState (state), trackLayout (layout)
{
    setWantsKeyboardFocus (true);
    trackPeakLevels.fill (0.0f);
}

//==============================================================================
// Strip type identification
//==============================================================================

int MixerComponent::getTotalStripCount() const
{
    // kNumTracks regular tracks + 2 send returns + N group buses + 1 master
    int numGroups = trackLayout.getNumGroups();
    return kNumTracks + 2 + numGroups + 1;
}

MixerComponent::StripInfo MixerComponent::getStripInfo (int visualIndex) const
{
    StripInfo info;

    if (visualIndex < kNumTracks)
    {
        info.type = StripType::Track;
        info.index = trackLayout.visualToPhysical (visualIndex);
        return info;
    }

    int offset = kNumTracks;

    // Delay return
    if (visualIndex == offset)
    {
        info.type = StripType::DelayReturn;
        info.index = 0;
        return info;
    }
    offset++;

    // Reverb return
    if (visualIndex == offset)
    {
        info.type = StripType::ReverbReturn;
        info.index = 1;
        return info;
    }
    offset++;

    // Group buses
    int numGroups = trackLayout.getNumGroups();
    if (visualIndex < offset + numGroups)
    {
        info.type = StripType::GroupBus;
        info.index = visualIndex - offset;
        return info;
    }
    offset += numGroups;

    // Master
    info.type = StripType::Master;
    info.index = 0;
    return info;
}

bool MixerComponent::isSeparatorPosition (int visualIndex) const
{
    // Separators before send returns, group buses section, and master
    if (visualIndex == kNumTracks) return true;  // before delay return
    int numGroups = trackLayout.getNumGroups();
    if (numGroups > 0 && visualIndex == kNumTracks + 2) return true;  // before group buses
    if (visualIndex == kNumTracks + 2 + numGroups) return true;  // before master
    return false;
}

int MixerComponent::getMasterInsertsSectionHeight() const
{
    int numSlots = static_cast<int> (mixerState.masterInsertSlots.size());
    return numSlots * kInsertRowHeight + kInsertAddButtonHeight;
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
    int x = 0;
    for (int i = scrollOffset; i < visualTrack; ++i)
    {
        x += kStripWidth + kStripGap;
        if (isSeparatorPosition (i))
            x += kSeparatorWidth;
    }
    return x;
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

    int totalStrips = getTotalStripCount();
    int visibleCount = getVisibleStripCount();

    for (int vi = scrollOffset; vi < juce::jmin (scrollOffset + visibleCount + 2, totalStrips); ++vi)
    {
        auto bounds = getStripBounds (vi);
        if (bounds.getRight() < 0 || bounds.getX() > getWidth())
            continue;

        // Draw separator before special sections
        if (isSeparatorPosition (vi))
        {
            int sepX = bounds.getX() - kSeparatorWidth;
            g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId).brighter (0.15f));
            g.fillRect (sepX, 0, kSeparatorWidth, getHeight());
        }

        auto info = getStripInfo (vi);
        switch (info.type)
        {
            case StripType::Track:
                paintStrip (g, vi, bounds);
                break;
            case StripType::DelayReturn:
            case StripType::ReverbReturn:
                paintSendReturnStrip (g, info.index, bounds, vi == selectedTrack);
                break;
            case StripType::GroupBus:
                paintGroupBusStrip (g, info.index, bounds, vi == selectedTrack);
                break;
            case StripType::Master:
                paintMasterStrip (g, bounds, vi == selectedTrack);
                break;
        }
    }

    // Draw scroll indicators if needed
    if (scrollOffset > 0)
    {
        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::fxColourId).withAlpha (0.6f));
        g.setFont (lookAndFeel.getMonoFont (13.0f));
        g.drawText ("<", 0, getHeight() / 2 - 10, 12, 20, juce::Justification::centred);
    }
    if (scrollOffset + visibleCount < totalStrips)
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
// Send Return Strip
//==============================================================================

void MixerComponent::paintSendReturnStrip (juce::Graphics& g, int returnIndex,
                                            juce::Rectangle<int> bounds, bool isSelected)
{
    auto& sr = mixerState.sendReturns[static_cast<size_t> (returnIndex)];

    // Strip background
    auto stripBg = lookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId).brighter (0.04f);
    if (isSelected)
        stripBg = stripBg.brighter (0.06f);
    g.setColour (stripBg);
    g.fillRect (bounds);

    // Strip border
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawVerticalLine (bounds.getRight(), 0.0f, static_cast<float> (getHeight()));

    auto r = bounds;

    // Header
    auto headerArea = r.removeFromTop (kHeaderHeight);
    auto sendCol = juce::Colour (returnIndex == 0 ? 0xff5577aa : 0xff7755aa);
    g.setColour (sendCol.withAlpha (0.3f));
    g.fillRect (headerArea);
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::textColourId));
    g.setFont (lookAndFeel.getMonoFont (14.0f));
    g.drawText (returnIndex == 0 ? "DELAY" : "REVERB", headerArea.reduced (4, 0), juce::Justification::centred);
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (headerArea.getBottom() - 1, static_cast<float> (bounds.getX()),
                          static_cast<float> (bounds.getRight()));

    // EQ section
    auto eqLabelArea = r.removeFromTop (kSectionLabelHeight);
    g.setFont (lookAndFeel.getMonoFont (12.0f));
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::fxColourId).withAlpha (0.6f));
    g.drawText ("EQ", eqLabelArea, juce::Justification::centred);

    auto eqArea = r.removeFromTop (kEqSectionHeight);
    paintGenericEqSection (g, sr.eqLowGain, sr.eqMidGain, sr.eqHighGain, sr.eqMidFreq,
                           eqArea, isSelected, (isSelected && currentSection == Section::EQ) ? currentParam : -1);

    // Separator
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (r.getY(), static_cast<float> (bounds.getX()),
                          static_cast<float> (bounds.getRight()));
    r.removeFromTop (1);

    // Pan
    auto panArea = r.removeFromTop (kPanSectionHeight);
    paintGenericPanSection (g, sr.pan, panArea, isSelected && currentSection == Section::Pan);

    // Separator
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (r.getY(), static_cast<float> (bounds.getX()),
                          static_cast<float> (bounds.getRight()));
    r.removeFromTop (1);

    // Mute button (no solo for send returns)
    auto muteSoloArea = r.removeFromBottom (kMuteSoloHeight);
    paintGenericMuteSolo (g, sr.muted, false, muteSoloArea, false);

    // Volume fader fills the rest
    paintGenericVolumeFader (g, sr.volume, r, isSelected && currentSection == Section::Volume);
}

//==============================================================================
// Group Bus Strip
//==============================================================================

void MixerComponent::paintGroupBusStrip (juce::Graphics& g, int groupIndex,
                                          juce::Rectangle<int> bounds, bool isSelected)
{
    if (groupIndex < 0 || groupIndex >= trackLayout.getNumGroups())
        return;

    auto& gb = mixerState.groupBuses[static_cast<size_t> (groupIndex)];
    auto& group = trackLayout.getGroup (groupIndex);

    // Strip background
    auto stripBg = lookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId).brighter (0.04f);
    if (isSelected)
        stripBg = stripBg.brighter (0.06f);
    g.setColour (stripBg);
    g.fillRect (bounds);

    // Strip border
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawVerticalLine (bounds.getRight(), 0.0f, static_cast<float> (getHeight()));

    auto r = bounds;

    // Header with group colour
    auto headerArea = r.removeFromTop (kHeaderHeight);
    g.setColour (group.colour.withAlpha (0.4f));
    g.fillRect (headerArea);
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::textColourId));
    g.setFont (lookAndFeel.getMonoFont (14.0f));
    juce::String name = group.name.isNotEmpty() ? group.name : ("GRP " + juce::String (groupIndex + 1));
    g.drawText (name, headerArea.reduced (4, 0), juce::Justification::centred);
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (headerArea.getBottom() - 1, static_cast<float> (bounds.getX()),
                          static_cast<float> (bounds.getRight()));

    // EQ section
    auto eqLabelArea = r.removeFromTop (kSectionLabelHeight);
    g.setFont (lookAndFeel.getMonoFont (12.0f));
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::fxColourId).withAlpha (0.6f));
    g.drawText ("EQ", eqLabelArea, juce::Justification::centred);

    auto eqArea = r.removeFromTop (kEqSectionHeight);
    paintGenericEqSection (g, gb.eqLowGain, gb.eqMidGain, gb.eqHighGain, gb.eqMidFreq,
                           eqArea, isSelected, (isSelected && currentSection == Section::EQ) ? currentParam : -1);

    // Comp section
    auto compLabelArea = r.removeFromTop (kSectionLabelHeight);
    g.setFont (lookAndFeel.getMonoFont (12.0f));
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::fxColourId).withAlpha (0.6f));
    g.drawText ("COMP", compLabelArea, juce::Justification::centred);

    auto compArea = r.removeFromTop (kCompSectionHeight);
    paintGenericCompSection (g, gb.compThreshold, gb.compRatio, gb.compAttack, gb.compRelease,
                             compArea, isSelected, (isSelected && currentSection == Section::Comp) ? currentParam : -1);

    // Separator
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (r.getY(), static_cast<float> (bounds.getX()),
                          static_cast<float> (bounds.getRight()));
    r.removeFromTop (1);

    // Pan
    auto panArea = r.removeFromTop (kPanSectionHeight);
    paintGenericPanSection (g, gb.pan, panArea, isSelected && currentSection == Section::Pan);

    // Separator
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (r.getY(), static_cast<float> (bounds.getX()),
                          static_cast<float> (bounds.getRight()));
    r.removeFromTop (1);

    // Mute/Solo at bottom
    auto muteSoloArea = r.removeFromBottom (kMuteSoloHeight);
    paintGenericMuteSolo (g, gb.muted, gb.soloed, muteSoloArea, true);

    // Volume fader fills the rest
    paintGenericVolumeFader (g, gb.volume, r, isSelected && currentSection == Section::Volume);
}

//==============================================================================
// Master Strip
//==============================================================================

void MixerComponent::paintMasterStrip (juce::Graphics& g, juce::Rectangle<int> bounds, bool isSelected)
{
    auto& master = mixerState.master;

    // Strip background - slightly brighter for master
    auto stripBg = lookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId).brighter (0.06f);
    if (isSelected)
        stripBg = stripBg.brighter (0.06f);
    g.setColour (stripBg);
    g.fillRect (bounds);

    // Strip border
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawVerticalLine (bounds.getRight(), 0.0f, static_cast<float> (getHeight()));

    auto r = bounds;

    // Header
    auto headerArea = r.removeFromTop (kHeaderHeight);
    g.setColour (juce::Colour (0xffcc8833).withAlpha (0.4f));
    g.fillRect (headerArea);
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::textColourId));
    g.setFont (lookAndFeel.getMonoFont (14.0f));
    g.drawText ("MASTER", headerArea.reduced (4, 0), juce::Justification::centred);
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (headerArea.getBottom() - 1, static_cast<float> (bounds.getX()),
                          static_cast<float> (bounds.getRight()));

    // EQ section
    auto eqLabelArea = r.removeFromTop (kSectionLabelHeight);
    g.setFont (lookAndFeel.getMonoFont (12.0f));
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::fxColourId).withAlpha (0.6f));
    g.drawText ("EQ", eqLabelArea, juce::Justification::centred);

    auto eqArea = r.removeFromTop (kEqSectionHeight);
    paintGenericEqSection (g, master.eqLowGain, master.eqMidGain, master.eqHighGain, master.eqMidFreq,
                           eqArea, isSelected, (isSelected && currentSection == Section::EQ) ? currentParam : -1);

    // Comp section
    auto compLabelArea = r.removeFromTop (kSectionLabelHeight);
    g.setFont (lookAndFeel.getMonoFont (12.0f));
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::fxColourId).withAlpha (0.6f));
    g.drawText ("COMP", compLabelArea, juce::Justification::centred);

    auto compArea = r.removeFromTop (kCompSectionHeight);
    paintGenericCompSection (g, master.compThreshold, master.compRatio, master.compAttack, master.compRelease,
                             compArea, isSelected, (isSelected && currentSection == Section::Comp) ? currentParam : -1);

    // Inserts section
    int insertHeight = getMasterInsertsSectionHeight();
    if (insertHeight > 0)
    {
        auto insertLabelArea = r.removeFromTop (kSectionLabelHeight);
        g.setFont (lookAndFeel.getMonoFont (12.0f));
        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::fxColourId).withAlpha (0.6f));
        g.drawText ("INSERTS", insertLabelArea, juce::Justification::centred);

        auto insertsArea = r.removeFromTop (insertHeight);
        paintMasterInsertsSection (g, insertsArea, isSelected,
                                   (isSelected && currentSection == Section::Inserts) ? currentParam : -1);
    }

    // Limiter section
    auto limLabelArea = r.removeFromTop (kSectionLabelHeight);
    g.setFont (lookAndFeel.getMonoFont (12.0f));
    g.setColour (juce::Colour (0xffcc3333).withAlpha (0.7f));
    g.drawText ("LIMITER", limLabelArea, juce::Justification::centred);

    auto limArea = r.removeFromTop (kLimiterSectionHeight);
    paintLimiterSection (g, master.limiterThreshold, master.limiterRelease,
                         limArea, isSelected, (isSelected && currentSection == Section::Limiter) ? currentParam : -1);

    // Separator
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (r.getY(), static_cast<float> (bounds.getX()),
                          static_cast<float> (bounds.getRight()));
    r.removeFromTop (1);

    // Pan
    auto panArea = r.removeFromTop (kPanSectionHeight);
    paintGenericPanSection (g, master.pan, panArea, isSelected && currentSection == Section::Pan);

    // Separator
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (r.getY(), static_cast<float> (bounds.getX()),
                          static_cast<float> (bounds.getRight()));
    r.removeFromTop (1);

    // Volume fader fills the rest
    paintGenericVolumeFader (g, master.volume, r, isSelected && currentSection == Section::Volume);
}

//==============================================================================
// Generic section painting (shared by special strips)
//==============================================================================

void MixerComponent::paintGenericEqSection (juce::Graphics& g, double eqLow, double eqMid,
                                             double eqHigh, double midFreq,
                                             juce::Rectangle<int> bounds, bool /*isSelected*/,
                                             int selectedParam)
{
    auto inner = bounds.reduced (4, 2);
    int barWidth = (inner.getWidth() - 8) / 3;
    auto volumeCol = lookAndFeel.findColour (TrackerLookAndFeel::volumeColourId);
    auto selCol = lookAndFeel.findColour (TrackerLookAndFeel::fxColourId);

    struct EqBand { const char* label; double value; int idx; };
    EqBand bands[] = {
        { "L", eqLow,  0 },
        { "M", eqMid,  1 },
        { "H", eqHigh, 2 }
    };

    for (int i = 0; i < 3; ++i)
    {
        int x = inner.getX() + i * (barWidth + 4);
        auto barArea = juce::Rectangle<int> (x, inner.getY(), barWidth, inner.getHeight() - 18);

        bool paramSelected = (selectedParam == i);
        auto col = paramSelected ? selCol : volumeCol;
        paintVerticalBar (g, barArea, bands[i].value, -12.0, 12.0, col, true);

        g.setFont (lookAndFeel.getMonoFont (10.0f));
        g.setColour (paramSelected ? selCol : lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.6f));
        juce::String valStr = (bands[i].value >= 0.0 ? "+" : "") + juce::String (bands[i].value, 1);
        g.drawText (bands[i].label + juce::String (" ") + valStr, x, barArea.getBottom() + 1, barWidth, 16, juce::Justification::centred);
    }

    if (selectedParam == 3)
    {
        g.setFont (lookAndFeel.getMonoFont (10.0f));
        g.setColour (selCol);
        juce::String freqStr = juce::String (static_cast<int> (midFreq)) + "Hz";
        g.drawText (freqStr, inner.getX(), inner.getBottom() - 12, inner.getWidth(), 10,
                    juce::Justification::centred);
    }
}

void MixerComponent::paintGenericCompSection (juce::Graphics& g, double threshold, double ratio,
                                               double attack, double release,
                                               juce::Rectangle<int> bounds, bool /*isSelected*/,
                                               int selectedParam)
{
    auto inner = bounds.reduced (2, 2);
    auto selCol = lookAndFeel.findColour (TrackerLookAndFeel::fxColourId);
    auto textCol = lookAndFeel.findColour (TrackerLookAndFeel::textColourId);

    int knobSize = (inner.getWidth() - 6) / 2;
    int knobH = (inner.getHeight() - 2) / 2;

    struct CompParam { const char* label; double value; double minV; double maxV; int idx; };
    CompParam params[] = {
        { "THR", threshold, -60.0, 0.0, 0 },
        { "RAT", ratio,     1.0,   20.0, 1 },
        { "ATT", attack,    0.1,   100.0, 2 },
        { "REL", release,   10.0,  1000.0, 3 }
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

        juce::String valueStr;
        switch (i)
        {
            case 0: valueStr = juce::String (static_cast<int> (params[i].value)) + "dB"; break;
            case 1: valueStr = juce::String (params[i].value, 1) + ":1"; break;
            case 2: valueStr = juce::String (params[i].value, 1) + "ms"; break;
            case 3: valueStr = juce::String (static_cast<int> (params[i].value)) + "ms"; break;
        }

        paintKnob (g, area, params[i].value, params[i].minV, params[i].maxV, colour, valueStr);
    }
}

void MixerComponent::paintGenericVolumeFader (juce::Graphics& g, double volume,
                                               juce::Rectangle<int> bounds, bool isSelected,
                                               float peakLinear)
{
    auto inner = bounds.reduced (6, 4);
    auto selCol = lookAndFeel.findColour (TrackerLookAndFeel::fxColourId);
    auto volCol = lookAndFeel.findColour (TrackerLookAndFeel::volumeColourId);

    g.setFont (lookAndFeel.getMonoFont (12.0f));
    g.setColour (isSelected ? selCol : lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.7f));

    juce::String volText;
    if (volume <= -99.0)
        volText = "-inf";
    else
        volText = juce::String (volume, 1) + "dB";
    g.drawText (volText, inner.getX(), inner.getY(), inner.getWidth(), 12, juce::Justification::centred);

    auto faderArea = inner.withTrimmedTop (14).withTrimmedBottom (2);
    auto trackArea = faderArea.reduced (faderArea.getWidth() / 2 - 6, 0);
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId).brighter (0.1f));
    g.fillRect (trackArea);

    if (peakLinear > 0.0f)
    {
        float peakDb = juce::Decibels::gainToDecibels (peakLinear, -100.0f);
        double peakNorm = (static_cast<double> (peakDb) - (-100.0)) / (12.0 - (-100.0));
        peakNorm = juce::jlimit (0.0, 1.0, peakNorm);
        int meterHeight = static_cast<int> (peakNorm * static_cast<double> (faderArea.getHeight()));

        juce::Colour meterCol;
        if (peakDb > 0.0f)
            meterCol = juce::Colour (0xffcc3333);
        else if (peakDb > -6.0f)
            meterCol = juce::Colour (0xffccaa33);
        else
            meterCol = juce::Colour (0xff33aa55);

        g.setColour (meterCol.withAlpha (0.25f));
        g.fillRect (juce::Rectangle<int> (faderArea.getX() + 1, faderArea.getBottom() - meterHeight,
                                           faderArea.getWidth() - 2, meterHeight));
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
        double norm2 = (dB - (-100.0)) / (12.0 - (-100.0));
        int y = faderArea.getBottom() - static_cast<int> (norm2 * static_cast<double> (faderArea.getHeight()));
        g.drawHorizontalLine (y, static_cast<float> (faderArea.getX()),
                              static_cast<float> (faderArea.getX() + 3));
        g.drawHorizontalLine (y, static_cast<float> (faderArea.getRight() - 3),
                              static_cast<float> (faderArea.getRight()));
    }

    double norm = (volume - (-100.0)) / (12.0 - (-100.0));
    norm = juce::jlimit (0.0, 1.0, norm);
    int fillHeight = static_cast<int> (norm * static_cast<double> (faderArea.getHeight()));

    auto fillCol = isSelected ? selCol : volCol;
    g.setColour (fillCol.withAlpha (0.7f));
    g.fillRect (trackArea.getX(), faderArea.getBottom() - fillHeight,
                trackArea.getWidth(), fillHeight);

    int handleY = faderArea.getBottom() - fillHeight;
    g.setColour (fillCol);
    g.fillRect (faderArea.getX(), handleY - 2, faderArea.getWidth(), 4);
}

void MixerComponent::paintGenericPanSection (juce::Graphics& g, int pan,
                                              juce::Rectangle<int> bounds, bool isSelected)
{
    auto inner = bounds.reduced (4, 3);
    auto selCol = lookAndFeel.findColour (TrackerLookAndFeel::fxColourId);
    auto panCol = lookAndFeel.findColour (TrackerLookAndFeel::instrumentColourId);

    juce::String panLabel;
    if (pan == 0)
        panLabel = "PAN C";
    else if (pan < 0)
        panLabel = "PAN L" + juce::String (-pan);
    else
        panLabel = "PAN R" + juce::String (pan);

    g.setFont (lookAndFeel.getMonoFont (9.0f));
    g.setColour (isSelected ? selCol : lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.5f));
    g.drawText (panLabel, inner.getX(), inner.getY(), 44, inner.getHeight(), juce::Justification::centredLeft);

    auto barArea = juce::Rectangle<int> (inner.getX() + 44, inner.getY() + 2,
                                          inner.getWidth() - 46, inner.getHeight() - 4);
    paintHorizontalBar (g, barArea, static_cast<double> (pan), -50.0, 50.0,
                        isSelected ? selCol : panCol, true);
}

void MixerComponent::paintGenericMuteSolo (juce::Graphics& g, bool muted, bool soloed,
                                            juce::Rectangle<int> bounds, bool hasSolo)
{
    // Top separator
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (bounds.getY(), static_cast<float> (bounds.getX()),
                          static_cast<float> (bounds.getRight()));

    if (hasSolo)
    {
        int halfW = bounds.getWidth() / 2;

        auto muteArea = juce::Rectangle<int> (bounds.getX() + 2, bounds.getY() + 2,
                                               halfW - 3, bounds.getHeight() - 4);
        auto muteCol = lookAndFeel.findColour (TrackerLookAndFeel::muteColourId);
        g.setColour (muted ? muteCol : muteCol.withAlpha (0.15f));
        g.fillRoundedRectangle (muteArea.toFloat(), 2.0f);
        g.setColour (muted ? juce::Colours::white : lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.4f));
        g.setFont (lookAndFeel.getMonoFont (13.0f));
        g.drawText ("M", muteArea, juce::Justification::centred);

        auto soloArea = juce::Rectangle<int> (bounds.getX() + halfW + 1, bounds.getY() + 2,
                                               halfW - 3, bounds.getHeight() - 4);
        auto soloCol = lookAndFeel.findColour (TrackerLookAndFeel::soloColourId);
        g.setColour (soloed ? soloCol : soloCol.withAlpha (0.15f));
        g.fillRoundedRectangle (soloArea.toFloat(), 2.0f);
        g.setColour (soloed ? juce::Colours::black : lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.4f));
        g.setFont (lookAndFeel.getMonoFont (13.0f));
        g.drawText ("S", soloArea, juce::Justification::centred);
    }
    else
    {
        // Mute only (full width)
        auto muteArea = bounds.reduced (2);
        auto muteCol = lookAndFeel.findColour (TrackerLookAndFeel::muteColourId);
        g.setColour (muted ? muteCol : muteCol.withAlpha (0.15f));
        g.fillRoundedRectangle (muteArea.toFloat(), 2.0f);
        g.setColour (muted ? juce::Colours::white : lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.4f));
        g.setFont (lookAndFeel.getMonoFont (13.0f));
        g.drawText ("M", muteArea, juce::Justification::centred);
    }
}

void MixerComponent::paintLimiterSection (juce::Graphics& g, double threshold, double release,
                                           juce::Rectangle<int> bounds, bool /*isSelected*/, int selectedParam)
{
    auto inner = bounds.reduced (2, 2);
    auto selCol = lookAndFeel.findColour (TrackerLookAndFeel::fxColourId);
    auto textCol = lookAndFeel.findColour (TrackerLookAndFeel::textColourId);

    int knobSize = (inner.getWidth() - 6) / 2;
    int knobH = inner.getHeight();

    // Threshold knob
    {
        auto area = juce::Rectangle<int> (inner.getX(), inner.getY(), knobSize, knobH);
        bool sel = (selectedParam == 0);
        auto colour = sel ? selCol : textCol.withAlpha (0.5f);
        juce::String valueStr = juce::String (threshold, 1) + "dB";
        paintKnob (g, area, threshold, -24.0, 0.0, colour, valueStr);
    }

    // Release knob
    {
        auto area = juce::Rectangle<int> (inner.getX() + knobSize + 3, inner.getY(), knobSize, knobH);
        bool sel = (selectedParam == 1);
        auto colour = sel ? selCol : textCol.withAlpha (0.5f);
        juce::String valueStr = juce::String (static_cast<int> (release)) + "ms";
        paintKnob (g, area, release, 1.0, 500.0, colour, valueStr);
    }
}

void MixerComponent::paintMasterInsertsSection (juce::Graphics& g, juce::Rectangle<int> bounds,
                                                 bool isSelected, int selectedParam)
{
    auto inner = bounds.reduced (2, 1);
    auto selCol = lookAndFeel.findColour (TrackerLookAndFeel::fxColourId);
    auto textCol = lookAndFeel.findColour (TrackerLookAndFeel::textColourId);
    auto bgCol = lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId).brighter (0.05f);

    auto& slots = mixerState.masterInsertSlots;
    int numSlots = static_cast<int> (slots.size());

    for (int i = 0; i < numSlots; ++i)
    {
        auto& slot = slots[static_cast<size_t> (i)];
        auto rowArea = juce::Rectangle<int> (inner.getX(), inner.getY() + i * kInsertRowHeight,
                                              inner.getWidth(), kInsertRowHeight);
        bool isSel = isSelected && (selectedParam == i);

        g.setColour (isSel ? bgCol.brighter (0.1f) : bgCol);
        g.fillRect (rowArea);

        auto bypassArea = rowArea.removeFromLeft (14);
        g.setColour (slot.bypassed ? textCol.withAlpha (0.2f) : juce::Colour (0xff33aa55));
        g.fillEllipse (bypassArea.getCentreX() - 3.0f, bypassArea.getCentreY() - 3.0f, 6.0f, 6.0f);

        auto removeArea = rowArea.removeFromRight (16);
        g.setFont (lookAndFeel.getMonoFont (10.0f));
        g.setColour (textCol.withAlpha (0.5f));
        g.drawText ("x", removeArea, juce::Justification::centred);

        g.setFont (lookAndFeel.getMonoFont (9.0f));
        g.setColour (isSel ? selCol : textCol.withAlpha (slot.bypassed ? 0.3f : 0.7f));
        auto nameText = slot.pluginName;
        if (nameText.length() > 10)
            nameText = nameText.substring (0, 9) + "~";
        g.drawText (nameText, rowArea.reduced (1, 0), juce::Justification::centredLeft);

        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
        g.drawHorizontalLine (inner.getY() + (i + 1) * kInsertRowHeight - 1,
                              static_cast<float> (inner.getX()),
                              static_cast<float> (inner.getRight()));
    }

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

    // Determine which strip by searching through visible strips
    int totalStrips = getTotalStripCount();
    int vi = -1;
    for (int i = scrollOffset; i < totalStrips; ++i)
    {
        auto stripBounds = getStripBounds (i);
        if (pos.x >= stripBounds.getX() && pos.x < stripBounds.getRight())
        {
            vi = i;
            break;
        }
        if (stripBounds.getX() > getWidth())
            break;
    }
    if (vi < 0 || vi >= totalStrips)
        return result;

    result.visualTrack = vi;
    auto bounds = getStripBounds (vi);
    auto info = getStripInfo (vi);

    int relY = pos.y;
    int y = 0;

    // Header (all strip types)
    y += kHeaderHeight;
    if (relY < y)
        return result;

    // Helper lambdas for common hit-test sections
    auto hitTestEq = [&] () -> bool
    {
        y += kSectionLabelHeight;
        int eqStart = y;
        y += kEqSectionHeight;
        if (relY < y)
        {
            result.section = Section::EQ;
            int relEqY = relY - eqStart;
            if (relEqY >= (kEqSectionHeight - 18))
            {
                result.param = 3;
                return true;
            }
            int relX = pos.x - bounds.getX();
            int barWidth = (bounds.getWidth() - 16) / 3;
            result.param = juce::jlimit (0, 2, (relX - 4) / (barWidth + 4));
            return true;
        }
        return false;
    };

    auto hitTestComp = [&] () -> bool
    {
        y += kSectionLabelHeight;
        int compStart = y;
        y += kCompSectionHeight;
        if (relY < y)
        {
            result.section = Section::Comp;
            int relX = pos.x - bounds.getX();
            int relCY = relY - compStart;
            int col2 = (relX < bounds.getWidth() / 2) ? 0 : 1;
            int row2 = (relCY < kCompSectionHeight / 2) ? 0 : 1;
            result.param = row2 * 2 + col2;
            return true;
        }
        return false;
    };

    auto hitTestInserts = [&] (int insertHeight, const std::vector<InsertSlotState>& slots) -> bool
    {
        if (insertHeight > 0)
        {
            y += kSectionLabelHeight;
            int insertsStart = y;
            y += insertHeight;
            if (relY < y)
            {
                result.section = Section::Inserts;
                int relInsertY = relY - insertsStart;
                int numSlots = static_cast<int> (slots.size());
                int slotIdx = relInsertY / kInsertRowHeight;
                if (slotIdx < numSlots)
                {
                    result.hitInsertSlot = slotIdx;
                    result.param = slotIdx;
                    int relX = pos.x - bounds.getX();
                    int innerRight = bounds.getRight() - 2;
                    if (relX < 16)
                        result.hitInsertBypass = true;
                    else if (relX > innerRight - bounds.getX() - 18)
                        result.hitInsertRemove = true;
                    else
                        result.hitInsertOpen = true;
                }
                else
                {
                    result.hitInsertAdd = true;
                    result.param = -1;
                }
                return true;
            }
        }
        return false;
    };

    auto hitTestPan = [&] () -> bool
    {
        y += kPanSectionHeight;
        if (relY < y)
        {
            result.section = Section::Pan;
            result.param = 0;
            return true;
        }
        return false;
    };

    auto hitTestMuteSolo = [&] (bool hasSolo) -> bool
    {
        int muteSoloTop = getHeight() - kMuteSoloHeight;
        if (relY >= muteSoloTop)
        {
            int relX = pos.x - bounds.getX();
            if (hasSolo && relX >= bounds.getWidth() / 2)
                result.hitSolo = true;
            else
                result.hitMute = true;
            return true;
        }
        return false;
    };

    if (info.type == StripType::Track)
    {
        // Regular track: EQ -> Comp -> Inserts -> Sends -> Sep -> Pan -> Sep -> Volume (Mute/Solo at bottom)
        if (hitTestEq()) return result;
        if (hitTestComp()) return result;

        int physTrack = info.index;
        int insertHeight = getInsertsSectionHeight (physTrack);
        auto& slots = mixerState.insertSlots[static_cast<size_t> (physTrack)];
        if (hitTestInserts (insertHeight, slots)) return result;

        // Sends
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
        if (hitTestPan()) return result;
        y += 1; // separator

        if (hitTestMuteSolo (true)) return result;

        result.section = Section::Volume;
        result.param = 0;
        return result;
    }
    else if (info.type == StripType::DelayReturn || info.type == StripType::ReverbReturn)
    {
        // Send return: EQ -> Sep -> Pan -> Sep -> Volume (Mute at bottom)
        if (hitTestEq()) return result;
        y += 1; // separator
        if (hitTestPan()) return result;
        y += 1; // separator

        if (hitTestMuteSolo (false)) return result;

        result.section = Section::Volume;
        result.param = 0;
        return result;
    }
    else if (info.type == StripType::GroupBus)
    {
        // Group bus: EQ -> Comp -> Sep -> Pan -> Sep -> Volume (Mute/Solo at bottom)
        if (hitTestEq()) return result;
        if (hitTestComp()) return result;
        y += 1; // separator
        if (hitTestPan()) return result;
        y += 1; // separator

        if (hitTestMuteSolo (true)) return result;

        result.section = Section::Volume;
        result.param = 0;
        return result;
    }
    else if (info.type == StripType::Master)
    {
        // Master: EQ -> Comp -> Inserts -> Limiter -> Sep -> Pan -> Sep -> Volume
        if (hitTestEq()) return result;
        if (hitTestComp()) return result;

        int insertHeight = getMasterInsertsSectionHeight();
        if (hitTestInserts (insertHeight, mixerState.masterInsertSlots)) return result;

        // Limiter
        y += kSectionLabelHeight;
        int limStart = y;
        y += kLimiterSectionHeight;
        if (relY < y)
        {
            result.section = Section::Limiter;
            int relX = pos.x - bounds.getX();
            result.param = (relX < bounds.getWidth() / 2) ? 0 : 1;
            juce::ignoreUnused (limStart);
            return result;
        }

        y += 1; // separator
        if (hitTestPan()) return result;
        y += 1; // separator

        result.section = Section::Volume;
        result.param = 0;
        return result;
    }

    result.section = Section::Volume;
    result.param = 0;
    return result;
}

//==============================================================================
// Parameter access
//==============================================================================

double MixerComponent::getParamValue (int visualTrack, Section section, int param) const
{
    auto info = getStripInfo (visualTrack);

    switch (info.type)
    {
        case StripType::Track:
        {
            auto& state = mixerState.tracks[static_cast<size_t> (info.index)];
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
                case Section::Inserts: return 0.0;
                case Section::Sends:
                    switch (param)
                    {
                        case 0: return state.reverbSend;
                        case 1: return state.delaySend;
                        default: return 0.0;
                    }
                case Section::Pan: return static_cast<double> (state.pan);
                case Section::Volume: return state.volume;
                case Section::Limiter: return 0.0;
            }
            break;
        }
        case StripType::DelayReturn:
        case StripType::ReverbReturn:
        {
            auto& sr = mixerState.sendReturns[static_cast<size_t> (info.index)];
            switch (section)
            {
                case Section::EQ:
                    switch (param)
                    {
                        case 0: return sr.eqLowGain;
                        case 1: return sr.eqMidGain;
                        case 2: return sr.eqHighGain;
                        case 3: return sr.eqMidFreq;
                        default: return 0.0;
                    }
                case Section::Pan: return static_cast<double> (sr.pan);
                case Section::Volume: return sr.volume;
                default: return 0.0;
            }
            break;
        }
        case StripType::GroupBus:
        {
            auto& gb = mixerState.groupBuses[static_cast<size_t> (info.index)];
            switch (section)
            {
                case Section::EQ:
                    switch (param)
                    {
                        case 0: return gb.eqLowGain;
                        case 1: return gb.eqMidGain;
                        case 2: return gb.eqHighGain;
                        case 3: return gb.eqMidFreq;
                        default: return 0.0;
                    }
                case Section::Comp:
                    switch (param)
                    {
                        case 0: return gb.compThreshold;
                        case 1: return gb.compRatio;
                        case 2: return gb.compAttack;
                        case 3: return gb.compRelease;
                        default: return 0.0;
                    }
                case Section::Pan: return static_cast<double> (gb.pan);
                case Section::Volume: return gb.volume;
                default: return 0.0;
            }
            break;
        }
        case StripType::Master:
        {
            auto& m = mixerState.master;
            switch (section)
            {
                case Section::EQ:
                    switch (param)
                    {
                        case 0: return m.eqLowGain;
                        case 1: return m.eqMidGain;
                        case 2: return m.eqHighGain;
                        case 3: return m.eqMidFreq;
                        default: return 0.0;
                    }
                case Section::Comp:
                    switch (param)
                    {
                        case 0: return m.compThreshold;
                        case 1: return m.compRatio;
                        case 2: return m.compAttack;
                        case 3: return m.compRelease;
                        default: return 0.0;
                    }
                case Section::Limiter:
                    switch (param)
                    {
                        case 0: return m.limiterThreshold;
                        case 1: return m.limiterRelease;
                        default: return 0.0;
                    }
                case Section::Inserts: return 0.0;
                case Section::Pan: return static_cast<double> (m.pan);
                case Section::Volume: return m.volume;
                default: return 0.0;
            }
            break;
        }
    }
    return 0.0;
}

void MixerComponent::setParamValue (int visualTrack, Section section, int param, double value)
{
    auto info = getStripInfo (visualTrack);

    switch (info.type)
    {
        case StripType::Track:
        {
            auto& state = mixerState.tracks[static_cast<size_t> (info.index)];
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
                case Section::Inserts: break;
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
                case Section::Limiter: break;
            }
            break;
        }
        case StripType::DelayReturn:
        case StripType::ReverbReturn:
        {
            auto& sr = mixerState.sendReturns[static_cast<size_t> (info.index)];
            switch (section)
            {
                case Section::EQ:
                    switch (param)
                    {
                        case 0: sr.eqLowGain  = juce::jlimit (-12.0, 12.0, value); break;
                        case 1: sr.eqMidGain  = juce::jlimit (-12.0, 12.0, value); break;
                        case 2: sr.eqHighGain = juce::jlimit (-12.0, 12.0, value); break;
                        case 3: sr.eqMidFreq  = juce::jlimit (200.0, 8000.0, value); break;
                        default: break;
                    }
                    break;
                case Section::Pan:
                    sr.pan = juce::jlimit (-50, 50, static_cast<int> (value));
                    break;
                case Section::Volume:
                    sr.volume = juce::jlimit (-100.0, 12.0, value);
                    break;
                default: break;
            }
            break;
        }
        case StripType::GroupBus:
        {
            auto& gb = mixerState.groupBuses[static_cast<size_t> (info.index)];
            switch (section)
            {
                case Section::EQ:
                    switch (param)
                    {
                        case 0: gb.eqLowGain  = juce::jlimit (-12.0, 12.0, value); break;
                        case 1: gb.eqMidGain  = juce::jlimit (-12.0, 12.0, value); break;
                        case 2: gb.eqHighGain = juce::jlimit (-12.0, 12.0, value); break;
                        case 3: gb.eqMidFreq  = juce::jlimit (200.0, 8000.0, value); break;
                        default: break;
                    }
                    break;
                case Section::Comp:
                    switch (param)
                    {
                        case 0: gb.compThreshold = juce::jlimit (-60.0, 0.0, value); break;
                        case 1: gb.compRatio     = juce::jlimit (1.0, 20.0, value); break;
                        case 2: gb.compAttack    = juce::jlimit (0.1, 100.0, value); break;
                        case 3: gb.compRelease   = juce::jlimit (10.0, 1000.0, value); break;
                        default: break;
                    }
                    break;
                case Section::Pan:
                    gb.pan = juce::jlimit (-50, 50, static_cast<int> (value));
                    break;
                case Section::Volume:
                    gb.volume = juce::jlimit (-100.0, 12.0, value);
                    break;
                default: break;
            }
            break;
        }
        case StripType::Master:
        {
            auto& m = mixerState.master;
            switch (section)
            {
                case Section::EQ:
                    switch (param)
                    {
                        case 0: m.eqLowGain  = juce::jlimit (-12.0, 12.0, value); break;
                        case 1: m.eqMidGain  = juce::jlimit (-12.0, 12.0, value); break;
                        case 2: m.eqHighGain = juce::jlimit (-12.0, 12.0, value); break;
                        case 3: m.eqMidFreq  = juce::jlimit (200.0, 8000.0, value); break;
                        default: break;
                    }
                    break;
                case Section::Comp:
                    switch (param)
                    {
                        case 0: m.compThreshold = juce::jlimit (-60.0, 0.0, value); break;
                        case 1: m.compRatio     = juce::jlimit (1.0, 20.0, value); break;
                        case 2: m.compAttack    = juce::jlimit (0.1, 100.0, value); break;
                        case 3: m.compRelease   = juce::jlimit (10.0, 1000.0, value); break;
                        default: break;
                    }
                    break;
                case Section::Limiter:
                    switch (param)
                    {
                        case 0: m.limiterThreshold = juce::jlimit (-24.0, 0.0, value); break;
                        case 1: m.limiterRelease   = juce::jlimit (1.0, 500.0, value); break;
                        default: break;
                    }
                    break;
                case Section::Inserts: break;
                case Section::Pan:
                    m.pan = juce::jlimit (-50, 50, static_cast<int> (value));
                    break;
                case Section::Volume:
                    m.volume = juce::jlimit (-100.0, 12.0, value);
                    break;
                default: break;
            }
            break;
        }
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
        case Section::Limiter:
            switch (param)
            {
                case 0: return -24.0;
                case 1: return 1.0;
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
        case Section::Limiter:
            switch (param)
            {
                case 0: return 0.0;
                case 1: return 500.0;
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
        case Section::Limiter:
            switch (param)
            {
                case 0: return 0.5;
                case 1: return 5.0;
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
        case Section::Limiter: return 2;  // Threshold, Release
        case Section::Inserts:
        {
            auto info = getStripInfo (selectedTrack);
            if (info.type == StripType::Master)
                return juce::jmax (1, static_cast<int> (mixerState.masterInsertSlots.size()));
            if (info.type == StripType::Track)
            {
                auto& slots = mixerState.insertSlots[static_cast<size_t> (info.index)];
                return juce::jmax (1, static_cast<int> (slots.size()));
            }
            return 1;
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
        int totalStrips = getTotalStripCount();
        if (shift)
        {
            if (selectedTrack > 0)
                selectedTrack--;
        }
        else
        {
            if (selectedTrack < totalStrips - 1)
                selectedTrack++;
        }
        ensureTrackVisible();
        repaint();
        return true;
    }

    // M: toggle mute
    if (key.getTextCharacter() == 'm' || key.getTextCharacter() == 'M')
    {
        auto info = getStripInfo (selectedTrack);
        switch (info.type)
        {
            case StripType::Track:
            {
                auto& state = mixerState.tracks[static_cast<size_t> (info.index)];
                state.muted = ! state.muted;
                if (onMuteChanged)
                    onMuteChanged (info.index, state.muted);
                break;
            }
            case StripType::DelayReturn:
            case StripType::ReverbReturn:
                mixerState.sendReturns[static_cast<size_t> (info.index)].muted =
                    ! mixerState.sendReturns[static_cast<size_t> (info.index)].muted;
                if (onMixStateChanged) onMixStateChanged();
                break;
            case StripType::GroupBus:
                mixerState.groupBuses[static_cast<size_t> (info.index)].muted =
                    ! mixerState.groupBuses[static_cast<size_t> (info.index)].muted;
                if (onMixStateChanged) onMixStateChanged();
                break;
            case StripType::Master:
                break;  // no mute on master
        }
        repaint();
        return true;
    }

    // S: toggle solo
    if (key.getTextCharacter() == 's' || key.getTextCharacter() == 'S')
    {
        auto info = getStripInfo (selectedTrack);
        switch (info.type)
        {
            case StripType::Track:
            {
                auto& state = mixerState.tracks[static_cast<size_t> (info.index)];
                state.soloed = ! state.soloed;
                if (onSoloChanged)
                    onSoloChanged (info.index, state.soloed);
                break;
            }
            case StripType::GroupBus:
                mixerState.groupBuses[static_cast<size_t> (info.index)].soloed =
                    ! mixerState.groupBuses[static_cast<size_t> (info.index)].soloed;
                if (onMixStateChanged) onMixStateChanged();
                break;
            default:
                break;
        }
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
        auto info = getStripInfo (hit.visualTrack);
        switch (info.type)
        {
            case StripType::Track:
            {
                auto& state = mixerState.tracks[static_cast<size_t> (info.index)];
                state.muted = ! state.muted;
                if (onMuteChanged)
                    onMuteChanged (info.index, state.muted);
                break;
            }
            case StripType::DelayReturn:
            case StripType::ReverbReturn:
                mixerState.sendReturns[static_cast<size_t> (info.index)].muted =
                    ! mixerState.sendReturns[static_cast<size_t> (info.index)].muted;
                if (onMixStateChanged) onMixStateChanged();
                break;
            case StripType::GroupBus:
                mixerState.groupBuses[static_cast<size_t> (info.index)].muted =
                    ! mixerState.groupBuses[static_cast<size_t> (info.index)].muted;
                if (onMixStateChanged) onMixStateChanged();
                break;
            case StripType::Master: break;
        }
        repaint();
        return;
    }
    if (hit.hitSolo)
    {
        auto info = getStripInfo (hit.visualTrack);
        switch (info.type)
        {
            case StripType::Track:
            {
                auto& state = mixerState.tracks[static_cast<size_t> (info.index)];
                state.soloed = ! state.soloed;
                if (onSoloChanged)
                    onSoloChanged (info.index, state.soloed);
                break;
            }
            case StripType::GroupBus:
                mixerState.groupBuses[static_cast<size_t> (info.index)].soloed =
                    ! mixerState.groupBuses[static_cast<size_t> (info.index)].soloed;
                if (onMixStateChanged) onMixStateChanged();
                break;
            default: break;
        }
        repaint();
        return;
    }

    // Handle insert-specific clicks
    if (hit.section == Section::Inserts)
    {
        auto info = getStripInfo (hit.visualTrack);

        if (info.type == StripType::Master)
        {
            if (hit.hitInsertAdd)
            {
                if (onAddMasterInsertClicked)
                    onAddMasterInsertClicked();
                repaint();
                return;
            }
            if (hit.hitInsertRemove && hit.hitInsertSlot >= 0)
            {
                if (onRemoveMasterInsertClicked)
                    onRemoveMasterInsertClicked (hit.hitInsertSlot);
                repaint();
                return;
            }
            if (hit.hitInsertBypass && hit.hitInsertSlot >= 0)
            {
                auto& slots = mixerState.masterInsertSlots;
                if (hit.hitInsertSlot < static_cast<int> (slots.size()))
                {
                    bool newState = ! slots[static_cast<size_t> (hit.hitInsertSlot)].bypassed;
                    if (onMasterInsertBypassToggled)
                        onMasterInsertBypassToggled (hit.hitInsertSlot, newState);
                }
                repaint();
                return;
            }
            if (hit.hitInsertOpen && hit.hitInsertSlot >= 0)
            {
                if (onOpenMasterInsertEditor)
                    onOpenMasterInsertEditor (hit.hitInsertSlot);
                repaint();
                return;
            }
        }
        else if (info.type == StripType::Track)
        {
            if (hit.hitInsertAdd)
            {
                if (onAddInsertClicked)
                    onAddInsertClicked (info.index);
                repaint();
                return;
            }
            if (hit.hitInsertRemove && hit.hitInsertSlot >= 0)
            {
                if (onRemoveInsertClicked)
                    onRemoveInsertClicked (info.index, hit.hitInsertSlot);
                repaint();
                return;
            }
            if (hit.hitInsertBypass && hit.hitInsertSlot >= 0)
            {
                auto& slots = mixerState.insertSlots[static_cast<size_t> (info.index)];
                if (hit.hitInsertSlot < static_cast<int> (slots.size()))
                {
                    bool newState = ! slots[static_cast<size_t> (hit.hitInsertSlot)].bypassed;
                    if (onInsertBypassToggled)
                        onInsertBypassToggled (info.index, hit.hitInsertSlot, newState);
                }
                repaint();
                return;
            }
            if (hit.hitInsertOpen && hit.hitInsertSlot >= 0)
            {
                if (onOpenInsertEditor)
                    onOpenInsertEditor (info.index, hit.hitInsertSlot);
                repaint();
                return;
            }
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

    dragStartValue = getParamValue (dragTrack, dragSection, dragParam);

    repaint();
}

void MixerComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (! dragging || dragTrack < 0)
        return;

    double minVal = getParamMin (dragSection, dragParam);
    double maxVal = getParamMax (dragSection, dragParam);
    double range = maxVal - minVal;

    // Vertical drag: up = increase, down = decrease
    double pixelRange = 200.0; // full range over 200 pixels
    double delta = static_cast<double> (dragStartY - event.getPosition().y) / pixelRange * range;

    double newValue = dragStartValue + delta;
    newValue = juce::jlimit (minVal, maxVal, newValue);

    setParamValue (dragTrack, dragSection, dragParam, newValue);
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
        int totalStrips = getTotalStripCount();
        scrollOffset = juce::jlimit (0, juce::jmax (0, totalStrips - getVisibleStripCount()),
                                      scrollOffset - static_cast<int> (wheel.deltaY * 3.0f));
        repaint();
        return;
    }

    // Adjust parameter under cursor
    selectedTrack = hit.visualTrack;
    currentSection = hit.section;
    if (hit.param >= 0)
        currentParam = hit.param;

    double step = getParamStep (hit.section, (hit.param >= 0) ? hit.param : 0);
    double delta = (wheel.deltaY > 0.0f ? 1.0 : -1.0) * step;

    int paramIdx = (hit.param >= 0) ? hit.param : 0;
    double current = getParamValue (hit.visualTrack, hit.section, paramIdx);
    double minVal = getParamMin (hit.section, paramIdx);
    double maxVal = getParamMax (hit.section, paramIdx);
    double newVal = juce::jlimit (minVal, maxVal, current + delta);

    setParamValue (hit.visualTrack, hit.section, paramIdx, newVal);
    repaint();
}

//==============================================================================
// Navigation helpers
//==============================================================================

void MixerComponent::adjustCurrentParam (double direction)
{
    double step = getParamStep (currentSection, currentParam) * direction;
    double current = getParamValue (selectedTrack, currentSection, currentParam);
    double minVal = getParamMin (currentSection, currentParam);
    double maxVal = getParamMax (currentSection, currentParam);
    double newVal = juce::jlimit (minVal, maxVal, current + step);
    setParamValue (selectedTrack, currentSection, currentParam, newVal);
}

void MixerComponent::nextSection()
{
    auto info = getStripInfo (selectedTrack);

    if (info.type == StripType::Master)
    {
        // Master: EQ -> Comp -> Inserts -> Limiter -> Pan -> Volume -> EQ
        switch (currentSection)
        {
            case Section::EQ:      currentSection = Section::Comp;    break;
            case Section::Comp:    currentSection = Section::Inserts; break;
            case Section::Inserts: currentSection = Section::Limiter; break;
            case Section::Limiter: currentSection = Section::Pan;     break;
            case Section::Pan:     currentSection = Section::Volume;  break;
            case Section::Volume:  currentSection = Section::EQ;      break;
            default:               currentSection = Section::EQ;      break;
        }
    }
    else if (info.type == StripType::DelayReturn || info.type == StripType::ReverbReturn)
    {
        // Send returns: EQ -> Pan -> Volume -> EQ
        switch (currentSection)
        {
            case Section::EQ:     currentSection = Section::Pan;    break;
            case Section::Pan:    currentSection = Section::Volume; break;
            case Section::Volume: currentSection = Section::EQ;     break;
            default:              currentSection = Section::EQ;     break;
        }
    }
    else if (info.type == StripType::GroupBus)
    {
        // Group bus: EQ -> Comp -> Pan -> Volume -> EQ
        switch (currentSection)
        {
            case Section::EQ:     currentSection = Section::Comp;   break;
            case Section::Comp:   currentSection = Section::Pan;    break;
            case Section::Pan:    currentSection = Section::Volume;  break;
            case Section::Volume: currentSection = Section::EQ;      break;
            default:              currentSection = Section::EQ;      break;
        }
    }
    else
    {
        // Regular track: EQ -> Comp -> Inserts -> Sends -> Pan -> Volume -> EQ
        switch (currentSection)
        {
            case Section::EQ:      currentSection = Section::Comp;    break;
            case Section::Comp:    currentSection = Section::Inserts; break;
            case Section::Inserts: currentSection = Section::Sends;   break;
            case Section::Sends:   currentSection = Section::Pan;     break;
            case Section::Pan:     currentSection = Section::Volume;  break;
            case Section::Volume:  currentSection = Section::EQ;      break;
            default:               currentSection = Section::EQ;      break;
        }
    }
    currentParam = 0;
}

void MixerComponent::prevSection()
{
    auto info = getStripInfo (selectedTrack);

    if (info.type == StripType::Master)
    {
        switch (currentSection)
        {
            case Section::EQ:      currentSection = Section::Volume;  break;
            case Section::Comp:    currentSection = Section::EQ;      break;
            case Section::Inserts: currentSection = Section::Comp;    break;
            case Section::Limiter: currentSection = Section::Inserts; break;
            case Section::Pan:     currentSection = Section::Limiter; break;
            case Section::Volume:  currentSection = Section::Pan;     break;
            default:               currentSection = Section::Volume;  break;
        }
    }
    else if (info.type == StripType::DelayReturn || info.type == StripType::ReverbReturn)
    {
        switch (currentSection)
        {
            case Section::EQ:     currentSection = Section::Volume; break;
            case Section::Pan:    currentSection = Section::EQ;     break;
            case Section::Volume: currentSection = Section::Pan;    break;
            default:              currentSection = Section::Volume;  break;
        }
    }
    else if (info.type == StripType::GroupBus)
    {
        switch (currentSection)
        {
            case Section::EQ:     currentSection = Section::Volume; break;
            case Section::Comp:   currentSection = Section::EQ;     break;
            case Section::Pan:    currentSection = Section::Comp;   break;
            case Section::Volume: currentSection = Section::Pan;    break;
            default:              currentSection = Section::Volume;  break;
        }
    }
    else
    {
        switch (currentSection)
        {
            case Section::EQ:      currentSection = Section::Volume;  break;
            case Section::Comp:    currentSection = Section::EQ;      break;
            case Section::Inserts: currentSection = Section::Comp;    break;
            case Section::Sends:   currentSection = Section::Inserts; break;
            case Section::Pan:     currentSection = Section::Sends;   break;
            case Section::Volume:  currentSection = Section::Pan;     break;
            default:               currentSection = Section::Volume;  break;
        }
    }
    currentParam = getParamCountForSection (currentSection) - 1;
}

void MixerComponent::ensureTrackVisible()
{
    int totalStrips = getTotalStripCount();
    int visCount = getVisibleStripCount();
    if (selectedTrack < scrollOffset)
        scrollOffset = selectedTrack;
    else if (selectedTrack >= scrollOffset + visCount)
        scrollOffset = selectedTrack - visCount + 1;
    scrollOffset = juce::jlimit (0, juce::jmax (0, totalStrips - visCount), scrollOffset);
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
