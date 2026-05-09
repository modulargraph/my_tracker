#include "TrackerSamplerPlugin.h"
#include "SimpleSampler.h"
#include "InstrumentRouting.h"
#include "FxParamTransport.h"
#include "LoopRegion.h"
#include "SamplePlaybackLayout.h"

const char* TrackerSamplerPlugin::xmlTypeName = "TrackerSampler";

namespace
{
constexpr int kCcSamplerHardCut = 86;
constexpr int kCcSamplerRetrigger = 42;
constexpr double kEnvelopeReleaseTailTimeConstants = 7.0;
}

TrackerSamplerPlugin::TrackerSamplerPlugin (te::PluginCreationInfo info)
    : te::Plugin (info)
{
    clearPendingParamHighBits();
    channelInstruments.fill (-1);
    channelBankMsbs.fill (0);
    for (auto& note : pendingPreviewNotes)
        note.store (-1, std::memory_order_relaxed);
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
    for (auto& v : voices)
        v.reset();
    for (auto& v : fadeOutVoices)
        v.reset();
    for (auto& v : previewVoices)
        v.reset();
    for (auto& v : previewFadeOutVoices)
        v.reset();
    channelInstruments.fill (-1);
    channelBankMsbs.fill (0);
    pendingSampleOffset = -1;
    clearPendingParamHighBits();
    directionOverride = -1;
    sliceOverride = -1;
    clearRetriggerState();
}

void TrackerSamplerPlugin::clearPendingParamHighBits()
{
    pendingParamHighBits.fill (FxParamTransport::kNoPendingParamHighBit);
    legacyPendingParamHighBit = FxParamTransport::kNoPendingParamHighBit;
}

void TrackerSamplerPlugin::setSampleBank (std::shared_ptr<const SampleBank> bank)
{
    const juce::SpinLock::ScopedLockType lock (bankLock);
    sharedBank = std::move (bank);
}

void TrackerSamplerPlugin::playNote (int note, float vel)
{
    playNotes ({ note }, vel);
}

void TrackerSamplerPlugin::playNotes (const std::vector<int>& notes, float vel)
{
    const int count = juce::jmin (static_cast<int> (notes.size()), kMaxPreviewVoices);
    if (count <= 0)
        return;

    previewVelocity.store (juce::jlimit (0.0f, 1.0f, vel), std::memory_order_relaxed);

    for (int i = 0; i < count; ++i)
        pendingPreviewNotes[static_cast<size_t> (i)].store (juce::jlimit (0, 127, notes[static_cast<size_t> (i)]),
                                                            std::memory_order_relaxed);

    pendingPreviewNoteCount.store (count, std::memory_order_release);
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
    // Apply FX pitch offset (slides, arpeggio, vibrato, portamento)
    float fxPitch = pitchOffset.load (std::memory_order_relaxed);
    if (std::abs (fxPitch) > 0.001f)
        ratio *= std::pow (2.0, static_cast<double> (fxPitch) / 12.0);
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
                                         std::shared_ptr<const SampleBank> bank,
                                         const InstrumentParams& params)
{
    if (bank == nullptr || bank->totalSamples <= 0)
    {
        v.reset();
        return;
    }

    v.reset();
    v.bank = std::move (bank);
    v.params = params;
    v.state = Voice::State::Playing;
    v.midiNote = note;
    v.velocity = vel;
    v.playingForward = true;
    v.inLoopPhase = false;

    const auto& bankRef = *v.bank;
    const auto& paramsRef = v.params;

    double totalSmp = static_cast<double> (bankRef.totalSamples);
    double regionStart = SamplePlaybackLayout::getRegionStartNorm (paramsRef) * totalSmp;
    double regionEnd = SamplePlaybackLayout::getRegionEndNorm (paramsRef) * totalSmp;

    auto playMode = paramsRef.playMode;

    // --- Slice / BeatSlice ---
    if (playMode == InstrumentParams::PlayMode::Slice
        || playMode == InstrumentParams::PlayMode::BeatSlice)
    {
        if (playMode == InstrumentParams::PlayMode::Slice && ! paramsRef.slicePoints.empty())
        {
            auto boundaries = SamplePlaybackLayout::getSliceBoundariesNorm (paramsRef);

            int numSlices = static_cast<int> (boundaries.size()) - 1;
            int sliceIndex = sliceOverride >= 0 ? sliceOverride : paramsRef.selectedSlice;
            sliceIndex = juce::jlimit (0, numSlices - 1, sliceIndex);

            v.sliceStart = boundaries[static_cast<size_t> (sliceIndex)] * totalSmp;
            v.sliceEnd = boundaries[static_cast<size_t> (sliceIndex + 1)] * totalSmp;
        }
        else if (playMode == InstrumentParams::PlayMode::Slice && paramsRef.slicePoints.empty())
        {
            // No slice points: play whole region as one-shot
            if (paramsRef.reversed)
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
            auto boundaries = SamplePlaybackLayout::getBeatSliceBoundariesNorm (paramsRef);

            int numSlices = static_cast<int> (boundaries.size()) - 1;
            int sliceIndex = note - 60;
            sliceIndex = juce::jlimit (0, numSlices - 1, juce::jmax (0, sliceIndex));

            v.sliceStart = boundaries[static_cast<size_t> (sliceIndex)] * totalSmp;
            v.sliceEnd = boundaries[static_cast<size_t> (sliceIndex + 1)] * totalSmp;
        }

        const bool defaultForward = playMode == InstrumentParams::PlayMode::Slice
                                        ? ! paramsRef.reversed
                                        : true;
        const bool useForward = directionOverride >= 0 ? directionOverride == 1 : defaultForward;

        if (! useForward)
        {
            v.playbackPos = juce::jmax (v.sliceStart, v.sliceEnd - 1.0);
            v.playingForward = false;
        }
        else
        {
            v.playbackPos = v.sliceStart;
            v.playingForward = true;
        }
        return;
    }

    // --- Granular ---
    if (playMode == InstrumentParams::PlayMode::Granular)
    {
        prepareGranularGrain (v, bankRef, paramsRef);
        v.grainPos = 0;
        const bool defaultForward = (paramsRef.granularLoop != InstrumentParams::GranLoop::Reverse);
        const bool useForward = directionOverride >= 0 ? directionOverride == 1 : defaultForward;
        v.playbackPos = useForward ? v.grainStart : juce::jmax (v.grainStart, v.grainEnd - 1.0);
        v.playingForward = useForward;
        return;
    }

    // --- Standard modes (OneShot, ForwardLoop, BackwardLoop, PingpongLoop) ---
    const bool defaultForward = (playMode == InstrumentParams::PlayMode::BackwardLoop)
                                    ? false
                                    : ! paramsRef.reversed;
    const bool useForward = directionOverride >= 0 ? directionOverride == 1 : defaultForward;

    if (! useForward)
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

void TrackerSamplerPlugin::startFadeOut (Voice& source, Voice& fadeTarget)
{
    if (source.state != Voice::State::Playing)
        return;

    fadeTarget = source;
    fadeTarget.state = Voice::State::FadingOut;
    fadeTarget.fadeOutRemaining = Voice::kFadeOutSamples;
    fadeTarget.releaseTailActive = false;
    fadeTarget.releaseTailRemaining = 0;
    fadeTarget.releaseStopFadeRemaining = 0;
    source.releaseTailActive = false;
    source.releaseTailRemaining = 0;
    source.releaseStopFadeRemaining = 0;
    source.state = Voice::State::Idle;
}

bool TrackerSamplerPlugin::hasAudibleVolumeEnvelopeRelease (const InstrumentParams& params)
{
    const auto& mod = params.modulations[static_cast<size_t> (InstrumentParams::ModDest::Volume)];
    return mod.type == InstrumentParams::Modulation::Type::Envelope
        && mod.amount > 0
        && mod.releaseS > 0.001;
}

int TrackerSamplerPlugin::getReleaseTailSamples (const InstrumentParams& params) const
{
    const auto& mod = params.modulations[static_cast<size_t> (InstrumentParams::ModDest::Volume)];
    const double releaseSeconds = juce::jmax (0.001, mod.releaseS) * kEnvelopeReleaseTailTimeConstants;
    const double sampleRate = juce::jmax (1.0, outputSampleRate);
    return juce::jmax (Voice::kFadeOutSamples,
                       static_cast<int> (std::ceil (releaseSeconds * sampleRate)));
}

void TrackerSamplerPlugin::releaseVoice (Voice& source, Voice& fadeTarget)
{
    if (source.state != Voice::State::Playing)
        return;

    if (! hasAudibleVolumeEnvelopeRelease (source.params))
    {
        startFadeOut (source, fadeTarget);
        return;
    }

    source.releaseTailActive = true;
    source.releaseTailRemaining = juce::jmax (source.releaseTailRemaining,
                                              getReleaseTailSamples (source.params));
    source.releaseStopFadeRemaining = 0;
}

float TrackerSamplerPlugin::getReleaseTailGain (Voice& v)
{
    if (v.releaseStopFadeRemaining > 0)
    {
        const float gain = static_cast<float> (v.releaseStopFadeRemaining)
                         / static_cast<float> (Voice::kFadeOutSamples);
        --v.releaseStopFadeRemaining;
        if (v.releaseStopFadeRemaining <= 0)
            v.state = Voice::State::Idle;
        return gain;
    }

    if (! v.releaseTailActive)
        return 1.0f;

    if (v.releaseTailRemaining > 0)
        --v.releaseTailRemaining;

    if (v.releaseTailRemaining <= 0)
    {
        v.releaseTailActive = false;
        v.releaseStopFadeRemaining = Voice::kFadeOutSamples;
    }

    return 1.0f;
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
    double regionStart = SamplePlaybackLayout::getRegionStartNorm (params) * totalSmp;
    double regionEnd = SamplePlaybackLayout::getRegionEndNorm (params) * totalSmp;
    double advance = v.playingForward ? pitchRatio : -pitchRatio;
    int numCh = buffer.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        if (v.state != Voice::State::Playing) break;
        const float releaseGain = getReleaseTailGain (v);

        for (int ch = 0; ch < numCh; ++ch)
            buffer.addSample (ch, startSample + i,
                              interpolateSample (bank, ch, v.playbackPos) * v.velocity * releaseGain);

        v.playbackPos += advance;

        if (! v.playingForward)
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
    const auto loop = LoopRegion::fromParams (params, static_cast<double> (bank.totalSamples));

    int numCh = buffer.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        if (v.state != Voice::State::Playing) break;
        const float releaseGain = getReleaseTailGain (v);

        for (int ch = 0; ch < numCh; ++ch)
            buffer.addSample (ch, startSample + i,
                              interpolateSample (bank, ch, v.playbackPos) * v.velocity * releaseGain);

        bool advancedInAttack = false;
        if (! v.inLoopPhase)
        {
            // Forward attack before loop start.
            if (v.playingForward)
            {
                v.playbackPos += pitchRatio;
                advancedInAttack = true;
                if (v.playbackPos >= loop.loopStart)
                {
                    v.inLoopPhase = true;
                    v.playbackPos = loop.wrapPosition (v.playbackPos);
                }
            }
            else
            {
                // Reverse command while in pre-loop attack: enter loop phase immediately.
                v.inLoopPhase = true;
                if (v.playbackPos < loop.loopStart)
                    v.playbackPos = loop.loopStart;
            }
        }

        if (v.inLoopPhase && ! advancedInAttack)
        {
            v.playbackPos += v.playingForward ? pitchRatio : -pitchRatio;
            v.playbackPos = loop.wrapPosition (v.playbackPos);
        }
    }
}

void TrackerSamplerPlugin::renderBackwardLoop (Voice& v, juce::AudioBuffer<float>& buffer,
                                                int startSample, int numSamples,
                                                const SampleBank& bank, const InstrumentParams& params)
{
    double pitchRatio = getPitchRatio (v.midiNote, bank, params);
    const auto loop = LoopRegion::fromParams (params, static_cast<double> (bank.totalSamples));

    int numCh = buffer.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        if (v.state != Voice::State::Playing) break;
        const float releaseGain = getReleaseTailGain (v);

        for (int ch = 0; ch < numCh; ++ch)
            buffer.addSample (ch, startSample + i,
                              interpolateSample (bank, ch, v.playbackPos) * v.velocity * releaseGain);

        if (! v.inLoopPhase)
        {
            if (v.playingForward)
            {
                v.playbackPos += pitchRatio;
                if (v.playbackPos >= loop.loopEnd)
                {
                    v.inLoopPhase = true;
                    v.playbackPos = loop.wrapPosition (v.playbackPos);
                }
            }
            else
            {
                v.playbackPos -= pitchRatio;
                if (v.playbackPos < loop.loopEnd)
                {
                    v.inLoopPhase = true;
                    v.playbackPos = loop.wrapPosition (v.playbackPos);
                }
            }
        }
        else
        {
            v.playbackPos += v.playingForward ? pitchRatio : -pitchRatio;
            v.playbackPos = loop.wrapPosition (v.playbackPos);
        }
    }
}

void TrackerSamplerPlugin::renderPingpongLoop (Voice& v, juce::AudioBuffer<float>& buffer,
                                                int startSample, int numSamples,
                                                const SampleBank& bank, const InstrumentParams& params)
{
    double pitchRatio = getPitchRatio (v.midiNote, bank, params);
    const auto loop = LoopRegion::fromParams (params, static_cast<double> (bank.totalSamples));

    int numCh = buffer.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        if (v.state != Voice::State::Playing) break;
        const float releaseGain = getReleaseTailGain (v);

        for (int ch = 0; ch < numCh; ++ch)
            buffer.addSample (ch, startSample + i,
                              interpolateSample (bank, ch, v.playbackPos) * v.velocity * releaseGain);

        if (! v.inLoopPhase)
        {
            // Attack: play forward to loop end (first pass through loop region)
            v.playbackPos += pitchRatio;
            if (v.playbackPos >= loop.loopEnd)
            {
                v.inLoopPhase = true;
                v.playingForward = false;
                v.playbackPos = 2.0 * loop.loopEnd - v.playbackPos;
            }
        }
        else
        {
            if (v.playingForward)
                v.playbackPos += pitchRatio;
            else
                v.playbackPos -= pitchRatio;

            if (v.playbackPos >= loop.loopEnd)
            {
                v.playbackPos = 2.0 * loop.loopEnd - v.playbackPos;
                v.playingForward = false;
            }
            else if (v.playbackPos < loop.loopStart)
            {
                v.playbackPos = 2.0 * loop.loopStart - v.playbackPos;
                v.playingForward = true;
            }
        }
    }
}

void TrackerSamplerPlugin::renderSlice (Voice& v, juce::AudioBuffer<float>& buffer,
                                         int startSample, int numSamples,
                                         const SampleBank& bank, const InstrumentParams& params)
{
    double pitchRatio = 0.0;
    if (params.playMode == InstrumentParams::PlayMode::Slice)
    {
        // Slice mode plays the selected slice melodically; Beat Slice uses
        // the note number as the slice index while still accepting FX pitch.
        pitchRatio = getPitchRatio (v.midiNote, bank, params);
    }
    else
    {
        pitchRatio = bank.sampleRate / outputSampleRate;
        pitchRatio *= std::pow (2.0, params.tune / 12.0);
        pitchRatio *= std::pow (2.0, params.finetune / 1200.0);
        float fxPitch = pitchOffset.load (std::memory_order_relaxed);
        if (std::abs (fxPitch) > 0.001f)
            pitchRatio *= std::pow (2.0, static_cast<double> (fxPitch) / 12.0);
    }

    int numCh = buffer.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        if (v.state != Voice::State::Playing) break;
        const float releaseGain = getReleaseTailGain (v);

        for (int ch = 0; ch < numCh; ++ch)
            buffer.addSample (ch, startSample + i,
                              interpolateSample (bank, ch, v.playbackPos) * v.velocity * releaseGain);

        v.playbackPos += v.playingForward ? pitchRatio : -pitchRatio;

        if (v.playingForward)
        {
            if (v.playbackPos >= v.sliceEnd)
                v.state = Voice::State::Idle;
        }
        else if (v.playbackPos < v.sliceStart)
        {
            v.state = Voice::State::Idle;
        }
    }
}

void TrackerSamplerPlugin::prepareGranularGrain (Voice& v, const SampleBank& bank,
                                                  const InstrumentParams& params)
{
    const double totalSmp = static_cast<double> (bank.totalSamples);
    const double regionStart = SamplePlaybackLayout::getRegionStartNorm (params) * totalSmp;
    const double regionEnd = SamplePlaybackLayout::getRegionEndNorm (params) * totalSmp;
    if (regionEnd <= regionStart)
    {
        v.grainStart = regionStart;
        v.grainEnd = regionStart;
        v.grainLength = 1;
        return;
    }

    const double regionLen = regionEnd - regionStart;

    const double pitchRatio = juce::jmax (1.0e-6, std::abs (getPitchRatio (v.midiNote, bank, params)));
    const double pitchOffsetSemitones = static_cast<double> (
        pitchOffset.load (std::memory_order_relaxed));
    const double requestedRenderLength = SamplePlaybackLayout::getGranularRenderLengthSamples (
        params, v.midiNote, outputSampleRate, pitchOffsetSemitones);

    double grainLenSamples = requestedRenderLength * pitchRatio;
    grainLenSamples = juce::jlimit (1.0, regionLen, juce::jmax (64.0, grainLenSamples));

    const double positionOffset = static_cast<double> (
        granularPositionOffset.load (std::memory_order_relaxed));
    double grainCenter = SamplePlaybackLayout::getGranularCenterNorm (params, positionOffset) * totalSmp;

    double grainStart = grainCenter - grainLenSamples / 2.0;
    double grainEnd = grainStart + grainLenSamples;

    if (grainStart < regionStart)
    {
        grainEnd += regionStart - grainStart;
        grainStart = regionStart;
    }
    if (grainEnd > regionEnd)
    {
        grainStart -= grainEnd - regionEnd;
        grainEnd = regionEnd;
    }

    v.grainStart = juce::jlimit (regionStart, regionEnd - 1.0, grainStart);
    v.grainEnd = juce::jlimit (v.grainStart + 1.0, regionEnd, grainEnd);
    v.grainLength = juce::jmax (1, juce::roundToInt ((v.grainEnd - v.grainStart) / pitchRatio));
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
        const float releaseGain = getReleaseTailGain (v);

        float env = getGranularEnvelope (params, v.grainPos, v.grainLength);

        for (int ch = 0; ch < numCh; ++ch)
            buffer.addSample (ch, startSample + i,
                              interpolateSample (bank, ch, v.playbackPos) * v.velocity * env * releaseGain);

        if (v.playingForward)
            v.playbackPos += pitchRatio;
        else
            v.playbackPos -= pitchRatio;

        v.grainPos++;

        if (v.grainPos >= v.grainLength)
        {
            v.grainPos = 0;
            prepareGranularGrain (v, bank, params);

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

void TrackerSamplerPlugin::applyPositionCommandToVoice (Voice& v, int positionByte)
{
    if (v.state != Voice::State::Playing || v.bank == nullptr || v.bank->totalSamples <= 0)
        return;

    const auto& bank = *v.bank;
    const auto& params = v.params;

    double totalSmp = static_cast<double> (bank.totalSamples);
    double regionStart = SamplePlaybackLayout::getRegionStartNorm (params) * totalSmp;
    double regionEnd = SamplePlaybackLayout::getRegionEndNorm (params) * totalSmp;

    if ((params.playMode == InstrumentParams::PlayMode::Slice
         || params.playMode == InstrumentParams::PlayMode::BeatSlice)
        && v.sliceEnd > v.sliceStart)
    {
        regionStart = v.sliceStart;
        regionEnd = v.sliceEnd;
    }
    else if (params.playMode == InstrumentParams::PlayMode::Granular && v.grainEnd > v.grainStart)
    {
        regionStart = v.grainStart;
        regionEnd = v.grainEnd;
    }

    if (regionEnd <= regionStart)
        return;

    const double frac = static_cast<double> (juce::jlimit (0, 255, positionByte)) / 255.0;
    const double regionLen = juce::jmax (1.0, regionEnd - regionStart - 1.0);
    v.playbackPos = regionStart + frac * regionLen;
}

void TrackerSamplerPlugin::applySliceCommandToVoice (Voice& v, int sliceByte)
{
    if (v.state != Voice::State::Playing || v.bank == nullptr || v.bank->totalSamples <= 0)
        return;

    const auto& params = v.params;
    if (params.playMode != InstrumentParams::PlayMode::Slice || params.slicePoints.empty())
        return;

    const auto boundaries = SamplePlaybackLayout::getSliceBoundariesNorm (params);
    const int numSlices = static_cast<int> (boundaries.size()) - 1;
    if (numSlices <= 0)
        return;

    const int sliceIndex = juce::jlimit (0, numSlices - 1, sliceByte);
    const double totalSmp = static_cast<double> (v.bank->totalSamples);
    v.sliceStart = boundaries[static_cast<size_t> (sliceIndex)] * totalSmp;
    v.sliceEnd = boundaries[static_cast<size_t> (sliceIndex + 1)] * totalSmp;

    if (v.sliceEnd <= v.sliceStart)
        return;

    v.playbackPos = v.playingForward ? v.sliceStart
                                      : juce::jmax (v.sliceStart, v.sliceEnd - 1.0);
}

void TrackerSamplerPlugin::clearRetriggerState()
{
    retriggerState.clear();
}

void TrackerSamplerPlugin::requestRetriggerCapture (int denominator)
{
    const auto roll = SamplePlaybackLayout::decodeRollFx (denominator);
    if (roll.divider <= 0)
    {
        clearRetriggerState();
        return;
    }

    retriggerState.clear();
    retriggerState.active = true;
    retriggerState.capturePending = true;
    retriggerState.stepDenominator = roll.divider;
    retriggerState.rollType = roll.type;
    retriggerState.randomSeed = 0x9e3779b9u
        ^ (static_cast<uint32_t> (denominator) * 2654435761u);
}

void TrackerSamplerPlugin::updateRetriggerIntervalSamples (double bpm)
{
    if (! retriggerState.active || retriggerState.stepDenominator <= 0)
        return;

    const double previousInterval = retriggerState.intervalSamples;
    retriggerState.intervalSamples = SamplePlaybackLayout::getRetriggerIntervalSamples (
        retriggerState.stepDenominator, outputSampleRate, bpm, rowsPerBeat);

    if (retriggerState.intervalSamples <= 0.0)
        return;

    if (previousInterval <= 0.0 || retriggerState.samplesUntilNext <= 0.0)
    {
        retriggerState.samplesUntilNext = retriggerState.intervalSamples;
        return;
    }

    const double progress = juce::jlimit (0.0, 1.0, retriggerState.samplesUntilNext / previousInterval);
    retriggerState.samplesUntilNext = juce::jlimit (1.0, retriggerState.intervalSamples,
                                                    progress * retriggerState.intervalSamples);
}

bool TrackerSamplerPlugin::captureRetriggerAnchors()
{
    bool captured = false;
    for (size_t i = 0; i < voices.size(); ++i)
    {
        if (voices[i].state != Voice::State::Playing || voices[i].bank == nullptr)
        {
            retriggerState.snapshots[i] = {};
            continue;
        }

        retriggerState.snapshots[i].capture (voices[i]);
        captured = true;
    }

    if (captured)
    {
        retriggerState.capturePending = false;
        retriggerState.repeatIndex = 0;
        retriggerState.samplesUntilNext = juce::jmax (1.0, retriggerState.intervalSamples);
    }

    return captured;
}

bool TrackerSamplerPlugin::hasRetriggerAnchors() const
{
    for (const auto& snapshot : retriggerState.snapshots)
        if (snapshot.valid)
            return true;

    return false;
}

void TrackerSamplerPlugin::applyRetrigger()
{
    ++retriggerState.repeatIndex;

    int noteOffset = 0;
    float velocityScale = 1.0f;
    const int denominator = juce::jmax (1, retriggerState.stepDenominator);
    const float progress = juce::jlimit (0.0f, 1.0f,
        static_cast<float> (retriggerState.repeatIndex) / static_cast<float> (denominator));

    switch (retriggerState.rollType)
    {
        case SamplePlaybackLayout::RollType::Regular:
            break;
        case SamplePlaybackLayout::RollType::VolumeDown:
            velocityScale = 1.0f - progress;
            break;
        case SamplePlaybackLayout::RollType::VolumeUp:
            velocityScale = progress;
            break;
        case SamplePlaybackLayout::RollType::NoteDown:
            noteOffset = -retriggerState.repeatIndex;
            break;
        case SamplePlaybackLayout::RollType::NoteUp:
            noteOffset = retriggerState.repeatIndex;
            break;
        case SamplePlaybackLayout::RollType::NoteRandom:
        {
            retriggerState.randomSeed = retriggerState.randomSeed * 1664525u + 1013904223u;
            const int span = denominator * 2 + 1;
            noteOffset = static_cast<int> (retriggerState.randomSeed % static_cast<uint32_t> (span)) - denominator;
            break;
        }
    }

    for (size_t i = 0; i < voices.size(); ++i)
        retriggerState.snapshots[i].restore (voices[i], noteOffset, velocityScale);
}

//==============================================================================
// Voice rendering dispatcher
//==============================================================================

void TrackerSamplerPlugin::renderVoice (Voice& v, juce::AudioBuffer<float>& buffer,
                                         int startSample, int numSamples)
{
    if (v.state != Voice::State::Playing || v.bank == nullptr || v.bank->totalSamples <= 0)
        return;

    const auto& bank = *v.bank;
    const auto& params = v.params;

    auto mode = params.playMode;

    // Slice with no slice points → OneShot fallback
    if (mode == InstrumentParams::PlayMode::Slice && params.slicePoints.empty())
        mode = InstrumentParams::PlayMode::OneShot;

    // BeatSlice uses slice renderer.
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

void TrackerSamplerPlugin::renderPlaybackVoices (juce::AudioBuffer<float>& buffer,
                                                  int startSample, int numSamples)
{
    for (auto& v : voices)
        renderVoice (v, buffer, startSample, numSamples);
    for (auto& previewVoice : previewVoices)
        renderVoice (previewVoice, buffer, startSample, numSamples);
}

void TrackerSamplerPlugin::renderPlaybackVoicesWithRetrigger (juce::AudioBuffer<float>& buffer,
                                                               int startSample, int numSamples)
{
    const bool canRetrigger = retriggerState.active
                           && ! retriggerState.capturePending
                           && retriggerState.intervalSamples >= 1.0
                           && hasRetriggerAnchors();

    if (! canRetrigger)
    {
        renderPlaybackVoices (buffer, startSample, numSamples);
        return;
    }

    int rendered = 0;
    while (rendered < numSamples)
    {
        while (retriggerState.samplesUntilNext <= 0.0 && retriggerState.active)
        {
            applyRetrigger();
            retriggerState.samplesUntilNext += retriggerState.intervalSamples;
        }

        int segmentSamples = numSamples - rendered;
        if (retriggerState.active && retriggerState.samplesUntilNext > 0.0)
        {
            segmentSamples = juce::jmin (segmentSamples,
                                         juce::jmax (1, static_cast<int> (std::ceil (retriggerState.samplesUntilNext))));
        }

        renderPlaybackVoices (buffer, startSample + rendered, segmentSamples);
        rendered += segmentSamples;

        retriggerState.samplesUntilNext -= static_cast<double> (segmentSamples);
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
    const double blockBpm = edit.tempoSequence.getTempos()[0]->getBpm();
    updateRetriggerIntervalSamples (blockBpm);

    // Get current selected bank (thread-safe shared_ptr copy).
    // Active voices keep their own bank snapshots, so missing this lock doesn't
    // break already-playing notes.
    std::shared_ptr<const SampleBank> currentBank;
    {
        const juce::SpinLock::ScopedTryLockType lock (bankLock);
        if (lock.isLocked())
            currentBank = sharedBank;
    }

    // Clear output region (synth, additive rendering)
    buffer.clear (startSample, numSamples);

    auto getInstrumentParams = [this] (int voiceInstrumentIndex)
    {
        if (samplerSource != nullptr && voiceInstrumentIndex >= 0)
            return samplerSource->getParams (voiceInstrumentIndex);
        return InstrumentParams {};
    };

    auto getChannelIndex = [] (const juce::MidiMessage& message)
    {
        return juce::jlimit (0, 15, message.getChannel() - 1);
    };

    auto getInstrumentForChannel = [&] (int channelIndex)
    {
        if (channelIndex >= 0 && channelIndex < static_cast<int> (channelInstruments.size())
            && channelInstruments[static_cast<size_t> (channelIndex)] >= 0)
            return channelInstruments[static_cast<size_t> (channelIndex)];

        return instrumentIndex;
    };

    auto getBankForInstrument = [&] (int voiceInstrumentIndex)
    {
        const juce::SpinLock::ScopedTryLockType lock (bankLock);
        if (lock.isLocked() && voiceInstrumentIndex >= 0)
        {
            auto it = preloadedBanks.find (voiceInstrumentIndex);
            if (it != preloadedBanks.end() && it->second != nullptr)
                return it->second;
        }

        return currentBank;
    };

    auto stopVoices = [&] (int channelIndex, int noteNumber, bool hardCut)
    {
        for (size_t i = 0; i < voices.size(); ++i)
        {
            auto& v = voices[i];
            if (v.state != Voice::State::Playing)
                continue;
            if (channelIndex >= 0 && v.midiChannel != channelIndex + 1)
                continue;
            if (noteNumber >= 0 && v.midiNote != noteNumber)
                continue;

            if (hardCut)
            {
                startFadeOut (v, fadeOutVoices[i]);
            }
            else
            {
                releaseVoice (v, fadeOutVoices[i]);
            }
        }
    };

    auto findVoiceForTrigger = [&]() -> Voice*
    {
        for (auto& v : voices)
            if (v.state == Voice::State::Idle)
                return &v;

        return &voices.front();
    };

    auto decodeControllerByte = [this] (int valueController, int controllerValue)
    {
        return FxParamTransport::consumeByteFromController (valueController, controllerValue,
                                                            pendingParamHighBits,
                                                            legacyPendingParamHighBit);
    };

    // --- Handle stop request before new note (avoids stopping a just-triggered note) ---
    if (previewStop.exchange (false))
    {
        for (size_t i = 0; i < previewVoices.size(); ++i)
            releaseVoice (previewVoices[i], previewFadeOutVoices[i]);
    }

    // --- Handle preview notes from message thread ---
    int previewCount = pendingPreviewNoteCount.exchange (0, std::memory_order_acquire);
    previewCount = juce::jlimit (0, kMaxPreviewVoices, previewCount);
    if (previewCount > 0)
    {
        float pVel = previewVelocity.load (std::memory_order_relaxed);

        for (size_t i = 0; i < previewVoices.size(); ++i)
            releaseVoice (previewVoices[i], previewFadeOutVoices[i]);

        if (currentBank != nullptr && currentBank->totalSamples > 0)
        {
            auto params = getInstrumentParams (instrumentIndex);
            for (int i = 0; i < previewCount; ++i)
            {
                const int note = pendingPreviewNotes[static_cast<size_t> (i)].load (std::memory_order_relaxed);
                if (note >= 0)
                    triggerNote (previewVoices[static_cast<size_t> (i)], note, pVel, currentBank, params);
            }
        }
    }

    // --- Process MIDI messages ---
    if (fc.bufferForMidiMessages != nullptr)
    {
        if (fc.bufferForMidiMessages->isAllNotesOff)
        {
            // OFF release path (same as noteOff)
            stopVoices (-1, -1, false);
            clearRetriggerState();
        }

        for (auto& m : *fc.bufferForMidiMessages)
        {
            if (m.isProgramChange())
            {
                const int channelIndex = getChannelIndex (m);
                // Switch to a preloaded bank for multi-instrument support
                int progNum = m.getProgramChangeNumber();
                const int bankMsb = channelBankMsbs[static_cast<size_t> (channelIndex)];
                const int instrument = InstrumentRouting::decodeInstrumentFromBankAndProgram (bankMsb, progNum);
                const juce::SpinLock::ScopedLockType lock (bankLock);
                auto it = preloadedBanks.find (instrument);
                if (it != preloadedBanks.end() && it->second != nullptr)
                {
                    channelInstruments[static_cast<size_t> (channelIndex)] = instrument;
                }
                else
                {
                    // Legacy fallback: older sessions that only used 7-bit program numbers.
                    auto legacyIt = preloadedBanks.find (progNum);
                    if (legacyIt != preloadedBanks.end() && legacyIt->second != nullptr)
                    {
                        channelInstruments[static_cast<size_t> (channelIndex)] = progNum;
                    }
                }
            }
            else if (m.isController())
            {
                if (m.isAllNotesOff())
                {
                    stopVoices (getChannelIndex (m), -1, false);
                    clearRetriggerState();
                }
                else if (m.isAllSoundOff())
                {
                    stopVoices (getChannelIndex (m), -1, true);
                    clearRetriggerState();
                }
                else if (m.getControllerNumber() == 0) // Bank Select MSB
                {
                    const int channelIndex = getChannelIndex (m);
                    const int msb = m.getControllerValue() & 0x7F;
                    channelBankMsbs[static_cast<size_t> (channelIndex)] = msb;
                    if (channelIndex == 0)
                        currentBankMsb = msb;
                }
                else if (m.getControllerNumber() == kCcSamplerHardCut)
                {
                    stopVoices (getChannelIndex (m), -1, true);
                }
                else if (auto valueController = FxParamTransport::getValueControllerForHighBitController (m.getControllerNumber());
                         valueController >= 0)
                {
                    pendingParamHighBits[static_cast<size_t> (valueController)] = m.getControllerValue() & 0x1;
                }
                else if (m.getControllerNumber() == FxParamTransport::kParamHighBitCc)
                {
                    legacyPendingParamHighBit = m.getControllerValue() & 0x1;
                }
                // B (direction) and P (position) modify independent voice state:
                // B sets active voice directions, P sets active voice positions via
                // applyPositionCommandToVoice() which computes an absolute position
                // (regionStart + frac * regionLen) without referencing direction.
                // This means slot order does not affect the final result when both
                // B and P appear in the same tracker step.  On a note-trigger row
                // both CCs arrive before the note-on, so triggerNote() sees the
                // directionOverride and pendingSampleOffset is applied afterwards.
                else if (m.getControllerNumber() == 37) // Bxx direction
                {
                    const int value = decodeControllerByte (m.getControllerNumber(), m.getControllerValue());
                    directionOverride = (value == 0) ? 0 : 1;
                    const int channelIndex = getChannelIndex (m);
                    for (auto& v : voices)
                        if (v.state == Voice::State::Playing
                            && (channelIndex == 0 || v.midiChannel == channelIndex + 1))
                            v.playingForward = (directionOverride == 1);
                }
                else if (m.getControllerNumber() == 38) // Pxx
                {
                    pendingSampleOffset = decodeControllerByte (m.getControllerNumber(), m.getControllerValue());
                    const int channelIndex = getChannelIndex (m);
                    for (auto& v : voices)
                        if (v.state == Voice::State::Playing
                            && (channelIndex == 0 || v.midiChannel == channelIndex + 1))
                            applyPositionCommandToVoice (v, pendingSampleOffset);
                }
                else if (m.getControllerNumber() == 39) // note-row reset
                {
                    directionOverride = -1;
                    pendingSampleOffset = -1;
                    sliceOverride = -1;
                    clearRetriggerState();
                    clearPendingParamHighBits();
                    pitchOffset.store (0.0f, std::memory_order_relaxed);
                    for (auto& v : voices)
                    {
                        if (v.state != Voice::State::Playing)
                            continue;

                        v.playingForward =
                            v.params.playMode == InstrumentParams::PlayMode::BackwardLoop
                                ? false
                                : ! v.params.reversed;
                    }
                }
                else if (m.getControllerNumber() == 31) // Txx tune
                {
                    const int value = decodeControllerByte (m.getControllerNumber(), m.getControllerValue());
                    pitchOffset.store (static_cast<float> (static_cast<int8_t> (value & 0xFF)),
                                       std::memory_order_relaxed);
                }
                else if (m.getControllerNumber() == 41) // Sxx slice select, 1-based in pattern data
                {
                    sliceOverride = juce::jmax (
                        0, decodeControllerByte (m.getControllerNumber(), m.getControllerValue()) - 1);
                    const int channelIndex = getChannelIndex (m);
                    for (auto& v : voices)
                        if (v.state == Voice::State::Playing
                            && (channelIndex == 0 || v.midiChannel == channelIndex + 1))
                            applySliceCommandToVoice (v, sliceOverride);
                }
                else if (m.getControllerNumber() == kCcSamplerRetrigger) // Rxx roll/retrigger
                {
                    const int value = decodeControllerByte (m.getControllerNumber(), m.getControllerValue());
                    requestRetriggerCapture (value);
                    updateRetriggerIntervalSamples (blockBpm);
                }
            }
            else if (m.isNoteOn())
            {
                const int channelIndex = getChannelIndex (m);
                const int voiceInstrument = getInstrumentForChannel (channelIndex);
                auto voiceBank = getBankForInstrument (voiceInstrument);

                if (voiceBank != nullptr && voiceBank->totalSamples > 0)
                {
                    auto params = getInstrumentParams (voiceInstrument);
                    auto* targetVoice = findVoiceForTrigger();
                    if (targetVoice->state == Voice::State::Playing)
                    {
                        const auto voiceIndex = static_cast<size_t> (targetVoice - voices.data());
                        startFadeOut (*targetVoice, fadeOutVoices[voiceIndex]);
                    }

                    triggerNote (*targetVoice, m.getNoteNumber(),
                                 m.getVelocity() / 127.0f, voiceBank, params);
                    targetVoice->midiChannel = channelIndex + 1;
                    targetVoice->instrumentIndex = voiceInstrument;

                    if (pendingSampleOffset >= 0)
                    {
                        applyPositionCommandToVoice (*targetVoice, pendingSampleOffset);
                    }
                }
            }
            else if (m.isNoteOff())
            {
                // OFF: let the sample source feed the envelope release if present.
                stopVoices (getChannelIndex (m), m.getNoteNumber(), false);
            }
            else if (m.isAllNotesOff())
            {
                // OFF: all voices on this channel enter release.
                stopVoices (getChannelIndex (m), -1, false);
                clearRetriggerState();
            }
            else if (m.isAllSoundOff())
            {
                // KILL: short anti-click fade, no envelope release tail.
                stopVoices (getChannelIndex (m), -1, true);
                clearRetriggerState();
            }
        }
    }

    updateRetriggerIntervalSamples (blockBpm);
    if (retriggerState.capturePending)
        captureRetriggerAnchors();

    auto renderFadeOut = [this, &buffer, startSample, numSamples] (Voice& fadingVoice)
    {
        if (fadingVoice.state != Voice::State::FadingOut || fadingVoice.fadeOutRemaining <= 0)
            return;

        int fadeSamples = juce::jmin (numSamples, fadingVoice.fadeOutRemaining);
        float startGain = static_cast<float> (fadingVoice.fadeOutRemaining)
                        / static_cast<float> (Voice::kFadeOutSamples);
        float endGain = static_cast<float> (fadingVoice.fadeOutRemaining - fadeSamples)
                      / static_cast<float> (Voice::kFadeOutSamples);

        // Render fade-out to scratch buffer, then apply gain ramp
        int scratchCh = scratchBuffer.getNumChannels();
        int scratchSmp = scratchBuffer.getNumSamples();
        if (scratchCh >= buffer.getNumChannels() && scratchSmp >= fadeSamples)
        {
            scratchBuffer.clear (0, fadeSamples);

            fadingVoice.state = Voice::State::Playing;
            renderVoice (fadingVoice, scratchBuffer, 0, fadeSamples);
            fadingVoice.state = Voice::State::FadingOut;

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

        fadingVoice.fadeOutRemaining -= fadeSamples;
        if (fadingVoice.fadeOutRemaining <= 0)
            fadingVoice.state = Voice::State::Idle;
    };

    // --- Render fade-out voices ---
    for (auto& fadingVoice : fadeOutVoices)
        renderFadeOut (fadingVoice);
    for (auto& fadingPreviewVoice : previewFadeOutVoices)
        renderFadeOut (fadingPreviewVoice);

    // --- Render main voices ---
    renderPlaybackVoicesWithRetrigger (buffer, startSample, numSamples);

    // Publish playback position for UI cursor
    for (auto& v : voices)
    {
        if (v.state == Voice::State::Playing && v.bank != nullptr && v.bank->totalSamples > 0)
        {
            playbackPosNorm.store (static_cast<float> (v.playbackPos / static_cast<double> (v.bank->totalSamples)),
                                   std::memory_order_relaxed);
            return;
        }
    }

    for (auto& previewVoice : previewVoices)
    {
        if (previewVoice.state == Voice::State::Playing && previewVoice.bank != nullptr && previewVoice.bank->totalSamples > 0)
        {
            playbackPosNorm.store (static_cast<float> (previewVoice.playbackPos / static_cast<double> (previewVoice.bank->totalSamples)),
                                   std::memory_order_relaxed);
            return;
        }
    }

    playbackPosNorm.store (-1.0f, std::memory_order_relaxed);
}
