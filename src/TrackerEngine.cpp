#include "TrackerEngine.h"

TrackerEngine::TrackerEngine()
{
}

TrackerEngine::~TrackerEngine()
{
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

    // Create an edit
    auto editFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("TrackerAdjust")
                        .getChildFile ("session.tracktionedit");
    editFile.getParentDirectory().createDirectory();

    edit = te::createEmptyEdit (*engine, editFile);
    edit->playInStopEnabled = true;

    // Create 16 audio tracks
    edit->ensureNumberOfAudioTracks (kNumTracks);

    // Listen for transport changes
    edit->getTransport().addChangeListener (this);

    // Ensure playback context
    edit->getTransport().ensureContextAllocated();
}

void TrackerEngine::syncPatternToEdit (const Pattern& pattern)
{
    if (edit == nullptr)
        return;

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

        for (int row = 0; row < pattern.numRows; ++row)
        {
            const auto& cell = pattern.getCell (row, trackIdx);
            if (cell.note < 0)
                continue;

            double startBeat = static_cast<double> (row) / static_cast<double> (rowsPerBeat);
            double endBeat = startBeat + (1.0 / static_cast<double> (rowsPerBeat));

            // Convert beat positions to time
            auto noteStart = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (startBeat));
            auto noteEnd = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (endBeat));

            int velocity = cell.volume >= 0 ? cell.volume : 127;

            midiSeq.addEvent (juce::MidiMessage::noteOn (1, cell.note, static_cast<juce::uint8> (velocity)),
                              noteStart.inSeconds());
            midiSeq.addEvent (juce::MidiMessage::noteOff (1, cell.note),
                              noteEnd.inSeconds());
        }

        midiSeq.updateMatchedPairs();
        midiClip->mergeInMidiSequence (midiSeq, te::MidiList::NoteAutomationType::none);
    }
}

void TrackerEngine::syncArrangementToEdit (const std::vector<std::pair<const Pattern*, int>>& sequence, int rpb)
{
    if (edit == nullptr || sequence.empty())
        return;

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

        for (auto& [pattern, repeats] : sequence)
        {
            double patternLengthBeats = static_cast<double> (pattern->numRows) / static_cast<double> (rpb);

            for (int rep = 0; rep < repeats; ++rep)
            {
                for (int row = 0; row < pattern->numRows; ++row)
                {
                    const auto& cell = pattern->getCell (row, trackIdx);
                    if (cell.note < 0)
                        continue;

                    double startBeat = beatOffset + static_cast<double> (row) / static_cast<double> (rpb);
                    double endBeat = startBeat + (1.0 / static_cast<double> (rpb));

                    auto noteStart = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (startBeat));
                    auto noteEnd = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (endBeat));

                    int velocity = cell.volume >= 0 ? cell.volume : 127;

                    midiSeq.addEvent (juce::MidiMessage::noteOn (1, cell.note, static_cast<juce::uint8> (velocity)),
                                      noteStart.inSeconds());
                    midiSeq.addEvent (juce::MidiMessage::noteOff (1, cell.note),
                                      noteEnd.inSeconds());
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

juce::String TrackerEngine::loadSampleForTrack (int trackIndex, const juce::File& sampleFile)
{
    auto* track = getTrack (trackIndex);
    if (track == nullptr)
        return "Track not found";

    return sampler.loadSample (*track, sampleFile, trackIndex);
}

void TrackerEngine::previewNote (int trackIndex, int midiNote)
{
    auto* track = getTrack (trackIndex);
    if (track != nullptr)
        sampler.playNote (*track, midiNote);
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

void TrackerEngine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (onTransportChanged)
        onTransportChanged();
}
