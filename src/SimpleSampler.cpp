#include "SimpleSampler.h"
#include "InstrumentEffectsPlugin.h"

te::SamplerPlugin* SimpleSampler::getOrCreateSampler (te::AudioTrack& track)
{
    if (auto* existing = track.pluginList.findFirstPluginOfType<te::SamplerPlugin>())
        return existing;

    if (auto sampler = dynamic_cast<te::SamplerPlugin*> (
            track.edit.getPluginCache().createNewPlugin (te::SamplerPlugin::xmlTypeName, {}).get()))
    {
        track.pluginList.insertPlugin (*sampler, 0, nullptr);
        return sampler;
    }

    return nullptr;
}

InstrumentEffectsPlugin* SimpleSampler::getOrCreateEffectsPlugin (te::AudioTrack& track, int instrumentIndex)
{
    // Check if track already has the effects plugin
    for (auto* plugin : track.pluginList)
    {
        if (auto* fx = dynamic_cast<InstrumentEffectsPlugin*> (plugin))
        {
            fx->setSamplerSource (this);
            fx->setInstrumentIndex (instrumentIndex);
            return fx;
        }
    }

    // Create new InstrumentEffectsPlugin
    if (auto fx = dynamic_cast<InstrumentEffectsPlugin*> (
            track.edit.getPluginCache().createNewPlugin (InstrumentEffectsPlugin::xmlTypeName, {}).get()))
    {
        // Insert after the SamplerPlugin (position 1)
        int insertPos = 1;
        track.pluginList.insertPlugin (*fx, insertPos, nullptr);
        fx->setSamplerSource (this);
        fx->setInstrumentIndex (instrumentIndex);
        return fx;
    }

    return nullptr;
}

void SimpleSampler::setupPluginChain (te::AudioTrack& track, int instrumentIndex)
{
    getOrCreateSampler (track);
    getOrCreateEffectsPlugin (track, instrumentIndex);
}

//==============================================================================
// Load sample
//==============================================================================

juce::String SimpleSampler::loadSample (te::AudioTrack& track, const juce::File& sampleFile, int instrumentIndex)
{
    if (! sampleFile.existsAsFile())
        return "File not found: " + sampleFile.getFullPathName();

    auto* sampler = getOrCreateSampler (track);
    if (sampler == nullptr)
        return "Failed to create sampler plugin";

    while (sampler->getNumSounds() > 0)
        sampler->removeSound (0);

    auto error = sampler->addSound (sampleFile.getFullPathName(),
                                    sampleFile.getFileNameWithoutExtension(),
                                    0.0, 0.0, 0.0f);

    if (error.isEmpty() && sampler->getNumSounds() > 0)
    {
        sampler->setSoundParams (sampler->getNumSounds() - 1, 60, 0, 127);
        sampler->setSoundOpenEnded (sampler->getNumSounds() - 1, true);
        loadedSamples[instrumentIndex] = sampleFile;

        if (instrumentParams.find (instrumentIndex) == instrumentParams.end())
            instrumentParams[instrumentIndex] = InstrumentParams{};

        // Set up effects plugin chain
        setupPluginChain (track, instrumentIndex);
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

//==============================================================================
// Helper: Read sample region
//==============================================================================

SimpleSampler::SampleData SimpleSampler::readSampleRegion (const juce::File& file, double startNorm,
                                                            double endNorm, bool reverse)
{
    SampleData result;

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
        return result;

    auto totalSamples = static_cast<int> (reader->lengthInSamples);
    result.numChannels = static_cast<int> (reader->numChannels);
    result.sampleRate = reader->sampleRate;

    int startSample = static_cast<int> (startNorm * totalSamples);
    int endSample   = static_cast<int> (endNorm * totalSamples);
    startSample = juce::jlimit (0, totalSamples - 1, startSample);
    endSample   = juce::jlimit (startSample + 1, totalSamples, endSample);
    int length = endSample - startSample;

    if (length <= 0)
        return result;

    result.buffer.setSize (result.numChannels, length);
    reader->read (&result.buffer, 0, length, static_cast<juce::int64> (startSample), true, true);

    if (reverse)
        result.buffer.reverse (0, length);

    return result;
}

//==============================================================================
// Helper: Write temp WAV and load
//==============================================================================

juce::File SimpleSampler::writeTempWav (int instrumentIndex, const juce::String& suffix,
                                         const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("TrackerAdjust");
    tempDir.createDirectory();

    auto tempFile = tempDir.getChildFile ("processed_" + juce::String (instrumentIndex) + suffix + ".wav");

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::OutputStream> outputStream = std::make_unique<juce::FileOutputStream> (tempFile);

    auto options = juce::AudioFormatWriterOptions{}
                       .withSampleRate (sampleRate)
                       .withNumChannels (buffer.getNumChannels())
                       .withBitsPerSample (16);

    auto writer = wavFormat.createWriterFor (outputStream, options);
    if (writer == nullptr)
        return {};

    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    writer.reset();

    return tempFile;
}

juce::String SimpleSampler::writeTempAndLoad (te::AudioTrack& track, int instrumentIndex,
                                               const juce::AudioBuffer<float>& buffer, double sampleRate,
                                               int rootNote, int lowNote, int highNote, bool openEnded)
{
    auto tempFile = writeTempWav (instrumentIndex, "", buffer, sampleRate);
    if (! tempFile.existsAsFile())
        return "Failed to create temp WAV file";

    auto originalFile = loadedSamples[instrumentIndex];
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
        sampler->setSoundParams (sampler->getNumSounds() - 1, rootNote, lowNote, highNote);
        sampler->setSoundOpenEnded (sampler->getNumSounds() - 1, openEnded);
    }

    return error;
}

//==============================================================================
// Apply params dispatcher
//==============================================================================

juce::String SimpleSampler::applyParams (te::AudioTrack& track, int instrumentIndex)
{
    auto fileIt = loadedSamples.find (instrumentIndex);
    if (fileIt == loadedSamples.end())
        return "No sample loaded for this instrument";

    auto params = getParams (instrumentIndex);
    auto originalFile = fileIt->second;

    if (! originalFile.existsAsFile())
        return "Original sample file not found";

    // Set up effects plugin chain (ensures it exists)
    setupPluginChain (track, instrumentIndex);

    // If params only affect real-time processing (filter, overdrive, etc.)
    // and sample position is default, just reload original
    bool needsSampleProcessing = (params.startPos != 0.0 || params.endPos != 1.0 || params.reversed
                                  || params.playMode != InstrumentParams::PlayMode::OneShot);

    if (! needsSampleProcessing)
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
            // Apply tune by offsetting root note
            int rootNote = 60 - params.tune;
            sampler->setSoundParams (sampler->getNumSounds() - 1, rootNote, 0, 127);
            sampler->setSoundOpenEnded (sampler->getNumSounds() - 1, true);
        }
        return error;
    }

    // Dispatch to mode-specific handler
    switch (params.playMode)
    {
        case InstrumentParams::PlayMode::OneShot:       return applyOneShotMode (track, instrumentIndex);
        case InstrumentParams::PlayMode::ForwardLoop:   return applyForwardLoopMode (track, instrumentIndex);
        case InstrumentParams::PlayMode::BackwardLoop:  return applyBackwardLoopMode (track, instrumentIndex);
        case InstrumentParams::PlayMode::PingpongLoop:  return applyPingpongLoopMode (track, instrumentIndex);
        case InstrumentParams::PlayMode::Slice:         return applySliceMode (track, instrumentIndex);
        case InstrumentParams::PlayMode::BeatSlice:     return applyBeatSliceMode (track, instrumentIndex);
        case InstrumentParams::PlayMode::Granular:      return applyGranularMode (track, instrumentIndex);
        default:                                        return applyOneShotMode (track, instrumentIndex);
    }
}

//==============================================================================
// Playback mode implementations
//==============================================================================

juce::String SimpleSampler::applyOneShotMode (te::AudioTrack& track, int instrumentIndex)
{
    auto params = getParams (instrumentIndex);
    auto originalFile = loadedSamples[instrumentIndex];

    auto data = readSampleRegion (originalFile, params.startPos, params.endPos, params.reversed);
    if (data.buffer.getNumSamples() <= 0)
        return "Invalid sample region";

    int rootNote = 60 - params.tune;
    return writeTempAndLoad (track, instrumentIndex, data.buffer, data.sampleRate, rootNote, 0, 127, true);
}

juce::String SimpleSampler::applyForwardLoopMode (te::AudioTrack& track, int instrumentIndex)
{
    auto params = getParams (instrumentIndex);
    auto originalFile = loadedSamples[instrumentIndex];

    // Read the full region
    auto data = readSampleRegion (originalFile, params.startPos, params.endPos, params.reversed);
    if (data.buffer.getNumSamples() <= 0)
        return "Invalid sample region";

    int totalSamples = data.buffer.getNumSamples();
    int loopStartSample = static_cast<int> (params.loopStart * totalSamples);
    int loopEndSample = static_cast<int> (params.loopEnd * totalSamples);
    loopStartSample = juce::jlimit (0, totalSamples - 1, loopStartSample);
    loopEndSample = juce::jlimit (loopStartSample + 1, totalSamples, loopEndSample);

    int loopLen = loopEndSample - loopStartSample;
    int numRepeats = 8; // Create enough repeats to fill a reasonable duration
    int outputLen = loopStartSample + loopLen * numRepeats;

    juce::AudioBuffer<float> looped (data.numChannels, outputLen);
    looped.clear();

    // Copy attack portion (before loop start)
    for (int ch = 0; ch < data.numChannels; ++ch)
        looped.copyFrom (ch, 0, data.buffer, ch, 0, loopStartSample);

    // Copy loop repeats
    for (int r = 0; r < numRepeats; ++r)
    {
        int destStart = loopStartSample + r * loopLen;
        for (int ch = 0; ch < data.numChannels; ++ch)
            looped.copyFrom (ch, destStart, data.buffer, ch, loopStartSample, loopLen);
    }

    int rootNote = 60 - params.tune;
    return writeTempAndLoad (track, instrumentIndex, looped, data.sampleRate, rootNote, 0, 127, true);
}

juce::String SimpleSampler::applyBackwardLoopMode (te::AudioTrack& track, int instrumentIndex)
{
    auto params = getParams (instrumentIndex);
    auto originalFile = loadedSamples[instrumentIndex];

    auto data = readSampleRegion (originalFile, params.startPos, params.endPos, false);
    if (data.buffer.getNumSamples() <= 0)
        return "Invalid sample region";

    int totalSamples = data.buffer.getNumSamples();
    int loopStartSample = static_cast<int> (params.loopStart * totalSamples);
    int loopEndSample = static_cast<int> (params.loopEnd * totalSamples);
    loopStartSample = juce::jlimit (0, totalSamples - 1, loopStartSample);
    loopEndSample = juce::jlimit (loopStartSample + 1, totalSamples, loopEndSample);
    int loopLen = loopEndSample - loopStartSample;

    // Create reversed loop region
    juce::AudioBuffer<float> revLoop (data.numChannels, loopLen);
    for (int ch = 0; ch < data.numChannels; ++ch)
        revLoop.copyFrom (ch, 0, data.buffer, ch, loopStartSample, loopLen);
    revLoop.reverse (0, loopLen);

    int numRepeats = 8;
    int outputLen = loopStartSample + loopLen * numRepeats;
    juce::AudioBuffer<float> looped (data.numChannels, outputLen);
    looped.clear();

    // Copy attack portion
    for (int ch = 0; ch < data.numChannels; ++ch)
        looped.copyFrom (ch, 0, data.buffer, ch, 0, loopStartSample);

    // Copy reversed loop repeats
    for (int r = 0; r < numRepeats; ++r)
    {
        int destStart = loopStartSample + r * loopLen;
        for (int ch = 0; ch < data.numChannels; ++ch)
            looped.copyFrom (ch, destStart, revLoop, ch, 0, loopLen);
    }

    int rootNote = 60 - params.tune;
    return writeTempAndLoad (track, instrumentIndex, looped, data.sampleRate, rootNote, 0, 127, true);
}

juce::String SimpleSampler::applyPingpongLoopMode (te::AudioTrack& track, int instrumentIndex)
{
    auto params = getParams (instrumentIndex);
    auto originalFile = loadedSamples[instrumentIndex];

    auto data = readSampleRegion (originalFile, params.startPos, params.endPos, false);
    if (data.buffer.getNumSamples() <= 0)
        return "Invalid sample region";

    int totalSamples = data.buffer.getNumSamples();
    int loopStartSample = static_cast<int> (params.loopStart * totalSamples);
    int loopEndSample = static_cast<int> (params.loopEnd * totalSamples);
    loopStartSample = juce::jlimit (0, totalSamples - 1, loopStartSample);
    loopEndSample = juce::jlimit (loopStartSample + 1, totalSamples, loopEndSample);
    int loopLen = loopEndSample - loopStartSample;

    // Forward and reverse loop regions
    juce::AudioBuffer<float> fwdLoop (data.numChannels, loopLen);
    juce::AudioBuffer<float> revLoop (data.numChannels, loopLen);
    for (int ch = 0; ch < data.numChannels; ++ch)
    {
        fwdLoop.copyFrom (ch, 0, data.buffer, ch, loopStartSample, loopLen);
        revLoop.copyFrom (ch, 0, data.buffer, ch, loopStartSample, loopLen);
    }
    revLoop.reverse (0, loopLen);

    int numPingpongs = 4; // 4 forward + 4 reverse = 8 total iterations
    int outputLen = loopStartSample + loopLen * numPingpongs * 2;
    juce::AudioBuffer<float> looped (data.numChannels, outputLen);
    looped.clear();

    for (int ch = 0; ch < data.numChannels; ++ch)
        looped.copyFrom (ch, 0, data.buffer, ch, 0, loopStartSample);

    for (int r = 0; r < numPingpongs; ++r)
    {
        int destFwd = loopStartSample + r * loopLen * 2;
        int destRev = destFwd + loopLen;
        for (int ch = 0; ch < data.numChannels; ++ch)
        {
            looped.copyFrom (ch, destFwd, fwdLoop, ch, 0, loopLen);
            looped.copyFrom (ch, destRev, revLoop, ch, 0, loopLen);
        }
    }

    int rootNote = 60 - params.tune;
    return writeTempAndLoad (track, instrumentIndex, looped, data.sampleRate, rootNote, 0, 127, true);
}

juce::String SimpleSampler::applySliceMode (te::AudioTrack& track, int instrumentIndex)
{
    auto params = getParams (instrumentIndex);
    auto originalFile = loadedSamples[instrumentIndex];

    if (params.slicePoints.empty())
        return applyOneShotMode (track, instrumentIndex);

    auto* sampler = getOrCreateSampler (track);
    if (sampler == nullptr)
        return "No sampler plugin";

    while (sampler->getNumSounds() > 0)
        sampler->removeSound (0);

    // Build slice boundaries: [startPos, slice0, slice1, ..., endPos]
    std::vector<double> boundaries;
    boundaries.push_back (params.startPos);
    for (auto sp : params.slicePoints)
    {
        double mapped = params.startPos + sp * (params.endPos - params.startPos);
        boundaries.push_back (mapped);
    }
    boundaries.push_back (params.endPos);

    int numSlices = static_cast<int> (boundaries.size()) - 1;
    int baseNote = 60; // C4

    for (int s = 0; s < numSlices; ++s)
    {
        auto data = readSampleRegion (originalFile, boundaries[static_cast<size_t> (s)],
                                       boundaries[static_cast<size_t> (s + 1)], false);
        if (data.buffer.getNumSamples() <= 0)
            continue;

        auto tempFile = writeTempWav (instrumentIndex, "_slice" + juce::String (s),
                                       data.buffer, data.sampleRate);
        if (! tempFile.existsAsFile())
            continue;

        auto error = sampler->addSound (tempFile.getFullPathName(),
                                        "Slice " + juce::String (s),
                                        0.0, 0.0, 0.0f);
        if (error.isEmpty() && sampler->getNumSounds() > 0)
        {
            int note = baseNote + s;
            sampler->setSoundParams (sampler->getNumSounds() - 1, note, note, note);
            sampler->setSoundOpenEnded (sampler->getNumSounds() - 1, true);
        }
    }

    return {};
}

juce::String SimpleSampler::applyBeatSliceMode (te::AudioTrack& track, int instrumentIndex)
{
    auto params = getParams (instrumentIndex);
    auto originalFile = loadedSamples[instrumentIndex];

    // Auto-slice: divide the sample into equal slices based on slice points count
    // If no slice points defined, create 16 equal slices
    int numSlices = params.slicePoints.empty() ? 16 : static_cast<int> (params.slicePoints.size()) + 1;

    auto* sampler = getOrCreateSampler (track);
    if (sampler == nullptr)
        return "No sampler plugin";

    while (sampler->getNumSounds() > 0)
        sampler->removeSound (0);

    double regionLen = params.endPos - params.startPos;
    int baseNote = 60;

    for (int s = 0; s < numSlices; ++s)
    {
        double sliceStart = params.startPos + (static_cast<double> (s) / numSlices) * regionLen;
        double sliceEnd = params.startPos + (static_cast<double> (s + 1) / numSlices) * regionLen;

        auto data = readSampleRegion (originalFile, sliceStart, sliceEnd, false);
        if (data.buffer.getNumSamples() <= 0)
            continue;

        auto tempFile = writeTempWav (instrumentIndex, "_beatslice" + juce::String (s),
                                       data.buffer, data.sampleRate);
        if (! tempFile.existsAsFile())
            continue;

        auto error = sampler->addSound (tempFile.getFullPathName(),
                                        "BSlice " + juce::String (s),
                                        0.0, 0.0, 0.0f);
        if (error.isEmpty() && sampler->getNumSounds() > 0)
        {
            int note = baseNote + s;
            sampler->setSoundParams (sampler->getNumSounds() - 1, note, note, note);
            sampler->setSoundOpenEnded (sampler->getNumSounds() - 1, true);
        }
    }

    return {};
}

juce::String SimpleSampler::applyGranularMode (te::AudioTrack& track, int instrumentIndex)
{
    auto params = getParams (instrumentIndex);
    auto originalFile = loadedSamples[instrumentIndex];

    auto fullData = readSampleRegion (originalFile, params.startPos, params.endPos, false);
    if (fullData.buffer.getNumSamples() <= 0)
        return "Invalid sample region";

    int grainLengthSamples = static_cast<int> (params.granularLength * 0.001 * fullData.sampleRate);
    grainLengthSamples = juce::jmax (64, grainLengthSamples);

    int grainCenter = static_cast<int> (params.granularPosition * fullData.buffer.getNumSamples());
    int grainStart = juce::jmax (0, grainCenter - grainLengthSamples / 2);
    int grainEnd = juce::jmin (fullData.buffer.getNumSamples(), grainStart + grainLengthSamples);
    int actualLen = grainEnd - grainStart;

    if (actualLen <= 0)
        return "Invalid grain region";

    // Extract grain
    juce::AudioBuffer<float> grain (fullData.numChannels, actualLen);
    for (int ch = 0; ch < fullData.numChannels; ++ch)
        grain.copyFrom (ch, 0, fullData.buffer, ch, grainStart, actualLen);

    // Apply grain envelope
    for (int ch = 0; ch < fullData.numChannels; ++ch)
    {
        auto* data = grain.getWritePointer (ch);
        for (int i = 0; i < actualLen; ++i)
        {
            float t = static_cast<float> (i) / static_cast<float> (actualLen);
            float env = 1.0f;

            switch (params.granularShape)
            {
                case InstrumentParams::GranShape::Square:
                    env = 1.0f;
                    break;
                case InstrumentParams::GranShape::Triangle:
                    env = (t < 0.5f) ? (t * 2.0f) : (2.0f - t * 2.0f);
                    break;
                case InstrumentParams::GranShape::Gauss:
                {
                    float x = (t - 0.5f) * 4.0f; // -2 to +2
                    env = std::exp (-x * x);
                    break;
                }
            }

            data[i] *= env;
        }
    }

    // Create looped grain based on loop direction
    int numRepeats = 32;
    juce::AudioBuffer<float> looped (fullData.numChannels, actualLen * numRepeats);
    looped.clear();

    for (int r = 0; r < numRepeats; ++r)
    {
        bool doReverse = false;
        switch (params.granularLoop)
        {
            case InstrumentParams::GranLoop::Forward:
                doReverse = false;
                break;
            case InstrumentParams::GranLoop::Reverse:
                doReverse = true;
                break;
            case InstrumentParams::GranLoop::Pingpong:
                doReverse = (r % 2 == 1);
                break;
        }

        int destStart = r * actualLen;
        for (int ch = 0; ch < fullData.numChannels; ++ch)
        {
            if (doReverse)
            {
                for (int s = 0; s < actualLen; ++s)
                    looped.setSample (ch, destStart + s, grain.getSample (ch, actualLen - 1 - s));
            }
            else
            {
                looped.copyFrom (ch, destStart, grain, ch, 0, actualLen);
            }
        }
    }

    int rootNote = 60 - params.tune;
    return writeTempAndLoad (track, instrumentIndex, looped, fullData.sampleRate, rootNote, 0, 127, true);
}

//==============================================================================
// Preview
//==============================================================================

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
