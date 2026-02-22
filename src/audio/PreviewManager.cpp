#include "PreviewManager.h"
#include "TrackerEngine.h"
#include "TrackerSamplerPlugin.h"
#include "InstrumentEffectsPlugin.h"

PreviewManager::PreviewManager (TrackerEngine& e)
    : engine (e)
{
}

void PreviewManager::previewNote (int trackIndex, int instrumentIndex, int midiNote, bool autoStop)
{
    juce::ignoreUnused (trackIndex);

    if (instrumentIndex < 0)
        return;

    stopPreview();

    // Plugin instrument: inject an explicit note-on on the owner track via
    // injectLiveMidiMessage so we have full control over note-off timing.
    // playGuideNote with autorelease killed the note after ~100ms, breaking
    // hold-to-preview; and clearing state immediately meant stopPluginPreview
    // could never send the matching note-off, causing stuck notes.
    if (engine.isPluginInstrument (instrumentIndex))
    {
        engine.ensurePluginInstrumentLoaded (instrumentIndex);

        const auto& slotInfo = engine.getInstrumentSlotInfo (instrumentIndex);
        auto* ownerTrack = engine.getTrack (slotInfo.ownerTrack);
        if (ownerTrack != nullptr)
        {
            int note = juce::jlimit (0, 127, midiNote);
            int velocity = juce::jlimit (1, 127, static_cast<int> (previewVolume * 127.0f + 0.5f));
            ownerTrack->injectLiveMidiMessage (
                juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (velocity)), 0);

            previewPluginNote = note;
            previewPluginInstrument = instrumentIndex;
            previewPluginTrack = slotInfo.ownerTrack;
        }

        if (autoStop)
            startTimer (kPluginPreviewDurationMs);
        return;
    }

    // Sample instrument: preview through the dedicated preview track.
    auto* track = engine.getTrack (kNumTracks);
    if (track == nullptr)
        return;

    engine.ensureTrackHasInstrument (kNumTracks, instrumentIndex);

    // Preview should match instrument DSP and sends, with preview volume applied
    // as a track-level output gain (not as note velocity).
    if (auto* fxPlugin = engine.getSampler().getOrCreateEffectsPlugin (*track, instrumentIndex))
    {
        fxPlugin->setRowsPerBeat (engine.getRowsPerBeat());
        auto* globalState = engine.getSampler().getOrCreateGlobalModState (instrumentIndex);
        fxPlugin->setGlobalModState (globalState);
        std::map<int, GlobalModState*> globalStates;
        globalStates[instrumentIndex] = globalState;
        fxPlugin->setGlobalModStates (globalStates);
        fxPlugin->setOutputGainLinear (previewVolume);
    }

    engine.getSampler().playNote (*track, midiNote, 1.0f);

    activePreviewTrack = kNumTracks;

    // Auto-stop: safety timeout; hold-to-preview relies on stopPreview() from key release
    if (autoStop)
        startTimer (kPreviewDurationMs);
}

float PreviewManager::getPreviewPlaybackPosition() const
{
    if (engine.getEdit() == nullptr || activePreviewTrack < 0)
        return -1.0f;

    auto tracks = te::getAudioTracks (*engine.getEdit());
    if (activePreviewTrack >= tracks.size())
        return -1.0f;

    auto* samplerPlugin = tracks[activePreviewTrack]->pluginList.findFirstPluginOfType<TrackerSamplerPlugin>();
    if (samplerPlugin == nullptr)
        return -1.0f;

    return samplerPlugin->getPlaybackPosition();
}

void PreviewManager::previewAudioFile (const juce::File& file)
{
    if (engine.getEdit() == nullptr)
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
    auto* track = engine.getTrack (kNumTracks);
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
    engine.setCurrentTrackInstrument (kNumTracks, -1);
    if (auto* fxPlugin = track->pluginList.findFirstPluginOfType<InstrumentEffectsPlugin>())
        fxPlugin->setSamplerSource (nullptr);

    samplerPlugin->setSampleBank (bank);
    samplerPlugin->playNote (60, previewVolume);

    activePreviewTrack = kNumTracks;
    startTimer (kPreviewDurationMs);
}

void PreviewManager::previewInstrument (int instrumentIndex)
{
    if (engine.getEdit() == nullptr)
        return;

    auto bank = engine.getSampler().getSampleBank (instrumentIndex);
    if (bank == nullptr)
        return;

    previewNote (kNumTracks, instrumentIndex, 60, true);
}

bool PreviewManager::stopPluginPreview()
{
    if (previewPluginNote >= 0 && previewPluginTrack >= 0)
    {
        auto* track = engine.getTrack (previewPluginTrack);
        if (track != nullptr)
            track->injectLiveMidiMessage (
                juce::MidiMessage::noteOff (1, previewPluginNote), 0);
    }

    previewPluginNote = -1;
    previewPluginInstrument = -1;
    previewPluginTrack = -1;
    return true;
}

void PreviewManager::stopPreview()
{
    stopTimer();
    stopPluginPreview();

    if (activePreviewTrack >= 0)
    {
        auto* track = engine.getTrack (activePreviewTrack);
        if (track != nullptr)
            engine.getSampler().stopNote (*track);

        activePreviewTrack = -1;
    }

    previewBank = nullptr;
}

void PreviewManager::setPreviewVolume (float gainLinear)
{
    previewVolume = juce::jlimit (0.0f, 1.0f, gainLinear);

    if (auto* track = engine.getTrack (kNumTracks))
        if (auto* fxPlugin = track->pluginList.findFirstPluginOfType<InstrumentEffectsPlugin>())
            fxPlugin->setOutputGainLinear (previewVolume);
}

void PreviewManager::timerCallback()
{
    stopTimer();
    stopPluginPreview();

    if (activePreviewTrack >= 0)
    {
        auto* track = engine.getTrack (activePreviewTrack);
        if (track != nullptr)
            engine.getSampler().stopNote (*track);

        activePreviewTrack = -1;
    }
}
