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

        // Init default params for new instrument if not already set
        if (instrumentParams.find (instrumentIndex) == instrumentParams.end())
            instrumentParams[instrumentIndex] = InstrumentParams{};
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

InstrumentParams SimpleSampler::getParams (int instrumentIndex) const
{
    auto it = instrumentParams.find (instrumentIndex);
    if (it != instrumentParams.end())
        return it->second;
    return {};
}

void SimpleSampler::setParams (int instrumentIndex, const InstrumentParams& params)
{
    instrumentParams[instrumentIndex] = params;
}

juce::String SimpleSampler::applyParams (te::AudioTrack& track, int instrumentIndex)
{
    auto fileIt = loadedSamples.find (instrumentIndex);
    if (fileIt == loadedSamples.end())
        return "No sample loaded for this instrument";

    auto params = getParams (instrumentIndex);
    auto originalFile = fileIt->second;

    if (! originalFile.existsAsFile())
        return "Original sample file not found";

    // If params are default, just reload the original
    if (params.isDefault())
    {
        auto* sampler = getOrCreateSampler (track);
        if (sampler == nullptr) return "No sampler plugin";

        while (sampler->getNumSounds() > 0)
            sampler->removeSound (0);

        auto error = sampler->addSound (originalFile.getFullPathName(),
                                        originalFile.getFileNameWithoutExtension(),
                                        0.0, 0.0, 0.0f);
        if (error.isEmpty() && sampler->getNumSounds() > 0)
        {
            sampler->setSoundParams (sampler->getNumSounds() - 1, 60, 0, 127);
            sampler->setSoundOpenEnded (sampler->getNumSounds() - 1, true);
        }
        return error;
    }

    // Read original audio file
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (originalFile));
    if (reader == nullptr)
        return "Could not read audio file";

    auto totalSamples = static_cast<int> (reader->lengthInSamples);
    auto numChannels  = static_cast<int> (reader->numChannels);
    auto sampleRate   = reader->sampleRate;

    // Crop to start/end
    int startSample = static_cast<int> (params.startPos * totalSamples);
    int endSample   = static_cast<int> (params.endPos * totalSamples);
    startSample = juce::jlimit (0, totalSamples - 1, startSample);
    endSample   = juce::jlimit (startSample + 1, totalSamples, endSample);
    int croppedLength = endSample - startSample;

    if (croppedLength <= 0)
        return "Invalid start/end range";

    // Read the cropped region
    juce::AudioBuffer<float> buffer (numChannels, croppedLength);
    reader->read (&buffer, 0, croppedLength, static_cast<juce::int64> (startSample), true, true);

    // Reverse if enabled
    if (params.reversed)
        buffer.reverse (0, croppedLength);

    // Apply ADSR envelope
    int attackSamples  = static_cast<int> (params.attackMs  * 0.001 * sampleRate);
    int decaySamples   = static_cast<int> (params.decayMs   * 0.001 * sampleRate);
    int releaseSamples = static_cast<int> (params.releaseMs * 0.001 * sampleRate);

    attackSamples  = juce::jmin (attackSamples,  croppedLength);
    decaySamples   = juce::jmin (decaySamples,   croppedLength - attackSamples);
    releaseSamples = juce::jmin (releaseSamples, croppedLength);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);

        // Attack: ramp 0 → 1
        for (int i = 0; i < attackSamples; ++i)
        {
            float env = static_cast<float> (i) / static_cast<float> (juce::jmax (1, attackSamples));
            data[i] *= env;
        }

        // Decay: ramp 1 → sustain
        for (int i = 0; i < decaySamples; ++i)
        {
            float t = static_cast<float> (i) / static_cast<float> (juce::jmax (1, decaySamples));
            float env = 1.0f - t * (1.0f - static_cast<float> (params.sustainLevel));
            data[attackSamples + i] *= env;
        }

        // Sustain: apply sustain level to middle section
        int sustainStart = attackSamples + decaySamples;
        int sustainEnd   = croppedLength - releaseSamples;
        for (int i = sustainStart; i < sustainEnd; ++i)
            data[i] *= static_cast<float> (params.sustainLevel);

        // Release: ramp sustain → 0
        int releaseStart = juce::jmax (sustainStart, croppedLength - releaseSamples);
        float releaseStartLevel = static_cast<float> (params.sustainLevel);
        int releaseLength = croppedLength - releaseStart;
        for (int i = 0; i < releaseLength; ++i)
        {
            float t = static_cast<float> (i) / static_cast<float> (juce::jmax (1, releaseLength));
            data[releaseStart + i] *= releaseStartLevel * (1.0f - t);
        }
    }

    // Write processed audio to temp file
    auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("TrackerAdjust");
    tempDir.createDirectory();
    auto tempFile = tempDir.getChildFile ("processed_" + juce::String (instrumentIndex) + ".wav");

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::OutputStream> outputStream = std::make_unique<juce::FileOutputStream> (tempFile);

    auto options = juce::AudioFormatWriterOptions{}
                       .withSampleRate (sampleRate)
                       .withNumChannels (numChannels)
                       .withBitsPerSample (16);

    auto writer = wavFormat.createWriterFor (outputStream, options);
    if (writer == nullptr)
        return "Failed to create temp WAV file";

    writer->writeFromAudioSampleBuffer (buffer, 0, croppedLength);
    writer.reset();

    // Reload into SamplerPlugin
    auto* sampler = getOrCreateSampler (track);
    if (sampler == nullptr)
        return "No sampler plugin";

    while (sampler->getNumSounds() > 0)
        sampler->removeSound (0);

    auto error = sampler->addSound (tempFile.getFullPathName(),
                                    originalFile.getFileNameWithoutExtension(),
                                    0.0, 0.0, 0.0f);
    if (error.isEmpty() && sampler->getNumSounds() > 0)
    {
        sampler->setSoundParams (sampler->getNumSounds() - 1, 60, 0, 127);
        sampler->setSoundOpenEnded (sampler->getNumSounds() - 1, true);
    }

    return error;
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
