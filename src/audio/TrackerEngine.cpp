#include <algorithm>
#include <cmath>
#include "TrackerEngine.h"
#include "Pattern.h"
#include "PluginAutomationData.h"
#include "PluginCatalogService.h"
#include "InstrumentEffectsPlugin.h"
#include "TrackerSamplerPlugin.h"
#include "MetronomePlugin.h"
#include "SendEffectsPlugin.h"
#include "SendEffectsParams.h"
#include "MixerPlugin.h"
#include "ChannelStripPlugin.h"
#include "TrackOutputPlugin.h"
#include "InstrumentRouting.h"
#include "FxParamTransport.h"
#include "InstrumentPlaybackTiming.h"
#include "StepFxResolver.h"

namespace
{
constexpr int kCcFxTune = 31;
constexpr int kCcFxPortaSteps = 32;
constexpr int kCcFxSlideUp = 33;
constexpr int kCcFxSlideDown = 34;
constexpr int kCcFxDelaySend = 35;
constexpr int kCcFxReverbSend = 36;
constexpr int kCcSamplerDirection = 37;
constexpr int kCcSamplerPosition = 38;
constexpr int kCcFxNoteReset = 39;
constexpr int kCcFxVolume = 40;
constexpr int kCcSamplerSlice = 41;
constexpr int kCcSamplerRetrigger = 42;
constexpr int kCcFxMicroTune = 43;
constexpr int kCcFxOverdrive = 44;
constexpr int kCcFxLowPass = 45;
constexpr int kCcFxBandPass = 46;
constexpr int kCcFxHighPass = 47;
constexpr int kCcFxBitDepth = 48;
constexpr int kCcFxVolumeLfoRate = 49;
constexpr int kCcFxPanLfoRate = 50;
constexpr int kCcFxFilterLfoRate = 51;
constexpr int kCcFxPositionLfoRate = 52;
constexpr int kCcFxFinetuneLfoRate = 53;
constexpr int kCcSamplerHardCut = 86;

char getSlotCommandLetter (const FxSlot& slot)
{
    return slot.getCommandLetter();
}

struct MidiOutRoute
{
    int channel = 1;
    std::array<InstrumentParams::MidiOutAssignment,
               InstrumentParams::kNumMidiOutLanes> assignments = InstrumentParams::makeDefaultMidiOutAssignments();
};

int getTimingOffsetMs (const std::vector<FxSlot>& fxSlots)
{
    int offsetMs = 0;

    for (const auto& fxSlot : fxSlots)
    {
        if (getSlotCommandLetter (fxSlot) == 'm')
            offsetMs = InstrumentPlaybackTiming::decodeSignedTimingOffsetMs (fxSlot.fxParam);
    }

    return offsetMs;
}

int getPatternSwingPercentAtRow (const Pattern& pattern, int row, int repeatIndex)
{
    int swingPercent = 50;
    const int lastRow = juce::jlimit (0, juce::jmax (0, pattern.numRows - 1), row);

    for (int r = 0; r <= lastRow; ++r)
    {
        for (int track = 0; track < kNumTracks; ++track)
        {
            const auto fxSlots = StepFxResolver::resolveFxSlots (pattern.getCell (r, track), r, track, 0, repeatIndex);
            const int rowSwing = StepFxResolver::getPercentFxParam (fxSlots, 'I');
            if (rowSwing >= 0)
                swingPercent = InstrumentPlaybackTiming::clampSwingPercent (rowSwing);
        }
    }

    return swingPercent;
}

double getStepEventSeconds (double rowSeconds,
                            double rowEndSeconds,
                            int row,
                            int swingPercent,
                            const std::vector<FxSlot>& fxSlots)
{
    const double swungSeconds = InstrumentPlaybackTiming::applySwingOffsetSeconds (
        rowSeconds, row, rowEndSeconds - rowSeconds, swingPercent);
    return InstrumentPlaybackTiming::applyTimingOffsetSeconds (swungSeconds, getTimingOffsetMs (fxSlots));
}

int getRowTempoCommand (const Pattern& pattern, int row)
{
    if (row < 0 || row >= pattern.numRows)
        return -1;

    int tempoPercent = -1;
    int laneCount = row < static_cast<int> (pattern.masterFxRows.size())
                        ? static_cast<int> (pattern.masterFxRows[static_cast<size_t> (row)].size())
                        : 0;

    for (int lane = 0; lane < laneCount; ++lane)
    {
        const auto& slot = pattern.getMasterFxSlot (row, lane);
        if (getSlotCommandLetter (slot) == 'T')
            tempoPercent = decodeTempoPercentFxParam (slot.fxParam);
    }

    return tempoPercent;
}

double findPatternTempoStopBeat (const Pattern& pattern, int rpb, double beatOffset)
{
    const int safeRpb = juce::jmax (1, rpb);

    for (int row = 0; row < pattern.numRows; ++row)
        if (getRowTempoCommand (pattern, row) == 0)
            return beatOffset + static_cast<double> (row) / static_cast<double> (safeRpb);

    return -1.0;
}

double findArrangementTempoStopBeat (const std::vector<std::pair<const Pattern*, int>>& sequence, int rpb)
{
    const int safeRpb = juce::jmax (1, rpb);
    double beatOffset = 0.0;

    for (const auto& [pattern, repeats] : sequence)
    {
        if (pattern == nullptr)
            continue;

        const int safeRepeats = juce::jmax (0, repeats);
        const double patternLengthBeats = static_cast<double> (pattern->numRows) / static_cast<double> (safeRpb);

        for (int rep = 0; rep < safeRepeats; ++rep)
        {
            const double stopBeat = findPatternTempoStopBeat (*pattern, safeRpb, beatOffset);
            if (stopBeat >= 0.0)
                return stopBeat;

            beatOffset += patternLengthBeats;
        }
    }

    return -1.0;
}

double tempoPercentToBpm (double baseBpm, int tempoPercent)
{
    return juce::jlimit (20.0, 999.0,
                         baseBpm * static_cast<double> (tempoPercent) / 100.0);
}

int getMidiOutLaneIndexForCommandLetter (char commandLetter)
{
    return commandLetter >= 'a' && commandLetter <= 'f' ? commandLetter - 'a' : -1;
}

MidiOutRoute getMidiOutRouteForCell (const Cell& cell, int numNoteLanes, const SimpleSampler& sampler)
{
    MidiOutRoute route;

    for (int lane = 0; lane < numNoteLanes; ++lane)
    {
        const auto noteSlot = cell.getNoteLane (lane);
        if (noteSlot.instrument >= 0 && noteSlot.instrument < 16)
        {
            route.channel = noteSlot.instrument + 1;

            InstrumentParams params;
            if (sampler.getParamsIfPresent (noteSlot.instrument, params))
                route.assignments = params.midiOutAssignments;

            return route;
        }
    }

    return route;
}

float getTrackFaderGain (const TrackMixState& mixState)
{
    if (mixState.volume <= -99.0)
        return 0.0f;

    return juce::Decibels::decibelsToGain (static_cast<float> (mixState.volume));
}

void appendSymbolicTrackFx (juce::MidiMessageSequence& midiSeq,
                            const FxSlot& slot,
                            double ccTime,
                            int midiOutChannel = 1,
                            const std::array<InstrumentParams::MidiOutAssignment,
                                             InstrumentParams::kNumMidiOutLanes>* midiOutAssignments = nullptr)
{
    midiOutChannel = juce::jlimit (1, 16, midiOutChannel);

    switch (getSlotCommandLetter (slot))
    {
        case '!':
            midiSeq.addEvent (juce::MidiMessage::controllerEvent (1, kCcFxNoteReset, 0), ccTime);
            break;
        case 'r':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcSamplerDirection, slot.fxParam, ccTime);
            break;
        case 'p':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcSamplerPosition, slot.fxParam, ccTime);
            break;
        case 'U':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxTune, slot.fxParam, ccTime);
            break;
        case 'M':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxMicroTune, slot.fxParam, ccTime);
            break;
        case 'G':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxPortaSteps, slot.fxParam, ccTime);
            break;
        case 'P':
            midiSeq.addEvent (juce::MidiMessage::controllerEvent (
                                  1, 10, juce::jlimit (0, 127, static_cast<int> (std::lround (slot.fxParam * 127.0 / 100.0)))),
                              ccTime);
            break;
        case 's':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxDelaySend, slot.fxParam, ccTime);
            break;
        case 't':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxReverbSend, slot.fxParam, ccTime);
            break;
        case 'F':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxSlideUp, slot.fxParam, ccTime);
            break;
        case 'J':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxSlideDown, slot.fxParam, ccTime);
            break;
        case 'V':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxVolume, slot.fxParam, ccTime);
            break;
        case 'S':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcSamplerSlice, slot.fxParam, ccTime);
            break;
        case 'R':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcSamplerRetrigger, slot.fxParam, ccTime);
            break;
        case 'D':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxOverdrive, slot.fxParam, ccTime);
            break;
        case 'L':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxLowPass, slot.fxParam, ccTime);
            break;
        case 'B':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxBandPass, slot.fxParam, ccTime);
            break;
        case 'H':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxHighPass, slot.fxParam, ccTime);
            break;
        case 'E':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxBitDepth, slot.fxParam, ccTime);
            break;
        case 'g':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxVolumeLfoRate, slot.fxParam, ccTime);
            break;
        case 'h':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxPanLfoRate, slot.fxParam, ccTime);
            break;
        case 'j':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxFilterLfoRate, slot.fxParam, ccTime);
            break;
        case 'k':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxPositionLfoRate, slot.fxParam, ccTime);
            break;
        case 'l':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxFinetuneLfoRate, slot.fxParam, ccTime);
            break;
        case 'a':
        case 'b':
        case 'c':
        case 'd':
        case 'e':
        case 'f':
            if (const int laneIndex = getMidiOutLaneIndexForCommandLetter (getSlotCommandLetter (slot)); laneIndex >= 0)
            {
                const auto assignment = midiOutAssignments != nullptr
                                            ? (*midiOutAssignments)[static_cast<size_t> (laneIndex)]
                                            : InstrumentParams::makeDefaultMidiOutAssignment (laneIndex);
                const int value = juce::jlimit (0, 127, slot.fxParam);
                const int number = juce::jlimit (0, 127, assignment.number);

                switch (assignment.type)
                {
                    case InstrumentParams::MidiOutMessageType::ControlChange:
                        midiSeq.addEvent (juce::MidiMessage::controllerEvent (midiOutChannel, number, value), ccTime);
                        break;
                    case InstrumentParams::MidiOutMessageType::ProgramChange:
                        midiSeq.addEvent (juce::MidiMessage::programChange (midiOutChannel, value), ccTime);
                        break;
                    case InstrumentParams::MidiOutMessageType::ChannelPressure:
                        midiSeq.addEvent (juce::MidiMessage::channelPressureChange (midiOutChannel, value), ccTime);
                        break;
                    case InstrumentParams::MidiOutMessageType::PolyPressure:
                        midiSeq.addEvent (juce::MidiMessage::aftertouchChange (midiOutChannel, number, value), ccTime);
                        break;
                }
            }
            break;
        case 'm':
            // Note timing is handled by the pattern scheduler, not by MIDI CC state.
            break;
        case 'x':
            // Random FX value is resolved before FX are emitted.
            break;
        case 'X':
            // Plugin-instrument modulation triggers are handled from the UI row clock.
            break;
        case 'T':
            // Tempo is handled via master lane tempo points.
            break;
        default:
            break;
    }
}

void appendNoteSequence (juce::MidiMessageSequence& midiSeq,
                         int noteChannel,
                         const std::vector<int>& chordNotes,
                         const std::vector<int>& arpNotes,
                         int velocity,
                         double startSeconds,
                         double noteOffSeconds,
                         double rowEndSeconds)
{
    if (! arpNotes.empty())
    {
        const double arpEndSeconds = juce::jmax (startSeconds + 0.001,
                                                 std::min (noteOffSeconds, rowEndSeconds));
        const double sliceSeconds = juce::jmax (0.001,
                                                (arpEndSeconds - startSeconds)
                                                    / static_cast<double> (arpNotes.size()));

        for (int i = 0; i < static_cast<int> (arpNotes.size()); ++i)
        {
            const int note = arpNotes[static_cast<size_t> (i)];
            const double eventStart = startSeconds + static_cast<double> (i) * sliceSeconds;
            const double eventEnd = i + 1 < static_cast<int> (arpNotes.size())
                                        ? eventStart + sliceSeconds
                                        : arpEndSeconds;

            midiSeq.addEvent (juce::MidiMessage::noteOn (noteChannel, note, static_cast<juce::uint8> (velocity)),
                              eventStart);
            midiSeq.addEvent (juce::MidiMessage::noteOff (noteChannel, note),
                              InstrumentPlaybackTiming::getHandoffEventTime (
                                  InstrumentPlaybackTiming::ensureNoteEndAfterStartSeconds (
                                      eventStart, eventEnd, noteOffSeconds)));
        }
        return;
    }

    for (int chordNote : chordNotes)
        midiSeq.addEvent (juce::MidiMessage::noteOn (noteChannel, chordNote, static_cast<juce::uint8> (velocity)),
                          startSeconds);

    for (int chordNote : chordNotes)
        midiSeq.addEvent (juce::MidiMessage::noteOff (noteChannel, chordNote), noteOffSeconds);
}

bool appendRolledNoteSequence (juce::MidiMessageSequence& midiSeq,
                               int noteChannel,
                               const std::vector<int>& chordNotes,
                               const SamplePlaybackLayout::RollFx& roll,
                               int velocity,
                               double startSeconds,
                               double noteOffSeconds,
                               double rowEndSeconds,
                               int row,
                               int track,
                               int lane,
                               int repeatIndex)
{
    if (roll.divider <= 0 || chordNotes.empty())
        return false;

    const double rollEndSeconds = juce::jmax (startSeconds + 0.001,
                                              std::min (noteOffSeconds, rowEndSeconds));
    const double sliceSeconds = juce::jmax (0.001,
                                            (rollEndSeconds - startSeconds)
                                                / static_cast<double> (roll.divider));

    for (int i = 0; i < roll.divider; ++i)
    {
        const double eventStart = startSeconds + static_cast<double> (i) * sliceSeconds;
        const double eventEnd = i + 1 < roll.divider ? eventStart + sliceSeconds : rollEndSeconds;
        const int eventVelocity = StepFxResolver::resolveRollVelocity (velocity, roll.type, i, roll.divider);
        const int noteOffset = StepFxResolver::resolveRollNoteOffset (roll.type, i, roll.divider,
                                                                      row, track, lane, repeatIndex);

        for (int chordNote : chordNotes)
        {
            const int note = juce::jlimit (0, 127, chordNote + noteOffset);
            midiSeq.addEvent (juce::MidiMessage::noteOn (noteChannel, note,
                                                         static_cast<juce::uint8> (eventVelocity)),
                              eventStart);
            midiSeq.addEvent (juce::MidiMessage::noteOff (noteChannel, note),
                              InstrumentPlaybackTiming::getHandoffEventTime (
                                  InstrumentPlaybackTiming::ensureNoteEndAfterStartSeconds (
                                      eventStart, eventEnd, noteOffSeconds)));
        }
    }

    return true;
}

te::Plugin* findInsertPluginForSlot (te::AudioTrack& track, int slotIndex)
{
    if (slotIndex < 0)
        return nullptr;

    bool inInsertZone = false;
    int insertIdx = 0;

    auto& pluginList = track.pluginList;
    for (int i = 0; i < pluginList.size(); ++i)
    {
        auto* plugin = pluginList[i];
        if (dynamic_cast<ChannelStripPlugin*> (plugin) != nullptr)
        {
            inInsertZone = true;
            continue;
        }

        if (! inInsertZone)
            continue;

        if (dynamic_cast<TrackOutputPlugin*> (plugin) != nullptr)
            break;

        if (dynamic_cast<te::ExternalPlugin*> (plugin) == nullptr)
            continue;

        if (insertIdx == slotIndex)
            return plugin;

        ++insertIdx;
    }

    return nullptr;
}

te::Plugin* findMasterInsertPluginForSlot (te::PluginList& pluginList, int slotIndex)
{
    if (slotIndex < 0)
        return nullptr;

    bool pastSendEffects = false;
    int insertIdx = 0;

    for (int i = 0; i < pluginList.size(); ++i)
    {
        auto* plugin = pluginList[i];
        if (dynamic_cast<SendEffectsPlugin*> (plugin) != nullptr)
        {
            pastSendEffects = true;
            continue;
        }

        if (! pastSendEffects)
            continue;

        if (dynamic_cast<te::ExternalPlugin*> (plugin) == nullptr)
            continue;

        if (insertIdx == slotIndex)
            return plugin;

        ++insertIdx;
    }

    return nullptr;
}

int getMasterInsertAppendPosition (te::PluginList& pluginList)
{
    int insertPos = pluginList.size();
    bool pastSendEffects = false;

    for (int i = 0; i < pluginList.size(); ++i)
    {
        auto* plugin = pluginList[i];
        if (dynamic_cast<SendEffectsPlugin*> (plugin) != nullptr)
        {
            pastSendEffects = true;
            insertPos = i + 1;
            continue;
        }

        if (pastSendEffects && dynamic_cast<te::ExternalPlugin*> (plugin) != nullptr)
            insertPos = i + 1;
    }

    return insertPos;
}

bool shouldSuppressDirectOutputForGroupSolo (const MixerState* mixerState, int groupIndex)
{
    if (mixerState == nullptr)
        return false;

    bool anyGroupSoloed = false;
    for (const auto& groupState : mixerState->groupBuses)
    {
        if (groupState.soloed)
        {
            anyGroupSoloed = true;
            break;
        }
    }

    if (! anyGroupSoloed)
        return false;

    if (groupIndex < 0 || groupIndex >= kMaxGroupBuses)
        return true;

    return ! mixerState->groupBuses[static_cast<size_t> (groupIndex)].soloed;
}
} // namespace

struct TrackerEngine::TransportStopTimer : private juce::Timer
{
    explicit TransportStopTimer (TrackerEngine& ownerIn) : owner (ownerIn) {}

    void schedule (int delayMs)
    {
        startTimer (juce::jmax (1, delayMs));
    }

    void cancel()
    {
        stopTimer();
    }

    void timerCallback() override
    {
        stopTimer();
        owner.handleTransportStopTimer();
    }

    TrackerEngine& owner;
};

TrackerEngine::TrackerEngine()
{
    currentTrackInstrument.fill (-1);
    transportStopTimer = std::make_unique<TransportStopTimer> (*this);
}

TrackerEngine::~TrackerEngine()
{
    cancelTransportStop();
    stopTimer();

    if (edit != nullptr)
    {
        auto& transport = edit->getTransport();
        transport.removeChangeListener (this);

        if (transport.isPlaying())
            transport.stop (false, false);
    }

    // Release plugin references while Edit is still alive to avoid dangling
    // access to ParameterChangeHandler mutexes during destruction.
    pluginInstrumentEditorWindows.clear();
    pluginEditorWindows.clear();
    pluginInstrumentInstances.clear();

    sendEffectsPlugin = nullptr;
    edit = nullptr;
    pluginCatalog = nullptr;
    engine = nullptr;
}

void TrackerEngine::initialise()
{
    engine = std::make_unique<te::Engine> ("VCTracker");

    // Register custom plugin types
    engine->getPluginManager().createBuiltInType<InstrumentEffectsPlugin>();
    engine->getPluginManager().createBuiltInType<TrackerSamplerPlugin>();
    engine->getPluginManager().createBuiltInType<MetronomePlugin>();
    engine->getPluginManager().createBuiltInType<SendEffectsPlugin>();
    engine->getPluginManager().createBuiltInType<MixerPlugin>();
    engine->getPluginManager().createBuiltInType<ChannelStripPlugin>();
    engine->getPluginManager().createBuiltInType<TrackOutputPlugin>();

    // Create plugin catalog service
    pluginCatalog = std::make_unique<PluginCatalogService> (*engine);

    // Create an edit
    auto editFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("VCTracker")
                        .getChildFile ("session.tracktionedit");
    editFile.getParentDirectory().createDirectory();

    edit = te::createEmptyEdit (*engine, editFile);
    edit->playInStopEnabled = true;

    // Create 16 audio tracks + 1 preview track + 1 metronome track + 1 send effects bus track
    edit->ensureNumberOfAudioTracks (kNumTracks + 3);

    // Set up the metronome track with MetronomePlugin
    if (auto* metroTrack = getTrack (kMetronomeTrack))
    {
        if (auto plugin = dynamic_cast<MetronomePlugin*> (
                edit->getPluginCache().createNewPlugin (MetronomePlugin::xmlTypeName, {}).get()))
        {
            metroTrack->pluginList.insertPlugin (*plugin, 0, nullptr);
        }
    }

    // Set up the send effects bus track
    setupSendEffectsTrack();

    // Listen for transport changes
    edit->getTransport().addChangeListener (this);

    // Ensure playback context
    edit->getTransport().ensureContextAllocated();
}

void TrackerEngine::rebuildTempoSequenceFromPatternMasterLane (const Pattern& pattern)
{
    if (edit == nullptr)
        return;

    auto& tempoSequence = edit->tempoSequence;
    while (tempoSequence.getNumTempos() > 1)
        tempoSequence.removeTempo (tempoSequence.getNumTempos() - 1, false);

    tempoSequence.getTempos()[0]->setBpm (baseBpm);

    std::map<double, int> tempoPoints;
    for (int row = 0; row < pattern.numRows; ++row)
    {
        int tempoPercent = getRowTempoCommand (pattern, row);
        if (tempoPercent <= 0)
            continue;

        double beat = static_cast<double> (row) / static_cast<double> (rowsPerBeat);
        tempoPoints[beat] = tempoPercent;
    }

    for (const auto& [beat, tempoPercent] : tempoPoints)
    {
        const double bpm = tempoPercentToBpm (baseBpm, tempoPercent);
        if (beat <= 0.0)
            tempoSequence.getTempos()[0]->setBpm (bpm);
        else
            tempoSequence.insertTempo (te::BeatPosition::fromBeats (beat), bpm, 0.0f);
    }

    if (sendEffectsPlugin != nullptr)
        sendEffectsPlugin->setTempoBpm (tempoSequence.getTempos()[0]->getBpm());
}

void TrackerEngine::rebuildTempoSequenceFromArrangementMasterLane (const std::vector<std::pair<const Pattern*, int>>& sequence, int rpb)
{
    if (edit == nullptr)
        return;

    auto& tempoSequence = edit->tempoSequence;
    while (tempoSequence.getNumTempos() > 1)
        tempoSequence.removeTempo (tempoSequence.getNumTempos() - 1, false);

    tempoSequence.getTempos()[0]->setBpm (baseBpm);

    std::map<double, int> tempoPoints;
    double beatOffset = 0.0;

    for (const auto& [pattern, repeats] : sequence)
    {
        if (pattern == nullptr)
            continue;

        const double patternLengthBeats = static_cast<double> (pattern->numRows) / static_cast<double> (rpb);

        for (int rep = 0; rep < repeats; ++rep)
        {
            for (int row = 0; row < pattern->numRows; ++row)
            {
                int tempoPercent = getRowTempoCommand (*pattern, row);
                if (tempoPercent <= 0)
                    continue;

                double beat = beatOffset + static_cast<double> (row) / static_cast<double> (rpb);
                tempoPoints[beat] = tempoPercent;
            }

            beatOffset += patternLengthBeats;
        }
    }

    for (const auto& [beat, tempoPercent] : tempoPoints)
    {
        const double bpm = tempoPercentToBpm (baseBpm, tempoPercent);
        if (beat <= 0.0)
            tempoSequence.getTempos()[0]->setBpm (bpm);
        else
            tempoSequence.insertTempo (te::BeatPosition::fromBeats (beat), bpm, 0.0f);
    }

    if (sendEffectsPlugin != nullptr)
        sendEffectsPlugin->setTempoBpm (tempoSequence.getTempos()[0]->getBpm());
}

void TrackerEngine::syncPatternToEdit (const Pattern& pattern,
                                       const std::array<bool, kNumTracks>& releaseMode)
{
    if (edit == nullptr)
        return;

    transportStopBeat = findPatternTempoStopBeat (pattern, rowsPerBeat, 0.0);
    cancelTransportStop();
    rebuildTempoSequenceFromPatternMasterLane (pattern);

    // Ensure correct instruments are loaded on each track
    prepareTracksForPattern (pattern);

    auto tracks = te::getAudioTracks (*edit);

    for (int trackIdx = 0; trackIdx < kNumTracks && trackIdx < tracks.size(); ++trackIdx)
    {
        auto* track = tracks[trackIdx];

        // Remove existing clips
        auto clips = track->getClips();
        for (int i = clips.size(); --i >= 0;)
            clips.getUnchecked (i)->removeFromParent();

        // Calculate pattern length in beats
        double patternLengthBeats = static_cast<double> (pattern.numRows) / static_cast<double> (rowsPerBeat);

        // Convert beats to time using the tempo sequence
        auto endTime = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (patternLengthBeats));
        auto startTime = te::TimePosition::fromSeconds (0.0);

        te::TimeRange timeRange { startTime, endTime };

        // Create MIDI clip
        auto midiClip = track->insertMIDIClip ("Pattern", timeRange, nullptr);
        if (midiClip == nullptr)
            continue;

        // Build MIDI sequence from pattern data (all note lanes)
        juce::MidiMessageSequence midiSeq;

        const bool isPluginInstrumentTrack = getTrackContentMode (trackIdx) == TrackContentMode::PluginInstrument;
        const bool isKill = ! releaseMode[static_cast<size_t> (trackIdx)];

        // Determine how many note lanes this track has
        int numNoteLanes = 1;
        for (int row = 0; row < pattern.numRows; ++row)
        {
            int nl = pattern.getCell (row, trackIdx).getNumNoteLanes();
            if (nl > numNoteLanes)
                numNoteLanes = nl;
        }

        // Process FX slots (shared across all note lanes, emitted once per row)
        // Also collect per-lane portamento state
        std::vector<int> laneActivePortaSteps (static_cast<size_t> (numNoteLanes), 0);

        for (int row = 0; row < pattern.numRows; ++row)
        {
            const auto& cell = pattern.getCell (row, trackIdx);
            const auto rowFxSlots = StepFxResolver::resolveFxSlots (cell, row, trackIdx, 0, 0);
            double startBeat = static_cast<double> (row) / static_cast<double> (rowsPerBeat);
            const double patternEndBeat = static_cast<double> (pattern.numRows) / static_cast<double> (rowsPerBeat);
            const double rowEndBeat = std::min (
                startBeat + 1.0 / static_cast<double> (rowsPerBeat), patternEndBeat);
            auto rowTime = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (startBeat));
            const double rowSeconds = rowTime.inSeconds();
            const double rowEndSeconds = edit->tempoSequence
                .toTime (te::BeatPosition::fromBeats (rowEndBeat)).inSeconds();
            const int swingPercent = getPatternSwingPercentAtRow (pattern, row, 0);
            const double noteEventSeconds = getStepEventSeconds (
                rowSeconds, rowEndSeconds, row, swingPercent, rowFxSlots);

            // Check if any lane has an allowed note for FX reset and shared FX.
            bool anyLaneHasCandidateNote = false;
            bool anyLaneHasAllowedNote = false;
            for (int nl = 0; nl < numNoteLanes; ++nl)
            {
                auto slot = cell.getNoteLane (nl);
                if (slot.note >= 0)
                {
                    const auto laneFxSlots = nl == 0
                                                 ? rowFxSlots
                                                 : StepFxResolver::resolveFxSlots (cell, row, trackIdx, nl, 0);
                    anyLaneHasCandidateNote = true;
                    if (StepFxResolver::chanceAllowsStep (laneFxSlots, row, trackIdx, nl, 0))
                        anyLaneHasAllowedNote = true;
                }
            }

            if (anyLaneHasAllowedNote)
            {
                const double resetTime = juce::jmax (0.0, noteEventSeconds - 0.00008);
                midiSeq.addEvent (juce::MidiMessage::controllerEvent (1, kCcFxNoteReset, 0), resetTime);
            }

            const bool rowAllowsSharedFx = anyLaneHasCandidateNote
                                               ? anyLaneHasAllowedNote
                                               : StepFxResolver::chanceAllowsStep (rowFxSlots, row, trackIdx, 0, 0);
            const auto midiOutRoute = getMidiOutRouteForCell (cell, numNoteLanes, sampler);

            // Process FX slots (shared)
            for (const auto& fxSlot : rowFxSlots)
            {
                if (fxSlot.isEmpty())
                    continue;

                const auto letter = getSlotCommandLetter (fxSlot);
                if (letter == '\0')
                    continue;

                if (! rowAllowsSharedFx)
                    continue;
                if (isPluginInstrumentTrack && letter == 'R')
                    continue;

                if (letter == 'G' && fxSlot.fxParam > 0)
                {
                    // Apply portamento to all lanes
                    for (auto& ps : laneActivePortaSteps)
                        ps = fxSlot.fxParam;
                }

                const double fxBaseSeconds = anyLaneHasAllowedNote ? std::min (rowSeconds, noteEventSeconds)
                                                                   : rowSeconds;
                const double ccTime = juce::jmax (0.0, fxBaseSeconds - 0.00005);
                appendSymbolicTrackFx (midiSeq, fxSlot, ccTime, midiOutRoute.channel, &midiOutRoute.assignments);
            }
        }

        // Per-lane note generation
        for (int laneIdx = 0; laneIdx < numNoteLanes; ++laneIdx)
        {
            const int noteChannel = isPluginInstrumentTrack ? 1 : juce::jlimit (1, 16, laneIdx + 1);
            int lastPlayingNote = -1;
            int currentInst = -1;
            int activePortaSteps = 0;

            for (int row = 0; row < pattern.numRows; ++row)
            {
                const auto& cell = pattern.getCell (row, trackIdx);
                const auto resolvedFxSlots = StepFxResolver::resolveFxSlots (cell, row, trackIdx, laneIdx, 0);
                auto noteSlot = cell.getNoteLane (laneIdx);

                double startBeat = static_cast<double> (row) / static_cast<double> (rowsPerBeat);
                const double patternEndBeat = static_cast<double> (pattern.numRows) / static_cast<double> (rowsPerBeat);
                const double rowEndBeat = std::min (
                    startBeat + 1.0 / static_cast<double> (rowsPerBeat), patternEndBeat);
                auto rowTime = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (startBeat));
                const double rowSeconds = rowTime.inSeconds();
                const double rowEndSeconds = edit->tempoSequence
                    .toTime (te::BeatPosition::fromBeats (rowEndBeat)).inSeconds();
                const int swingPercent = getPatternSwingPercentAtRow (pattern, row, 0);
                const double noteEventSeconds = getStepEventSeconds (
                    rowSeconds, rowEndSeconds, row, swingPercent, resolvedFxSlots);

                // Check FX for portamento (shared FX affects all lanes)
                for (const auto& fxSlot : resolvedFxSlots)
                {
                    if (fxSlot.isEmpty()) continue;
                    const auto letter = getSlotCommandLetter (fxSlot);
                    if (letter == 'G' && fxSlot.fxParam > 0)
                        activePortaSteps = fxSlot.fxParam;
                }

                const bool rowHasPorta = (activePortaSteps > 0);

                if (noteSlot.note < 0)
                    continue;

                if (! StepFxResolver::chanceAllowsStep (resolvedFxSlots, row, trackIdx, laneIdx, 0))
                    continue;

                noteSlot = StepFxResolver::resolveNoteSlot (noteSlot, resolvedFxSlots, row, trackIdx, laneIdx, 0);

                // OFF (255)
                if (noteSlot.note == 255)
                {
                    const double offTime = InstrumentPlaybackTiming::getHandoffEventTime (noteEventSeconds);
                    if (lastPlayingNote >= 0)
                        midiSeq.addEvent (juce::MidiMessage::noteOff (noteChannel, lastPlayingNote), offTime);
                    else
                        midiSeq.addEvent (juce::MidiMessage::allNotesOff (noteChannel), offTime);
                    lastPlayingNote = -1;
                    activePortaSteps = 0;
                    continue;
                }

                // KILL (254)
                if (noteSlot.note == 254)
                {
                    const double killTime = InstrumentPlaybackTiming::getHandoffEventTime (noteEventSeconds);
                    if (isPluginInstrumentTrack)
                    {
                        midiSeq.addEvent (juce::MidiMessage::allSoundOff (noteChannel), killTime);
                    }
                    else
                    {
                        midiSeq.addEvent (juce::MidiMessage::controllerEvent (noteChannel, kCcSamplerHardCut, 0),
                                          killTime);
                        midiSeq.addEvent (lastPlayingNote >= 0
                                              ? juce::MidiMessage::noteOff (noteChannel, lastPlayingNote)
                                              : juce::MidiMessage::allNotesOff (noteChannel),
                                          killTime);
                    }
                    lastPlayingNote = -1;
                    activePortaSteps = 0;
                    continue;
                }

                // Portamento
                if (rowHasPorta && lastPlayingNote >= 0)
                {
                    midiSeq.addEvent (juce::MidiMessage::controllerEvent (noteChannel, 28, noteSlot.note & 0x7F),
                                      noteEventSeconds);
                    if (noteSlot.volume >= 0)
                        midiSeq.addEvent (juce::MidiMessage::controllerEvent (noteChannel, 7, noteSlot.volume),
                                          juce::jmax (0.0, noteEventSeconds - 0.00003));
                    activePortaSteps = 0;
                    continue;
                }

                // Program change (sample instruments only — plugin presets are
                // managed via the plugin state, not MIDI program changes)
                if (noteSlot.instrument >= 0 && noteSlot.instrument != currentInst
                    && getTrackContentMode (trackIdx) != TrackContentMode::PluginInstrument)
                {
                    currentInst = InstrumentRouting::clampInstrumentIndex (noteSlot.instrument);
                    const double bankTime = juce::jmax (0.0, noteEventSeconds - 0.00012);
                    const double progTime = juce::jmax (0.0, noteEventSeconds - 0.0001);
                    midiSeq.addEvent (juce::MidiMessage::controllerEvent (noteChannel, 0,
                                      InstrumentRouting::getBankMsbForInstrument (currentInst)), bankTime);
                    midiSeq.addEvent (juce::MidiMessage::programChange (noteChannel,
                                      InstrumentRouting::getProgramForInstrument (currentInst)), progTime);
                }

                // Notes sustain until the next trigger in this lane or the
                // pattern end. Kill/release only changes sample handoff style.
                double nextTriggerSeconds = -1.0;
                bool nextTriggerIsNormalNote = false;
                for (int nextRow = row + 1; nextRow < pattern.numRows; ++nextRow)
                {
                    const auto& nextCell = pattern.getCell (nextRow, trackIdx);
                    const auto nextFxSlots = StepFxResolver::resolveFxSlots (nextCell, nextRow, trackIdx, laneIdx, 0);
                    auto nextSlot = nextCell.getNoteLane (laneIdx);
                    if (nextSlot.note >= 0
                        && StepFxResolver::chanceAllowsStep (nextFxSlots, nextRow, trackIdx, laneIdx, 0))
                    {
                        nextSlot = StepFxResolver::resolveNoteSlot (nextSlot, nextFxSlots, nextRow, trackIdx, laneIdx, 0);
                        bool nextIsPorta = false;
                        if (nextSlot.note < 254)
                        {
                            for (const auto& ns : nextFxSlots)
                            {
                                if (getSlotCommandLetter (ns) == 'G' && ns.fxParam > 0)
                                    nextIsPorta = true;
                            }
                        }
                        if (nextIsPorta)
                            continue;
                        const double nextBeat = static_cast<double> (nextRow) / static_cast<double> (rowsPerBeat);
                        const double nextRowEndBeat = std::min (
                            nextBeat + 1.0 / static_cast<double> (rowsPerBeat), patternEndBeat);
                        const double nextRowSeconds = edit->tempoSequence
                            .toTime (te::BeatPosition::fromBeats (nextBeat)).inSeconds();
                        const double nextRowEndSeconds = edit->tempoSequence
                            .toTime (te::BeatPosition::fromBeats (nextRowEndBeat)).inSeconds();
                        const int nextSwingPercent = getPatternSwingPercentAtRow (pattern, nextRow, 0);
                        nextTriggerSeconds = getStepEventSeconds (
                            nextRowSeconds, nextRowEndSeconds, nextRow, nextSwingPercent, nextFxSlots);
                        nextTriggerIsNormalNote = nextSlot.note < 128;
                        break;
                    }
                }
                const double patternEndSeconds = endTime.inSeconds();
                const double endSeconds = InstrumentPlaybackTiming::chooseNoteEndSeconds (
                    isKill,
                    rowEndSeconds,
                    nextTriggerSeconds,
                    patternEndSeconds);
                double noteEndSeconds = InstrumentPlaybackTiming::ensureNoteEndAfterStartSeconds (
                    noteEventSeconds, endSeconds, patternEndSeconds);
                const int gatePercent = StepFxResolver::getPercentFxParam (resolvedFxSlots, 'q');
                if (gatePercent >= 0)
                    noteEndSeconds = InstrumentPlaybackTiming::applyGateLengthSeconds (
                        noteEventSeconds, rowEndSeconds, noteEndSeconds, patternEndSeconds, gatePercent);

                int velocity = noteSlot.volume >= 0 ? noteSlot.volume : 127;
                const auto chordNotes = StepFxResolver::resolveChordNotes (noteSlot.note, resolvedFxSlots);
                const auto arpNotes = StepFxResolver::resolveArpNotes (
                    noteSlot.note, resolvedFxSlots, row, trackIdx, laneIdx, 0);

                const bool hardCutSampleAtEnd = InstrumentPlaybackTiming::shouldSendHardCutAtNoteHandoff (
                    isKill, isPluginInstrumentTrack, nextTriggerIsNormalNote);
                if (hardCutSampleAtEnd)
                    midiSeq.addEvent (juce::MidiMessage::controllerEvent (noteChannel, kCcSamplerHardCut, 0),
                                      InstrumentPlaybackTiming::getHardCutEventTime (noteEndSeconds));

                const double noteOffSeconds = nextTriggerSeconds >= 0.0
                    ? InstrumentPlaybackTiming::getHandoffEventTime (noteEndSeconds)
                    : noteEndSeconds;
                const auto rollFx = StepFxResolver::getRollFx (resolvedFxSlots);
                if (! isPluginInstrumentTrack
                    || ! arpNotes.empty()
                    || ! appendRolledNoteSequence (midiSeq, noteChannel, chordNotes, rollFx, velocity,
                                                   noteEventSeconds, noteOffSeconds, rowEndSeconds,
                                                   row, trackIdx, laneIdx, 0))
                {
                    appendNoteSequence (midiSeq, noteChannel, chordNotes, arpNotes, velocity,
                                        noteEventSeconds, noteOffSeconds, rowEndSeconds);
                }

                lastPlayingNote = chordNotes.size() == 1 && arpNotes.empty() ? noteSlot.note : -1;
                activePortaSteps = 0;
            }
        }

        midiSeq.updateMatchedPairs();
        midiClip->mergeInMidiSequence (midiSeq, te::MidiList::NoteAutomationType::none);
    }

    // Apply plugin automation from pattern data (Phase 5)
    applyPatternAutomation (pattern.getAutomationData(), pattern.numRows, rowsPerBeat);

    refreshTransportLoopRangeFromClip();
    scheduleTransportStopFromCurrentPosition();
}

void TrackerEngine::syncArrangementToEdit (const std::vector<std::pair<const Pattern*, int>>& sequence, int rpb,
                                            const std::array<bool, kNumTracks>& releaseMode)
{
    if (edit == nullptr)
        return;

    transportStopBeat = -1.0;
    cancelTransportStop();
    if (sequence.empty())
    {
        refreshTransportLoopRangeFromClip();
        return;
    }

    transportStopBeat = findArrangementTempoStopBeat (sequence, rpb);
    rebuildTempoSequenceFromArrangementMasterLane (sequence, rpb);

    // Prepare instruments once across the full arrangement so program changes can
    // switch to any instrument used by any pattern in the sequence.
    std::array<std::vector<int>, kNumTracks> instrumentsByTrack {};
    for (auto& [pattern, repeats] : sequence)
    {
        juce::ignoreUnused (repeats);
        for (int t = 0; t < kNumTracks; ++t)
        {
            auto& trackInstruments = instrumentsByTrack[static_cast<size_t> (t)];
            for (int row = 0; row < pattern->numRows; ++row)
            {
                const auto& cell = pattern->getCell (row, t);
                int numLanes = cell.getNumNoteLanes();
                for (int nl = 0; nl < numLanes; ++nl)
                {
                    int inst = cell.getNoteLane (nl).instrument;
                    if (inst >= 0
                        && std::find (trackInstruments.begin(), trackInstruments.end(), inst) == trackInstruments.end())
                    {
                        trackInstruments.push_back (inst);
                    }
                }
            }
        }
    }
    prepareTracksForInstrumentUsage (instrumentsByTrack);

    auto tracks = te::getAudioTracks (*edit);

    // Calculate total length in beats
    double totalBeats = 0.0;
    for (auto& [pattern, repeats] : sequence)
        totalBeats += (static_cast<double> (pattern->numRows) / static_cast<double> (rpb)) * repeats;

    auto totalEndTime = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (totalBeats));
    auto startTime = te::TimePosition::fromSeconds (0.0);
    te::TimeRange fullRange { startTime, totalEndTime };

    for (int trackIdx = 0; trackIdx < kNumTracks && trackIdx < tracks.size(); ++trackIdx)
    {
        auto* track = tracks[trackIdx];

        // Remove existing clips
        auto clips = track->getClips();
        for (int i = clips.size(); --i >= 0;)
            clips.getUnchecked (i)->removeFromParent();

        // Create one long MIDI clip spanning all entries
        auto midiClip = track->insertMIDIClip ("Arrangement", fullRange, nullptr);
        if (midiClip == nullptr)
            continue;

        juce::MidiMessageSequence midiSeq;
        const bool isPluginInstrumentTrack = getTrackContentMode (trackIdx) == TrackContentMode::PluginInstrument;
        const bool isKill = ! releaseMode[static_cast<size_t> (trackIdx)];

        // Determine how many note lanes this track has across all patterns
        int numNoteLanes = 1;
        for (auto& [pat, reps] : sequence)
        {
            juce::ignoreUnused (reps);
            for (int row = 0; row < pat->numRows; ++row)
            {
                int nl = pat->getCell (row, trackIdx).getNumNoteLanes();
                if (nl > numNoteLanes)
                    numNoteLanes = nl;
            }
        }

        // First pass: process FX slots and note resets (shared across all lanes)
        {
            double beatOffset = 0.0;
            for (auto& [pattern, repeats] : sequence)
            {
                double patternLengthBeats = static_cast<double> (pattern->numRows) / static_cast<double> (rpb);

                for (int rep = 0; rep < repeats; ++rep)
                {
                    for (int row = 0; row < pattern->numRows; ++row)
                    {
                        const auto& cell = pattern->getCell (row, trackIdx);
                        const auto rowFxSlots = StepFxResolver::resolveFxSlots (cell, row, trackIdx, 0, rep);
                        double startBeat = beatOffset + static_cast<double> (row) / static_cast<double> (rpb);
                        const double rowEndBeat = std::min (
                            startBeat + 1.0 / static_cast<double> (rpb), beatOffset + patternLengthBeats);
                        auto rowTime = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (startBeat));
                        const double rowSeconds = rowTime.inSeconds();
                        const double rowEndSeconds = edit->tempoSequence
                            .toTime (te::BeatPosition::fromBeats (rowEndBeat)).inSeconds();
                        const int swingPercent = getPatternSwingPercentAtRow (*pattern, row, rep);
                        const double noteEventSeconds = getStepEventSeconds (
                            rowSeconds, rowEndSeconds, row, swingPercent, rowFxSlots);

                        // Check if any lane has an allowed note for FX reset and shared FX.
                        bool anyLaneHasCandidateNote = false;
                        bool anyLaneHasAllowedNote = false;
                        for (int nl = 0; nl < numNoteLanes; ++nl)
                        {
                            auto slot = cell.getNoteLane (nl);
                            if (slot.note >= 0)
                            {
                                const auto laneFxSlots = nl == 0
                                                             ? rowFxSlots
                                                             : StepFxResolver::resolveFxSlots (cell, row, trackIdx, nl, rep);
                                anyLaneHasCandidateNote = true;
                                if (StepFxResolver::chanceAllowsStep (laneFxSlots, row, trackIdx, nl, rep))
                                    anyLaneHasAllowedNote = true;
                            }
                        }

                        if (anyLaneHasAllowedNote)
                        {
                            const double resetTime = juce::jmax (0.0, noteEventSeconds - 0.00008);
                            midiSeq.addEvent (juce::MidiMessage::controllerEvent (1, kCcFxNoteReset, 0), resetTime);
                        }

                        const bool rowAllowsSharedFx = anyLaneHasCandidateNote
                                                           ? anyLaneHasAllowedNote
                                                           : StepFxResolver::chanceAllowsStep (rowFxSlots, row, trackIdx, 0, rep);
                        const auto midiOutRoute = getMidiOutRouteForCell (cell, numNoteLanes, sampler);

                        // Process FX slots (shared across all note lanes)
                        for (const auto& slot : rowFxSlots)
                        {
                            if (slot.isEmpty())
                                continue;

                            const auto letter = getSlotCommandLetter (slot);
                            if (letter == '\0')
                                continue;

                            if (! rowAllowsSharedFx)
                                continue;
                            if (isPluginInstrumentTrack && letter == 'R')
                                continue;

                            const double fxBaseSeconds = anyLaneHasAllowedNote ? std::min (rowSeconds, noteEventSeconds)
                                                                               : rowSeconds;
                            const double ccTime = juce::jmax (0.0, fxBaseSeconds - 0.00005);
                            appendSymbolicTrackFx (midiSeq, slot, ccTime, midiOutRoute.channel, &midiOutRoute.assignments);
                        }
                    }

                    beatOffset += patternLengthBeats;
                }
            }
        }

        // Per-lane note generation (mirrors syncPatternToEdit approach)
        for (int laneIdx = 0; laneIdx < numNoteLanes; ++laneIdx)
        {
            const int noteChannel = isPluginInstrumentTrack ? 1 : juce::jlimit (1, 16, laneIdx + 1);
            int lastPlayingNote = -1;
            int currentInst = -1;
            int activePortaSteps = 0;
            double beatOffset = 0.0;

            for (auto& [pattern, repeats] : sequence)
            {
                double patternLengthBeats = static_cast<double> (pattern->numRows) / static_cast<double> (rpb);

                for (int rep = 0; rep < repeats; ++rep)
                {
                    for (int row = 0; row < pattern->numRows; ++row)
                    {
                        const auto& cell = pattern->getCell (row, trackIdx);
                        const auto resolvedFxSlots = StepFxResolver::resolveFxSlots (cell, row, trackIdx, laneIdx, rep);
                        auto noteSlot = cell.getNoteLane (laneIdx);

                        double startBeat = beatOffset + static_cast<double> (row) / static_cast<double> (rpb);
                        const double repeatEndBeat = beatOffset + patternLengthBeats;
                        const double rowEndBeat = std::min (
                            startBeat + 1.0 / static_cast<double> (rpb), repeatEndBeat);
                        auto rowTime = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (startBeat));
                        const double rowSeconds = rowTime.inSeconds();
                        const double rowEndSeconds = edit->tempoSequence
                            .toTime (te::BeatPosition::fromBeats (rowEndBeat)).inSeconds();
                        const int swingPercent = getPatternSwingPercentAtRow (*pattern, row, rep);
                        const double noteEventSeconds = getStepEventSeconds (
                            rowSeconds, rowEndSeconds, row, swingPercent, resolvedFxSlots);

                        // Check FX for portamento (shared FX affects all lanes)
                        for (const auto& fxSlot : resolvedFxSlots)
                        {
                            if (fxSlot.isEmpty()) continue;
                            const auto letter = getSlotCommandLetter (fxSlot);
                            if (letter == 'G' && fxSlot.fxParam > 0)
                                activePortaSteps = fxSlot.fxParam;
                        }

                        const bool rowHasPorta = (activePortaSteps > 0);

                        if (noteSlot.note < 0)
                            continue;

                        if (! StepFxResolver::chanceAllowsStep (resolvedFxSlots, row, trackIdx, laneIdx, rep))
                            continue;

                        noteSlot = StepFxResolver::resolveNoteSlot (noteSlot, resolvedFxSlots, row, trackIdx, laneIdx, rep);

                        // OFF (255)
                        if (noteSlot.note == 255)
                        {
                            const double offTime = InstrumentPlaybackTiming::getHandoffEventTime (noteEventSeconds);
                            if (lastPlayingNote >= 0)
                                midiSeq.addEvent (juce::MidiMessage::noteOff (noteChannel, lastPlayingNote), offTime);
                            else
                                midiSeq.addEvent (juce::MidiMessage::allNotesOff (noteChannel), offTime);
                            lastPlayingNote = -1;
                            activePortaSteps = 0;
                            continue;
                        }

                        // KILL (254)
                        if (noteSlot.note == 254)
                        {
                            const double killTime = InstrumentPlaybackTiming::getHandoffEventTime (noteEventSeconds);
                            if (isPluginInstrumentTrack)
                            {
                                midiSeq.addEvent (juce::MidiMessage::allSoundOff (noteChannel), killTime);
                            }
                            else
                            {
                                midiSeq.addEvent (juce::MidiMessage::controllerEvent (noteChannel, kCcSamplerHardCut, 0),
                                                  killTime);
                                midiSeq.addEvent (lastPlayingNote >= 0
                                                      ? juce::MidiMessage::noteOff (noteChannel, lastPlayingNote)
                                                      : juce::MidiMessage::allNotesOff (noteChannel),
                                                  killTime);
                            }
                            lastPlayingNote = -1;
                            activePortaSteps = 0;
                            continue;
                        }

                        // Portamento
                        if (rowHasPorta && lastPlayingNote >= 0)
                        {
                            midiSeq.addEvent (juce::MidiMessage::controllerEvent (noteChannel, 28, noteSlot.note & 0x7F),
                                              noteEventSeconds);
                            if (noteSlot.volume >= 0)
                                midiSeq.addEvent (juce::MidiMessage::controllerEvent (noteChannel, 7, noteSlot.volume),
                                                  juce::jmax (0.0, noteEventSeconds - 0.00003));
                            activePortaSteps = 0;
                            continue;
                        }

                        // Program change (sample instruments only)
                        if (noteSlot.instrument >= 0 && noteSlot.instrument != currentInst
                            && getTrackContentMode (trackIdx) != TrackContentMode::PluginInstrument)
                        {
                            currentInst = InstrumentRouting::clampInstrumentIndex (noteSlot.instrument);
                            const double bankTime = juce::jmax (0.0, noteEventSeconds - 0.00012);
                            const double progTime = juce::jmax (0.0, noteEventSeconds - 0.0001);
                            midiSeq.addEvent (juce::MidiMessage::controllerEvent (noteChannel, 0,
                                              InstrumentRouting::getBankMsbForInstrument (currentInst)), bankTime);
                            midiSeq.addEvent (juce::MidiMessage::programChange (noteChannel,
                                              InstrumentRouting::getProgramForInstrument (currentInst)), progTime);
                        }

                        // Notes sustain until the next trigger in this lane or
                        // repeat end. Kill/release only changes sample handoff style.
                        double nextTriggerSeconds = -1.0;
                        bool nextTriggerIsNormalNote = false;
                        for (int nextRow = row + 1; nextRow < pattern->numRows; ++nextRow)
                        {
                            const auto& nextCell = pattern->getCell (nextRow, trackIdx);
                            const auto nextFxSlots = StepFxResolver::resolveFxSlots (nextCell, nextRow, trackIdx, laneIdx, rep);
                            auto nextSlot = nextCell.getNoteLane (laneIdx);
                            if (nextSlot.note >= 0
                                && StepFxResolver::chanceAllowsStep (nextFxSlots, nextRow, trackIdx, laneIdx, rep))
                            {
                                nextSlot = StepFxResolver::resolveNoteSlot (nextSlot, nextFxSlots, nextRow, trackIdx, laneIdx, rep);
                                bool nextIsPorta = false;
                                if (nextSlot.note < 254)
                                {
                                    for (const auto& ns : nextFxSlots)
                                    {
                                        if (getSlotCommandLetter (ns) == 'G' && ns.fxParam > 0)
                                            nextIsPorta = true;
                                    }
                                }
                                if (nextIsPorta)
                                    continue;
                                const double nextBeat = beatOffset + static_cast<double> (nextRow) / static_cast<double> (rpb);
                                const double nextRowEndBeat = std::min (
                                    nextBeat + 1.0 / static_cast<double> (rpb), repeatEndBeat);
                                const double nextRowSeconds = edit->tempoSequence
                                    .toTime (te::BeatPosition::fromBeats (nextBeat)).inSeconds();
                                const double nextRowEndSeconds = edit->tempoSequence
                                    .toTime (te::BeatPosition::fromBeats (nextRowEndBeat)).inSeconds();
                                const int nextSwingPercent = getPatternSwingPercentAtRow (*pattern, nextRow, rep);
                                nextTriggerSeconds = getStepEventSeconds (
                                    nextRowSeconds, nextRowEndSeconds, nextRow, nextSwingPercent, nextFxSlots);
                                nextTriggerIsNormalNote = nextSlot.note < 128;
                                break;
                            }
                        }

                        const double repeatEndSeconds = edit->tempoSequence
                            .toTime (te::BeatPosition::fromBeats (repeatEndBeat)).inSeconds();
                        const double endSeconds = InstrumentPlaybackTiming::chooseNoteEndSeconds (
                            isKill,
                            rowEndSeconds,
                            nextTriggerSeconds,
                            repeatEndSeconds);
                        double noteEndSeconds = InstrumentPlaybackTiming::ensureNoteEndAfterStartSeconds (
                            noteEventSeconds, endSeconds, repeatEndSeconds);
                        const int gatePercent = StepFxResolver::getPercentFxParam (resolvedFxSlots, 'q');
                        if (gatePercent >= 0)
                            noteEndSeconds = InstrumentPlaybackTiming::applyGateLengthSeconds (
                                noteEventSeconds, rowEndSeconds, noteEndSeconds, repeatEndSeconds, gatePercent);

                        int velocity = noteSlot.volume >= 0 ? noteSlot.volume : 127;
                        const auto chordNotes = StepFxResolver::resolveChordNotes (noteSlot.note, resolvedFxSlots);
                        const auto arpNotes = StepFxResolver::resolveArpNotes (
                            noteSlot.note, resolvedFxSlots, row, trackIdx, laneIdx, rep);

                        const bool hardCutSampleAtEnd = InstrumentPlaybackTiming::shouldSendHardCutAtNoteHandoff (
                            isKill, isPluginInstrumentTrack, nextTriggerIsNormalNote);
                        if (hardCutSampleAtEnd)
                            midiSeq.addEvent (juce::MidiMessage::controllerEvent (noteChannel, kCcSamplerHardCut, 0),
                                              InstrumentPlaybackTiming::getHardCutEventTime (noteEndSeconds));

                        const double noteOffSeconds = nextTriggerSeconds >= 0.0
                            ? InstrumentPlaybackTiming::getHandoffEventTime (noteEndSeconds)
                            : noteEndSeconds;
                        const auto rollFx = StepFxResolver::getRollFx (resolvedFxSlots);
                        if (! isPluginInstrumentTrack
                            || ! arpNotes.empty()
                            || ! appendRolledNoteSequence (midiSeq, noteChannel, chordNotes, rollFx, velocity,
                                                           noteEventSeconds, noteOffSeconds, rowEndSeconds,
                                                           row, trackIdx, laneIdx, rep))
                        {
                            appendNoteSequence (midiSeq, noteChannel, chordNotes, arpNotes, velocity,
                                                noteEventSeconds, noteOffSeconds, rowEndSeconds);
                        }

                        lastPlayingNote = chordNotes.size() == 1 && arpNotes.empty() ? noteSlot.note : -1;
                        activePortaSteps = 0;
                    }

                    beatOffset += patternLengthBeats;
                }
            }
        }

        midiSeq.updateMatchedPairs();
        midiClip->mergeInMidiSequence (midiSeq, te::MidiList::NoteAutomationType::none);
    }

    // Prime automation baselines and initial values for arrangement playback.
    if (! sequence.empty() && sequence.front().first != nullptr)
        applyPatternAutomation (sequence.front().first->getAutomationData(), sequence.front().first->numRows, rpb);

    refreshTransportLoopRangeFromClip();
    scheduleTransportStopFromCurrentPosition();
}

void TrackerEngine::play()
{
    if (edit == nullptr)
        return;

    auto& transport = edit->getTransport();
    cancelTransportStop();
    refreshTransportLoopRangeFromClip();

    transport.setPosition (te::TimePosition::fromSeconds (0.0));
    transport.play (false);
    scheduleTransportStopFromCurrentPosition();
}

void TrackerEngine::playFromBeat (double beat)
{
    if (edit == nullptr)
        return;

    auto& transport = edit->getTransport();
    cancelTransportStop();
    refreshTransportLoopRangeFromClip();

    auto loopRange = transport.getLoopRange();
    if (loopRange.isEmpty())
    {
        play();
        return;
    }

    auto startTime = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (juce::jmax (0.0, beat)));
    if (startTime < loopRange.getStart() || startTime >= loopRange.getEnd())
        startTime = loopRange.getStart();

    transport.setPosition (startTime);
    if (! transport.isPlaying())
        transport.play (false);
    scheduleTransportStopFromCurrentPosition();
}

void TrackerEngine::playFromRow (int row)
{
    const double beat = static_cast<double> (juce::jmax (0, row)) / static_cast<double> (juce::jmax (1, rowsPerBeat));
    playFromBeat (beat);
}

void TrackerEngine::playFocusLoopRows (int startRow, int endRow)
{
    if (edit == nullptr)
        return;

    if (startRow > endRow)
        std::swap (startRow, endRow);

    const int safeRpb = juce::jmax (1, rowsPerBeat);
    const int clampedStartRow = juce::jmax (0, startRow);
    const int clampedEndRow = juce::jmax (clampedStartRow, endRow);
    const double startBeat = static_cast<double> (clampedStartRow) / static_cast<double> (safeRpb);
    double endBeat = static_cast<double> (clampedEndRow + 1) / static_cast<double> (safeRpb);
    endBeat = juce::jmax (startBeat + 1.0 / static_cast<double> (safeRpb), endBeat);

    auto& transport = edit->getTransport();
    cancelTransportStop();

    bool shouldLoop = true;
    if (transportStopBeat >= 0.0)
    {
        if (transportStopBeat <= startBeat)
        {
            stop();
            return;
        }

        if (transportStopBeat < endBeat)
        {
            endBeat = transportStopBeat;
            shouldLoop = false;
        }
    }

    const auto startTime = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (startBeat));
    const auto endTime = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (endBeat));
    transport.setLoopRange ({ startTime, endTime });
    transport.looping = shouldLoop;
    transport.setPosition (startTime);

    if (! transport.isPlaying())
        transport.play (false);

    scheduleTransportStopFromCurrentPosition();
}

void TrackerEngine::stop()
{
    if (edit == nullptr)
        return;

    cancelTransportStop();

    // Stop any active preview before transport shutdown so live preview MIDI
    // gets a matching note-off while the tracks can still process it.
    stopPreview();

    // Send all-notes-off to every plugin instrument track BEFORE stopping
    // transport.  This prevents stuck notes from in-progress MIDI clips and
    // avoids potential deadlocks from the transport's stop logic trying to
    // flush MIDI while plugins are actively processing.
    for (const auto& [instIdx, info] : instrumentSlotInfos)
    {
        if (info.isPlugin() && info.ownerTrack >= 0 && info.ownerTrack < kNumTracks)
        {
            auto* track = getTrack (info.ownerTrack);
            if (track != nullptr)
            {
                track->injectLiveMidiMessage (
                    juce::MidiMessage::allNotesOff (1), 0);
                track->turnOffGuideNotes();
            }
        }
    }

    releaseAllPluginInstrumentNoteModulators();
    resetPluginInstrumentModulations();

    edit->getTransport().stop (false, false);
    // Avoid synchronous parameter writes during transport stop (can block with
    // some plugin combinations). New play/sync re-establishes automation state.
    lastAutomatedParams.clear();
}

void TrackerEngine::togglePlayStop()
{
    if (isPlaying())
        stop();
    else
        play();
}

bool TrackerEngine::isPlaying() const
{
    if (edit == nullptr)
        return false;

    return edit->getTransport().isPlaying();
}

int TrackerEngine::getPlaybackRow (int numRows) const
{
    if (edit == nullptr || ! isPlaying())
        return -1;

    auto& transport = edit->getTransport();
    auto pos = transport.getPosition();
    auto loopRange = transport.getLoopRange();

    if (loopRange.isEmpty())
        return -1;

    // Convert time position to beat position
    auto beatPos = edit->tempoSequence.toBeats (pos);

    // Convert beats to row
    int row = static_cast<int> (beatPos.inBeats() * static_cast<double> (rowsPerBeat));
    return juce::jlimit (0, numRows - 1, row);
}

void TrackerEngine::updateLoopRangeForPatternLength (int numRows)
{
    if (edit == nullptr || ! isPlaying())
        return;

    auto& transport = edit->getTransport();

    // Calculate the new pattern length in beats and convert to time
    double patternLengthBeats = static_cast<double> (numRows) / static_cast<double> (rowsPerBeat);
    auto newEndTime = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (patternLengthBeats));
    if (transportStopBeat >= 0.0)
    {
        const auto stopTime = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (transportStopBeat));
        if (stopTime < newEndTime)
            newEndTime = stopTime;
        transport.looping = false;
    }
    else
    {
        transport.looping = true;
    }
    auto startTime = te::TimePosition::fromSeconds (0.0);

    te::TimeRange newRange { startTime, newEndTime };
    transport.setLoopRange (newRange);

    // If the playhead is past the new end, wrap to the beginning
    auto currentPos = transport.getPosition();
    if (currentPos >= newEndTime)
        transport.setPosition (startTime);
}

void TrackerEngine::scheduleTransportStopFromCurrentPosition()
{
    if (edit == nullptr || transportStopTimer == nullptr || transportStopBeat < 0.0)
        return;

    auto& transport = edit->getTransport();
    if (! transport.isPlaying())
        return;

    const double nowSeconds = transport.getPosition().inSeconds();
    const double stopSeconds = edit->tempoSequence
        .toTime (te::BeatPosition::fromBeats (transportStopBeat)).inSeconds();
    const double remainingSeconds = stopSeconds - nowSeconds;

    if (remainingSeconds <= 0.0)
    {
        handleTransportStopTimer();
        return;
    }

    const int delayMs = juce::jlimit (1, 60 * 60 * 1000,
                                      static_cast<int> (std::ceil (remainingSeconds * 1000.0)));
    transportStopTimer->schedule (delayMs);
}

void TrackerEngine::cancelTransportStop()
{
    if (transportStopTimer != nullptr)
        transportStopTimer->cancel();
}

void TrackerEngine::handleTransportStopTimer()
{
    if (edit == nullptr || transportStopBeat < 0.0)
        return;

    auto& transport = edit->getTransport();
    if (! transport.isPlaying())
        return;

    const double nowSeconds = transport.getPosition().inSeconds();
    const double stopSeconds = edit->tempoSequence
        .toTime (te::BeatPosition::fromBeats (transportStopBeat)).inSeconds();
    if (nowSeconds + 0.01 < stopSeconds)
    {
        scheduleTransportStopFromCurrentPosition();
        return;
    }

    stop();
}

void TrackerEngine::refreshTransportLoopRangeFromClip()
{
    if (edit == nullptr)
        return;

    auto& transport = edit->getTransport();
    auto tracks = te::getAudioTracks (*edit);
    if (tracks.isEmpty())
        return;

    auto clips = tracks[0]->getClips();
    if (clips.isEmpty())
        return;

    auto clipRange = clips[0]->getEditTimeRange();
    if (transportStopBeat >= 0.0)
    {
        const auto stopTime = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (transportStopBeat));
        if (stopTime > clipRange.getStart())
            clipRange = { clipRange.getStart(), stopTime };
        else
            clipRange = { clipRange.getStart(), clipRange.getStart() + te::TimeDuration::fromSeconds (0.001) };
    }

    transport.setLoopRange (clipRange);
    transport.looping = transportStopBeat < 0.0;

    auto currentPos = transport.getPosition();
    if (currentPos < clipRange.getStart() || currentPos >= clipRange.getEnd())
        transport.setPosition (clipRange.getStart());
}

void TrackerEngine::refreshTracksForInstrument (int instrumentIndex, const Pattern& pattern)
{
    if (edit == nullptr || instrumentIndex < 0)
        return;

    auto tracks = te::getAudioTracks (*edit);

    for (int t = 0; t < kNumTracks && t < tracks.size(); ++t)
    {
        // Check if this track uses the specified instrument (across all note lanes)
        bool usesInstrument = false;
        for (int row = 0; row < pattern.numRows && ! usesInstrument; ++row)
        {
            const auto& cell = pattern.getCell (row, t);
            for (int nl = 0; nl < cell.getNumNoteLanes(); ++nl)
            {
                if (cell.getNoteLane (nl).instrument == instrumentIndex)
                {
                    usesInstrument = true;
                    break;
                }
            }
        }

        if (! usesInstrument)
            continue;

        // Reload the bank for this instrument on the track's sampler plugin
        if (auto* samplerPlugin = tracks[t]->pluginList.findFirstPluginOfType<TrackerSamplerPlugin>())
        {
            auto bank = sampler.getSampleBank (instrumentIndex);
            if (bank != nullptr)
                samplerPlugin->updateBank (instrumentIndex, bank);
        }

        // If this track's current instrument matches, re-apply params
        if (currentTrackInstrument[static_cast<size_t> (t)] == instrumentIndex)
            sampler.applyParams (*tracks[t], instrumentIndex);
    }
}

double TrackerEngine::getPlaybackBeatPosition() const
{
    if (edit == nullptr || ! isPlaying())
        return -1.0;

    auto pos = edit->getTransport().getPosition();
    return edit->tempoSequence.toBeats (pos).inBeats();
}

void TrackerEngine::setRowsPerBeat (int rpb)
{
    rowsPerBeat = juce::jlimit (1, 16, rpb);

    if (edit == nullptr)
        return;

    auto tracks = te::getAudioTracks (*edit);
    for (int t = 0; t < kNumTracks && t < tracks.size(); ++t)
    {
        if (auto* samplerPlugin = tracks[t]->pluginList.findFirstPluginOfType<TrackerSamplerPlugin>())
            samplerPlugin->setRowsPerBeat (rowsPerBeat);

        if (auto* fxPlugin = tracks[t]->pluginList.findFirstPluginOfType<InstrumentEffectsPlugin>())
            fxPlugin->setRowsPerBeat (rowsPerBeat);
    }
}

void TrackerEngine::setBpm (double bpm)
{
    if (edit == nullptr)
        return;

    const double clampedBpm = juce::jlimit (20.0, 999.0, bpm);
    baseBpm = clampedBpm;
    edit->tempoSequence.getTempos()[0]->setBpm (clampedBpm);
    if (sendEffectsPlugin != nullptr)
        sendEffectsPlugin->setTempoBpm (clampedBpm);
}

double TrackerEngine::getBpm() const
{
    if (edit == nullptr)
        return 120.0;

    return baseBpm;
}

juce::String TrackerEngine::loadSampleForInstrument (int instrumentIndex, const juce::File& sampleFile)
{
    auto result = sampler.loadInstrumentSample (sampleFile, instrumentIndex);
    if (result.isEmpty())
    {
        // Invalidate all tracks using this instrument so they pick up the new bank
        for (int t = 0; t < kNumTracks; ++t)
            if (currentTrackInstrument[static_cast<size_t> (t)] == instrumentIndex)
                currentTrackInstrument[static_cast<size_t> (t)] = -1;
    }
    return result;
}

void TrackerEngine::clearSampleForInstrument (int instrumentIndex)
{
    if (instrumentIndex < 0)
        return;

    sampler.clearInstrumentSample (instrumentIndex);

    for (int t = 0; t < kNumTracks; ++t)
    {
        if (currentTrackInstrument[static_cast<size_t> (t)] == instrumentIndex)
            currentTrackInstrument[static_cast<size_t> (t)] = -1;
    }
}

void TrackerEngine::ensureTrackHasInstrument (int trackIndex, int instrumentIndex)
{
    if (trackIndex < 0
        || trackIndex >= static_cast<int> (currentTrackInstrument.size())
        || instrumentIndex < 0)
        return;

    auto* track = getTrack (trackIndex);
    if (track == nullptr)
        return;

    if (currentTrackInstrument[static_cast<size_t> (trackIndex)] != instrumentIndex)
    {
        auto applyError = sampler.applyParams (*track, instrumentIndex);
        if (applyError.isEmpty())
            currentTrackInstrument[static_cast<size_t> (trackIndex)] = instrumentIndex;
        else
            currentTrackInstrument[static_cast<size_t> (trackIndex)] = -1;
    }

    if (mixerStatePtr != nullptr && trackIndex < kNumTracks)
        if (auto* fxPlugin = track->pluginList.findFirstPluginOfType<InstrumentEffectsPlugin>())
            fxPlugin->setTrackSendGainLinear (
                getTrackFaderGain (mixerStatePtr->tracks[static_cast<size_t> (trackIndex)]));

    if (auto* samplerPlugin = track->pluginList.findFirstPluginOfType<TrackerSamplerPlugin>())
        samplerPlugin->setRowsPerBeat (rowsPerBeat);
}

void TrackerEngine::prepareTracksForPattern (const Pattern& pattern)
{
    std::array<std::vector<int>, kNumTracks> instrumentsByTrack {};

    for (int t = 0; t < kNumTracks; ++t)
    {
        auto& trackInstruments = instrumentsByTrack[static_cast<size_t> (t)];
        for (int row = 0; row < pattern.numRows; ++row)
        {
            const auto& cell = pattern.getCell (row, t);
            // Scan all note lanes for instruments
            int numLanes = cell.getNumNoteLanes();
            for (int nl = 0; nl < numLanes; ++nl)
            {
                int inst = cell.getNoteLane (nl).instrument;
                if (inst >= 0
                    && std::find (trackInstruments.begin(), trackInstruments.end(), inst) == trackInstruments.end())
                {
                    trackInstruments.push_back (inst);
                }
            }
        }
    }

    prepareTracksForInstrumentUsage (instrumentsByTrack);
}

void TrackerEngine::prepareTracksForInstrumentUsage (const std::array<std::vector<int>, kNumTracks>& instrumentsByTrack)
{
    if (edit == nullptr)
        return;

    auto tracks = te::getAudioTracks (*edit);

    for (int t = 0; t < kNumTracks && t < tracks.size(); ++t)
    {
        const auto& usedInstruments = instrumentsByTrack[static_cast<size_t> (t)];
        if (usedInstruments.empty())
            continue;

        // Skip sample setup for tracks that are in plugin instrument mode.
        // The plugin instrument is already loaded via ensurePluginInstrumentLoaded.
        if (getTrackContentMode (t) == TrackContentMode::PluginInstrument)
        {
            // Ensure all plugin instruments assigned to this track are loaded
            for (const auto& [instIdx, info] : instrumentSlotInfos)
            {
                if (info.isPlugin() && info.ownerTrack == t)
                    ensurePluginInstrumentLoaded (instIdx);
            }
            continue;
        }

        const int firstInst = usedInstruments.front();

        // Load the first (default) instrument onto this track
        if (firstInst != currentTrackInstrument[static_cast<size_t> (t)])
        {
            auto applyError = sampler.applyParams (*tracks[t], firstInst);
            if (applyError.isEmpty())
                currentTrackInstrument[static_cast<size_t> (t)] = firstInst;
            else
                currentTrackInstrument[static_cast<size_t> (t)] = -1;
        }

        // Pre-load all banks for multi-instrument support (and clear stale banks
        // by always replacing the map, even when only one instrument is used).
        if (auto* samplerPlugin = tracks[t]->pluginList.findFirstPluginOfType<TrackerSamplerPlugin>())
        {
            std::map<int, std::shared_ptr<const SampleBank>> banks;
            for (int inst : usedInstruments)
            {
                auto bank = sampler.getSampleBank (inst);
                if (bank != nullptr)
                    banks[inst] = bank;
            }
            samplerPlugin->preloadBanks (banks);
            samplerPlugin->setRowsPerBeat (rowsPerBeat);
        }

        // Configure effects plugin with rowsPerBeat, global mod state, and send buffers
        if (auto* fxPlugin = sampler.getOrCreateEffectsPlugin (*tracks[t], firstInst))
        {
            std::map<int, GlobalModState*> globalStates;
            for (int inst : usedInstruments)
                globalStates[inst] = sampler.getOrCreateGlobalModState (inst);

            fxPlugin->setRowsPerBeat (rowsPerBeat);
            fxPlugin->setGlobalModState (sampler.getOrCreateGlobalModState (firstInst));
            fxPlugin->setGlobalModStates (globalStates);
            fxPlugin->setSendBuffers (&sampler.getSendBuffers());
            if (mixerStatePtr != nullptr)
                fxPlugin->setTrackSendGainLinear (
                    getTrackFaderGain (mixerStatePtr->tracks[static_cast<size_t> (t)]));
            fxPlugin->onTempoChange = nullptr;
        }
    }
}

int TrackerEngine::getTrackInstrument (int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= kNumTracks)
        return -1;
    return currentTrackInstrument[static_cast<size_t> (trackIndex)];
}

void TrackerEngine::invalidateTrackInstruments()
{
    currentTrackInstrument.fill (-1);
}

void TrackerEngine::previewNote (int trackIndex, int instrumentIndex, int midiNote, bool autoStop)
{
    previewNotes (trackIndex, instrumentIndex, { midiNote }, autoStop);
}

void TrackerEngine::previewNotes (int trackIndex, int instrumentIndex, const std::vector<int>& midiNotes, bool autoStop)
{
    juce::ignoreUnused (trackIndex);

    if (instrumentIndex < 0 || midiNotes.empty())
        return;

    std::vector<int> notes;
    notes.reserve (midiNotes.size());
    for (int note : midiNotes)
    {
        auto clamped = juce::jlimit (0, 127, note);
        if (std::find (notes.begin(), notes.end(), clamped) == notes.end())
            notes.push_back (clamped);
    }

    // Plugin instrument: inject an explicit note-on on the owner track via
    // injectLiveMidiMessage so we have full control over note-off timing.
    // playGuideNote with autorelease killed the note after ~100ms, breaking
    // hold-to-preview; and clearing state immediately meant stopPluginPreview
    // could never send the matching note-off, causing stuck notes.
    if (isPluginInstrument (instrumentIndex))
    {
        ensurePluginInstrumentLoaded (instrumentIndex);

        const auto& slotInfo = getInstrumentSlotInfo (instrumentIndex);

        if (autoStop)
        {
            stopPreview();
        }
        else
        {
            const bool pendingAutoStop = isTimerRunning();
            stopTimer();

            const bool sentSampleNoteOff = stopSamplePreview();
            if (activePreviewTrack >= 0)
            {
                auto* track = getTrack (activePreviewTrack);
                if (track != nullptr && ! sentSampleNoteOff)
                    sampler.stopNote (*track);

                activePreviewTrack = -1;
            }

            previewBank = nullptr;

            if (pendingAutoStop
                || previewPluginInstrument != instrumentIndex
                || previewPluginTrack != slotInfo.ownerTrack)
                stopPluginPreview();
        }

        auto* ownerTrack = getTrack (slotInfo.ownerTrack);
        if (ownerTrack != nullptr)
        {
            int velocity = juce::jlimit (1, 127, static_cast<int> (previewVolume * 127.0f + 0.5f));
            bool startedAnyNote = false;

            for (int note : notes)
            {
                if (! autoStop
                    && std::find (previewPluginNotes.begin(), previewPluginNotes.end(), note) != previewPluginNotes.end())
                    continue;

                ownerTrack->injectLiveMidiMessage (
                    juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (velocity)), 0);

                previewPluginNotes.push_back (note);
                startedAnyNote = true;
            }

            previewPluginInstrument = instrumentIndex;
            previewPluginTrack = slotInfo.ownerTrack;

            if (startedAnyNote)
                triggerPluginInstrumentNoteModulators (instrumentIndex);
        }

        if (autoStop)
            startTimer (kPluginPreviewDurationMs);
        return;
    }

    stopPreview();

    // Sample instrument: preview through the dedicated preview track.
    if (sampler.getSampleBank (instrumentIndex) == nullptr)
        return;

    auto* track = getTrack (kPreviewTrack);
    if (track == nullptr)
        return;

    ensureTrackHasInstrument (kPreviewTrack, instrumentIndex);
    if (getTrackInstrument (kPreviewTrack) != instrumentIndex)
        return;

    // Preview should match instrument DSP and sends, with preview volume applied
    // as a track-level output gain (not as note velocity).
    if (auto* fxPlugin = sampler.getOrCreateEffectsPlugin (*track, instrumentIndex))
    {
        fxPlugin->setRowsPerBeat (rowsPerBeat);
        auto* globalState = sampler.getOrCreateGlobalModState (instrumentIndex);
        fxPlugin->setGlobalModState (globalState);
        std::map<int, GlobalModState*> globalStates;
        globalStates[instrumentIndex] = globalState;
        fxPlugin->setGlobalModStates (globalStates);
        fxPlugin->setOutputGainLinear (previewVolume);
    }

    if (track->pluginList.findFirstPluginOfType<TrackerSamplerPlugin>() == nullptr)
        return;

    for (int note : notes)
        track->injectLiveMidiMessage (juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (127)), 0);

    previewSampleNotes = notes;
    previewSampleTrack = kPreviewTrack;
    activePreviewTrack = kPreviewTrack;

    // Auto-stop: safety timeout; hold-to-preview relies on stopPreview() from key release
    if (autoStop)
        startTimer (kPreviewDurationMs);
}

float TrackerEngine::getPreviewPlaybackPosition() const
{
    if (edit == nullptr || activePreviewTrack < 0)
        return -1.0f;

    auto tracks = te::getAudioTracks (*edit);
    if (activePreviewTrack >= tracks.size())
        return -1.0f;

    auto* samplerPlugin = tracks[activePreviewTrack]->pluginList.findFirstPluginOfType<TrackerSamplerPlugin>();
    if (samplerPlugin == nullptr)
        return -1.0f;

    return samplerPlugin->getPlaybackPosition();
}

void TrackerEngine::previewAudioFile (const juce::File& file)
{
    if (edit == nullptr)
        return;

    // Stop any current preview
    stopPreview();

    // Load the audio file into a temporary bank
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
        return;

    auto bank = std::make_shared<SampleBank>();
    bank->sampleRate = reader->sampleRate;
    bank->numChannels = static_cast<int> (reader->numChannels);
    bank->totalSamples = static_cast<juce::int64> (reader->lengthInSamples);
    bank->sourceFile = file;
    bank->buffer.setSize (bank->numChannels, static_cast<int> (reader->lengthInSamples));
    reader->read (&bank->buffer, 0, static_cast<int> (reader->lengthInSamples), 0, true, true);

    // Keep bank alive
    previewBank = bank;

    // Ensure preview track has a sampler plugin
    auto* track = getTrack (kPreviewTrack);
    if (track == nullptr)
        return;

    auto* samplerPlugin = track->pluginList.findFirstPluginOfType<TrackerSamplerPlugin>();
    if (samplerPlugin == nullptr)
    {
        if (auto plugin = dynamic_cast<TrackerSamplerPlugin*> (
                track->edit.getPluginCache().createNewPlugin (TrackerSamplerPlugin::xmlTypeName, {}).get()))
        {
            track->pluginList.insertPlugin (*plugin, 0, nullptr);
            samplerPlugin = plugin;
        }
    }

    if (samplerPlugin == nullptr)
        return;

    // Browser file previews should use neutral/default sampler params.
    samplerPlugin->setSamplerSource (nullptr);
    currentTrackInstrument[static_cast<size_t> (kPreviewTrack)] = -1;
    if (auto* fxPlugin = track->pluginList.findFirstPluginOfType<InstrumentEffectsPlugin>())
        fxPlugin->setSamplerSource (nullptr);

    samplerPlugin->setSampleBank (bank);
    samplerPlugin->playNote (60, previewVolume);

    activePreviewTrack = kPreviewTrack;
    startTimer (kPreviewDurationMs);
}

void TrackerEngine::previewInstrument (int instrumentIndex)
{
    if (edit == nullptr)
        return;

    stopPreview();

    if (isPluginInstrument (instrumentIndex))
        return;

    auto bank = sampler.getSampleBank (instrumentIndex);
    if (bank == nullptr)
        return;

    previewNote (kPreviewTrack, instrumentIndex, 60, true);
}

bool TrackerEngine::stopPluginPreview()
{
    if (! previewPluginNotes.empty() && previewPluginTrack >= 0)
    {
        auto* track = getTrack (previewPluginTrack);
        if (track != nullptr)
        {
            for (int note : previewPluginNotes)
            {
                track->injectLiveMidiMessage (
                    juce::MidiMessage::noteOff (1, note), 0);
            }
        }
    }

    previewPluginNotes.clear();
    if (previewPluginInstrument >= 0)
        releasePluginInstrumentNoteModulators (previewPluginInstrument);
    previewPluginInstrument = -1;
    previewPluginTrack = -1;
    return true;
}

bool TrackerEngine::stopSamplePreview()
{
    if (! previewSampleNotes.empty() && previewSampleTrack >= 0)
    {
        auto* track = getTrack (previewSampleTrack);
        if (track != nullptr)
        {
            for (int note : previewSampleNotes)
                track->injectLiveMidiMessage (
                    juce::MidiMessage::noteOff (1, note), 0);
        }
    }

    const bool hadSamplePreview = ! previewSampleNotes.empty();
    previewSampleNotes.clear();
    previewSampleTrack = -1;
    return hadSamplePreview;
}

void TrackerEngine::stopPreview()
{
    stopTimer();
    stopPluginPreview();
    const bool sentSampleNoteOff = stopSamplePreview();

    if (activePreviewTrack >= 0)
    {
        auto* track = getTrack (activePreviewTrack);
        if (track != nullptr && ! sentSampleNoteOff)
            sampler.stopNote (*track);

        activePreviewTrack = -1;
    }

    previewBank = nullptr;
}

void TrackerEngine::stopPreviewNote (int instrumentIndex, int midiNote)
{
    if (instrumentIndex < 0)
        return;

    const int note = juce::jlimit (0, 127, midiNote);

    if (isPluginInstrument (instrumentIndex))
    {
        const auto& slotInfo = getInstrumentSlotInfo (instrumentIndex);
        const int trackIndex = slotInfo.ownerTrack >= 0 ? slotInfo.ownerTrack : previewPluginTrack;

        if (auto* track = getTrack (trackIndex))
            track->injectLiveMidiMessage (juce::MidiMessage::noteOff (1, note), 0);

        auto it = std::find (previewPluginNotes.begin(), previewPluginNotes.end(), note);
        if (it != previewPluginNotes.end())
            previewPluginNotes.erase (it);

        if (previewPluginNotes.empty() && previewPluginInstrument == instrumentIndex)
        {
            stopTimer();
            releasePluginInstrumentNoteModulators (instrumentIndex);
            previewPluginInstrument = -1;
            previewPluginTrack = -1;
        }

        return;
    }

    auto it = std::find (previewSampleNotes.begin(), previewSampleNotes.end(), note);
    if (it == previewSampleNotes.end())
        return;

    if (previewSampleTrack >= 0)
        if (auto* track = getTrack (previewSampleTrack))
            track->injectLiveMidiMessage (juce::MidiMessage::noteOff (1, note), 0);

    previewSampleNotes.erase (it);
    if (previewSampleNotes.empty())
    {
        stopTimer();
        if (activePreviewTrack == previewSampleTrack)
            activePreviewTrack = -1;
        previewSampleTrack = -1;
    }
}

void TrackerEngine::setPreviewVolume (float gainLinear)
{
    previewVolume = juce::jlimit (0.0f, 1.0f, gainLinear);

    if (auto* track = getTrack (kPreviewTrack))
        if (auto* fxPlugin = track->pluginList.findFirstPluginOfType<InstrumentEffectsPlugin>())
            fxPlugin->setOutputGainLinear (previewVolume);
}

void TrackerEngine::timerCallback()
{
    stopTimer();
    stopPluginPreview();
    const bool sentSampleNoteOff = stopSamplePreview();

    if (activePreviewTrack >= 0)
    {
        auto* track = getTrack (activePreviewTrack);
        if (track != nullptr && ! sentSampleNoteOff)
            sampler.stopNote (*track);

        activePreviewTrack = -1;
    }
}

te::AudioTrack* TrackerEngine::getTrack (int index)
{
    if (edit == nullptr)
        return nullptr;

    auto tracks = te::getAudioTracks (*edit);
    if (index >= 0 && index < tracks.size())
        return tracks[index];

    return nullptr;
}

void TrackerEngine::setMetronomeEnabled (bool enabled)
{
    if (auto* track = getTrack (kMetronomeTrack))
    {
        if (auto* metro = track->pluginList.findFirstPluginOfType<MetronomePlugin>())
            metro->setEnabled (enabled);
    }
}

bool TrackerEngine::isMetronomeEnabled() const
{
    if (edit == nullptr)
        return false;

    auto tracks = te::getAudioTracks (*edit);
    if (kMetronomeTrack < tracks.size())
    {
        if (auto* metro = tracks[kMetronomeTrack]->pluginList.findFirstPluginOfType<MetronomePlugin>())
            return metro->isEnabled();
    }
    return false;
}

void TrackerEngine::setMetronomeVolume (float gainLinear)
{
    if (auto* track = getTrack (kMetronomeTrack))
    {
        if (auto* metro = track->pluginList.findFirstPluginOfType<MetronomePlugin>())
            metro->setVolume (gainLinear);
    }
}

float TrackerEngine::getMetronomeVolume() const
{
    if (edit == nullptr)
        return 0.7f;

    auto tracks = te::getAudioTracks (*edit);
    if (kMetronomeTrack < tracks.size())
    {
        if (auto* metro = tracks[kMetronomeTrack]->pluginList.findFirstPluginOfType<MetronomePlugin>())
            return metro->getVolume();
    }
    return 0.7f;
}

void TrackerEngine::setupSendEffectsTrack()
{
    sendEffectsPlugin = nullptr;

    // Prepare send buffers (default block size, stereo)
    sampler.getSendBuffers().prepare (8192, 2);
    groupRoutingBuffers.prepare (8192, 2);

    // Older builds hosted this on a silent bus track, which meant master
    // processing could miss the real summed mix. Keep the final send/master
    // processor on Tracktion's master plugin list instead.
    if (auto* legacyBusTrack = getTrack (kSendEffectsTrack))
        if (auto* legacy = legacyBusTrack->pluginList.findFirstPluginOfType<SendEffectsPlugin>())
            legacy->removeFromParent();

    if (edit == nullptr)
        return;

    auto& masterPlugins = edit->getMasterPluginList();
    auto* existing = masterPlugins.findFirstPluginOfType<SendEffectsPlugin>();
    if (existing == nullptr)
    {
        if (auto plugin = dynamic_cast<SendEffectsPlugin*> (
                edit->getPluginCache().createNewPlugin (SendEffectsPlugin::xmlTypeName, {}).get()))
        {
            masterPlugins.insertPlugin (*plugin, 0, nullptr);
            existing = plugin;
        }
    }

    if (existing != nullptr)
    {
        existing->setSendBuffers (&sampler.getSendBuffers());
        existing->setGroupRoutingBuffers (&groupRoutingBuffers);
        existing->setMixerState (mixerStatePtr);
        existing->setTempoBpm (getBpm());
        sendEffectsPlugin = existing;
    }
}

void TrackerEngine::setDelayParams (const DelayParams& params)
{
    if (sendEffectsPlugin != nullptr)
        sendEffectsPlugin->setDelayParams (params);
}

void TrackerEngine::setReverbParams (const ReverbParams& params)
{
    if (sendEffectsPlugin != nullptr)
        sendEffectsPlugin->setReverbParams (params);
}

DelayParams TrackerEngine::getDelayParams() const
{
    if (sendEffectsPlugin != nullptr)
        return sendEffectsPlugin->getDelayParams();
    return {};
}

ReverbParams TrackerEngine::getReverbParams() const
{
    if (sendEffectsPlugin != nullptr)
        return sendEffectsPlugin->getReverbParams();
    return {};
}

void TrackerEngine::setMixerState (MixerState* state)
{
    mixerStatePtr = state;
    setupSendEffectsTrack();
    setupMixerPlugins();
    rebuildMasterInsertChain();
}

void TrackerEngine::setTrackLayout (TrackLayout* layout)
{
    trackLayoutPtr = layout;
    refreshMixerRouting();
}

void TrackerEngine::setupChannelStripAndOutput (int trackIndex)
{
    if (edit == nullptr || mixerStatePtr == nullptr)
        return;

    auto tracks = te::getAudioTracks (*edit);
    if (trackIndex < 0 || trackIndex >= tracks.size())
        return;

    auto* track = tracks[trackIndex];

    // Ensure ChannelStripPlugin exists (position 2: sampler=0, effects=1, channelstrip=2)
    auto* strip = track->pluginList.findFirstPluginOfType<ChannelStripPlugin>();
    if (strip == nullptr)
    {
        if (auto plugin = dynamic_cast<ChannelStripPlugin*> (
                track->edit.getPluginCache().createNewPlugin (ChannelStripPlugin::xmlTypeName, {}).get()))
        {
            track->pluginList.insertPlugin (*plugin, 2, nullptr);
            strip = plugin;
        }
    }

    if (strip != nullptr)
        strip->setMixState (mixerStatePtr->tracks[static_cast<size_t> (trackIndex)]);

    // Ensure TrackOutputPlugin exists (always the last plugin in the chain)
    auto* output = track->pluginList.findFirstPluginOfType<TrackOutputPlugin>();
    if (output == nullptr)
    {
        if (auto plugin = dynamic_cast<TrackOutputPlugin*> (
                track->edit.getPluginCache().createNewPlugin (TrackOutputPlugin::xmlTypeName, {}).get()))
        {
            // Insert at end of plugin list
            track->pluginList.insertPlugin (*plugin, -1, nullptr);
            output = plugin;
        }
    }

    if (output != nullptr)
    {
        output->setMixState (mixerStatePtr->tracks[static_cast<size_t> (trackIndex)]);
        output->setSendBuffers (&sampler.getSendBuffers());
        const int groupIndex = trackLayoutPtr != nullptr ? trackLayoutPtr->getGroupForTrack (trackIndex) : -1;
        output->setGroupRouting (&groupRoutingBuffers,
                                 groupIndex,
                                 shouldSuppressDirectOutputForGroupSolo (mixerStatePtr, groupIndex));
    }

    if (auto* fxPlugin = track->pluginList.findFirstPluginOfType<InstrumentEffectsPlugin>())
        fxPlugin->setTrackSendGainLinear (
            getTrackFaderGain (mixerStatePtr->tracks[static_cast<size_t> (trackIndex)]));

    // Also remove any legacy MixerPlugin if present (migrating from old chain)
    auto* legacyMixer = track->pluginList.findFirstPluginOfType<MixerPlugin>();
    if (legacyMixer != nullptr)
        legacyMixer->removeFromParent();
}

void TrackerEngine::setupMixerPlugins()
{
    if (edit == nullptr || mixerStatePtr == nullptr)
        return;

    for (int t = 0; t < kNumTracks; ++t)
        setupChannelStripAndOutput (t);
}

void TrackerEngine::refreshMixerPlugins()
{
    if (edit == nullptr || mixerStatePtr == nullptr)
        return;

    if (sendEffectsPlugin == nullptr)
        setupSendEffectsTrack();

    if (sendEffectsPlugin != nullptr)
        sendEffectsPlugin->refreshMixerStateSnapshot();

    auto tracks = te::getAudioTracks (*edit);
    const int numTracks = juce::jmin (kNumTracks, tracks.size());

    for (int trackIndex = 0; trackIndex < numTracks; ++trackIndex)
    {
        auto* track = tracks[trackIndex];
        if (track == nullptr)
            continue;

        const auto& mixState = mixerStatePtr->tracks[static_cast<size_t> (trackIndex)];

        if (auto* strip = track->pluginList.findFirstPluginOfType<ChannelStripPlugin>())
            strip->setMixState (mixState);

        if (auto* output = track->pluginList.findFirstPluginOfType<TrackOutputPlugin>())
        {
            output->setMixState (mixState);
            output->setSendBuffers (&sampler.getSendBuffers());
        }

        if (auto* fxPlugin = track->pluginList.findFirstPluginOfType<InstrumentEffectsPlugin>())
            fxPlugin->setTrackSendGainLinear (getTrackFaderGain (mixState));

        if (auto* legacyMixer = track->pluginList.findFirstPluginOfType<MixerPlugin>())
            legacyMixer->setMixState (mixState);
    }
}

void TrackerEngine::rebuildMixerPluginChains()
{
    setupSendEffectsTrack();
    setupMixerPlugins();

    for (int t = 0; t < kNumTracks; ++t)
        rebuildInsertChain (t);

    rebuildMasterInsertChain();
}

void TrackerEngine::refreshMixerRouting()
{
    if (edit == nullptr)
        return;

    auto tracks = te::getAudioTracks (*edit);
    for (int trackIndex = 0; trackIndex < kNumTracks && trackIndex < tracks.size(); ++trackIndex)
    {
        const int groupIndex = trackLayoutPtr != nullptr ? trackLayoutPtr->getGroupForTrack (trackIndex) : -1;
        if (auto* output = tracks[trackIndex]->pluginList.findFirstPluginOfType<TrackOutputPlugin>())
            output->setGroupRouting (&groupRoutingBuffers,
                                     groupIndex,
                                     shouldSuppressDirectOutputForGroupSolo (mixerStatePtr, groupIndex));
    }
}

float TrackerEngine::getTrackPeakLevel (int trackIndex) const
{
    if (edit == nullptr || trackIndex < 0 || trackIndex >= kNumTracks)
        return 0.0f;

    auto tracks = te::getAudioTracks (*edit);
    if (trackIndex >= tracks.size())
        return 0.0f;

    // Try TrackOutputPlugin first (new chain), fall back to MixerPlugin (legacy)
    auto* output = tracks[trackIndex]->pluginList.findFirstPluginOfType<TrackOutputPlugin>();
    if (output != nullptr)
    {
        float peak = output->getPeakLevel();
        output->resetPeak();
        return peak;
    }

    auto* mixer = tracks[trackIndex]->pluginList.findFirstPluginOfType<MixerPlugin>();
    if (mixer == nullptr)
        return 0.0f;

    float peak = mixer->getPeakLevel();
    mixer->resetPeak();
    return peak;
}

float TrackerEngine::getSendReturnPeakLevel (int returnIndex) const
{
    if (sendEffectsPlugin == nullptr)
        return 0.0f;

    float peak = sendEffectsPlugin->getSendReturnPeakLevel (returnIndex);
    sendEffectsPlugin->resetSendReturnPeak (returnIndex);
    return peak;
}

float TrackerEngine::getMasterPeakLevel() const
{
    if (sendEffectsPlugin == nullptr)
        return 0.0f;

    float peak = sendEffectsPlugin->getMasterPeakLevel();
    sendEffectsPlugin->resetMasterPeak();
    return peak;
}

void TrackerEngine::decayTrackPeaks()
{
    if (edit == nullptr)
        return;

    auto tracks = te::getAudioTracks (*edit);
    for (int t = 0; t < kNumTracks && t < tracks.size(); ++t)
    {
        auto* output = tracks[t]->pluginList.findFirstPluginOfType<TrackOutputPlugin>();
        if (output != nullptr)
        {
            output->resetPeak();
            continue;
        }

        auto* mixer = tracks[t]->pluginList.findFirstPluginOfType<MixerPlugin>();
        if (mixer != nullptr)
            mixer->resetPeak();
    }

    if (sendEffectsPlugin != nullptr)
    {
        sendEffectsPlugin->resetSendReturnPeak (0);
        sendEffectsPlugin->resetSendReturnPeak (1);
        sendEffectsPlugin->resetMasterPeak();
    }
}

//==============================================================================
// Insert plugin management
//==============================================================================

bool TrackerEngine::addInsertPlugin (int trackIndex, const juce::PluginDescription& desc)
{
    if (edit == nullptr || mixerStatePtr == nullptr)
        return false;
    if (trackIndex < 0 || trackIndex >= kNumTracks)
        return false;

    auto& slots = mixerStatePtr->insertSlots[static_cast<size_t> (trackIndex)];
    if (static_cast<int> (slots.size()) >= kMaxInsertSlots)
        return false;

    auto* track = getTrack (trackIndex);
    if (track == nullptr)
        return false;

    stopPreview();

    // Create a Tracktion ExternalPlugin wrapper. Avoid an eager standalone
    // createPluginInstance preflight here: shell plugins such as Waves can be
    // valid in Tracktion while failing or flickering under double-instantiation.
    auto externalPlugin = track->edit.getPluginCache().createNewPlugin (
        te::ExternalPlugin::xmlTypeName, desc);

    if (externalPlugin == nullptr)
        return false;

    // Find insertion position: after ChannelStripPlugin + existing inserts, before TrackOutputPlugin
    int insertPos = -1;
    auto& pluginList = track->pluginList;
    for (int i = 0; i < pluginList.size(); ++i)
    {
        if (dynamic_cast<TrackOutputPlugin*> (pluginList[i]) != nullptr)
        {
            insertPos = i;
            break;
        }
    }

    if (insertPos < 0)
        insertPos = pluginList.size(); // Fallback: insert at end

    pluginList.insertPlugin (*externalPlugin, insertPos, nullptr);

    // Add to state model
    InsertSlotState newSlot;
    newSlot.pluginName = desc.name;
    newSlot.pluginIdentifier = desc.createIdentifierString();
    newSlot.pluginFormatName = desc.pluginFormatName;
    newSlot.bypassed = false;
    slots.push_back (std::move (newSlot));

    if (onInsertStateChanged)
        onInsertStateChanged();

    return true;
}

void TrackerEngine::removeInsertPlugin (int trackIndex, int slotIndex)
{
    if (edit == nullptr || mixerStatePtr == nullptr)
        return;
    if (trackIndex < 0 || trackIndex >= kNumTracks)
        return;

    auto& slots = mixerStatePtr->insertSlots[static_cast<size_t> (trackIndex)];
    if (slotIndex < 0 || slotIndex >= static_cast<int> (slots.size()))
        return;

    closePluginEditorsForTrack (trackIndex);
    forgetAutomatedParamsForInsertTrack (trackIndex);

    // Find and remove the plugin from the track's plugin list
    auto* track = getTrack (trackIndex);
    if (track != nullptr)
    {
        if (auto* plugin = findInsertPluginForSlot (*track, slotIndex))
            plugin->deleteFromParent();
    }

    slots.erase (slots.begin() + slotIndex);

    if (onInsertStateChanged)
        onInsertStateChanged();
}

void TrackerEngine::setInsertBypassed (int trackIndex, int slotIndex, bool bypassed)
{
    if (mixerStatePtr == nullptr)
        return;
    if (trackIndex < 0 || trackIndex >= kNumTracks)
        return;

    auto& slots = mixerStatePtr->insertSlots[static_cast<size_t> (trackIndex)];
    if (slotIndex < 0 || slotIndex >= static_cast<int> (slots.size()))
        return;

    slots[static_cast<size_t> (slotIndex)].bypassed = bypassed;

    // Find the corresponding external plugin and toggle its enabled state
    auto* track = getTrack (trackIndex);
    if (track != nullptr)
    {
        if (auto* plugin = findInsertPluginForSlot (*track, slotIndex))
            plugin->setEnabled (! bypassed);
    }

    if (onInsertStateChanged)
        onInsertStateChanged();
}

te::Plugin* TrackerEngine::getInsertPlugin (int trackIndex, int slotIndex)
{
    if (edit == nullptr || trackIndex < 0 || trackIndex >= kNumTracks)
        return nullptr;

    auto* track = getTrack (trackIndex);
    if (track == nullptr)
        return nullptr;

    return findInsertPluginForSlot (*track, slotIndex);
}

void TrackerEngine::rebuildInsertChain (int trackIndex)
{
    if (edit == nullptr || mixerStatePtr == nullptr)
        return;
    if (trackIndex < 0 || trackIndex >= kNumTracks)
        return;

    auto* track = getTrack (trackIndex);
    if (track == nullptr)
        return;

    closePluginEditorsForTrack (trackIndex);
    forgetAutomatedParamsForInsertTrack (trackIndex);

    // Remove all external plugins between ChannelStrip and TrackOutput
    std::vector<te::Plugin*> toRemove;
    bool pastChannelStrip = false;
    for (int i = 0; i < track->pluginList.size(); ++i)
    {
        auto* plugin = track->pluginList[i];
        if (dynamic_cast<ChannelStripPlugin*> (plugin) != nullptr)
        {
            pastChannelStrip = true;
            continue;
        }
        if (dynamic_cast<TrackOutputPlugin*> (plugin) != nullptr)
            break;
        if (pastChannelStrip && dynamic_cast<te::ExternalPlugin*> (plugin) != nullptr)
            toRemove.push_back (plugin);
    }

    for (auto* p : toRemove)
        p->deleteFromParent();

    // Re-add inserts from state
    auto& slots = mixerStatePtr->insertSlots[static_cast<size_t> (trackIndex)];
    for (auto& slot : slots)
    {
        if (slot.isEmpty())
            continue;

        // Find the matching PluginDescription from the known plugin list
        auto& knownList = engine->getPluginManager().knownPluginList;
        const juce::PluginDescription* matchedDesc = nullptr;

        for (auto& desc : knownList.getTypes())
        {
            if (desc.createIdentifierString() == slot.pluginIdentifier)
            {
                matchedDesc = &desc;
                break;
            }
        }

        if (matchedDesc == nullptr)
            continue;

        auto externalPlugin = track->edit.getPluginCache().createNewPlugin (
            te::ExternalPlugin::xmlTypeName, *matchedDesc);

        if (externalPlugin == nullptr)
            continue;

        // Find insertion position before TrackOutputPlugin
        int insertPos = -1;
        for (int i = 0; i < track->pluginList.size(); ++i)
        {
            if (dynamic_cast<TrackOutputPlugin*> (track->pluginList[i]) != nullptr)
            {
                insertPos = i;
                break;
            }
        }

        if (insertPos < 0)
            insertPos = track->pluginList.size();

        track->pluginList.insertPlugin (*externalPlugin, insertPos, nullptr);

        // Restore plugin state if available
        if (slot.pluginState.isValid())
        {
            if (auto* ext = dynamic_cast<te::ExternalPlugin*> (externalPlugin.get()))
                ext->restorePluginStateFromValueTree (slot.pluginState);
        }

        // Apply bypass state
        externalPlugin->setEnabled (! slot.bypassed);
    }
}

bool TrackerEngine::addMasterInsertPlugin (const juce::PluginDescription& desc)
{
    if (edit == nullptr || mixerStatePtr == nullptr)
        return false;

    auto& slots = mixerStatePtr->masterInsertSlots;
    if (static_cast<int> (slots.size()) >= kMaxInsertSlots)
        return false;

    stopPreview();

    setupSendEffectsTrack();

    auto externalPlugin = edit->getPluginCache().createNewPlugin (
        te::ExternalPlugin::xmlTypeName, desc);

    if (externalPlugin == nullptr)
        return false;

    auto& masterPlugins = edit->getMasterPluginList();
    masterPlugins.insertPlugin (*externalPlugin, getMasterInsertAppendPosition (masterPlugins), nullptr);

    InsertSlotState newSlot;
    newSlot.pluginName = desc.name;
    newSlot.pluginIdentifier = desc.createIdentifierString();
    newSlot.pluginFormatName = desc.pluginFormatName;
    newSlot.bypassed = false;
    slots.push_back (std::move (newSlot));

    if (onInsertStateChanged)
        onInsertStateChanged();

    return true;
}

void TrackerEngine::removeMasterInsertPlugin (int slotIndex)
{
    if (edit == nullptr || mixerStatePtr == nullptr)
        return;

    auto& slots = mixerStatePtr->masterInsertSlots;
    if (slotIndex < 0 || slotIndex >= static_cast<int> (slots.size()))
        return;

    closeMasterPluginEditor (slotIndex);

    auto& masterPlugins = edit->getMasterPluginList();
    if (auto* plugin = findMasterInsertPluginForSlot (masterPlugins, slotIndex))
        plugin->removeFromParent();

    slots.erase (slots.begin() + slotIndex);

    if (onInsertStateChanged)
        onInsertStateChanged();
}

void TrackerEngine::setMasterInsertBypassed (int slotIndex, bool bypassed)
{
    if (edit == nullptr || mixerStatePtr == nullptr)
        return;

    auto& slots = mixerStatePtr->masterInsertSlots;
    if (slotIndex < 0 || slotIndex >= static_cast<int> (slots.size()))
        return;

    slots[static_cast<size_t> (slotIndex)].bypassed = bypassed;

    if (auto* plugin = getMasterInsertPlugin (slotIndex))
        plugin->setEnabled (! bypassed);

    if (onInsertStateChanged)
        onInsertStateChanged();
}

te::Plugin* TrackerEngine::getMasterInsertPlugin (int slotIndex)
{
    if (edit == nullptr)
        return nullptr;

    return findMasterInsertPluginForSlot (edit->getMasterPluginList(), slotIndex);
}

void TrackerEngine::rebuildMasterInsertChain()
{
    if (edit == nullptr || mixerStatePtr == nullptr)
        return;

    setupSendEffectsTrack();

    auto& masterPlugins = edit->getMasterPluginList();

    std::vector<te::Plugin*> toRemove;
    bool pastSendEffects = false;
    for (int i = 0; i < masterPlugins.size(); ++i)
    {
        auto* plugin = masterPlugins[i];
        if (dynamic_cast<SendEffectsPlugin*> (plugin) != nullptr)
        {
            pastSendEffects = true;
            continue;
        }

        if (pastSendEffects && dynamic_cast<te::ExternalPlugin*> (plugin) != nullptr)
            toRemove.push_back (plugin);
    }

    for (auto* p : toRemove)
        p->removeFromParent();

    for (auto& slot : mixerStatePtr->masterInsertSlots)
    {
        if (slot.isEmpty())
            continue;

        auto& knownList = engine->getPluginManager().knownPluginList;
        const juce::PluginDescription* matchedDesc = nullptr;

        for (auto& desc : knownList.getTypes())
        {
            if (desc.createIdentifierString() == slot.pluginIdentifier)
            {
                matchedDesc = &desc;
                break;
            }
        }

        if (matchedDesc == nullptr)
            continue;

        auto externalPlugin = edit->getPluginCache().createNewPlugin (
            te::ExternalPlugin::xmlTypeName, *matchedDesc);

        if (externalPlugin == nullptr)
            continue;

        masterPlugins.insertPlugin (*externalPlugin, getMasterInsertAppendPosition (masterPlugins), nullptr);

        if (slot.pluginState.isValid())
        {
            if (auto* ext = dynamic_cast<te::ExternalPlugin*> (externalPlugin.get()))
                ext->restorePluginStateFromValueTree (slot.pluginState);
        }

        externalPlugin->setEnabled (! slot.bypassed);
    }
}

void TrackerEngine::snapshotInsertPluginStates()
{
    if (edit == nullptr || mixerStatePtr == nullptr)
        return;

    for (int trackIndex = 0; trackIndex < kNumTracks; ++trackIndex)
    {
        auto& slots = mixerStatePtr->insertSlots[static_cast<size_t> (trackIndex)];
        for (int slotIndex = 0; slotIndex < static_cast<int> (slots.size()); ++slotIndex)
        {
            auto& slot = slots[static_cast<size_t> (slotIndex)];
            if (slot.isEmpty())
                continue;

            if (auto* ext = dynamic_cast<te::ExternalPlugin*> (getInsertPlugin (trackIndex, slotIndex)))
            {
                ext->flushPluginStateToValueTree();
                auto stateCopy = ext->state.createCopy();
                if (stateCopy.isValid())
                    slot.pluginState = stateCopy;
            }
        }
    }

    for (int slotIndex = 0; slotIndex < static_cast<int> (mixerStatePtr->masterInsertSlots.size()); ++slotIndex)
    {
        auto& slot = mixerStatePtr->masterInsertSlots[static_cast<size_t> (slotIndex)];
        if (slot.isEmpty())
            continue;

        if (auto* ext = dynamic_cast<te::ExternalPlugin*> (getMasterInsertPlugin (slotIndex)))
        {
            ext->flushPluginStateToValueTree();
            slot.pluginState = ext->state.createCopy();
        }
        else
        {
            slot.pluginState = {};
        }
    }
}

void TrackerEngine::snapshotPluginInstrumentStates()
{
    for (auto& [instrumentIndex, info] : instrumentSlotInfos)
    {
        if (! info.isPlugin())
            continue;

        auto instanceIt = pluginInstrumentInstances.find (instrumentIndex);
        if (instanceIt != pluginInstrumentInstances.end() && instanceIt->second != nullptr)
        {
            if (auto* ext = dynamic_cast<te::ExternalPlugin*> (instanceIt->second.get()))
            {
                ext->flushPluginStateToValueTree();
                auto stateCopy = ext->state.createCopy();
                if (stateCopy.isValid())
                    info.pluginState = stateCopy;
            }
        }
    }
}

void TrackerEngine::openPluginEditor (int trackIndex, int slotIndex)
{
    auto* plugin = getInsertPlugin (trackIndex, slotIndex);
    if (plugin == nullptr)
        return;

    juce::String key = juce::String (trackIndex) + ":" + juce::String (slotIndex);
    openExternalPluginEditor (plugin, key);
}

void TrackerEngine::openMasterPluginEditor (int slotIndex)
{
    auto* plugin = getMasterInsertPlugin (slotIndex);
    if (plugin == nullptr)
        return;

    openExternalPluginEditor (plugin, "master:" + juce::String (slotIndex));
}

void TrackerEngine::openExternalPluginEditor (te::Plugin* plugin, const juce::String& key)
{
    if (plugin == nullptr)
        return;

    if (pluginEditorWindows.count (key) > 0 && pluginEditorWindows[key] != nullptr)
    {
        auto* existing = pluginEditorWindows[key].get();
        if (existing->isMinimised())
            existing->setMinimised (false);
        if (! existing->isShowing() || ! existing->isVisible())
            existing->setVisible (true);
        existing->toFront (true);
        return;
    }

    auto* externalPlugin = dynamic_cast<te::ExternalPlugin*> (plugin);
    if (externalPlugin == nullptr)
        return;

    auto audioPlugin = externalPlugin->getAudioPluginInstance();
    if (audioPlugin == nullptr)
        return;

    auto editor = audioPlugin->createEditorIfNeeded();
    if (editor == nullptr)
        return;

    struct PluginEditorWindow : public juce::DocumentWindow
    {
        explicit PluginEditorWindow (const juce::String& name)
            : juce::DocumentWindow (name, juce::Colours::darkgrey,
                                    juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
        {
        }

        void closeButtonPressed() override
        {
            setVisible (false);
        }
    };

    auto window = std::make_unique<PluginEditorWindow> (externalPlugin->getName());

    window->setContentOwned (editor, true);
    window->setResizable (true, false);
    window->centreWithSize (window->getWidth(), window->getHeight());
    window->setVisible (true);

    pluginEditorWindows[key] = std::move (window);
}

void TrackerEngine::closePluginEditor (int trackIndex, int slotIndex)
{
    juce::String key = juce::String (trackIndex) + ":" + juce::String (slotIndex);
    pluginEditorWindows.erase (key);
}

void TrackerEngine::closePluginEditorsForTrack (int trackIndex)
{
    const auto keyPrefix = juce::String (trackIndex) + ":";

    for (auto it = pluginEditorWindows.begin(); it != pluginEditorWindows.end();)
    {
        if (it->first.startsWith (keyPrefix))
            it = pluginEditorWindows.erase (it);
        else
            ++it;
    }
}

void TrackerEngine::closeMasterPluginEditor (int slotIndex)
{
    pluginEditorWindows.erase ("master:" + juce::String (slotIndex));
}

void TrackerEngine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (onTransportChanged)
        onTransportChanged();
}

//==============================================================================
// Plugin instrument slot management (Phase 4)
//==============================================================================

namespace
{
    static const InstrumentSlotInfo kDefaultSlotInfo {};
}

const InstrumentSlotInfo& TrackerEngine::getInstrumentSlotInfo (int instrumentIndex) const
{
    auto it = instrumentSlotInfos.find (instrumentIndex);
    if (it != instrumentSlotInfos.end())
        return it->second;
    return kDefaultSlotInfo;
}

PluginInstrumentModulation& TrackerEngine::getPluginInstrumentModulation (int instrumentIndex)
{
    return instrumentSlotInfos[instrumentIndex].pluginModulation;
}

const PluginInstrumentModulation& TrackerEngine::getPluginInstrumentModulation (int instrumentIndex) const
{
    static const PluginInstrumentModulation kEmptyModulation {};
    auto it = instrumentSlotInfos.find (instrumentIndex);
    if (it != instrumentSlotInfos.end())
        return it->second.pluginModulation;
    return kEmptyModulation;
}

void TrackerEngine::notifyPluginInstrumentModulationChanged (int instrumentIndex)
{
    pluginModulationRuntime[instrumentIndex].params.clear();
    ensurePluginModulationRuntimeSize (instrumentIndex);

    if (onPluginInstrumentModulationChanged)
        onPluginInstrumentModulationChanged (instrumentIndex);
}

bool TrackerEngine::setPluginInstrument (int instrumentIndex, const juce::PluginDescription& desc, int ownerTrack)
{
    if (instrumentIndex < 0 || instrumentIndex >= 256)
        return false;

    if (ownerTrack < 0 || ownerTrack >= kNumTracks)
        return false;

    stopPreview();

    clearPluginInstrumentInternal (instrumentIndex, false);

    auto& info = instrumentSlotInfos[instrumentIndex];
    info.setPlugin (desc, ownerTrack);
    info.pluginModulation.ensureDefaultSources();

    // Load the new plugin on the owner track
    ensurePluginInstrumentLoaded (instrumentIndex);

    return true;
}

void TrackerEngine::clearPluginInstrument (int instrumentIndex)
{
    clearPluginInstrumentInternal (instrumentIndex, true);
}

void TrackerEngine::clearPluginInstrumentInternal (int instrumentIndex, bool notifyAutomation)
{
    if (instrumentIndex < 0)
        return;

    const auto pluginId = "inst:" + juce::String (instrumentIndex);

    closePluginInstrumentEditor (instrumentIndex);
    removePluginInstrumentFromTrack (instrumentIndex);
    forgetAutomatedParamsForPlugin (pluginId);

    if (notifyAutomation && onPluginInstrumentCleared)
        onPluginInstrumentCleared (pluginId);

    instrumentSlotInfos.erase (instrumentIndex);
    pluginInstrumentInstances.erase (instrumentIndex);
    pluginModulationRuntime.erase (instrumentIndex);
}

void TrackerEngine::unloadAllPluginInstruments (bool notifyAutomation)
{
    std::vector<int> instrumentIndices;

    auto addInstrumentIndex = [&instrumentIndices] (int instrumentIndex)
    {
        if (std::find (instrumentIndices.begin(), instrumentIndices.end(), instrumentIndex) == instrumentIndices.end())
            instrumentIndices.push_back (instrumentIndex);
    };

    for (const auto& [instrumentIndex, info] : instrumentSlotInfos)
        if (info.isPlugin())
            addInstrumentIndex (instrumentIndex);

    for (const auto& [instrumentIndex, plugin] : pluginInstrumentInstances)
    {
        juce::ignoreUnused (plugin);
        addInstrumentIndex (instrumentIndex);
    }

    for (const auto& [instrumentIndex, window] : pluginInstrumentEditorWindows)
    {
        juce::ignoreUnused (window);
        addInstrumentIndex (instrumentIndex);
    }

    for (int instrumentIndex : instrumentIndices)
        clearPluginInstrumentInternal (instrumentIndex, notifyAutomation);

    pluginInstrumentEditorWindows.clear();
    pluginInstrumentInstances.clear();
}

bool TrackerEngine::isPluginInstrument (int instrumentIndex) const
{
    auto it = instrumentSlotInfos.find (instrumentIndex);
    return it != instrumentSlotInfos.end() && it->second.isPlugin();
}

int TrackerEngine::getPluginInstrumentOwnerTrack (int instrumentIndex) const
{
    auto it = instrumentSlotInfos.find (instrumentIndex);
    if (it != instrumentSlotInfos.end() && it->second.isPlugin())
        return it->second.ownerTrack;
    return -1;
}

void TrackerEngine::setInstrumentSlotInfos (const std::map<int, InstrumentSlotInfo>& infos)
{
    unloadAllPluginInstruments (false);

    instrumentSlotInfos = infos;
    pluginModulationRuntime.clear();
    invalidateTrackInstruments();
}

TrackContentMode TrackerEngine::getTrackContentMode (int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= kNumTracks)
        return TrackContentMode::Empty;

    for (const auto& [instIdx, info] : instrumentSlotInfos)
    {
        if (info.isPlugin() && info.ownerTrack == trackIndex)
            return TrackContentMode::PluginInstrument;
    }

    return TrackContentMode::Sample;
}

juce::String TrackerEngine::validateNoteEntry (int instrumentIndex, int trackIndex) const
{
    if (instrumentIndex < 0 || trackIndex < 0 || trackIndex >= kNumTracks)
        return {};

    auto it = instrumentSlotInfos.find (instrumentIndex);

    // If the instrument is a plugin instrument, check ownership
    if (it != instrumentSlotInfos.end() && it->second.isPlugin())
    {
        int ownerTrack = it->second.ownerTrack;
        if (ownerTrack >= 0 && ownerTrack != trackIndex)
        {
            return "Plugin instrument " + juce::String::formatted ("%02X", instrumentIndex)
                   + " is owned by track " + juce::String (ownerTrack + 1)
                   + " -- cannot enter notes on track " + juce::String (trackIndex + 1);
        }

        return {};
    }

    // If it's a sample instrument, check that the track isn't in plugin mode
    for (const auto& [instIdx, info] : instrumentSlotInfos)
    {
        if (info.isPlugin() && info.ownerTrack == trackIndex)
        {
            return "Track " + juce::String (trackIndex + 1)
                   + " is in plugin instrument mode -- cannot use sample instrument "
                   + juce::String::formatted ("%02X", instrumentIndex);
        }
    }

    return {};
}

te::Plugin* TrackerEngine::getPluginInstrumentInstance (int instrumentIndex)
{
    auto instanceIt = pluginInstrumentInstances.find (instrumentIndex);
    if (instanceIt != pluginInstrumentInstances.end())
        return instanceIt->second.get();
    return nullptr;
}

juce::AudioPluginInstance* TrackerEngine::getPluginInstrumentAudioPluginInstance (int instrumentIndex)
{
    auto* plugin = getPluginInstrumentInstance (instrumentIndex);
    if (plugin == nullptr)
    {
        ensurePluginInstrumentLoaded (instrumentIndex);
        plugin = getPluginInstrumentInstance (instrumentIndex);
    }

    if (auto* extPlugin = dynamic_cast<te::ExternalPlugin*> (plugin))
        return extPlugin->getAudioPluginInstance();

    return nullptr;
}

void TrackerEngine::ensurePluginInstrumentLoaded (int instrumentIndex)
{
    if (edit == nullptr)
        return;

    auto it = instrumentSlotInfos.find (instrumentIndex);
    if (it == instrumentSlotInfos.end() || ! it->second.isPlugin())
        return;

    int ownerTrack = it->second.ownerTrack;
    if (ownerTrack < 0 || ownerTrack >= kNumTracks)
        return;

    auto* track = getTrack (ownerTrack);
    if (track == nullptr)
        return;

    // Remove any sample-related plugins from the track.  TrackerSamplerPlugin has
    // takesAudioInput()==false, so it would overwrite the plugin instrument's audio
    // output with silence if left in the chain.
    while (auto* samplerPlugin = track->pluginList.findFirstPluginOfType<TrackerSamplerPlugin>())
        samplerPlugin->removeFromParent();
    while (auto* effectsPlugin = track->pluginList.findFirstPluginOfType<InstrumentEffectsPlugin>())
        effectsPlugin->removeFromParent();
    currentTrackInstrument[static_cast<size_t> (ownerTrack)] = -1;

    // Check if already loaded after pruning stale sample plugins. Live note entry
    // calls this path repeatedly while playback is running.
    auto instanceIt = pluginInstrumentInstances.find (instrumentIndex);
    if (instanceIt != pluginInstrumentInstances.end() && instanceIt->second != nullptr)
        return;

    // Try to find a fully-populated description from the known plugin list so that
    // all metadata fields (numInputChannels, numOutputChannels, etc.) are present.
    // Fall back to the saved (partial) description if the plugin hasn't been scanned.
    auto& savedDesc = it->second.pluginDescription;
    const juce::PluginDescription* descToUse = &savedDesc;

    auto& knownList = engine->getPluginManager().knownPluginList;
    for (auto& known : knownList.getTypes())
    {
        if (known.fileOrIdentifier == savedDesc.fileOrIdentifier
            && known.pluginFormatName == savedDesc.pluginFormatName)
        {
            descToUse = &known;
            break;
        }
    }

    // Create the external plugin instance
    auto pluginPtr = edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, *descToUse);

    if (pluginPtr != nullptr)
    {
        // Insert at position 0 -- the plugin instrument acts as the sound source
        track->pluginList.insertPlugin (*pluginPtr, 0, nullptr);
        pluginInstrumentInstances[instrumentIndex] = pluginPtr;

        // Restore plugin state (preset) if available
        if (it->second.pluginState.isValid())
        {
            if (auto* ext = dynamic_cast<te::ExternalPlugin*> (pluginPtr.get()))
                ext->restorePluginStateFromValueTree (it->second.pluginState);
        }
    }
}

void TrackerEngine::removePluginInstrumentFromTrack (int instrumentIndex)
{
    if (previewPluginInstrument == instrumentIndex)
        stopPluginPreview();

    auto instanceIt = pluginInstrumentInstances.find (instrumentIndex);
    if (instanceIt == pluginInstrumentInstances.end() || instanceIt->second == nullptr)
        return;

    auto* plugin = instanceIt->second.get();
    if (plugin != nullptr)
        plugin->deleteFromParent();

    pluginInstrumentInstances.erase (instanceIt);
}

namespace
{
class PluginModulationComponent : public juce::Component
{
public:
    PluginModulationComponent (juce::AudioPluginInstance* api, TrackerEngine& eng, int instIdx)
        : pluginInstance (api), engine (eng), instrumentIndex (instIdx)
    {
        titleLabel.setText ("Plugin modulation", juce::dontSendNotification);
        titleLabel.setJustificationType (juce::Justification::centredLeft);
        titleLabel.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
        addAndMakeVisible (titleLabel);

        setupButton (addLfoButton, "Add LFO");
        addLfoButton.onClick = [this]
        {
            auto& modulation = engine.getPluginInstrumentModulation (instrumentIndex);
            currentSourceIndex = modulation.addLfo();
            engine.notifyPluginInstrumentModulationChanged (instrumentIndex);
            refreshAll();
        };

        setupButton (addEnvelopeButton, "Add Env");
        addEnvelopeButton.onClick = [this]
        {
            auto& modulation = engine.getPluginInstrumentModulation (instrumentIndex);
            currentSourceIndex = modulation.addEnvelope();
            engine.notifyPluginInstrumentModulationChanged (instrumentIndex);
            refreshAll();
        };

        setupButton (removeSourceButton, "Remove Source");
        removeSourceButton.onClick = [this]
        {
            auto& modulation = engine.getPluginInstrumentModulation (instrumentIndex);
            modulation.removeSource (currentSourceIndex);
            currentSourceIndex = juce::jlimit (0, juce::jmax (0, static_cast<int> (modulation.sources.size()) - 1), currentSourceIndex);
            currentRouteIndex = juce::jlimit (0, juce::jmax (0, static_cast<int> (modulation.routes.size()) - 1), currentRouteIndex);
            engine.notifyPluginInstrumentModulationChanged (instrumentIndex);
            refreshAll();
        };

        setupButton (addRouteButton, "Add Route");
        addRouteButton.onClick = [this]
        {
            auto& modulation = engine.getPluginInstrumentModulation (instrumentIndex);
            if (modulation.sources.empty())
                currentSourceIndex = modulation.addLfo();

            if (parameterIndices.empty())
                return;

            PluginModulationRoute route;
            route.sourceIndex = juce::jlimit (0, static_cast<int> (modulation.sources.size()) - 1, currentSourceIndex);
            route.parameterIndex = parameterIndices.front();
            route.parameterName = parameterNames.front();
            route.amount = 0.25f;
            modulation.routes.push_back (route);
            currentRouteIndex = static_cast<int> (modulation.routes.size()) - 1;
            engine.notifyPluginInstrumentModulationChanged (instrumentIndex);
            refreshAll();
        };

        setupButton (removeRouteButton, "Remove Route");
        removeRouteButton.onClick = [this]
        {
            auto& modulation = engine.getPluginInstrumentModulation (instrumentIndex);
            modulation.removeRoute (currentRouteIndex);
            currentRouteIndex = juce::jlimit (0, juce::jmax (0, static_cast<int> (modulation.routes.size()) - 1), currentRouteIndex);
            engine.notifyPluginInstrumentModulationChanged (instrumentIndex);
            refreshAll();
        };

        setupCombo (sourceCombo);
        sourceCombo.onChange = [this]
        {
            if (refreshing) return;
            currentSourceIndex = sourceCombo.getSelectedId() - 1;
            refreshAll();
        };

        sourceTypeLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (sourceTypeLabel);

        setupCombo (lfoShapeCombo);
        lfoShapeCombo.addItem ("Sine", 1);
        lfoShapeCombo.addItem ("Triangle", 2);
        lfoShapeCombo.addItem ("Saw", 3);
        lfoShapeCombo.addItem ("Square", 4);
        lfoShapeCombo.addItem ("Random", 5);
        lfoShapeCombo.onChange = [this]
        {
            if (refreshing) return;
            if (auto* source = getCurrentSource())
            {
                source->lfoShape = static_cast<PluginModulatorSource::LfoShape> (lfoShapeCombo.getSelectedId() - 1);
                engine.notifyPluginInstrumentModulationChanged (instrumentIndex);
            }
        };

        setupCombo (lfoRateModeCombo);
        lfoRateModeCombo.addItem ("Steps", 1);
        lfoRateModeCombo.addItem ("Hz", 2);
        lfoRateModeCombo.onChange = [this]
        {
            if (refreshing) return;
            if (auto* source = getCurrentSource())
            {
                source->lfoRateMode = lfoRateModeCombo.getSelectedId() == 2
                                          ? PluginModulatorSource::LfoRateMode::Hz
                                          : PluginModulatorSource::LfoRateMode::Steps;
                engine.notifyPluginInstrumentModulationChanged (instrumentIndex);
                refreshEnablement();
            }
        };

        setupSlider (lfoStepsSlider, 1.0, 256.0, 1.0, " steps");
        lfoStepsSlider.onValueChange = [this]
        {
            if (refreshing) return;
            if (auto* source = getCurrentSource())
            {
                source->lfoRateSteps = lfoStepsSlider.getValue();
                engine.notifyPluginInstrumentModulationChanged (instrumentIndex);
            }
        };

        setupSlider (lfoHzSlider, 0.01, 40.0, 0.01, " Hz");
        lfoHzSlider.onValueChange = [this]
        {
            if (refreshing) return;
            if (auto* source = getCurrentSource())
            {
                source->lfoRateHz = lfoHzSlider.getValue();
                engine.notifyPluginInstrumentModulationChanged (instrumentIndex);
            }
        };

        setupCombo (envTriggerCombo);
        envTriggerCombo.addItem ("Note Gate", 1);
        envTriggerCombo.addItem ("Step FX", 2);
        envTriggerCombo.onChange = [this]
        {
            if (refreshing) return;
            if (auto* source = getCurrentSource())
            {
                source->envelopeTriggerMode = envTriggerCombo.getSelectedId() == 2
                                                  ? PluginModulatorSource::EnvelopeTriggerMode::StepFxOnly
                                                  : PluginModulatorSource::EnvelopeTriggerMode::NoteGate;
                engine.notifyPluginInstrumentModulationChanged (instrumentIndex);
            }
        };

        setupSlider (attackSlider, 0.0, 30.0, 0.001, " s");
        setupSlider (decaySlider, 0.0, 30.0, 0.001, " s");
        setupSlider (sustainSlider, 0.0, 100.0, 1.0, "%");
        setupSlider (releaseSlider, 0.0, 30.0, 0.001, " s");

        attackSlider.onValueChange = [this] { updateEnvelopeValue(); };
        decaySlider.onValueChange = [this] { updateEnvelopeValue(); };
        sustainSlider.onValueChange = [this] { updateEnvelopeValue(); };
        releaseSlider.onValueChange = [this] { updateEnvelopeValue(); };

        setupCombo (routeCombo);
        routeCombo.onChange = [this]
        {
            if (refreshing) return;
            currentRouteIndex = routeCombo.getSelectedId() - 1;
            refreshAll();
        };

        setupCombo (routeSourceCombo);
        routeSourceCombo.onChange = [this]
        {
            if (refreshing) return;
            if (auto* route = getCurrentRoute())
            {
                route->sourceIndex = routeSourceCombo.getSelectedId() - 1;
                engine.notifyPluginInstrumentModulationChanged (instrumentIndex);
            }
        };

        setupCombo (routeParamCombo);
        routeParamCombo.onChange = [this]
        {
            if (refreshing) return;
            if (auto* route = getCurrentRoute())
            {
                route->parameterIndex = routeParamCombo.getSelectedId() - 1;
                route->parameterName = routeParamCombo.getText();
                engine.notifyPluginInstrumentModulationChanged (instrumentIndex);
            }
        };

        setupSlider (routeAmountSlider, -100.0, 100.0, 1.0, "%");
        routeAmountSlider.onValueChange = [this]
        {
            if (refreshing) return;
            if (auto* route = getCurrentRoute())
            {
                route->amount = static_cast<float> (routeAmountSlider.getValue() / 100.0);
                engine.notifyPluginInstrumentModulationChanged (instrumentIndex);
            }
        };

        routeEnabledButton.setButtonText ("Enabled");
        routeEnabledButton.onClick = [this]
        {
            if (refreshing) return;
            if (auto* route = getCurrentRoute())
            {
                route->enabled = routeEnabledButton.getToggleState();
                engine.notifyPluginInstrumentModulationChanged (instrumentIndex);
            }
        };
        addAndMakeVisible (routeEnabledButton);

        collectParameters();
        refreshAll();
        setSize (620, 360);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff202020));
        g.setColour (juce::Colour (0xffd6d6d6));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));

        auto area = getLocalBounds().reduced (12);
        area.removeFromTop (24);
        area.removeFromTop (6);
        area.removeFromTop (30);
        area.removeFromTop (10);

        auto sourceArea = area.removeFromTop (130);
        auto left = sourceArea.removeFromLeft (292);
        auto right = sourceArea.removeFromLeft (292);
        paintLabelColumn (g, left, { "Type", "Shape", "Rate", "Steps", "Hz" });
        paintLabelColumn (g, right, { "Trigger", "Attack", "Decay", "Sustain", "Release" });

        area.removeFromTop (12);
        area.removeFromTop (30);
        area.removeFromTop (10);
        auto routeArea = area.removeFromTop (120);
        paintLabelColumn (g, routeArea, { "Source", "Param", "Amount" });
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        titleLabel.setBounds (area.removeFromTop (24));
        area.removeFromTop (6);

        auto top = area.removeFromTop (30);
        sourceCombo.setBounds (top.removeFromLeft (170));
        top.removeFromLeft (8);
        addLfoButton.setBounds (top.removeFromLeft (78));
        top.removeFromLeft (6);
        addEnvelopeButton.setBounds (top.removeFromLeft (78));
        top.removeFromLeft (6);
        removeSourceButton.setBounds (top.removeFromLeft (124));

        area.removeFromTop (10);
        auto sourceArea = area.removeFromTop (130);
        auto left = sourceArea.removeFromLeft (292);
        auto right = sourceArea.removeFromLeft (292);

        layoutLabelled (left, "Type", sourceTypeLabel);
        layoutLabelled (left, "Shape", lfoShapeCombo);
        layoutLabelled (left, "Rate", lfoRateModeCombo);
        layoutLabelled (left, "Steps", lfoStepsSlider);
        layoutLabelled (left, "Hz", lfoHzSlider);

        layoutLabelled (right, "Trigger", envTriggerCombo);
        layoutLabelled (right, "Attack", attackSlider);
        layoutLabelled (right, "Decay", decaySlider);
        layoutLabelled (right, "Sustain", sustainSlider);
        layoutLabelled (right, "Release", releaseSlider);

        area.removeFromTop (12);
        auto routeTop = area.removeFromTop (30);
        routeCombo.setBounds (routeTop.removeFromLeft (170));
        routeTop.removeFromLeft (8);
        addRouteButton.setBounds (routeTop.removeFromLeft (92));
        routeTop.removeFromLeft (6);
        removeRouteButton.setBounds (routeTop.removeFromLeft (104));

        area.removeFromTop (10);
        auto routeArea = area.removeFromTop (120);
        layoutLabelled (routeArea, "Source", routeSourceCombo);
        layoutLabelled (routeArea, "Param", routeParamCombo);
        layoutLabelled (routeArea, "Amount", routeAmountSlider);
        routeEnabledButton.setBounds (routeArea.removeFromTop (26).removeFromLeft (120));
    }

private:
    juce::AudioPluginInstance* pluginInstance = nullptr;
    TrackerEngine& engine;
    int instrumentIndex = -1;
    int currentSourceIndex = 0;
    int currentRouteIndex = 0;
    bool refreshing = false;
    std::vector<int> parameterIndices;
    std::vector<juce::String> parameterNames;

    juce::Label titleLabel;
    juce::ComboBox sourceCombo;
    juce::TextButton addLfoButton;
    juce::TextButton addEnvelopeButton;
    juce::TextButton removeSourceButton;
    juce::Label sourceTypeLabel;
    juce::ComboBox lfoShapeCombo;
    juce::ComboBox lfoRateModeCombo;
    juce::Slider lfoStepsSlider;
    juce::Slider lfoHzSlider;
    juce::ComboBox envTriggerCombo;
    juce::Slider attackSlider;
    juce::Slider decaySlider;
    juce::Slider sustainSlider;
    juce::Slider releaseSlider;
    juce::ComboBox routeCombo;
    juce::TextButton addRouteButton;
    juce::TextButton removeRouteButton;
    juce::ComboBox routeSourceCombo;
    juce::ComboBox routeParamCombo;
    juce::Slider routeAmountSlider;
    juce::ToggleButton routeEnabledButton;

    void setupButton (juce::TextButton& button, const juce::String& text)
    {
        button.setButtonText (text);
        button.setWantsKeyboardFocus (false);
        addAndMakeVisible (button);
    }

    void setupCombo (juce::ComboBox& combo)
    {
        combo.setWantsKeyboardFocus (false);
        addAndMakeVisible (combo);
    }

    void setupSlider (juce::Slider& slider, double min, double max, double interval, const juce::String& suffix)
    {
        slider.setRange (min, max, interval);
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 76, 20);
        slider.setTextValueSuffix (suffix);
        slider.setWantsKeyboardFocus (false);
        addAndMakeVisible (slider);
    }

    void layoutLabelled (juce::Rectangle<int>& area, const juce::String& text, juce::Component& component)
    {
        auto row = area.removeFromTop (26);
        row.removeFromLeft (58);
        juce::ignoreUnused (text);
        component.setBounds (row);
        area.removeFromTop (4);
    }

    static void paintLabelColumn (juce::Graphics& g,
                                  juce::Rectangle<int> area,
                                  std::initializer_list<const char*> labels)
    {
        for (auto* label : labels)
        {
            auto row = area.removeFromTop (26);
            g.drawFittedText (label, row.removeFromLeft (54), juce::Justification::centredLeft, 1);
            area.removeFromTop (4);
        }
    }

    PluginModulatorSource* getCurrentSource()
    {
        auto& modulation = engine.getPluginInstrumentModulation (instrumentIndex);
        if (currentSourceIndex < 0 || currentSourceIndex >= static_cast<int> (modulation.sources.size()))
            return nullptr;
        return &modulation.sources[static_cast<size_t> (currentSourceIndex)];
    }

    PluginModulationRoute* getCurrentRoute()
    {
        auto& modulation = engine.getPluginInstrumentModulation (instrumentIndex);
        if (currentRouteIndex < 0 || currentRouteIndex >= static_cast<int> (modulation.routes.size()))
            return nullptr;
        return &modulation.routes[static_cast<size_t> (currentRouteIndex)];
    }

    void collectParameters()
    {
        parameterIndices.clear();
        parameterNames.clear();

        if (pluginInstance == nullptr)
            return;

        auto& params = pluginInstance->getParameters();
        for (int i = 0; i < params.size(); ++i)
        {
            auto* param = params[i];
            if (param == nullptr)
                continue;

            parameterIndices.push_back (i);
            parameterNames.push_back (param->getName (48));
        }
    }

    void refreshAll()
    {
        const juce::ScopedValueSetter<bool> setter (refreshing, true);
        auto& modulation = engine.getPluginInstrumentModulation (instrumentIndex);

        currentSourceIndex = juce::jlimit (0, juce::jmax (0, static_cast<int> (modulation.sources.size()) - 1), currentSourceIndex);
        currentRouteIndex = juce::jlimit (0, juce::jmax (0, static_cast<int> (modulation.routes.size()) - 1), currentRouteIndex);

        sourceCombo.clear (juce::dontSendNotification);
        for (int i = 0; i < static_cast<int> (modulation.sources.size()); ++i)
        {
            const auto& source = modulation.sources[static_cast<size_t> (i)];
            const auto fallback = source.type == PluginModulatorSource::Type::LFO ? "LFO " : "Env ";
            sourceCombo.addItem (source.name.isNotEmpty() ? source.name : fallback + juce::String (i + 1), i + 1);
        }
        sourceCombo.setSelectedId (modulation.sources.empty() ? 0 : currentSourceIndex + 1, juce::dontSendNotification);

        routeCombo.clear (juce::dontSendNotification);
        for (int i = 0; i < static_cast<int> (modulation.routes.size()); ++i)
        {
            const auto& route = modulation.routes[static_cast<size_t> (i)];
            const auto target = route.parameterName.isNotEmpty() ? route.parameterName
                                                                 : "Param " + juce::String (route.parameterIndex);
            routeCombo.addItem ("Route " + juce::String (i + 1) + " -> " + target, i + 1);
        }
        routeCombo.setSelectedId (modulation.routes.empty() ? 0 : currentRouteIndex + 1, juce::dontSendNotification);

        routeSourceCombo.clear (juce::dontSendNotification);
        for (int i = 0; i < static_cast<int> (modulation.sources.size()); ++i)
        {
            const auto& source = modulation.sources[static_cast<size_t> (i)];
            const auto fallback = source.type == PluginModulatorSource::Type::LFO ? "LFO " : "Env ";
            routeSourceCombo.addItem (source.name.isNotEmpty() ? source.name : fallback + juce::String (i + 1), i + 1);
        }

        routeParamCombo.clear (juce::dontSendNotification);
        for (int i = 0; i < static_cast<int> (parameterIndices.size()); ++i)
            routeParamCombo.addItem (parameterNames[static_cast<size_t> (i)], parameterIndices[static_cast<size_t> (i)] + 1);

        if (auto* source = getCurrentSource())
        {
            sourceTypeLabel.setText (source->type == PluginModulatorSource::Type::LFO ? "LFO" : "Envelope",
                                     juce::dontSendNotification);
            lfoShapeCombo.setSelectedId (static_cast<int> (source->lfoShape) + 1, juce::dontSendNotification);
            lfoRateModeCombo.setSelectedId (source->lfoRateMode == PluginModulatorSource::LfoRateMode::Hz ? 2 : 1,
                                            juce::dontSendNotification);
            lfoStepsSlider.setValue (source->lfoRateSteps, juce::dontSendNotification);
            lfoHzSlider.setValue (source->lfoRateHz, juce::dontSendNotification);
            envTriggerCombo.setSelectedId (source->envelopeTriggerMode == PluginModulatorSource::EnvelopeTriggerMode::StepFxOnly ? 2 : 1,
                                           juce::dontSendNotification);
            attackSlider.setValue (source->attackS, juce::dontSendNotification);
            decaySlider.setValue (source->decayS, juce::dontSendNotification);
            sustainSlider.setValue (source->sustain * 100.0, juce::dontSendNotification);
            releaseSlider.setValue (source->releaseS, juce::dontSendNotification);
        }
        else
        {
            sourceTypeLabel.setText ("None", juce::dontSendNotification);
        }

        if (auto* route = getCurrentRoute())
        {
            routeSourceCombo.setSelectedId (route->sourceIndex + 1, juce::dontSendNotification);
            routeParamCombo.setSelectedId (route->parameterIndex + 1, juce::dontSendNotification);
            routeAmountSlider.setValue (static_cast<double> (route->amount) * 100.0, juce::dontSendNotification);
            routeEnabledButton.setToggleState (route->enabled, juce::dontSendNotification);
        }
        else
        {
            routeSourceCombo.setSelectedId (0, juce::dontSendNotification);
            routeParamCombo.setSelectedId (0, juce::dontSendNotification);
            routeAmountSlider.setValue (0.0, juce::dontSendNotification);
            routeEnabledButton.setToggleState (false, juce::dontSendNotification);
        }

        refreshEnablement();
    }

    void refreshEnablement()
    {
        auto* source = getCurrentSource();
        auto* route = getCurrentRoute();
        const bool hasSource = source != nullptr;
        const bool sourceIsLfo = hasSource && source->type == PluginModulatorSource::Type::LFO;
        const bool sourceIsEnvelope = hasSource && source->type == PluginModulatorSource::Type::Envelope;

        removeSourceButton.setEnabled (hasSource);
        lfoShapeCombo.setEnabled (sourceIsLfo);
        lfoRateModeCombo.setEnabled (sourceIsLfo);
        lfoStepsSlider.setEnabled (sourceIsLfo && source->lfoRateMode == PluginModulatorSource::LfoRateMode::Steps);
        lfoHzSlider.setEnabled (sourceIsLfo && source->lfoRateMode == PluginModulatorSource::LfoRateMode::Hz);
        envTriggerCombo.setEnabled (sourceIsEnvelope);
        attackSlider.setEnabled (sourceIsEnvelope);
        decaySlider.setEnabled (sourceIsEnvelope);
        sustainSlider.setEnabled (sourceIsEnvelope);
        releaseSlider.setEnabled (sourceIsEnvelope);

        addRouteButton.setEnabled (! parameterIndices.empty());
        removeRouteButton.setEnabled (route != nullptr);
        routeSourceCombo.setEnabled (route != nullptr);
        routeParamCombo.setEnabled (route != nullptr);
        routeAmountSlider.setEnabled (route != nullptr);
        routeEnabledButton.setEnabled (route != nullptr);
    }

    void updateEnvelopeValue()
    {
        if (refreshing)
            return;

        if (auto* source = getCurrentSource())
        {
            if (source->type != PluginModulatorSource::Type::Envelope)
                return;

            source->attackS = attackSlider.getValue();
            source->decayS = decaySlider.getValue();
            source->sustain = sustainSlider.getValue() / 100.0;
            source->releaseS = releaseSlider.getValue();
            engine.notifyPluginInstrumentModulationChanged (instrumentIndex);
        }
    }
};
}

void TrackerEngine::openPluginInstrumentEditor (int instrumentIndex)
{
    auto* plugin = getPluginInstrumentInstance (instrumentIndex);
    if (plugin == nullptr)
    {
        ensurePluginInstrumentLoaded (instrumentIndex);
        plugin = getPluginInstrumentInstance (instrumentIndex);
        if (plugin == nullptr)
        {
            if (onStatusMessage)
                onStatusMessage ("Failed to load plugin instrument " + juce::String::formatted ("%02X", instrumentIndex), true, 3000);
            return;
        }
    }

    // Check if a window already exists; recover from stale hidden entries.
    if (auto it = pluginInstrumentEditorWindows.find (instrumentIndex);
        it != pluginInstrumentEditorWindows.end())
    {
        auto* existing = it->second.get();
        if (existing != nullptr)
        {
            if (existing->isMinimised())
                existing->setMinimised (false);

            if (! existing->isShowing() || ! existing->isVisible())
                existing->setVisible (true);

            existing->toFront (true);
            return;
        }
        else
        {
            pluginInstrumentEditorWindows.erase (it);
        }
    }

    auto* extPlugin = dynamic_cast<te::ExternalPlugin*> (plugin);
    if (extPlugin == nullptr)
        return;

    auto audioPlugin = extPlugin->getAudioPluginInstance();
    if (audioPlugin == nullptr)
        return;

    auto* editor = audioPlugin->createEditorIfNeeded();
    if (editor == nullptr)
        return;

    //==========================================================================
    // Content component: wraps the VST editor + toolbar at the bottom.
    //==========================================================================
    struct PluginEditorContent : public juce::Component,
                                 public juce::KeyListener,
                                 private juce::Timer
    {
        using juce::Component::keyPressed;
        using juce::Component::keyStateChanged;

        PluginEditorContent (juce::AudioProcessorEditor* ed,
                             juce::AudioPluginInstance* api,
                             TrackerEngine& eng,
                             int instIdx)
            : vstEditor (ed), pluginInstance (api), engine (eng), instrumentIndex (instIdx)
        {
            addAndMakeVisible (vstEditor);
            addKeyHookToComponentTree (*vstEditor);

            previewKbButton.setButtonText ("Preview KB");
            previewKbButton.setClickingTogglesState (true);
            previewKbButton.setWantsKeyboardFocus (false);
            previewKbButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::steelblue);
            previewKbButton.onClick = [this]
            {
                setPreviewKeyboardEnabled (previewKbButton.getToggleState());
            };
            addAndMakeVisible (previewKbButton);

            autoLearnButton.setButtonText ("Auto Learn");
            autoLearnButton.setClickingTogglesState (true);
            autoLearnButton.setWantsKeyboardFocus (false);
            autoLearnButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::orange);
            autoLearnButton.onClick = [this]
            {
                bool enabled = autoLearnButton.getToggleState();
                autoLearnEnabled = enabled;
                lastDispatchedAutoLearnParam = -1;

                if (enabled)
                    captureAutoLearnSnapshot();

                updatePollingTimerState();
            };
            addAndMakeVisible (autoLearnButton);

            octaveLabel.setText ("Oct: " + juce::String (currentOctave), juce::dontSendNotification);
            octaveLabel.setWantsKeyboardFocus (false);
            octaveLabel.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (octaveLabel);

            setWantsKeyboardFocus (true);
            previewKbButton.setToggleState (true, juce::dontSendNotification);
            setPreviewKeyboardEnabled (true);

            auto edW = vstEditor->getWidth();
            auto edH = vstEditor->getHeight();
            setSize (juce::jmax (edW, 430), edH + kToolbarHeight);
        }

        ~PluginEditorContent() override
        {
            stopTimer();
            releaseHeldPreviewNotes();
            removeKeyHookFromComponentTree (*vstEditor);
        }

        void resized() override
        {
            addKeyHookToComponentTree (*vstEditor);

            auto area = getLocalBounds();
            auto toolbar = area.removeFromBottom (kToolbarHeight);

            vstEditor->setBounds (area);

            previewKbButton.setBounds (toolbar.removeFromLeft (100).reduced (4));
            octaveLabel.setBounds (toolbar.removeFromLeft (60).reduced (4));
            autoLearnButton.setBounds (toolbar.removeFromLeft (100).reduced (4));
        }

        bool keyPressed (const juce::KeyPress& key, juce::Component*) override
        {
            if (! previewKbButton.getToggleState())
                return false;

            if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown() || key.getModifiers().isAltDown())
                return false;

            // Octave change: F1-F8
            auto keyCode = key.getKeyCode();
            if (keyCode >= juce::KeyPress::F1Key && keyCode <= juce::KeyPress::F8Key)
            {
                currentOctave = keyCode - juce::KeyPress::F1Key;
                octaveLabel.setText ("Oct: " + juce::String (currentOctave), juce::dontSendNotification);
                return true;
            }

            int note = getMappedNoteForKeyCode (keyCode);
            if (note < 0 || note > 127)
                return false;

            auto pressedKeyCode = normaliseAlphaKeyCode (keyCode);
            if (heldNotesByKeyCode.find (pressedKeyCode) == heldNotesByKeyCode.end())
            {
                engine.previewNote (0, instrumentIndex, note, false);
                heldNotesByKeyCode[pressedKeyCode] = note;
            }
            return true;
        }

        bool keyStateChanged (bool isKeyDown, juce::Component*) override
        {
            if (! previewKbButton.getToggleState())
                return false;

            // Check which held notes are no longer pressed
            bool handled = false;
            auto it = heldNotesByKeyCode.begin();
            while (it != heldNotesByKeyCode.end())
            {
                bool stillDown = juce::KeyPress::isKeyCurrentlyDown (it->first);
                if (! stillDown)
                {
                    engine.stopPreviewNote (instrumentIndex, it->second);
                    it = heldNotesByKeyCode.erase (it);
                    handled = true;
                }
                else
                {
                    ++it;
                }
            }

            juce::ignoreUnused (isKeyDown);
            return handled;
        }

        void timerCallback() override
        {
            pollAutoLearnParameterChanges();

            if (! previewKeyboardEnabled)
                return;

            // Don't keep sounding notes if this editor window loses focus.
            if (auto* topLevel = findParentComponentOfClass<juce::TopLevelWindow>())
            {
                if (! topLevel->isActiveWindow())
                {
                    releaseHeldPreviewNotes();
                    return;
                }
            }

            pollOctaveKeys();
            pollMappedNoteKeys();
        }

    private:
        enum { kToolbarHeight = 32 };

        juce::AudioProcessorEditor* vstEditor;
        juce::AudioPluginInstance* pluginInstance;
        TrackerEngine& engine;
        int instrumentIndex;
        int currentOctave = 4;

        juce::TextButton previewKbButton;
        juce::TextButton autoLearnButton;
        juce::Label octaveLabel;

        bool autoLearnEnabled = false;
        int lastDispatchedAutoLearnParam = -1;
        std::vector<float> autoLearnParamSnapshot;
        bool previewKeyboardEnabled = false;

        std::map<int, int> heldNotesByKeyCode;
        bool octaveKeysDown[8] = { false, false, false, false, false, false, false, false };

        void flushAutoLearnNavigation (int parameterIndex)
        {
            if (! autoLearnEnabled)
                return;

            if (parameterIndex < 0)
                return;
            if (parameterIndex == lastDispatchedAutoLearnParam)
                return;

            lastDispatchedAutoLearnParam = parameterIndex;

            if (engine.onNavigateToAutomation)
            {
                auto pluginId = "inst:" + juce::String (instrumentIndex);
                engine.onNavigateToAutomation (pluginId, parameterIndex);
            }

            // One-shot learn: after capturing a parameter, return to idle mode.
            if (autoLearnButton.getToggleState())
            {
                autoLearnButton.setToggleState (false, juce::dontSendNotification);
                autoLearnEnabled = false;
                updatePollingTimerState();
            }
        }

        void captureAutoLearnSnapshot()
        {
            autoLearnParamSnapshot.clear();

            if (pluginInstance == nullptr)
                return;

            // tryEnter: audio thread may hold the callback lock (playInStopEnabled).
            auto& lock = pluginInstance->getCallbackLock();
            if (! lock.tryEnter())
                return;

            auto& params = pluginInstance->getParameters();
            autoLearnParamSnapshot.reserve (static_cast<size_t> (params.size()));

            for (int i = 0; i < params.size(); ++i)
            {
                auto* p = params[i];
                autoLearnParamSnapshot.push_back (p != nullptr ? p->getValue() : 0.0f);
            }

            lock.exit();
        }

        void pollAutoLearnParameterChanges()
        {
            if (! autoLearnEnabled || pluginInstance == nullptr)
                return;

            // tryEnter: audio thread may hold the callback lock (playInStopEnabled).
            // If we can't get the lock, skip this poll cycle — the next timer
            // tick will try again.
            auto& lock = pluginInstance->getCallbackLock();
            if (! lock.tryEnter())
                return;

            auto& params = pluginInstance->getParameters();
            if (params.isEmpty())
            {
                lock.exit();
                return;
            }

            if (autoLearnParamSnapshot.size() != static_cast<size_t> (params.size()))
            {
                lock.exit();
                captureAutoLearnSnapshot();
                return;
            }

            constexpr float kLearnThreshold = 0.004f;
            int changedParam = -1;
            float maxDelta = kLearnThreshold;
            const auto pluginId = "inst:" + juce::String (instrumentIndex);

            for (int i = 0; i < params.size(); ++i)
            {
                auto* p = params[i];
                if (p == nullptr)
                    continue;

                float current = p->getValue();
                float delta = std::abs (current - autoLearnParamSnapshot[static_cast<size_t> (i)]);
                autoLearnParamSnapshot[static_cast<size_t> (i)] = current;

                if (engine.shouldSuppressPluginAutoLearnChange (pluginId, i, current))
                    continue;

                if (delta > maxDelta)
                {
                    maxDelta = delta;
                    changedParam = i;
                }
            }

            lock.exit();

            if (changedParam >= 0)
                flushAutoLearnNavigation (changedParam);
        }

        static int normaliseAlphaKeyCode (int keyCode)
        {
            if (keyCode >= 'a' && keyCode <= 'z')
                return keyCode - ('a' - 'A');
            return keyCode;
        }

        int getMappedNoteForKeyCode (int keyCode) const
        {
            keyCode = normaliseAlphaKeyCode (keyCode);

            int baseNote = currentOctave * 12;
            int upperBase = (currentOctave + 1) * 12;
            switch (keyCode)
            {
                case 'Z': return baseNote + 0;
                case 'S': return baseNote + 1;
                case 'X': return baseNote + 2;
                case 'D': return baseNote + 3;
                case 'C': return baseNote + 4;
                case 'V': return baseNote + 5;
                case 'G': return baseNote + 6;
                case 'B': return baseNote + 7;
                case 'H': return baseNote + 8;
                case 'N': return baseNote + 9;
                case 'J': return baseNote + 10;
                case 'M': return baseNote + 11;
                case 'Q': return upperBase + 0;
                case '2': return upperBase + 1;
                case 'W': return upperBase + 2;
                case '3': return upperBase + 3;
                case 'E': return upperBase + 4;
                case 'R': return upperBase + 5;
                case '5': return upperBase + 6;
                case 'T': return upperBase + 7;
                case '6': return upperBase + 8;
                case 'Y': return upperBase + 9;
                case '7': return upperBase + 10;
                case 'U': return upperBase + 11;
                default: break;
            }

            return -1;
        }

        void releaseHeldPreviewNotes()
        {
            for (const auto& held : heldNotesByKeyCode)
                engine.stopPreviewNote (instrumentIndex, held.second);

            heldNotesByKeyCode.clear();
        }

        void setPreviewKeyboardEnabled (bool enabled)
        {
            previewKeyboardEnabled = enabled;

            if (! enabled)
            {
                releaseHeldPreviewNotes();
                for (bool& down : octaveKeysDown)
                    down = false;
            }
            else
            {
                grabKeyboardFocus();
            }

            updatePollingTimerState();
        }

        void updatePollingTimerState()
        {
            bool shouldPoll = previewKeyboardEnabled
                              || autoLearnEnabled;

            if (shouldPoll)
                startTimerHz (75);
            else
                stopTimer();
        }

        void pollOctaveKeys()
        {
            for (int i = 0; i < 8; ++i)
            {
                int keyCode = juce::KeyPress::F1Key + i;
                bool down = juce::KeyPress::isKeyCurrentlyDown (keyCode);

                if (down && ! octaveKeysDown[i])
                {
                    currentOctave = i;
                    octaveLabel.setText ("Oct: " + juce::String (currentOctave), juce::dontSendNotification);
                }

                octaveKeysDown[i] = down;
            }
        }

        void pollMappedNoteKeys()
        {
            static constexpr int keyCodes[] =
            {
                'Z', 'S', 'X', 'D', 'C', 'V', 'G', 'B', 'H', 'N', 'J', 'M',
                'Q', '2', 'W', '3', 'E', 'R', '5', 'T', '6', 'Y', '7', 'U'
            };

            for (auto keyCode : keyCodes)
            {
                bool down = juce::KeyPress::isKeyCurrentlyDown (keyCode);
                auto it = heldNotesByKeyCode.find (keyCode);

                if (down && it == heldNotesByKeyCode.end())
                {
                    int note = getMappedNoteForKeyCode (keyCode);
                    if (note >= 0 && note <= 127)
                    {
                        engine.previewNote (0, instrumentIndex, note, false);
                        heldNotesByKeyCode[keyCode] = note;
                    }
                }
                else if (! down && it != heldNotesByKeyCode.end())
                {
                    engine.stopPreviewNote (instrumentIndex, it->second);
                    heldNotesByKeyCode.erase (it);
                }
            }
        }

        void addKeyHookToComponentTree (juce::Component& component)
        {
            component.addKeyListener (this);
            for (int i = 0; i < component.getNumChildComponents(); ++i)
                addKeyHookToComponentTree (*component.getChildComponent (i));
        }

        void removeKeyHookFromComponentTree (juce::Component& component)
        {
            component.removeKeyListener (this);
            for (int i = 0; i < component.getNumChildComponents(); ++i)
                removeKeyHookFromComponentTree (*component.getChildComponent (i));
        }
    };

    //==========================================================================
    // Window wrapper
    //==========================================================================
    struct PluginInstrumentEditorWindow : public juce::DocumentWindow
    {
        PluginInstrumentEditorWindow (const juce::String& name)
            : juce::DocumentWindow (name, juce::Colours::darkgrey,
                                    juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
        {
        }

        void closeButtonPressed() override
        {
            // Hide instead of destroy to avoid repeated editor teardown races.
            setVisible (false);
        }
    };

    auto* content = new PluginEditorContent (editor, audioPlugin, *this, instrumentIndex);

    auto window = std::make_unique<PluginInstrumentEditorWindow> (extPlugin->getName());

    window->setContentOwned (content, true);
    window->setResizable (true, false);
    window->centreWithSize (window->getWidth(), window->getHeight());
    window->setVisible (true);
    window->toFront (true);
    content->grabKeyboardFocus();

    // Keep a window-level hook as a fallback for editor implementations that
    // don't route key events through child JUCE components.
    window->addKeyListener (content);

    pluginInstrumentEditorWindows[instrumentIndex] = std::move (window);
}

void TrackerEngine::closePluginInstrumentEditor (int instrumentIndex)
{
    pluginInstrumentEditorWindows.erase (instrumentIndex);
}

//==============================================================================
// Plugin instrument modulation
//==============================================================================

namespace
{
    constexpr float kPluginModulationEpsilon = 1.0e-5f;
}

float TrackerEngine::evaluatePluginLfo (double phase,
                                        PluginModulatorSource::LfoShape shape,
                                        PluginModulatorRuntime& state)
{
    const auto p = static_cast<float> (phase - std::floor (phase));

    switch (shape)
    {
        case PluginModulatorSource::LfoShape::Sine:
            return std::sin (juce::MathConstants<float>::twoPi * p);
        case PluginModulatorSource::LfoShape::Triangle:
            return p < 0.5f ? -1.0f + 4.0f * p : 3.0f - 4.0f * p;
        case PluginModulatorSource::LfoShape::Saw:
            return -1.0f + 2.0f * p;
        case PluginModulatorSource::LfoShape::Square:
            return p < 0.5f ? 1.0f : -1.0f;
        case PluginModulatorSource::LfoShape::Random:
            if (state.randomNeedsNew)
            {
                state.randomHoldValue = juce::Random::getSystemRandom().nextFloat() * 2.0f - 1.0f;
                state.randomNeedsNew = false;
            }
            return state.randomHoldValue;
    }

    return 0.0f;
}

float TrackerEngine::advancePluginEnvelope (PluginModulatorRuntime& state,
                                            const PluginModulatorSource& source,
                                            double deltaSeconds)
{
    using EnvStage = PluginModulatorRuntime::EnvStage;

    switch (state.envStage)
    {
        case EnvStage::Idle:
            state.envLevel = 0.0f;
            break;
        case EnvStage::Attack:
        {
            const double attack = juce::jmax (0.001, source.attackS);
            state.envLevel += static_cast<float> (deltaSeconds / attack);
            if (state.envLevel >= 1.0f)
            {
                state.envLevel = 1.0f;
                state.envStage = EnvStage::Decay;
            }
            break;
        }
        case EnvStage::Decay:
        {
            const double decay = juce::jmax (0.001, source.decayS);
            const float sustain = static_cast<float> (juce::jlimit (0.0, 1.0, source.sustain));
            state.envLevel -= static_cast<float> (deltaSeconds / decay) * (1.0f - sustain);
            if (state.envLevel <= sustain)
            {
                state.envLevel = sustain;
                state.envStage = EnvStage::Sustain;
            }
            break;
        }
        case EnvStage::Sustain:
            state.envLevel = static_cast<float> (juce::jlimit (0.0, 1.0, source.sustain));
            break;
        case EnvStage::Release:
        {
            const double release = juce::jmax (0.001, source.releaseS);
            state.envLevel -= static_cast<float> (deltaSeconds / release) * state.envLevel;
            if (state.envLevel < 0.001f)
            {
                state.envLevel = 0.0f;
                state.envStage = EnvStage::Idle;
            }
            break;
        }
    }

    return juce::jlimit (0.0f, 1.0f, state.envLevel);
}

void TrackerEngine::ensurePluginModulationRuntimeSize (int instrumentIndex)
{
    auto it = instrumentSlotInfos.find (instrumentIndex);
    if (it == instrumentSlotInfos.end())
        return;

    auto& runtime = pluginModulationRuntime[instrumentIndex];
    const auto sourceCount = it->second.pluginModulation.sources.size();
    if (runtime.sources.size() != sourceCount)
        runtime.sources.resize (sourceCount);
}

void TrackerEngine::triggerPluginEnvelope (int instrumentIndex,
                                           int sourceIndex,
                                           PluginModulatorSource::EnvelopeTriggerMode triggerMode)
{
    auto it = instrumentSlotInfos.find (instrumentIndex);
    if (it == instrumentSlotInfos.end() || ! it->second.isPlugin())
        return;

    ensurePluginModulationRuntimeSize (instrumentIndex);
    auto& runtime = pluginModulationRuntime[instrumentIndex];
    const auto& sources = it->second.pluginModulation.sources;

    for (int i = 0; i < static_cast<int> (sources.size()); ++i)
    {
        if (sourceIndex >= 0 && sourceIndex != i)
            continue;

        const auto& source = sources[static_cast<size_t> (i)];
        if (! source.enabled
            || source.type != PluginModulatorSource::Type::Envelope
            || source.envelopeTriggerMode != triggerMode)
            continue;

        auto& state = runtime.sources[static_cast<size_t> (i)];
        state.envStage = PluginModulatorRuntime::EnvStage::Attack;
        state.envLevel = 0.0f;
    }
}

void TrackerEngine::releasePluginEnvelope (int instrumentIndex,
                                           int sourceIndex,
                                           PluginModulatorSource::EnvelopeTriggerMode triggerMode)
{
    auto it = instrumentSlotInfos.find (instrumentIndex);
    if (it == instrumentSlotInfos.end() || ! it->second.isPlugin())
        return;

    ensurePluginModulationRuntimeSize (instrumentIndex);
    auto& runtime = pluginModulationRuntime[instrumentIndex];
    const auto& sources = it->second.pluginModulation.sources;

    for (int i = 0; i < static_cast<int> (sources.size()); ++i)
    {
        if (sourceIndex >= 0 && sourceIndex != i)
            continue;

        const auto& source = sources[static_cast<size_t> (i)];
        if (! source.enabled
            || source.type != PluginModulatorSource::Type::Envelope
            || source.envelopeTriggerMode != triggerMode)
            continue;

        auto& state = runtime.sources[static_cast<size_t> (i)];
        if (state.envStage != PluginModulatorRuntime::EnvStage::Idle)
            state.envStage = PluginModulatorRuntime::EnvStage::Release;
    }
}

void TrackerEngine::triggerPluginInstrumentNoteModulators (int instrumentIndex)
{
    triggerPluginEnvelope (instrumentIndex, -1, PluginModulatorSource::EnvelopeTriggerMode::NoteGate);
}

void TrackerEngine::releasePluginInstrumentNoteModulators (int instrumentIndex)
{
    releasePluginEnvelope (instrumentIndex, -1, PluginModulatorSource::EnvelopeTriggerMode::NoteGate);
}

void TrackerEngine::triggerPluginInstrumentStepModulatorsForTrack (int trackIndex, int sourceIndex)
{
    for (const auto& [instrumentIndex, info] : instrumentSlotInfos)
        if (info.isPlugin() && info.ownerTrack == trackIndex)
            triggerPluginEnvelope (instrumentIndex, sourceIndex, PluginModulatorSource::EnvelopeTriggerMode::StepFxOnly);
}

void TrackerEngine::releasePluginInstrumentStepModulatorsForTrack (int trackIndex, int sourceIndex)
{
    for (const auto& [instrumentIndex, info] : instrumentSlotInfos)
        if (info.isPlugin() && info.ownerTrack == trackIndex)
            releasePluginEnvelope (instrumentIndex, sourceIndex, PluginModulatorSource::EnvelopeTriggerMode::StepFxOnly);
}

void TrackerEngine::releaseAllPluginInstrumentNoteModulators()
{
    for (const auto& [instrumentIndex, info] : instrumentSlotInfos)
        if (info.isPlugin())
            releasePluginInstrumentNoteModulators (instrumentIndex);
}

void TrackerEngine::resetPluginInstrumentModulations()
{
    for (auto& [instrumentIndex, runtime] : pluginModulationRuntime)
    {
        auto* audioPlugin = resolvePluginInstance ("inst:" + juce::String (instrumentIndex));
        if (audioPlugin == nullptr)
            continue;

        auto& params = audioPlugin->getParameters();
        auto& lock = audioPlugin->getCallbackLock();
        if (! lock.tryEnter())
            continue;

        for (auto& [paramIndex, paramState] : runtime.params)
        {
            if (! paramState.valid || paramIndex < 0 || paramIndex >= params.size())
                continue;

            auto* param = params[paramIndex];
            if (param == nullptr)
                continue;

            const float base = juce::jlimit (0.0f, 1.0f, paramState.baseValue);
            if (std::abs (param->getValue() - base) > kPluginModulationEpsilon)
            {
                param->setValue (base);
                rememberAutomatedParamWrite ("inst:" + juce::String (instrumentIndex), paramIndex, param->getValue());
            }
        }

        lock.exit();
    }

    pluginModulationRuntime.clear();
}

void TrackerEngine::applyPluginInstrumentModulations (double transportBeat,
                                                      const juce::String& excludedPluginId,
                                                      int excludedParamIndex)
{
    const auto nowMs = juce::Time::getMillisecondCounter();
    const double bpm = getBpm();
    const double rowsPerSecond = static_cast<double> (juce::jmax (1, rowsPerBeat)) * bpm / 60.0;

    for (const auto& [instrumentIndex, info] : instrumentSlotInfos)
    {
        if (! info.isPlugin() || info.pluginModulation.isDefault())
            continue;

        const auto pluginId = "inst:" + juce::String (instrumentIndex);
        auto* audioPlugin = resolvePluginInstance (pluginId);
        if (audioPlugin == nullptr)
            continue;

        ensurePluginModulationRuntimeSize (instrumentIndex);
        auto& runtime = pluginModulationRuntime[instrumentIndex];

        double deltaSeconds = 1.0 / 30.0;
        if (runtime.lastUpdateMs != 0)
        {
            const auto elapsedMs = nowMs - runtime.lastUpdateMs;
            deltaSeconds = juce::jlimit (1.0 / 240.0, 0.25, static_cast<double> (elapsedMs) / 1000.0);
        }

        if (runtime.lastTransportBeat >= 0.0 && transportBeat < runtime.lastTransportBeat - 0.125)
        {
            for (auto& sourceState : runtime.sources)
                sourceState.randomNeedsNew = true;
        }

        runtime.lastUpdateMs = nowMs;
        runtime.lastTransportBeat = transportBeat;

        std::vector<float> sourceValues (info.pluginModulation.sources.size(), 0.0f);
        for (int i = 0; i < static_cast<int> (info.pluginModulation.sources.size()); ++i)
        {
            const auto& source = info.pluginModulation.sources[static_cast<size_t> (i)];
            auto& state = runtime.sources[static_cast<size_t> (i)];

            if (! source.enabled)
                continue;

            if (source.type == PluginModulatorSource::Type::LFO)
            {
                double lfoHz = source.lfoRateHz;
                if (source.lfoRateMode == PluginModulatorSource::LfoRateMode::Steps)
                    lfoHz = rowsPerSecond / juce::jmax (1.0, source.lfoRateSteps);

                const double previousPhase = state.lfoPhase;
                state.lfoPhase += juce::jmax (0.0, lfoHz) * deltaSeconds;
                if (std::floor (state.lfoPhase) > std::floor (previousPhase))
                    state.randomNeedsNew = true;
                state.lfoPhase -= std::floor (state.lfoPhase);

                sourceValues[static_cast<size_t> (i)] = evaluatePluginLfo (state.lfoPhase, source.lfoShape, state);
            }
            else
            {
                sourceValues[static_cast<size_t> (i)] = advancePluginEnvelope (state, source, deltaSeconds);
            }
        }

        std::map<int, float> modulationByParam;
        for (const auto& route : info.pluginModulation.routes)
        {
            if (! route.enabled
                || route.parameterIndex < 0
                || route.sourceIndex < 0
                || route.sourceIndex >= static_cast<int> (sourceValues.size())
                || std::abs (route.amount) <= kPluginModulationEpsilon)
            {
                continue;
            }

            if (excludedParamIndex >= 0
                && route.parameterIndex == excludedParamIndex
                && pluginId == excludedPluginId)
            {
                continue;
            }

            modulationByParam[route.parameterIndex] += sourceValues[static_cast<size_t> (route.sourceIndex)] * route.amount;
        }

        auto& params = audioPlugin->getParameters();
        auto& lock = audioPlugin->getCallbackLock();
        if (! lock.tryEnter())
            continue;

        for (auto it = runtime.params.begin(); it != runtime.params.end();)
        {
            if (modulationByParam.find (it->first) != modulationByParam.end() || ! it->second.valid)
            {
                ++it;
                continue;
            }

            const int paramIndex = it->first;
            if (paramIndex >= 0 && paramIndex < params.size())
            {
                if (auto* param = params[paramIndex])
                {
                    const float base = juce::jlimit (0.0f, 1.0f, it->second.baseValue);
                    if (std::abs (param->getValue() - base) > kPluginModulationEpsilon)
                    {
                        param->setValue (base);
                        rememberAutomatedParamWrite (pluginId, paramIndex, param->getValue());
                    }
                }
            }

            it = runtime.params.erase (it);
        }

        for (const auto& [paramIndex, modulationValue] : modulationByParam)
        {
            if (paramIndex < 0 || paramIndex >= params.size())
                continue;

            auto* param = params[paramIndex];
            if (param == nullptr)
                continue;

            auto& paramState = runtime.params[paramIndex];
            const float current = param->getValue();
            float base = current;

            if (paramState.valid
                && std::abs (current - paramState.lastWrittenValue) <= 0.002f)
            {
                base = paramState.baseValue;
            }

            const float clampedBase = juce::jlimit (0.0f, 1.0f, base);
            const float nextValue = juce::jlimit (0.0f, 1.0f, clampedBase + modulationValue);

            if (! paramState.valid || std::abs (current - nextValue) > kPluginModulationEpsilon)
                param->setValue (nextValue);

            paramState.valid = true;
            paramState.baseValue = clampedBase;
            paramState.lastModulation = modulationValue;
            paramState.lastWrittenValue = param->getValue();
            rememberAutomatedParamWrite (pluginId, paramIndex, paramState.lastWrittenValue);
        }

        lock.exit();
    }
}

//==============================================================================
// Plugin automation (Phase 5)
//==============================================================================

namespace
{
    constexpr float kAutomationParameterEpsilon = 1.0e-5f;
    constexpr float kAutoLearnAutomationSuppressTolerance = 1.0e-3f;
}

juce::AudioPluginInstance* TrackerEngine::resolvePluginInstance (const juce::String& pluginId)
{
    if (pluginId.startsWith ("inst:"))
    {
        int instIdx = pluginId.substring (5).getIntValue();
        auto* plugin = getPluginInstrumentInstance (instIdx);
        if (plugin != nullptr)
        {
            if (auto* ext = dynamic_cast<te::ExternalPlugin*> (plugin))
                return ext->getAudioPluginInstance();
        }
    }
    else if (pluginId.startsWith ("insert:"))
    {
        // Format: "insert:trackIndex:slotIndex"
        auto parts = juce::StringArray::fromTokens (pluginId.substring (7), ":", "");
        if (parts.size() >= 2)
        {
            int trackIdx = parts[0].getIntValue();
            int slotIdx = parts[1].getIntValue();
            auto* plugin = getInsertPlugin (trackIdx, slotIdx);
            if (plugin != nullptr)
            {
                if (auto* ext = dynamic_cast<te::ExternalPlugin*> (plugin))
                    return ext->getAudioPluginInstance();
            }
        }
    }

    return nullptr;
}

TrackerEngine::AutomatedParam* TrackerEngine::findAutomatedParam (const juce::String& pluginId, int paramIndex)
{
    for (auto& ap : lastAutomatedParams)
    {
        if (ap.pluginId == pluginId && ap.paramIndex == paramIndex)
            return &ap;
    }

    return nullptr;
}

const TrackerEngine::AutomatedParam* TrackerEngine::findAutomatedParam (const juce::String& pluginId,
                                                                         int paramIndex) const
{
    for (const auto& ap : lastAutomatedParams)
    {
        if (ap.pluginId == pluginId && ap.paramIndex == paramIndex)
            return &ap;
    }

    return nullptr;
}

void TrackerEngine::forgetAutomatedParamsForPlugin (const juce::String& pluginId)
{
    lastAutomatedParams.erase (std::remove_if (lastAutomatedParams.begin(), lastAutomatedParams.end(),
                                               [&pluginId] (const AutomatedParam& ap)
                                               {
                                                   return ap.pluginId == pluginId;
                                               }),
                               lastAutomatedParams.end());

    recentAutomatedParamWrites.erase (std::remove_if (recentAutomatedParamWrites.begin(), recentAutomatedParamWrites.end(),
                                                      [&pluginId] (const AutomatedParamWrite& write)
                                                      {
                                                          return write.pluginId == pluginId;
                                                      }),
                                      recentAutomatedParamWrites.end());
}

void TrackerEngine::forgetAutomatedParamsForInsertTrack (int trackIndex)
{
    const auto pluginIdPrefix = "insert:" + juce::String (trackIndex) + ":";

    lastAutomatedParams.erase (std::remove_if (lastAutomatedParams.begin(), lastAutomatedParams.end(),
                                               [&pluginIdPrefix] (const AutomatedParam& ap)
                                               {
                                                   return ap.pluginId.startsWith (pluginIdPrefix);
                                               }),
                               lastAutomatedParams.end());

    recentAutomatedParamWrites.erase (std::remove_if (recentAutomatedParamWrites.begin(), recentAutomatedParamWrites.end(),
                                                      [&pluginIdPrefix] (const AutomatedParamWrite& write)
                                                      {
                                                          return write.pluginId.startsWith (pluginIdPrefix);
                                                      }),
                                      recentAutomatedParamWrites.end());
}

const TrackerEngine::AutomatedParamWrite* TrackerEngine::findRecentAutomatedParamWrite (const juce::String& pluginId,
                                                                                        int paramIndex) const
{
    for (const auto& write : recentAutomatedParamWrites)
    {
        if (write.pluginId == pluginId && write.paramIndex == paramIndex)
            return &write;
    }

    return nullptr;
}

void TrackerEngine::rememberAutomatedParamWrite (const juce::String& pluginId, int paramIndex, float value)
{
    for (auto& write : recentAutomatedParamWrites)
    {
        if (write.pluginId == pluginId && write.paramIndex == paramIndex)
        {
            write.value = value;
            return;
        }
    }

    recentAutomatedParamWrites.push_back ({ pluginId, paramIndex, value });
}

bool TrackerEngine::shouldSuppressPluginAutoLearnChange (const juce::String& pluginId,
                                                         int paramIndex,
                                                         float currentValue) const
{
    auto* recentWrite = findRecentAutomatedParamWrite (pluginId, paramIndex);
    if (recentWrite == nullptr)
        return false;

    return std::abs (currentValue - recentWrite->value) <= kAutoLearnAutomationSuppressTolerance;
}

void TrackerEngine::applyPatternAutomation (const PatternAutomationData& automationData,
                                            int /*patternLength*/, int /*rpb*/)
{
    if (edit == nullptr)
        return;

    // Clear previous tracking without touching plugin parameters synchronously.
    // resetAutomationParameters() used to call param->setValue() for every
    // tracked param, which deadlocks when the audio thread is processing the
    // plugin (playInStopEnabled = true means the graph is always live).
    lastAutomatedParams.clear();
    recentAutomatedParamWrites.clear();

    if (automationData.isEmpty())
        return;

    for (const auto& lane : automationData.lanes)
    {
        if (lane.isEmpty())
            continue;

        auto* audioPlugin = resolvePluginInstance (lane.pluginId);
        if (audioPlugin == nullptr)
            continue;

        auto& params = audioPlugin->getParameters();
        if (lane.parameterId < 0 || lane.parameterId >= params.size())
            continue;

        auto* param = params[lane.parameterId];
        if (param == nullptr)
            continue;

        // Store baseline for later row-wise playback updates.
        // tryEnter: audio thread may hold the callback lock (playInStopEnabled).
        float baseline = 0.5f;
        auto& lock = audioPlugin->getCallbackLock();
        if (lock.tryEnter())
        {
            baseline = param->getValue();
            lock.exit();
        }
        lastAutomatedParams.push_back ({ lane.pluginId, lane.parameterId, baseline });
    }

    // Prime row-0 value immediately so playback starts from correct automation state.
    applyAutomationForPlaybackRow (automationData, 0);
}

void TrackerEngine::applyAutomationForPlaybackRow (const PatternAutomationData& automationData,
                                                   int row,
                                                   const juce::String& excludedPluginId,
                                                   int excludedParamIndex)
{
    if (automationData.isEmpty())
    {
        recentAutomatedParamWrites.clear();
        return;
    }

    const float rowPosition = static_cast<float> (juce::jmax (0, row));

    for (const auto& lane : automationData.lanes)
    {
        if (lane.isEmpty())
            continue;
        if (excludedParamIndex >= 0 && lane.parameterId == excludedParamIndex && lane.pluginId == excludedPluginId)
            continue;

        auto* audioPlugin = resolvePluginInstance (lane.pluginId);
        if (audioPlugin == nullptr)
            continue;

        auto& params = audioPlugin->getParameters();
        if (lane.parameterId < 0 || lane.parameterId >= params.size())
            continue;

        auto* param = params[lane.parameterId];
        if (param == nullptr)
            continue;

        auto* tracked = findAutomatedParam (lane.pluginId, lane.parameterId);
        if (tracked == nullptr)
        {
            lastAutomatedParams.push_back ({ lane.pluginId, lane.parameterId, param->getValue() });
            tracked = &lastAutomatedParams.back();
        }

        const float value = lane.getValueAtRow (rowPosition, tracked->baselineValue);

        // Use tryEnter on the plugin's callback lock to avoid deadlocking
        // with the audio thread.  playInStopEnabled = true means the
        // playback graph is always live, so processBlock() can hold the
        // lock at any time.  If we can't get the lock we skip this tick;
        // the next timer callback (30 Hz) will try again.
        auto& lock = audioPlugin->getCallbackLock();
        if (lock.tryEnter())
        {
            auto appliedValue = param->getValue();
            if (std::abs (appliedValue - value) > kAutomationParameterEpsilon)
            {
                param->setValue (value);
                appliedValue = param->getValue();
            }
            rememberAutomatedParamWrite (lane.pluginId, lane.parameterId, appliedValue);
            lock.exit();
        }
    }
}

void TrackerEngine::resetAutomationParameters()
{
    for (auto& ap : lastAutomatedParams)
    {
        auto* audioPlugin = resolvePluginInstance (ap.pluginId);
        if (audioPlugin == nullptr)
            continue;

        auto& params = audioPlugin->getParameters();
        if (ap.paramIndex < 0 || ap.paramIndex >= params.size())
            continue;

        auto* param = params[ap.paramIndex];
        if (param == nullptr)
            continue;

        // Try-lock to avoid deadlocking with the audio thread.
        auto& lock = audioPlugin->getCallbackLock();
        if (lock.tryEnter())
        {
            auto appliedValue = param->getValue();
            if (std::abs (appliedValue - ap.baselineValue) > kAutomationParameterEpsilon)
            {
                param->setValue (ap.baselineValue);
                appliedValue = param->getValue();
            }
            rememberAutomatedParamWrite (ap.pluginId, ap.paramIndex, appliedValue);
            lock.exit();
        }
    }

    lastAutomatedParams.clear();
}
