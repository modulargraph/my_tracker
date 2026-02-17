#include "MetronomePlugin.h"

const char* MetronomePlugin::xmlTypeName = "Metronome";

MetronomePlugin::MetronomePlugin (te::PluginCreationInfo info)
    : te::Plugin (info)
{
}

MetronomePlugin::~MetronomePlugin()
{
}

void MetronomePlugin::initialise (const te::PluginInitialisationInfo& info)
{
    outputSampleRate = info.sampleRate;
}

void MetronomePlugin::deinitialise()
{
    clickSamplesRemaining = 0;
    lastBeatPosition = -1.0;
}

void MetronomePlugin::setEnabled (bool enabled)
{
    metronomeEnabled.store (enabled);
    if (! enabled)
    {
        clickSamplesRemaining = 0;
        lastBeatPosition = -1.0;
    }
}

bool MetronomePlugin::isEnabled() const
{
    return metronomeEnabled.load();
}

void MetronomePlugin::setVolume (float gainLinear)
{
    volume.store (juce::jlimit (0.0f, 1.0f, gainLinear));
}

float MetronomePlugin::getVolume() const
{
    return volume.load();
}

void MetronomePlugin::setAccentEnabled (bool accent)
{
    accentEnabled.store (accent);
}

bool MetronomePlugin::isAccentEnabled() const
{
    return accentEnabled.load();
}

void MetronomePlugin::applyToBuffer (const te::PluginRenderContext& rc)
{
    if (rc.destBuffer == nullptr)
        return;

    auto& buffer = *rc.destBuffer;
    int numSamples = rc.bufferNumSamples;

    // Only produce clicks when enabled and transport is playing
    if (! metronomeEnabled.load())
    {
        lastBeatPosition = -1.0;
        clickSamplesRemaining = 0;
        return;
    }

    auto& transport = edit.getTransport();
    if (! transport.isPlaying())
    {
        lastBeatPosition = -1.0;
        clickSamplesRemaining = 0;
        return;
    }

    // Get the current beat position from the transport
    auto currentPos = transport.getPosition();
    double currentBeat = edit.tempoSequence.toBeats (currentPos).inBeats();

    // Get beats per bar from the time signature (default 4/4)
    int beatsPerBar = 4;
    auto& timeSigs = edit.tempoSequence.getTimeSigs();
    if (timeSigs.size() > 0)
        beatsPerBar = timeSigs[0]->numerator;

    // Check if we crossed a beat boundary since last call
    if (lastBeatPosition >= 0.0)
    {
        int lastWholeBeat = static_cast<int> (std::floor (lastBeatPosition));
        int currentWholeBeat = static_cast<int> (std::floor (currentBeat));

        if (currentWholeBeat > lastWholeBeat && currentBeat >= 0.0)
        {
            // New beat detected - determine if it's a downbeat
            bool isDownbeat = (currentWholeBeat % beatsPerBar == 0) && accentEnabled.load();

            if (isDownbeat)
            {
                clickFrequency = 1200.0f;
                clickGain = 1.0f;
                clickSamplesRemaining = static_cast<int> (outputSampleRate * 0.015); // 15ms
            }
            else
            {
                clickFrequency = 800.0f;
                clickGain = 0.7f;
                clickSamplesRemaining = static_cast<int> (outputSampleRate * 0.010); // 10ms
            }

            clickPhase = 0.0f;
        }
    }
    else if (currentBeat >= 0.0)
    {
        // First call after starting playback - trigger a click immediately
        int currentWholeBeat = static_cast<int> (std::floor (currentBeat));
        bool isDownbeat = (currentWholeBeat % beatsPerBar == 0) && accentEnabled.load();

        if (isDownbeat)
        {
            clickFrequency = 1200.0f;
            clickGain = 1.0f;
            clickSamplesRemaining = static_cast<int> (outputSampleRate * 0.015);
        }
        else
        {
            clickFrequency = 800.0f;
            clickGain = 0.7f;
            clickSamplesRemaining = static_cast<int> (outputSampleRate * 0.010);
        }

        clickPhase = 0.0f;
    }

    lastBeatPosition = currentBeat;

    // Render click audio if active
    if (clickSamplesRemaining > 0)
    {
        float vol = volume.load();
        float phaseIncrement = clickFrequency / static_cast<float> (outputSampleRate);
        int totalClickLength = (clickFrequency > 1000.0f)
                                   ? static_cast<int> (outputSampleRate * 0.015)
                                   : static_cast<int> (outputSampleRate * 0.010);

        int samplesToRender = juce::jmin (numSamples, clickSamplesRemaining);

        for (int i = 0; i < samplesToRender; ++i)
        {
            // Sine wave
            float sample = std::sin (clickPhase * juce::MathConstants<float>::twoPi);

            // Exponential decay envelope
            int sampleIndex = totalClickLength - clickSamplesRemaining + i;
            float t = static_cast<float> (sampleIndex) / static_cast<float> (totalClickLength);
            float envelope = std::exp (-6.0f * t); // rapid exponential decay

            sample *= envelope * clickGain * vol;

            // Add to both channels
            int bufferSample = rc.bufferStartSample + i;
            for (int ch = 0; ch < buffer.getNumChannels() && ch < 2; ++ch)
                buffer.addSample (ch, bufferSample, sample);

            clickPhase += phaseIncrement;
            if (clickPhase >= 1.0f)
                clickPhase -= 1.0f;
        }

        clickSamplesRemaining -= samplesToRender;
    }
}
