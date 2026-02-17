#include <set>
#include "TrackerEngine.h"
#include "InstrumentEffectsPlugin.h"
#include "TrackerSamplerPlugin.h"
#include "SendEffectsPlugin.h"

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

    edit = nullptr;
    engine = nullptr;
}

void TrackerEngine::initialise()
{
    engine = std::make_unique<te::Engine> ("TrackerAdjust");

    // Register custom plugin types
    engine->getPluginManager().createBuiltInType<InstrumentEffectsPlugin>();
    engine->getPluginManager().createBuiltInType<TrackerSamplerPlugin>();
    engine->getPluginManager().createBuiltInType<SendEffectsPlugin>();

    // Create an edit
    auto editFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("TrackerAdjust")
                        .getChildFile ("session.tracktionedit");
    editFile.getParentDirectory().createDirectory();

    edit = te::createEmptyEdit (*engine, editFile);
    edit->playInStopEnabled = true;

    // Create 16 audio tracks + 1 preview track + 1 send effects bus track
    edit->ensureNumberOfAudioTracks (kNumTracks + 2);

    // Set up the send effects bus track
    setupSendEffectsTrack();

    // Listen for transport changes
    edit->getTransport().addChangeListener (this);

    // Ensure playback context
    edit->getTransport().ensureContextAllocated();
}

void TrackerEngine::syncPatternToEdit (const Pattern& pattern,
                                       const std::array<bool, kNumTracks>& releaseMode)
{
    if (edit == nullptr)
        return;

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

        for (int row = 0; row < pattern.numRows; ++row)
        {
            const auto& cell = pattern.getCell (row, trackIdx);

            // Compute row time for ALL rows (effects can exist without notes)
            double startBeat = static_cast<double> (row) / static_cast<double> (rowsPerBeat);
            auto rowTime = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (startBeat));

            // Process effect commands (before note check — effects work independently)
            if (cell.fx > 0)
            {
                if (cell.fx == 0x8) // Panning: 8xx → CC 10
                {
                    int ccVal = juce::jlimit (0, 127, cell.fxParam / 2);
                    midiSeq.addEvent (juce::MidiMessage::controllerEvent (1, 10, ccVal),
                                      rowTime.inSeconds() - 0.00005);
                }
                else if (cell.fx == 0xE) // Mod mode: Exy → CC 85
                {
                    midiSeq.addEvent (juce::MidiMessage::controllerEvent (1, 85, cell.fxParam),
                                      rowTime.inSeconds() - 0.00005);
                }
            }

            // Skip note-less rows for note processing
            if (cell.note < 0)
                continue;

            // OFF (255) → graceful release (noteOff for last playing note)
            if (cell.note == 255)
            {
                if (lastPlayingNote >= 0)
                    midiSeq.addEvent (juce::MidiMessage::noteOff (1, lastPlayingNote), rowTime.inSeconds());
                else
                    midiSeq.addEvent (juce::MidiMessage::allNotesOff (1), rowTime.inSeconds());
                lastPlayingNote = -1;
                continue;
            }

            // KILL (254) → hard cut (allSoundOff, CC#120)
            if (cell.note == 254)
            {
                midiSeq.addEvent (juce::MidiMessage::allSoundOff (1), rowTime.inSeconds());
                lastPlayingNote = -1;
                continue;
            }

            // Insert program change if instrument changes
            if (cell.instrument >= 0 && cell.instrument != currentInst)
            {
                currentInst = cell.instrument;
                midiSeq.addEvent (juce::MidiMessage::programChange (1, currentInst),
                                  rowTime.inSeconds() - 0.0001);
            }

            // Calculate note end time: sustain until next note/OFF/KILL or pattern end
            int endRow = pattern.numRows;
            for (int nextRow = row + 1; nextRow < pattern.numRows; ++nextRow)
            {
                if (pattern.getCell (nextRow, trackIdx).note >= 0)
                {
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
        }

        midiSeq.updateMatchedPairs();
        midiClip->mergeInMidiSequence (midiSeq, te::MidiList::NoteAutomationType::none);
    }
}

void TrackerEngine::syncArrangementToEdit (const std::vector<std::pair<const Pattern*, int>>& sequence, int rpb,
                                            const std::array<bool, kNumTracks>& releaseMode)
{
    if (edit == nullptr || sequence.empty())
        return;

    // Prepare instruments for all patterns in the arrangement
    for (auto& [pattern, repeats] : sequence)
        prepareTracksForPattern (*pattern);

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

                    // Process effect commands (before note check)
                    if (cell.fx > 0)
                    {
                        if (cell.fx == 0x8) // Panning: 8xx → CC 10
                        {
                            int ccVal = juce::jlimit (0, 127, cell.fxParam / 2);
                            midiSeq.addEvent (juce::MidiMessage::controllerEvent (1, 10, ccVal),
                                              rowTime.inSeconds() - 0.00005);
                        }
                        else if (cell.fx == 0xE) // Mod mode: Exy → CC 85
                        {
                            midiSeq.addEvent (juce::MidiMessage::controllerEvent (1, 85, cell.fxParam),
                                              rowTime.inSeconds() - 0.00005);
                        }
                    }

                    // Skip note-less rows for note processing
                    if (cell.note < 0)
                        continue;

                    // OFF (255) → graceful release
                    if (cell.note == 255)
                    {
                        if (lastPlayingNote >= 0)
                            midiSeq.addEvent (juce::MidiMessage::noteOff (1, lastPlayingNote), rowTime.inSeconds());
                        else
                            midiSeq.addEvent (juce::MidiMessage::allNotesOff (1), rowTime.inSeconds());
                        lastPlayingNote = -1;
                        continue;
                    }

                    // KILL (254) → hard cut
                    if (cell.note == 254)
                    {
                        midiSeq.addEvent (juce::MidiMessage::allSoundOff (1), rowTime.inSeconds());
                        lastPlayingNote = -1;
                        continue;
                    }

                    // Insert program change if instrument changes
                    if (cell.instrument >= 0 && cell.instrument != currentInst)
                    {
                        currentInst = cell.instrument;
                        midiSeq.addEvent (juce::MidiMessage::programChange (1, currentInst),
                                          rowTime.inSeconds() - 0.0001);
                    }

                    // Calculate note end time: sustain until next note/OFF/KILL or end of repeat
                    double repeatEndBeat = beatOffset + patternLengthBeats;
                    double endBeat = repeatEndBeat;
                    for (int nextRow = row + 1; nextRow < pattern->numRows; ++nextRow)
                    {
                        if (pattern->getCell (nextRow, trackIdx).note >= 0)
                        {
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
                }

                beatOffset += patternLengthBeats;
            }
        }

        midiSeq.updateMatchedPairs();
        midiClip->mergeInMidiSequence (midiSeq, te::MidiList::NoteAutomationType::none);
    }
}

void TrackerEngine::play()
{
    if (edit == nullptr)
        return;

    auto& transport = edit->getTransport();

    // Set loop range to the full pattern
    auto tracks = te::getAudioTracks (*edit);
    if (tracks.size() > 0)
    {
        auto clips = tracks[0]->getClips();
        if (clips.size() > 0)
        {
            auto clipRange = clips[0]->getEditTimeRange();
            transport.setLoopRange (clipRange);
            transport.looping = true;
        }
    }

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

double TrackerEngine::getPlaybackBeatPosition() const
{
    if (edit == nullptr || ! isPlaying())
        return -1.0;

    auto pos = edit->getTransport().getPosition();
    return edit->tempoSequence.toBeats (pos).inBeats();
}

void TrackerEngine::setBpm (double bpm)
{
    if (edit == nullptr)
        return;

    edit->tempoSequence.getTempos()[0]->setBpm (bpm);
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

void TrackerEngine::ensureTrackHasInstrument (int trackIndex, int instrumentIndex)
{
    if (trackIndex < 0 || trackIndex >= kNumTracks || instrumentIndex < 0)
        return;

    auto* track = getTrack (trackIndex);
    if (track == nullptr)
        return;

    if (currentTrackInstrument[static_cast<size_t> (trackIndex)] != instrumentIndex)
    {
        sampler.applyParams (*track, instrumentIndex);
        currentTrackInstrument[static_cast<size_t> (trackIndex)] = instrumentIndex;
    }
}

void TrackerEngine::prepareTracksForPattern (const Pattern& pattern)
{
    if (edit == nullptr)
        return;

    auto tracks = te::getAudioTracks (*edit);

    for (int t = 0; t < kNumTracks && t < tracks.size(); ++t)
    {
        // Collect ALL unique instruments used on this track
        std::set<int> usedInstruments;
        int firstInst = -1;
        for (int row = 0; row < pattern.numRows; ++row)
        {
            int inst = pattern.getCell (row, t).instrument;
            if (inst >= 0)
            {
                usedInstruments.insert (inst);
                if (firstInst < 0)
                    firstInst = inst;
            }
        }

        // If no instrument specified in pattern, skip this track
        if (firstInst < 0)
            continue;

        // Load the first (default) instrument onto this track
        if (firstInst != currentTrackInstrument[static_cast<size_t> (t)])
        {
            sampler.applyParams (*tracks[t], firstInst);
            currentTrackInstrument[static_cast<size_t> (t)] = firstInst;
        }

        // Pre-load all banks for multi-instrument support
        if (usedInstruments.size() > 1)
        {
            std::map<int, std::shared_ptr<const SampleBank>> banks;
            for (int inst : usedInstruments)
            {
                auto bank = sampler.getSampleBank (inst);
                if (bank != nullptr)
                    banks[inst] = bank;
            }

            if (auto* samplerPlugin = tracks[t]->pluginList.findFirstPluginOfType<TrackerSamplerPlugin>())
                samplerPlugin->preloadBanks (banks);
        }

        // Configure effects plugin with rowsPerBeat, global mod state, and send buffers
        if (auto* fxPlugin = sampler.getOrCreateEffectsPlugin (*tracks[t], firstInst))
        {
            fxPlugin->setRowsPerBeat (rowsPerBeat);
            fxPlugin->setGlobalModState (sampler.getOrCreateGlobalModState (firstInst));
            fxPlugin->setSendBuffers (&sampler.getSendBuffers());
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

void TrackerEngine::previewNote (int trackIndex, int instrumentIndex, int midiNote)
{
    auto* track = getTrack (trackIndex);
    if (track == nullptr)
        return;

    ensureTrackHasInstrument (trackIndex, instrumentIndex);
    sampler.playNote (*track, midiNote);

    // Auto-stop after preview duration
    activePreviewTrack = trackIndex;
    startTimer (kPreviewDurationMs);
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

    samplerPlugin->setSampleBank (bank);
    samplerPlugin->playNote (60, 1.0f);

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

    // Stop any current preview
    stopPreview();

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

    samplerPlugin->setSampleBank (bank);
    samplerPlugin->playNote (60, 1.0f);

    activePreviewTrack = kPreviewTrack;
    startTimer (kPreviewDurationMs);
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

void TrackerEngine::setupSendEffectsTrack()
{
    auto* track = getTrack (kSendEffectsTrack);
    if (track == nullptr) return;

    // Prepare send buffers (default block size, stereo)
    sampler.getSendBuffers().prepare (2048, 2);

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
        sendEffectsPlugin->delayParams = params;
}

void TrackerEngine::setReverbParams (const ReverbParams& params)
{
    if (sendEffectsPlugin != nullptr)
        sendEffectsPlugin->reverbParams = params;
}

DelayParams TrackerEngine::getDelayParams() const
{
    if (sendEffectsPlugin != nullptr)
        return sendEffectsPlugin->delayParams;
    return {};
}

ReverbParams TrackerEngine::getReverbParams() const
{
    if (sendEffectsPlugin != nullptr)
        return sendEffectsPlugin->reverbParams;
    return {};
}

void TrackerEngine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (onTransportChanged)
        onTransportChanged();
}
