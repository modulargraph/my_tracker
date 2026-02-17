#include "TrackerSamplerPlugin.h"
#include "SimpleSampler.h"

const char* TrackerSamplerPlugin::xmlTypeName = "TrackerSampler";

TrackerSamplerPlugin::TrackerSamplerPlugin (te::PluginCreationInfo info)
    : te::Plugin (info)
{
}

TrackerSamplerPlugin::~TrackerSamplerPlugin()
{
}

void TrackerSamplerPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    outputSampleRate = info.sampleRate;
    scratchBuffer.setSize (2, info.blockSizeSamples);
}

void TrackerSamplerPlugin::deinitialise()
{
    voice.reset();
    fadeOutVoice.reset();
}

void TrackerSamplerPlugin::setSampleBank (std::shared_ptr<const SampleBank> bank)
{
    const juce::SpinLock::ScopedLockType lock (bankLock);
    sharedBank = std::move (bank);
}

void TrackerSamplerPlugin::playNote (int note, float vel)
{
    previewVelocity.store (vel);
    previewNote.store (note);
}

void TrackerSamplerPlugin::stopAllNotes()
{
    previewStop.store (true);
}

//==============================================================================
// Pitch and interpolation
//==============================================================================

double TrackerSamplerPlugin::getPitchRatio (int midiNote, const SampleBank& bank,
                                             const InstrumentParams& params) const
{
    double ratio = bank.sampleRate / outputSampleRate;
    ratio *= std::pow (2.0, params.tune / 12.0);
    ratio *= std::pow (2.0, params.finetune / 1200.0);
    ratio *= std::pow (2.0, (midiNote - 60) / 12.0);
    return ratio;
}

float TrackerSamplerPlugin::interpolateSample (const SampleBank& bank, int channel, double pos) const
{
    if (bank.totalSamples <= 0) return 0.0f;

    int idx0 = static_cast<int> (pos);
    int idx1 = idx0 + 1;
    float frac = static_cast<float> (pos - idx0);

    int maxIdx = static_cast<int> (bank.totalSamples) - 1;
    idx0 = juce::jlimit (0, maxIdx, idx0);
    idx1 = juce::jlimit (0, maxIdx, idx1);

    int ch = juce::jmin (channel, bank.numChannels - 1);

    return bank.buffer.getSample (ch, idx0) * (1.0f - frac)
         + bank.buffer.getSample (ch, idx1) * frac;
}

float TrackerSamplerPlugin::getGranularEnvelope (const InstrumentParams& params, int pos, int length) const
{
    if (length <= 0) return 0.0f;
    float t = static_cast<float> (pos) / static_cast<float> (length);

    switch (params.granularShape)
    {
        case InstrumentParams::GranShape::Square:
            return 1.0f;
        case InstrumentParams::GranShape::Triangle:
            return (t < 0.5f) ? (t * 2.0f) : (2.0f - t * 2.0f);
        case InstrumentParams::GranShape::Gauss:
        {
            float x = (t - 0.5f) * 4.0f;
            return std::exp (-x * x);
        }
    }
    return 1.0f;
}

//==============================================================================
// Note triggering
//==============================================================================

void TrackerSamplerPlugin::triggerNote (Voice& v, int note, float vel,
                                         const SampleBank& bank, const InstrumentParams& params)
{
    v.reset();
    v.state = Voice::State::Playing;
    v.midiNote = note;
    v.velocity = vel;
    v.playingForward = true;
    v.inLoopPhase = false;

    double totalSmp = static_cast<double> (bank.totalSamples);
    double regionStart = params.startPos * totalSmp;
    double regionEnd = params.endPos * totalSmp;

    auto playMode = params.playMode;

    // --- Slice / BeatSlice ---
    if (playMode == InstrumentParams::PlayMode::Slice
        || playMode == InstrumentParams::PlayMode::BeatSlice)
    {
        int sliceIndex = note - 60;
        if (sliceIndex < 0) sliceIndex = 0;

        if (playMode == InstrumentParams::PlayMode::Slice && ! params.slicePoints.empty())
        {
            std::vector<double> boundaries;
            boundaries.push_back (params.startPos);
            for (auto sp : params.slicePoints)
                boundaries.push_back (params.startPos + sp * (params.endPos - params.startPos));
            boundaries.push_back (params.endPos);

            int numSlices = static_cast<int> (boundaries.size()) - 1;
            sliceIndex = juce::jlimit (0, numSlices - 1, sliceIndex);

            v.sliceStart = boundaries[static_cast<size_t> (sliceIndex)] * totalSmp;
            v.sliceEnd = boundaries[static_cast<size_t> (sliceIndex + 1)] * totalSmp;
        }
        else if (playMode == InstrumentParams::PlayMode::Slice && params.slicePoints.empty())
        {
            // No slice points: play whole region as one-shot
            if (params.reversed)
            {
                v.playbackPos = regionEnd - 1.0;
                v.playingForward = false;
            }
            else
            {
                v.playbackPos = regionStart;
            }
            return;
        }
        else
        {
            // BeatSlice: equal divisions
            int numSlices = params.slicePoints.empty()
                ? 16
                : static_cast<int> (params.slicePoints.size()) + 1;
            sliceIndex = juce::jlimit (0, numSlices - 1, sliceIndex);

            double regionLen = regionEnd - regionStart;
            v.sliceStart = regionStart + (static_cast<double> (sliceIndex) / numSlices) * regionLen;
            v.sliceEnd = regionStart + (static_cast<double> (sliceIndex + 1) / numSlices) * regionLen;
        }

        v.playbackPos = v.sliceStart;
        return;
    }

    // --- Granular ---
    if (playMode == InstrumentParams::PlayMode::Granular)
    {
        double regionLen = regionEnd - regionStart;
        int grainLenSamples = static_cast<int> (params.granularLength * 0.001 * bank.sampleRate);
        grainLenSamples = juce::jmax (64, grainLenSamples);

        double grainCenter = regionStart + params.granularPosition * regionLen;
        v.grainStart = juce::jmax (regionStart, grainCenter - grainLenSamples / 2.0);
        v.grainEnd = juce::jmin (regionEnd, v.grainStart + grainLenSamples);
        v.grainLength = static_cast<int> (v.grainEnd - v.grainStart);
        v.grainPos = 0;
        v.playbackPos = v.grainStart;
        v.playingForward = (params.granularLoop != InstrumentParams::GranLoop::Reverse);
        return;
    }

    // --- Standard modes (OneShot, ForwardLoop, BackwardLoop, PingpongLoop) ---
    if (params.reversed)
    {
        v.playbackPos = regionEnd - 1.0;
        v.playingForward = false;
    }
    else
    {
        v.playbackPos = regionStart;
        v.playingForward = true;
    }
}

//==============================================================================
// Per-mode render methods
//==============================================================================

void TrackerSamplerPlugin::renderOneShot (Voice& v, juce::AudioBuffer<float>& buffer,
                                           int startSample, int numSamples,
                                           const SampleBank& bank, const InstrumentParams& params)
{
    double pitchRatio = getPitchRatio (v.midiNote, bank, params);
    double totalSmp = static_cast<double> (bank.totalSamples);
    double regionStart = params.startPos * totalSmp;
    double regionEnd = params.endPos * totalSmp;
    double advance = params.reversed ? -pitchRatio : pitchRatio;
    int numCh = buffer.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        if (v.state != Voice::State::Playing) break;

        for (int ch = 0; ch < numCh; ++ch)
            buffer.addSample (ch, startSample + i,
                              interpolateSample (bank, ch, v.playbackPos) * v.velocity);

        v.playbackPos += advance;

        if (params.reversed)
        {
            if (v.playbackPos < regionStart)
                v.state = Voice::State::Idle;
        }
        else
        {
            if (v.playbackPos >= regionEnd)
                v.state = Voice::State::Idle;
        }
    }
}

void TrackerSamplerPlugin::renderForwardLoop (Voice& v, juce::AudioBuffer<float>& buffer,
                                               int startSample, int numSamples,
                                               const SampleBank& bank, const InstrumentParams& params)
{
    double pitchRatio = getPitchRatio (v.midiNote, bank, params);
    double totalSmp = static_cast<double> (bank.totalSamples);
    double regionStart = params.startPos * totalSmp;
    double regionEnd = params.endPos * totalSmp;
    double regionLen = regionEnd - regionStart;

    double loopStartPos = regionStart + params.loopStart * regionLen;
    double loopEndPos = regionStart + params.loopEnd * regionLen;
    if (loopEndPos <= loopStartPos) loopEndPos = loopStartPos + 1.0;
    double loopLen = loopEndPos - loopStartPos;

    int numCh = buffer.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        if (v.state != Voice::State::Playing) break;

        for (int ch = 0; ch < numCh; ++ch)
            buffer.addSample (ch, startSample + i,
                              interpolateSample (bank, ch, v.playbackPos) * v.velocity);

        v.playbackPos += pitchRatio;

        if (! v.inLoopPhase && v.playbackPos >= loopStartPos)
            v.inLoopPhase = true;

        if (v.inLoopPhase && v.playbackPos >= loopEndPos)
            v.playbackPos = loopStartPos + std::fmod (v.playbackPos - loopStartPos, loopLen);

        if (v.playbackPos >= regionEnd)
            v.playbackPos = loopStartPos;
    }
}

void TrackerSamplerPlugin::renderBackwardLoop (Voice& v, juce::AudioBuffer<float>& buffer,
                                                int startSample, int numSamples,
                                                const SampleBank& bank, const InstrumentParams& params)
{
    double pitchRatio = getPitchRatio (v.midiNote, bank, params);
    double totalSmp = static_cast<double> (bank.totalSamples);
    double regionStart = params.startPos * totalSmp;
    double regionEnd = params.endPos * totalSmp;
    double regionLen = regionEnd - regionStart;

    double loopStartPos = regionStart + params.loopStart * regionLen;
    double loopEndPos = regionStart + params.loopEnd * regionLen;
    if (loopEndPos <= loopStartPos) loopEndPos = loopStartPos + 1.0;

    int numCh = buffer.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        if (v.state != Voice::State::Playing) break;

        for (int ch = 0; ch < numCh; ++ch)
            buffer.addSample (ch, startSample + i,
                              interpolateSample (bank, ch, v.playbackPos) * v.velocity);

        if (! v.inLoopPhase)
        {
            // Attack: play forward to loop start
            v.playbackPos += pitchRatio;
            if (v.playbackPos >= loopStartPos)
            {
                v.inLoopPhase = true;
                v.playbackPos = loopEndPos - 1.0;
            }
        }
        else
        {
            // Loop: play backward, wrapping at loop boundaries
            v.playbackPos -= pitchRatio;
            if (v.playbackPos < loopStartPos)
                v.playbackPos = loopEndPos - 1.0;
        }
    }
}

void TrackerSamplerPlugin::renderPingpongLoop (Voice& v, juce::AudioBuffer<float>& buffer,
                                                int startSample, int numSamples,
                                                const SampleBank& bank, const InstrumentParams& params)
{
    double pitchRatio = getPitchRatio (v.midiNote, bank, params);
    double totalSmp = static_cast<double> (bank.totalSamples);
    double regionStart = params.startPos * totalSmp;
    double regionEnd = params.endPos * totalSmp;
    double regionLen = regionEnd - regionStart;

    double loopStartPos = regionStart + params.loopStart * regionLen;
    double loopEndPos = regionStart + params.loopEnd * regionLen;
    if (loopEndPos <= loopStartPos) loopEndPos = loopStartPos + 1.0;

    int numCh = buffer.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        if (v.state != Voice::State::Playing) break;

        for (int ch = 0; ch < numCh; ++ch)
            buffer.addSample (ch, startSample + i,
                              interpolateSample (bank, ch, v.playbackPos) * v.velocity);

        if (! v.inLoopPhase)
        {
            // Attack: play forward to loop end (first pass through loop region)
            v.playbackPos += pitchRatio;
            if (v.playbackPos >= loopEndPos)
            {
                v.inLoopPhase = true;
                v.playingForward = false;
                v.playbackPos = 2.0 * loopEndPos - v.playbackPos;
            }
        }
        else
        {
            if (v.playingForward)
                v.playbackPos += pitchRatio;
            else
                v.playbackPos -= pitchRatio;

            if (v.playbackPos >= loopEndPos)
            {
                v.playbackPos = 2.0 * loopEndPos - v.playbackPos;
                v.playingForward = false;
            }
            else if (v.playbackPos < loopStartPos)
            {
                v.playbackPos = 2.0 * loopStartPos - v.playbackPos;
                v.playingForward = true;
            }
        }
    }
}

void TrackerSamplerPlugin::renderSlice (Voice& v, juce::AudioBuffer<float>& buffer,
                                         int startSample, int numSamples,
                                         const SampleBank& bank, const InstrumentParams& params)
{
    // Slices play at original pitch (note selects slice, not pitch)
    double pitchRatio = bank.sampleRate / outputSampleRate;
    pitchRatio *= std::pow (2.0, params.tune / 12.0);
    pitchRatio *= std::pow (2.0, params.finetune / 1200.0);

    int numCh = buffer.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        if (v.state != Voice::State::Playing) break;

        for (int ch = 0; ch < numCh; ++ch)
            buffer.addSample (ch, startSample + i,
                              interpolateSample (bank, ch, v.playbackPos) * v.velocity);

        v.playbackPos += pitchRatio;

        if (v.playbackPos >= v.sliceEnd)
            v.state = Voice::State::Idle;
    }
}

void TrackerSamplerPlugin::renderGranular (Voice& v, juce::AudioBuffer<float>& buffer,
                                            int startSample, int numSamples,
                                            const SampleBank& bank, const InstrumentParams& params)
{
    double pitchRatio = getPitchRatio (v.midiNote, bank, params);
    int numCh = buffer.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        if (v.state != Voice::State::Playing) break;

        float env = getGranularEnvelope (params, v.grainPos, v.grainLength);

        for (int ch = 0; ch < numCh; ++ch)
            buffer.addSample (ch, startSample + i,
                              interpolateSample (bank, ch, v.playbackPos) * v.velocity * env);

        if (v.playingForward)
            v.playbackPos += pitchRatio;
        else
            v.playbackPos -= pitchRatio;

        v.grainPos++;

        if (v.grainPos >= v.grainLength)
        {
            v.grainPos = 0;

            switch (params.granularLoop)
            {
                case InstrumentParams::GranLoop::Forward:
                    v.playbackPos = v.grainStart;
                    v.playingForward = true;
                    break;
                case InstrumentParams::GranLoop::Reverse:
                    v.playbackPos = v.grainEnd - 1.0;
                    v.playingForward = false;
                    break;
                case InstrumentParams::GranLoop::Pingpong:
                    v.playingForward = ! v.playingForward;
                    if (v.playingForward)
                        v.playbackPos = v.grainStart;
                    else
                        v.playbackPos = v.grainEnd - 1.0;
                    break;
            }
        }
    }
}

//==============================================================================
// Voice rendering dispatcher
//==============================================================================

void TrackerSamplerPlugin::renderVoice (Voice& v, juce::AudioBuffer<float>& buffer,
                                         int startSample, int numSamples,
                                         const SampleBank& bank, const InstrumentParams& params)
{
    if (v.state != Voice::State::Playing) return;

    auto mode = params.playMode;

    // Slice with no slice points → OneShot fallback
    if (mode == InstrumentParams::PlayMode::Slice && params.slicePoints.empty())
        mode = InstrumentParams::PlayMode::OneShot;

    // BeatSlice uses slice renderer
    if (mode == InstrumentParams::PlayMode::BeatSlice)
        mode = InstrumentParams::PlayMode::Slice;

    switch (mode)
    {
        case InstrumentParams::PlayMode::OneShot:       renderOneShot (v, buffer, startSample, numSamples, bank, params); break;
        case InstrumentParams::PlayMode::ForwardLoop:   renderForwardLoop (v, buffer, startSample, numSamples, bank, params); break;
        case InstrumentParams::PlayMode::BackwardLoop:  renderBackwardLoop (v, buffer, startSample, numSamples, bank, params); break;
        case InstrumentParams::PlayMode::PingpongLoop:  renderPingpongLoop (v, buffer, startSample, numSamples, bank, params); break;
        case InstrumentParams::PlayMode::Slice:
        case InstrumentParams::PlayMode::BeatSlice:     renderSlice (v, buffer, startSample, numSamples, bank, params); break;
        case InstrumentParams::PlayMode::Granular:      renderGranular (v, buffer, startSample, numSamples, bank, params); break;
    }
}

//==============================================================================
// Main processing
//==============================================================================

void TrackerSamplerPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr) return;

    auto& buffer = *fc.destBuffer;
    int startSample = fc.bufferStartSample;
    int numSamples = fc.bufferNumSamples;

    // Get bank (thread-safe shared_ptr copy)
    std::shared_ptr<const SampleBank> bank;
    {
        const juce::SpinLock::ScopedTryLockType lock (bankLock);
        if (lock.isLocked())
            bank = sharedBank;
    }

    if (! bank || bank->totalSamples <= 0)
    {
        buffer.clear (startSample, numSamples);
        return;
    }

    // Get params snapshot
    InstrumentParams params;
    if (samplerSource != nullptr && instrumentIndex >= 0)
        params = samplerSource->getParams (instrumentIndex);

    // Clear output region (synth, additive rendering)
    buffer.clear (startSample, numSamples);

    // --- Handle preview notes from message thread ---
    int pNote = previewNote.exchange (-1);
    if (pNote >= 0)
    {
        float pVel = previewVelocity.load();

        if (voice.state == Voice::State::Playing)
        {
            fadeOutVoice = voice;
            fadeOutVoice.state = Voice::State::FadingOut;
            fadeOutVoice.fadeOutRemaining = Voice::kFadeOutSamples;
        }

        // Preview notes always play as OneShot (no looping)
        InstrumentParams previewParams = params;
        previewParams.playMode = InstrumentParams::PlayMode::OneShot;
        triggerNote (voice, pNote, pVel, *bank, previewParams);
        voiceTriggeredByPreview = true;
    }

    if (previewStop.exchange (false))
    {
        if (voice.state == Voice::State::Playing)
        {
            fadeOutVoice = voice;
            fadeOutVoice.state = Voice::State::FadingOut;
            fadeOutVoice.fadeOutRemaining = Voice::kFadeOutSamples;
            voice.state = Voice::State::Idle;
        }
    }

    // --- Process MIDI messages ---
    if (fc.bufferForMidiMessages != nullptr)
    {
        if (fc.bufferForMidiMessages->isAllNotesOff)
        {
            // Graceful fade (same as noteOff)
            if (voice.state == Voice::State::Playing)
            {
                fadeOutVoice = voice;
                fadeOutVoice.state = Voice::State::FadingOut;
                fadeOutVoice.fadeOutRemaining = Voice::kFadeOutSamples;
                voice.state = Voice::State::Idle;
            }
        }

        for (auto& m : *fc.bufferForMidiMessages)
        {
            if (m.isProgramChange())
            {
                // Switch to a preloaded bank for multi-instrument support
                int progNum = m.getProgramChangeNumber();
                auto it = preloadedBanks.find (progNum);
                if (it != preloadedBanks.end() && it->second != nullptr)
                {
                    const juce::SpinLock::ScopedLockType lock (bankLock);
                    sharedBank = it->second;
                    bank = sharedBank;
                    instrumentIndex = progNum;
                }
            }
            else if (m.isNoteOn())
            {
                if (voice.state == Voice::State::Playing)
                {
                    fadeOutVoice = voice;
                    fadeOutVoice.state = Voice::State::FadingOut;
                    fadeOutVoice.fadeOutRemaining = Voice::kFadeOutSamples;
                }

                // Re-read params for the current instrument (may have changed via program change)
                if (samplerSource != nullptr && instrumentIndex >= 0)
                    params = samplerSource->getParams (instrumentIndex);

                triggerNote (voice, m.getNoteNumber(),
                             m.getVelocity() / 127.0f, *bank, params);
                voiceTriggeredByPreview = false;
            }
            else if (m.isNoteOff())
            {
                // Graceful fade-out with crossfade
                if (voice.state == Voice::State::Playing)
                {
                    fadeOutVoice = voice;
                    fadeOutVoice.state = Voice::State::FadingOut;
                    fadeOutVoice.fadeOutRemaining = Voice::kFadeOutSamples;
                    voice.state = Voice::State::Idle;
                }
            }
            else if (m.isAllNotesOff())
            {
                // Graceful fade (OFF) — same as noteOff
                if (voice.state == Voice::State::Playing)
                {
                    fadeOutVoice = voice;
                    fadeOutVoice.state = Voice::State::FadingOut;
                    fadeOutVoice.fadeOutRemaining = Voice::kFadeOutSamples;
                    voice.state = Voice::State::Idle;
                }
            }
            else if (m.isAllSoundOff())
            {
                // Hard cut (KILL) — immediate silence
                voice.state = Voice::State::Idle;
                fadeOutVoice.state = Voice::State::Idle;
            }
        }
    }

    // --- Render fade-out voice ---
    if (fadeOutVoice.state == Voice::State::FadingOut && fadeOutVoice.fadeOutRemaining > 0)
    {
        int fadeSamples = juce::jmin (numSamples, fadeOutVoice.fadeOutRemaining);
        float startGain = static_cast<float> (fadeOutVoice.fadeOutRemaining)
                        / static_cast<float> (Voice::kFadeOutSamples);
        float endGain = static_cast<float> (fadeOutVoice.fadeOutRemaining - fadeSamples)
                      / static_cast<float> (Voice::kFadeOutSamples);

        // Render fade-out to scratch buffer, then apply gain ramp
        int scratchCh = scratchBuffer.getNumChannels();
        int scratchSmp = scratchBuffer.getNumSamples();
        if (scratchCh >= buffer.getNumChannels() && scratchSmp >= fadeSamples)
        {
            scratchBuffer.clear (0, fadeSamples);

            fadeOutVoice.state = Voice::State::Playing;
            renderVoice (fadeOutVoice, scratchBuffer, 0, fadeSamples, *bank, params);
            fadeOutVoice.state = Voice::State::FadingOut;

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                const float* src = scratchBuffer.getReadPointer (ch);
                float* dst = buffer.getWritePointer (ch, startSample);

                for (int i = 0; i < fadeSamples; ++i)
                {
                    float t = (fadeSamples > 1)
                        ? static_cast<float> (i) / static_cast<float> (fadeSamples - 1)
                        : 0.0f;
                    dst[i] += src[i] * (startGain + (endGain - startGain) * t);
                }
            }
        }

        fadeOutVoice.fadeOutRemaining -= fadeSamples;
        if (fadeOutVoice.fadeOutRemaining <= 0)
            fadeOutVoice.state = Voice::State::Idle;
    }

    // --- Render main voice ---
    if (voiceTriggeredByPreview)
    {
        InstrumentParams previewParams = params;
        previewParams.playMode = InstrumentParams::PlayMode::OneShot;
        renderVoice (voice, buffer, startSample, numSamples, *bank, previewParams);
    }
    else
    {
        renderVoice (voice, buffer, startSample, numSamples, *bank, params);
    }
}
