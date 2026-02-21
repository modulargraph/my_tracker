#include <algorithm>
#include "TrackerEngine.h"
#include "InstrumentEffectsPlugin.h"
#include "TrackerSamplerPlugin.h"
#include "MetronomePlugin.h"
#include "SendEffectsPlugin.h"
#include "MixerPlugin.h"
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

        // Build MIDI sequence from pattern data
        juce::MidiMessageSequence midiSeq;
        bool isKill = ! releaseMode[static_cast<size_t> (trackIdx)];
        int lastPlayingNote = -1;
        int currentInst = -1;
        int activePortaSteps = 0;

        for (int row = 0; row < pattern.numRows; ++row)
        {
            const auto& cell = pattern.getCell (row, trackIdx);

            // Compute row time for ALL rows (effects can exist without notes)
            double startBeat = static_cast<double> (row) / static_cast<double> (rowsPerBeat);
            auto rowTime = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (startBeat));

            if (cell.note >= 0)
            {
                const double resetTime = juce::jmax (0.0, rowTime.inSeconds() - 0.00008);
                midiSeq.addEvent (juce::MidiMessage::controllerEvent (1, kCcFxNoteReset, 0), resetTime);
            }

            // Process all FX slots before note handling.  Slots are emitted
            // in lane order at the same ccTime; the sampler processes them
            // sequentially.  B (direction) and P (position) operate on
            // independent voice state so their relative slot order does not
            // matter — see the matching comment in TrackerSamplerPlugin.
            for (int fxSlotIdx = 0; fxSlotIdx < cell.getNumFxSlots(); ++fxSlotIdx)
            {
                const auto& slot = cell.getFxSlot (fxSlotIdx);
                if (slot.isEmpty())
                    continue;

                const auto letter = getSlotCommandLetter (slot);
                if (letter == '\0')
                    continue;

                if (letter == 'G' && slot.fxParam > 0)
                    activePortaSteps = slot.fxParam;

                const double ccTime = juce::jmax (0.0, rowTime.inSeconds() - 0.00005);
                appendSymbolicTrackFx (midiSeq, slot, ccTime);
            }

            const bool rowHasPorta = (activePortaSteps > 0);

            // Skip note-less rows for note processing
            if (cell.note < 0)
                continue;

            // OFF (255) -> graceful release (noteOff for last playing note)
            if (cell.note == 255)
            {
                if (lastPlayingNote >= 0)
                    midiSeq.addEvent (juce::MidiMessage::noteOff (1, lastPlayingNote), rowTime.inSeconds());
                else
                    midiSeq.addEvent (juce::MidiMessage::allNotesOff (1), rowTime.inSeconds());
                lastPlayingNote = -1;
                activePortaSteps = 0;
                continue;
            }

            // KILL (254) -> hard cut (allSoundOff, CC#120)
            if (cell.note == 254)
            {
                midiSeq.addEvent (juce::MidiMessage::allSoundOff (1), rowTime.inSeconds());
                lastPlayingNote = -1;
                activePortaSteps = 0;
                continue;
            }

            // Portamento: send target CC instead of retriggering the note
            if (rowHasPorta && lastPlayingNote >= 0)
            {
                // Send portamento target (CC#28) — no note-on, voice keeps playing
                midiSeq.addEvent (juce::MidiMessage::controllerEvent (1, 28, cell.note & 0x7F),
                                  rowTime.inSeconds());

                // Apply volume change during portamento if specified
                if (cell.volume >= 0)
                    midiSeq.addEvent (juce::MidiMessage::controllerEvent (1, 7, cell.volume),
                                      juce::jmax (0.0, rowTime.inSeconds() - 0.00003));
                activePortaSteps = 0;
                continue;
            }

            // Insert program change if instrument changes
            if (cell.instrument >= 0 && cell.instrument != currentInst)
            {
                currentInst = InstrumentRouting::clampInstrumentIndex (cell.instrument);
                const double bankTime = juce::jmax (0.0, rowTime.inSeconds() - 0.00012);
                const double progTime = juce::jmax (0.0, rowTime.inSeconds() - 0.0001);
                midiSeq.addEvent (juce::MidiMessage::controllerEvent (1, 0,
                                  InstrumentRouting::getBankMsbForInstrument (currentInst)), bankTime);
                midiSeq.addEvent (juce::MidiMessage::programChange (1,
                                  InstrumentRouting::getProgramForInstrument (currentInst)), progTime);
            }

            // Calculate note end time: sustain until next non-porta note/OFF/KILL or pattern end
            int endRow = pattern.numRows;
            for (int nextRow = row + 1; nextRow < pattern.numRows; ++nextRow)
            {
                const auto& nextCell = pattern.getCell (nextRow, trackIdx);
                if (nextCell.note >= 0)
                {
                    // Check if the next note row is a portamento target
                    bool nextIsPorta = false;
                    if (nextCell.note < 254) // Not OFF/KILL
                    {
                        for (int fxi = 0; fxi < nextCell.getNumFxSlots(); ++fxi)
                        {
                            const auto& nextSlot = nextCell.getFxSlot (fxi);
                            if (getSlotCommandLetter (nextSlot) == 'G' && nextSlot.fxParam > 0)
                                nextIsPorta = true;
                        }
                    }

                    if (nextIsPorta)
                        continue; // Skip porta targets, note sustains through them

                    endRow = nextRow;
                    break;
                }
            }
            double endBeat = static_cast<double> (endRow) / static_cast<double> (rowsPerBeat);

            auto noteEnd = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (endBeat));

            int velocity = cell.volume >= 0 ? cell.volume : 127;

            midiSeq.addEvent (juce::MidiMessage::noteOn (1, cell.note, static_cast<juce::uint8> (velocity)),
                              rowTime.inSeconds());

            // Kill mode: hard-cut at end (allSoundOff before noteOff)
            // Release mode: graceful noteOff so release envelope plays
            if (isKill)
                midiSeq.addEvent (juce::MidiMessage::allSoundOff (1), noteEnd.inSeconds());

            midiSeq.addEvent (juce::MidiMessage::noteOff (1, cell.note),
                              noteEnd.inSeconds());

            lastPlayingNote = cell.note;
            activePortaSteps = 0;
        }

        midiSeq.updateMatchedPairs();
        midiClip->mergeInMidiSequence (midiSeq, te::MidiList::NoteAutomationType::none);
    }

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
                int inst = pattern->getCell (row, t).instrument;
                if (inst >= 0
                    && std::find (trackInstruments.begin(), trackInstruments.end(), inst) == trackInstruments.end())
                {
                    trackInstruments.push_back (inst);
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
        double beatOffset = 0.0;
        bool isKill = ! releaseMode[static_cast<size_t> (trackIdx)];
        int lastPlayingNote = -1;
        int currentInst = -1;
        int activePortaSteps = 0;

        for (auto& [pattern, repeats] : sequence)
        {
            double patternLengthBeats = static_cast<double> (pattern->numRows) / static_cast<double> (rpb);

            for (int rep = 0; rep < repeats; ++rep)
            {
                for (int row = 0; row < pattern->numRows; ++row)
                {
                    const auto& cell = pattern->getCell (row, trackIdx);

                    // Compute row time for ALL rows (effects can exist without notes)
                    double startBeat = beatOffset + static_cast<double> (row) / static_cast<double> (rpb);
                    auto rowTime = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (startBeat));

                    if (cell.note >= 0)
                    {
                        const double resetTime = juce::jmax (0.0, rowTime.inSeconds() - 0.00008);
                        midiSeq.addEvent (juce::MidiMessage::controllerEvent (1, kCcFxNoteReset, 0), resetTime);
                    }

                    // See syncPatternToEdit() — B and P are order-independent.
                    for (int fxSlotIdx = 0; fxSlotIdx < cell.getNumFxSlots(); ++fxSlotIdx)
                    {
                        const auto& slot = cell.getFxSlot (fxSlotIdx);
                        if (slot.isEmpty())
                            continue;

                        const auto letter = getSlotCommandLetter (slot);
                        if (letter == '\0')
                            continue;

                        if (letter == 'G' && slot.fxParam > 0)
                            activePortaSteps = slot.fxParam;

                        const double ccTime = juce::jmax (0.0, rowTime.inSeconds() - 0.00005);
                        appendSymbolicTrackFx (midiSeq, slot, ccTime);
                    }

                    const bool rowHasPorta = (activePortaSteps > 0);

                    // Skip note-less rows for note processing
                    if (cell.note < 0)
                        continue;

                    // OFF (255) -> graceful release
                    if (cell.note == 255)
                    {
                        if (lastPlayingNote >= 0)
                            midiSeq.addEvent (juce::MidiMessage::noteOff (1, lastPlayingNote), rowTime.inSeconds());
                        else
                            midiSeq.addEvent (juce::MidiMessage::allNotesOff (1), rowTime.inSeconds());
                        lastPlayingNote = -1;
                        activePortaSteps = 0;
                        continue;
                    }

                    // KILL (254) -> hard cut
                    if (cell.note == 254)
                    {
                        midiSeq.addEvent (juce::MidiMessage::allSoundOff (1), rowTime.inSeconds());
                        lastPlayingNote = -1;
                        activePortaSteps = 0;
                        continue;
                    }

                    // Portamento: send target CC instead of retriggering the note
                    if (rowHasPorta && lastPlayingNote >= 0)
                    {
                        midiSeq.addEvent (juce::MidiMessage::controllerEvent (1, 28, cell.note & 0x7F),
                                          rowTime.inSeconds());
                        if (cell.volume >= 0)
                            midiSeq.addEvent (juce::MidiMessage::controllerEvent (1, 7, cell.volume),
                                              juce::jmax (0.0, rowTime.inSeconds() - 0.00003));
                        activePortaSteps = 0;
                        continue;
                    }

                    // Insert program change if instrument changes
                    if (cell.instrument >= 0 && cell.instrument != currentInst)
                    {
                        currentInst = InstrumentRouting::clampInstrumentIndex (cell.instrument);
                        const double bankTime = juce::jmax (0.0, rowTime.inSeconds() - 0.00012);
                        const double progTime = juce::jmax (0.0, rowTime.inSeconds() - 0.0001);
                        midiSeq.addEvent (juce::MidiMessage::controllerEvent (1, 0,
                                          InstrumentRouting::getBankMsbForInstrument (currentInst)), bankTime);
                        midiSeq.addEvent (juce::MidiMessage::programChange (1,
                                          InstrumentRouting::getProgramForInstrument (currentInst)), progTime);
                    }

                    // Calculate note end time: sustain until next non-porta note/OFF/KILL or end of repeat
                    double repeatEndBeat = beatOffset + patternLengthBeats;
                    double endBeat = repeatEndBeat;
                    for (int nextRow = row + 1; nextRow < pattern->numRows; ++nextRow)
                    {
                        const auto& nextCell = pattern->getCell (nextRow, trackIdx);
                        if (nextCell.note >= 0)
                        {
                            bool nextIsPorta = false;
                            if (nextCell.note < 254)
                            {
                                for (int fxi = 0; fxi < nextCell.getNumFxSlots(); ++fxi)
                                {
                                    const auto& nextSlot = nextCell.getFxSlot (fxi);
                                    if (getSlotCommandLetter (nextSlot) == 'G' && nextSlot.fxParam > 0)
                                        nextIsPorta = true;
                                }
                            }

                            if (nextIsPorta)
                                continue; // Skip porta targets, note sustains through them

                            endBeat = beatOffset + static_cast<double> (nextRow) / static_cast<double> (rpb);
                            break;
                        }
                    }

                    auto noteEnd = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (endBeat));

                    int velocity = cell.volume >= 0 ? cell.volume : 127;

                    midiSeq.addEvent (juce::MidiMessage::noteOn (1, cell.note, static_cast<juce::uint8> (velocity)),
                                      rowTime.inSeconds());

                    // Kill mode: hard-cut at end (allSoundOff before noteOff)
                    // Release mode: graceful noteOff so release envelope plays
                    if (isKill)
                        midiSeq.addEvent (juce::MidiMessage::allSoundOff (1), noteEnd.inSeconds());

                    midiSeq.addEvent (juce::MidiMessage::noteOff (1, cell.note),
                                      noteEnd.inSeconds());

                    lastPlayingNote = cell.note;
                    activePortaSteps = 0;
                }

                beatOffset += patternLengthBeats;
            }
        }

        midiSeq.updateMatchedPairs();
        midiClip->mergeInMidiSequence (midiSeq, te::MidiList::NoteAutomationType::none);
    }

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
        // Check if this track uses the specified instrument
        bool usesInstrument = false;
        for (int row = 0; row < pattern.numRows; ++row)
        {
            if (pattern.getCell (row, t).instrument == instrumentIndex)
            {
                usesInstrument = true;
                break;
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
            int inst = pattern.getCell (row, t).instrument;
            if (inst >= 0
                && std::find (trackInstruments.begin(), trackInstruments.end(), inst) == trackInstruments.end())
            {
                trackInstruments.push_back (inst);
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

void TrackerEngine::setMixerState (const MixerState* state)
{
    mixerStatePtr = state;
    setupMixerPlugins();
}

void TrackerEngine::setupMixerPlugins()
{
    if (edit == nullptr || mixerStatePtr == nullptr)
        return;

    auto tracks = te::getAudioTracks (*edit);

    for (int t = 0; t < kNumTracks && t < tracks.size(); ++t)
    {
        auto* track = tracks[t];

        auto* existing = track->pluginList.findFirstPluginOfType<MixerPlugin>();
        if (existing == nullptr)
        {
            if (auto plugin = dynamic_cast<MixerPlugin*> (
                    track->edit.getPluginCache().createNewPlugin (MixerPlugin::xmlTypeName, {}).get()))
            {
                // Insert after InstrumentEffectsPlugin (position 2: sampler=0, effects=1, mixer=2)
                track->pluginList.insertPlugin (*plugin, 2, nullptr);
                existing = plugin;
            }
        }

        if (existing != nullptr)
        {
            existing->setMixState (mixerStatePtr->tracks[static_cast<size_t> (t)]);
            existing->setSendBuffers (&sampler.getSendBuffers());
        }
    }
}

void TrackerEngine::refreshMixerPlugins()
{
    setupMixerPlugins();
}

float TrackerEngine::getTrackPeakLevel (int trackIndex) const
{
    if (edit == nullptr || trackIndex < 0 || trackIndex >= kNumTracks)
        return 0.0f;

    auto tracks = te::getAudioTracks (*edit);
    if (trackIndex >= tracks.size())
        return 0.0f;

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
        auto* mixer = tracks[t]->pluginList.findFirstPluginOfType<MixerPlugin>();
        if (mixer != nullptr)
            mixer->resetPeak();
    }
}

void TrackerEngine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (onTransportChanged)
        onTransportChanged();
}
