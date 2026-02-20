#include <cmath>
#include <iostream>
#include <map>
#include <vector>

#include <JuceHeader.h>

#include "Arrangement.h"
#include "ArrangementComponent.h"
#include "MixerState.h"
#include "PatternData.h"
#include "ProjectSerializer.h"
#include "SendBuffers.h"
#include "SendEffectsParams.h"
#include "TrackLayout.h"
#include "TrackerLookAndFeel.h"

namespace
{

bool floatsClose (float a, float b, float eps = 1.0e-6f)
{
    return std::abs (a - b) <= eps;
}

bool testPatternRoundTripNoExtraPattern()
{
    PatternData source;
    source.getPattern (0).name = "Intro";
    source.getPattern (0).resize (32);

    Cell introCell;
    introCell.note = 60;
    introCell.instrument = 3;
    introCell.volume = 96;
    source.getPattern (0).setCell (0, 0, introCell);

    source.addPattern (48);
    source.getPattern (1).name = "Verse";
    source.addPattern (16);
    source.getPattern (2).name = "Fill";

    Arrangement arrangement;
    arrangement.addEntry (0, 1);
    arrangement.addEntry (1, 2);

    TrackLayout trackLayout;
    MixerState mixerState;
    DelayParams delayParams;
    ReverbParams reverbParams;
    std::map<int, juce::File> loadedSamples;
    std::map<int, InstrumentParams> instrumentParams;

    const double bpm = 133.5;
    const int rpb = 6;

    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getNonexistentChildFile ("tracker_adjust_tests_project", ".tkadj", false);

    auto saveErr = ProjectSerializer::saveToFile (file,
                                                  source,
                                                  bpm,
                                                  rpb,
                                                  loadedSamples,
                                                  instrumentParams,
                                                  arrangement,
                                                  trackLayout,
                                                  mixerState,
                                                  delayParams,
                                                  reverbParams,
                                                  0,
                                                  {});
    if (saveErr.isNotEmpty())
    {
        std::cerr << "save failed: " << saveErr << "\n";
        return false;
    }

    PatternData loaded;
    double loadedBpm = 0.0;
    int loadedRpb = 0;
    std::map<int, juce::File> loadedSamplesOut;
    std::map<int, InstrumentParams> instrumentParamsOut;
    Arrangement arrangementOut;
    TrackLayout trackLayoutOut;
    MixerState mixerStateOut;
    DelayParams delayOut;
    ReverbParams reverbOut;

    auto loadErr = ProjectSerializer::loadFromFile (file,
                                                    loaded,
                                                    loadedBpm,
                                                    loadedRpb,
                                                    loadedSamplesOut,
                                                    instrumentParamsOut,
                                                    arrangementOut,
                                                    trackLayoutOut,
                                                    mixerStateOut,
                                                    delayOut,
                                                    reverbOut,
                                                    nullptr,
                                                    nullptr);

    file.deleteFile();

    if (loadErr.isNotEmpty())
    {
        std::cerr << "load failed: " << loadErr << "\n";
        return false;
    }

    if (loaded.getNumPatterns() != source.getNumPatterns())
    {
        std::cerr << "pattern count mismatch: expected " << source.getNumPatterns()
                  << " got " << loaded.getNumPatterns() << "\n";
        return false;
    }

    if (loaded.getPattern (0).name != "Intro"
        || loaded.getPattern (1).name != "Verse"
        || loaded.getPattern (2).name != "Fill")
    {
        std::cerr << "pattern names mismatch after load\n";
        return false;
    }

    if (loaded.getPattern (0).numRows != 32
        || loaded.getPattern (1).numRows != 48
        || loaded.getPattern (2).numRows != 16)
    {
        std::cerr << "pattern row counts mismatch after load\n";
        return false;
    }

    const auto loadedCell = loaded.getPattern (0).getCell (0, 0);
    if (loadedCell.note != 60 || loadedCell.instrument != 3 || loadedCell.volume != 96)
    {
        std::cerr << "pattern cell data mismatch after load\n";
        return false;
    }

    if (std::abs (loadedBpm - bpm) > 1.0e-6 || loadedRpb != rpb)
    {
        std::cerr << "transport metadata mismatch after load\n";
        return false;
    }

    return true;
}

bool testSinglePatternRoundTripStaysSingle()
{
    PatternData source;
    source.getPattern (0).name = "Single";

    Arrangement arrangement;
    TrackLayout trackLayout;
    MixerState mixerState;
    DelayParams delayParams;
    ReverbParams reverbParams;
    std::map<int, juce::File> loadedSamples;
    std::map<int, InstrumentParams> instrumentParams;

    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getNonexistentChildFile ("tracker_adjust_tests_single", ".tkadj", false);

    auto saveErr = ProjectSerializer::saveToFile (file,
                                                  source,
                                                  120.0,
                                                  4,
                                                  loadedSamples,
                                                  instrumentParams,
                                                  arrangement,
                                                  trackLayout,
                                                  mixerState,
                                                  delayParams,
                                                  reverbParams,
                                                  0,
                                                  {});
    if (saveErr.isNotEmpty())
    {
        std::cerr << "save failed: " << saveErr << "\n";
        return false;
    }

    PatternData loaded;
    double bpm = 0.0;
    int rpb = 0;
    std::map<int, juce::File> loadedSamplesOut;
    std::map<int, InstrumentParams> instrumentParamsOut;
    Arrangement arrangementOut;
    TrackLayout trackLayoutOut;
    MixerState mixerStateOut;
    DelayParams delayOut;
    ReverbParams reverbOut;

    auto loadErr = ProjectSerializer::loadFromFile (file,
                                                    loaded,
                                                    bpm,
                                                    rpb,
                                                    loadedSamplesOut,
                                                    instrumentParamsOut,
                                                    arrangementOut,
                                                    trackLayoutOut,
                                                    mixerStateOut,
                                                    delayOut,
                                                    reverbOut,
                                                    nullptr,
                                                    nullptr);

    file.deleteFile();

    if (loadErr.isNotEmpty())
    {
        std::cerr << "load failed: " << loadErr << "\n";
        return false;
    }

    if (loaded.getNumPatterns() != 1)
    {
        std::cerr << "expected single pattern after round-trip, got " << loaded.getNumPatterns() << "\n";
        return false;
    }

    if (loaded.getPattern (0).name != "Single")
    {
        std::cerr << "single-pattern name mismatch\n";
        return false;
    }

    return true;
}

bool testSendBuffersStartSampleAlignmentAndConsume()
{
    SendBuffers buffers;
    buffers.prepare (64, 2);

    juce::AudioBuffer<float> source (2, 64);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            source.setSample (ch, i, static_cast<float> (i + 1 + ch * 100));

    buffers.addToDelay (source, 8, 16, 0.5f);
    buffers.addToReverb (source, 8, 16, 0.25f);

    juce::AudioBuffer<float> delayOut;
    juce::AudioBuffer<float> reverbOut;
    buffers.consumeSlice (delayOut, reverbOut, 8, 16, 2);

    if (delayOut.getNumSamples() != 16 || reverbOut.getNumSamples() != 16)
    {
        std::cerr << "consumeSlice returned wrong slice size\n";
        return false;
    }

    for (int i = 0; i < 16; ++i)
    {
        float expectedLDelay = source.getSample (0, 8 + i) * 0.5f;
        float expectedRDelay = source.getSample (1, 8 + i) * 0.5f;
        float expectedLReverb = source.getSample (0, 8 + i) * 0.25f;
        float expectedRReverb = source.getSample (1, 8 + i) * 0.25f;

        if (! floatsClose (delayOut.getSample (0, i), expectedLDelay)
            || ! floatsClose (delayOut.getSample (1, i), expectedRDelay)
            || ! floatsClose (reverbOut.getSample (0, i), expectedLReverb)
            || ! floatsClose (reverbOut.getSample (1, i), expectedRReverb))
        {
            std::cerr << "send buffer slice mismatch at sample " << i << "\n";
            return false;
        }
    }

    juce::AudioBuffer<float> delayOut2;
    juce::AudioBuffer<float> reverbOut2;
    buffers.consumeSlice (delayOut2, reverbOut2, 8, 16, 2);

    for (int ch = 0; ch < 2; ++ch)
    {
        for (int i = 0; i < 16; ++i)
        {
            if (! floatsClose (delayOut2.getSample (ch, i), 0.0f)
                || ! floatsClose (reverbOut2.getSample (ch, i), 0.0f))
            {
                std::cerr << "consumeSlice did not clear consumed data\n";
                return false;
            }
        }
    }

    return true;
}

bool testArrangementDeleteNotifiesChangeCallback()
{
    TrackerLookAndFeel lnf;
    PatternData patternData;
    Arrangement arrangement;
    arrangement.addEntry (0, 1);

    ArrangementComponent component (arrangement, patternData, lnf);
    component.setSize (200, 200);
    component.setSelectedEntry (0);

    int changeCount = 0;
    component.onArrangementChanged = [&changeCount]
    {
        ++changeCount;
    };

    bool handled = component.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
    if (! handled)
    {
        std::cerr << "delete key not handled\n";
        return false;
    }

    if (arrangement.getNumEntries() != 0)
    {
        std::cerr << "arrangement entry not deleted\n";
        return false;
    }

    if (changeCount != 1)
    {
        std::cerr << "expected 1 arrangement change callback, got " << changeCount << "\n";
        return false;
    }

    return true;
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    struct TestCase
    {
        const char* name;
        bool (*fn)();
    };

    const std::vector<TestCase> tests = {
        { "PatternRoundTripNoExtraPattern", &testPatternRoundTripNoExtraPattern },
        { "SinglePatternRoundTripStaysSingle", &testSinglePatternRoundTripStaysSingle },
        { "SendBuffersStartSampleAlignmentAndConsume", &testSendBuffersStartSampleAlignmentAndConsume },
        { "ArrangementDeleteNotifiesChangeCallback", &testArrangementDeleteNotifiesChangeCallback },
    };

    int failures = 0;
    for (const auto& test : tests)
    {
        const bool ok = test.fn();
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << test.name << "\n";
        if (! ok)
            ++failures;
    }

    if (failures != 0)
        std::cout << failures << " test(s) failed\n";

    return failures == 0 ? 0 : 1;
}
