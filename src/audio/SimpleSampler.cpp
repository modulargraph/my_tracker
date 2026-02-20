#include "SimpleSampler.h"
#include "InstrumentEffectsPlugin.h"

GlobalModState* SimpleSampler::getOrCreateGlobalModState (int instrumentIndex)
{
    const juce::SpinLock::ScopedLockType lock (stateLock);

    auto it = globalModStates.find (instrumentIndex);
    if (it != globalModStates.end())
        return it->second.get();

    auto state = std::make_unique<GlobalModState>();
    auto* ptr = state.get();
    globalModStates[instrumentIndex] = std::move (state);
    return ptr;
}

TrackerSamplerPlugin* SimpleSampler::getOrCreateTrackerSampler (te::AudioTrack& track)
{
    if (auto* existing = track.pluginList.findFirstPluginOfType<TrackerSamplerPlugin>())
        return existing;

    if (auto plugin = dynamic_cast<TrackerSamplerPlugin*> (
            track.edit.getPluginCache().createNewPlugin (TrackerSamplerPlugin::xmlTypeName, {}).get()))
    {
        track.pluginList.insertPlugin (*plugin, 0, nullptr);
        return plugin;
    }

    return nullptr;
}

InstrumentEffectsPlugin* SimpleSampler::getOrCreateEffectsPlugin (te::AudioTrack& track, int instrumentIndex)
{
    for (auto* plugin : track.pluginList)
    {
        if (auto* fx = dynamic_cast<InstrumentEffectsPlugin*> (plugin))
        {
            fx->setSamplerSource (this);
            fx->setInstrumentIndex (instrumentIndex);
            fx->setSendBuffers (&sendBuffers);
            return fx;
        }
    }

    if (auto fx = dynamic_cast<InstrumentEffectsPlugin*> (
            track.edit.getPluginCache().createNewPlugin (InstrumentEffectsPlugin::xmlTypeName, {}).get()))
    {
        int insertPos = 1;
        track.pluginList.insertPlugin (*fx, insertPos, nullptr);
        fx->setSamplerSource (this);
        fx->setInstrumentIndex (instrumentIndex);
        fx->setSendBuffers (&sendBuffers);
        return fx;
    }

    return nullptr;
}

void SimpleSampler::setupPluginChain (te::AudioTrack& track, int instrumentIndex)
{
    getOrCreateTrackerSampler (track);
    getOrCreateEffectsPlugin (track, instrumentIndex);
}

//==============================================================================
// Load sample
//==============================================================================

juce::String SimpleSampler::loadInstrumentSample (const juce::File& sampleFile, int instrumentIndex)
{
    if (! sampleFile.existsAsFile())
        return "File not found: " + sampleFile.getFullPathName();

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (sampleFile));
    if (reader == nullptr)
        return "Failed to read audio file: " + sampleFile.getFullPathName();

    auto bank = std::make_shared<SampleBank>();
    bank->sampleRate = reader->sampleRate;
    bank->numChannels = static_cast<int> (reader->numChannels);
    bank->totalSamples = static_cast<juce::int64> (reader->lengthInSamples);
    bank->sourceFile = sampleFile;
    bank->buffer.setSize (bank->numChannels, static_cast<int> (reader->lengthInSamples));
    reader->read (&bank->buffer, 0, static_cast<int> (reader->lengthInSamples), 0, true, true);

    {
        const juce::SpinLock::ScopedLockType lock (stateLock);
        sampleBanks[instrumentIndex] = bank;
        loadedSamples[instrumentIndex] = sampleFile;

        if (instrumentParams.find (instrumentIndex) == instrumentParams.end())
            instrumentParams[instrumentIndex] = InstrumentParams {};
    }

    return {};
}

juce::String SimpleSampler::loadSample (te::AudioTrack& track, const juce::File& sampleFile, int instrumentIndex)
{
    auto result = loadInstrumentSample (sampleFile, instrumentIndex);
    if (result.isNotEmpty())
        return result;

    return applyParams (track, instrumentIndex);
}

juce::File SimpleSampler::getSampleFile (int instrumentIndex) const
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    auto it = loadedSamples.find (instrumentIndex);
    if (it != loadedSamples.end())
        return it->second;
    return {};
}

void SimpleSampler::clearInstrumentSample (int instrumentIndex)
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    loadedSamples.erase (instrumentIndex);
    sampleBanks.erase (instrumentIndex);
}

std::map<int, juce::File> SimpleSampler::getLoadedSamples() const
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    return loadedSamples;
}

void SimpleSampler::clearLoadedSamples()
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    loadedSamples.clear();
    instrumentParams.clear();
    sampleBanks.clear();
    // Keep globalModStates alive: effects plugins can still hold pointers.
}

std::shared_ptr<const SampleBank> SimpleSampler::getSampleBank (int instrumentIndex) const
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    auto it = sampleBanks.find (instrumentIndex);
    if (it != sampleBanks.end())
        return it->second;
    return nullptr;
}

InstrumentParams SimpleSampler::getParams (int instrumentIndex) const
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    auto it = instrumentParams.find (instrumentIndex);
    if (it != instrumentParams.end())
        return it->second;
    return {};
}

bool SimpleSampler::getParamsIfPresent (int instrumentIndex, InstrumentParams& outParams) const
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    auto it = instrumentParams.find (instrumentIndex);
    if (it == instrumentParams.end())
        return false;

    outParams = it->second;
    return true;
}

void SimpleSampler::setParams (int instrumentIndex, const InstrumentParams& params)
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    instrumentParams[instrumentIndex] = params;
}

std::map<int, InstrumentParams> SimpleSampler::getAllParams() const
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    return instrumentParams;
}

void SimpleSampler::clearAllParams()
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    instrumentParams.clear();
}

//==============================================================================
// Apply params (no file I/O - just update plugin state)
//==============================================================================

juce::String SimpleSampler::applyParams (te::AudioTrack& track, int instrumentIndex)
{
    std::shared_ptr<const SampleBank> bank;
    {
        const juce::SpinLock::ScopedLockType lock (stateLock);
        auto bankIt = sampleBanks.find (instrumentIndex);
        if (bankIt == sampleBanks.end())
            return "No sample loaded for this instrument";
        bank = bankIt->second;
    }

    auto* sampler = getOrCreateTrackerSampler (track);
    if (sampler == nullptr)
        return "No sampler plugin";

    sampler->setSampleBank (bank);
    sampler->setSamplerSource (this);
    sampler->setInstrumentIndex (instrumentIndex);

    setupPluginChain (track, instrumentIndex);

    return {};
}

//==============================================================================
// Preview
//==============================================================================

void SimpleSampler::playNote (te::AudioTrack& track, int midiNote, float velocity)
{
    if (auto* sampler = track.pluginList.findFirstPluginOfType<TrackerSamplerPlugin>())
        sampler->playNote (midiNote, juce::jlimit (0.0f, 1.0f, velocity));
}

void SimpleSampler::stopNote (te::AudioTrack& track)
{
    if (auto* sampler = track.pluginList.findFirstPluginOfType<TrackerSamplerPlugin>())
        sampler->stopAllNotes();
}
