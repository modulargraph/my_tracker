#include <algorithm>
#include "TrackerEngine.h"
#include "InstrumentEffectsPlugin.h"
#include "TrackerSamplerPlugin.h"
#include "MetronomePlugin.h"
#include "SendEffectsPlugin.h"
#include "MixerPlugin.h"
#include "ChannelStripPlugin.h"
#include "TrackOutputPlugin.h"
#include "InstrumentRouting.h"
#include "FxParamTransport.h"

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

char getSlotCommandLetter (const FxSlot& slot)
{
    return slot.getCommandLetter();
}

int getRowTempoCommand (const Pattern& pattern, int row)
{
    if (row < 0 || row >= pattern.numRows)
        return -1;

    int bpm = -1;
    int laneCount = row < static_cast<int> (pattern.masterFxRows.size())
                        ? static_cast<int> (pattern.masterFxRows[static_cast<size_t> (row)].size())
                        : 0;

    for (int lane = 0; lane < laneCount; ++lane)
    {
        const auto& slot = pattern.getMasterFxSlot (row, lane);
        if (getSlotCommandLetter (slot) == 'F')
            bpm = juce::jlimit (20, 300, slot.fxParam);
    }

    return bpm;
}

void appendSymbolicTrackFx (juce::MidiMessageSequence& midiSeq, const FxSlot& slot, double ccTime)
{
    switch (getSlotCommandLetter (slot))
    {
        case 'B':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcSamplerDirection, slot.fxParam, ccTime);
            break;
        case 'P':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcSamplerPosition, slot.fxParam, ccTime);
            break;
        case 'T':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxTune, slot.fxParam, ccTime);
            break;
        case 'G':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxPortaSteps, slot.fxParam, ccTime);
            break;
        case 'Y':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxDelaySend, slot.fxParam, ccTime);
            break;
        case 'R':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxReverbSend, slot.fxParam, ccTime);
            break;
        case 'S':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxSlideUp, slot.fxParam, ccTime);
            break;
        case 'D':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxSlideDown, slot.fxParam, ccTime);
            break;
        case 'V':
            FxParamTransport::appendByteAsControllers (midiSeq, 1, kCcFxVolume, slot.fxParam, ccTime);
            break;
        case 'F':
            // Tempo is handled via master lane tempo points.
            break;
        default:
            break;
    }
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
} // namespace

TrackerEngine::TrackerEngine()
{
    currentTrackInstrument.fill (-1);
}

TrackerEngine::~TrackerEngine()
{
    stopTimer();

    if (edit != nullptr)
    {
        auto& transport = edit->getTransport();
        transport.removeChangeListener (this);

        if (transport.isPlaying())
            transport.stop (false, false);
    }

    sendEffectsPlugin = nullptr;
    edit = nullptr;
    pluginCatalog = nullptr;
    engine = nullptr;
}

void TrackerEngine::initialise()
{
    engine = std::make_unique<te::Engine> ("TrackerAdjust");

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
                        .getChildFile ("TrackerAdjust")
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
    const double baseBpm = tempoSequence.getTempos()[0]->getBpm();

    while (tempoSequence.getNumTempos() > 1)
        tempoSequence.removeTempo (tempoSequence.getNumTempos() - 1, false);

    tempoSequence.getTempos()[0]->setBpm (baseBpm);

    std::map<double, int> tempoPoints;
    for (int row = 0; row < pattern.numRows; ++row)
    {
        int bpm = getRowTempoCommand (pattern, row);
        if (bpm <= 0)
            continue;

        double beat = static_cast<double> (row) / static_cast<double> (rowsPerBeat);
        tempoPoints[beat] = bpm;
    }

    for (const auto& [beat, bpm] : tempoPoints)
    {
        if (beat <= 0.0)
            tempoSequence.getTempos()[0]->setBpm (bpm);
        else
            tempoSequence.insertTempo (te::BeatPosition::fromBeats (beat), bpm, 0.0f);
    }
}

void TrackerEngine::rebuildTempoSequenceFromArrangementMasterLane (const std::vector<std::pair<const Pattern*, int>>& sequence, int rpb)
{
    if (edit == nullptr)
        return;

    auto& tempoSequence = edit->tempoSequence;
    const double baseBpm = tempoSequence.getTempos()[0]->getBpm();

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
                int bpm = getRowTempoCommand (*pattern, row);
                if (bpm <= 0)
                    continue;

                double beat = beatOffset + static_cast<double> (row) / static_cast<double> (rpb);
                tempoPoints[beat] = bpm;
            }

            beatOffset += patternLengthBeats;
        }
    }

    for (const auto& [beat, bpm] : tempoPoints)
    {
        if (beat <= 0.0)
            tempoSequence.getTempos()[0]->setBpm (bpm);
        else
            tempoSequence.insertTempo (te::BeatPosition::fromBeats (beat), bpm, 0.0f);
    }
}

void TrackerEngine::syncPatternToEdit (const Pattern& pattern,
                                       const std::array<bool, kNumTracks>& releaseMode)
{
    if (edit == nullptr)
        return;

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

        // For plugin instrument tracks, always use release mode (no allSoundOff).
        // allSoundOff kills ALL voices on the channel, preventing subsequent notes
        // from sounding.  Kill/release mode only applies to sample instruments.
        bool isKill = ! releaseMode[static_cast<size_t> (trackIdx)];
        if (getTrackContentMode (trackIdx) == TrackContentMode::PluginInstrument)
            isKill = false;

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
            double startBeat = static_cast<double> (row) / static_cast<double> (rowsPerBeat);
            auto rowTime = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (startBeat));

            // Check if any lane has a note for FX reset
            bool anyLaneHasNote = false;
            for (int nl = 0; nl < numNoteLanes; ++nl)
            {
                auto slot = cell.getNoteLane (nl);
                if (slot.note >= 0)
                    anyLaneHasNote = true;
            }

            if (anyLaneHasNote)
            {
                const double resetTime = juce::jmax (0.0, rowTime.inSeconds() - 0.00008);
                midiSeq.addEvent (juce::MidiMessage::controllerEvent (1, kCcFxNoteReset, 0), resetTime);
            }

            // Process FX slots (shared)
            for (int fxSlotIdx = 0; fxSlotIdx < cell.getNumFxSlots(); ++fxSlotIdx)
            {
                const auto& fxSlot = cell.getFxSlot (fxSlotIdx);
                if (fxSlot.isEmpty())
                    continue;

                const auto letter = getSlotCommandLetter (fxSlot);
                if (letter == '\0')
                    continue;

                if (letter == 'G' && fxSlot.fxParam > 0)
                {
                    // Apply portamento to all lanes
                    for (auto& ps : laneActivePortaSteps)
                        ps = fxSlot.fxParam;
                }

                const double ccTime = juce::jmax (0.0, rowTime.inSeconds() - 0.00005);
                appendSymbolicTrackFx (midiSeq, fxSlot, ccTime);
            }
        }

        // Per-lane note generation
        for (int laneIdx = 0; laneIdx < numNoteLanes; ++laneIdx)
        {
            int lastPlayingNote = -1;
            int currentInst = -1;
            int activePortaSteps = 0;

            for (int row = 0; row < pattern.numRows; ++row)
            {
                const auto& cell = pattern.getCell (row, trackIdx);
                auto noteSlot = cell.getNoteLane (laneIdx);

                double startBeat = static_cast<double> (row) / static_cast<double> (rowsPerBeat);
                auto rowTime = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (startBeat));

                // Check FX for portamento (shared FX affects all lanes)
                for (int fxSlotIdx = 0; fxSlotIdx < cell.getNumFxSlots(); ++fxSlotIdx)
                {
                    const auto& fxSlot = cell.getFxSlot (fxSlotIdx);
                    if (fxSlot.isEmpty()) continue;
                    const auto letter = getSlotCommandLetter (fxSlot);
                    if (letter == 'G' && fxSlot.fxParam > 0)
                        activePortaSteps = fxSlot.fxParam;
                }

                const bool rowHasPorta = (activePortaSteps > 0);

                if (noteSlot.note < 0)
                    continue;

                // OFF (255)
                if (noteSlot.note == 255)
                {
                    if (lastPlayingNote >= 0)
                        midiSeq.addEvent (juce::MidiMessage::noteOff (1, lastPlayingNote), rowTime.inSeconds());
                    else
                        midiSeq.addEvent (juce::MidiMessage::allNotesOff (1), rowTime.inSeconds());
                    lastPlayingNote = -1;
                    activePortaSteps = 0;
                    continue;
                }

                // KILL (254)
                if (noteSlot.note == 254)
                {
                    midiSeq.addEvent (juce::MidiMessage::allSoundOff (1), rowTime.inSeconds());
                    lastPlayingNote = -1;
                    activePortaSteps = 0;
                    continue;
                }

                // Portamento
                if (rowHasPorta && lastPlayingNote >= 0)
                {
                    midiSeq.addEvent (juce::MidiMessage::controllerEvent (1, 28, noteSlot.note & 0x7F),
                                      rowTime.inSeconds());
                    if (noteSlot.volume >= 0)
                        midiSeq.addEvent (juce::MidiMessage::controllerEvent (1, 7, noteSlot.volume),
                                          juce::jmax (0.0, rowTime.inSeconds() - 0.00003));
                    activePortaSteps = 0;
                    continue;
                }

                // Program change
                if (noteSlot.instrument >= 0 && noteSlot.instrument != currentInst)
                {
                    currentInst = InstrumentRouting::clampInstrumentIndex (noteSlot.instrument);
                    const double bankTime = juce::jmax (0.0, rowTime.inSeconds() - 0.00012);
                    const double progTime = juce::jmax (0.0, rowTime.inSeconds() - 0.0001);
                    midiSeq.addEvent (juce::MidiMessage::controllerEvent (1, 0,
                                      InstrumentRouting::getBankMsbForInstrument (currentInst)), bankTime);
                    midiSeq.addEvent (juce::MidiMessage::programChange (1,
                                      InstrumentRouting::getProgramForInstrument (currentInst)), progTime);
                }

                // Calculate note end: sustain until next note in this lane
                int endRow = pattern.numRows;
                for (int nextRow = row + 1; nextRow < pattern.numRows; ++nextRow)
                {
                    auto nextSlot = pattern.getCell (nextRow, trackIdx).getNoteLane (laneIdx);
                    if (nextSlot.note >= 0)
                    {
                        bool nextIsPorta = false;
                        if (nextSlot.note < 254)
                        {
                            for (int fxi = 0; fxi < pattern.getCell (nextRow, trackIdx).getNumFxSlots(); ++fxi)
                            {
                                const auto& ns = pattern.getCell (nextRow, trackIdx).getFxSlot (fxi);
                                if (getSlotCommandLetter (ns) == 'G' && ns.fxParam > 0)
                                    nextIsPorta = true;
                            }
                        }
                        if (nextIsPorta)
                            continue;
                        endRow = nextRow;
                        break;
                    }
                }
                double endBeat = static_cast<double> (endRow) / static_cast<double> (rowsPerBeat);
                auto noteEnd = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (endBeat));

                int velocity = noteSlot.volume >= 0 ? noteSlot.volume : 127;

                midiSeq.addEvent (juce::MidiMessage::noteOn (1, noteSlot.note, static_cast<juce::uint8> (velocity)),
                                  rowTime.inSeconds());

                if (isKill)
                    midiSeq.addEvent (juce::MidiMessage::allSoundOff (1), noteEnd.inSeconds());

                midiSeq.addEvent (juce::MidiMessage::noteOff (1, noteSlot.note),
                                  noteEnd.inSeconds());

                lastPlayingNote = noteSlot.note;
                activePortaSteps = 0;
            }
        }

        midiSeq.updateMatchedPairs();
        midiClip->mergeInMidiSequence (midiSeq, te::MidiList::NoteAutomationType::none);
    }

    // Apply plugin automation from pattern data (Phase 5)
    applyPatternAutomation (pattern.automationData, pattern.numRows, rowsPerBeat);

    refreshTransportLoopRangeFromClip();
}

void TrackerEngine::syncArrangementToEdit (const std::vector<std::pair<const Pattern*, int>>& sequence, int rpb,
                                            const std::array<bool, kNumTracks>& releaseMode)
{
    if (edit == nullptr || sequence.empty())
        return;

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
        bool isKill = ! releaseMode[static_cast<size_t> (trackIdx)];
        if (getTrackContentMode (trackIdx) == TrackContentMode::PluginInstrument)
            isKill = false;

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
                        double startBeat = beatOffset + static_cast<double> (row) / static_cast<double> (rpb);
                        auto rowTime = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (startBeat));

                        // Check if any lane has a note for FX reset
                        bool anyLaneHasNote = false;
                        for (int nl = 0; nl < numNoteLanes; ++nl)
                        {
                            auto slot = cell.getNoteLane (nl);
                            if (slot.note >= 0)
                                anyLaneHasNote = true;
                        }

                        if (anyLaneHasNote)
                        {
                            const double resetTime = juce::jmax (0.0, rowTime.inSeconds() - 0.00008);
                            midiSeq.addEvent (juce::MidiMessage::controllerEvent (1, kCcFxNoteReset, 0), resetTime);
                        }

                        // Process FX slots (shared across all note lanes)
                        for (int fxSlotIdx = 0; fxSlotIdx < cell.getNumFxSlots(); ++fxSlotIdx)
                        {
                            const auto& slot = cell.getFxSlot (fxSlotIdx);
                            if (slot.isEmpty())
                                continue;

                            const auto letter = getSlotCommandLetter (slot);
                            if (letter == '\0')
                                continue;

                            const double ccTime = juce::jmax (0.0, rowTime.inSeconds() - 0.00005);
                            appendSymbolicTrackFx (midiSeq, slot, ccTime);
                        }
                    }

                    beatOffset += patternLengthBeats;
                }
            }
        }

        // Per-lane note generation (mirrors syncPatternToEdit approach)
        for (int laneIdx = 0; laneIdx < numNoteLanes; ++laneIdx)
        {
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
                        auto noteSlot = cell.getNoteLane (laneIdx);

                        double startBeat = beatOffset + static_cast<double> (row) / static_cast<double> (rpb);
                        auto rowTime = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (startBeat));

                        // Check FX for portamento (shared FX affects all lanes)
                        for (int fxSlotIdx = 0; fxSlotIdx < cell.getNumFxSlots(); ++fxSlotIdx)
                        {
                            const auto& fxSlot = cell.getFxSlot (fxSlotIdx);
                            if (fxSlot.isEmpty()) continue;
                            const auto letter = getSlotCommandLetter (fxSlot);
                            if (letter == 'G' && fxSlot.fxParam > 0)
                                activePortaSteps = fxSlot.fxParam;
                        }

                        const bool rowHasPorta = (activePortaSteps > 0);

                        if (noteSlot.note < 0)
                            continue;

                        // OFF (255)
                        if (noteSlot.note == 255)
                        {
                            if (lastPlayingNote >= 0)
                                midiSeq.addEvent (juce::MidiMessage::noteOff (1, lastPlayingNote), rowTime.inSeconds());
                            else
                                midiSeq.addEvent (juce::MidiMessage::allNotesOff (1), rowTime.inSeconds());
                            lastPlayingNote = -1;
                            activePortaSteps = 0;
                            continue;
                        }

                        // KILL (254)
                        if (noteSlot.note == 254)
                        {
                            midiSeq.addEvent (juce::MidiMessage::allSoundOff (1), rowTime.inSeconds());
                            lastPlayingNote = -1;
                            activePortaSteps = 0;
                            continue;
                        }

                        // Portamento
                        if (rowHasPorta && lastPlayingNote >= 0)
                        {
                            midiSeq.addEvent (juce::MidiMessage::controllerEvent (1, 28, noteSlot.note & 0x7F),
                                              rowTime.inSeconds());
                            if (noteSlot.volume >= 0)
                                midiSeq.addEvent (juce::MidiMessage::controllerEvent (1, 7, noteSlot.volume),
                                                  juce::jmax (0.0, rowTime.inSeconds() - 0.00003));
                            activePortaSteps = 0;
                            continue;
                        }

                        // Program change
                        if (noteSlot.instrument >= 0 && noteSlot.instrument != currentInst)
                        {
                            currentInst = InstrumentRouting::clampInstrumentIndex (noteSlot.instrument);
                            const double bankTime = juce::jmax (0.0, rowTime.inSeconds() - 0.00012);
                            const double progTime = juce::jmax (0.0, rowTime.inSeconds() - 0.0001);
                            midiSeq.addEvent (juce::MidiMessage::controllerEvent (1, 0,
                                              InstrumentRouting::getBankMsbForInstrument (currentInst)), bankTime);
                            midiSeq.addEvent (juce::MidiMessage::programChange (1,
                                              InstrumentRouting::getProgramForInstrument (currentInst)), progTime);
                        }

                        // Calculate note end: sustain until next note in this lane or end of repeat
                        double repeatEndBeat = beatOffset + patternLengthBeats;
                        double endBeat = repeatEndBeat;
                        for (int nextRow = row + 1; nextRow < pattern->numRows; ++nextRow)
                        {
                            auto nextSlot = pattern->getCell (nextRow, trackIdx).getNoteLane (laneIdx);
                            if (nextSlot.note >= 0)
                            {
                                bool nextIsPorta = false;
                                if (nextSlot.note < 254)
                                {
                                    for (int fxi = 0; fxi < pattern->getCell (nextRow, trackIdx).getNumFxSlots(); ++fxi)
                                    {
                                        const auto& ns = pattern->getCell (nextRow, trackIdx).getFxSlot (fxi);
                                        if (getSlotCommandLetter (ns) == 'G' && ns.fxParam > 0)
                                            nextIsPorta = true;
                                    }
                                }
                                if (nextIsPorta)
                                    continue;
                                endBeat = beatOffset + static_cast<double> (nextRow) / static_cast<double> (rpb);
                                break;
                            }
                        }

                        auto noteEnd = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (endBeat));

                        int velocity = noteSlot.volume >= 0 ? noteSlot.volume : 127;

                        midiSeq.addEvent (juce::MidiMessage::noteOn (1, noteSlot.note, static_cast<juce::uint8> (velocity)),
                                          rowTime.inSeconds());

                        if (isKill)
                            midiSeq.addEvent (juce::MidiMessage::allSoundOff (1), noteEnd.inSeconds());

                        midiSeq.addEvent (juce::MidiMessage::noteOff (1, noteSlot.note),
                                          noteEnd.inSeconds());

                        lastPlayingNote = noteSlot.note;
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
        applyPatternAutomation (sequence.front().first->automationData, sequence.front().first->numRows, rpb);

    refreshTransportLoopRangeFromClip();
}

void TrackerEngine::play()
{
    if (edit == nullptr)
        return;

    auto& transport = edit->getTransport();
    refreshTransportLoopRangeFromClip();

    transport.setPosition (te::TimePosition::fromSeconds (0.0));
    transport.play (false);
}

void TrackerEngine::stop()
{
    if (edit == nullptr)
        return;

    edit->getTransport().stop (false, false);

    // Reset automated parameters back to their baseline values
    resetAutomationParameters();
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
    auto startTime = te::TimePosition::fromSeconds (0.0);

    te::TimeRange newRange { startTime, newEndTime };
    transport.setLoopRange (newRange);

    // If the playhead is past the new end, wrap to the beginning
    auto currentPos = transport.getPosition();
    if (currentPos >= newEndTime)
        transport.setPosition (startTime);
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
    transport.setLoopRange (clipRange);
    transport.looping = true;

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
        if (auto* fxPlugin = tracks[t]->pluginList.findFirstPluginOfType<InstrumentEffectsPlugin>())
            fxPlugin->setRowsPerBeat (rowsPerBeat);
}

void TrackerEngine::setBpm (double bpm)
{
    if (edit == nullptr)
        return;

    edit->tempoSequence.getTempos()[0]->setBpm (juce::jlimit (20.0, 999.0, bpm));
}

double TrackerEngine::getBpm() const
{
    if (edit == nullptr)
        return 120.0;

    return edit->tempoSequence.getTempos()[0]->getBpm();
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
    juce::ignoreUnused (trackIndex);

    if (instrumentIndex < 0)
        return;

    // Preview routing is always through the dedicated preview track.
    auto* track = getTrack (kPreviewTrack);
    if (track == nullptr)
        return;

    stopPreview();
    ensureTrackHasInstrument (kPreviewTrack, instrumentIndex);

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

    sampler.playNote (*track, midiNote, 1.0f);

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

    auto bank = sampler.getSampleBank (instrumentIndex);
    if (bank == nullptr)
        return;

    previewNote (kPreviewTrack, instrumentIndex, 60, true);
}

void TrackerEngine::stopPreview()
{
    stopTimer();

    if (activePreviewTrack >= 0)
    {
        auto* track = getTrack (activePreviewTrack);
        if (track != nullptr)
            sampler.stopNote (*track);

        activePreviewTrack = -1;
    }

    previewBank = nullptr;
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

    if (activePreviewTrack >= 0)
    {
        auto* track = getTrack (activePreviewTrack);
        if (track != nullptr)
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
    auto* track = getTrack (kSendEffectsTrack);
    if (track == nullptr) return;

    // Prepare send buffers (default block size, stereo)
    sampler.getSendBuffers().prepare (8192, 2);

    // Create and insert the SendEffectsPlugin on the bus track
    auto* existing = track->pluginList.findFirstPluginOfType<SendEffectsPlugin>();
    if (existing == nullptr)
    {
        if (auto plugin = dynamic_cast<SendEffectsPlugin*> (
                track->edit.getPluginCache().createNewPlugin (SendEffectsPlugin::xmlTypeName, {}).get()))
        {
            track->pluginList.insertPlugin (*plugin, 0, nullptr);
            existing = plugin;
        }
    }

    if (existing != nullptr)
    {
        existing->setSendBuffers (&sampler.getSendBuffers());
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
    setupMixerPlugins();
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
    }

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
    setupMixerPlugins();

    for (int t = 0; t < kNumTracks; ++t)
        rebuildInsertChain (t);
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

    // Create the plugin instance via Tracktion's plugin cache
    auto& formatManager = engine->getPluginManager().pluginFormatManager;
    juce::String errorMessage;

    auto instance = formatManager.createPluginInstance (desc, 44100.0, 512, errorMessage);
    if (instance == nullptr)
    {
        DBG ("Failed to create insert plugin: " + errorMessage);
        return false;
    }

    // Create a Tracktion ExternalPlugin wrapper
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

    // Close any editor window
    closePluginEditor (trackIndex, slotIndex);

    // Find and remove the plugin from the track's plugin list
    auto* track = getTrack (trackIndex);
    if (track != nullptr)
    {
        if (auto* plugin = findInsertPluginForSlot (*track, slotIndex))
            plugin->removeFromParent();
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
        p->removeFromParent();

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
                slot.pluginState = ext->state.createCopy();
            }
            else
            {
                slot.pluginState = {};
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

    // Check if window already exists
    if (pluginEditorWindows.count (key) > 0 && pluginEditorWindows[key] != nullptr)
    {
        pluginEditorWindows[key]->toFront (true);
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
        PluginEditorWindow (const juce::String& name,
                            std::map<juce::String, std::unique_ptr<juce::DocumentWindow>>& windowMap,
                            const juce::String& mapKey)
            : juce::DocumentWindow (name, juce::Colours::darkgrey,
                                    juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
              windows (windowMap), key (mapKey)
        {
        }

        void closeButtonPressed() override
        {
            windows.erase (key);  // destroys this window
        }

    private:
        std::map<juce::String, std::unique_ptr<juce::DocumentWindow>>& windows;
        juce::String key;
    };

    auto window = std::make_unique<PluginEditorWindow> (
        externalPlugin->getName(), pluginEditorWindows, key);

    window->setContentOwned (editor, true);
    window->setResizable (true, false);
    window->centreWithSize (editor->getWidth(), editor->getHeight());
    window->setVisible (true);
    window->setAlwaysOnTop (true);

    pluginEditorWindows[key] = std::move (window);
}

void TrackerEngine::closePluginEditor (int trackIndex, int slotIndex)
{
    juce::String key = juce::String (trackIndex) + ":" + juce::String (slotIndex);
    pluginEditorWindows.erase (key);
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

bool TrackerEngine::setPluginInstrument (int instrumentIndex, const juce::PluginDescription& desc, int ownerTrack)
{
    if (instrumentIndex < 0 || instrumentIndex >= 256)
        return false;

    if (ownerTrack < 0 || ownerTrack >= kNumTracks)
        return false;

    auto& info = instrumentSlotInfos[instrumentIndex];
    info.setPlugin (desc, ownerTrack);

    // Load the plugin on the owner track
    ensurePluginInstrumentLoaded (instrumentIndex);

    return true;
}

void TrackerEngine::clearPluginInstrument (int instrumentIndex)
{
    // Close any editor window
    closePluginInstrumentEditor (instrumentIndex);

    // Remove plugin from track
    removePluginInstrumentFromTrack (instrumentIndex);

    // Remove from slot infos
    instrumentSlotInfos.erase (instrumentIndex);

    // Remove from loaded instances
    pluginInstrumentInstances.erase (instrumentIndex);
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
    // Unload all existing plugin instrument instances/editor windows to avoid stale
    // plugins surviving project switches.
    std::vector<int> loadedInstrumentIndices;
    loadedInstrumentIndices.reserve (pluginInstrumentInstances.size());
    for (const auto& [instrumentIndex, plugin] : pluginInstrumentInstances)
    {
        juce::ignoreUnused (plugin);
        loadedInstrumentIndices.push_back (instrumentIndex);
    }

    for (int instrumentIndex : loadedInstrumentIndices)
        clearPluginInstrument (instrumentIndex);

    instrumentSlotInfos = infos;
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

    // Check if already loaded
    auto instanceIt = pluginInstrumentInstances.find (instrumentIndex);
    if (instanceIt != pluginInstrumentInstances.end() && instanceIt->second != nullptr)
        return;

    // Create the external plugin instance
    auto& desc = it->second.pluginDescription;
    auto pluginPtr = edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, desc);

    if (pluginPtr != nullptr)
    {
        // Insert at position 0 -- the plugin instrument acts as the sound source
        track->pluginList.insertPlugin (*pluginPtr, 0, nullptr);
        pluginInstrumentInstances[instrumentIndex] = pluginPtr;
    }
}

void TrackerEngine::removePluginInstrumentFromTrack (int instrumentIndex)
{
    auto instanceIt = pluginInstrumentInstances.find (instrumentIndex);
    if (instanceIt == pluginInstrumentInstances.end() || instanceIt->second == nullptr)
        return;

    auto* plugin = instanceIt->second.get();
    if (plugin != nullptr)
        plugin->deleteFromParent();

    pluginInstrumentInstances.erase (instanceIt);
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

    // Check if window already exists
    if (pluginInstrumentEditorWindows.count (instrumentIndex) > 0
        && pluginInstrumentEditorWindows[instrumentIndex] != nullptr)
    {
        pluginInstrumentEditorWindows[instrumentIndex]->toFront (true);
        return;
    }

    auto* extPlugin = dynamic_cast<te::ExternalPlugin*> (plugin);
    if (extPlugin == nullptr)
        return;

    auto audioPlugin = extPlugin->getAudioPluginInstance();
    if (audioPlugin == nullptr)
        return;

    auto editor = audioPlugin->createEditorIfNeeded();
    if (editor == nullptr)
        return;

    struct PluginInstrumentEditorWindow : public juce::DocumentWindow
    {
        PluginInstrumentEditorWindow (const juce::String& name,
                                       std::map<int, std::unique_ptr<juce::DocumentWindow>>& windowMap,
                                       int instIndex)
            : juce::DocumentWindow (name, juce::Colours::darkgrey,
                                    juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
              windows (windowMap), instrumentIndex (instIndex)
        {
        }

        void closeButtonPressed() override
        {
            windows.erase (instrumentIndex);  // destroys this window
        }

    private:
        std::map<int, std::unique_ptr<juce::DocumentWindow>>& windows;
        int instrumentIndex;
    };

    auto window = std::make_unique<PluginInstrumentEditorWindow> (
        extPlugin->getName(), pluginInstrumentEditorWindows, instrumentIndex);

    window->setContentOwned (editor, true);
    window->setResizable (true, false);
    window->centreWithSize (editor->getWidth(), editor->getHeight());
    window->setVisible (true);
    window->setAlwaysOnTop (true);

    pluginInstrumentEditorWindows[instrumentIndex] = std::move (window);
}

void TrackerEngine::closePluginInstrumentEditor (int instrumentIndex)
{
    pluginInstrumentEditorWindows.erase (instrumentIndex);
}

//==============================================================================
// Plugin automation (Phase 5)
//==============================================================================

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

void TrackerEngine::applyPatternAutomation (const PatternAutomationData& automationData,
                                            int /*patternLength*/, int /*rpb*/)
{
    if (edit == nullptr)
        return;

    // Re-baseline whenever a new pattern/arrangement sync happens.
    resetAutomationParameters();

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

        // Store baseline for later reset and row-wise playback updates.
        lastAutomatedParams.push_back ({ lane.pluginId, lane.parameterId, param->getValue() });
    }

    // Prime row-0 value immediately so playback starts from correct automation state.
    applyAutomationForPlaybackRow (automationData, 0);
}

void TrackerEngine::applyAutomationForPlaybackRow (const PatternAutomationData& automationData, int row)
{
    if (automationData.isEmpty())
        return;

    const float rowPosition = static_cast<float> (juce::jmax (0, row));

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

        auto* tracked = findAutomatedParam (lane.pluginId, lane.parameterId);
        if (tracked == nullptr)
        {
            lastAutomatedParams.push_back ({ lane.pluginId, lane.parameterId, param->getValue() });
            tracked = &lastAutomatedParams.back();
        }

        const float value = lane.getValueAtRow (rowPosition, tracked->baselineValue);
        param->setValue (value);
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
        if (param != nullptr)
            param->setValue (ap.baselineValue);
    }

    lastAutomatedParams.clear();
}
