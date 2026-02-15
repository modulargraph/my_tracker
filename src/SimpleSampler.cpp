#include "SimpleSampler.h"

te::SamplerPlugin* SimpleSampler::getOrCreateSampler (te::AudioTrack& track)
{
    // Check if track already has a SamplerPlugin
    if (auto* existing = track.pluginList.findFirstPluginOfType<te::SamplerPlugin>())
        return existing;

    // Create new SamplerPlugin
    if (auto sampler = dynamic_cast<te::SamplerPlugin*> (
            track.edit.getPluginCache().createNewPlugin (te::SamplerPlugin::xmlTypeName, {}).get()))
    {
        track.pluginList.insertPlugin (*sampler, 0, nullptr);
        return sampler;
    }

    return nullptr;
}

juce::String SimpleSampler::loadSample (te::AudioTrack& track, const juce::File& sampleFile, int instrumentIndex)
{
    if (! sampleFile.existsAsFile())
        return "File not found: " + sampleFile.getFullPathName();

    auto* sampler = getOrCreateSampler (track);
    if (sampler == nullptr)
        return "Failed to create sampler plugin";

    // Clear existing sounds
    while (sampler->getNumSounds() > 0)
        sampler->removeSound (0);

    // Add the sample, mapped across the full MIDI range
    auto error = sampler->addSound (sampleFile.getFullPathName(),
                                    sampleFile.getFileNameWithoutExtension(),
                                    0.0, 0.0, 0.0f);

    if (error.isEmpty() && sampler->getNumSounds() > 0)
    {
        // Map sample to full MIDI range with root at C4 (60)
        sampler->setSoundParams (sampler->getNumSounds() - 1, 60, 0, 127);
        sampler->setSoundOpenEnded (sampler->getNumSounds() - 1, true);
        loadedSamples[instrumentIndex] = sampleFile;
    }

    return error;
}

juce::File SimpleSampler::getSampleFile (int instrumentIndex) const
{
    auto it = loadedSamples.find (instrumentIndex);
    if (it != loadedSamples.end())
        return it->second;
    return {};
}

void SimpleSampler::playNote (te::AudioTrack& track, int midiNote)
{
    if (auto* sampler = track.pluginList.findFirstPluginOfType<te::SamplerPlugin>())
    {
        juce::BigInteger notes;
        notes.setBit (midiNote);
        sampler->playNotes (notes);
    }
}

void SimpleSampler::stopNote (te::AudioTrack& track)
{
    if (auto* sampler = track.pluginList.findFirstPluginOfType<te::SamplerPlugin>())
    {
        sampler->allNotesOff();
    }
}
