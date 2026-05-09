#include "SampleEditorComponent.h"
#include "NoteUtils.h"
#include "FormatUtils.h"
#include "SamplePlaybackLayout.h"
#include "TransientDetector.h"
#include <algorithm>
#include <cmath>

//==============================================================================
// LFO speed presets (descending, in steps)
//==============================================================================

const int SampleEditorComponent::kLfoSpeeds[] = {
    128, 96, 64, 48, 32, 24, 16, 12, 8, 6, 4, 3, 2, 1
};

using namespace FormatUtils;

namespace
{
constexpr int kPluginParameterRowHeight = 28;
constexpr int kPluginParameterScrollbarWidth = 10;
constexpr int kPluginParameterScrollbarGap = 6;
constexpr double kPluginParameterWheelRowsPerUnit = 6.0;

juce::String getPluginSourceDisplayName (const PluginModulatorSource& source, int index)
{
    if (source.name.isNotEmpty())
        return source.name;

    return (source.type == PluginModulatorSource::Type::LFO ? "LFO " : "Env ") + juce::String (index + 1);
}

juce::String getPluginLfoShapeName (PluginModulatorSource::LfoShape shape)
{
    switch (shape)
    {
        case PluginModulatorSource::LfoShape::Sine:     return "Sine";
        case PluginModulatorSource::LfoShape::Triangle: return "Tri";
        case PluginModulatorSource::LfoShape::Saw:      return "Saw";
        case PluginModulatorSource::LfoShape::Square:   return "Square";
        case PluginModulatorSource::LfoShape::Random:   return "Random";
    }
    return "Tri";
}

juce::String getPluginEnvTriggerName (PluginModulatorSource::EnvelopeTriggerMode mode)
{
    return mode == PluginModulatorSource::EnvelopeTriggerMode::StepFxOnly ? "Step FX" : "Note";
}

juce::String formatPluginSeconds (double seconds)
{
    if (seconds < 1.0)
        return juce::String (juce::roundToInt (seconds * 1000.0)) + "ms";
    return juce::String (seconds, 2) + "s";
}

double clampLofiSampleRateHz (double hz)
{
    if (hz <= 0.0)
        return 0.0;

    return juce::jlimit (InstrumentParams::kMinLofiSampleRateHz,
                         InstrumentParams::kMaxLofiSampleRateHz,
                         hz);
}

float lofiSampleRateToNorm (double hz)
{
    if (hz <= 0.0)
        return 1.0f;

    const double clampedHz = clampLofiSampleRateHz (hz);
    return static_cast<float> ((clampedHz - InstrumentParams::kMinLofiSampleRateHz)
        / (InstrumentParams::kMaxLofiSampleRateHz - InstrumentParams::kMinLofiSampleRateHz));
}

double normToLofiSampleRateHz (double norm)
{
    norm = juce::jlimit (0.0, 1.0, norm);
    if (norm >= 0.995)
        return 0.0;

    return InstrumentParams::kMinLofiSampleRateHz
         + norm * (InstrumentParams::kMaxLofiSampleRateHz - InstrumentParams::kMinLofiSampleRateHz);
}

juce::String formatLofiSampleRate (double hz)
{
    const double clampedHz = clampLofiSampleRateHz (hz);
    if (clampedHz <= 0.0)
        return "Off";

    const double khz = clampedHz / 1000.0;
    return juce::String (khz, khz >= 10.0 ? 1 : 2) + "k";
}

int getMidiOutLaneForColumn (int column)
{
    return juce::jlimit (0, InstrumentParams::kNumMidiOutLanes - 1, column / 2);
}

bool isMidiOutTypeColumn (int column)
{
    return (column % 2) == 0;
}

juce::String getMidiOutLaneName (int lane)
{
    static constexpr char laneNames[] = "ABCDEF";
    return juce::String::charToString (laneNames[static_cast<size_t> (
        juce::jlimit (0, InstrumentParams::kNumMidiOutLanes - 1, lane))]);
}

juce::String getMidiOutTypeName (InstrumentParams::MidiOutMessageType type)
{
    switch (type)
    {
        case InstrumentParams::MidiOutMessageType::ControlChange:    return "CC";
        case InstrumentParams::MidiOutMessageType::ProgramChange:    return "PC";
        case InstrumentParams::MidiOutMessageType::ChannelPressure:  return "ChanPr";
        case InstrumentParams::MidiOutMessageType::PolyPressure:     return "PolyPr";
    }
    return "CC";
}

bool midiOutAssignmentUsesNumber (InstrumentParams::MidiOutMessageType type)
{
    return type == InstrumentParams::MidiOutMessageType::ControlChange
        || type == InstrumentParams::MidiOutMessageType::PolyPressure;
}

juce::Colour getPluginSourceColour (int index)
{
    static constexpr juce::uint32 colours[] = {
        0xff7dd3fc, 0xfff0abfc, 0xff86efac, 0xfffacc15,
        0xfffb7185, 0xffc4b5fd, 0xff5eead4, 0xfffdba74
    };
    return juce::Colour (colours[static_cast<size_t> (juce::jlimit (0, 7, index % 8))]);
}

void fillRoundedRow (juce::Graphics& g, juce::Rectangle<int> row, juce::Colour colour, bool selected)
{
    g.setColour (selected ? colour.withAlpha (0.16f) : juce::Colour (0xff171a1f));
    g.fillRoundedRectangle (row.toFloat(), 6.0f);
    g.setColour (selected ? colour.withAlpha (0.75f) : juce::Colour (0xff30343b));
    g.drawRoundedRectangle (row.toFloat().reduced (0.5f), 6.0f, 1.0f);
}
}

//==============================================================================
// Construction / Destruction
//==============================================================================

SampleEditorComponent::SampleEditorComponent (TrackerLookAndFeel& lnf)
    : lookAndFeel (lnf), waveformView (lnf)
{
    setWantsKeyboardFocus (true);
    addChildComponent (waveformView);

    pluginParameterSearchBox.setMultiLine (false);
    pluginParameterSearchBox.setReturnKeyStartsNewLine (false);
    pluginParameterSearchBox.setScrollbarsShown (false);
    pluginParameterSearchBox.setSelectAllWhenFocused (true);
    pluginParameterSearchBox.setInputRestrictions (64);
    pluginParameterSearchBox.setFont (lookAndFeel.getMonoFont (10.0f));
    pluginParameterSearchBox.setTextToShowWhenEmpty ("search", lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.35f));
    pluginParameterSearchBox.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0b0d10));
    pluginParameterSearchBox.setColour (juce::TextEditor::textColourId, lookAndFeel.findColour (TrackerLookAndFeel::textColourId));
    pluginParameterSearchBox.setColour (juce::TextEditor::outlineColourId,
                                        lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    pluginParameterSearchBox.setColour (juce::TextEditor::focusedOutlineColourId,
                                        lookAndFeel.findColour (TrackerLookAndFeel::fxColourId));
    pluginParameterSearchBox.setColour (juce::TextEditor::shadowColourId, juce::Colours::transparentBlack);
    pluginParameterSearchBox.onTextChange = [this]
    {
        pluginParameterScroll = 0;
        pluginParameterWheelAccumulator = 0.0;
        updatePluginParameterScrollbar();
        if (focusedPluginHit.kind == PluginHitKind::ParamAssign)
            focusFirstPluginParameter();
        repaint (getPluginParameterListBounds().expanded (2));
        repaint (getPluginParameterSearchBoxBounds().expanded (2));
    };
    pluginParameterSearchBox.onReturnKey = [this]
    {
        grabKeyboardFocus();
        focusFirstPluginParameter();
    };
    pluginParameterSearchBox.onEscapeKey = [this]
    {
        grabKeyboardFocus();
        focusedPluginHit = { PluginHitKind::ParamSearch, -1, -1 };
        repaint (getPluginKeyboardFocusBounds().expanded (4));
    };
    addChildComponent (pluginParameterSearchBox);

    pluginParameterScrollbar.setWantsKeyboardFocus (false);
    pluginParameterScrollbar.setAutoHide (false);
    pluginParameterScrollbar.setSingleStepSize (1.0);
    pluginParameterScrollbar.setColour (juce::ScrollBar::backgroundColourId,
                                        juce::Colour (0xff0b0d10));
    pluginParameterScrollbar.setColour (juce::ScrollBar::trackColourId,
                                        lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId)
                                            .withAlpha (0.45f));
    pluginParameterScrollbar.setColour (juce::ScrollBar::thumbColourId,
                                        lookAndFeel.findColour (TrackerLookAndFeel::fxColourId)
                                            .withAlpha (0.85f));
    pluginParameterScrollbar.addListener (this);
    addChildComponent (pluginParameterScrollbar);
}

SampleEditorComponent::~SampleEditorComponent()
{
    pluginParameterScrollbar.removeListener (this);
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
    flushPendingParams();

    currentInstrument = instrumentIndex;
    showingPlugin = false;
    pluginModulation = PluginInstrumentModulation();
    pluginParameterInfos.clear();
    pluginDragHit = {};
    focusedPluginHit = { PluginHitKind::OpenEditor, -1, -1 };
    pluginParameterScroll = 0;
    pluginParameterWheelAccumulator = 0.0;
    pluginParameterSearchBox.setText ({}, false);
    pluginParameterSearchBox.setVisible (false);
    pluginParameterScrollbar.setVisible (false);
    currentFile = sampleFile;
    currentParams = params;

    // Reset zoom when switching instruments, then clamp persisted playback state.
    resetWaveformState();
    constrainPlaybackMarkersToRegion();
    lastCommittedParams = currentParams;
    paramsDirty = false;

    waveformView.setSample (sampleFile);
    syncWaveformView();
    repaint();
}

void SampleEditorComponent::clearInstrument()
{
    flushPendingParams();

    currentInstrument = -1;
    showingPlugin = false;
    pluginModulation = PluginInstrumentModulation();
    pluginParameterInfos.clear();
    selectedPluginSourceIndex = 0;
    selectedPluginRouteIndex = -1;
    focusedPluginHit = { PluginHitKind::OpenEditor, -1, -1 };
    pluginParameterScroll = 0;
    pluginParameterWheelAccumulator = 0.0;
    pluginDragHit = {};
    pluginParameterSearchBox.setText ({}, false);
    pluginParameterSearchBox.setVisible (false);
    pluginParameterScrollbar.setVisible (false);
    currentFile = juce::File();
    currentParams = InstrumentParams();
    lastCommittedParams = InstrumentParams();
    paramsDirty = false;
    isDragging = false;

    resetWaveformState();

    waveformView.clearSample();
    syncWaveformView();
    repaint();
}

void SampleEditorComponent::setPluginInstrument (int instrumentIndex,
                                                const juce::String& pluginName,
                                                int ownerTrack,
                                                const PluginInstrumentModulation& modulation,
                                                std::vector<PluginInstrumentParameterInfo> parameterInfos)
{
    flushPendingParams();

    currentInstrument = instrumentIndex;
    showingPlugin = true;
    pluginInstrumentName = pluginName;
    pluginOwnerTrack = ownerTrack;
    pluginModulation = modulation;
    pluginModulation.ensureDefaultSources();
    pluginParameterInfos = std::move (parameterInfos);
    selectedPluginSourceIndex = juce::jlimit (0, juce::jmax (0, static_cast<int> (pluginModulation.sources.size()) - 1),
                                              selectedPluginSourceIndex);
    selectedPluginRouteIndex = pluginModulation.routes.empty()
                                   ? -1
                                   : juce::jlimit (0, static_cast<int> (pluginModulation.routes.size()) - 1,
                                                   selectedPluginRouteIndex);
    pluginParameterScroll = juce::jlimit (0, juce::jmax (0, static_cast<int> (pluginParameterInfos.size()) - 1),
                                          pluginParameterScroll);
    pluginParameterWheelAccumulator = 0.0;
    ensureFocusedPluginHitValid();

    currentFile = juce::File();
    currentParams = InstrumentParams();
    lastCommittedParams = InstrumentParams();
    paramsDirty = false;
    isDragging = false;
    resetWaveformState();
    hoveredMarker = MarkerType::None;
    setMouseCursor (juce::MouseCursor::NormalCursor);

    waveformView.clearSample();
    syncWaveformView();
    updatePluginParameterSearchBoxBounds();
    updatePluginParameterScrollbar();
    repaint();
}

//==============================================================================
// Debounced apply
//==============================================================================

void SampleEditorComponent::timerCallback()
{
    if (previewActive)
    {
        if (onGetPreviewPosition)
            currentPlaybackPos = onGetPreviewPosition();
        else
            currentPlaybackPos = -1.0f;

        waveformView.setPlaybackPosition (currentPlaybackPos);

        // Detect natural end of playback (voice went idle)
        if (currentPlaybackPos < 0.0f)
        {
            previewActive = false;
            previewKeyCode = -1;
            if (onPreviewStopped)
                onPreviewStopped();
        }
        repaint();
    }

    if (paramsDirty)
    {
        paramsDirty = false;
        if (onParamsChanged)
            onParamsChanged (currentInstrument, currentParams);
        lastCommittedParams = currentParams;
    }

    if (! previewActive && ! paramsDirty)
        stopTimer();
}

void SampleEditorComponent::scheduleApply()
{
    paramsDirty = true;
    startTimer (30);
}

void SampleEditorComponent::flushPendingParams()
{
    if (paramsDirty)
    {
        stopTimer();
        paramsDirty = false;
        if (onParamsChanged)
            onParamsChanged (currentInstrument, currentParams);
    }
}

void SampleEditorComponent::resetWaveformState()
{
    viewStart = 0.0;
    viewEnd = 1.0;
    selectedSliceIndex = -1;
    isWaveformDragging = false;
    draggingMarker = MarkerType::None;
    isPanning = false;
}

void SampleEditorComponent::syncWaveformView()
{
    waveformView.setParams (currentParams);
    waveformView.setViewRange (viewStart, viewEnd);
    waveformView.setSelectedSliceIndex (selectedSliceIndex);
    waveformView.setPlaybackPosition (currentPlaybackPos);

    // Map marker hover/drag state to WaveformView's MarkerType
    auto mapMarker = [] (MarkerType m) -> WaveformView::MarkerType
    {
        switch (m)
        {
            case MarkerType::None:      return WaveformView::MarkerType::None;
            case MarkerType::Start:     return WaveformView::MarkerType::Start;
            case MarkerType::End:       return WaveformView::MarkerType::End;
            case MarkerType::LoopStart: return WaveformView::MarkerType::LoopStart;
            case MarkerType::LoopEnd:   return WaveformView::MarkerType::LoopEnd;
            case MarkerType::GranPos:   return WaveformView::MarkerType::GranPos;
            case MarkerType::Slice:     return WaveformView::MarkerType::Slice;
        }
        return WaveformView::MarkerType::None;
    };
    waveformView.setHoveredMarker (mapMarker (hoveredMarker));
    waveformView.setDraggingMarker (mapMarker (draggingMarker));
    waveformView.setDraggingSliceIndex (draggingSliceIndex);
}

void SampleEditorComponent::setFilterTypeWithDefaultCutoff (InstrumentParams::FilterType newType)
{
    auto oldType = currentParams.filterType;
    currentParams.filterType = newType;
    if (currentParams.filterType != oldType)
    {
        switch (currentParams.filterType)
        {
            case InstrumentParams::FilterType::Disabled:  break;
            case InstrumentParams::FilterType::HighPass:  currentParams.cutoff = 5;  break;
            case InstrumentParams::FilterType::BandPass:  currentParams.cutoff = 50; break;
            case InstrumentParams::FilterType::LowPass:   currentParams.cutoff = 100; break;
        }
    }
}

bool SampleEditorComponent::isRealtimeOnlyChange (const InstrumentParams& oldP, const InstrumentParams& newP) const
{
    // Structural params that require sample reload via applyParams()
    if (oldP.tune != newP.tune) return false;
    if (oldP.finetune != newP.finetune) return false;
    if (! InstrumentParams::approximatelyEqual (oldP.startPos, newP.startPos)) return false;
    if (! InstrumentParams::approximatelyEqual (oldP.endPos, newP.endPos)) return false;
    if (oldP.reversed != newP.reversed) return false;
    if (oldP.playMode != newP.playMode) return false;
    if (! InstrumentParams::approximatelyEqual (oldP.loopStart, newP.loopStart)) return false;
    if (! InstrumentParams::approximatelyEqual (oldP.loopEnd, newP.loopEnd)) return false;
    if (! InstrumentParams::approximatelyEqual (oldP.granularPosition, newP.granularPosition)) return false;
    if (oldP.granularLength != newP.granularLength) return false;
    if (oldP.granularLengthMode != newP.granularLengthMode) return false;
    if (! InstrumentParams::approximatelyEqual (oldP.granularLengthSteps, newP.granularLengthSteps)) return false;
    if (oldP.granularShape != newP.granularShape) return false;
    if (oldP.granularLoop != newP.granularLoop) return false;
    if (oldP.slicePoints != newP.slicePoints) return false;
    // Everything else (volume, pan, filter, overdrive, bitDepth, sample-rate reduction, sends, modulations)
    // is handled by InstrumentEffectsPlugin reading from the params map each block
    return true;
}

void SampleEditorComponent::constrainPlaybackMarkersToRegion()
{
    constexpr double kDuplicateEps = 1.0e-6;

    currentParams.startPos = juce::jlimit (0.0, 1.0, currentParams.startPos);
    currentParams.endPos = juce::jlimit (currentParams.startPos, 1.0, currentParams.endPos);
    currentParams.loopStart = juce::jlimit (currentParams.startPos, currentParams.endPos,
                                             currentParams.loopStart);
    currentParams.loopEnd = juce::jlimit (currentParams.loopStart, currentParams.endPos,
                                           currentParams.loopEnd);
    currentParams.granularPosition = juce::jlimit (currentParams.startPos, currentParams.endPos,
                                                   currentParams.granularPosition);

    for (auto& slicePos : currentParams.slicePoints)
        slicePos = juce::jlimit (currentParams.startPos, currentParams.endPos, slicePos);

    std::sort (currentParams.slicePoints.begin(), currentParams.slicePoints.end());
    currentParams.slicePoints.erase (
        std::unique (currentParams.slicePoints.begin(),
                     currentParams.slicePoints.end(),
                     [] (double a, double b) { return std::abs (a - b) <= kDuplicateEps; }),
        currentParams.slicePoints.end());

    if (currentParams.playMode == InstrumentParams::PlayMode::BeatSlice)
    {
        const int regionCount = SamplePlaybackLayout::getBeatSliceRegionCount (currentParams);
        currentParams.selectedSlice = juce::jlimit (0, juce::jmax (0, regionCount - 1),
                                                    currentParams.selectedSlice);
        selectedSliceIndex = currentParams.selectedSlice;
    }
    else if (currentParams.playMode == InstrumentParams::PlayMode::Slice)
    {
        const int regionCount = SamplePlaybackLayout::getSliceRegionCount (currentParams);
        currentParams.selectedSlice = juce::jlimit (0, juce::jmax (0, regionCount - 1),
                                                    currentParams.selectedSlice);
        selectedSliceIndex = currentParams.selectedSlice;
    }
    else
    {
        currentParams.selectedSlice = 0;
        selectedSliceIndex = -1;
    }
}

void SampleEditorComponent::notifyParamsChanged()
{
    if (showingPlugin)
    {
        paramsDirty = false;
        return;
    }

    constrainPlaybackMarkersToRegion();

    if (currentInstrument >= 0 && isRealtimeOnlyChange (lastCommittedParams, currentParams))
    {
        // DSP-only change: push directly to engine, no debounce, no sample reload
        if (onRealtimeParamsChanged)
            onRealtimeParamsChanged (currentInstrument, currentParams);
    }
    else
    {
        // Structural change: use debounced full apply path
        scheduleApply();
    }
    syncWaveformView();
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

juce::String SampleEditorComponent::getGranLengthModeName (InstrumentParams::GranLengthMode mode) const
{
    switch (mode)
    {
        case InstrumentParams::GranLengthMode::MS:     return "MS";
        case InstrumentParams::GranLengthMode::Steps:  return "Steps";
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

juce::String SampleEditorComponent::formatGranularLengthSteps (double steps) const
{
    steps = SamplePlaybackLayout::snapGranularLengthSteps (steps);
    if (InstrumentParams::approximatelyEqual (steps, 1.0))
        return "1 step";

    if (InstrumentParams::approximatelyEqual (std::fmod (steps, 1.0), 0.0))
        return juce::String (static_cast<int> (std::round (steps))) + " steps";

    return juce::String (steps, 1) + " steps";
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
        if (editSubTab == EditSubTab::MidiOut)
            return midiOutColumn;
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
        else if (editSubTab == EditSubTab::MidiOut)
            midiOutColumn = col;
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
            return 12; // Vol, Pan, Tune, Fine, Filter, Cutoff, Rez, OD, BitDepth, Rate, RevSend, DlySend
        if (editSubTab == EditSubTab::MidiOut)
            return InstrumentParams::kNumMidiOutLanes * 2; // Type + target number per MIDI Out lane
        else
            return 8; // Modulation page
    }
    else // InstrumentType
    {
        auto mode = currentParams.playMode;
        switch (mode)
        {
            case InstrumentParams::PlayMode::OneShot:                       return 3;
            case InstrumentParams::PlayMode::ForwardLoop:
            case InstrumentParams::PlayMode::BackwardLoop:
            case InstrumentParams::PlayMode::PingpongLoop:                  return 5;
            case InstrumentParams::PlayMode::Slice:                          return 7; // Start, End, Slices, Sel, AutoSlice, EqChop, PlayMode
            case InstrumentParams::PlayMode::BeatSlice:                     return 7; // Start, End, NumSlices, Sel, AutoSlice, EqChop, PlayMode
            case InstrumentParams::PlayMode::Granular:                      return 8;
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
                                    "Cutoff", "Resonance", "Overdrive", "Bit Depth",
                                    "Rate kHz", "Reverb Send", "Delay Send" };
            if (col >= 0 && col < 12) return names[col];
        }
        else if (editSubTab == EditSubTab::MidiOut)
        {
            const int lane = getMidiOutLaneForColumn (col);
            return getMidiOutLaneName (lane) + (isMidiOutTypeColumn (col) ? " Type" : " Target");
        }
        else // Modulation
        {
            if (col == 0) return "Destination";
            if (col == 1) return "Type";
            if (col == 2) return "Mode";

            auto& mod = currentParams.modulations[static_cast<size_t> (modDestIndex)];
            if (mod.type == InstrumentParams::Modulation::Type::LFO)
            {
                if (col == 3) return "Shape";
                if (col == 4)
                    return mod.lfoSpeedMode == InstrumentParams::Modulation::LFOSpeedMode::MS
                               ? "Speed (ms)" : "Speed";
                if (col == 5) return "Amount";
                if (col == 6) return "Speed Mode";
            }
            else if (mod.type == InstrumentParams::Modulation::Type::Envelope)
            {
                const char* names[] = { "", "", "", "Attack", "Decay", "Sustain", "Release", "Amount" };
                if (col >= 3 && col < 8) return names[col];
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
                const char* n[] = { "Start", "End" };
                if (col < 2) return n[col];
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
            {
                const char* n[] = { "Start", "End", "Slices", "Selected", "AutoSlice", "EqChop" };
                if (col < 6) return n[col];
                break;
            }
            case InstrumentParams::PlayMode::BeatSlice:
            {
                const char* n[] = { "Start", "End", "Num Slices", "Selected", "AutoSlice", "EqChop" };
                if (col < 6) return n[col];
                break;
            }
            case InstrumentParams::PlayMode::Granular:
            {
                const char* n[] = { "Start", "End", "Grain Pos", "Grain Len", "Len Mode", "Shape", "Loop" };
                if (col < 7) return n[col];
                break;
            }
        }
    }
    return {};
}

juce::String SampleEditorComponent::getColumnValue (int col) const
{
    double totalLen = waveformView.getTotalLength();
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
                case 9: return formatLofiSampleRate (currentParams.lofiSampleRateHz);
                case 10: return formatDb (currentParams.reverbSend);
                case 11: return formatDb (currentParams.delaySend);
            }
        }
        else if (editSubTab == EditSubTab::MidiOut)
        {
            const int lane = getMidiOutLaneForColumn (col);
            const auto& assignment = currentParams.midiOutAssignments[static_cast<size_t> (lane)];
            if (isMidiOutTypeColumn (col))
                return getMidiOutTypeName (assignment.type);

            if (assignment.type == InstrumentParams::MidiOutMessageType::ControlChange)
                return "CC " + juce::String (assignment.number);
            if (assignment.type == InstrumentParams::MidiOutMessageType::PolyPressure)
                return "Note " + juce::String (assignment.number);
            return "n/a";
        }
        else // Modulation
        {
            auto& mod = currentParams.modulations[static_cast<size_t> (modDestIndex)];
            if (col == 0) return getModDestFullName (modDestIndex);
            if (col == 1) return getModTypeName (mod.type);
            if (col == 2) return mod.modMode == InstrumentParams::Modulation::ModMode::Global
                                     ? "Global" : "Per-Note";

            if (mod.type == InstrumentParams::Modulation::Type::LFO)
            {
                switch (col)
                {
                    case 3: return getLfoShapeName (mod.lfoShape);
                    case 4:
                        if (mod.lfoSpeedMode == InstrumentParams::Modulation::LFOSpeedMode::MS)
                            return juce::String (mod.lfoSpeedMs) + " ms";
                        return formatLfoSpeed (mod.lfoSpeed);
                    case 5: return juce::String (mod.amount);
                    case 6:
                        return mod.lfoSpeedMode == InstrumentParams::Modulation::LFOSpeedMode::MS
                                   ? "MS" : "Steps";
                }
            }
            else if (mod.type == InstrumentParams::Modulation::Type::Envelope)
            {
                switch (col)
                {
                    case 3: return formatSeconds (mod.attackS);
                    case 4: return formatSeconds (mod.decayS);
                    case 5: return juce::String (mod.sustain);
                    case 6: return formatSeconds (mod.releaseS);
                    case 7: return juce::String (mod.amount);
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
                    case 2:
                    {
                        if (mode == InstrumentParams::PlayMode::BeatSlice)
                            return juce::String (SamplePlaybackLayout::getBeatSliceRegionCount (currentParams));
                        return juce::String (SamplePlaybackLayout::getSliceRegionCount (currentParams));
                    }
                    case 3:
                        {
                            int numSlices = (mode == InstrumentParams::PlayMode::BeatSlice)
                                ? SamplePlaybackLayout::getBeatSliceRegionCount (currentParams)
                                : SamplePlaybackLayout::getSliceRegionCount (currentParams);
                            int idx = juce::jlimit (0, juce::jmax (0, numSlices - 1),
                                                    currentParams.selectedSlice);
                            return juce::String (idx + 1) + "/" + juce::String (numSlices);
                        }
                    case 4: return juce::String (static_cast<int> (autoSliceSensitivity * 100.0)) + "%";
                    case 5:
                    {
                        int regions = SamplePlaybackLayout::getSliceRegionCount (currentParams);
                        return regions > 1 ? juce::String (regions) : "8";
                    }
                }
                break;

            case InstrumentParams::PlayMode::Granular:
                switch (col)
                {
                    case 0: return formatPosSec (currentParams.startPos, totalLen);
                    case 1: return formatPosSec (currentParams.endPos, totalLen);
                    case 2: return formatPosSec (currentParams.granularPosition, totalLen);
                    case 3:
                        if (currentParams.granularLengthMode == InstrumentParams::GranLengthMode::Steps)
                            return formatGranularLengthSteps (currentParams.granularLengthSteps);
                        return juce::String (currentParams.granularLength) + "ms";
                    case 4: return getGranLengthModeName (currentParams.granularLengthMode);
                    case 5: return getGranShapeName (currentParams.granularShape);
                    case 6: return getGranLoopName (currentParams.granularLoop);
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
        waveformView.setVisible (false);
        pluginParameterSearchBox.setVisible (false);
        pluginParameterScrollbar.setVisible (false);
        g.setFont (lookAndFeel.getMonoFont (12.0f));
        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.25f));
        g.drawText ("No instrument selected", getLocalBounds(), juce::Justification::centred);
        return;
    }

    // Outer border
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawRect (getLocalBounds(), 1);

    // Plugin instrument: show simplified info page instead of sample editor
    if (showingPlugin)
    {
        waveformView.setVisible (false);
        updatePluginParameterSearchBoxBounds();
        pluginParameterSearchBox.setVisible (true);
        updatePluginParameterScrollbar();
        drawHeader (g, { 0, 0, getWidth(), kHeaderHeight });
        auto contentArea = juce::Rectangle<int> (0, kHeaderHeight, getWidth(),
                                                  getHeight() - kHeaderHeight);
        drawPluginInstrumentPage (g, contentArea);
        return;
    }

    pluginParameterSearchBox.setVisible (false);
    pluginParameterScrollbar.setVisible (false);

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
        waveformView.setVisible (false);

        // Sub-tab sidebar on the left
        auto subTabArea = contentArea.removeFromLeft (kSubTabWidth);
        drawSubTabBar (g, subTabArea);

        if (editSubTab == EditSubTab::Parameters)
            drawParametersPage (g, contentArea);
        else if (editSubTab == EditSubTab::MidiOut)
            drawMidiOutPage (g, contentArea);
        else
            drawModulationPage (g, contentArea);
    }
    else // InstrumentType
    {
        drawPlaybackPage (g, contentArea);
    }
}

void SampleEditorComponent::paintOverChildren (juce::Graphics& g)
{
    // Draw the play mode list overlay on top of the waveformView child
    if (currentInstrument < 0 || showingPlugin || displayMode != DisplayMode::InstrumentType)
        return;

    auto waveArea = getWaveformArea();
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

void SampleEditorComponent::resized()
{
    updatePluginParameterSearchBoxBounds();
    updatePluginParameterScrollbar();
}

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
    SubTabItem items[] = {
        { "PARAMS", EditSubTab::Parameters },
        { "MIDI", EditSubTab::MidiOut },
        { "MOD", EditSubTab::Modulation }
    };

    g.setFont (lookAndFeel.getMonoFont (10.0f));
    int itemH = 30;

    for (int i = 0; i < 3; ++i)
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
// Drawing: Plugin instrument info page
//==============================================================================

void SampleEditorComponent::drawPluginInstrumentPage (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto textCol = lookAndFeel.findColour (TrackerLookAndFeel::textColourId);
    auto mutedText = textCol.withAlpha (0.58f);
    auto gridCol = lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId);
    auto accentCol = lookAndFeel.findColour (TrackerLookAndFeel::fxColourId);
    auto pluginCol = juce::Colour (0xff89b4fa);

    area = area.reduced (16, 12);

    auto top = area.removeFromTop (48);
    auto openEditorBounds = getPluginEditorButtonBounds();

    g.setFont (lookAndFeel.getMonoFont (16.0f));
    g.setColour (pluginCol);
    g.drawText (pluginInstrumentName.isNotEmpty() ? pluginInstrumentName : "Plugin Instrument",
                top.removeFromLeft (juce::jmax (180, top.getWidth() - 170)),
                juce::Justification::centredLeft);
    drawPluginActionButton (g, openEditorBounds, "OPEN EDITOR", pluginCol);

    g.setFont (lookAndFeel.getMonoFont (10.0f));
    g.setColour (mutedText);
    g.drawText ("INST " + juce::String::formatted ("%02X", currentInstrument)
                    + "  TRACK " + (pluginOwnerTrack >= 0 ? juce::String (pluginOwnerTrack + 1) : juce::String ("-")),
                area.getX(), top.getY() + 24, area.getWidth(), 16, juce::Justification::centredLeft);

    area.removeFromTop (8);

    juce::Rectangle<int> sourceArea;
    juce::Rectangle<int> routeArea;
    if (area.getWidth() < 720)
    {
        sourceArea = area.removeFromTop (juce::jmin (300, area.getHeight() / 2));
        area.removeFromTop (12);
        routeArea = area;
    }
    else
    {
        const int sourceW = juce::jlimit (430, juce::jmax (430, area.getWidth() - 340),
                                          static_cast<int> (area.getWidth() * 0.58f));
        sourceArea = area.removeFromLeft (sourceW);
        area.removeFromLeft (14);
        routeArea = area;
    }

    auto drawSectionTitle = [&] (juce::Rectangle<int> titleArea, const juce::String& title)
    {
        g.setFont (lookAndFeel.getMonoFont (11.0f));
        g.setColour (textCol);
        g.drawText (title, titleArea, juce::Justification::centredLeft);
        g.setColour (gridCol);
        g.drawHorizontalLine (titleArea.getBottom() - 1,
                              static_cast<float> (titleArea.getX()),
                              static_cast<float> (titleArea.getRight()));
    };

    auto sourceTitle = sourceArea.removeFromTop (30);
    auto addEnvBounds = sourceTitle.removeFromRight (78).reduced (0, 3);
    sourceTitle.removeFromRight (8);
    auto addLfoBounds = sourceTitle.removeFromRight (72).reduced (0, 3);
    drawSectionTitle (sourceTitle, "MODULATORS");
    drawPluginActionButton (g, addLfoBounds, "+ LFO", getPluginSourceColour (0));
    drawPluginActionButton (g, addEnvBounds, "+ ENV", getPluginSourceColour (2));
    sourceArea.removeFromTop (10);

    const int sourceRowH = 52;
    for (int i = 0; i < static_cast<int> (pluginModulation.sources.size()); ++i)
    {
        if (sourceArea.getHeight() < sourceRowH)
            break;

        const auto& source = pluginModulation.sources[static_cast<size_t> (i)];
        auto row = sourceArea.removeFromTop (sourceRowH);
        sourceArea.removeFromTop (8);
        const auto colour = getPluginSourceColour (i);
        const bool selected = i == selectedPluginSourceIndex;
        fillRoundedRow (g, row, colour, selected);

        auto inner = row.reduced (8, 6);
        auto enableBox = inner.removeFromLeft (20).withSizeKeepingCentre (14, 14);
        g.setColour (source.enabled ? colour : mutedText.withAlpha (0.35f));
        g.drawRect (enableBox, 1);
        if (source.enabled)
            g.fillRect (enableBox.reduced (3));
        inner.removeFromLeft (8);

        auto nameArea = inner.removeFromLeft (72);
        g.setFont (lookAndFeel.getMonoFont (12.0f));
        g.setColour (source.enabled ? textCol : mutedText);
        g.drawText (getPluginSourceDisplayName (source, i), nameArea.removeFromTop (20),
                    juce::Justification::centredLeft);
        g.setFont (lookAndFeel.getMonoFont (9.0f));
        g.setColour (colour.withAlpha (0.75f));
        g.drawText (source.type == PluginModulatorSource::Type::LFO ? "LFO" : "ENV",
                    nameArea, juce::Justification::centredLeft);

        auto preview = inner.removeFromLeft (94).reduced (2, 2);
        if (source.type == PluginModulatorSource::Type::LFO)
            drawPluginLfoPreview (g, preview, source, colour);
        else
            drawPluginEnvelopePreview (g, preview, source, colour);

        inner.removeFromLeft (8);
        auto removeBounds = inner.removeFromRight (20).withSizeKeepingCentre (18, 18);

        auto drawCell = [&] (juce::Rectangle<int> cell, const juce::String& label,
                             const juce::String& value, juce::Colour cellColour)
        {
            g.setColour (cellColour.withAlpha (0.12f));
            g.fillRect (cell);
            g.setColour (cellColour.withAlpha (0.42f));
            g.drawRect (cell, 1);
            g.setFont (lookAndFeel.getMonoFont (8.0f));
            g.setColour (mutedText);
            g.drawText (label, cell.reduced (5, 1).removeFromTop (12), juce::Justification::centredLeft);
            g.setFont (lookAndFeel.getMonoFont (10.0f));
            g.setColour (textCol);
            g.drawText (value, cell.reduced (5, 1).withTrimmedTop (13), juce::Justification::centredLeft);
        };

        if (source.type == PluginModulatorSource::Type::LFO)
        {
            drawCell (inner.removeFromLeft (66), "SHAPE", getPluginLfoShapeName (source.lfoShape), colour);
            inner.removeFromLeft (6);
            drawCell (inner.removeFromLeft (58), "RATE", source.lfoRateMode == PluginModulatorSource::LfoRateMode::Hz ? "Hz" : "Step", colour);
            inner.removeFromLeft (6);
            drawCell (inner.removeFromLeft (78), "VALUE",
                      source.lfoRateMode == PluginModulatorSource::LfoRateMode::Hz
                          ? juce::String (source.lfoRateHz, 2)
                          : juce::String (juce::roundToInt (source.lfoRateSteps)),
                      colour);
        }
        else
        {
            drawCell (inner.removeFromLeft (76), "TRIG", getPluginEnvTriggerName (source.envelopeTriggerMode), colour);
            inner.removeFromLeft (6);
            drawCell (inner.removeFromLeft (55), "A", formatPluginSeconds (source.attackS), colour);
            inner.removeFromLeft (5);
            drawCell (inner.removeFromLeft (55), "D", formatPluginSeconds (source.decayS), colour);
            inner.removeFromLeft (5);
            drawCell (inner.removeFromLeft (50), "S", juce::String (juce::roundToInt (source.sustain * 100.0)) + "%", colour);
            inner.removeFromLeft (5);
            drawCell (inner.removeFromLeft (55), "R", formatPluginSeconds (source.releaseS), colour);
        }

        g.setColour (mutedText);
        g.setFont (lookAndFeel.getMonoFont (13.0f));
        g.drawText ("x", removeBounds, juce::Justification::centred);
    }

    auto routeTitle = routeArea.removeFromTop (30);
    drawSectionTitle (routeTitle, "ROUTES");
    routeArea.removeFromTop (10);

    const int routeRowH = 36;
    const int maxRouteRows = juce::jmax (2, juce::jmin (5, (routeArea.getHeight() - 150) / (routeRowH + 6)));
    auto routesBlock = routeArea.removeFromTop (juce::jmin (routeArea.getHeight(), maxRouteRows * (routeRowH + 6)));

    for (int i = 0; i < static_cast<int> (pluginModulation.routes.size()) && i < maxRouteRows; ++i)
    {
        const auto& route = pluginModulation.routes[static_cast<size_t> (i)];
        auto row = routesBlock.removeFromTop (routeRowH);
        routesBlock.removeFromTop (6);
        const int sourceIndex = juce::jlimit (0, juce::jmax (0, static_cast<int> (pluginModulation.sources.size()) - 1), route.sourceIndex);
        const auto colour = getPluginSourceColour (sourceIndex);
        fillRoundedRow (g, row, colour, i == selectedPluginRouteIndex);

        auto inner = row.reduced (7, 5);
        auto enableBox = inner.removeFromLeft (16).withSizeKeepingCentre (12, 12);
        g.setColour (route.enabled ? colour : mutedText.withAlpha (0.35f));
        g.drawRect (enableBox, 1);
        if (route.enabled)
            g.fillRect (enableBox.reduced (3));
        inner.removeFromLeft (6);

        auto src = inner.removeFromLeft (68);
        g.setFont (lookAndFeel.getMonoFont (9.0f));
        g.setColour (colour);
        if (route.sourceIndex >= 0 && route.sourceIndex < static_cast<int> (pluginModulation.sources.size()))
            g.drawText (getPluginSourceDisplayName (pluginModulation.sources[static_cast<size_t> (route.sourceIndex)], route.sourceIndex),
                        src, juce::Justification::centredLeft);
        else
            g.drawText ("Source", src, juce::Justification::centredLeft);

        auto removeBounds = inner.removeFromRight (18);
        auto amountBounds = inner.removeFromRight (70).reduced (0, 4);
        inner.removeFromRight (8);
        auto paramBounds = inner;

        g.setColour (textCol);
        g.setFont (lookAndFeel.getMonoFont (10.0f));
        g.drawFittedText (route.parameterName.isNotEmpty() ? route.parameterName : ("Param " + juce::String (route.parameterIndex)),
                          paramBounds, juce::Justification::centredLeft, 1);

        g.setColour (juce::Colour (0xff0b0d10));
        g.fillRect (amountBounds);
        g.setColour (gridCol);
        g.drawRect (amountBounds, 1);
        const int centreX = amountBounds.getCentreX();
        const int amountPixels = juce::roundToInt (route.amount * static_cast<float> (amountBounds.getWidth()) * 0.5f);
        auto fill = amountBounds.withX (amountPixels >= 0 ? centreX : centreX + amountPixels)
                                .withWidth (std::abs (amountPixels));
        g.setColour ((route.amount >= 0.0f ? colour : juce::Colour (0xffff7a7a)).withAlpha (0.75f));
        g.fillRect (fill);
        g.setColour (mutedText);
        g.setFont (lookAndFeel.getMonoFont (8.0f));
        g.drawText (juce::String (juce::roundToInt (route.amount * 100.0f)) + "%",
                    amountBounds, juce::Justification::centred);
        g.setFont (lookAndFeel.getMonoFont (12.0f));
        g.drawText ("x", removeBounds, juce::Justification::centred);
    }

    if (pluginModulation.routes.empty())
    {
        g.setColour (mutedText);
        g.setFont (lookAndFeel.getMonoFont (11.0f));
        g.drawText ("No routes", routesBlock.withHeight (30), juce::Justification::centredLeft);
    }

    routeArea.removeFromTop (8);
    auto paramTitle = routeArea.removeFromTop (24);
    auto searchBounds = getPluginParameterSearchBoxBounds();
    g.setColour (accentCol);
    g.setFont (lookAndFeel.getMonoFont (10.0f));
    const juce::String selectedSourceName = selectedPluginSourceIndex >= 0
        && selectedPluginSourceIndex < static_cast<int> (pluginModulation.sources.size())
            ? getPluginSourceDisplayName (pluginModulation.sources[static_cast<size_t> (selectedPluginSourceIndex)],
                                          selectedPluginSourceIndex)
            : juce::String ("Source");
    auto titleTextArea = paramTitle.withRight (juce::jmax (paramTitle.getX(), searchBounds.getX() - 8));
    g.drawText ("ASSIGN " + selectedSourceName + " TO PARAMETER", titleTextArea, juce::Justification::centredLeft);

    auto paramList = getPluginParameterListContentBounds();
    const int visibleParams = getPluginParameterVisibleRows();
    pluginParameterScroll = juce::jlimit (0, getPluginParameterMaxScroll(), pluginParameterScroll);
    const int filteredParamCount = getFilteredPluginParameterCount();

    for (int rowIndex = 0; rowIndex < visibleParams; ++rowIndex)
    {
        const int filteredRow = pluginParameterScroll + rowIndex;
        if (filteredRow >= filteredParamCount)
            break;

        const int paramListIndex = getPluginParameterInfoIndexAtFilteredRow (filteredRow);
        if (paramListIndex < 0 || paramListIndex >= static_cast<int> (pluginParameterInfos.size()))
            break;

        const auto& param = pluginParameterInfos[static_cast<size_t> (paramListIndex)];
        auto row = paramList.removeFromTop (kPluginParameterRowHeight).reduced (0, 3);
        bool hasRoute = false;
        float existingAmount = 0.0f;
        for (const auto& route : pluginModulation.routes)
        {
            if (route.sourceIndex == selectedPluginSourceIndex && route.parameterIndex == param.index)
            {
                hasRoute = true;
                existingAmount = route.amount;
                break;
            }
        }

        g.setColour (hasRoute ? accentCol.withAlpha (0.16f) : juce::Colour (0xff15181d));
        g.fillRect (row);
        g.setColour (hasRoute ? accentCol.withAlpha (0.65f) : gridCol);
        g.drawRect (row, 1);
        auto plus = row.removeFromLeft (28);
        g.setFont (lookAndFeel.getMonoFont (13.0f));
        g.setColour (hasRoute ? accentCol : mutedText);
        g.drawText (hasRoute ? juce::String (juce::roundToInt (existingAmount * 100.0f)) + "%" : "+",
                    plus, juce::Justification::centred);
        g.setColour (textCol);
        g.setFont (lookAndFeel.getMonoFont (10.0f));
        g.drawFittedText (param.name, row.reduced (6, 0), juce::Justification::centredLeft, 1);
    }

    if (filteredParamCount == 0)
    {
        g.setColour (mutedText);
        g.setFont (lookAndFeel.getMonoFont (10.0f));
        g.drawText (pluginParameterSearchBox.getText().isNotEmpty() ? "No matching params" : "No params",
                    getPluginParameterListContentBounds().withHeight (24),
                    juce::Justification::centredLeft);
    }

    auto focusBounds = getPluginKeyboardFocusBounds();
    if (! focusBounds.isEmpty())
    {
        g.setColour (accentCol.withAlpha (0.95f));
        g.drawRect (focusBounds.expanded (2), 2);
    }
}

juce::Rectangle<int> SampleEditorComponent::getPluginEditorButtonBounds() const
{
    auto area = juce::Rectangle<int> (0, kHeaderHeight, getWidth(), getHeight() - kHeaderHeight).reduced (16, 12);
    return { area.getRight() - 140, area.getY() + 8, 124, 28 };
}

void SampleEditorComponent::drawPluginActionButton (juce::Graphics& g,
                                                    juce::Rectangle<int> bounds,
                                                    const juce::String& text,
                                                    juce::Colour colour)
{
    auto bg = colour.withAlpha (0.18f);
    auto border = colour.withAlpha (0.75f);
    auto textCol = lookAndFeel.findColour (TrackerLookAndFeel::textColourId);

    g.setColour (bg);
    g.fillRect (bounds);
    g.setColour (border);
    g.drawRect (bounds, 1);
    g.setColour (textCol);
    g.setFont (lookAndFeel.getMonoFont (11.0f));
    g.drawText (text, bounds.reduced (8, 0), juce::Justification::centred);
}

void SampleEditorComponent::drawPluginLfoPreview (juce::Graphics& g,
                                                  juce::Rectangle<int> bounds,
                                                  const PluginModulatorSource& source,
                                                  juce::Colour colour)
{
    g.setColour (juce::Colour (0xff0b0d10));
    g.fillRect (bounds);
    g.setColour (colour.withAlpha (0.28f));
    g.drawRect (bounds, 1);

    juce::Path path;
    const int w = juce::jmax (2, bounds.getWidth());
    const float midY = static_cast<float> (bounds.getCentreY());
    const float amp = static_cast<float> (bounds.getHeight()) * 0.36f;

    for (int x = 0; x < w; ++x)
    {
        const float phase = static_cast<float> (x) / static_cast<float> (w - 1);
        float value = 0.0f;
        switch (source.lfoShape)
        {
            case PluginModulatorSource::LfoShape::Sine:     value = std::sin (phase * juce::MathConstants<float>::twoPi); break;
            case PluginModulatorSource::LfoShape::Triangle: value = 1.0f - 4.0f * std::abs (phase - 0.5f); break;
            case PluginModulatorSource::LfoShape::Saw:      value = phase * 2.0f - 1.0f; break;
            case PluginModulatorSource::LfoShape::Square:   value = phase < 0.5f ? 1.0f : -1.0f; break;
            case PluginModulatorSource::LfoShape::Random:   value = std::sin (phase * 31.0f) > 0.0f ? 0.75f : -0.75f; break;
        }

        const float y = midY - value * amp;
        const float px = static_cast<float> (bounds.getX() + x);
        if (x == 0) path.startNewSubPath (px, y);
        else        path.lineTo (px, y);
    }

    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (1.8f));
}

void SampleEditorComponent::drawPluginEnvelopePreview (juce::Graphics& g,
                                                       juce::Rectangle<int> bounds,
                                                       const PluginModulatorSource& source,
                                                       juce::Colour colour)
{
    g.setColour (juce::Colour (0xff0b0d10));
    g.fillRect (bounds);
    g.setColour (colour.withAlpha (0.28f));
    g.drawRect (bounds, 1);

    const float left = static_cast<float> (bounds.getX() + 4);
    const float right = static_cast<float> (bounds.getRight() - 4);
    const float bottom = static_cast<float> (bounds.getBottom() - 4);
    const float top = static_cast<float> (bounds.getY() + 4);
    const float sustainY = bottom - static_cast<float> (source.sustain) * (bottom - top);
    const float width = right - left;

    const float a = juce::jlimit (0.08f, 0.34f, static_cast<float> (source.attackS / 2.0));
    const float d = juce::jlimit (0.08f, 0.34f, static_cast<float> (source.decayS / 2.0));
    const float r = juce::jlimit (0.10f, 0.36f, static_cast<float> (source.releaseS / 2.0));
    const float x0 = left;
    const float x1 = left + width * a;
    const float x2 = x1 + width * d;
    const float x3 = right - width * r;
    const float x4 = right;

    juce::Path path;
    path.startNewSubPath (x0, bottom);
    path.lineTo (x1, top);
    path.lineTo (x2, sustainY);
    path.lineTo (x3, sustainY);
    path.lineTo (x4, bottom);

    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (1.8f));
}

SampleEditorComponent::PluginHit SampleEditorComponent::hitTestPluginPage (juce::Point<int> pos) const
{
    if (getPluginEditorButtonBounds().contains (pos))
        return { PluginHitKind::OpenEditor, -1, -1 };

    auto area = juce::Rectangle<int> (0, kHeaderHeight, getWidth(), getHeight() - kHeaderHeight).reduced (16, 12);
    area.removeFromTop (48);
    area.removeFromTop (8);

    juce::Rectangle<int> sourceArea;
    juce::Rectangle<int> routeArea;
    if (area.getWidth() < 720)
    {
        sourceArea = area.removeFromTop (juce::jmin (300, area.getHeight() / 2));
        area.removeFromTop (12);
        routeArea = area;
    }
    else
    {
        const int sourceW = juce::jlimit (430, juce::jmax (430, area.getWidth() - 340),
                                          static_cast<int> (area.getWidth() * 0.58f));
        sourceArea = area.removeFromLeft (sourceW);
        area.removeFromLeft (14);
        routeArea = area;
    }

    auto sourceTitle = sourceArea.removeFromTop (30);
    auto addEnvBounds = sourceTitle.removeFromRight (78).reduced (0, 3);
    sourceTitle.removeFromRight (8);
    auto addLfoBounds = sourceTitle.removeFromRight (72).reduced (0, 3);
    if (addLfoBounds.contains (pos))
        return { PluginHitKind::AddLfo, -1, -1 };
    if (addEnvBounds.contains (pos))
        return { PluginHitKind::AddEnvelope, -1, -1 };

    sourceArea.removeFromTop (10);
    const int sourceRowH = 52;
    for (int i = 0; i < static_cast<int> (pluginModulation.sources.size()); ++i)
    {
        if (sourceArea.getHeight() < sourceRowH)
            break;

        auto row = sourceArea.removeFromTop (sourceRowH);
        sourceArea.removeFromTop (8);
        if (! row.contains (pos))
            continue;

        auto inner = row.reduced (8, 6);
        auto enableBox = inner.removeFromLeft (20).withSizeKeepingCentre (14, 14);
        if (enableBox.expanded (4).contains (pos))
            return { PluginHitKind::SourceEnable, i, -1 };

        inner.removeFromLeft (8);
        inner.removeFromLeft (72);
        inner.removeFromLeft (94);
        inner.removeFromLeft (8);
        auto removeBounds = inner.removeFromRight (20).withSizeKeepingCentre (18, 18);
        if (removeBounds.expanded (5).contains (pos))
            return { PluginHitKind::SourceRemove, i, -1 };

        const auto& source = pluginModulation.sources[static_cast<size_t> (i)];
        if (source.type == PluginModulatorSource::Type::LFO)
        {
            auto shape = inner.removeFromLeft (66);
            inner.removeFromLeft (6);
            auto rateMode = inner.removeFromLeft (58);
            inner.removeFromLeft (6);
            auto value = inner.removeFromLeft (78);
            if (shape.contains (pos))    return { PluginHitKind::LfoShape, i, -1 };
            if (rateMode.contains (pos)) return { PluginHitKind::LfoRateMode, i, -1 };
            if (value.contains (pos))    return { PluginHitKind::LfoRateValue, i, -1 };
        }
        else
        {
            auto trigger = inner.removeFromLeft (76);
            inner.removeFromLeft (6);
            auto attack = inner.removeFromLeft (55);
            inner.removeFromLeft (5);
            auto decay = inner.removeFromLeft (55);
            inner.removeFromLeft (5);
            auto sustain = inner.removeFromLeft (50);
            inner.removeFromLeft (5);
            auto release = inner.removeFromLeft (55);
            if (trigger.contains (pos)) return { PluginHitKind::EnvTrigger, i, -1 };
            if (attack.contains (pos))  return { PluginHitKind::EnvAttack, i, -1 };
            if (decay.contains (pos))   return { PluginHitKind::EnvDecay, i, -1 };
            if (sustain.contains (pos)) return { PluginHitKind::EnvSustain, i, -1 };
            if (release.contains (pos)) return { PluginHitKind::EnvRelease, i, -1 };
        }

        return { PluginHitKind::SourceSelect, i, -1 };
    }

    routeArea.removeFromTop (30);
    routeArea.removeFromTop (10);
    const int routeRowH = 36;
    const int maxRouteRows = juce::jmax (2, juce::jmin (5, (routeArea.getHeight() - 150) / (routeRowH + 6)));
    auto routesBlock = routeArea.removeFromTop (juce::jmin (routeArea.getHeight(), maxRouteRows * (routeRowH + 6)));

    for (int i = 0; i < static_cast<int> (pluginModulation.routes.size()) && i < maxRouteRows; ++i)
    {
        auto row = routesBlock.removeFromTop (routeRowH);
        routesBlock.removeFromTop (6);
        if (! row.contains (pos))
            continue;

        auto inner = row.reduced (7, 5);
        auto enableBox = inner.removeFromLeft (16).withSizeKeepingCentre (12, 12);
        if (enableBox.expanded (4).contains (pos))
            return { PluginHitKind::RouteEnable, i, -1 };

        inner.removeFromLeft (6);
        auto src = inner.removeFromLeft (68);
        auto removeBounds = inner.removeFromRight (18);
        auto amountBounds = inner.removeFromRight (70).reduced (0, 4);
        inner.removeFromRight (8);
        auto paramBounds = inner;

        if (src.contains (pos))             return { PluginHitKind::RouteSource, i, -1 };
        if (removeBounds.contains (pos))    return { PluginHitKind::RouteRemove, i, -1 };
        if (amountBounds.contains (pos))    return { PluginHitKind::RouteAmount, i, -1 };
        if (paramBounds.contains (pos))     return { PluginHitKind::RouteParam, i, -1 };
        return { PluginHitKind::RouteSelect, i, -1 };
    }

    routeArea.removeFromTop (8);
    routeArea.removeFromTop (24);
    if (getPluginParameterSearchBoxBounds().contains (pos))
        return {};

    auto paramList = getPluginParameterListContentBounds();
    const int visibleParams = getPluginParameterVisibleRows();
    const int filteredParamCount = getFilteredPluginParameterCount();
    for (int rowIndex = 0; rowIndex < visibleParams; ++rowIndex)
    {
        const int filteredRow = pluginParameterScroll + rowIndex;
        if (filteredRow >= filteredParamCount)
            break;

        const int paramListIndex = getPluginParameterInfoIndexAtFilteredRow (filteredRow);
        if (paramListIndex < 0 || paramListIndex >= static_cast<int> (pluginParameterInfos.size()))
            break;

        auto row = paramList.removeFromTop (kPluginParameterRowHeight).reduced (0, 3);
        if (row.contains (pos))
            return { PluginHitKind::ParamAssign, paramListIndex, pluginParameterInfos[static_cast<size_t> (paramListIndex)].index };
    }

    return {};
}

std::vector<SampleEditorComponent::PluginKeyboardTarget> SampleEditorComponent::getPluginKeyboardTargets() const
{
    std::vector<PluginKeyboardTarget> targets;

    auto addTarget = [&] (PluginHit hit, juce::Rectangle<int> bounds)
    {
        if (! bounds.isEmpty())
            targets.push_back ({ hit, bounds });
    };

    addTarget ({ PluginHitKind::OpenEditor, -1, -1 }, getPluginEditorButtonBounds());

    auto area = juce::Rectangle<int> (0, kHeaderHeight, getWidth(), getHeight() - kHeaderHeight).reduced (16, 12);
    area.removeFromTop (48);
    area.removeFromTop (8);

    juce::Rectangle<int> sourceArea;
    juce::Rectangle<int> routeArea;
    if (area.getWidth() < 720)
    {
        sourceArea = area.removeFromTop (juce::jmin (300, area.getHeight() / 2));
        area.removeFromTop (12);
        routeArea = area;
    }
    else
    {
        const int sourceW = juce::jlimit (430, juce::jmax (430, area.getWidth() - 340),
                                          static_cast<int> (area.getWidth() * 0.58f));
        sourceArea = area.removeFromLeft (sourceW);
        area.removeFromLeft (14);
        routeArea = area;
    }

    auto sourceTitle = sourceArea.removeFromTop (30);
    auto addEnvBounds = sourceTitle.removeFromRight (78).reduced (0, 3);
    sourceTitle.removeFromRight (8);
    auto addLfoBounds = sourceTitle.removeFromRight (72).reduced (0, 3);
    addTarget ({ PluginHitKind::AddLfo, -1, -1 }, addLfoBounds);
    addTarget ({ PluginHitKind::AddEnvelope, -1, -1 }, addEnvBounds);

    sourceArea.removeFromTop (10);
    const int sourceRowH = 52;
    for (int i = 0; i < static_cast<int> (pluginModulation.sources.size()); ++i)
    {
        if (sourceArea.getHeight() < sourceRowH)
            break;

        auto row = sourceArea.removeFromTop (sourceRowH);
        sourceArea.removeFromTop (8);

        auto inner = row.reduced (8, 6);
        auto enableBox = inner.removeFromLeft (20).withSizeKeepingCentre (14, 14).expanded (4);
        inner.removeFromLeft (8);
        auto nameArea = inner.removeFromLeft (72);
        auto preview = inner.removeFromLeft (94).reduced (2, 2);
        auto selectBounds = nameArea.getUnion (preview).expanded (2);
        inner.removeFromLeft (8);
        auto removeBounds = inner.removeFromRight (20).withSizeKeepingCentre (18, 18).expanded (5);

        addTarget ({ PluginHitKind::SourceSelect, i, -1 }, selectBounds);
        addTarget ({ PluginHitKind::SourceEnable, i, -1 }, enableBox);

        const auto& source = pluginModulation.sources[static_cast<size_t> (i)];
        if (source.type == PluginModulatorSource::Type::LFO)
        {
            auto shape = inner.removeFromLeft (66);
            inner.removeFromLeft (6);
            auto rateMode = inner.removeFromLeft (58);
            inner.removeFromLeft (6);
            auto value = inner.removeFromLeft (78);
            addTarget ({ PluginHitKind::LfoShape, i, -1 }, shape);
            addTarget ({ PluginHitKind::LfoRateMode, i, -1 }, rateMode);
            addTarget ({ PluginHitKind::LfoRateValue, i, -1 }, value);
        }
        else
        {
            auto trigger = inner.removeFromLeft (76);
            inner.removeFromLeft (6);
            auto attack = inner.removeFromLeft (55);
            inner.removeFromLeft (5);
            auto decay = inner.removeFromLeft (55);
            inner.removeFromLeft (5);
            auto sustain = inner.removeFromLeft (50);
            inner.removeFromLeft (5);
            auto release = inner.removeFromLeft (55);
            addTarget ({ PluginHitKind::EnvTrigger, i, -1 }, trigger);
            addTarget ({ PluginHitKind::EnvAttack, i, -1 }, attack);
            addTarget ({ PluginHitKind::EnvDecay, i, -1 }, decay);
            addTarget ({ PluginHitKind::EnvSustain, i, -1 }, sustain);
            addTarget ({ PluginHitKind::EnvRelease, i, -1 }, release);
        }

        addTarget ({ PluginHitKind::SourceRemove, i, -1 }, removeBounds);
    }

    routeArea.removeFromTop (30);
    routeArea.removeFromTop (10);
    const int routeRowH = 36;
    const int maxRouteRows = juce::jmax (2, juce::jmin (5, (routeArea.getHeight() - 150) / (routeRowH + 6)));
    auto routesBlock = routeArea.removeFromTop (juce::jmin (routeArea.getHeight(), maxRouteRows * (routeRowH + 6)));

    for (int i = 0; i < static_cast<int> (pluginModulation.routes.size()) && i < maxRouteRows; ++i)
    {
        auto row = routesBlock.removeFromTop (routeRowH);
        routesBlock.removeFromTop (6);

        auto inner = row.reduced (7, 5);
        auto enableBox = inner.removeFromLeft (16).withSizeKeepingCentre (12, 12).expanded (4);
        inner.removeFromLeft (6);
        auto src = inner.removeFromLeft (68);
        auto removeBounds = inner.removeFromRight (18);
        auto amountBounds = inner.removeFromRight (70).reduced (0, 4);
        inner.removeFromRight (8);
        auto paramBounds = inner;

        addTarget ({ PluginHitKind::RouteSelect, i, -1 }, row.withRight (src.getRight()));
        addTarget ({ PluginHitKind::RouteEnable, i, -1 }, enableBox);
        addTarget ({ PluginHitKind::RouteSource, i, -1 }, src);
        addTarget ({ PluginHitKind::RouteParam, i, -1 }, paramBounds);
        addTarget ({ PluginHitKind::RouteAmount, i, -1 }, amountBounds);
        addTarget ({ PluginHitKind::RouteRemove, i, -1 }, removeBounds);
    }

    routeArea.removeFromTop (8);
    routeArea.removeFromTop (24);
    addTarget ({ PluginHitKind::ParamSearch, -1, -1 }, getPluginParameterSearchBoxBounds());

    auto paramList = getPluginParameterListContentBounds();
    const int visibleParams = getPluginParameterVisibleRows();
    const int filteredParamCount = getFilteredPluginParameterCount();
    for (int rowIndex = 0; rowIndex < visibleParams; ++rowIndex)
    {
        const int filteredRow = pluginParameterScroll + rowIndex;
        if (filteredRow >= filteredParamCount)
            break;

        const int paramListIndex = getPluginParameterInfoIndexAtFilteredRow (filteredRow);
        if (paramListIndex < 0 || paramListIndex >= static_cast<int> (pluginParameterInfos.size()))
            break;

        auto row = paramList.removeFromTop (kPluginParameterRowHeight).reduced (0, 3);
        addTarget ({ PluginHitKind::ParamAssign, paramListIndex,
                     pluginParameterInfos[static_cast<size_t> (paramListIndex)].index },
                   row);
    }

    return targets;
}

juce::Rectangle<int> SampleEditorComponent::getPluginKeyboardFocusBounds() const
{
    for (const auto& target : getPluginKeyboardTargets())
        if (pluginHitsEqual (target.hit, focusedPluginHit))
            return target.bounds;

    return {};
}

bool SampleEditorComponent::pluginHitsEqual (const PluginHit& a, const PluginHit& b) const
{
    return a.kind == b.kind && a.index == b.index && a.parameterIndex == b.parameterIndex;
}

bool SampleEditorComponent::pluginHitCanAdjustWithKeyboard (const PluginHit& hit) const
{
    switch (hit.kind)
    {
        case PluginHitKind::LfoShape:
        case PluginHitKind::LfoRateMode:
        case PluginHitKind::LfoRateValue:
        case PluginHitKind::EnvTrigger:
        case PluginHitKind::EnvAttack:
        case PluginHitKind::EnvDecay:
        case PluginHitKind::EnvSustain:
        case PluginHitKind::EnvRelease:
        case PluginHitKind::RouteSource:
        case PluginHitKind::RouteParam:
        case PluginHitKind::RouteAmount:
            return true;
        case PluginHitKind::None:
        case PluginHitKind::OpenEditor:
        case PluginHitKind::AddLfo:
        case PluginHitKind::AddEnvelope:
        case PluginHitKind::SourceSelect:
        case PluginHitKind::SourceEnable:
        case PluginHitKind::SourceRemove:
        case PluginHitKind::ParamSearch:
        case PluginHitKind::ParamAssign:
        case PluginHitKind::RouteSelect:
        case PluginHitKind::RouteEnable:
        case PluginHitKind::RouteRemove:
            return false;
    }

    return false;
}

void SampleEditorComponent::ensureFocusedPluginHitValid()
{
    auto targets = getPluginKeyboardTargets();
    if (targets.empty())
    {
        focusedPluginHit = {};
        return;
    }

    for (const auto& target : targets)
        if (pluginHitsEqual (target.hit, focusedPluginHit))
            return;

    if (focusedPluginHit.kind == PluginHitKind::ParamAssign)
    {
        for (const auto& target : targets)
        {
            if (target.hit.kind == PluginHitKind::ParamAssign)
            {
                focusedPluginHit = target.hit;
                return;
            }
        }
    }

    focusedPluginHit = targets.front().hit;
}

void SampleEditorComponent::movePluginKeyboardFocus (int direction)
{
    auto targets = getPluginKeyboardTargets();
    if (targets.empty())
        return;

    int current = 0;
    for (int i = 0; i < static_cast<int> (targets.size()); ++i)
    {
        if (pluginHitsEqual (targets[static_cast<size_t> (i)].hit, focusedPluginHit))
        {
            current = i;
            break;
        }
    }

    const int next = (current + direction + static_cast<int> (targets.size()))
                     % static_cast<int> (targets.size());
    focusedPluginHit = targets[static_cast<size_t> (next)].hit;

    if (focusedPluginHit.kind == PluginHitKind::SourceSelect
        || focusedPluginHit.kind == PluginHitKind::SourceEnable
        || focusedPluginHit.kind == PluginHitKind::SourceRemove
        || focusedPluginHit.kind == PluginHitKind::LfoShape
        || focusedPluginHit.kind == PluginHitKind::LfoRateMode
        || focusedPluginHit.kind == PluginHitKind::LfoRateValue
        || focusedPluginHit.kind == PluginHitKind::EnvTrigger
        || focusedPluginHit.kind == PluginHitKind::EnvAttack
        || focusedPluginHit.kind == PluginHitKind::EnvDecay
        || focusedPluginHit.kind == PluginHitKind::EnvSustain
        || focusedPluginHit.kind == PluginHitKind::EnvRelease)
        selectPluginSource (focusedPluginHit.index);

    if (focusedPluginHit.kind == PluginHitKind::RouteSelect
        || focusedPluginHit.kind == PluginHitKind::RouteEnable
        || focusedPluginHit.kind == PluginHitKind::RouteRemove
        || focusedPluginHit.kind == PluginHitKind::RouteSource
        || focusedPluginHit.kind == PluginHitKind::RouteParam
        || focusedPluginHit.kind == PluginHitKind::RouteAmount)
    {
        selectedPluginRouteIndex = focusedPluginHit.index;
        if (focusedPluginHit.index >= 0
            && focusedPluginHit.index < static_cast<int> (pluginModulation.routes.size()))
            selectPluginSource (pluginModulation.routes[static_cast<size_t> (focusedPluginHit.index)].sourceIndex);
    }

    repaint();
}

void SampleEditorComponent::movePluginKeyboardFocusToParameter (int direction)
{
    const int filteredCount = getFilteredPluginParameterCount();
    if (filteredCount <= 0)
    {
        focusedPluginHit = { PluginHitKind::ParamSearch, -1, -1 };
        repaint();
        return;
    }

    int currentFilteredRow = pluginParameterScroll;
    if (focusedPluginHit.kind == PluginHitKind::ParamAssign)
    {
        for (int row = 0; row < filteredCount; ++row)
        {
            if (getPluginParameterInfoIndexAtFilteredRow (row) == focusedPluginHit.index)
            {
                currentFilteredRow = row;
                break;
            }
        }
    }
    else if (direction > 0)
    {
        currentFilteredRow = pluginParameterScroll - 1;
    }
    else if (direction < 0)
    {
        currentFilteredRow = juce::jmin (filteredCount,
                                         pluginParameterScroll + juce::jmax (1, getPluginParameterVisibleRows()));
    }

    const int targetFilteredRow = juce::jlimit (0, filteredCount - 1,
                                                currentFilteredRow + direction);
    const int visibleRows = juce::jmax (1, getPluginParameterVisibleRows());
    if (targetFilteredRow < pluginParameterScroll)
        pluginParameterScroll = targetFilteredRow;
    else if (targetFilteredRow >= pluginParameterScroll + visibleRows)
        pluginParameterScroll = targetFilteredRow - visibleRows + 1;

    pluginParameterScroll = juce::jlimit (0, getPluginParameterMaxScroll(), pluginParameterScroll);
    updatePluginParameterScrollbar();

    const int paramInfoIndex = getPluginParameterInfoIndexAtFilteredRow (targetFilteredRow);
    if (paramInfoIndex >= 0 && paramInfoIndex < static_cast<int> (pluginParameterInfos.size()))
    {
        focusedPluginHit = { PluginHitKind::ParamAssign, paramInfoIndex,
                             pluginParameterInfos[static_cast<size_t> (paramInfoIndex)].index };
    }

    repaint (getPluginParameterListBounds().expanded (2));
}

void SampleEditorComponent::focusFirstPluginParameter()
{
    if (getFilteredPluginParameterCount() <= 0)
    {
        focusedPluginHit = { PluginHitKind::ParamSearch, -1, -1 };
        repaint();
        return;
    }

    pluginParameterScroll = juce::jlimit (0, getPluginParameterMaxScroll(), pluginParameterScroll);
    const int paramInfoIndex = getPluginParameterInfoIndexAtFilteredRow (pluginParameterScroll);
    if (paramInfoIndex >= 0 && paramInfoIndex < static_cast<int> (pluginParameterInfos.size()))
    {
        focusedPluginHit = { PluginHitKind::ParamAssign, paramInfoIndex,
                             pluginParameterInfos[static_cast<size_t> (paramInfoIndex)].index };
    }
    updatePluginParameterScrollbar();
    repaint();
}

void SampleEditorComponent::activateFocusedPluginHit()
{
    ensureFocusedPluginHitValid();
    const auto hit = focusedPluginHit;

    auto sourceInRange = [&] (int index)
    {
        return index >= 0 && index < static_cast<int> (pluginModulation.sources.size());
    };
    auto routeInRange = [&] (int index)
    {
        return index >= 0 && index < static_cast<int> (pluginModulation.routes.size());
    };

    switch (hit.kind)
    {
        case PluginHitKind::OpenEditor:
            if (onOpenPluginEditorRequested)
                onOpenPluginEditorRequested (currentInstrument);
            break;
        case PluginHitKind::AddLfo:
            selectedPluginSourceIndex = pluginModulation.addLfo();
            focusedPluginHit = { PluginHitKind::SourceSelect, selectedPluginSourceIndex, -1 };
            notifyPluginModulationChanged();
            break;
        case PluginHitKind::AddEnvelope:
            selectedPluginSourceIndex = pluginModulation.addEnvelope();
            focusedPluginHit = { PluginHitKind::SourceSelect, selectedPluginSourceIndex, -1 };
            notifyPluginModulationChanged();
            break;
        case PluginHitKind::SourceSelect:
            if (sourceInRange (hit.index))
            {
                selectPluginSource (hit.index);
                repaint();
            }
            break;
        case PluginHitKind::SourceEnable:
            if (sourceInRange (hit.index))
            {
                auto& source = pluginModulation.sources[static_cast<size_t> (hit.index)];
                source.enabled = ! source.enabled;
                selectPluginSource (hit.index);
                notifyPluginModulationChanged();
            }
            break;
        case PluginHitKind::SourceRemove:
            if (sourceInRange (hit.index))
            {
                pluginModulation.removeSource (hit.index);
                selectedPluginSourceIndex = juce::jlimit (0, juce::jmax (0, static_cast<int> (pluginModulation.sources.size()) - 1),
                                                          selectedPluginSourceIndex);
                selectedPluginRouteIndex = pluginModulation.routes.empty()
                                               ? -1
                                               : juce::jlimit (0, static_cast<int> (pluginModulation.routes.size()) - 1,
                                                               selectedPluginRouteIndex);
                focusedPluginHit = { PluginHitKind::SourceSelect, selectedPluginSourceIndex, -1 };
                notifyPluginModulationChanged();
            }
            break;
        case PluginHitKind::LfoShape:
        case PluginHitKind::LfoRateMode:
        case PluginHitKind::EnvTrigger:
            adjustFocusedPluginHit (1, false, false);
            break;
        case PluginHitKind::ParamSearch:
            pluginParameterSearchBox.grabKeyboardFocus();
            pluginParameterSearchBox.selectAll();
            repaint();
            break;
        case PluginHitKind::ParamAssign:
            addPluginRouteForParam (hit.parameterIndex);
            break;
        case PluginHitKind::RouteSelect:
            if (routeInRange (hit.index))
            {
                selectedPluginRouteIndex = hit.index;
                selectPluginSource (pluginModulation.routes[static_cast<size_t> (hit.index)].sourceIndex);
                repaint();
            }
            break;
        case PluginHitKind::RouteEnable:
            if (routeInRange (hit.index))
            {
                auto& route = pluginModulation.routes[static_cast<size_t> (hit.index)];
                route.enabled = ! route.enabled;
                selectedPluginRouteIndex = hit.index;
                selectPluginSource (route.sourceIndex);
                notifyPluginModulationChanged();
            }
            break;
        case PluginHitKind::RouteRemove:
            if (routeInRange (hit.index))
            {
                pluginModulation.removeRoute (hit.index);
                selectedPluginRouteIndex = pluginModulation.routes.empty()
                                               ? -1
                                               : juce::jlimit (0, static_cast<int> (pluginModulation.routes.size()) - 1,
                                                               selectedPluginRouteIndex);
                focusedPluginHit = selectedPluginRouteIndex >= 0
                                       ? PluginHit { PluginHitKind::RouteSelect, selectedPluginRouteIndex, -1 }
                                       : PluginHit { PluginHitKind::AddLfo, -1, -1 };
                notifyPluginModulationChanged();
            }
            break;
        case PluginHitKind::RouteSource:
            showPluginRouteSourceMenu (hit.index);
            break;
        case PluginHitKind::RouteParam:
            showPluginRouteParamMenu (hit.index);
            break;
        case PluginHitKind::LfoRateValue:
        case PluginHitKind::EnvAttack:
        case PluginHitKind::EnvDecay:
        case PluginHitKind::EnvSustain:
        case PluginHitKind::EnvRelease:
        case PluginHitKind::RouteAmount:
        case PluginHitKind::None:
            break;
    }
}

void SampleEditorComponent::adjustFocusedPluginHit (int direction, bool fine, bool large)
{
    ensureFocusedPluginHitValid();
    auto hit = focusedPluginHit;

    auto sourceInRange = [&] (int index)
    {
        return index >= 0 && index < static_cast<int> (pluginModulation.sources.size());
    };
    auto routeInRange = [&] (int index)
    {
        return index >= 0 && index < static_cast<int> (pluginModulation.routes.size());
    };

    switch (hit.kind)
    {
        case PluginHitKind::LfoShape:
            if (sourceInRange (hit.index))
            {
                auto& source = pluginModulation.sources[static_cast<size_t> (hit.index)];
                constexpr int shapeCount = static_cast<int> (PluginModulatorSource::LfoShape::Random) + 1;
                source.lfoShape = static_cast<PluginModulatorSource::LfoShape> (
                    (static_cast<int> (source.lfoShape) + direction + shapeCount) % shapeCount);
                selectPluginSource (hit.index);
                notifyPluginModulationChanged();
            }
            break;
        case PluginHitKind::LfoRateMode:
            if (sourceInRange (hit.index))
            {
                auto& source = pluginModulation.sources[static_cast<size_t> (hit.index)];
                source.lfoRateMode = source.lfoRateMode == PluginModulatorSource::LfoRateMode::Hz
                                         ? PluginModulatorSource::LfoRateMode::Steps
                                         : PluginModulatorSource::LfoRateMode::Hz;
                selectPluginSource (hit.index);
                notifyPluginModulationChanged();
            }
            break;
        case PluginHitKind::EnvTrigger:
            if (sourceInRange (hit.index))
            {
                auto& source = pluginModulation.sources[static_cast<size_t> (hit.index)];
                source.envelopeTriggerMode = source.envelopeTriggerMode == PluginModulatorSource::EnvelopeTriggerMode::StepFxOnly
                                                 ? PluginModulatorSource::EnvelopeTriggerMode::NoteGate
                                                 : PluginModulatorSource::EnvelopeTriggerMode::StepFxOnly;
                selectPluginSource (hit.index);
                notifyPluginModulationChanged();
            }
            break;
        case PluginHitKind::RouteSource:
            if (routeInRange (hit.index) && ! pluginModulation.sources.empty())
            {
                auto& route = pluginModulation.routes[static_cast<size_t> (hit.index)];
                const int sourceCount = static_cast<int> (pluginModulation.sources.size());
                route.sourceIndex = (route.sourceIndex + direction + sourceCount) % sourceCount;
                selectedPluginRouteIndex = hit.index;
                selectPluginSource (route.sourceIndex);
                notifyPluginModulationChanged();
            }
            break;
        case PluginHitKind::RouteParam:
            if (routeInRange (hit.index) && ! pluginParameterInfos.empty())
            {
                auto& route = pluginModulation.routes[static_cast<size_t> (hit.index)];
                const int filteredCount = getFilteredPluginParameterCount();
                if (filteredCount <= 0)
                    break;

                int currentFiltered = direction > 0 ? -1 : filteredCount;
                for (int row = 0; row < filteredCount; ++row)
                {
                    const int paramInfoIndex = getPluginParameterInfoIndexAtFilteredRow (row);
                    if (paramInfoIndex >= 0
                        && pluginParameterInfos[static_cast<size_t> (paramInfoIndex)].index == route.parameterIndex)
                    {
                        currentFiltered = row;
                        break;
                    }
                }

                const int nextFiltered = (currentFiltered + direction + filteredCount) % filteredCount;
                const int paramInfoIndex = getPluginParameterInfoIndexAtFilteredRow (nextFiltered);
                if (paramInfoIndex >= 0 && paramInfoIndex < static_cast<int> (pluginParameterInfos.size()))
                {
                    const auto& param = pluginParameterInfos[static_cast<size_t> (paramInfoIndex)];
                    route.parameterIndex = param.index;
                    route.parameterName = param.name;
                    selectedPluginRouteIndex = hit.index;
                    notifyPluginModulationChanged();
                }
            }
            break;
        case PluginHitKind::LfoRateValue:
        case PluginHitKind::EnvAttack:
        case PluginHitKind::EnvDecay:
        case PluginHitKind::EnvSustain:
        case PluginHitKind::EnvRelease:
        case PluginHitKind::RouteAmount:
        {
            const double amount = large ? 0.20 : (fine ? 0.01 : 0.05);
            adjustPluginHitValue (hit, static_cast<double> (direction) * amount);
            break;
        }
        case PluginHitKind::None:
        case PluginHitKind::OpenEditor:
        case PluginHitKind::AddLfo:
        case PluginHitKind::AddEnvelope:
        case PluginHitKind::SourceSelect:
        case PluginHitKind::SourceEnable:
        case PluginHitKind::SourceRemove:
        case PluginHitKind::ParamSearch:
        case PluginHitKind::ParamAssign:
        case PluginHitKind::RouteSelect:
        case PluginHitKind::RouteEnable:
        case PluginHitKind::RouteRemove:
            break;
    }
}

bool SampleEditorComponent::handlePluginKeyboard (const juce::KeyPress& key)
{
    const int keyCode = key.getKeyCode();
    const bool shift = key.getModifiers().isShiftDown();
    const bool cmd = key.getModifiers().isCommandDown();

    if (cmd)
        return false;

    ensureFocusedPluginHitValid();

    if (keyCode == juce::KeyPress::tabKey)
    {
        movePluginKeyboardFocus (shift ? -1 : 1);
        return true;
    }

    if (key.getTextCharacter() == '/')
    {
        focusedPluginHit = { PluginHitKind::ParamSearch, -1, -1 };
        activateFocusedPluginHit();
        return true;
    }

    if (! shift && (key.getTextCharacter() == 'l' || key.getTextCharacter() == 'L'))
    {
        focusedPluginHit = { PluginHitKind::AddLfo, -1, -1 };
        activateFocusedPluginHit();
        return true;
    }

    if (! shift && (key.getTextCharacter() == 'e' || key.getTextCharacter() == 'E'))
    {
        focusedPluginHit = { PluginHitKind::AddEnvelope, -1, -1 };
        activateFocusedPluginHit();
        return true;
    }

    if (keyCode == juce::KeyPress::returnKey || keyCode == juce::KeyPress::spaceKey)
    {
        activateFocusedPluginHit();
        return true;
    }

    if (keyCode == juce::KeyPress::deleteKey || keyCode == juce::KeyPress::backspaceKey)
    {
        if (focusedPluginHit.kind == PluginHitKind::SourceSelect
            || focusedPluginHit.kind == PluginHitKind::SourceEnable
            || focusedPluginHit.kind == PluginHitKind::SourceRemove
            || focusedPluginHit.kind == PluginHitKind::LfoShape
            || focusedPluginHit.kind == PluginHitKind::LfoRateMode
            || focusedPluginHit.kind == PluginHitKind::LfoRateValue
            || focusedPluginHit.kind == PluginHitKind::EnvTrigger
            || focusedPluginHit.kind == PluginHitKind::EnvAttack
            || focusedPluginHit.kind == PluginHitKind::EnvDecay
            || focusedPluginHit.kind == PluginHitKind::EnvSustain
            || focusedPluginHit.kind == PluginHitKind::EnvRelease)
        {
            focusedPluginHit = { PluginHitKind::SourceRemove, focusedPluginHit.index, -1 };
            activateFocusedPluginHit();
            return true;
        }

        if (focusedPluginHit.kind == PluginHitKind::RouteSelect
            || focusedPluginHit.kind == PluginHitKind::RouteEnable
            || focusedPluginHit.kind == PluginHitKind::RouteRemove
            || focusedPluginHit.kind == PluginHitKind::RouteSource
            || focusedPluginHit.kind == PluginHitKind::RouteParam
            || focusedPluginHit.kind == PluginHitKind::RouteAmount)
        {
            focusedPluginHit = { PluginHitKind::RouteRemove, focusedPluginHit.index, -1 };
            activateFocusedPluginHit();
            return true;
        }

        return true;
    }

    if (keyCode == juce::KeyPress::pageUpKey || keyCode == juce::KeyPress::pageDownKey)
    {
        if (focusedPluginHit.kind == PluginHitKind::ParamAssign
            || focusedPluginHit.kind == PluginHitKind::ParamSearch)
        {
            const int rows = juce::jmax (1, getPluginParameterVisibleRows());
            movePluginKeyboardFocusToParameter (keyCode == juce::KeyPress::pageUpKey ? -rows : rows);
        }
        else if (pluginHitCanAdjustWithKeyboard (focusedPluginHit))
        {
            adjustFocusedPluginHit (keyCode == juce::KeyPress::pageUpKey ? 1 : -1, false, true);
        }
        else
        {
            movePluginKeyboardFocus (keyCode == juce::KeyPress::pageUpKey ? -1 : 1);
        }
        return true;
    }

    if (keyCode == juce::KeyPress::homeKey)
    {
        auto targets = getPluginKeyboardTargets();
        if (! targets.empty())
        {
            focusedPluginHit = targets.front().hit;
            repaint();
        }
        return true;
    }

    if (keyCode == juce::KeyPress::endKey)
    {
        auto targets = getPluginKeyboardTargets();
        if (! targets.empty())
        {
            focusedPluginHit = targets.back().hit;
            repaint();
        }
        return true;
    }

    if (keyCode == juce::KeyPress::upKey || keyCode == juce::KeyPress::downKey
        || keyCode == juce::KeyPress::leftKey || keyCode == juce::KeyPress::rightKey)
    {
        const bool forward = keyCode == juce::KeyPress::rightKey || keyCode == juce::KeyPress::downKey;
        const int direction = forward ? 1 : -1;

        if ((focusedPluginHit.kind == PluginHitKind::ParamAssign
             || focusedPluginHit.kind == PluginHitKind::ParamSearch)
            && (keyCode == juce::KeyPress::upKey || keyCode == juce::KeyPress::downKey))
        {
            movePluginKeyboardFocusToParameter (direction);
            return true;
        }

        if (pluginHitCanAdjustWithKeyboard (focusedPluginHit))
        {
            adjustFocusedPluginHit (direction, shift, false);
            return true;
        }

        movePluginKeyboardFocus (direction);
        return true;
    }

    return true;
}

juce::Rectangle<int> SampleEditorComponent::getPluginParameterSearchBoxBounds() const
{
    auto area = juce::Rectangle<int> (0, kHeaderHeight, getWidth(), getHeight() - kHeaderHeight).reduced (16, 12);
    area.removeFromTop (48);
    area.removeFromTop (8);

    juce::Rectangle<int> routeArea;
    if (area.getWidth() < 720)
    {
        area.removeFromTop (juce::jmin (300, area.getHeight() / 2));
        area.removeFromTop (12);
        routeArea = area;
    }
    else
    {
        const int sourceW = juce::jlimit (430, juce::jmax (430, area.getWidth() - 340),
                                          static_cast<int> (area.getWidth() * 0.58f));
        area.removeFromLeft (sourceW);
        area.removeFromLeft (14);
        routeArea = area;
    }

    routeArea.removeFromTop (30);
    routeArea.removeFromTop (10);
    const int routeRowH = 36;
    const int maxRouteRows = juce::jmax (2, juce::jmin (5, (routeArea.getHeight() - 150) / (routeRowH + 6)));
    routeArea.removeFromTop (juce::jmin (routeArea.getHeight(), maxRouteRows * (routeRowH + 6)));
    routeArea.removeFromTop (8);
    auto titleArea = routeArea.removeFromTop (24);
    return titleArea.removeFromRight (juce::jmin (150, titleArea.getWidth())).reduced (0, 2);
}

juce::Rectangle<int> SampleEditorComponent::getPluginParameterListBounds() const
{
    auto area = juce::Rectangle<int> (0, kHeaderHeight, getWidth(), getHeight() - kHeaderHeight).reduced (16, 12);
    area.removeFromTop (48);
    area.removeFromTop (8);

    juce::Rectangle<int> routeArea;
    if (area.getWidth() < 720)
    {
        area.removeFromTop (juce::jmin (300, area.getHeight() / 2));
        area.removeFromTop (12);
        routeArea = area;
    }
    else
    {
        const int sourceW = juce::jlimit (430, juce::jmax (430, area.getWidth() - 340),
                                          static_cast<int> (area.getWidth() * 0.58f));
        area.removeFromLeft (sourceW);
        area.removeFromLeft (14);
        routeArea = area;
    }

    routeArea.removeFromTop (30);
    routeArea.removeFromTop (10);
    const int routeRowH = 36;
    const int maxRouteRows = juce::jmax (2, juce::jmin (5, (routeArea.getHeight() - 150) / (routeRowH + 6)));
    routeArea.removeFromTop (juce::jmin (routeArea.getHeight(), maxRouteRows * (routeRowH + 6)));
    routeArea.removeFromTop (8);
    routeArea.removeFromTop (24);
    return routeArea;
}

juce::Rectangle<int> SampleEditorComponent::getPluginParameterListContentBounds() const
{
    auto bounds = getPluginParameterListBounds();
    if (getPluginParameterMaxScroll() > 0)
        bounds.removeFromRight (juce::jmin (bounds.getWidth(),
                                            kPluginParameterScrollbarWidth + kPluginParameterScrollbarGap));

    return bounds;
}

juce::Rectangle<int> SampleEditorComponent::getPluginParameterScrollbarBounds() const
{
    auto bounds = getPluginParameterListBounds();
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0)
        return {};

    return bounds.removeFromRight (juce::jmin (bounds.getWidth(), kPluginParameterScrollbarWidth));
}

int SampleEditorComponent::getPluginParameterVisibleRows() const
{
    const auto bounds = getPluginParameterListBounds();
    if (bounds.getHeight() <= 0)
        return 0;

    return juce::jmax (0, bounds.getHeight() / kPluginParameterRowHeight);
}

int SampleEditorComponent::getFilteredPluginParameterCount() const
{
    if (pluginParameterSearchBox.getText().trim().isEmpty())
        return static_cast<int> (pluginParameterInfos.size());

    int count = 0;
    for (const auto& param : pluginParameterInfos)
        if (pluginParameterMatchesSearch (param))
            ++count;

    return count;
}

int SampleEditorComponent::getPluginParameterInfoIndexAtFilteredRow (int filteredRow) const
{
    if (filteredRow < 0)
        return -1;

    if (pluginParameterSearchBox.getText().trim().isEmpty())
        return filteredRow < static_cast<int> (pluginParameterInfos.size()) ? filteredRow : -1;

    int matchingRow = 0;
    for (int i = 0; i < static_cast<int> (pluginParameterInfos.size()); ++i)
    {
        if (! pluginParameterMatchesSearch (pluginParameterInfos[static_cast<size_t> (i)]))
            continue;

        if (matchingRow == filteredRow)
            return i;

        ++matchingRow;
    }

    return -1;
}

bool SampleEditorComponent::pluginParameterMatchesSearch (const PluginInstrumentParameterInfo& param) const
{
    const auto query = pluginParameterSearchBox.getText().trim().toLowerCase();
    if (query.isEmpty())
        return true;

    const auto searchable = (param.name + " " + juce::String (param.index)).toLowerCase();
    return searchable.contains (query);
}

void SampleEditorComponent::updatePluginParameterSearchBoxBounds()
{
    pluginParameterSearchBox.setBounds (getPluginParameterSearchBoxBounds());
    pluginParameterSearchBox.toFront (false);
}

int SampleEditorComponent::getPluginParameterMaxScroll() const
{
    const int visibleRows = getPluginParameterVisibleRows();
    if (visibleRows <= 0)
        return 0;

    return juce::jmax (0, getFilteredPluginParameterCount() - visibleRows);
}

void SampleEditorComponent::scrollPluginParameterListBy (int rows)
{
    if (rows == 0)
        return;

    const int maxScroll = getPluginParameterMaxScroll();
    const int newScroll = juce::jlimit (0, maxScroll, pluginParameterScroll + rows);
    if (newScroll == pluginParameterScroll)
        return;

    pluginParameterScroll = newScroll;
    updatePluginParameterScrollbar();
    repaint (getPluginParameterListBounds().expanded (2));
}

void SampleEditorComponent::updatePluginParameterScrollbar()
{
    const int visibleRows = getPluginParameterVisibleRows();
    const int maxScroll = getPluginParameterMaxScroll();
    pluginParameterScroll = juce::jlimit (0, maxScroll, pluginParameterScroll);

    const auto scrollbarBounds = getPluginParameterScrollbarBounds();
    const bool shouldShowScrollbar = showingPlugin
                                     && currentInstrument >= 0
                                     && visibleRows > 0
                                     && maxScroll > 0
                                     && scrollbarBounds.getWidth() > 0
                                     && scrollbarBounds.getHeight() > 0;

    if (! shouldShowScrollbar)
    {
        pluginParameterScrollbar.setVisible (false);
        return;
    }

    pluginParameterScrollbar.setBounds (scrollbarBounds);
    pluginParameterScrollbar.setRangeLimits (0.0, static_cast<double> (getFilteredPluginParameterCount()));
    pluginParameterScrollbar.setCurrentRange (static_cast<double> (pluginParameterScroll),
                                              static_cast<double> (visibleRows),
                                              juce::dontSendNotification);
    pluginParameterScrollbar.setVisible (true);
    pluginParameterScrollbar.toFront (false);
}

void SampleEditorComponent::scrollBarMoved (juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart)
{
    if (scrollBarThatHasMoved != &pluginParameterScrollbar)
        return;

    const int newScroll = juce::jlimit (0, getPluginParameterMaxScroll(), juce::roundToInt (newRangeStart));
    if (newScroll == pluginParameterScroll)
        return;

    pluginParameterScroll = newScroll;
    pluginParameterWheelAccumulator = 0.0;
    updatePluginParameterScrollbar();
    repaint (getPluginParameterListBounds().expanded (2));
}

void SampleEditorComponent::handlePluginHit (const PluginHit& hit, const juce::MouseEvent& event)
{
    auto sourceInRange = [&] (int index)
    {
        return index >= 0 && index < static_cast<int> (pluginModulation.sources.size());
    };
    auto routeInRange = [&] (int index)
    {
        return index >= 0 && index < static_cast<int> (pluginModulation.routes.size());
    };
    auto beginDrag = [&]
    {
        pluginDragHit = hit;
        pluginDragStartY = event.position.y;
        pluginDragStartModulation = pluginModulation;
    };

    switch (hit.kind)
    {
        case PluginHitKind::OpenEditor:
            if (onOpenPluginEditorRequested)
                onOpenPluginEditorRequested (currentInstrument);
            break;
        case PluginHitKind::AddLfo:
            selectedPluginSourceIndex = pluginModulation.addLfo();
            notifyPluginModulationChanged();
            break;
        case PluginHitKind::AddEnvelope:
            selectedPluginSourceIndex = pluginModulation.addEnvelope();
            notifyPluginModulationChanged();
            break;
        case PluginHitKind::SourceSelect:
            selectPluginSource (hit.index);
            repaint();
            break;
        case PluginHitKind::SourceEnable:
            if (sourceInRange (hit.index))
            {
                auto& source = pluginModulation.sources[static_cast<size_t> (hit.index)];
                source.enabled = ! source.enabled;
                selectPluginSource (hit.index);
                notifyPluginModulationChanged();
            }
            break;
        case PluginHitKind::SourceRemove:
            if (sourceInRange (hit.index))
            {
                pluginModulation.removeSource (hit.index);
                selectedPluginSourceIndex = juce::jlimit (0, juce::jmax (0, static_cast<int> (pluginModulation.sources.size()) - 1),
                                                          selectedPluginSourceIndex);
                selectedPluginRouteIndex = pluginModulation.routes.empty()
                                               ? -1
                                               : juce::jlimit (0, static_cast<int> (pluginModulation.routes.size()) - 1,
                                                               selectedPluginRouteIndex);
                notifyPluginModulationChanged();
            }
            break;
        case PluginHitKind::LfoShape:
            if (sourceInRange (hit.index))
            {
                auto& source = pluginModulation.sources[static_cast<size_t> (hit.index)];
                source.lfoShape = static_cast<PluginModulatorSource::LfoShape> (
                    (static_cast<int> (source.lfoShape) + 1)
                    % (static_cast<int> (PluginModulatorSource::LfoShape::Random) + 1));
                selectPluginSource (hit.index);
                notifyPluginModulationChanged();
            }
            break;
        case PluginHitKind::LfoRateMode:
            if (sourceInRange (hit.index))
            {
                auto& source = pluginModulation.sources[static_cast<size_t> (hit.index)];
                source.lfoRateMode = source.lfoRateMode == PluginModulatorSource::LfoRateMode::Hz
                                         ? PluginModulatorSource::LfoRateMode::Steps
                                         : PluginModulatorSource::LfoRateMode::Hz;
                selectPluginSource (hit.index);
                notifyPluginModulationChanged();
            }
            break;
        case PluginHitKind::EnvTrigger:
            if (sourceInRange (hit.index))
            {
                auto& source = pluginModulation.sources[static_cast<size_t> (hit.index)];
                source.envelopeTriggerMode = source.envelopeTriggerMode == PluginModulatorSource::EnvelopeTriggerMode::StepFxOnly
                                                 ? PluginModulatorSource::EnvelopeTriggerMode::NoteGate
                                                 : PluginModulatorSource::EnvelopeTriggerMode::StepFxOnly;
                selectPluginSource (hit.index);
                notifyPluginModulationChanged();
            }
            break;
        case PluginHitKind::ParamSearch:
            pluginParameterSearchBox.grabKeyboardFocus();
            pluginParameterSearchBox.selectAll();
            break;
        case PluginHitKind::ParamAssign:
            addPluginRouteForParam (hit.parameterIndex);
            break;
        case PluginHitKind::RouteSelect:
            if (routeInRange (hit.index))
            {
                selectedPluginRouteIndex = hit.index;
                selectPluginSource (pluginModulation.routes[static_cast<size_t> (hit.index)].sourceIndex);
                repaint();
            }
            break;
        case PluginHitKind::RouteEnable:
            if (routeInRange (hit.index))
            {
                auto& route = pluginModulation.routes[static_cast<size_t> (hit.index)];
                route.enabled = ! route.enabled;
                selectedPluginRouteIndex = hit.index;
                selectPluginSource (route.sourceIndex);
                notifyPluginModulationChanged();
            }
            break;
        case PluginHitKind::RouteRemove:
            if (routeInRange (hit.index))
            {
                pluginModulation.removeRoute (hit.index);
                selectedPluginRouteIndex = pluginModulation.routes.empty()
                                               ? -1
                                               : juce::jlimit (0, static_cast<int> (pluginModulation.routes.size()) - 1,
                                                               selectedPluginRouteIndex);
                notifyPluginModulationChanged();
            }
            break;
        case PluginHitKind::RouteSource:
            showPluginRouteSourceMenu (hit.index);
            break;
        case PluginHitKind::RouteParam:
            showPluginRouteParamMenu (hit.index);
            break;
        case PluginHitKind::LfoRateValue:
        case PluginHitKind::EnvAttack:
        case PluginHitKind::EnvDecay:
        case PluginHitKind::EnvSustain:
        case PluginHitKind::EnvRelease:
        case PluginHitKind::RouteAmount:
            if (sourceInRange (hit.index) || routeInRange (hit.index))
                beginDrag();
            break;
        case PluginHitKind::None:
            break;
    }
}

void SampleEditorComponent::adjustPluginHitValue (const PluginHit& hit, double delta)
{
    auto sourceInRange = [&] (int index)
    {
        return index >= 0 && index < static_cast<int> (pluginModulation.sources.size());
    };
    auto routeInRange = [&] (int index)
    {
        return index >= 0 && index < static_cast<int> (pluginModulation.routes.size());
    };

    if (sourceInRange (hit.index))
        selectPluginSource (hit.index);

    switch (hit.kind)
    {
        case PluginHitKind::LfoRateValue:
            if (sourceInRange (hit.index))
            {
                auto& source = pluginModulation.sources[static_cast<size_t> (hit.index)];
                if (source.lfoRateMode == PluginModulatorSource::LfoRateMode::Hz)
                    source.lfoRateHz = juce::jlimit (0.01, 40.0, source.lfoRateHz + delta * 8.0);
                else
                    source.lfoRateSteps = juce::jlimit (1.0, 256.0, source.lfoRateSteps + delta * 64.0);
            }
            break;
        case PluginHitKind::EnvAttack:
            if (sourceInRange (hit.index))
                pluginModulation.sources[static_cast<size_t> (hit.index)].attackS =
                    juce::jlimit (0.0, 30.0, pluginModulation.sources[static_cast<size_t> (hit.index)].attackS + delta * 2.0);
            break;
        case PluginHitKind::EnvDecay:
            if (sourceInRange (hit.index))
                pluginModulation.sources[static_cast<size_t> (hit.index)].decayS =
                    juce::jlimit (0.0, 30.0, pluginModulation.sources[static_cast<size_t> (hit.index)].decayS + delta * 2.0);
            break;
        case PluginHitKind::EnvSustain:
            if (sourceInRange (hit.index))
                pluginModulation.sources[static_cast<size_t> (hit.index)].sustain =
                    juce::jlimit (0.0, 1.0, pluginModulation.sources[static_cast<size_t> (hit.index)].sustain + delta);
            break;
        case PluginHitKind::EnvRelease:
            if (sourceInRange (hit.index))
                pluginModulation.sources[static_cast<size_t> (hit.index)].releaseS =
                    juce::jlimit (0.0, 30.0, pluginModulation.sources[static_cast<size_t> (hit.index)].releaseS + delta * 2.0);
            break;
        case PluginHitKind::RouteAmount:
            if (routeInRange (hit.index))
            {
                pluginModulation.routes[static_cast<size_t> (hit.index)].amount =
                    juce::jlimit (-1.0f, 1.0f,
                                  pluginModulation.routes[static_cast<size_t> (hit.index)].amount
                                      + static_cast<float> (delta * 1.5));
                selectedPluginRouteIndex = hit.index;
            }
            break;
        case PluginHitKind::None:
        case PluginHitKind::OpenEditor:
        case PluginHitKind::AddLfo:
        case PluginHitKind::AddEnvelope:
        case PluginHitKind::SourceSelect:
        case PluginHitKind::SourceEnable:
        case PluginHitKind::SourceRemove:
        case PluginHitKind::LfoShape:
        case PluginHitKind::LfoRateMode:
        case PluginHitKind::EnvTrigger:
        case PluginHitKind::ParamSearch:
        case PluginHitKind::ParamAssign:
        case PluginHitKind::RouteSelect:
        case PluginHitKind::RouteEnable:
        case PluginHitKind::RouteRemove:
        case PluginHitKind::RouteSource:
        case PluginHitKind::RouteParam:
            return;
    }

    notifyPluginModulationChanged();
}

void SampleEditorComponent::notifyPluginModulationChanged()
{
    if (onPluginModulationChanged)
        onPluginModulationChanged (currentInstrument, pluginModulation);
    ensureFocusedPluginHitValid();
    updatePluginParameterScrollbar();
    repaint();
}

void SampleEditorComponent::selectPluginSource (int sourceIndex)
{
    selectedPluginSourceIndex = juce::jlimit (0, juce::jmax (0, static_cast<int> (pluginModulation.sources.size()) - 1),
                                              sourceIndex);
}

void SampleEditorComponent::addPluginRouteForParam (int parameterIndex)
{
    if (parameterIndex < 0)
        return;

    if (pluginModulation.sources.empty())
        selectedPluginSourceIndex = pluginModulation.addLfo();

    selectPluginSource (selectedPluginSourceIndex);

    for (int i = 0; i < static_cast<int> (pluginModulation.routes.size()); ++i)
    {
        const auto& route = pluginModulation.routes[static_cast<size_t> (i)];
        if (route.sourceIndex == selectedPluginSourceIndex && route.parameterIndex == parameterIndex)
        {
            selectedPluginRouteIndex = i;
            repaint();
            return;
        }
    }

    PluginModulationRoute route;
    route.sourceIndex = selectedPluginSourceIndex;
    route.parameterIndex = parameterIndex;
    route.amount = 0.25f;

    for (const auto& param : pluginParameterInfos)
    {
        if (param.index == parameterIndex)
        {
            route.parameterName = param.name;
            break;
        }
    }

    pluginModulation.routes.push_back (route);
    selectedPluginRouteIndex = static_cast<int> (pluginModulation.routes.size()) - 1;
    notifyPluginModulationChanged();
}

void SampleEditorComponent::showPluginRouteSourceMenu (int routeIndex)
{
    if (routeIndex < 0 || routeIndex >= static_cast<int> (pluginModulation.routes.size()))
        return;

    juce::PopupMenu menu;
    for (int i = 0; i < static_cast<int> (pluginModulation.sources.size()); ++i)
        menu.addItem (i + 1, getPluginSourceDisplayName (pluginModulation.sources[static_cast<size_t> (i)], i),
                      true, i == pluginModulation.routes[static_cast<size_t> (routeIndex)].sourceIndex);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [this, routeIndex] (int result)
                        {
                            if (result <= 0 || routeIndex >= static_cast<int> (pluginModulation.routes.size()))
                                return;
                            pluginModulation.routes[static_cast<size_t> (routeIndex)].sourceIndex = result - 1;
                            selectPluginSource (result - 1);
                            selectedPluginRouteIndex = routeIndex;
                            notifyPluginModulationChanged();
                        });
}

void SampleEditorComponent::showPluginRouteParamMenu (int routeIndex)
{
    if (routeIndex < 0 || routeIndex >= static_cast<int> (pluginModulation.routes.size()))
        return;

    juce::PopupMenu menu;
    std::vector<int> menuParameterInfoIndices;
    menuParameterInfoIndices.reserve (static_cast<size_t> (getFilteredPluginParameterCount()));

    for (int i = 0; i < static_cast<int> (pluginParameterInfos.size()); ++i)
    {
        if (! pluginParameterMatchesSearch (pluginParameterInfos[static_cast<size_t> (i)]))
            continue;

        menuParameterInfoIndices.push_back (i);
        menu.addItem (static_cast<int> (menuParameterInfoIndices.size()),
                      pluginParameterInfos[static_cast<size_t> (i)].name,
                      true,
                      pluginParameterInfos[static_cast<size_t> (i)].index
                                == pluginModulation.routes[static_cast<size_t> (routeIndex)].parameterIndex);
    }

    if (menuParameterInfoIndices.empty())
        menu.addItem (1, pluginParameterSearchBox.getText().isNotEmpty() ? "No matching params" : "No params", false, false);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [this, routeIndex, menuParameterInfoIndices] (int result)
                        {
                            if (result <= 0
                                || routeIndex >= static_cast<int> (pluginModulation.routes.size())
                                || result - 1 >= static_cast<int> (menuParameterInfoIndices.size()))
                                return;

                            const int paramInfoIndex = menuParameterInfoIndices[static_cast<size_t> (result - 1)];
                            if (paramInfoIndex < 0 || paramInfoIndex >= static_cast<int> (pluginParameterInfos.size()))
                                return;

                            const auto& param = pluginParameterInfos[static_cast<size_t> (paramInfoIndex)];
                            auto& route = pluginModulation.routes[static_cast<size_t> (routeIndex)];
                            route.parameterIndex = param.index;
                            route.parameterName = param.name;
                            selectedPluginRouteIndex = routeIndex;
                            notifyPluginModulationChanged();
                        });
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
    if (showingPlugin)
    {
        title = "Plugin Instrument";
    }
    else if (displayMode == DisplayMode::InstrumentEdit)
    {
        if (editSubTab == EditSubTab::Parameters)
            title = "Instrument Parameters";
        else if (editSubTab == EditSubTab::MidiOut)
            title = "MIDI Out Assignments";
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
// Drawing: Parameters page (merged General + Effects = 12 columns)
//==============================================================================

void SampleEditorComponent::drawParametersPage (juce::Graphics& g, juce::Rectangle<int> area)
{
    int numCols = 12;
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

    // Col 9: Sample-rate reduction
    drawBarMeter (g, colRect (9), lofiSampleRateToNorm (currentParams.lofiSampleRateHz),
                  parametersColumn == 9, amberCol);

    // Col 10: Reverb Send (-100..0 dB)
    float rev01 = static_cast<float> ((currentParams.reverbSend + 100.0) / 100.0);
    drawBarMeter (g, colRect (10), rev01, parametersColumn == 10, blueCol);

    // Col 11: Delay Send (-100..0 dB)
    float dly01 = static_cast<float> ((currentParams.delaySend + 100.0) / 100.0);
    drawBarMeter (g, colRect (11), dly01, parametersColumn == 11, blueCol);
}

//==============================================================================
// Drawing: MIDI Out assignment page
//==============================================================================

void SampleEditorComponent::drawMidiOutPage (juce::Graphics& g, juce::Rectangle<int> area)
{
    const int numCols = InstrumentParams::kNumMidiOutLanes * 2;
    const int colW = area.getWidth() / numCols;
    auto midiCol = lookAndFeel.findColour (TrackerLookAndFeel::fxColourId);
    auto mutedCol = lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.45f);

    auto colRect = [&] (int c) -> juce::Rectangle<int>
    {
        const int w = (c < numCols - 1) ? colW : (area.getWidth() - c * colW);
        return { area.getX() + c * colW, area.getY(), w, area.getHeight() };
    };

    const juce::StringArray typeItems = { "CC", "PC", "ChanPr", "PolyPr" };

    for (int lane = 0; lane < InstrumentParams::kNumMidiOutLanes; ++lane)
    {
        const auto& assignment = currentParams.midiOutAssignments[static_cast<size_t> (lane)];
        const int typeCol = lane * 2;
        const int numberCol = typeCol + 1;
        drawListColumn (g,
                        colRect (typeCol),
                        typeItems,
                        static_cast<int> (assignment.type),
                        midiOutColumn == typeCol,
                        midiCol);

        const auto numberColour = midiOutAssignmentUsesNumber (assignment.type) ? midiCol : mutedCol;
        drawBarMeter (g,
                      colRect (numberCol),
                      midiOutAssignmentUsesNumber (assignment.type)
                          ? static_cast<float> (juce::jlimit (0, 127, assignment.number)) / 127.0f
                          : 0.0f,
                      midiOutColumn == numberCol,
                      numberColour);
    }
}

//==============================================================================
// Drawing: Modulation page
//==============================================================================

void SampleEditorComponent::drawModulationPage (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto& mod = currentParams.modulations[static_cast<size_t> (modDestIndex)];
    int numCols = 8;
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

    // Col 2: Mode list
    juce::StringArray modeItems = { "Per-Note", "Global" };
    int modeIdx = static_cast<int> (mod.modMode);
    drawListColumn (g, { area.getX() + 2 * colW, area.getY(), colW, area.getHeight() },
                    modeItems, modeIdx, modColumn == 2, orangeCol);

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
        // Col 3: Shape list
        juce::StringArray shapeItems = { "Rev Saw", "Saw", "Triangle", "Square", "Random" };
        int shapeIdx = static_cast<int> (mod.lfoShape);
        drawListColumn (g, { area.getX() + 3 * colW, area.getY(), colW, area.getHeight() },
                        shapeItems, shapeIdx, modColumn == 3, orangeCol);

        // Col 4: Speed (list of presets in Steps mode, bar meter in MS mode)
        if (mod.lfoSpeedMode == InstrumentParams::Modulation::LFOSpeedMode::MS)
        {
            float ms01 = static_cast<float> (mod.lfoSpeedMs) / 5000.0f;
            drawBarMeter (g, { area.getX() + 4 * colW, area.getY(), colW, area.getHeight() },
                          ms01, modColumn == 4, orangeCol);
        }
        else
        {
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
            drawListColumn (g, { area.getX() + 4 * colW, area.getY(), colW, area.getHeight() },
                            speedItems, speedSelectedIdx, modColumn == 4, orangeCol);
        }

        // Col 5: Amount bar
        float amt01 = static_cast<float> (mod.amount) / 100.0f;
        drawBarMeter (g, { area.getX() + 5 * colW, area.getY(), colW, area.getHeight() },
                      amt01, modColumn == 5, orangeCol);

        // Col 6: Speed Mode toggle list
        juce::StringArray speedModeItems = { "Steps", "MS" };
        int speedModeIdx = static_cast<int> (mod.lfoSpeedMode);
        drawListColumn (g, { area.getX() + 6 * colW, area.getY(), colW, area.getHeight() },
                        speedModeItems, speedModeIdx, modColumn == 6, orangeCol);

        // Col 7: Empty
        drawEmptyCol (7);
    }
    else if (mod.type == InstrumentParams::Modulation::Type::Envelope)
    {
        // Col 3: Attack bar
        float atk01 = static_cast<float> (mod.attackS / 10.0);
        drawBarMeter (g, { area.getX() + 3 * colW, area.getY(), colW, area.getHeight() },
                      atk01, modColumn == 3, orangeCol);

        // Col 4: Decay bar
        float dec01 = static_cast<float> (mod.decayS / 10.0);
        drawBarMeter (g, { area.getX() + 4 * colW, area.getY(), colW, area.getHeight() },
                      dec01, modColumn == 4, orangeCol);

        // Col 5: Sustain bar
        float sus01 = static_cast<float> (mod.sustain) / 100.0f;
        drawBarMeter (g, { area.getX() + 5 * colW, area.getY(), colW, area.getHeight() },
                      sus01, modColumn == 5, orangeCol);

        // Col 6: Release bar
        float rel01 = static_cast<float> (mod.releaseS / 10.0);
        drawBarMeter (g, { area.getX() + 6 * colW, area.getY(), colW, area.getHeight() },
                      rel01, modColumn == 6, orangeCol);

        // Col 7: Amount bar
        float amt01 = static_cast<float> (mod.amount) / 100.0f;
        int lastColW = area.getWidth() - 7 * colW;
        drawBarMeter (g, { area.getX() + 7 * colW, area.getY(), lastColW, area.getHeight() },
                      amt01, modColumn == 7, orangeCol);
    }
    else // Off
    {
        for (int c = 3; c < numCols; ++c)
            drawEmptyCol (c);
    }
}

//==============================================================================
// Drawing: Playback page
//==============================================================================

void SampleEditorComponent::drawPlaybackPage (juce::Graphics& g, juce::Rectangle<int> area)
{
    // Position and show the waveformView child component.
    // It renders the waveform, markers, and overview bar.
    waveformView.setBounds (area);
    waveformView.setVisible (true);
    syncWaveformView();

    // The play mode list overlay is drawn in paintOverChildren()
    // so it appears on top of the waveformView child.
    (void) g;
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
                    int v = (static_cast<int> (currentParams.filterType) + direction + 4) % 4;
                    setFilterTypeWithDefaultCutoff (static_cast<InstrumentParams::FilterType> (v));
                    break;
                }
                case 5: // Cutoff
                {
                    int step = fine ? 1 : (large ? 10 : 5);
                    currentParams.cutoff = juce::jlimit (0, 100,
                        currentParams.cutoff + direction * step);
                    break;
                }
                case 6: // Resonance
                {
                    int step = fine ? 1 : (large ? 10 : 5);
                    currentParams.resonance = juce::jlimit (0, 100,
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
                case 9: // Sample-rate reduction
                {
                    const double step = fine ? 100.0 : (large ? 4000.0 : 1000.0);
                    const double currentHz = currentParams.lofiSampleRateHz <= 0.0
                        ? InstrumentParams::kMaxLofiSampleRateHz
                        : clampLofiSampleRateHz (currentParams.lofiSampleRateHz);
                    const double nextHz = currentHz + static_cast<double> (direction) * step;
                    currentParams.lofiSampleRateHz = nextHz >= InstrumentParams::kMaxLofiSampleRateHz
                        ? 0.0
                        : juce::jlimit (InstrumentParams::kMinLofiSampleRateHz,
                                        InstrumentParams::kMaxLofiSampleRateHz,
                                        nextHz);
                    break;
                }
                case 10: // Reverb Send
                {
                    double step = fine ? 0.5 : (large ? 12.0 : 1.0);
                    currentParams.reverbSend = juce::jlimit (-100.0, 0.0,
                        currentParams.reverbSend + direction * step);
                    break;
                }
                case 11: // Delay Send
                {
                    double step = fine ? 0.5 : (large ? 12.0 : 1.0);
                    currentParams.delaySend = juce::jlimit (-100.0, 0.0,
                        currentParams.delaySend + direction * step);
                    break;
                }
            }
        }
        else if (editSubTab == EditSubTab::MidiOut)
        {
            const int lane = getMidiOutLaneForColumn (midiOutColumn);
            auto& assignment = currentParams.midiOutAssignments[static_cast<size_t> (lane)];

            if (isMidiOutTypeColumn (midiOutColumn))
            {
                int v = static_cast<int> (assignment.type);
                v = (v + direction + 4) % 4;
                assignment.type = static_cast<InstrumentParams::MidiOutMessageType> (v);
            }
            else if (midiOutAssignmentUsesNumber (assignment.type))
            {
                const int step = fine ? 1 : (large ? 12 : 5);
                assignment.number = juce::jlimit (0, 127, assignment.number + direction * step);
            }
        }
        else // Modulation
        {
            auto& mod = currentParams.modulations[static_cast<size_t> (modDestIndex)];

            switch (modColumn)
            {
                case 0: // Destination
                    modDestIndex = (modDestIndex + direction + InstrumentParams::kNumModDests)
                                   % InstrumentParams::kNumModDests;
                    break;

                case 1: // Type
                {
                    auto oldType = mod.type;
                    int v = static_cast<int> (mod.type);
                    v = (v + direction + 3) % 3;
                    mod.type = static_cast<InstrumentParams::Modulation::Type> (v);
                    if (mod.type != oldType)
                        mod.amount = 0;
                    break;
                }

                case 2: // Mode (Per-Note / Global)
                {
                    int v = static_cast<int> (mod.modMode);
                    v = (v + direction + 2) % 2;
                    mod.modMode = static_cast<InstrumentParams::Modulation::ModMode> (v);
                    break;
                }

                case 3: // Shape (LFO) or Attack (Envelope)
                {
                    if (mod.type == InstrumentParams::Modulation::Type::LFO)
                    {
                        int v = static_cast<int> (mod.lfoShape);
                        v = (v + direction + 5) % 5;
                        mod.lfoShape = static_cast<InstrumentParams::Modulation::LFOShape> (v);
                    }
                    else if (mod.type == InstrumentParams::Modulation::Type::Envelope)
                    {
                        double step = fine ? 0.001 : (large ? 0.5 : 0.01);
                        mod.attackS = juce::jlimit (0.0, 10.0, mod.attackS + direction * step);
                    }
                    break;
                }

                case 4: // Speed (LFO) or Decay (Envelope)
                {
                    if (mod.type == InstrumentParams::Modulation::Type::LFO)
                    {
                        if (mod.lfoSpeedMode == InstrumentParams::Modulation::LFOSpeedMode::MS)
                        {
                            int step = fine ? 1 : (large ? 100 : 10);
                            mod.lfoSpeedMs = juce::jlimit (1, 5000,
                                mod.lfoSpeedMs + direction * step);
                        }
                        else
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
                            curIdx = juce::jlimit (0, kNumLfoSpeeds - 1, curIdx + direction);
                            mod.lfoSpeed = kLfoSpeeds[curIdx];
                        }
                    }
                    else if (mod.type == InstrumentParams::Modulation::Type::Envelope)
                    {
                        double step = fine ? 0.001 : (large ? 0.5 : 0.01);
                        mod.decayS = juce::jlimit (0.0, 10.0, mod.decayS + direction * step);
                    }
                    break;
                }

                case 5: // Amount (LFO) or Sustain (Envelope)
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

                case 6: // Speed Mode (LFO) or Release (Envelope)
                {
                    if (mod.type == InstrumentParams::Modulation::Type::LFO)
                    {
                        int v = static_cast<int> (mod.lfoSpeedMode);
                        v = (v + direction + 2) % 2;
                        mod.lfoSpeedMode = static_cast<InstrumentParams::Modulation::LFOSpeedMode> (v);
                    }
                    else if (mod.type == InstrumentParams::Modulation::Type::Envelope)
                    {
                        double step = fine ? 0.001 : (large ? 0.5 : 0.01);
                        mod.releaseS = juce::jlimit (0.0, 10.0, mod.releaseS + direction * step);
                    }
                    break;
                }

                case 7: // Amount (Envelope only)
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
            v = (v + direction + 7) % 7;
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
                        currentParams.loopStart = juce::jlimit (currentParams.startPos, currentParams.loopEnd,
                            currentParams.loopStart + direction * step);
                        break;
                    case 2: // Loop End
                        currentParams.loopEnd = juce::jlimit (currentParams.loopStart, currentParams.endPos,
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
                    case 2: // Num Slices (BeatSlice: regenerate equal slices)
                    {
                        if (mode == InstrumentParams::PlayMode::BeatSlice)
                        {
                            int numSlices = SamplePlaybackLayout::getBeatSliceRegionCount (currentParams) + direction;
                            numSlices = juce::jlimit (1, 48, numSlices);
                            generateEqualSlices (numSlices);
                        }
                        // For Slice mode, Slices column is read-only (shows count)
                        break;
                    }
                    case 3: // Selected slice
                    {
                        int numSlices = (mode == InstrumentParams::PlayMode::BeatSlice)
                            ? SamplePlaybackLayout::getBeatSliceRegionCount (currentParams)
                            : SamplePlaybackLayout::getSliceRegionCount (currentParams);
                        if (numSlices > 0)
                        {
                            currentParams.selectedSlice = juce::jlimit (0, numSlices - 1,
                                currentParams.selectedSlice + direction);
                            selectedSliceIndex = currentParams.selectedSlice;
                        }
                        break;
                    }
                    case 4: // Auto-Slice sensitivity
                    {
                        double step = fine ? 0.01 : 0.05;
                        autoSliceSensitivity = juce::jlimit (0.0, 1.0,
                            autoSliceSensitivity + direction * step);
                        autoSlice();
                        break;
                    }
                    case 5: // Equal Chop count
                    {
                        int current = SamplePlaybackLayout::getSliceRegionCount (currentParams);
                        if (current < 2) current = 8;
                        int step = fine ? 1 : (large ? 8 : 1);
                        int numSlices = juce::jlimit (1, 48, current + direction * step);
                        generateEqualSlices (numSlices);
                        break;
                    }
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
                        if (currentParams.granularLengthMode == InstrumentParams::GranLengthMode::Steps)
                        {
                            double step = large ? 4.0 : 0.5;
                            if (fine) step = 0.5;
                            currentParams.granularLengthSteps = SamplePlaybackLayout::snapGranularLengthSteps (
                                currentParams.granularLengthSteps + direction * step);
                        }
                        else
                        {
                            int step = fine ? 1 : (large ? 50 : 10);
                            currentParams.granularLength = juce::jlimit (1, 1000,
                                currentParams.granularLength + direction * step);
                        }
                        break;
                    }
                    case 4: // Length Mode
                    {
                        int v = static_cast<int> (currentParams.granularLengthMode);
                        v = (v + direction + 2) % 2;
                        currentParams.granularLengthMode = static_cast<InstrumentParams::GranLengthMode> (v);
                        currentParams.granularLengthSteps = SamplePlaybackLayout::snapGranularLengthSteps (
                            currentParams.granularLengthSteps);
                        break;
                    }
                    case 5: // Shape
                    {
                        int v = static_cast<int> (currentParams.granularShape);
                        v = (v + direction + 3) % 3;
                        currentParams.granularShape = static_cast<InstrumentParams::GranShape> (v);
                        break;
                    }
                    case 6: // Grain Loop
                    {
                        int v = static_cast<int> (currentParams.granularLoop);
                        v = (v + direction + 3) % 3;
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
                    setFilterTypeWithDefaultCutoff (static_cast<InstrumentParams::FilterType> (
                        juce::jlimit (0, 3, v)));
                    break;
                }
                case 5: // Cutoff 0-100
                    currentParams.cutoff = juce::jlimit (0, 100,
                        currentParams.cutoff + juce::roundToInt (normDelta * 100.0));
                    break;
                case 6: // Resonance 0-100
                    currentParams.resonance = juce::jlimit (0, 100,
                        currentParams.resonance + juce::roundToInt (normDelta * 100.0));
                    break;
                case 7: // Overdrive 0-100
                    currentParams.overdrive = juce::jlimit (0, 100,
                        currentParams.overdrive + juce::roundToInt (normDelta * 100.0));
                    break;
                case 8: // Bit Depth 4-16
                    currentParams.bitDepth = juce::jlimit (4, 16,
                        currentParams.bitDepth + juce::roundToInt (normDelta * 12.0));
                    break;
                case 9: // Sample-rate reduction
                {
                    const double currentHz = currentParams.lofiSampleRateHz <= 0.0
                        ? InstrumentParams::kMaxLofiSampleRateHz
                        : clampLofiSampleRateHz (currentParams.lofiSampleRateHz);
                    const double nextHz = currentHz
                        + normDelta * (InstrumentParams::kMaxLofiSampleRateHz
                                     - InstrumentParams::kMinLofiSampleRateHz);
                    currentParams.lofiSampleRateHz = nextHz >= InstrumentParams::kMaxLofiSampleRateHz
                        ? 0.0
                        : juce::jlimit (InstrumentParams::kMinLofiSampleRateHz,
                                        InstrumentParams::kMaxLofiSampleRateHz,
                                        nextHz);
                    break;
                }
                case 10: // Reverb Send -100..0 dB
                    currentParams.reverbSend = juce::jlimit (-100.0, 0.0,
                        currentParams.reverbSend + normDelta * 100.0);
                    break;
                case 11: // Delay Send -100..0 dB
                    currentParams.delaySend = juce::jlimit (-100.0, 0.0,
                        currentParams.delaySend + normDelta * 100.0);
                    break;
            }
        }
        else if (editSubTab == EditSubTab::MidiOut)
        {
            const int lane = getMidiOutLaneForColumn (midiOutColumn);
            auto& assignment = currentParams.midiOutAssignments[static_cast<size_t> (lane)];

            if (isMidiOutTypeColumn (midiOutColumn))
            {
                int v = static_cast<int> (assignment.type)
                        - juce::roundToInt (normDelta * 4.0);
                assignment.type = static_cast<InstrumentParams::MidiOutMessageType> (juce::jlimit (0, 3, v));
            }
            else if (midiOutAssignmentUsesNumber (assignment.type))
            {
                assignment.number = juce::jlimit (0, 127,
                    assignment.number + juce::roundToInt (normDelta * 127.0));
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
                    auto oldType = mod.type;
                    int v = static_cast<int> (mod.type)
                            - juce::roundToInt (normDelta * 3.0);
                    mod.type = static_cast<InstrumentParams::Modulation::Type> (
                        juce::jlimit (0, 2, v));
                    if (mod.type != oldType)
                        mod.amount = 0;
                    break;
                }
                case 2: // Mode (2 items, list)
                {
                    int v = static_cast<int> (mod.modMode)
                            - juce::roundToInt (normDelta * 2.0);
                    mod.modMode = static_cast<InstrumentParams::Modulation::ModMode> (
                        juce::jlimit (0, 1, v));
                    break;
                }
                case 3: // Shape (LFO list) or Attack (Env bar 0-10)
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
                case 4: // Speed (LFO list/bar) or Decay (Env bar 0-10)
                {
                    if (mod.type == InstrumentParams::Modulation::Type::LFO)
                    {
                        if (mod.lfoSpeedMode == InstrumentParams::Modulation::LFOSpeedMode::MS)
                        {
                            mod.lfoSpeedMs = juce::jlimit (1, 5000,
                                mod.lfoSpeedMs + juce::roundToInt (normDelta * 5000.0));
                        }
                        else
                        {
                            int curIdx = 0;
                            for (int i = 0; i < kNumLfoSpeeds; ++i)
                                if (kLfoSpeeds[i] == mod.lfoSpeed) curIdx = i;
                            int newIdx = curIdx - juce::roundToInt (
                                normDelta * static_cast<double> (kNumLfoSpeeds));
                            mod.lfoSpeed = kLfoSpeeds[juce::jlimit (0, kNumLfoSpeeds - 1, newIdx)];
                        }
                    }
                    else if (mod.type == InstrumentParams::Modulation::Type::Envelope)
                        mod.decayS = juce::jlimit (0.0, 10.0, mod.decayS + normDelta * 10.0);
                    break;
                }
                case 5: // Amount (LFO 0-100) or Sustain (Env 0-100)
                {
                    if (mod.type == InstrumentParams::Modulation::Type::LFO)
                        mod.amount = juce::jlimit (0, 100,
                            mod.amount + juce::roundToInt (normDelta * 100.0));
                    else if (mod.type == InstrumentParams::Modulation::Type::Envelope)
                        mod.sustain = juce::jlimit (0, 100,
                            mod.sustain + juce::roundToInt (normDelta * 100.0));
                    break;
                }
                case 6: // Speed Mode (LFO list) or Release (Env 0-10)
                    if (mod.type == InstrumentParams::Modulation::Type::LFO)
                    {
                        int v = static_cast<int> (mod.lfoSpeedMode)
                                - juce::roundToInt (normDelta * 2.0);
                        mod.lfoSpeedMode = static_cast<InstrumentParams::Modulation::LFOSpeedMode> (
                            juce::jlimit (0, 1, v));
                    }
                    else if (mod.type == InstrumentParams::Modulation::Type::Envelope)
                        mod.releaseS = juce::jlimit (0.0, 10.0, mod.releaseS + normDelta * 10.0);
                    break;
                case 7: // Amount (Env 0-100)
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
                        currentParams.loopStart = juce::jlimit (currentParams.startPos, currentParams.loopEnd,
                            currentParams.loopStart + normDelta);
                        break;
                    case 2:
                        currentParams.loopEnd = juce::jlimit (currentParams.loopStart, currentParams.endPos,
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
                    case 2: // Num Slices (BeatSlice: regenerate)
                    {
                        if (mode == InstrumentParams::PlayMode::BeatSlice)
                        {
                            int numSlices = SamplePlaybackLayout::getBeatSliceRegionCount (currentParams)
                                            + juce::roundToInt (normDelta * 32.0);
                            numSlices = juce::jlimit (1, 48, numSlices);
                            generateEqualSlices (numSlices);
                        }
                        break;
                    }
                    case 3: // Selected slice
                    {
                        int numSlices = (mode == InstrumentParams::PlayMode::BeatSlice)
                            ? SamplePlaybackLayout::getBeatSliceRegionCount (currentParams)
                            : SamplePlaybackLayout::getSliceRegionCount (currentParams);
                        if (numSlices > 0)
                        {
                            int idx = currentParams.selectedSlice
                                      - juce::roundToInt (normDelta * static_cast<double> (numSlices));
                            currentParams.selectedSlice = juce::jlimit (0, numSlices - 1, idx);
                            selectedSliceIndex = currentParams.selectedSlice;
                        }
                        break;
                    }
                    case 4: // Auto-Slice sensitivity
                    {
                        autoSliceSensitivity = juce::jlimit (0.0, 1.0,
                            autoSliceSensitivity + normDelta);
                        autoSlice();
                        break;
                    }
                    case 5: // Equal Chop count
                    {
                        int current = SamplePlaybackLayout::getSliceRegionCount (currentParams);
                        if (current < 2) current = 8;
                        int numSlices = current + juce::roundToInt (normDelta * 32.0);
                        numSlices = juce::jlimit (1, 48, numSlices);
                        generateEqualSlices (numSlices);
                        break;
                    }
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
                        if (currentParams.granularLengthMode == InstrumentParams::GranLengthMode::Steps)
                        {
                            currentParams.granularLengthSteps = SamplePlaybackLayout::snapGranularLengthSteps (
                                currentParams.granularLengthSteps + normDelta * 64.0);
                        }
                        else
                        {
                            currentParams.granularLength = juce::jlimit (1, 1000,
                                currentParams.granularLength + juce::roundToInt (normDelta * 999.0));
                        }
                        break;
                    case 4: // Length Mode (list)
                    {
                        int v = static_cast<int> (currentParams.granularLengthMode)
                                - juce::roundToInt (normDelta * 2.0);
                        currentParams.granularLengthMode = static_cast<InstrumentParams::GranLengthMode> (
                            juce::jlimit (0, 1, v));
                        break;
                    }
                    case 5: // Shape (list)
                    {
                        int v = static_cast<int> (currentParams.granularShape)
                                - juce::roundToInt (normDelta * 3.0);
                        currentParams.granularShape = static_cast<InstrumentParams::GranShape> (
                            juce::jlimit (0, 2, v));
                        break;
                    }
                    case 6: // Loop (list)
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
        if (editSubTab == EditSubTab::MidiOut)
            return isMidiOutTypeColumn (midiOutColumn);

        // Modulation
        if (modColumn <= 2) return true; // Destination, Type, Mode are always lists
        auto& mod = currentParams.modulations[static_cast<size_t> (modDestIndex)];
        if (mod.type == InstrumentParams::Modulation::Type::LFO)
        {
            if (modColumn == 3) return true; // Shape list
            if (modColumn == 4)
                return mod.lfoSpeedMode != InstrumentParams::Modulation::LFOSpeedMode::MS; // Steps=list, MS=bar
            if (modColumn == 6) return true; // Speed Mode list
            return false;
        }
        if (mod.type == InstrumentParams::Modulation::Type::Off)
            return true; // Empty columns
        return false;
    }
    else // InstrumentType
    {
        int numCols = getColumnCount();
        if (playbackColumn == numCols - 1) return true; // Play Mode list
        auto mode = currentParams.playMode;
        if ((mode == InstrumentParams::PlayMode::Slice || mode == InstrumentParams::PlayMode::BeatSlice)
            && playbackColumn >= 2)
            return true; // Slices count, Selected slice
        if (mode == InstrumentParams::PlayMode::Granular && playbackColumn >= 4)
            return true; // Length mode, Shape, Loop
        return false;
    }
}

//==============================================================================
// Keyboard
//==============================================================================

int SampleEditorComponent::keyToNote (const juce::KeyPress& key) const
{
    return NoteUtils::keyToNote (key, currentOctave);
}

bool SampleEditorComponent::keyPressed (const juce::KeyPress& key)
{
    if (currentInstrument < 0) return false;

    // Plugin instrument mode: route keyboard input through the inline modulation matrix.
    if (showingPlugin)
        return handlePluginKeyboard (key);

    auto keyCode = key.getKeyCode();
    bool shift = key.getModifiers().isShiftDown();
    bool cmd   = key.getModifiers().isCommandDown();

    // Cmd+E: Equal chop (in Slice/BeatSlice mode on playback page)
    if (cmd && (keyCode == 'E' || keyCode == 'e'))
    {
        if (displayMode == DisplayMode::InstrumentType
            && (currentParams.playMode == InstrumentParams::PlayMode::Slice
                || currentParams.playMode == InstrumentParams::PlayMode::BeatSlice))
        {
            int numSlices = SamplePlaybackLayout::getSliceRegionCount (currentParams);
            if (numSlices < 2) numSlices = 8; // default to 8 slices
            generateEqualSlices (numSlices);
            notifyParamsChanged();
            return true;
        }
    }

    // Cmd+T: Auto-slice (transient detection)
    if (cmd && (keyCode == 'T' || keyCode == 't'))
    {
        if (displayMode == DisplayMode::InstrumentType)
        {
            autoSlice();
            notifyParamsChanged();
            return true;
        }
    }

    // Let other Cmd shortcuts pass through to ApplicationCommandTarget
    if (cmd) return false;

    // Backtick (`): toggle Parameters/Modulation sub-tab (in InstrumentEdit mode)
    if (key.getTextCharacter() == '`')
    {
        if (displayMode == DisplayMode::InstrumentEdit)
        {
            if (editSubTab == EditSubTab::Parameters)
                setEditSubTab (EditSubTab::MidiOut);
            else if (editSubTab == EditSubTab::MidiOut)
                setEditSubTab (EditSubTab::Modulation);
            else
                setEditSubTab (EditSubTab::Parameters);
        }
        return true;
    }

    // Space: preview current note or selected slice
    if (keyCode == juce::KeyPress::spaceKey)
    {
        // Ignore key repeat while already previewing
        if (previewActive && previewKeyCode == juce::KeyPress::spaceKey)
            return true;

        flushPendingParams();

        // In slice modes, space previews the currently selected slice region.
        const bool isSliceMode = (currentParams.playMode == InstrumentParams::PlayMode::Slice
                                  || currentParams.playMode == InstrumentParams::PlayMode::BeatSlice);
        if (displayMode == DisplayMode::InstrumentType && isSliceMode)
        {
            int numSlices = (currentParams.playMode == InstrumentParams::PlayMode::BeatSlice)
                ? SamplePlaybackLayout::getBeatSliceRegionCount (currentParams)
                : SamplePlaybackLayout::getSliceRegionCount (currentParams);
            int sliceIndex = juce::jlimit (0, numSlices - 1, currentParams.selectedSlice);
            if (onPreviewRequested)
            {
                const int previewNote = currentParams.playMode == InstrumentParams::PlayMode::BeatSlice
                                            ? 60 + sliceIndex
                                            : currentOctave * 12;
                onPreviewRequested (currentInstrument, previewNote);
            }
        }
        else
        {
            if (onPreviewRequested)
                onPreviewRequested (currentInstrument, currentOctave * 12);
        }
        previewActive = true;
        previewKeyCode = juce::KeyPress::spaceKey;
        startTimerHz (30);
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

    // ── Zoom shortcuts (InstrumentType / playback page only) ──
    if (displayMode == DisplayMode::InstrumentType)
    {
        // + / = : zoom in
        if (key.getTextCharacter() == '+' || key.getTextCharacter() == '=')
        {
            double centre = (viewStart + viewEnd) * 0.5;
            zoomAroundPoint (0.8, centre);
            repaint();
            return true;
        }
        // - : zoom out
        if (key.getTextCharacter() == '-')
        {
            double centre = (viewStart + viewEnd) * 0.5;
            zoomAroundPoint (1.25, centre);
            repaint();
            return true;
        }
        // 0 : reset zoom
        if (key.getTextCharacter() == '0')
        {
            viewStart = 0.0;
            viewEnd = 1.0;
            repaint();
            return true;
        }

        // ── Slice mode keyboard shortcuts ──
        bool isSliceMode = (currentParams.playMode == InstrumentParams::PlayMode::Slice
                            || currentParams.playMode == InstrumentParams::PlayMode::BeatSlice);

        if (isSliceMode)
        {
            // Shift+Left/Right: select different slice regions
            if (shift && keyCode == juce::KeyPress::leftKey)
            {
                setSelectedSliceRegion (currentParams.selectedSlice - 1);
                notifyParamsChanged();
                return true;
            }
            if (shift && keyCode == juce::KeyPress::rightKey)
            {
                setSelectedSliceRegion (currentParams.selectedSlice + 1);
                notifyParamsChanged();
                return true;
            }

            // Shift+Up/Down: nudge selected slice position
            if (shift && keyCode == juce::KeyPress::upKey)
            {
                const int regionIndex = currentParams.selectedSlice;
                const double step = 0.005;
                if (regionIndex == 0)
                {
                    const double next = currentParams.slicePoints.empty()
                                            ? currentParams.endPos
                                            : currentParams.slicePoints.front();
                    currentParams.startPos = juce::jlimit (0.0, next, currentParams.startPos + step);
                    notifyParamsChanged();
                }
                else if (regionIndex - 1 < static_cast<int> (currentParams.slicePoints.size()))
                {
                    const int pointIndex = regionIndex - 1;
                    const double lower = pointIndex == 0
                                             ? currentParams.startPos
                                             : currentParams.slicePoints[static_cast<size_t> (pointIndex - 1)];
                    const double upper = pointIndex + 1 < static_cast<int> (currentParams.slicePoints.size())
                                             ? currentParams.slicePoints[static_cast<size_t> (pointIndex + 1)]
                                             : currentParams.endPos;
                    currentParams.slicePoints[static_cast<size_t> (pointIndex)] =
                        juce::jlimit (lower, upper,
                            currentParams.slicePoints[static_cast<size_t> (pointIndex)] + step);
                    notifyParamsChanged();
                }
                return true;
            }
            if (shift && keyCode == juce::KeyPress::downKey)
            {
                const int regionIndex = currentParams.selectedSlice;
                const double step = 0.005;
                if (regionIndex == 0)
                {
                    const double next = currentParams.slicePoints.empty()
                                            ? currentParams.endPos
                                            : currentParams.slicePoints.front();
                    currentParams.startPos = juce::jlimit (0.0, next, currentParams.startPos - step);
                    notifyParamsChanged();
                }
                else if (regionIndex - 1 < static_cast<int> (currentParams.slicePoints.size()))
                {
                    const int pointIndex = regionIndex - 1;
                    const double lower = pointIndex == 0
                                             ? currentParams.startPos
                                             : currentParams.slicePoints[static_cast<size_t> (pointIndex - 1)];
                    const double upper = pointIndex + 1 < static_cast<int> (currentParams.slicePoints.size())
                                             ? currentParams.slicePoints[static_cast<size_t> (pointIndex + 1)]
                                             : currentParams.endPos;
                    currentParams.slicePoints[static_cast<size_t> (pointIndex)] =
                        juce::jlimit (lower, upper,
                            currentParams.slicePoints[static_cast<size_t> (pointIndex)] - step);
                    notifyParamsChanged();
                }
                return true;
            }

            // Delete or Backspace: remove selected slice
            if (keyCode == juce::KeyPress::deleteKey || keyCode == juce::KeyPress::backspaceKey)
            {
                if (currentParams.selectedSlice > 0)
                {
                    removeSlice (currentParams.selectedSlice - 1);
                    notifyParamsChanged();
                }
                return true;
            }

            // 'a' key (not mapped to note in this context, check): add slice at view centre
            // We need to be careful not to conflict with note keys. 'a' is not a note key
            // in the tracker layout, so it is safe.
            if (key.getTextCharacter() == 'a' && ! shift)
            {
                // Note: 'a' is not in the note-key mapping (NoteUtils), so it's free
                double centrePos = (viewStart + viewEnd) * 0.5;
                addSliceAtPosition (centrePos);
                notifyParamsChanged();
                return true;
            }
        }
    }

    // Up/Down: adjust value in current column
    // For discrete lists: Down=+1 moves selection down, Up=-1 moves up
    // For continuous bars: Up=+1 increases, Down=-1 decreases
    if (keyCode == juce::KeyPress::upKey)
    {
        adjustCurrentValue (isCurrentColumnDiscrete() ? -1 : 1, shift, false);
        return true;
    }
    if (keyCode == juce::KeyPress::downKey)
    {
        adjustCurrentValue (isCurrentColumnDiscrete() ? 1 : -1, shift, false);
        return true;
    }

    // Left: move to previous column (seamless across sub-tabs in InstrumentEdit)
    if (keyCode == juce::KeyPress::leftKey)
    {
        int col = getFocusedColumn();
        if (col > 0)
        {
            setFocusedColumn (col - 1);
            repaint();
        }
        else if (displayMode == DisplayMode::InstrumentEdit && col == 0)
        {
            // Wrap to the other sub-tab's last column
            if (editSubTab == EditSubTab::Parameters)
            {
                setEditSubTab (EditSubTab::Modulation);
                modColumn = 7; // last modulation column (8 cols, 0-7)
            }
            else if (editSubTab == EditSubTab::MidiOut)
            {
                setEditSubTab (EditSubTab::Parameters);
                parametersColumn = 11; // last parameters column (12 cols, 0-11)
            }
            else
            {
                setEditSubTab (EditSubTab::MidiOut);
                midiOutColumn = InstrumentParams::kNumMidiOutLanes * 2 - 1;
            }
            repaint();
        }
        return true;
    }

    // Right: move to next column (seamless across sub-tabs in InstrumentEdit)
    if (keyCode == juce::KeyPress::rightKey)
    {
        int col = getFocusedColumn();
        int count = getColumnCount();
        if (col < count - 1)
        {
            setFocusedColumn (col + 1);
            repaint();
        }
        else if (displayMode == DisplayMode::InstrumentEdit && col == count - 1)
        {
            // Wrap to the other sub-tab's first column
            if (editSubTab == EditSubTab::Parameters)
            {
                setEditSubTab (EditSubTab::MidiOut);
                midiOutColumn = 0;
            }
            else if (editSubTab == EditSubTab::MidiOut)
            {
                setEditSubTab (EditSubTab::Modulation);
                modColumn = 0;
            }
            else
            {
                setEditSubTab (EditSubTab::Parameters);
                parametersColumn = 0;
            }
            repaint();
        }
        return true;
    }

    // ── Beat Slice mode: keyboard previews individual slices ──
    if (displayMode == DisplayMode::InstrumentType
        && currentParams.playMode == InstrumentParams::PlayMode::BeatSlice)
    {
        // Map note keys to sequential slice indices instead of pitched notes.
        // The sampler uses (note - 60) as the slice index.
        int note = keyToNote (key);
        if (note >= 0 && note < 128)
        {
            // Ignore key repeat while already previewing
            if (previewActive && previewKeyCode == key.getKeyCode())
                return true;

            // Convert the note into a sequential slice index based on keyboard position.
            // Keys are laid out chromatically: lowest key = slice 0, next = slice 1, etc.
            // The octave-relative note offset from the base octave gives us the slice index.
            int sliceIndex = note - (currentOctave * 12);
            int numSlices = SamplePlaybackLayout::getBeatSliceRegionCount (currentParams);
            sliceIndex = juce::jlimit (0, numSlices - 1, sliceIndex);

            setSelectedSliceRegion (sliceIndex);
            notifyParamsChanged();

            // Flush pending params before preview
            flushPendingParams();

            // Send note 60 + sliceIndex to trigger that specific slice in the sampler
            if (onPreviewRequested)
                onPreviewRequested (currentInstrument, 60 + sliceIndex);

            previewActive = true;
            previewKeyCode = key.getKeyCode();
            startTimerHz (30);
            repaint();
            return true;
        }
    }

    // Note keys: preview the note (normal pitched preview)
    int note = keyToNote (key);
    if (note >= 0 && note < 128)
    {
        // Ignore key repeat while already previewing
        if (previewActive && previewKeyCode == key.getKeyCode())
            return true;

        flushPendingParams();
        if (onPreviewRequested)
            onPreviewRequested (currentInstrument, note);
        previewActive = true;
        previewKeyCode = key.getKeyCode();
        startTimerHz (30);
        return true;
    }

    // Consume all other non-modifier keys to prevent macOS beep
    if (! key.getModifiers().isAnyModifierKeyDown())
        return true;

    return false;
}

bool SampleEditorComponent::keyStateChanged (bool isKeyDown)
{
    if (! isKeyDown && previewActive && previewKeyCode >= 0)
    {
        if (! juce::KeyPress::isKeyCurrentlyDown (previewKeyCode))
        {
            previewActive = false;
            previewKeyCode = -1;
            currentPlaybackPos = -1.0f;
            if (onPreviewStopped)
                onPreviewStopped();
            if (! paramsDirty)
                stopTimer();
            repaint();
        }
    }
    return false;
}

//==============================================================================
// Mouse
//==============================================================================

void SampleEditorComponent::mouseDown (const juce::MouseEvent& event)
{
    if (currentInstrument < 0) return;
    grabKeyboardFocus();

    if (showingPlugin)
    {
        if (event.mods.isLeftButtonDown())
        {
            auto hit = hitTestPluginPage (event.getPosition());
            if (hit.kind == PluginHitKind::None && event.getNumberOfClicks() >= 2)
            {
                if (onOpenPluginEditorRequested)
                    onOpenPluginEditorRequested (currentInstrument);
            }
            else
            {
                if (hit.kind != PluginHitKind::None)
                    focusedPluginHit = hit;
                handlePluginHit (hit, event);
            }
        }
        return;
    }

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
            setEditSubTab (EditSubTab::MidiOut);
        else if (itemIdx == 2)
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

    // Click in content area
    if (event.y >= contentTop && event.y < contentBottom)
    {
        // ── InstrumentType / Playback page: waveform interaction ──
        if (displayMode == DisplayMode::InstrumentType)
        {
            auto waveArea = getWaveformArea();

            // Check if click is in the mode list area (top-right overlay)
            int numCols = getColumnCount();
            int listW = 140;
            int listX = waveArea.getRight() - listW - 2;
            int listY = waveArea.getY() + 2;
            int listH = 7 * kListItemHeight + 2;

            if (event.x >= listX && event.x <= listX + listW
                && event.y >= listY && event.y <= listY + listH)
            {
                setFocusedColumn (numCols - 1);
                int itemIdx = (event.y - listY) / kListItemHeight;
                if (itemIdx >= 0 && itemIdx < 7)
                {
                    currentParams.playMode = static_cast<InstrumentParams::PlayMode> (itemIdx);
                    if (playbackColumn >= getColumnCount())
                        playbackColumn = getColumnCount() - 1;
                    notifyParamsChanged();
                }
                repaint();
                return;
            }

            // Check if click is in waveform area
            if (waveArea.contains (event.x, event.y))
            {
                bool isSliceMode = (currentParams.playMode == InstrumentParams::PlayMode::Slice
                                    || currentParams.playMode == InstrumentParams::PlayMode::BeatSlice);

                // Middle mouse button or Alt+click: start panning
                if (event.mods.isMiddleButtonDown()
                    || (event.mods.isLeftButtonDown() && event.mods.isAltDown()))
                {
                    isPanning = true;
                    panStartX = event.position.x;
                    panStartViewStart = viewStart;
                    panStartViewEnd = viewEnd;
                    return;
                }

                // Shift+click in slice mode: remove nearest slice
                if (isSliceMode && event.mods.isShiftDown() && event.mods.isLeftButtonDown())
                {
                    int sliceIdx = hitTestSlice (event.x, waveArea);
                    if (sliceIdx >= 0)
                    {
                        removeSlice (sliceIdx);
                        notifyParamsChanged();
                    }
                    return;
                }

                // Right-click in slice mode: remove nearest slice
                if (isSliceMode && event.mods.isPopupMenu())
                {
                    int sliceIdx = hitTestSlice (event.x, waveArea);
                    if (sliceIdx >= 0)
                    {
                        removeSlice (sliceIdx);
                        notifyParamsChanged();
                    }
                    return;
                }

                // Left click: check for marker hit first
                auto marker = hitTestMarker (event.x, waveArea);

                if (marker != MarkerType::None)
                {
                    // Start dragging a marker
                    isWaveformDragging = true;
                    draggingMarker = marker;
                    waveformDragStartX = event.position.x;
                    if (marker == MarkerType::Slice)
                    {
                        draggingSliceIndex = hitTestSlice (event.x, waveArea);
                        if (draggingSliceIndex >= 0)
                            setSelectedSliceRegion (draggingSliceIndex + 1);
                    }
                    repaint();
                    return;
                }

                // No marker hit: mode-specific behavior
                if (isSliceMode)
                {
                    // Click on waveform in slice mode: add a slice point
                    double normPos = pixelToNormPos (event.x, waveArea);
                    addSliceAtPosition (normPos);
                    notifyParamsChanged();
                    return;
                }

                // For other modes: set the focused column's value to clicked position
                double normPos = pixelToNormPos (event.x, waveArea);
                auto mode = currentParams.playMode;
                switch (mode)
                {
                    case InstrumentParams::PlayMode::OneShot:
                        switch (playbackColumn)
                        {
                            case 0: currentParams.startPos = juce::jlimit (0.0, currentParams.endPos, normPos); break;
                            case 1: currentParams.endPos = juce::jlimit (currentParams.startPos, 1.0, normPos); break;
                            default: break;
                        }
                        break;
                    case InstrumentParams::PlayMode::ForwardLoop:
                    case InstrumentParams::PlayMode::BackwardLoop:
                    case InstrumentParams::PlayMode::PingpongLoop:
                        switch (playbackColumn)
                        {
                            case 0: currentParams.startPos = juce::jlimit (0.0, currentParams.endPos, normPos); break;
                            case 1: currentParams.loopStart = juce::jlimit (currentParams.startPos, currentParams.loopEnd, normPos); break;
                            case 2: currentParams.loopEnd = juce::jlimit (currentParams.loopStart, currentParams.endPos, normPos); break;
                            case 3: currentParams.endPos = juce::jlimit (currentParams.startPos, 1.0, normPos); break;
                            default: break;
                        }
                        break;
                    case InstrumentParams::PlayMode::Granular:
                        switch (playbackColumn)
                        {
                            case 0: currentParams.startPos = juce::jlimit (0.0, currentParams.endPos, normPos); break;
                            case 1: currentParams.endPos = juce::jlimit (currentParams.startPos, 1.0, normPos); break;
                            case 2: currentParams.granularPosition = juce::jlimit (0.0, 1.0, normPos); break;
                            default: break;
                        }
                        break;
                    case InstrumentParams::PlayMode::Slice:
                    case InstrumentParams::PlayMode::BeatSlice:
                        break;
                }
                notifyParamsChanged();
                return;
            }
            return;
        }

        // ── InstrumentEdit page: column-based interaction ──
        int contentWidth = getWidth() - contentLeftOffset;
        int contentX = event.x - contentLeftOffset;
        if (contentX < 0) return;

        int numCols = getColumnCount();
        if (numCols > 0)
        {
            int colW = contentWidth / numCols;
            int col = contentX / juce::jmax (1, colW);
            col = juce::jlimit (0, numCols - 1, col);

            setFocusedColumn (col);

            // For list columns in modulation page, handle item clicks
            if (editSubTab == EditSubTab::Modulation)
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
                        auto oldType = mod.type;
                        mod.type = static_cast<InstrumentParams::Modulation::Type> (itemIdx);
                        if (mod.type != oldType)
                            mod.amount = 0;
                    }
                    handledAsList = true;
                }
                else if (col == 2) // Mode list
                {
                    int itemIdx = relY / juce::jmax (1, kListItemHeight);
                    if (itemIdx >= 0 && itemIdx < 2)
                    {
                        auto& mod = currentParams.modulations[static_cast<size_t> (modDestIndex)];
                        mod.modMode = static_cast<InstrumentParams::Modulation::ModMode> (itemIdx);
                    }
                    handledAsList = true;
                }
                else if (col == 3 && currentParams.modulations[static_cast<size_t> (modDestIndex)].type
                                      == InstrumentParams::Modulation::Type::LFO)
                {
                    int itemIdx = relY / juce::jmax (1, kListItemHeight);
                    if (itemIdx >= 0 && itemIdx < 5)
                    {
                        auto& mod = currentParams.modulations[static_cast<size_t> (modDestIndex)];
                        mod.lfoShape = static_cast<InstrumentParams::Modulation::LFOShape> (itemIdx);
                    }
                    handledAsList = true;
                }
                else if (col == 4 && currentParams.modulations[static_cast<size_t> (modDestIndex)].type
                                      == InstrumentParams::Modulation::Type::LFO
                                  && currentParams.modulations[static_cast<size_t> (modDestIndex)].lfoSpeedMode
                                      == InstrumentParams::Modulation::LFOSpeedMode::Steps)
                {
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
                else if (col == 6 && currentParams.modulations[static_cast<size_t> (modDestIndex)].type
                                      == InstrumentParams::Modulation::Type::LFO)
                {
                    int itemIdx = relY / juce::jmax (1, kListItemHeight);
                    if (itemIdx >= 0 && itemIdx < 2)
                    {
                        auto& mod = currentParams.modulations[static_cast<size_t> (modDestIndex)];
                        mod.lfoSpeedMode = static_cast<InstrumentParams::Modulation::LFOSpeedMode> (itemIdx);
                    }
                    handledAsList = true;
                }

                if (handledAsList)
                {
                    notifyParamsChanged();
                    return;
                }
            }

            if (editSubTab == EditSubTab::MidiOut && isMidiOutTypeColumn (col))
            {
                const int relY = event.y - contentTop;
                const int itemIdx = relY / juce::jmax (1, kListItemHeight);
                if (itemIdx >= 0 && itemIdx < 4)
                {
                    const int lane = getMidiOutLaneForColumn (col);
                    currentParams.midiOutAssignments[static_cast<size_t> (lane)].type
                        = static_cast<InstrumentParams::MidiOutMessageType> (itemIdx);
                }
                notifyParamsChanged();
                return;
            }

            // For parameters page, handle filter type list clicks (col 4)
            if (editSubTab == EditSubTab::Parameters && col == 4)
            {
                int relY = event.y - contentTop;
                int itemIdx = relY / juce::jmax (1, kListItemHeight);
                if (itemIdx >= 0 && itemIdx < 4)
                {
                    setFilterTypeWithDefaultCutoff (static_cast<InstrumentParams::FilterType> (itemIdx));
                }
                notifyParamsChanged();
                return;
            }

            // Click-to-set for bar columns: set value based on click Y position
            if (! isCurrentColumnDiscrete())
            {
                int contentH = contentBottom - contentTop;
                double norm = 1.0 - static_cast<double> (event.y - contentTop)
                                   / static_cast<double> (juce::jmax (1, contentH));
                norm = juce::jlimit (0.0, 1.0, norm);

                if (editSubTab == EditSubTab::Parameters)
                {
                    switch (parametersColumn)
                    {
                        case 0: currentParams.volume    = -100.0 + norm * 124.0; break;
                        case 1: currentParams.panning   = static_cast<int> (-50.0 + norm * 100.0); break;
                        case 2: currentParams.tune      = static_cast<int> (-24.0 + norm * 48.0); break;
                        case 3: currentParams.finetune  = static_cast<int> (-100.0 + norm * 200.0); break;
                        case 5: currentParams.cutoff    = static_cast<int> (norm * 100.0); break;
                        case 6: currentParams.resonance = static_cast<int> (norm * 100.0); break;
                        case 7: currentParams.overdrive = static_cast<int> (norm * 100.0); break;
                        case 8: currentParams.bitDepth  = 4 + static_cast<int> (norm * 12.0); break;
                        case 9: currentParams.lofiSampleRateHz = normToLofiSampleRateHz (norm); break;
                        case 10: currentParams.reverbSend = -100.0 + norm * 100.0; break;
                        case 11: currentParams.delaySend = -100.0 + norm * 100.0; break;
                        default: break;
                    }
                }
                else if (editSubTab == EditSubTab::MidiOut)
                {
                    const int lane = getMidiOutLaneForColumn (midiOutColumn);
                    auto& assignment = currentParams.midiOutAssignments[static_cast<size_t> (lane)];
                    if (midiOutAssignmentUsesNumber (assignment.type))
                        assignment.number = juce::jlimit (0, 127, static_cast<int> (std::round (norm * 127.0)));
                }
                else if (editSubTab == EditSubTab::Modulation)
                {
                    auto& mod = currentParams.modulations[static_cast<size_t> (modDestIndex)];
                    if (mod.type == InstrumentParams::Modulation::Type::LFO)
                    {
                        if (modColumn == 4 && mod.lfoSpeedMode == InstrumentParams::Modulation::LFOSpeedMode::MS)
                            mod.lfoSpeedMs = juce::jlimit (1, 5000, static_cast<int> (norm * 5000.0));
                        else if (modColumn == 5) // Amount
                            mod.amount = static_cast<int> (norm * 100.0);
                    }
                    else if (mod.type == InstrumentParams::Modulation::Type::Envelope)
                    {
                        switch (modColumn)
                        {
                            case 3: mod.attackS  = norm * 10.0; break;
                            case 4: mod.decayS   = norm * 10.0; break;
                            case 5: mod.sustain  = static_cast<int> (norm * 100.0); break;
                            case 6: mod.releaseS = norm * 10.0; break;
                            case 7: mod.amount   = static_cast<int> (norm * 100.0); break;
                            default: break;
                        }
                    }
                }
                notifyParamsChanged();
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
    if (showingPlugin)
    {
        if (pluginDragHit.kind != PluginHitKind::None)
        {
            pluginModulation = pluginDragStartModulation;
            const double delta = static_cast<double> (pluginDragStartY - event.position.y) / 180.0;
            adjustPluginHitValue (pluginDragHit, delta);
        }
        return;
    }

    // ── Waveform panning ──
    if (isPanning)
    {
        auto waveArea = getWaveformArea();
        float deltaX = event.position.x - panStartX;
        double viewWidth = panStartViewEnd - panStartViewStart;
        double normDelta = -static_cast<double> (deltaX) / static_cast<double> (juce::jmax (1, waveArea.getWidth())) * viewWidth;

        double newStart = panStartViewStart + normDelta;
        double newEnd = panStartViewEnd + normDelta;

        // Clamp to 0-1 range
        if (newStart < 0.0) { newEnd -= newStart; newStart = 0.0; }
        if (newEnd > 1.0)   { newStart -= (newEnd - 1.0); newEnd = 1.0; }
        newStart = juce::jlimit (0.0, 1.0, newStart);
        newEnd = juce::jlimit (0.0, 1.0, newEnd);

        viewStart = newStart;
        viewEnd = newEnd;
        repaint();
        return;
    }

    // ── Waveform marker dragging ──
    if (isWaveformDragging && draggingMarker != MarkerType::None)
    {
        auto waveArea = getWaveformArea();
        double normPos = pixelToNormPos (juce::roundToInt (event.position.x), waveArea);
        normPos = juce::jlimit (0.0, 1.0, normPos);

        switch (draggingMarker)
        {
            case MarkerType::Start:
                currentParams.startPos = juce::jlimit (0.0, currentParams.endPos, normPos);
                break;
            case MarkerType::End:
                currentParams.endPos = juce::jlimit (currentParams.startPos, 1.0, normPos);
                break;
            case MarkerType::LoopStart:
                currentParams.loopStart = juce::jlimit (currentParams.startPos, currentParams.loopEnd, normPos);
                break;
            case MarkerType::LoopEnd:
                currentParams.loopEnd = juce::jlimit (currentParams.loopStart, currentParams.endPos, normPos);
                break;
            case MarkerType::GranPos:
                currentParams.granularPosition = juce::jlimit (0.0, 1.0, normPos);
                break;
            case MarkerType::Slice:
                if (draggingSliceIndex >= 0 && draggingSliceIndex < static_cast<int> (currentParams.slicePoints.size()))
                {
                    currentParams.slicePoints[static_cast<size_t> (draggingSliceIndex)] =
                        juce::jlimit (currentParams.startPos, currentParams.endPos, normPos);
                    // Keep sorted
                    std::sort (currentParams.slicePoints.begin(), currentParams.slicePoints.end());
                    // Update index after sort
                    for (int i = 0; i < static_cast<int> (currentParams.slicePoints.size()); ++i)
                    {
                        if (std::abs (currentParams.slicePoints[static_cast<size_t> (i)] - normPos) < 0.0001)
                        {
                            draggingSliceIndex = i;
                            setSelectedSliceRegion (i + 1);
                            break;
                        }
                    }
                }
                break;
            case MarkerType::None:
                break;
        }
        notifyParamsChanged();
        return;
    }

    // ── Column bar/list drag (InstrumentEdit pages) ──
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
    if (showingPlugin)
    {
        isPanning = false;
        isWaveformDragging = false;
        isDragging = false;
        pluginDragHit = {};
        draggingMarker = MarkerType::None;
        draggingSliceIndex = -1;
        return;
    }

    if (isPanning)
    {
        isPanning = false;
        return;
    }

    if (isWaveformDragging)
    {
        isWaveformDragging = false;
        draggingMarker = MarkerType::None;
        draggingSliceIndex = -1;

        // Full commit on mouse-up
        stopTimer();
        paramsDirty = false;
        if (onParamsChanged)
            onParamsChanged (currentInstrument, currentParams);
        lastCommittedParams = currentParams;
        repaint();
        return;
    }

    if (isDragging)
    {
        isDragging = false;
        // Always do a full commit on mouse-up to ensure structural params are applied
        stopTimer();
        paramsDirty = false;
        if (onParamsChanged)
            onParamsChanged (currentInstrument, currentParams);
        lastCommittedParams = currentParams;
    }
}

void SampleEditorComponent::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (currentInstrument < 0) return;

    float delta = wheel.deltaY;
    if (std::abs (delta) < 0.001f) return;

    if (showingPlugin)
    {
        auto hit = hitTestPluginPage (event.getPosition());
        switch (hit.kind)
        {
            case PluginHitKind::LfoRateValue:
            case PluginHitKind::EnvAttack:
            case PluginHitKind::EnvDecay:
            case PluginHitKind::EnvSustain:
            case PluginHitKind::EnvRelease:
            case PluginHitKind::RouteAmount:
                adjustPluginHitValue (hit, static_cast<double> (delta) * 0.12);
                return;
            case PluginHitKind::None:
            case PluginHitKind::OpenEditor:
            case PluginHitKind::AddLfo:
            case PluginHitKind::AddEnvelope:
            case PluginHitKind::SourceSelect:
            case PluginHitKind::SourceEnable:
            case PluginHitKind::SourceRemove:
            case PluginHitKind::LfoShape:
            case PluginHitKind::LfoRateMode:
            case PluginHitKind::EnvTrigger:
            case PluginHitKind::ParamSearch:
            case PluginHitKind::ParamAssign:
            case PluginHitKind::RouteSelect:
            case PluginHitKind::RouteEnable:
            case PluginHitKind::RouteRemove:
            case PluginHitKind::RouteSource:
            case PluginHitKind::RouteParam:
                break;
        }

        if (getPluginParameterListBounds().contains (event.getPosition()))
        {
            pluginParameterWheelAccumulator += -static_cast<double> (delta) * kPluginParameterWheelRowsPerUnit;
            const int rows = static_cast<int> (pluginParameterWheelAccumulator);
            if (rows != 0)
            {
                scrollPluginParameterListBy (rows);
                pluginParameterWheelAccumulator -= static_cast<double> (rows);
            }
        }
        return;
    }

    // ── Waveform zoom/scroll (InstrumentType page) ──
    if (displayMode == DisplayMode::InstrumentType)
    {
        auto waveArea = getWaveformArea();
        if (waveArea.contains (event.x, event.y))
        {
            // Cmd/Ctrl + scroll: zoom
            if (event.mods.isCommandDown())
            {
                double normPos = pixelToNormPos (event.x, waveArea);
                double zoomFactor = (delta > 0) ? 0.85 : 1.18;
                zoomAroundPoint (zoomFactor, normPos);
                repaint();
                return;
            }

            // Shift + scroll: horizontal pan
            if (event.mods.isShiftDown())
            {
                double viewWidth = viewEnd - viewStart;
                double scrollAmount = -static_cast<double> (delta) * viewWidth * 0.15;
                scrollView (scrollAmount);
                repaint();
                return;
            }

            // Plain scroll on waveform: also horizontal pan (natural for zoomed waveforms)
            {
                double viewWidth = viewEnd - viewStart;
                double scrollAmount = -static_cast<double> (delta) * viewWidth * 0.15;
                scrollView (scrollAmount);
                repaint();
                return;
            }
        }
    }

    // ── Column-based scroll (InstrumentEdit pages or bottom bar) ──
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

//==============================================================================
// Mouse move (for hover feedback)
//==============================================================================

void SampleEditorComponent::mouseMove (const juce::MouseEvent& event)
{
    if (currentInstrument < 0 || showingPlugin || displayMode != DisplayMode::InstrumentType)
    {
        if (hoveredMarker != MarkerType::None)
        {
            hoveredMarker = MarkerType::None;
            setMouseCursor (juce::MouseCursor::NormalCursor);
            repaint();
        }
        return;
    }

    auto waveArea = getWaveformArea();
    if (waveArea.contains (event.x, event.y))
    {
        auto marker = hitTestMarker (event.x, waveArea);
        if (marker != hoveredMarker)
        {
            hoveredMarker = marker;
            if (marker != MarkerType::None)
                setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
            else
                setMouseCursor (juce::MouseCursor::NormalCursor);
            repaint();
        }
    }
    else
    {
        if (hoveredMarker != MarkerType::None)
        {
            hoveredMarker = MarkerType::None;
            setMouseCursor (juce::MouseCursor::NormalCursor);
            repaint();
        }
    }
}

//==============================================================================
// Waveform coordinate helpers
//==============================================================================

juce::Rectangle<int> SampleEditorComponent::getWaveformArea() const
{
    int contentTop = kHeaderHeight;
    int contentBottom = getHeight() - kBottomBarHeight;
    auto contentArea = juce::Rectangle<int> (0, contentTop, getWidth(), contentBottom - contentTop);
    // Remove overview bar space at bottom
    contentArea = contentArea.withTrimmedBottom (kOverviewBarHeight + 2);
    return contentArea.reduced (4, 4);
}

double SampleEditorComponent::pixelToNormPos (int pixelX, juce::Rectangle<int> waveArea) const
{
    double frac = static_cast<double> (pixelX - waveArea.getX())
                  / static_cast<double> (juce::jmax (1, waveArea.getWidth()));
    frac = juce::jlimit (0.0, 1.0, frac);
    // Map from view coordinates to normalized sample position
    return viewStart + frac * (viewEnd - viewStart);
}

int SampleEditorComponent::normPosToPixel (double normPos, juce::Rectangle<int> waveArea) const
{
    double viewWidth = viewEnd - viewStart;
    if (viewWidth <= 0.0) viewWidth = 1.0;
    double frac = (normPos - viewStart) / viewWidth;
    return waveArea.getX() + juce::roundToInt (frac * waveArea.getWidth());
}

SampleEditorComponent::MarkerType SampleEditorComponent::hitTestMarker (int pixelX, juce::Rectangle<int> waveArea) const
{
    constexpr int kHitRadius = 6; // pixels

    auto mode = currentParams.playMode;

    // Check slice markers first (they can be numerous)
    if (mode == InstrumentParams::PlayMode::Slice || mode == InstrumentParams::PlayMode::BeatSlice)
    {
        for (int i = 0; i < static_cast<int> (currentParams.slicePoints.size()); ++i)
        {
            int px = normPosToPixel (currentParams.slicePoints[static_cast<size_t> (i)], waveArea);
            if (std::abs (pixelX - px) <= kHitRadius)
                return MarkerType::Slice;
        }
    }

    // Start marker
    {
        int px = normPosToPixel (currentParams.startPos, waveArea);
        if (std::abs (pixelX - px) <= kHitRadius)
            return MarkerType::Start;
    }

    // End marker
    {
        int px = normPosToPixel (currentParams.endPos, waveArea);
        if (std::abs (pixelX - px) <= kHitRadius)
            return MarkerType::End;
    }

    // Loop markers
    if (mode == InstrumentParams::PlayMode::ForwardLoop
        || mode == InstrumentParams::PlayMode::BackwardLoop
        || mode == InstrumentParams::PlayMode::PingpongLoop)
    {
        int lsPx = normPosToPixel (currentParams.loopStart, waveArea);
        if (std::abs (pixelX - lsPx) <= kHitRadius)
            return MarkerType::LoopStart;

        int lePx = normPosToPixel (currentParams.loopEnd, waveArea);
        if (std::abs (pixelX - lePx) <= kHitRadius)
            return MarkerType::LoopEnd;
    }

    // Granular position marker
    if (mode == InstrumentParams::PlayMode::Granular)
    {
        int gPx = normPosToPixel (currentParams.granularPosition, waveArea);
        if (std::abs (pixelX - gPx) <= kHitRadius)
            return MarkerType::GranPos;
    }

    return MarkerType::None;
}

int SampleEditorComponent::hitTestSlice (int pixelX, juce::Rectangle<int> waveArea) const
{
    constexpr int kHitRadius = 6;
    int bestIdx = -1;
    int bestDist = kHitRadius + 1;

    for (int i = 0; i < static_cast<int> (currentParams.slicePoints.size()); ++i)
    {
        int px = normPosToPixel (currentParams.slicePoints[static_cast<size_t> (i)], waveArea);
        int dist = std::abs (pixelX - px);
        if (dist < bestDist)
        {
            bestDist = dist;
            bestIdx = i;
        }
    }
    return bestIdx;
}

//==============================================================================
// Zoom helpers
//==============================================================================

void SampleEditorComponent::zoomAroundPoint (double zoomFactor, double normPos)
{
    double viewWidth = viewEnd - viewStart;
    double newWidth = viewWidth * zoomFactor;

    // Clamp minimum zoom (don't zoom in past ~0.1% of sample)
    newWidth = juce::jlimit (0.001, 1.0, newWidth);

    // Calculate where normPos sits in the current view (0-1 fraction)
    double viewFrac = (viewWidth > 0.0) ? (normPos - viewStart) / viewWidth : 0.5;
    viewFrac = juce::jlimit (0.0, 1.0, viewFrac);

    double newStart = normPos - viewFrac * newWidth;
    double newEnd = newStart + newWidth;

    // Clamp to 0-1
    if (newStart < 0.0) { newEnd -= newStart; newStart = 0.0; }
    if (newEnd > 1.0)   { newStart -= (newEnd - 1.0); newEnd = 1.0; }
    newStart = juce::jlimit (0.0, 1.0, newStart);
    newEnd   = juce::jlimit (0.0, 1.0, newEnd);

    viewStart = newStart;
    viewEnd = newEnd;
}

void SampleEditorComponent::scrollView (double deltaNorm)
{
    double viewWidth = viewEnd - viewStart;
    double newStart = viewStart + deltaNorm;
    double newEnd = newStart + viewWidth;

    if (newStart < 0.0) { newEnd -= newStart; newStart = 0.0; }
    if (newEnd > 1.0)   { newStart -= (newEnd - 1.0); newEnd = 1.0; }
    newStart = juce::jlimit (0.0, 1.0, newStart);
    newEnd   = juce::jlimit (0.0, 1.0, newEnd);

    viewStart = newStart;
    viewEnd = newEnd;
}

//==============================================================================
// Slice operations
//==============================================================================

int SampleEditorComponent::getSliceRegionCountForCurrentMode() const
{
    if (currentParams.playMode == InstrumentParams::PlayMode::BeatSlice)
        return SamplePlaybackLayout::getBeatSliceRegionCount (currentParams);

    if (currentParams.playMode == InstrumentParams::PlayMode::Slice)
        return SamplePlaybackLayout::getSliceRegionCount (currentParams);

    return 0;
}

void SampleEditorComponent::setSelectedSliceRegion (int regionIndex)
{
    const int regionCount = getSliceRegionCountForCurrentMode();
    currentParams.selectedSlice = juce::jlimit (0, juce::jmax (0, regionCount - 1), regionIndex);
    selectedSliceIndex = currentParams.selectedSlice;
}

void SampleEditorComponent::addSliceAtPosition (double normPos)
{
    if (currentParams.slicePoints.size() >= 47)
        return;

    normPos = juce::jlimit (currentParams.startPos, currentParams.endPos, normPos);

    // Check for duplicate (within small tolerance)
    for (auto sp : currentParams.slicePoints)
    {
        if (std::abs (sp - normPos) < 0.001)
            return;
    }

    currentParams.slicePoints.push_back (normPos);
    std::sort (currentParams.slicePoints.begin(), currentParams.slicePoints.end());

    // Set play mode to Slice if not already a slice mode
    if (currentParams.playMode != InstrumentParams::PlayMode::Slice
        && currentParams.playMode != InstrumentParams::PlayMode::BeatSlice)
    {
        currentParams.playMode = InstrumentParams::PlayMode::Slice;
        playbackColumn = juce::jmin (playbackColumn, getColumnCount() - 1);
    }

    // Select the newly added slice
    for (int i = 0; i < static_cast<int> (currentParams.slicePoints.size()); ++i)
    {
        if (std::abs (currentParams.slicePoints[static_cast<size_t> (i)] - normPos) < 0.001)
        {
            setSelectedSliceRegion (i + 1);
            break;
        }
    }
}

void SampleEditorComponent::removeSlice (int sliceIdx)
{
    if (sliceIdx < 0 || sliceIdx >= static_cast<int> (currentParams.slicePoints.size()))
        return;

    currentParams.slicePoints.erase (currentParams.slicePoints.begin() + sliceIdx);
    setSelectedSliceRegion (juce::jmax (0, currentParams.selectedSlice - 1));
}

void SampleEditorComponent::generateEqualSlices (int numSlices)
{
    numSlices = juce::jlimit (1, 48, numSlices);
    currentParams.slicePoints = SamplePlaybackLayout::makeEqualSlicePointsNorm (
        currentParams.startPos, currentParams.endPos, numSlices);

    setSelectedSliceRegion (0);
}

void SampleEditorComponent::autoSlice()
{
    if (! currentFile.existsAsFile()) return;

    // Read the audio file using a local format manager
    juce::AudioFormatManager fmtMgr;
    fmtMgr.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (fmtMgr.createReaderFor (currentFile));
    if (reader == nullptr) return;

    auto numSamples = static_cast<int> (reader->lengthInSamples);
    if (numSamples <= 0) return;

    juce::AudioBuffer<float> buffer (1, numSamples);
    reader->read (&buffer, 0, numSamples, 0, true, false);

    // Delegate DSP to TransientDetector
    currentParams.slicePoints = TransientDetector::detectTransients (
        buffer, reader->sampleRate, autoSliceSensitivity,
        currentParams.startPos, currentParams.endPos);

    if (currentParams.slicePoints.size() > 47)
        currentParams.slicePoints.resize (47);

    // Switch to Slice mode
    if (! currentParams.slicePoints.empty())
    {
        if (currentParams.playMode != InstrumentParams::PlayMode::Slice
            && currentParams.playMode != InstrumentParams::PlayMode::BeatSlice)
        {
            currentParams.playMode = InstrumentParams::PlayMode::Slice;
            playbackColumn = juce::jmin (playbackColumn, getColumnCount() - 1);
        }
        setSelectedSliceRegion (0);
    }
}

// (drawOverviewBar has been moved to WaveformView)
