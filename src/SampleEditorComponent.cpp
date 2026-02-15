#include "SampleEditorComponent.h"

//==============================================================================
// LFO speed presets (descending, in steps)
//==============================================================================

const int SampleEditorComponent::kLfoSpeeds[] = {
    128, 96, 64, 48, 32, 24, 16, 12, 8, 6, 4, 3, 2, 1
};

//==============================================================================
// Formatting helpers
//==============================================================================

static juce::String formatDb (double db)
{
    if (db <= -99.0) return "-inf";
    return juce::String (db, 1) + " dB";
}

static juce::String formatPercent (int v)
{
    return juce::String (v);
}

static juce::String formatPan (int v)
{
    if (v == 0) return "C";
    if (v < 0)  return "L" + juce::String (-v);
    return "R" + juce::String (v);
}

static juce::String formatSemitones (int v)
{
    if (v > 0) return "+" + juce::String (v);
    return juce::String (v);
}

static juce::String formatCents (int v)
{
    if (v > 0) return "+" + juce::String (v);
    return juce::String (v);
}

static juce::String formatSeconds (double s)
{
    return juce::String (s, 3) + "s";
}

static juce::String formatPosSec (double normPos, double totalLen)
{
    return juce::String (normPos * totalLen, 3) + "s";
}

//==============================================================================
// Construction / Destruction
//==============================================================================

SampleEditorComponent::SampleEditorComponent (TrackerLookAndFeel& lnf)
    : lookAndFeel (lnf)
{
    formatManager.registerBasicFormats();
    setWantsKeyboardFocus (true);
}

SampleEditorComponent::~SampleEditorComponent()
{
    stopTimer();
}

//==============================================================================
// Display mode and sub-tab
//==============================================================================

void SampleEditorComponent::setDisplayMode (DisplayMode mode)
{
    if (displayMode != mode)
    {
        displayMode = mode;
        repaint();
    }
}

void SampleEditorComponent::setEditSubTab (EditSubTab tab)
{
    if (editSubTab != tab)
    {
        editSubTab = tab;
        repaint();
    }
}

//==============================================================================
// Instrument management
//==============================================================================

void SampleEditorComponent::setInstrument (int instrumentIndex, const juce::File& sampleFile,
                                            const InstrumentParams& params)
{
    if (paramsDirty)
    {
        stopTimer();
        paramsDirty = false;
        if (onParamsChanged)
            onParamsChanged (currentInstrument, currentParams);
    }

    currentInstrument = instrumentIndex;
    currentFile = sampleFile;
    currentParams = params;
    paramsDirty = false;

    thumbnail.clear();
    if (sampleFile.existsAsFile())
        thumbnail.setSource (new juce::FileInputSource (sampleFile));

    repaint();
}

void SampleEditorComponent::clearInstrument()
{
    if (paramsDirty)
    {
        stopTimer();
        paramsDirty = false;
        if (onParamsChanged)
            onParamsChanged (currentInstrument, currentParams);
    }

    currentInstrument = -1;
    currentFile = juce::File();
    currentParams = InstrumentParams();
    paramsDirty = false;
    isDragging = false;

    thumbnail.clear();
    repaint();
}

//==============================================================================
// Debounced apply
//==============================================================================

void SampleEditorComponent::timerCallback()
{
    stopTimer();
    if (paramsDirty)
    {
        paramsDirty = false;
        if (onParamsChanged)
            onParamsChanged (currentInstrument, currentParams);
    }
}

void SampleEditorComponent::scheduleApply()
{
    paramsDirty = true;
    startTimer (80);
}

void SampleEditorComponent::notifyParamsChanged()
{
    scheduleApply();
    repaint();
}

//==============================================================================
// String helpers
//==============================================================================

juce::String SampleEditorComponent::getPlayModeName (InstrumentParams::PlayMode mode) const
{
    switch (mode)
    {
        case InstrumentParams::PlayMode::OneShot:        return "1-Shot";
        case InstrumentParams::PlayMode::ForwardLoop:    return "Forward loop";
        case InstrumentParams::PlayMode::BackwardLoop:   return "Backward loop";
        case InstrumentParams::PlayMode::PingpongLoop:   return "Pingpong loop";
        case InstrumentParams::PlayMode::Slice:          return "Slice";
        case InstrumentParams::PlayMode::BeatSlice:      return "Beat Slice";
        case InstrumentParams::PlayMode::Granular:       return "Granular";
    }
    return "???";
}

juce::String SampleEditorComponent::getFilterTypeName (InstrumentParams::FilterType type) const
{
    switch (type)
    {
        case InstrumentParams::FilterType::Disabled: return "Off";
        case InstrumentParams::FilterType::LowPass:  return "LowPass";
        case InstrumentParams::FilterType::HighPass:  return "HighPass";
        case InstrumentParams::FilterType::BandPass:  return "BandPass";
    }
    return "???";
}

juce::String SampleEditorComponent::getModTypeName (InstrumentParams::Modulation::Type type) const
{
    switch (type)
    {
        case InstrumentParams::Modulation::Type::Off:       return "Off";
        case InstrumentParams::Modulation::Type::Envelope:  return "Envelope";
        case InstrumentParams::Modulation::Type::LFO:       return "LFO";
    }
    return "???";
}

juce::String SampleEditorComponent::getLfoShapeName (InstrumentParams::Modulation::LFOShape shape) const
{
    switch (shape)
    {
        case InstrumentParams::Modulation::LFOShape::RevSaw:    return "Rev Saw";
        case InstrumentParams::Modulation::LFOShape::Saw:       return "Saw";
        case InstrumentParams::Modulation::LFOShape::Triangle:  return "Triangle";
        case InstrumentParams::Modulation::LFOShape::Square:    return "Square";
        case InstrumentParams::Modulation::LFOShape::Random:    return "Random";
    }
    return "???";
}

juce::String SampleEditorComponent::getModDestFullName (int dest) const
{
    switch (static_cast<InstrumentParams::ModDest> (dest))
    {
        case InstrumentParams::ModDest::Volume:        return "Volume";
        case InstrumentParams::ModDest::Panning:       return "Panning";
        case InstrumentParams::ModDest::Cutoff:        return "Cutoff";
        case InstrumentParams::ModDest::GranularPos:   return "Granular Position";
        case InstrumentParams::ModDest::Finetune:      return "Finetune";
    }
    return "???";
}

juce::String SampleEditorComponent::getGranShapeName (InstrumentParams::GranShape shape) const
{
    switch (shape)
    {
        case InstrumentParams::GranShape::Square:    return "Square";
        case InstrumentParams::GranShape::Triangle:  return "Triangle";
        case InstrumentParams::GranShape::Gauss:     return "Gauss";
    }
    return "???";
}

juce::String SampleEditorComponent::getGranLoopName (InstrumentParams::GranLoop loop) const
{
    switch (loop)
    {
        case InstrumentParams::GranLoop::Forward:   return "Forward";
        case InstrumentParams::GranLoop::Reverse:   return "Reverse";
        case InstrumentParams::GranLoop::Pingpong:  return "Pingpong";
    }
    return "???";
}

juce::String SampleEditorComponent::formatLfoSpeed (int speed) const
{
    if (speed == 1) return "1 step";
    return juce::String (speed) + " steps";
}

//==============================================================================
// Focus helpers
//==============================================================================

int SampleEditorComponent::getFocusedColumn() const
{
    if (displayMode == DisplayMode::InstrumentEdit)
    {
        if (editSubTab == EditSubTab::Parameters)
            return parametersColumn;
        else
            return modColumn;
    }
    else // InstrumentType
    {
        return playbackColumn;
    }
}

void SampleEditorComponent::setFocusedColumn (int col)
{
    int count = getColumnCount();
    col = juce::jlimit (0, juce::jmax (0, count - 1), col);

    if (displayMode == DisplayMode::InstrumentEdit)
    {
        if (editSubTab == EditSubTab::Parameters)
            parametersColumn = col;
        else
            modColumn = col;
    }
    else
    {
        playbackColumn = col;
    }
}

int SampleEditorComponent::getColumnCount() const
{
    if (displayMode == DisplayMode::InstrumentEdit)
    {
        if (editSubTab == EditSubTab::Parameters)
            return 11; // Vol, Pan, Tune, Fine, Filter, Cutoff, Rez, OD, BitDepth, Reverb, Delay
        else
            return 7; // Modulation page
    }
    else // InstrumentType
    {
        auto mode = currentParams.playMode;
        switch (mode)
        {
            case InstrumentParams::PlayMode::OneShot:                       return 4;
            case InstrumentParams::PlayMode::ForwardLoop:
            case InstrumentParams::PlayMode::BackwardLoop:
            case InstrumentParams::PlayMode::PingpongLoop:                  return 5;
            case InstrumentParams::PlayMode::Slice:
            case InstrumentParams::PlayMode::BeatSlice:                     return 3;
            case InstrumentParams::PlayMode::Granular:                      return 7;
        }
        return 4;
    }
}

//==============================================================================
// Bottom bar info: column names and values
//==============================================================================

juce::String SampleEditorComponent::getColumnName (int col) const
{
    if (displayMode == DisplayMode::InstrumentEdit)
    {
        if (editSubTab == EditSubTab::Parameters)
        {
            const char* names[] = { "Volume", "Panning", "Tune", "Finetune", "Filter",
                                    "Cutoff", "Resonance", "Overdrive", "Bit Depth", "Reverb", "Delay" };
            if (col >= 0 && col < 11) return names[col];
        }
        else // Modulation
        {
            if (col == 0) return "Destination";
            if (col == 1) return "Type";

            auto& mod = currentParams.modulations[static_cast<size_t> (modDestIndex)];
            if (mod.type == InstrumentParams::Modulation::Type::LFO)
            {
                const char* names[] = { "", "", "Shape", "Speed", "Amount" };
                if (col >= 2 && col < 5) return names[col];
            }
            else if (mod.type == InstrumentParams::Modulation::Type::Envelope)
            {
                const char* names[] = { "", "", "Attack", "Decay", "Sustain", "Release", "Amount" };
                if (col >= 2 && col < 7) return names[col];
            }
        }
    }
    else // InstrumentType
    {
        int numCols = getColumnCount();
        if (col == numCols - 1) return "Play Mode";

        auto mode = currentParams.playMode;
        switch (mode)
        {
            case InstrumentParams::PlayMode::OneShot:
            {
                const char* n[] = { "Start", "End", "Reverse" };
                if (col < 3) return n[col];
                break;
            }
            case InstrumentParams::PlayMode::ForwardLoop:
            case InstrumentParams::PlayMode::BackwardLoop:
            case InstrumentParams::PlayMode::PingpongLoop:
            {
                const char* n[] = { "Start", "Loop Start", "Loop End", "End" };
                if (col < 4) return n[col];
                break;
            }
            case InstrumentParams::PlayMode::Slice:
            case InstrumentParams::PlayMode::BeatSlice:
            {
                const char* n[] = { "Start", "End" };
                if (col < 2) return n[col];
                break;
            }
            case InstrumentParams::PlayMode::Granular:
            {
                const char* n[] = { "Start", "End", "Grain Pos", "Grain Len", "Shape", "Loop" };
                if (col < 6) return n[col];
                break;
            }
        }
    }
    return {};
}

juce::String SampleEditorComponent::getColumnValue (int col) const
{
    double totalLen = thumbnail.getTotalLength();
    if (totalLen <= 0.0) totalLen = 1.0;

    if (displayMode == DisplayMode::InstrumentEdit)
    {
        if (editSubTab == EditSubTab::Parameters)
        {
            switch (col)
            {
                case 0: return formatDb (currentParams.volume);
                case 1: return formatPan (currentParams.panning);
                case 2: return formatSemitones (currentParams.tune);
                case 3: return formatCents (currentParams.finetune);
                case 4: return getFilterTypeName (currentParams.filterType);
                case 5: return formatPercent (currentParams.cutoff);
                case 6: return formatPercent (currentParams.resonance);
                case 7: return formatPercent (currentParams.overdrive);
                case 8: return juce::String (currentParams.bitDepth);
                case 9: return formatDb (currentParams.reverbSend);
                case 10: return formatDb (currentParams.delaySend);
            }
        }
        else // Modulation
        {
            auto& mod = currentParams.modulations[static_cast<size_t> (modDestIndex)];
            if (col == 0) return getModDestFullName (modDestIndex);
            if (col == 1) return getModTypeName (mod.type);

            if (mod.type == InstrumentParams::Modulation::Type::LFO)
            {
                switch (col)
                {
                    case 2: return getLfoShapeName (mod.lfoShape);
                    case 3: return formatLfoSpeed (mod.lfoSpeed);
                    case 4: return juce::String (mod.amount);
                }
            }
            else if (mod.type == InstrumentParams::Modulation::Type::Envelope)
            {
                switch (col)
                {
                    case 2: return formatSeconds (mod.attackS);
                    case 3: return formatSeconds (mod.decayS);
                    case 4: return juce::String (mod.sustain);
                    case 5: return formatSeconds (mod.releaseS);
                    case 6: return juce::String (mod.amount);
                }
            }
        }
    }
    else // InstrumentType
    {
        int numCols = getColumnCount();
        if (col == numCols - 1) return getPlayModeName (currentParams.playMode);

        auto mode = currentParams.playMode;
        switch (mode)
        {
            case InstrumentParams::PlayMode::OneShot:
                switch (col)
                {
                    case 0: return formatPosSec (currentParams.startPos, totalLen);
                    case 1: return formatPosSec (currentParams.endPos, totalLen);
                    case 2: return currentParams.reversed ? "ON" : "OFF";
                }
                break;

            case InstrumentParams::PlayMode::ForwardLoop:
            case InstrumentParams::PlayMode::BackwardLoop:
            case InstrumentParams::PlayMode::PingpongLoop:
                switch (col)
                {
                    case 0: return formatPosSec (currentParams.startPos, totalLen);
                    case 1: return formatPosSec (currentParams.loopStart, totalLen);
                    case 2: return formatPosSec (currentParams.loopEnd, totalLen);
                    case 3: return formatPosSec (currentParams.endPos, totalLen);
                }
                break;

            case InstrumentParams::PlayMode::Slice:
            case InstrumentParams::PlayMode::BeatSlice:
                switch (col)
                {
                    case 0: return formatPosSec (currentParams.startPos, totalLen);
                    case 1: return formatPosSec (currentParams.endPos, totalLen);
                }
                break;

            case InstrumentParams::PlayMode::Granular:
                switch (col)
                {
                    case 0: return formatPosSec (currentParams.startPos, totalLen);
                    case 1: return formatPosSec (currentParams.endPos, totalLen);
                    case 2: return formatPosSec (currentParams.granularPosition, totalLen);
                    case 3: return juce::String (currentParams.granularLength) + "ms";
                    case 4: return getGranShapeName (currentParams.granularShape);
                    case 5: return getGranLoopName (currentParams.granularLoop);
                }
                break;
        }
    }
    return {};
}

//==============================================================================
// Paint
//==============================================================================

void SampleEditorComponent::paint (juce::Graphics& g)
{
    auto bg = lookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId);
    g.fillAll (bg);

    if (currentInstrument < 0)
    {
        g.setFont (lookAndFeel.getMonoFont (12.0f));
        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.25f));
        g.drawText ("No instrument selected", getLocalBounds(), juce::Justification::centred);
        return;
    }

    // Outer border
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawRect (getLocalBounds(), 1);

    // Header
    drawHeader (g, { 0, 0, getWidth(), kHeaderHeight });

    // Bottom bar
    auto bottomBarArea = juce::Rectangle<int> (0, getHeight() - kBottomBarHeight,
                                                getWidth(), kBottomBarHeight);
    drawBottomBar (g, bottomBarArea);

    // Content area between header and bottom bar
    int contentTop = kHeaderHeight;
    int contentBottom = getHeight() - kBottomBarHeight;
    auto contentArea = juce::Rectangle<int> (0, contentTop, getWidth(), contentBottom - contentTop);

    if (displayMode == DisplayMode::InstrumentEdit)
    {
        // Sub-tab sidebar on the left
        auto subTabArea = contentArea.removeFromLeft (kSubTabWidth);
        drawSubTabBar (g, subTabArea);

        if (editSubTab == EditSubTab::Parameters)
            drawParametersPage (g, contentArea);
        else
            drawModulationPage (g, contentArea);
    }
    else // InstrumentType
    {
        drawPlaybackPage (g, contentArea);
    }
}

void SampleEditorComponent::resized() {}

//==============================================================================
// Drawing: Sub-tab sidebar
//==============================================================================

void SampleEditorComponent::drawSubTabBar (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto bg = lookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId).brighter (0.03f);
    auto gridCol = lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId);
    auto textCol = lookAndFeel.findColour (TrackerLookAndFeel::textColourId);
    auto accentCol = lookAndFeel.findColour (TrackerLookAndFeel::fxColourId);

    g.setColour (bg);
    g.fillRect (area);

    // Right border
    g.setColour (gridCol);
    g.drawVerticalLine (area.getRight() - 1, static_cast<float> (area.getY()),
                        static_cast<float> (area.getBottom()));

    struct SubTabItem { juce::String label; EditSubTab tab; };
    SubTabItem items[] = { { "PARAMS", EditSubTab::Parameters }, { "MOD", EditSubTab::Modulation } };

    g.setFont (lookAndFeel.getMonoFont (10.0f));
    int itemH = 30;

    for (int i = 0; i < 2; ++i)
    {
        auto itemArea = juce::Rectangle<int> (area.getX(), area.getY() + i * itemH,
                                               area.getWidth(), itemH);
        bool active = (items[i].tab == editSubTab);

        if (active)
        {
            // Accent indicator on the left
            g.setColour (accentCol);
            g.fillRect (area.getX(), itemArea.getY() + 4, 3, itemH - 8);
        }

        g.setColour (active ? textCol : textCol.withAlpha (0.4f));
        g.drawText (items[i].label, itemArea.withTrimmedLeft (8), juce::Justification::centredLeft);
    }
}

//==============================================================================
// Drawing: Header
//==============================================================================

void SampleEditorComponent::drawHeader (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::headerColourId));
    g.fillRect (area);

    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (area.getBottom() - 1, static_cast<float> (area.getX()),
                          static_cast<float> (area.getRight()));

    // Page title (left)
    g.setFont (lookAndFeel.getMonoFont (12.0f));
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::textColourId));

    juce::String title;
    if (displayMode == DisplayMode::InstrumentEdit)
    {
        if (editSubTab == EditSubTab::Parameters)
            title = "Instrument Parameters";
        else
            title = "Instrument Automation";
    }
    else
    {
        title = "Sample Playback";
    }

    g.drawText (title, area.getX() + 8, area.getY(), area.getWidth() / 2, area.getHeight(),
                juce::Justification::centredLeft);

    // Instrument info (right)
    g.setFont (lookAndFeel.getMonoFont (11.0f));
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::instrumentColourId));
    juce::String instInfo = juce::String::formatted ("%d.", currentInstrument)
                          + currentFile.getFileNameWithoutExtension();
    g.drawText (instInfo, area.getWidth() / 2, area.getY(), area.getWidth() / 2 - 8, area.getHeight(),
                juce::Justification::centredRight);
}

//==============================================================================
// Drawing: Bottom bar
//==============================================================================

void SampleEditorComponent::drawBottomBar (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto bg = lookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId).brighter (0.06f);
    auto gridCol = lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId);
    auto textCol = lookAndFeel.findColour (TrackerLookAndFeel::textColourId);
    auto cursorCol = lookAndFeel.findColour (TrackerLookAndFeel::cursorCellColourId);

    g.setColour (bg);
    g.fillRect (area);

    // Top border
    g.setColour (gridCol);
    g.drawHorizontalLine (area.getY(), static_cast<float> (area.getX()),
                          static_cast<float> (area.getRight()));

    int numCols = getColumnCount();
    if (numCols == 0) return;

    int focusCol = getFocusedColumn();
    int colW = area.getWidth() / numCols;

    int nameRowY = area.getY() + 2;
    int nameRowH = 16;
    int valRowY = nameRowY + nameRowH;
    int valRowH = area.getHeight() - nameRowH - 4;

    for (int col = 0; col < numCols; ++col)
    {
        int x = area.getX() + col * colW;
        int w = (col < numCols - 1) ? colW : (area.getWidth() - col * colW);
        bool focused = (col == focusCol);

        if (focused)
        {
            g.setColour (cursorCol);
            g.fillRect (x, area.getY() + 1, w, area.getHeight() - 1);
        }

        // Column name
        g.setFont (lookAndFeel.getMonoFont (9.0f));
        g.setColour (textCol.withAlpha (focused ? 0.9f : 0.45f));
        g.drawText (getColumnName (col), x + 2, nameRowY, w - 4, nameRowH,
                    juce::Justification::centred);

        // Column value
        g.setFont (lookAndFeel.getMonoFont (11.0f));
        g.setColour (textCol.withAlpha (focused ? 1.0f : 0.65f));
        g.drawText (getColumnValue (col), x + 2, valRowY, w - 4, valRowH,
                    juce::Justification::centred);

        // Separator
        if (col < numCols - 1)
        {
            g.setColour (gridCol.withAlpha (0.5f));
            g.drawVerticalLine (x + w, static_cast<float> (area.getY() + 1),
                                static_cast<float> (area.getBottom()));
        }
    }
}

//==============================================================================
// Drawing: List column
//==============================================================================

void SampleEditorComponent::drawListColumn (juce::Graphics& g, juce::Rectangle<int> area,
                                             const juce::StringArray& items, int selectedIndex,
                                             bool focused, juce::Colour colour)
{
    auto bg = lookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId);
    auto gridCol = lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId);
    auto textCol = lookAndFeel.findColour (TrackerLookAndFeel::textColourId);

    // Column border
    g.setColour (focused ? gridCol.brighter (0.4f) : gridCol);
    g.drawRect (area, 1);

    auto inner = area.reduced (1);
    int numItems = items.size();
    if (numItems == 0) return;

    // Calculate visible items and scrolling
    int maxVisible = inner.getHeight() / kListItemHeight;
    int scrollOffset = 0;

    if (numItems > maxVisible && selectedIndex >= 0)
        scrollOffset = juce::jlimit (0, numItems - maxVisible,
                                      selectedIndex - maxVisible / 2);

    int visibleCount = juce::jmin (numItems - scrollOffset, maxVisible);

    g.setFont (lookAndFeel.getMonoFont (11.0f));

    for (int vi = 0; vi < visibleCount; ++vi)
    {
        int i = scrollOffset + vi;
        int y = inner.getY() + vi * kListItemHeight;
        auto itemRect = juce::Rectangle<int> (inner.getX(), y, inner.getWidth(), kListItemHeight);

        if (i == selectedIndex)
        {
            // Highlighted item: filled background with inverted text
            g.setColour (focused ? colour : colour.withAlpha (0.4f));
            g.fillRect (itemRect);
            g.setColour (focused ? bg : textCol);
        }
        else
        {
            g.setColour (textCol.withAlpha (focused ? 0.65f : 0.35f));
        }

        g.drawText (items[i], itemRect.reduced (6, 0), juce::Justification::centredLeft);
    }

    // Scroll indicators
    if (scrollOffset > 0)
    {
        g.setColour (textCol.withAlpha (0.3f));
        g.drawText ("...", inner.getX(), inner.getY() - 2, inner.getWidth(), 12,
                    juce::Justification::centredRight);
    }
    if (scrollOffset + visibleCount < numItems)
    {
        g.setColour (textCol.withAlpha (0.3f));
        int bottomY = inner.getY() + visibleCount * kListItemHeight;
        g.drawText ("...", inner.getX(), bottomY, inner.getWidth(), 12,
                    juce::Justification::centredRight);
    }
}

//==============================================================================
// Drawing: Bar meter
//==============================================================================

void SampleEditorComponent::drawBarMeter (juce::Graphics& g, juce::Rectangle<int> area,
                                           float value01, bool focused, juce::Colour colour)
{
    auto bg = lookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId);
    auto gridCol = lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId);

    // Column border
    g.setColour (focused ? gridCol.brighter (0.4f) : gridCol);
    g.drawRect (area, 1);

    // Inner bar area with padding
    auto inner = area.reduced (6, 4);

    // Bar background
    g.setColour (bg.brighter (0.04f));
    g.fillRect (inner);

    // Bar outline
    g.setColour (gridCol.withAlpha (0.6f));
    g.drawRect (inner, 1);

    // Bar fill from bottom
    value01 = juce::jlimit (0.0f, 1.0f, value01);
    int fillH = juce::roundToInt (value01 * static_cast<float> (inner.getHeight() - 2));

    if (fillH > 0)
    {
        auto fillRect = juce::Rectangle<int> (
            inner.getX() + 1,
            inner.getBottom() - 1 - fillH,
            inner.getWidth() - 2,
            fillH);

        g.setColour (colour.withAlpha (focused ? 0.85f : 0.5f));
        g.fillRect (fillRect);
    }
}

//==============================================================================
// Drawing: Parameters page (merged General + Effects = 11 columns)
//==============================================================================

void SampleEditorComponent::drawParametersPage (juce::Graphics& g, juce::Rectangle<int> area)
{
    int numCols = 11;
    int colW = area.getWidth() / numCols;
    auto greenCol = lookAndFeel.findColour (TrackerLookAndFeel::volumeColourId);
    auto blueCol = lookAndFeel.findColour (TrackerLookAndFeel::fxColourId);
    auto textCol = lookAndFeel.findColour (TrackerLookAndFeel::textColourId);
    auto amberCol = lookAndFeel.findColour (TrackerLookAndFeel::instrumentColourId);

    auto colRect = [&] (int c) -> juce::Rectangle<int>
    {
        int w = (c < numCols - 1) ? colW : (area.getWidth() - c * colW);
        return { area.getX() + c * colW, area.getY(), w, area.getHeight() };
    };

    // Col 0: Volume bar
    float vol01 = static_cast<float> ((currentParams.volume + 100.0) / 124.0);
    drawBarMeter (g, colRect (0), vol01, parametersColumn == 0, greenCol);

    // Col 1: Panning bar
    float pan01 = static_cast<float> (currentParams.panning + 50) / 100.0f;
    drawBarMeter (g, colRect (1), pan01, parametersColumn == 1, textCol);

    // Col 2: Tune bar
    float tune01 = static_cast<float> (currentParams.tune + 24) / 48.0f;
    drawBarMeter (g, colRect (2), tune01, parametersColumn == 2, textCol);

    // Col 3: Finetune bar
    float fine01 = static_cast<float> (currentParams.finetune + 100) / 200.0f;
    drawBarMeter (g, colRect (3), fine01, parametersColumn == 3, textCol);

    // Col 4: Filter type list
    juce::StringArray filterItems = { "Off", "LowPass", "HighPass", "BandPass" };
    int filterIdx = static_cast<int> (currentParams.filterType);
    drawListColumn (g, colRect (4), filterItems, filterIdx, parametersColumn == 4, blueCol);

    // Col 5: Cutoff bar
    float cut01 = static_cast<float> (currentParams.cutoff) / 100.0f;
    drawBarMeter (g, colRect (5), cut01, parametersColumn == 5, blueCol);

    // Col 6: Resonance bar
    float rez01 = static_cast<float> (currentParams.resonance) / 100.0f;
    drawBarMeter (g, colRect (6), rez01, parametersColumn == 6, blueCol);

    // Col 7: Overdrive bar
    float od01 = static_cast<float> (currentParams.overdrive) / 100.0f;
    drawBarMeter (g, colRect (7), od01, parametersColumn == 7, amberCol);

    // Col 8: Bit Depth bar
    float bd01 = static_cast<float> (currentParams.bitDepth - 4) / 12.0f;
    drawBarMeter (g, colRect (8), bd01, parametersColumn == 8, amberCol);

    // Col 9: Reverb Send bar
    float rv01 = static_cast<float> ((currentParams.reverbSend + 100.0) / 100.0);
    drawBarMeter (g, colRect (9), rv01, parametersColumn == 9, amberCol);

    // Col 10: Delay Send bar
    float dl01 = static_cast<float> ((currentParams.delaySend + 100.0) / 100.0);
    drawBarMeter (g, colRect (10), dl01, parametersColumn == 10, amberCol);
}

//==============================================================================
// Drawing: Modulation page
//==============================================================================

void SampleEditorComponent::drawModulationPage (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto& mod = currentParams.modulations[static_cast<size_t> (modDestIndex)];
    int numCols = 7;
    int colW = area.getWidth() / numCols;
    auto orangeCol = juce::Colour (0xffffaa44);
    auto gridCol = lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId);

    // Col 0: Destination list
    juce::StringArray destItems;
    for (int i = 0; i < InstrumentParams::kNumModDests; ++i)
        destItems.add (getModDestFullName (i));
    drawListColumn (g, { area.getX(), area.getY(), colW, area.getHeight() },
                    destItems, modDestIndex, modColumn == 0, orangeCol);

    // Col 1: Type list
    juce::StringArray typeItems = { "Off", "Envelope", "LFO" };
    int typeIdx = static_cast<int> (mod.type);
    drawListColumn (g, { area.getX() + colW, area.getY(), colW, area.getHeight() },
                    typeItems, typeIdx, modColumn == 1, orangeCol);

    // Helper to draw an empty column with just a border
    auto drawEmptyCol = [&] (int c)
    {
        int w = (c < numCols - 1) ? colW : (area.getWidth() - (numCols - 1) * colW);
        auto colArea = juce::Rectangle<int> (area.getX() + c * colW, area.getY(), w, area.getHeight());
        g.setColour (gridCol);
        g.drawRect (colArea, 1);
    };

    if (mod.type == InstrumentParams::Modulation::Type::LFO)
    {
        // Col 2: Shape list
        juce::StringArray shapeItems = { "Rev Saw", "Saw", "Triangle", "Square", "Random" };
        int shapeIdx = static_cast<int> (mod.lfoShape);
        drawListColumn (g, { area.getX() + 2 * colW, area.getY(), colW, area.getHeight() },
                        shapeItems, shapeIdx, modColumn == 2, orangeCol);

        // Col 3: Speed list
        juce::StringArray speedItems;
        int speedSelectedIdx = -1;
        for (int i = 0; i < kNumLfoSpeeds; ++i)
        {
            speedItems.add (formatLfoSpeed (kLfoSpeeds[i]));
            if (kLfoSpeeds[i] == mod.lfoSpeed)
                speedSelectedIdx = i;
        }
        if (speedSelectedIdx < 0)
        {
            speedItems.add (formatLfoSpeed (mod.lfoSpeed));
            speedSelectedIdx = speedItems.size() - 1;
        }
        drawListColumn (g, { area.getX() + 3 * colW, area.getY(), colW, area.getHeight() },
                        speedItems, speedSelectedIdx, modColumn == 3, orangeCol);

        // Col 4: Amount bar
        float amt01 = static_cast<float> (mod.amount) / 100.0f;
        drawBarMeter (g, { area.getX() + 4 * colW, area.getY(), colW, area.getHeight() },
                      amt01, modColumn == 4, orangeCol);

        // Cols 5-6: Empty
        for (int c = 5; c < numCols; ++c)
            drawEmptyCol (c);
    }
    else if (mod.type == InstrumentParams::Modulation::Type::Envelope)
    {
        // Col 2: Attack bar
        float atk01 = static_cast<float> (mod.attackS / 10.0);
        drawBarMeter (g, { area.getX() + 2 * colW, area.getY(), colW, area.getHeight() },
                      atk01, modColumn == 2, orangeCol);

        // Col 3: Decay bar
        float dec01 = static_cast<float> (mod.decayS / 10.0);
        drawBarMeter (g, { area.getX() + 3 * colW, area.getY(), colW, area.getHeight() },
                      dec01, modColumn == 3, orangeCol);

        // Col 4: Sustain bar
        float sus01 = static_cast<float> (mod.sustain) / 100.0f;
        drawBarMeter (g, { area.getX() + 4 * colW, area.getY(), colW, area.getHeight() },
                      sus01, modColumn == 4, orangeCol);

        // Col 5: Release bar
        float rel01 = static_cast<float> (mod.releaseS / 10.0);
        drawBarMeter (g, { area.getX() + 5 * colW, area.getY(), colW, area.getHeight() },
                      rel01, modColumn == 5, orangeCol);

        // Col 6: Amount bar
        float amt01 = static_cast<float> (mod.amount) / 100.0f;
        int lastColW = area.getWidth() - 6 * colW;
        drawBarMeter (g, { area.getX() + 6 * colW, area.getY(), lastColW, area.getHeight() },
                      amt01, modColumn == 6, orangeCol);
    }
    else // Off
    {
        for (int c = 2; c < numCols; ++c)
            drawEmptyCol (c);
    }
}

//==============================================================================
// Drawing: Playback page
//==============================================================================

void SampleEditorComponent::drawPlaybackPage (juce::Graphics& g, juce::Rectangle<int> area)
{
    // Waveform fills the content area
    auto waveArea = area.reduced (4, 4);
    drawWaveform (g, waveArea);
    drawWaveformMarkers (g, waveArea);

    // Play mode list overlay in top-right corner of waveform
    int numCols = getColumnCount();
    bool modeColFocused = (playbackColumn == numCols - 1);

    juce::StringArray modeItems = {
        "1-Shot", "Forward loop", "Backward loop", "Pingpong loop",
        "Slice", "Beat Slice", "Granular"
    };
    int modeIdx = static_cast<int> (currentParams.playMode);

    int listW = 140;
    int listH = 7 * kListItemHeight + 2;
    int listX = waveArea.getRight() - listW - 2;
    int listY = waveArea.getY() + 2;
    auto listArea = juce::Rectangle<int> (listX, listY, listW, listH);

    // Semi-transparent background behind the list
    auto bg = lookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId);
    g.setColour (bg.withAlpha (0.85f));
    g.fillRect (listArea);

    auto textCol = lookAndFeel.findColour (TrackerLookAndFeel::textColourId);
    drawListColumn (g, listArea, modeItems, modeIdx, modeColFocused, textCol);
}

//==============================================================================
// Drawing: Waveform
//==============================================================================

void SampleEditorComponent::drawWaveform (juce::Graphics& g, juce::Rectangle<int> area)
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

    if (thumbnail.getTotalLength() > 0.0)
    {
        // Shade outside start/end
        float startX = area.getX() + static_cast<float> (currentParams.startPos * area.getWidth());
        float endX   = area.getX() + static_cast<float> (currentParams.endPos * area.getWidth());

        g.setColour (juce::Colour (0x40000000));
        g.fillRect (static_cast<float> (area.getX()), static_cast<float> (area.getY()),
                    startX - area.getX(), static_cast<float> (area.getHeight()));
        g.fillRect (endX, static_cast<float> (area.getY()),
                    static_cast<float> (area.getRight()) - endX,
                    static_cast<float> (area.getHeight()));

        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::fxColourId).withAlpha (0.7f));
        thumbnail.drawChannels (g, area.reduced (1), 0.0, thumbnail.getTotalLength(), 1.0f);
    }
    else
    {
        g.setFont (lookAndFeel.getMonoFont (12.0f));
        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.25f));
        g.drawText ("No waveform data", area, juce::Justification::centred);
    }
}

void SampleEditorComponent::drawWaveformMarkers (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (thumbnail.getTotalLength() <= 0.0) return;

    auto drawMarker = [&] (double normPos, juce::Colour colour, const juce::String& label)
    {
        int x = area.getX() + juce::roundToInt (normPos * area.getWidth());
        g.setColour (colour);
        g.drawVerticalLine (x, static_cast<float> (area.getY()),
                            static_cast<float> (area.getBottom()));
        g.setFont (lookAndFeel.getMonoFont (9.0f));
        g.drawText (label, x + 2, area.getY() + 2, 30, 12, juce::Justification::centredLeft);
    };

    drawMarker (currentParams.startPos, juce::Colour (0xff44cc44), "S");
    drawMarker (currentParams.endPos, juce::Colour (0xffcc4444), "E");

    auto mode = currentParams.playMode;
    if (mode == InstrumentParams::PlayMode::ForwardLoop
        || mode == InstrumentParams::PlayMode::BackwardLoop
        || mode == InstrumentParams::PlayMode::PingpongLoop)
    {
        drawMarker (currentParams.loopStart, juce::Colour (0xff4488ff), "LS");
        drawMarker (currentParams.loopEnd, juce::Colour (0xff4488ff), "LE");
    }

    if (mode == InstrumentParams::PlayMode::Slice || mode == InstrumentParams::PlayMode::BeatSlice)
    {
        for (auto sp : currentParams.slicePoints)
            drawMarker (sp, juce::Colour (0xffddcc44), "|");
    }

    if (mode == InstrumentParams::PlayMode::Granular)
        drawMarker (currentParams.granularPosition, juce::Colour (0xffffaa44), "G");
}

//==============================================================================
// Value Adjustment
//==============================================================================

void SampleEditorComponent::adjustCurrentValue (int direction, bool fine, bool large)
{
    if (displayMode == DisplayMode::InstrumentEdit)
    {
        if (editSubTab == EditSubTab::Parameters)
        {
            switch (parametersColumn)
            {
                case 0: // Volume
                {
                    double step = fine ? 0.1 : (large ? 6.0 : 1.0);
                    currentParams.volume = juce::jlimit (-100.0, 24.0,
                        currentParams.volume + direction * step);
                    break;
                }
                case 1: // Panning
                {
                    int step = fine ? 1 : (large ? 10 : 5);
                    currentParams.panning = juce::jlimit (-50, 50,
                        currentParams.panning + direction * step);
                    break;
                }
                case 2: // Tune
                {
                    int step = fine ? 1 : (large ? 12 : 1);
                    currentParams.tune = juce::jlimit (-24, 24,
                        currentParams.tune + direction * step);
                    break;
                }
                case 3: // Finetune
                {
                    int step = fine ? 1 : (large ? 25 : 5);
                    currentParams.finetune = juce::jlimit (-100, 100,
                        currentParams.finetune + direction * step);
                    break;
                }
                case 4: // Filter type
                {
                    int v = static_cast<int> (currentParams.filterType);
                    v = (v - direction + 4) % 4;
                    currentParams.filterType = static_cast<InstrumentParams::FilterType> (v);
                    break;
                }
                case 5: // Cutoff
                {
                    int step = fine ? 1 : (large ? 10 : 5);
                    currentParams.cutoff = juce::jlimit (0, 100,
                        currentParams.cutoff + direction * step);
                    break;
                }
                case 6: // Resonance (capped at 85 for speaker safety)
                {
                    int step = fine ? 1 : (large ? 10 : 5);
                    currentParams.resonance = juce::jlimit (0, 85,
                        currentParams.resonance + direction * step);
                    break;
                }
                case 7: // Overdrive
                {
                    int step = fine ? 1 : (large ? 10 : 5);
                    currentParams.overdrive = juce::jlimit (0, 100,
                        currentParams.overdrive + direction * step);
                    break;
                }
                case 8: // Bit Depth
                    currentParams.bitDepth = juce::jlimit (4, 16,
                        currentParams.bitDepth + direction);
                    break;
                case 9: // Reverb Send
                {
                    double step = fine ? 0.1 : (large ? 6.0 : 1.0);
                    currentParams.reverbSend = juce::jlimit (-100.0, 0.0,
                        currentParams.reverbSend + direction * step);
                    break;
                }
                case 10: // Delay Send
                {
                    double step = fine ? 0.1 : (large ? 6.0 : 1.0);
                    currentParams.delaySend = juce::jlimit (-100.0, 0.0,
                        currentParams.delaySend + direction * step);
                    break;
                }
            }
        }
        else // Modulation
        {
            auto& mod = currentParams.modulations[static_cast<size_t> (modDestIndex)];

            switch (modColumn)
            {
                case 0: // Destination
                    modDestIndex = (modDestIndex - direction + InstrumentParams::kNumModDests)
                                   % InstrumentParams::kNumModDests;
                    break;

                case 1: // Type
                {
                    int v = static_cast<int> (mod.type);
                    v = (v - direction + 3) % 3;
                    mod.type = static_cast<InstrumentParams::Modulation::Type> (v);
                    break;
                }

                case 2: // Shape (LFO) or Attack (Envelope)
                {
                    if (mod.type == InstrumentParams::Modulation::Type::LFO)
                    {
                        int v = static_cast<int> (mod.lfoShape);
                        v = (v - direction + 5) % 5;
                        mod.lfoShape = static_cast<InstrumentParams::Modulation::LFOShape> (v);
                    }
                    else if (mod.type == InstrumentParams::Modulation::Type::Envelope)
                    {
                        double step = fine ? 0.001 : (large ? 0.5 : 0.01);
                        mod.attackS = juce::jlimit (0.0, 10.0, mod.attackS + direction * step);
                    }
                    break;
                }

                case 3: // Speed (LFO) or Decay (Envelope)
                {
                    if (mod.type == InstrumentParams::Modulation::Type::LFO)
                    {
                        // Jump between speed presets
                        int curIdx = -1;
                        for (int i = 0; i < kNumLfoSpeeds; ++i)
                        {
                            if (kLfoSpeeds[i] == mod.lfoSpeed)
                            {
                                curIdx = i;
                                break;
                            }
                        }
                        if (curIdx < 0)
                        {
                            // Find nearest preset
                            curIdx = 0;
                            for (int i = 1; i < kNumLfoSpeeds; ++i)
                                if (std::abs (kLfoSpeeds[i] - mod.lfoSpeed) < std::abs (kLfoSpeeds[curIdx] - mod.lfoSpeed))
                                    curIdx = i;
                        }
                        curIdx = juce::jlimit (0, kNumLfoSpeeds - 1, curIdx - direction);
                        mod.lfoSpeed = kLfoSpeeds[curIdx];
                    }
                    else if (mod.type == InstrumentParams::Modulation::Type::Envelope)
                    {
                        double step = fine ? 0.001 : (large ? 0.5 : 0.01);
                        mod.decayS = juce::jlimit (0.0, 10.0, mod.decayS + direction * step);
                    }
                    break;
                }

                case 4: // Amount (LFO) or Sustain (Envelope)
                {
                    if (mod.type == InstrumentParams::Modulation::Type::LFO)
                    {
                        int step = fine ? 1 : (large ? 10 : 5);
                        mod.amount = juce::jlimit (0, 100, mod.amount + direction * step);
                    }
                    else if (mod.type == InstrumentParams::Modulation::Type::Envelope)
                    {
                        int step = fine ? 1 : (large ? 10 : 5);
                        mod.sustain = juce::jlimit (0, 100, mod.sustain + direction * step);
                    }
                    break;
                }

                case 5: // Release (Envelope only)
                {
                    if (mod.type == InstrumentParams::Modulation::Type::Envelope)
                    {
                        double step = fine ? 0.001 : (large ? 0.5 : 0.01);
                        mod.releaseS = juce::jlimit (0.0, 10.0, mod.releaseS + direction * step);
                    }
                    break;
                }

                case 6: // Amount (Envelope only)
                {
                    if (mod.type == InstrumentParams::Modulation::Type::Envelope)
                    {
                        int step = fine ? 1 : (large ? 10 : 5);
                        mod.amount = juce::jlimit (0, 100, mod.amount + direction * step);
                    }
                    break;
                }
            }
        }
    }
    else // InstrumentType (Playback)
    {
        auto mode = currentParams.playMode;
        int numCols = getColumnCount();

        // Last column is always Play Mode
        if (playbackColumn == numCols - 1)
        {
            int v = static_cast<int> (mode);
            v = (v - direction + 7) % 7;
            currentParams.playMode = static_cast<InstrumentParams::PlayMode> (v);
            playbackColumn = getColumnCount() - 1;
            notifyParamsChanged();
            return;
        }

        switch (mode)
        {
            case InstrumentParams::PlayMode::OneShot:
            {
                switch (playbackColumn)
                {
                    case 0: // Start
                    {
                        double step = fine ? 0.001 : (large ? 0.1 : 0.01);
                        currentParams.startPos = juce::jlimit (0.0, currentParams.endPos,
                            currentParams.startPos + direction * step);
                        break;
                    }
                    case 1: // End
                    {
                        double step = fine ? 0.001 : (large ? 0.1 : 0.01);
                        currentParams.endPos = juce::jlimit (currentParams.startPos, 1.0,
                            currentParams.endPos + direction * step);
                        break;
                    }
                    case 2: // Reverse
                        currentParams.reversed = ! currentParams.reversed;
                        break;
                }
                break;
            }

            case InstrumentParams::PlayMode::ForwardLoop:
            case InstrumentParams::PlayMode::BackwardLoop:
            case InstrumentParams::PlayMode::PingpongLoop:
            {
                double step = fine ? 0.001 : (large ? 0.1 : 0.01);
                switch (playbackColumn)
                {
                    case 0: // Start
                        currentParams.startPos = juce::jlimit (0.0, currentParams.endPos,
                            currentParams.startPos + direction * step);
                        break;
                    case 1: // Loop Start
                        currentParams.loopStart = juce::jlimit (0.0, currentParams.loopEnd,
                            currentParams.loopStart + direction * step);
                        break;
                    case 2: // Loop End
                        currentParams.loopEnd = juce::jlimit (currentParams.loopStart, 1.0,
                            currentParams.loopEnd + direction * step);
                        break;
                    case 3: // End
                        currentParams.endPos = juce::jlimit (currentParams.startPos, 1.0,
                            currentParams.endPos + direction * step);
                        break;
                }
                break;
            }

            case InstrumentParams::PlayMode::Slice:
            case InstrumentParams::PlayMode::BeatSlice:
            {
                double step = fine ? 0.001 : (large ? 0.1 : 0.01);
                switch (playbackColumn)
                {
                    case 0:
                        currentParams.startPos = juce::jlimit (0.0, currentParams.endPos,
                            currentParams.startPos + direction * step);
                        break;
                    case 1:
                        currentParams.endPos = juce::jlimit (currentParams.startPos, 1.0,
                            currentParams.endPos + direction * step);
                        break;
                }
                break;
            }

            case InstrumentParams::PlayMode::Granular:
            {
                switch (playbackColumn)
                {
                    case 0:
                    {
                        double step = fine ? 0.001 : (large ? 0.1 : 0.01);
                        currentParams.startPos = juce::jlimit (0.0, currentParams.endPos,
                            currentParams.startPos + direction * step);
                        break;
                    }
                    case 1:
                    {
                        double step = fine ? 0.001 : (large ? 0.1 : 0.01);
                        currentParams.endPos = juce::jlimit (currentParams.startPos, 1.0,
                            currentParams.endPos + direction * step);
                        break;
                    }
                    case 2: // Grain Pos
                    {
                        double step = fine ? 0.001 : (large ? 0.1 : 0.01);
                        currentParams.granularPosition = juce::jlimit (0.0, 1.0,
                            currentParams.granularPosition + direction * step);
                        break;
                    }
                    case 3: // Grain Len
                    {
                        int step = fine ? 1 : (large ? 50 : 10);
                        currentParams.granularLength = juce::jlimit (1, 1000,
                            currentParams.granularLength + direction * step);
                        break;
                    }
                    case 4: // Shape
                    {
                        int v = static_cast<int> (currentParams.granularShape);
                        v = (v - direction + 3) % 3;
                        currentParams.granularShape = static_cast<InstrumentParams::GranShape> (v);
                        break;
                    }
                    case 5: // Grain Loop
                    {
                        int v = static_cast<int> (currentParams.granularLoop);
                        v = (v - direction + 3) % 3;
                        currentParams.granularLoop = static_cast<InstrumentParams::GranLoop> (v);
                        break;
                    }
                }
                break;
            }
        }
    }

    notifyParamsChanged();
}

//==============================================================================
// Proportional value adjustment (for mouse drag and scroll)
//==============================================================================

void SampleEditorComponent::adjustCurrentValueByDelta (double normDelta)
{
    if (displayMode == DisplayMode::InstrumentEdit)
    {
        if (editSubTab == EditSubTab::Parameters)
        {
            switch (parametersColumn)
            {
                case 0: // Volume -100 to 24
                    currentParams.volume = juce::jlimit (-100.0, 24.0,
                        currentParams.volume + normDelta * 124.0);
                    break;
                case 1: // Panning -50 to 50
                    currentParams.panning = juce::jlimit (-50, 50,
                        currentParams.panning + juce::roundToInt (normDelta * 100.0));
                    break;
                case 2: // Tune -24 to 24
                    currentParams.tune = juce::jlimit (-24, 24,
                        currentParams.tune + juce::roundToInt (normDelta * 48.0));
                    break;
                case 3: // Finetune -100 to 100
                    currentParams.finetune = juce::jlimit (-100, 100,
                        currentParams.finetune + juce::roundToInt (normDelta * 200.0));
                    break;
                case 4: // Filter type (list - drag inverted)
                {
                    int v = static_cast<int> (currentParams.filterType)
                            - juce::roundToInt (normDelta * 4.0);
                    currentParams.filterType = static_cast<InstrumentParams::FilterType> (
                        juce::jlimit (0, 3, v));
                    break;
                }
                case 5: // Cutoff 0-100
                    currentParams.cutoff = juce::jlimit (0, 100,
                        currentParams.cutoff + juce::roundToInt (normDelta * 100.0));
                    break;
                case 6: // Resonance 0-85
                    currentParams.resonance = juce::jlimit (0, 85,
                        currentParams.resonance + juce::roundToInt (normDelta * 85.0));
                    break;
                case 7: // Overdrive 0-100
                    currentParams.overdrive = juce::jlimit (0, 100,
                        currentParams.overdrive + juce::roundToInt (normDelta * 100.0));
                    break;
                case 8: // Bit Depth 4-16
                    currentParams.bitDepth = juce::jlimit (4, 16,
                        currentParams.bitDepth + juce::roundToInt (normDelta * 12.0));
                    break;
                case 9: // Reverb Send -100 to 0
                    currentParams.reverbSend = juce::jlimit (-100.0, 0.0,
                        currentParams.reverbSend + normDelta * 100.0);
                    break;
                case 10: // Delay Send -100 to 0
                    currentParams.delaySend = juce::jlimit (-100.0, 0.0,
                        currentParams.delaySend + normDelta * 100.0);
                    break;
            }
        }
        else // Modulation
        {
            auto& mod = currentParams.modulations[static_cast<size_t> (modDestIndex)];

            switch (modColumn)
            {
                case 0: // Destination (6 items, list)
                {
                    int idx = modDestIndex - juce::roundToInt (
                        normDelta * static_cast<double> (InstrumentParams::kNumModDests));
                    modDestIndex = juce::jlimit (0, InstrumentParams::kNumModDests - 1, idx);
                    break;
                }
                case 1: // Type (3 items, list)
                {
                    int v = static_cast<int> (mod.type)
                            - juce::roundToInt (normDelta * 3.0);
                    mod.type = static_cast<InstrumentParams::Modulation::Type> (
                        juce::jlimit (0, 2, v));
                    break;
                }
                case 2: // Shape (LFO list) or Attack (Env bar 0-10)
                {
                    if (mod.type == InstrumentParams::Modulation::Type::LFO)
                    {
                        int v = static_cast<int> (mod.lfoShape)
                                - juce::roundToInt (normDelta * 5.0);
                        mod.lfoShape = static_cast<InstrumentParams::Modulation::LFOShape> (
                            juce::jlimit (0, 4, v));
                    }
                    else if (mod.type == InstrumentParams::Modulation::Type::Envelope)
                        mod.attackS = juce::jlimit (0.0, 10.0, mod.attackS + normDelta * 10.0);
                    break;
                }
                case 3: // Speed (LFO list) or Decay (Env bar 0-10)
                {
                    if (mod.type == InstrumentParams::Modulation::Type::LFO)
                    {
                        int curIdx = 0;
                        for (int i = 0; i < kNumLfoSpeeds; ++i)
                            if (kLfoSpeeds[i] == mod.lfoSpeed) curIdx = i;
                        int newIdx = curIdx - juce::roundToInt (
                            normDelta * static_cast<double> (kNumLfoSpeeds));
                        mod.lfoSpeed = kLfoSpeeds[juce::jlimit (0, kNumLfoSpeeds - 1, newIdx)];
                    }
                    else if (mod.type == InstrumentParams::Modulation::Type::Envelope)
                        mod.decayS = juce::jlimit (0.0, 10.0, mod.decayS + normDelta * 10.0);
                    break;
                }
                case 4: // Amount (LFO 0-100) or Sustain (Env 0-100)
                {
                    if (mod.type == InstrumentParams::Modulation::Type::LFO)
                        mod.amount = juce::jlimit (0, 100,
                            mod.amount + juce::roundToInt (normDelta * 100.0));
                    else if (mod.type == InstrumentParams::Modulation::Type::Envelope)
                        mod.sustain = juce::jlimit (0, 100,
                            mod.sustain + juce::roundToInt (normDelta * 100.0));
                    break;
                }
                case 5: // Release (Env 0-10)
                    if (mod.type == InstrumentParams::Modulation::Type::Envelope)
                        mod.releaseS = juce::jlimit (0.0, 10.0, mod.releaseS + normDelta * 10.0);
                    break;
                case 6: // Amount (Env 0-100)
                    if (mod.type == InstrumentParams::Modulation::Type::Envelope)
                        mod.amount = juce::jlimit (0, 100,
                            mod.amount + juce::roundToInt (normDelta * 100.0));
                    break;
            }
        }
    }
    else // InstrumentType (Playback)
    {
        auto mode = currentParams.playMode;
        int numCols = getColumnCount();

        if (playbackColumn == numCols - 1) // Play Mode (list)
        {
            int v = static_cast<int> (mode) - juce::roundToInt (normDelta * 7.0);
            currentParams.playMode = static_cast<InstrumentParams::PlayMode> (
                juce::jlimit (0, 6, v));
            playbackColumn = getColumnCount() - 1;
            notifyParamsChanged();
            return;
        }

        switch (mode)
        {
            case InstrumentParams::PlayMode::OneShot:
                switch (playbackColumn)
                {
                    case 0:
                        currentParams.startPos = juce::jlimit (0.0, currentParams.endPos,
                            currentParams.startPos + normDelta);
                        break;
                    case 1:
                        currentParams.endPos = juce::jlimit (currentParams.startPos, 1.0,
                            currentParams.endPos + normDelta);
                        break;
                    case 2: // Reverse toggle
                        if (std::abs (normDelta) > 0.15)
                            currentParams.reversed = (normDelta > 0);
                        break;
                }
                break;

            case InstrumentParams::PlayMode::ForwardLoop:
            case InstrumentParams::PlayMode::BackwardLoop:
            case InstrumentParams::PlayMode::PingpongLoop:
                switch (playbackColumn)
                {
                    case 0:
                        currentParams.startPos = juce::jlimit (0.0, currentParams.endPos,
                            currentParams.startPos + normDelta);
                        break;
                    case 1:
                        currentParams.loopStart = juce::jlimit (0.0, currentParams.loopEnd,
                            currentParams.loopStart + normDelta);
                        break;
                    case 2:
                        currentParams.loopEnd = juce::jlimit (currentParams.loopStart, 1.0,
                            currentParams.loopEnd + normDelta);
                        break;
                    case 3:
                        currentParams.endPos = juce::jlimit (currentParams.startPos, 1.0,
                            currentParams.endPos + normDelta);
                        break;
                }
                break;

            case InstrumentParams::PlayMode::Slice:
            case InstrumentParams::PlayMode::BeatSlice:
                switch (playbackColumn)
                {
                    case 0:
                        currentParams.startPos = juce::jlimit (0.0, currentParams.endPos,
                            currentParams.startPos + normDelta);
                        break;
                    case 1:
                        currentParams.endPos = juce::jlimit (currentParams.startPos, 1.0,
                            currentParams.endPos + normDelta);
                        break;
                }
                break;

            case InstrumentParams::PlayMode::Granular:
                switch (playbackColumn)
                {
                    case 0:
                        currentParams.startPos = juce::jlimit (0.0, currentParams.endPos,
                            currentParams.startPos + normDelta);
                        break;
                    case 1:
                        currentParams.endPos = juce::jlimit (currentParams.startPos, 1.0,
                            currentParams.endPos + normDelta);
                        break;
                    case 2:
                        currentParams.granularPosition = juce::jlimit (0.0, 1.0,
                            currentParams.granularPosition + normDelta);
                        break;
                    case 3:
                        currentParams.granularLength = juce::jlimit (1, 1000,
                            currentParams.granularLength + juce::roundToInt (normDelta * 999.0));
                        break;
                    case 4: // Shape (list)
                    {
                        int v = static_cast<int> (currentParams.granularShape)
                                - juce::roundToInt (normDelta * 3.0);
                        currentParams.granularShape = static_cast<InstrumentParams::GranShape> (
                            juce::jlimit (0, 2, v));
                        break;
                    }
                    case 5: // Loop (list)
                    {
                        int v = static_cast<int> (currentParams.granularLoop)
                                - juce::roundToInt (normDelta * 3.0);
                        currentParams.granularLoop = static_cast<InstrumentParams::GranLoop> (
                            juce::jlimit (0, 2, v));
                        break;
                    }
                }
                break;
        }
    }

    notifyParamsChanged();
}

//==============================================================================
// Discrete column detection (for scroll wheel behavior)
//==============================================================================

bool SampleEditorComponent::isCurrentColumnDiscrete() const
{
    if (displayMode == DisplayMode::InstrumentEdit)
    {
        if (editSubTab == EditSubTab::Parameters)
            return parametersColumn == 4; // Filter type list

        // Modulation
        if (modColumn <= 1) return true; // Destination, Type are always lists
        auto& mod = currentParams.modulations[static_cast<size_t> (modDestIndex)];
        if (mod.type == InstrumentParams::Modulation::Type::LFO)
            return modColumn == 2 || modColumn == 3; // Shape, Speed lists
        if (mod.type == InstrumentParams::Modulation::Type::Off)
            return true; // Empty columns
        return false;
    }
    else // InstrumentType
    {
        int numCols = getColumnCount();
        if (playbackColumn == numCols - 1) return true; // Play Mode list
        auto mode = currentParams.playMode;
        if (mode == InstrumentParams::PlayMode::OneShot && playbackColumn == 2)
            return true; // Reverse toggle
        if (mode == InstrumentParams::PlayMode::Granular && playbackColumn >= 4)
            return true; // Shape, Loop
        return false;
    }
}

//==============================================================================
// Keyboard
//==============================================================================

int SampleEditorComponent::keyToNote (const juce::KeyPress& key) const
{
    if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown()
        || key.getModifiers().isAltDown())
        return -1;

    auto c = key.getTextCharacter();
    int baseNote = currentOctave * 12;

    // Lower octave: Z-M
    switch (c)
    {
        case 'z': return baseNote + 0;   // C
        case 's': return baseNote + 1;   // C#
        case 'x': return baseNote + 2;   // D
        case 'd': return baseNote + 3;   // D#
        case 'c': return baseNote + 4;   // E
        case 'v': return baseNote + 5;   // F
        case 'g': return baseNote + 6;   // F#
        case 'b': return baseNote + 7;   // G
        case 'h': return baseNote + 8;   // G#
        case 'n': return baseNote + 9;   // A
        case 'j': return baseNote + 10;  // A#
        case 'm': return baseNote + 11;  // B
        default: break;
    }

    // Upper octave: Q-U
    int upperBase = (currentOctave + 1) * 12;
    switch (c)
    {
        case 'q': return upperBase + 0;   // C
        case '2': return upperBase + 1;   // C#
        case 'w': return upperBase + 2;   // D
        case '3': return upperBase + 3;   // D#
        case 'e': return upperBase + 4;   // E
        case 'r': return upperBase + 5;   // F
        case '5': return upperBase + 6;   // F#
        case 't': return upperBase + 7;   // G
        case '6': return upperBase + 8;   // G#
        case 'y': return upperBase + 9;   // A
        case '7': return upperBase + 10;  // A#
        case 'u': return upperBase + 11;  // B
        default: break;
    }

    return -1;
}

bool SampleEditorComponent::keyPressed (const juce::KeyPress& key)
{
    if (currentInstrument < 0) return false;

    auto keyCode = key.getKeyCode();
    bool shift = key.getModifiers().isShiftDown();
    bool cmd   = key.getModifiers().isCommandDown();

    // Let Cmd shortcuts pass through to ApplicationCommandTarget
    if (cmd) return false;

    // Backtick (`): toggle Parameters/Modulation sub-tab (in InstrumentEdit mode)
    if (key.getTextCharacter() == '`')
    {
        if (displayMode == DisplayMode::InstrumentEdit)
        {
            if (editSubTab == EditSubTab::Parameters)
                setEditSubTab (EditSubTab::Modulation);
            else
                setEditSubTab (EditSubTab::Parameters);
        }
        return true;
    }

    // Space: preview with middle C note
    if (keyCode == juce::KeyPress::spaceKey)
    {
        if (paramsDirty)
        {
            stopTimer();
            paramsDirty = false;
            if (onParamsChanged)
                onParamsChanged (currentInstrument, currentParams);
        }
        if (onPreviewRequested)
            onPreviewRequested (currentInstrument, currentOctave * 12);
        return true;
    }

    // Tab / Shift+Tab: alias for Right/Left
    if (keyCode == juce::KeyPress::tabKey)
    {
        int col = getFocusedColumn();
        int count = getColumnCount();
        if (count > 0)
        {
            if (shift)
                col = juce::jmax (0, col - 1);
            else
                col = juce::jmin (count - 1, col + 1);
            setFocusedColumn (col);
            repaint();
        }
        return true;
    }

    // Up/Down: adjust value in current column
    if (keyCode == juce::KeyPress::upKey)
    {
        adjustCurrentValue (1, shift, false);
        return true;
    }
    if (keyCode == juce::KeyPress::downKey)
    {
        adjustCurrentValue (-1, shift, false);
        return true;
    }

    // Left: move to previous column (stop at boundary)
    if (keyCode == juce::KeyPress::leftKey)
    {
        int col = getFocusedColumn();
        if (col > 0)
        {
            setFocusedColumn (col - 1);
            repaint();
        }
        return true;
    }

    // Right: move to next column (stop at boundary)
    if (keyCode == juce::KeyPress::rightKey)
    {
        int col = getFocusedColumn();
        int count = getColumnCount();
        if (col < count - 1)
        {
            setFocusedColumn (col + 1);
            repaint();
        }
        return true;
    }

    // Note keys: preview the note
    int note = keyToNote (key);
    if (note >= 0 && note < 128)
    {
        if (paramsDirty)
        {
            stopTimer();
            paramsDirty = false;
            if (onParamsChanged)
                onParamsChanged (currentInstrument, currentParams);
        }
        if (onPreviewRequested)
            onPreviewRequested (currentInstrument, note);
        return true;
    }

    // Consume all other non-modifier keys to prevent macOS beep
    if (! key.getModifiers().isAnyModifierKeyDown())
        return true;

    return false;
}

//==============================================================================
// Mouse
//==============================================================================

void SampleEditorComponent::mouseDown (const juce::MouseEvent& event)
{
    if (currentInstrument < 0) return;
    grabKeyboardFocus();

    int contentTop = kHeaderHeight;
    int contentBottom = getHeight() - kBottomBarHeight;

    // Determine content offset for sub-tab bar
    int contentLeftOffset = 0;
    if (displayMode == DisplayMode::InstrumentEdit)
        contentLeftOffset = kSubTabWidth;

    // Click on sub-tab sidebar
    if (displayMode == DisplayMode::InstrumentEdit && event.x < kSubTabWidth
        && event.y >= contentTop && event.y < contentBottom)
    {
        int relY = event.y - contentTop;
        int itemH = 30;
        int itemIdx = relY / itemH;
        if (itemIdx == 0)
            setEditSubTab (EditSubTab::Parameters);
        else if (itemIdx == 1)
            setEditSubTab (EditSubTab::Modulation);
        return;
    }

    // Click on bottom bar column
    if (event.y >= contentBottom)
    {
        int numCols = getColumnCount();
        if (numCols > 0)
        {
            int colW = getWidth() / numCols;
            int col = event.x / juce::jmax (1, colW);
            col = juce::jlimit (0, numCols - 1, col);
            setFocusedColumn (col);
            repaint();
        }
        return;
    }

    // Click in content area: determine column
    if (event.y >= contentTop && event.y < contentBottom)
    {
        int contentWidth = getWidth() - contentLeftOffset;
        int contentX = event.x - contentLeftOffset;
        if (contentX < 0) return;

        int numCols = getColumnCount();
        if (numCols > 0)
        {
            int colW = contentWidth / numCols;
            int col = contentX / juce::jmax (1, colW);
            col = juce::jlimit (0, numCols - 1, col);

            // For playback page, the content area is waveform + mode list overlay.
            if (displayMode == DisplayMode::InstrumentType)
            {
                // Check if click is in the mode list area
                int listW = 140;
                int listX = getWidth() - 4 - listW - 2;
                if (event.x >= listX)
                {
                    setFocusedColumn (numCols - 1); // Focus play mode column
                    // Calculate which mode item was clicked
                    int listY = contentTop + 4 + 2;
                    int itemIdx = (event.y - listY) / kListItemHeight;
                    if (itemIdx >= 0 && itemIdx < 7)
                    {
                        currentParams.playMode = static_cast<InstrumentParams::PlayMode> (itemIdx);
                        if (playbackColumn >= getColumnCount())
                            playbackColumn = getColumnCount() - 1;
                        notifyParamsChanged();
                    }
                    repaint();
                }
                return;
            }

            setFocusedColumn (col);

            // For list columns in modulation page, handle item clicks
            if (displayMode == DisplayMode::InstrumentEdit && editSubTab == EditSubTab::Modulation)
            {
                int contentH = contentBottom - contentTop;
                int relY = event.y - contentTop;
                bool handledAsList = false;

                if (col == 0) // Destination list
                {
                    int itemIdx = relY / juce::jmax (1, kListItemHeight);
                    if (itemIdx >= 0 && itemIdx < InstrumentParams::kNumModDests)
                        modDestIndex = itemIdx;
                    handledAsList = true;
                }
                else if (col == 1) // Type list
                {
                    int itemIdx = relY / juce::jmax (1, kListItemHeight);
                    if (itemIdx >= 0 && itemIdx < 3)
                    {
                        auto& mod = currentParams.modulations[static_cast<size_t> (modDestIndex)];
                        mod.type = static_cast<InstrumentParams::Modulation::Type> (itemIdx);
                    }
                    handledAsList = true;
                }
                else if (col == 2 && currentParams.modulations[static_cast<size_t> (modDestIndex)].type
                                      == InstrumentParams::Modulation::Type::LFO)
                {
                    // Shape list
                    int itemIdx = relY / juce::jmax (1, kListItemHeight);
                    if (itemIdx >= 0 && itemIdx < 5)
                    {
                        auto& mod = currentParams.modulations[static_cast<size_t> (modDestIndex)];
                        mod.lfoShape = static_cast<InstrumentParams::Modulation::LFOShape> (itemIdx);
                    }
                    handledAsList = true;
                }
                else if (col == 3 && currentParams.modulations[static_cast<size_t> (modDestIndex)].type
                                      == InstrumentParams::Modulation::Type::LFO)
                {
                    // Speed list - calculate which preset was clicked
                    int numVisible = contentH / kListItemHeight;
                    auto& mod = currentParams.modulations[static_cast<size_t> (modDestIndex)];
                    int curSpeedIdx = 0;
                    for (int i = 0; i < kNumLfoSpeeds; ++i)
                        if (kLfoSpeeds[i] == mod.lfoSpeed)
                            curSpeedIdx = i;
                    int scrollOff = juce::jlimit (0, juce::jmax (0, kNumLfoSpeeds - numVisible),
                                                   curSpeedIdx - numVisible / 2);
                    int clickedItem = scrollOff + relY / juce::jmax (1, kListItemHeight);
                    if (clickedItem >= 0 && clickedItem < kNumLfoSpeeds)
                        mod.lfoSpeed = kLfoSpeeds[clickedItem];
                    handledAsList = true;
                }

                if (handledAsList)
                {
                    notifyParamsChanged();
                    return;
                }
                // Bar columns (attack, decay, sustain, release, amount) fall through to drag
            }

            // For parameters page, handle filter type list clicks (col 4)
            if (displayMode == DisplayMode::InstrumentEdit
                && editSubTab == EditSubTab::Parameters && col == 4)
            {
                int relY = event.y - contentTop;
                int itemIdx = relY / juce::jmax (1, kListItemHeight);
                if (itemIdx >= 0 && itemIdx < 4)
                    currentParams.filterType = static_cast<InstrumentParams::FilterType> (itemIdx);
                notifyParamsChanged();
                return;
            }

            // Start drag for bar columns
            isDragging = true;
            dragStartY = event.position.y;
            dragStartParams = currentParams;
            dragStartModDestIndex = modDestIndex;
            repaint();
        }
    }
}

void SampleEditorComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (! isDragging) return;

    float deltaY = dragStartY - event.position.y;
    currentParams = dragStartParams;
    modDestIndex = dragStartModDestIndex;

    int contentH = getHeight() - kHeaderHeight - kBottomBarHeight;
    double normDelta = static_cast<double> (deltaY)
                       / static_cast<double> (juce::jmax (1, contentH));

    if (event.mods.isShiftDown())
        normDelta *= 0.1;

    adjustCurrentValueByDelta (normDelta);
}

void SampleEditorComponent::mouseUp (const juce::MouseEvent&)
{
    if (isDragging)
    {
        isDragging = false;
        if (paramsDirty)
        {
            stopTimer();
            paramsDirty = false;
            if (onParamsChanged)
                onParamsChanged (currentInstrument, currentParams);
        }
    }
}

void SampleEditorComponent::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (currentInstrument < 0) return;

    float delta = wheel.deltaY;
    if (std::abs (delta) < 0.001f) return;

    // For discrete/list columns: step one item per scroll event
    if (isCurrentColumnDiscrete())
    {
        adjustCurrentValue (delta > 0 ? 1 : -1, false, false);
        return;
    }

    // For continuous columns: proportional adjustment
    double normDelta = static_cast<double> (delta) * 0.12;

    if (event.mods.isShiftDown())
        normDelta *= 0.1;

    adjustCurrentValueByDelta (normDelta);
}
