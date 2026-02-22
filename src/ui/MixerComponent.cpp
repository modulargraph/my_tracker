#include "MixerComponent.h"
#include "MixerNavigation.h"
#include "MixerStripPainter.h"

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
                                      juce::Rectangle<int> bounds, bool isSelected, int selectedParam)
{
    MixerStripPainter::paintGenericEqSection (g, lookAndFeel, state.eqLowGain, state.eqMidGain,
        state.eqHighGain, state.eqMidFreq, bounds, isSelected, selectedParam);
}

//==============================================================================
// Compressor Section: 4 small knobs (Thr, Rat, Att, Rel)
//==============================================================================

void MixerComponent::paintCompSection (juce::Graphics& g, const TrackMixState& state,
                                        juce::Rectangle<int> bounds, bool isSelected, int selectedParam)
{
    MixerStripPainter::paintGenericCompSection (g, lookAndFeel, state.compThreshold, state.compRatio,
        state.compAttack, state.compRelease, bounds, isSelected, selectedParam);
}

//==============================================================================
// Inserts Section: rows for insert plugins + add button
//==============================================================================

void MixerComponent::paintInsertsSection (juce::Graphics& g, int physTrack,
                                           juce::Rectangle<int> bounds, bool isSelected, int selectedParam)
{
    auto& slots = mixerState.insertSlots[static_cast<size_t> (physTrack)];
    MixerStripPainter::paintInsertSlots (g, lookAndFeel, slots,
        kInsertRowHeight, kInsertAddButtonHeight, bounds, isSelected, selectedParam);
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
        MixerStripPainter::paintHorizontalBar (g, lookAndFeel, barArea, sends[i].value, -100.0, 0.0, col);
    }
}

//==============================================================================
// Pan Section: horizontal bar, center-zero
//==============================================================================

void MixerComponent::paintPanSection (juce::Graphics& g, const TrackMixState& state,
                                       juce::Rectangle<int> bounds, bool isSelected)
{
    MixerStripPainter::paintGenericPanSection (g, lookAndFeel, state.pan, bounds, isSelected);
}

//==============================================================================
// Volume Fader: large vertical bar
//==============================================================================

void MixerComponent::paintVolumeFader (juce::Graphics& g, const TrackMixState& state,
                                        juce::Rectangle<int> bounds, bool isSelected,
                                        float peakLinear)
{
    MixerStripPainter::paintGenericVolumeFader (g, lookAndFeel, state.volume, bounds, isSelected, peakLinear);
}

//==============================================================================
// Mute/Solo buttons
//==============================================================================

void MixerComponent::paintMuteSolo (juce::Graphics& g, const TrackMixState& state,
                                     juce::Rectangle<int> bounds, int /*physTrack*/)
{
    MixerStripPainter::paintGenericMuteSolo (g, lookAndFeel, state.muted, state.soloed, bounds, true);
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
    MixerStripPainter::paintGenericEqSection (g, lookAndFeel, sr.eqLowGain, sr.eqMidGain, sr.eqHighGain, sr.eqMidFreq,
                                              eqArea, isSelected, (isSelected && currentSection == Section::EQ) ? currentParam : -1);

    // Separator
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (r.getY(), static_cast<float> (bounds.getX()),
                          static_cast<float> (bounds.getRight()));
    r.removeFromTop (1);

    // Pan
    auto panArea = r.removeFromTop (kPanSectionHeight);
    MixerStripPainter::paintGenericPanSection (g, lookAndFeel, sr.pan, panArea, isSelected && currentSection == Section::Pan);

    // Separator
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (r.getY(), static_cast<float> (bounds.getX()),
                          static_cast<float> (bounds.getRight()));
    r.removeFromTop (1);

    // Mute button (no solo for send returns)
    auto muteSoloArea = r.removeFromBottom (kMuteSoloHeight);
    MixerStripPainter::paintGenericMuteSolo (g, lookAndFeel, sr.muted, false, muteSoloArea, false);

    // Volume fader fills the rest
    MixerStripPainter::paintGenericVolumeFader (g, lookAndFeel, sr.volume, r, isSelected && currentSection == Section::Volume);
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
    MixerStripPainter::paintGenericEqSection (g, lookAndFeel, gb.eqLowGain, gb.eqMidGain, gb.eqHighGain, gb.eqMidFreq,
                                              eqArea, isSelected, (isSelected && currentSection == Section::EQ) ? currentParam : -1);

    // Comp section
    auto compLabelArea = r.removeFromTop (kSectionLabelHeight);
    g.setFont (lookAndFeel.getMonoFont (12.0f));
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::fxColourId).withAlpha (0.6f));
    g.drawText ("COMP", compLabelArea, juce::Justification::centred);

    auto compArea = r.removeFromTop (kCompSectionHeight);
    MixerStripPainter::paintGenericCompSection (g, lookAndFeel, gb.compThreshold, gb.compRatio, gb.compAttack, gb.compRelease,
                                                compArea, isSelected, (isSelected && currentSection == Section::Comp) ? currentParam : -1);

    // Separator
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (r.getY(), static_cast<float> (bounds.getX()),
                          static_cast<float> (bounds.getRight()));
    r.removeFromTop (1);

    // Pan
    auto panArea = r.removeFromTop (kPanSectionHeight);
    MixerStripPainter::paintGenericPanSection (g, lookAndFeel, gb.pan, panArea, isSelected && currentSection == Section::Pan);

    // Separator
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (r.getY(), static_cast<float> (bounds.getX()),
                          static_cast<float> (bounds.getRight()));
    r.removeFromTop (1);

    // Mute/Solo at bottom
    auto muteSoloArea = r.removeFromBottom (kMuteSoloHeight);
    MixerStripPainter::paintGenericMuteSolo (g, lookAndFeel, gb.muted, gb.soloed, muteSoloArea, true);

    // Volume fader fills the rest
    MixerStripPainter::paintGenericVolumeFader (g, lookAndFeel, gb.volume, r, isSelected && currentSection == Section::Volume);
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
    MixerStripPainter::paintGenericEqSection (g, lookAndFeel, master.eqLowGain, master.eqMidGain, master.eqHighGain, master.eqMidFreq,
                                              eqArea, isSelected, (isSelected && currentSection == Section::EQ) ? currentParam : -1);

    // Comp section
    auto compLabelArea = r.removeFromTop (kSectionLabelHeight);
    g.setFont (lookAndFeel.getMonoFont (12.0f));
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::fxColourId).withAlpha (0.6f));
    g.drawText ("COMP", compLabelArea, juce::Justification::centred);

    auto compArea = r.removeFromTop (kCompSectionHeight);
    MixerStripPainter::paintGenericCompSection (g, lookAndFeel, master.compThreshold, master.compRatio, master.compAttack, master.compRelease,
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
        MixerStripPainter::paintInsertSlots (g, lookAndFeel, mixerState.masterInsertSlots,
            kInsertRowHeight, kInsertAddButtonHeight, insertsArea, isSelected,
            (isSelected && currentSection == Section::Inserts) ? currentParam : -1);
    }

    // Limiter section
    auto limLabelArea = r.removeFromTop (kSectionLabelHeight);
    g.setFont (lookAndFeel.getMonoFont (12.0f));
    g.setColour (juce::Colour (0xffcc3333).withAlpha (0.7f));
    g.drawText ("LIMITER", limLabelArea, juce::Justification::centred);

    auto limArea = r.removeFromTop (kLimiterSectionHeight);
    MixerStripPainter::paintLimiterSection (g, lookAndFeel, master.limiterThreshold, master.limiterRelease,
                                            limArea, isSelected, (isSelected && currentSection == Section::Limiter) ? currentParam : -1);

    // Separator
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (r.getY(), static_cast<float> (bounds.getX()),
                          static_cast<float> (bounds.getRight()));
    r.removeFromTop (1);

    // Pan
    auto panArea = r.removeFromTop (kPanSectionHeight);
    MixerStripPainter::paintGenericPanSection (g, lookAndFeel, master.pan, panArea, isSelected && currentSection == Section::Pan);

    // Separator
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (r.getY(), static_cast<float> (bounds.getX()),
                          static_cast<float> (bounds.getRight()));
    r.removeFromTop (1);

    // Volume fader fills the rest
    MixerStripPainter::paintGenericVolumeFader (g, lookAndFeel, master.volume, r, isSelected && currentSection == Section::Volume);
}

//==============================================================================
// Hit testing
//==============================================================================

MixerComponent::HitResult MixerComponent::hitTestStrip (juce::Point<int> pos) const
{
    MixerHitTestContext ctx;
    ctx.scrollOffset     = scrollOffset;
    ctx.componentWidth   = getWidth();
    ctx.componentHeight  = getHeight();
    ctx.totalStripCount  = getTotalStripCount();

    ctx.getStripBounds = [this] (int vi) { return getStripBounds (vi); };
    ctx.getStripInfo   = [this] (int vi) { return getStripInfo (vi); };
    ctx.getInsertsSectionHeight      = [this] (int pt) { return getInsertsSectionHeight (pt); };
    ctx.getMasterInsertsSectionHeight = [this] () { return getMasterInsertsSectionHeight(); };
    ctx.getTrackInsertSlots  = [this] (int pt) -> const std::vector<InsertSlotState>& {
        return mixerState.insertSlots[static_cast<size_t> (pt)];
    };
    ctx.getMasterInsertSlots = [this] () -> const std::vector<InsertSlotState>& {
        return mixerState.masterInsertSlots;
    };

    return mixerHitTestStrip (pos, ctx);
}

//==============================================================================
// Parameter access  (thin wrappers delegating to MixerParamModel)
//==============================================================================

double MixerComponent::getParamValue (int visualTrack, Section section, int param) const
{
    auto info = getStripInfo (visualTrack);
    return MixerParamModel::getParamValue (mixerState, info.type, info.index, section, param);
}

void MixerComponent::setParamValue (int visualTrack, Section section, int param, double value)
{
    auto info = getStripInfo (visualTrack);
    MixerParamModel::setParamValue (mixerState, info.type, info.index, section, param, value);

    if (onMixStateChanged)
        onMixStateChanged();
}

double MixerComponent::getParamMin (Section section, int param) const
{
    return MixerParamModel::getParamMin (section, param);
}

double MixerComponent::getParamMax (Section section, int param) const
{
    return MixerParamModel::getParamMax (section, param);
}

double MixerComponent::getParamStep (Section section, int param) const
{
    return MixerParamModel::getParamStep (section, param);
}

int MixerComponent::getParamCountForSection (Section section) const
{
    auto info = getStripInfo (selectedTrack);
    return MixerParamModel::getParamCountForSection (section, mixerState, info.type, info.index);
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
    auto info      = getStripInfo (selectedTrack);
    currentSection = MixerNavigation::nextSection (currentSection, info.type);
    currentParam   = 0;
}

void MixerComponent::prevSection()
{
    auto info      = getStripInfo (selectedTrack);
    currentSection = MixerNavigation::prevSection (currentSection, info.type);
    currentParam   = getParamCountForSection (currentSection) - 1;
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
