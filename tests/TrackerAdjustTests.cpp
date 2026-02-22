#include <array>
#include <cmath>
#include <iostream>
#include <map>
#include <vector>

#include <JuceHeader.h>

#include "Arrangement.h"
#include "ArrangementComponent.h"
#include "InstrumentRouting.h"
#include "FxParamTransport.h"
#include "MixerState.h"
#include "PatternData.h"
#include "ProjectSerializer.h"
#include "SamplePlaybackLayout.h"
#include "SendBuffers.h"
#include "SendEffectsParams.h"
#include "PluginAutomationComponent.h"
#include "PanMapping.h"
#include "TrackLayout.h"
#include "TrackerGrid.h"
#include "TrackerLookAndFeel.h"

namespace
{

bool floatsClose (float a, float b, float eps = 1.0e-6f)
{
    return std::abs (a - b) <= eps;
}

bool doublesClose (double a, double b, double eps = 1.0e-6)
{
    return std::abs (a - b) <= eps;
}

bool vectorsClose (const std::vector<double>& a, const std::vector<double>& b, double eps = 1.0e-6)
{
    if (a.size() != b.size())
        return false;

    for (size_t i = 0; i < a.size(); ++i)
    {
        if (! doublesClose (a[i], b[i], eps))
            return false;
    }

    return true;
}

juce::String runProjectRoundTrip (const juce::String& fileStem,
                                  const PatternData& source,
                                  double bpm,
                                  int rowsPerBeat,
                                  const std::map<int, juce::File>& loadedSamples,
                                  const std::map<int, InstrumentParams>& instrumentParams,
                                  const Arrangement& arrangement,
                                  const TrackLayout& trackLayout,
                                  const MixerState& mixerState,
                                  const DelayParams& delayParams,
                                  const ReverbParams& reverbParams,
                                  int followMode,
                                  const juce::String& browserDir,
                                  PatternData& loaded,
                                  double& loadedBpm,
                                  int& loadedRowsPerBeat,
                                  std::map<int, juce::File>& loadedSamplesOut,
                                  std::map<int, InstrumentParams>& instrumentParamsOut,
                                  Arrangement& arrangementOut,
                                  TrackLayout& trackLayoutOut,
                                  MixerState& mixerStateOut,
                                  DelayParams& delayOut,
                                  ReverbParams& reverbOut,
                                  int* followModeOut = nullptr,
                                  juce::String* browserDirOut = nullptr)
{
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getNonexistentChildFile (fileStem, ".tkadj", false);

    auto saveErr = ProjectSerializer::saveToFile (file,
                                                  source,
                                                  bpm,
                                                  rowsPerBeat,
                                                  loadedSamples,
                                                  instrumentParams,
                                                  arrangement,
                                                  trackLayout,
                                                  mixerState,
                                                  delayParams,
                                                  reverbParams,
                                                  followMode,
                                                  browserDir);
    if (saveErr.isNotEmpty())
    {
        file.deleteFile();
        return "save failed: " + saveErr;
    }

    auto loadErr = ProjectSerializer::loadFromFile (file,
                                                    loaded,
                                                    loadedBpm,
                                                    loadedRowsPerBeat,
                                                    loadedSamplesOut,
                                                    instrumentParamsOut,
                                                    arrangementOut,
                                                    trackLayoutOut,
                                                    mixerStateOut,
                                                    delayOut,
                                                    reverbOut,
                                                    followModeOut,
                                                    browserDirOut);
    file.deleteFile();

    if (loadErr.isNotEmpty())
        return "load failed: " + loadErr;

    return {};
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

bool testSendBuffersAutoResizeForLargeWrites()
{
    SendBuffers buffers;
    buffers.prepare (8, 2);

    juce::AudioBuffer<float> source (2, 24);
    for (int ch = 0; ch < source.getNumChannels(); ++ch)
        for (int i = 0; i < source.getNumSamples(); ++i)
            source.setSample (ch, i, static_cast<float> ((ch + 1) * 100 + i));

    // Write beyond initial prepared length; addTo* should resize and keep all samples.
    buffers.addToDelay (source, 4, 20, 1.0f);
    buffers.addToReverb (source, 4, 20, 0.5f);

    juce::AudioBuffer<float> delayOut;
    juce::AudioBuffer<float> reverbOut;
    buffers.consumeSlice (delayOut, reverbOut, 4, 20, 2);

    if (delayOut.getNumSamples() != 20 || reverbOut.getNumSamples() != 20)
    {
        std::cerr << "auto-resize send consume returned wrong slice size\n";
        return false;
    }

    for (int i = 0; i < 20; ++i)
    {
        const float expectedDelayL = source.getSample (0, 4 + i);
        const float expectedDelayR = source.getSample (1, 4 + i);
        const float expectedReverbL = source.getSample (0, 4 + i) * 0.5f;
        const float expectedReverbR = source.getSample (1, 4 + i) * 0.5f;

        if (! floatsClose (delayOut.getSample (0, i), expectedDelayL)
            || ! floatsClose (delayOut.getSample (1, i), expectedDelayR)
            || ! floatsClose (reverbOut.getSample (0, i), expectedReverbL)
            || ! floatsClose (reverbOut.getSample (1, i), expectedReverbR))
        {
            std::cerr << "auto-resize send buffer mismatch at sample " << i << "\n";
            return false;
        }
    }

    return true;
}

bool testPanMappingCenterAndExtremes()
{
    if (! floatsClose (PanMapping::cc10ToPan (0), -50.0f))
    {
        std::cerr << "CC10 pan at 0 should map to -50\n";
        return false;
    }

    if (! floatsClose (PanMapping::cc10ToPan (64), 0.0f))
    {
        std::cerr << "CC10 pan at 64 should map to exact center 0\n";
        return false;
    }

    if (! floatsClose (PanMapping::cc10ToPan (127), 50.0f))
    {
        std::cerr << "CC10 pan at 127 should map to +50\n";
        return false;
    }

    if (PanMapping::cc10ToPan (63) >= 0.0f || PanMapping::cc10ToPan (65) <= 0.0f)
    {
        std::cerr << "CC10 pan should be negative below 64 and positive above 64\n";
        return false;
    }

    return true;
}

bool testInstrumentRoutingRoundTripFullRange()
{
    for (int instrument = 0; instrument <= 255; ++instrument)
    {
        int bankMsb = InstrumentRouting::getBankMsbForInstrument (instrument);
        int program = InstrumentRouting::getProgramForInstrument (instrument);
        int decoded = InstrumentRouting::decodeInstrumentFromBankAndProgram (bankMsb, program);

        if (decoded != instrument)
        {
            std::cerr << "routing round-trip mismatch for instrument " << instrument << "\n";
            return false;
        }

        int expectedBank = instrument >= 128 ? 1 : 0;
        if (bankMsb != expectedBank)
        {
            std::cerr << "unexpected bank for instrument " << instrument << ": " << bankMsb << "\n";
            return false;
        }
    }

    return true;
}

bool testInstrumentRoutingClampsOutOfRange()
{
    if (InstrumentRouting::clampInstrumentIndex (-42) != 0)
    {
        std::cerr << "negative instrument should clamp to 0\n";
        return false;
    }

    if (InstrumentRouting::clampInstrumentIndex (999) != 255)
    {
        std::cerr << "large instrument should clamp to 255\n";
        return false;
    }

    if (InstrumentRouting::getBankMsbForInstrument (999) != 1
        || InstrumentRouting::getProgramForInstrument (999) != 127)
    {
        std::cerr << "bank/program split for out-of-range instrument is wrong\n";
        return false;
    }

    if (InstrumentRouting::decodeInstrumentFromBankAndProgram (-1, -1) != 0)
    {
        std::cerr << "negative bank/program should decode to 0\n";
        return false;
    }

    if (InstrumentRouting::decodeInstrumentFromBankAndProgram (127, 127) != 255)
    {
        std::cerr << "large bank/program should clamp decode to 255\n";
        return false;
    }

    if (InstrumentRouting::decodeInstrumentFromBankAndProgram (1, 5) != 133)
    {
        std::cerr << "bank/program decode mismatch for 0x85\n";
        return false;
    }

    return true;
}

bool testArrangementRemapAfterPatternRemoved()
{
    Arrangement arrangement;
    arrangement.addEntry (0, 1);
    arrangement.addEntry (1, 2);
    arrangement.addEntry (2, 1);
    arrangement.addEntry (5, 3);

    arrangement.remapAfterPatternRemoved (1, 3);

    const std::array<int, 4> expectedAfterFirst { 0, 1, 1, 2 };
    for (int i = 0; i < arrangement.getNumEntries(); ++i)
    {
        if (arrangement.getEntry (i).patternIndex != expectedAfterFirst[static_cast<size_t> (i)])
        {
            std::cerr << "unexpected remap result after first deletion at entry " << i << "\n";
            return false;
        }
    }

    arrangement.remapAfterPatternRemoved (0, 2);

    const std::array<int, 4> expectedAfterSecond { 0, 0, 0, 1 };
    for (int i = 0; i < arrangement.getNumEntries(); ++i)
    {
        if (arrangement.getEntry (i).patternIndex != expectedAfterSecond[static_cast<size_t> (i)])
        {
            std::cerr << "unexpected remap result after second deletion at entry " << i << "\n";
            return false;
        }
    }

    return true;
}

bool testArrangementRemapNoOpWhenPatternCountInvalid()
{
    Arrangement arrangement;
    arrangement.addEntry (3, 1);
    arrangement.addEntry (7, 1);

    arrangement.remapAfterPatternRemoved (0, 0);

    if (arrangement.getEntry (0).patternIndex != 3
        || arrangement.getEntry (1).patternIndex != 7)
    {
        std::cerr << "remap should not mutate arrangement when new pattern count is invalid\n";
        return false;
    }

    return true;
}

bool testProjectRoundTripKeepsHighInstrumentAndFxSlots()
{
    PatternData source;
    source.getPattern (0).name = "HighInst";
    source.getPattern (0).resize (32);

    Cell cell;
    cell.note = 72;
    cell.instrument = 255;
    cell.volume = 127;
    cell.getFxSlot (0).setSymbolicCommand ('D', 0xFF);
    cell.getFxSlot (1).setSymbolicCommand ('F', 0x1F);
    source.getPattern (0).setCell (0, 0, cell);

    Arrangement arrangement;
    TrackLayout trackLayout;
    MixerState mixerState;
    DelayParams delayParams;
    ReverbParams reverbParams;
    std::map<int, juce::File> loadedSamples;
    std::map<int, InstrumentParams> instrumentParams;

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

    auto err = runProjectRoundTrip ("tracker_adjust_tests_high_inst",
                                    source,
                                    150.0,
                                    16,
                                    loadedSamples,
                                    instrumentParams,
                                    arrangement,
                                    trackLayout,
                                    mixerState,
                                    delayParams,
                                    reverbParams,
                                    0,
                                    {},
                                    loaded,
                                    loadedBpm,
                                    loadedRpb,
                                    loadedSamplesOut,
                                    instrumentParamsOut,
                                    arrangementOut,
                                    trackLayoutOut,
                                    mixerStateOut,
                                    delayOut,
                                    reverbOut);
    if (err.isNotEmpty())
    {
        std::cerr << err << "\n";
        return false;
    }

    const auto loadedCell = loaded.getPattern (0).getCell (0, 0);
    if (loadedCell.instrument != 255 || loadedCell.note != 72 || loadedCell.volume != 127)
    {
        std::cerr << "high instrument cell was not preserved\n";
        return false;
    }

    if (loadedCell.getNumFxSlots() < 2)
    {
        std::cerr << "expected at least 2 FX slots after round-trip\n";
        return false;
    }

    if (loadedCell.getFxSlot (0).fxCommand != 'D' || loadedCell.getFxSlot (0).fxParam != 0xFF
        || loadedCell.getFxSlot (1).fxCommand != 'F' || loadedCell.getFxSlot (1).fxParam != 0x1F)
    {
        std::cerr << "FX slot data mismatch after round-trip\n";
        return false;
    }

    if (std::abs (loadedBpm - 150.0) > 1.0e-6 || loadedRpb != 16)
    {
        std::cerr << "transport settings mismatch for high-instrument round-trip\n";
        return false;
    }

    return true;
}

bool testProjectRoundTripKeepsFollowModeAndBrowserDir()
{
    PatternData source;
    Arrangement arrangement;
    TrackLayout trackLayout;
    MixerState mixerState;
    DelayParams delayParams;
    ReverbParams reverbParams;
    std::map<int, juce::File> loadedSamples;
    std::map<int, InstrumentParams> instrumentParams;

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
    int loadedFollowMode = 0;
    juce::String loadedBrowserDir;
    const juce::String browserDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getFullPathName();

    auto err = runProjectRoundTrip ("tracker_adjust_tests_follow_browser",
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
                                    2,
                                    browserDir,
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
                                    &loadedFollowMode,
                                    &loadedBrowserDir);
    if (err.isNotEmpty())
    {
        std::cerr << err << "\n";
        return false;
    }

    if (loadedFollowMode != 2)
    {
        std::cerr << "follow mode mismatch after round-trip\n";
        return false;
    }

    if (loadedBrowserDir != browserDir)
    {
        std::cerr << "browser directory mismatch after round-trip\n";
        return false;
    }

    return true;
}

bool testProjectRoundTripKeepsMixerLayoutAndInstrumentParams()
{
    PatternData source;
    source.getPattern (0).name = "LayoutMix";
    source.getPattern (0).resize (64);

    Cell cell;
    cell.note = 60;
    cell.instrument = 255;
    source.getPattern (0).setCell (0, 2, cell);

    Arrangement arrangement;
    arrangement.addEntry (0, 3);

    TrackLayout trackLayout;
    trackLayout.moveTrack (0, 5);
    trackLayout.setTrackName (2, "Bass");
    trackLayout.setTrackNoteMode (2, NoteMode::Release);
    trackLayout.setTrackFxLaneCount (2, 4);
    trackLayout.createGroup ("Rhythm", 0, 3);

    MixerState mixerState;
    auto& mixTrack2 = mixerState.tracks[2];
    mixTrack2.volume = -6.0;
    mixTrack2.pan = 20;
    mixTrack2.muted = true;
    mixTrack2.soloed = false;
    mixTrack2.eqMidGain = 2.5;
    mixTrack2.reverbSend = -12.0;
    mixTrack2.delaySend = -18.0;

    auto& mixTrack5 = mixerState.tracks[5];
    mixTrack5.soloed = true;

    DelayParams delayParams;
    delayParams.feedback = 67.0;
    delayParams.filterCutoff = 42.0;
    ReverbParams reverbParams;
    reverbParams.roomSize = 71.0;
    reverbParams.preDelay = 22.0;

    std::map<int, juce::File> loadedSamples;
    std::map<int, InstrumentParams> instrumentParams;
    InstrumentParams params;
    params.volume = -9.0;
    params.panning = -12;
    params.reverbSend = -18.0;
    params.delaySend = -24.0;
    params.playMode = InstrumentParams::PlayMode::Granular;
    params.granularLength = 333;
    params.modulations[static_cast<size_t> (InstrumentParams::ModDest::Cutoff)].type
        = InstrumentParams::Modulation::Type::LFO;
    params.modulations[static_cast<size_t> (InstrumentParams::ModDest::Cutoff)].amount = 48;
    instrumentParams[255] = params;

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

    auto err = runProjectRoundTrip ("tracker_adjust_tests_mix_layout_params",
                                    source,
                                    126.0,
                                    8,
                                    loadedSamples,
                                    instrumentParams,
                                    arrangement,
                                    trackLayout,
                                    mixerState,
                                    delayParams,
                                    reverbParams,
                                    1,
                                    {},
                                    loaded,
                                    loadedBpm,
                                    loadedRpb,
                                    loadedSamplesOut,
                                    instrumentParamsOut,
                                    arrangementOut,
                                    trackLayoutOut,
                                    mixerStateOut,
                                    delayOut,
                                    reverbOut);
    if (err.isNotEmpty())
    {
        std::cerr << err << "\n";
        return false;
    }

    if (arrangementOut.getNumEntries() != 1
        || arrangementOut.getEntry (0).patternIndex != 0
        || arrangementOut.getEntry (0).repeats != 3)
    {
        std::cerr << "arrangement mismatch after round-trip\n";
        return false;
    }

    if (trackLayoutOut.getVisualOrder() != trackLayout.getVisualOrder())
    {
        std::cerr << "visual order mismatch after round-trip\n";
        return false;
    }

    if (trackLayoutOut.getTrackName (2) != "Bass"
        || trackLayoutOut.getTrackNoteMode (2) != NoteMode::Release
        || trackLayoutOut.getTrackFxLaneCount (2) != 4)
    {
        std::cerr << "track layout metadata mismatch after round-trip\n";
        return false;
    }

    if (trackLayoutOut.getNumGroups() != 1
        || trackLayoutOut.getGroup (0).name != "Rhythm"
        || trackLayoutOut.getGroup (0).trackIndices != trackLayout.getGroup (0).trackIndices)
    {
        std::cerr << "track grouping mismatch after round-trip\n";
        return false;
    }

    const auto& loadedMixTrack2 = mixerStateOut.tracks[2];
    const auto& loadedMixTrack5 = mixerStateOut.tracks[5];
    if (std::abs (loadedMixTrack2.volume - (-6.0)) > 1.0e-6
        || loadedMixTrack2.pan != 20
        || ! loadedMixTrack2.muted
        || loadedMixTrack2.soloed
        || std::abs (loadedMixTrack2.eqMidGain - 2.5) > 1.0e-6
        || std::abs (loadedMixTrack2.reverbSend - (-12.0)) > 1.0e-6
        || std::abs (loadedMixTrack2.delaySend - (-18.0)) > 1.0e-6
        || ! loadedMixTrack5.soloed)
    {
        std::cerr << "mixer state mismatch after round-trip\n";
        return false;
    }

    auto it = instrumentParamsOut.find (255);
    if (it == instrumentParamsOut.end())
    {
        std::cerr << "instrument params for 0xFF missing after round-trip\n";
        return false;
    }

    const auto& loadedParams = it->second;
    if (std::abs (loadedParams.volume - (-9.0)) > 1.0e-6
        || loadedParams.panning != -12
        || std::abs (loadedParams.reverbSend - (-18.0)) > 1.0e-6
        || std::abs (loadedParams.delaySend - (-24.0)) > 1.0e-6
        || loadedParams.playMode != InstrumentParams::PlayMode::Granular
        || loadedParams.granularLength != 333
        || loadedParams.modulations[static_cast<size_t> (InstrumentParams::ModDest::Cutoff)].type
            != InstrumentParams::Modulation::Type::LFO
        || loadedParams.modulations[static_cast<size_t> (InstrumentParams::ModDest::Cutoff)].amount != 48)
    {
        std::cerr << "instrument params mismatch after round-trip\n";
        return false;
    }

    if (std::abs (delayOut.feedback - 67.0) > 1.0e-6
        || std::abs (delayOut.filterCutoff - 42.0) > 1.0e-6
        || std::abs (reverbOut.roomSize - 71.0) > 1.0e-6
        || std::abs (reverbOut.preDelay - 22.0) > 1.0e-6)
    {
        std::cerr << "send FX parameters mismatch after round-trip\n";
        return false;
    }

    if (std::abs (loadedBpm - 126.0) > 1.0e-6 || loadedRpb != 8)
    {
        std::cerr << "transport settings mismatch in layout/mixer round-trip\n";
        return false;
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

// --- Regression tests for issues 1-10 ---

bool testBpmBoundaryRoundTrip()
{
    // Issue #5 / #9: Verify extreme BPM values survive save/load round-trip
    for (double testBpm : { 20.0, 999.0, 1.0, 5000.0, 120.0 })
    {
        PatternData source;
        Arrangement arrangement;
        TrackLayout trackLayout;
        MixerState mixerState;
        DelayParams delayParams;
        ReverbParams reverbParams;
        std::map<int, juce::File> loadedSamples;
        std::map<int, InstrumentParams> instrumentParams;

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

        auto err = runProjectRoundTrip ("tracker_adjust_tests_bpm_boundary",
                                        source, testBpm, 4,
                                        loadedSamples, instrumentParams,
                                        arrangement, trackLayout, mixerState,
                                        delayParams, reverbParams, 0, {},
                                        loaded, loadedBpm, loadedRpb,
                                        loadedSamplesOut, instrumentParamsOut,
                                        arrangementOut, trackLayoutOut,
                                        mixerStateOut, delayOut, reverbOut);
        if (err.isNotEmpty())
        {
            std::cerr << "BPM boundary round-trip failed for bpm=" << testBpm << ": " << err << "\n";
            return false;
        }

        if (std::abs (loadedBpm - testBpm) > 1.0e-6)
        {
            std::cerr << "BPM boundary mismatch: saved " << testBpm << " got " << loadedBpm << "\n";
            return false;
        }
    }
    return true;
}

bool testRpbBoundaryRoundTrip()
{
    // Issue #4 / #10: Verify RPB boundary values survive save/load round-trip
    for (int testRpb : { 1, 2, 4, 8, 16 })
    {
        PatternData source;
        Arrangement arrangement;
        TrackLayout trackLayout;
        MixerState mixerState;
        DelayParams delayParams;
        ReverbParams reverbParams;
        std::map<int, juce::File> loadedSamples;
        std::map<int, InstrumentParams> instrumentParams;

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

        auto err = runProjectRoundTrip ("tracker_adjust_tests_rpb_boundary",
                                        source, 120.0, testRpb,
                                        loadedSamples, instrumentParams,
                                        arrangement, trackLayout, mixerState,
                                        delayParams, reverbParams, 0, {},
                                        loaded, loadedBpm, loadedRpb,
                                        loadedSamplesOut, instrumentParamsOut,
                                        arrangementOut, trackLayoutOut,
                                        mixerStateOut, delayOut, reverbOut);
        if (err.isNotEmpty())
        {
            std::cerr << "RPB boundary round-trip failed for rpb=" << testRpb << ": " << err << "\n";
            return false;
        }

        if (loadedRpb != testRpb)
        {
            std::cerr << "RPB boundary mismatch: saved " << testRpb << " got " << loadedRpb << "\n";
            return false;
        }
    }
    return true;
}

bool testArrangementRemapEntryAtRemovedIndex()
{
    // Issue #8: Entry pointing exactly at the removed pattern should be clamped
    Arrangement arrangement;
    arrangement.addEntry (0, 1);
    arrangement.addEntry (1, 1);  // This entry points at the pattern being removed
    arrangement.addEntry (2, 1);

    arrangement.remapAfterPatternRemoved (1, 2);

    // Entry 0 stays at 0, entry 1 was at removed index so stays at 1 (but clamped to max 1),
    // entry 2 was > removed so decremented to 1
    if (arrangement.getEntry (0).patternIndex != 0)
    {
        std::cerr << "entry 0 should remain 0 after remap\n";
        return false;
    }
    if (arrangement.getEntry (1).patternIndex != 1)
    {
        std::cerr << "entry 1 (at removed index) should be 1 after remap\n";
        return false;
    }
    if (arrangement.getEntry (2).patternIndex != 1)
    {
        std::cerr << "entry 2 should decrement to 1 after remap\n";
        return false;
    }
    return true;
}

bool testArrangementRemapClampsAboveNewCount()
{
    // Issue #8: Entries above new pattern count should be clamped
    Arrangement arrangement;
    arrangement.addEntry (9, 1);
    arrangement.addEntry (0, 1);

    arrangement.remapAfterPatternRemoved (5, 3);

    if (arrangement.getEntry (0).patternIndex != 2)
    {
        std::cerr << "high entry should clamp to newPatternCount-1\n";
        return false;
    }
    if (arrangement.getEntry (1).patternIndex != 0)
    {
        std::cerr << "low entry should remain unchanged\n";
        return false;
    }
    return true;
}

bool testPatternMixedInstrumentIdsRoundTrip()
{
    // Issue #1: Verify instruments 0, 127, 128, 255 survive save/load
    PatternData source;
    source.getPattern (0).name = "MixedInst";
    source.getPattern (0).resize (16);

    const int testInstruments[] = { 0, 127, 128, 255 };
    for (int i = 0; i < 4; ++i)
    {
        Cell cell;
        cell.note = 60 + i;
        cell.instrument = testInstruments[i];
        cell.volume = 100;
        source.getPattern (0).setCell (i, i, cell);
    }

    Arrangement arrangement;
    TrackLayout trackLayout;
    MixerState mixerState;
    DelayParams delayParams;
    ReverbParams reverbParams;
    std::map<int, juce::File> loadedSamples;
    std::map<int, InstrumentParams> instrumentParams;

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

    auto err = runProjectRoundTrip ("tracker_adjust_tests_mixed_inst",
                                    source, 120.0, 4,
                                    loadedSamples, instrumentParams,
                                    arrangement, trackLayout, mixerState,
                                    delayParams, reverbParams, 0, {},
                                    loaded, loadedBpm, loadedRpb,
                                    loadedSamplesOut, instrumentParamsOut,
                                    arrangementOut, trackLayoutOut,
                                    mixerStateOut, delayOut, reverbOut);
    if (err.isNotEmpty())
    {
        std::cerr << "mixed instruments round-trip failed: " << err << "\n";
        return false;
    }

    for (int i = 0; i < 4; ++i)
    {
        auto c = loaded.getPattern (0).getCell (i, i);
        if (c.instrument != testInstruments[i])
        {
            std::cerr << "instrument mismatch at track " << i
                      << ": expected " << testInstruments[i] << " got " << c.instrument << "\n";
            return false;
        }
        if (c.note != 60 + i)
        {
            std::cerr << "note mismatch at track " << i << "\n";
            return false;
        }
    }
    return true;
}

bool testCellEdgeValuesRoundTrip()
{
    // Verify cell boundary values survive save/load
    PatternData source;
    source.getPattern (0).resize (8);

    // Row 0: min values
    Cell minCell;
    minCell.note = 0;
    minCell.instrument = 0;
    minCell.volume = 0;
    source.getPattern (0).setCell (0, 0, minCell);

    // Row 1: max values
    Cell maxCell;
    maxCell.note = 127;
    maxCell.instrument = 255;
    maxCell.volume = 127;
    maxCell.getFxSlot (0).setSymbolicCommand ('F', 0xFF);
    source.getPattern (0).setCell (1, 0, maxCell);

    // Row 2: note-off
    Cell offCell;
    offCell.note = 255; // note-off
    source.getPattern (0).setCell (2, 0, offCell);

    Arrangement arrangement;
    TrackLayout trackLayout;
    MixerState mixerState;
    DelayParams delayParams;
    ReverbParams reverbParams;
    std::map<int, juce::File> loadedSamples;
    std::map<int, InstrumentParams> instrumentParams;

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

    auto err = runProjectRoundTrip ("tracker_adjust_tests_cell_edge",
                                    source, 120.0, 4,
                                    loadedSamples, instrumentParams,
                                    arrangement, trackLayout, mixerState,
                                    delayParams, reverbParams, 0, {},
                                    loaded, loadedBpm, loadedRpb,
                                    loadedSamplesOut, instrumentParamsOut,
                                    arrangementOut, trackLayoutOut,
                                    mixerStateOut, delayOut, reverbOut);
    if (err.isNotEmpty())
    {
        std::cerr << "cell edge values round-trip failed: " << err << "\n";
        return false;
    }

    auto c0 = loaded.getPattern (0).getCell (0, 0);
    if (c0.note != 0 || c0.instrument != 0 || c0.volume != 0)
    {
        std::cerr << "min cell values mismatch\n";
        return false;
    }

    auto c1 = loaded.getPattern (0).getCell (1, 0);
    if (c1.note != 127 || c1.instrument != 255 || c1.volume != 127)
    {
        std::cerr << "max cell values mismatch\n";
        return false;
    }
    if (c1.getFxSlot (0).fxCommand != 'F' || c1.getFxSlot (0).fxParam != 0xFF)
    {
        std::cerr << "max FX values mismatch\n";
        return false;
    }

    auto c2 = loaded.getPattern (0).getCell (2, 0);
    if (c2.note != 255)
    {
        std::cerr << "note-off not preserved\n";
        return false;
    }

    return true;
}

bool testArrangementInsertMoveRemoveIntegrity()
{
    // Issue #8 / #24: Arrangement operations maintain data integrity
    Arrangement arrangement;

    arrangement.addEntry (0, 1);
    arrangement.addEntry (1, 2);
    arrangement.addEntry (2, 3);

    if (arrangement.getNumEntries() != 3)
    {
        std::cerr << "arrangement should have 3 entries\n";
        return false;
    }

    // Insert at position 1
    arrangement.insertEntry (1, 5, 4);
    if (arrangement.getNumEntries() != 4
        || arrangement.getEntry (1).patternIndex != 5
        || arrangement.getEntry (1).repeats != 4)
    {
        std::cerr << "insert at position 1 failed\n";
        return false;
    }

    // Move entry 0 down
    arrangement.moveEntryDown (0);
    if (arrangement.getEntry (0).patternIndex != 5
        || arrangement.getEntry (1).patternIndex != 0)
    {
        std::cerr << "moveEntryDown failed\n";
        return false;
    }

    // Move entry 1 up
    arrangement.moveEntryUp (1);
    if (arrangement.getEntry (0).patternIndex != 0
        || arrangement.getEntry (1).patternIndex != 5)
    {
        std::cerr << "moveEntryUp failed\n";
        return false;
    }

    // Remove entry 1
    arrangement.removeEntry (1);
    if (arrangement.getNumEntries() != 3
        || arrangement.getEntry (0).patternIndex != 0
        || arrangement.getEntry (1).patternIndex != 1)
    {
        std::cerr << "removeEntry failed\n";
        return false;
    }

    return true;
}

bool testMultiPatternArrangementRoundTrip()
{
    // Issue #8 / #24: Complex arrangement with multiple patterns and repeats
    PatternData source;
    source.getPattern (0).name = "Intro";
    source.getPattern (0).resize (16);
    source.addPattern (32);
    source.getPattern (1).name = "Verse";
    source.addPattern (16);
    source.getPattern (2).name = "Chorus";
    source.addPattern (8);
    source.getPattern (3).name = "Bridge";

    Arrangement arrangement;
    arrangement.addEntry (0, 1);
    arrangement.addEntry (1, 4);
    arrangement.addEntry (2, 2);
    arrangement.addEntry (3, 1);
    arrangement.addEntry (1, 4);
    arrangement.addEntry (2, 2);

    TrackLayout trackLayout;
    MixerState mixerState;
    DelayParams delayParams;
    ReverbParams reverbParams;
    std::map<int, juce::File> loadedSamples;
    std::map<int, InstrumentParams> instrumentParams;

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

    auto err = runProjectRoundTrip ("tracker_adjust_tests_multi_arr",
                                    source, 140.0, 4,
                                    loadedSamples, instrumentParams,
                                    arrangement, trackLayout, mixerState,
                                    delayParams, reverbParams, 0, {},
                                    loaded, loadedBpm, loadedRpb,
                                    loadedSamplesOut, instrumentParamsOut,
                                    arrangementOut, trackLayoutOut,
                                    mixerStateOut, delayOut, reverbOut);
    if (err.isNotEmpty())
    {
        std::cerr << "multi-pattern arrangement round-trip failed: " << err << "\n";
        return false;
    }

    if (loaded.getNumPatterns() != 4)
    {
        std::cerr << "expected 4 patterns, got " << loaded.getNumPatterns() << "\n";
        return false;
    }

    if (arrangementOut.getNumEntries() != 6)
    {
        std::cerr << "expected 6 arrangement entries, got " << arrangementOut.getNumEntries() << "\n";
        return false;
    }

    // Verify all entries match
    const int expectedPatterns[] = { 0, 1, 2, 3, 1, 2 };
    const int expectedRepeats[] = { 1, 4, 2, 1, 4, 2 };
    for (int i = 0; i < 6; ++i)
    {
        if (arrangementOut.getEntry (i).patternIndex != expectedPatterns[i]
            || arrangementOut.getEntry (i).repeats != expectedRepeats[i])
        {
            std::cerr << "arrangement entry " << i << " mismatch\n";
            return false;
        }
    }

    return true;
}

bool testSendBuffersMultipleAddAccumulates()
{
    // Issue #29: Multiple addToDelay/addToReverb calls accumulate correctly
    SendBuffers buffers;
    buffers.prepare (32, 2);

    juce::AudioBuffer<float> src1 (2, 32);
    juce::AudioBuffer<float> src2 (2, 32);
    for (int ch = 0; ch < 2; ++ch)
    {
        for (int i = 0; i < 32; ++i)
        {
            src1.setSample (ch, i, 1.0f);
            src2.setSample (ch, i, 2.0f);
        }
    }

    buffers.addToDelay (src1, 0, 32, 1.0f);
    buffers.addToDelay (src2, 0, 32, 1.0f);

    juce::AudioBuffer<float> delayOut;
    juce::AudioBuffer<float> reverbOut;
    buffers.consumeSlice (delayOut, reverbOut, 0, 32, 2);

    // Should be 1.0 + 2.0 = 3.0
    for (int i = 0; i < 32; ++i)
    {
        if (! floatsClose (delayOut.getSample (0, i), 3.0f, 1.0e-4f))
        {
            std::cerr << "send buffer accumulation mismatch at sample " << i
                      << ": expected 3.0, got " << delayOut.getSample (0, i) << "\n";
            return false;
        }
    }

    return true;
}

bool testSendBuffersZeroLengthSlice()
{
    // Edge case: zero-length consume should not crash
    SendBuffers buffers;
    buffers.prepare (64, 2);

    juce::AudioBuffer<float> delayOut;
    juce::AudioBuffer<float> reverbOut;
    buffers.consumeSlice (delayOut, reverbOut, 0, 0, 2);

    if (delayOut.getNumSamples() != 0 || reverbOut.getNumSamples() != 0)
    {
        std::cerr << "zero-length consumeSlice should produce 0-sample buffers\n";
        return false;
    }

    return true;
}

bool testInstrumentRoutingBankProgramSplit()
{
    // Issue #1: Verify specific bank/program splits for boundary instruments
    struct TestCase { int instrument; int expectedBank; int expectedProgram; };
    const TestCase cases[] = {
        { 0,   0, 0 },
        { 1,   0, 1 },
        { 126, 0, 126 },
        { 127, 0, 127 },
        { 128, 1, 0 },
        { 129, 1, 1 },
        { 254, 1, 126 },
        { 255, 1, 127 },
    };

    for (const auto& tc : cases)
    {
        int bank = InstrumentRouting::getBankMsbForInstrument (tc.instrument);
        int prog = InstrumentRouting::getProgramForInstrument (tc.instrument);
        if (bank != tc.expectedBank || prog != tc.expectedProgram)
        {
            std::cerr << "bank/program split for instrument " << tc.instrument
                      << ": expected bank=" << tc.expectedBank << " prog=" << tc.expectedProgram
                      << " got bank=" << bank << " prog=" << prog << "\n";
            return false;
        }
    }

    return true;
}

bool testFxParamTransportByteRoundTrip()
{
    for (int value = 0; value <= 255; ++value)
    {
        int pendingHighBit = (value >> 7) & 0x1;
        int decoded = FxParamTransport::consumeByteFromController (value & 0x7F, pendingHighBit);
        if (decoded != value || pendingHighBit != 0)
        {
            std::cerr << "FX byte transport round-trip mismatch for value " << value << "\n";
            return false;
        }
    }

    return true;
}

bool testFxParamTransportSequenceOrdering()
{
    juce::MidiMessageSequence sequence;
    FxParamTransport::appendByteAsControllers (sequence, 1, 110, 0xE9, 1.0);
    FxParamTransport::appendByteAsControllers (sequence, 1, 110, 0x35, 1.0);

    if (sequence.getNumEvents() != 4)
    {
        std::cerr << "FX byte transport should emit exactly 4 MIDI events\n";
        return false;
    }

    int pendingHighBit = 0;
    std::vector<int> decodedValues;
    double firstTime = 0.0;
    double lastTime = 0.0;

    for (int i = 0; i < sequence.getNumEvents(); ++i)
    {
        auto* event = sequence.getEventPointer (i);
        if (event == nullptr || ! event->message.isController())
            continue;

        if (i == 0) firstTime = event->message.getTimeStamp();
        if (i == sequence.getNumEvents() - 1) lastTime = event->message.getTimeStamp();

        int ccNum = event->message.getControllerNumber();
        int ccVal = event->message.getControllerValue();

        if (ccNum == FxParamTransport::kParamHighBitCc)
            pendingHighBit = ccVal;
        else if (ccNum == 110)
            decodedValues.push_back (FxParamTransport::consumeByteFromController (ccVal, pendingHighBit));
    }

    if (decodedValues.size() != 2
        || decodedValues[0] != 0xE9
        || decodedValues[1] != 0x35)
    {
        std::cerr << "FX byte transport decode mismatch for same-time sequence test\n";
        return false;
    }

    if (firstTime > lastTime)
    {
        std::cerr << "FX byte transport should emit high-bit CC before value CC\n";
        return false;
    }

    return true;
}

bool testEmptyArrangementRoundTrip()
{
    // Verify empty arrangement round-trips correctly
    PatternData source;
    Arrangement arrangement; // empty
    TrackLayout trackLayout;
    MixerState mixerState;
    DelayParams delayParams;
    ReverbParams reverbParams;
    std::map<int, juce::File> loadedSamples;
    std::map<int, InstrumentParams> instrumentParams;

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

    auto err = runProjectRoundTrip ("tracker_adjust_tests_empty_arr",
                                    source, 120.0, 4,
                                    loadedSamples, instrumentParams,
                                    arrangement, trackLayout, mixerState,
                                    delayParams, reverbParams, 0, {},
                                    loaded, loadedBpm, loadedRpb,
                                    loadedSamplesOut, instrumentParamsOut,
                                    arrangementOut, trackLayoutOut,
                                    mixerStateOut, delayOut, reverbOut);
    if (err.isNotEmpty())
    {
        std::cerr << "empty arrangement round-trip failed: " << err << "\n";
        return false;
    }

    if (arrangementOut.getNumEntries() != 0)
    {
        std::cerr << "empty arrangement should have 0 entries after round-trip\n";
        return false;
    }

    return true;
}

bool testPatternMultiFxSlotRoundTrip()
{
    // Issue #9: Verify multiple FX slots per cell survive round-trip
    PatternData source;
    source.getPattern (0).resize (4);

    Cell cell;
    cell.note = 60;
    cell.instrument = 0;
    // Set up to 4 FX slots if supported
    cell.getFxSlot (0).setSymbolicCommand ('S', 0x37);
    cell.getFxSlot (1).setSymbolicCommand ('F', 0x80);
    source.getPattern (0).setCell (0, 0, cell);

    Arrangement arrangement;
    TrackLayout trackLayout;
    MixerState mixerState;
    DelayParams delayParams;
    ReverbParams reverbParams;
    std::map<int, juce::File> loadedSamples;
    std::map<int, InstrumentParams> instrumentParams;

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

    auto err = runProjectRoundTrip ("tracker_adjust_tests_multi_fx",
                                    source, 120.0, 4,
                                    loadedSamples, instrumentParams,
                                    arrangement, trackLayout, mixerState,
                                    delayParams, reverbParams, 0, {},
                                    loaded, loadedBpm, loadedRpb,
                                    loadedSamplesOut, instrumentParamsOut,
                                    arrangementOut, trackLayoutOut,
                                    mixerStateOut, delayOut, reverbOut);
    if (err.isNotEmpty())
    {
        std::cerr << "multi FX slot round-trip failed: " << err << "\n";
        return false;
    }

    auto c = loaded.getPattern (0).getCell (0, 0);
    if (c.getFxSlot (0).fxCommand != 'S' || c.getFxSlot (0).fxParam != 0x37
        || c.getFxSlot (1).fxCommand != 'F' || c.getFxSlot (1).fxParam != 0x80)
    {
        std::cerr << "multi FX slot data mismatch\n";
        return false;
    }

    return true;
}

bool testTrackLayoutFxLaneCountRoundTrip()
{
    // Issue #10: FX lane count per track survives round-trip
    PatternData source;
    TrackLayout trackLayout;
    trackLayout.setTrackFxLaneCount (0, 1);
    trackLayout.setTrackFxLaneCount (1, 4);
    trackLayout.setTrackFxLaneCount (2, 8);

    Arrangement arrangement;
    MixerState mixerState;
    DelayParams delayParams;
    ReverbParams reverbParams;
    std::map<int, juce::File> loadedSamples;
    std::map<int, InstrumentParams> instrumentParams;

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

    auto err = runProjectRoundTrip ("tracker_adjust_tests_fx_lanes",
                                    source, 120.0, 4,
                                    loadedSamples, instrumentParams,
                                    arrangement, trackLayout, mixerState,
                                    delayParams, reverbParams, 0, {},
                                    loaded, loadedBpm, loadedRpb,
                                    loadedSamplesOut, instrumentParamsOut,
                                    arrangementOut, trackLayoutOut,
                                    mixerStateOut, delayOut, reverbOut);
    if (err.isNotEmpty())
    {
        std::cerr << "FX lane count round-trip failed: " << err << "\n";
        return false;
    }

    if (trackLayoutOut.getTrackFxLaneCount (0) != 1
        || trackLayoutOut.getTrackFxLaneCount (1) != 4
        || trackLayoutOut.getTrackFxLaneCount (2) != 8)
    {
        std::cerr << "FX lane counts mismatch after round-trip\n";
        return false;
    }

    return true;
}

bool testSymbolicFxTokenRoundTrip()
{
    PatternData source;
    source.getPattern (0).resize (8);

    TrackLayout trackLayout;
    trackLayout.setTrackFxLaneCount (0, 2);

    Cell cell;
    cell.note = 60;
    cell.instrument = 1;
    cell.getFxSlot (0).setSymbolicCommand ('T', 0xF8);
    cell.getFxSlot (1).setSymbolicCommand ('G', 0x14);
    source.getPattern (0).setCell (1, 0, cell);

    Arrangement arrangement;
    MixerState mixerState;
    DelayParams delayParams;
    ReverbParams reverbParams;
    std::map<int, juce::File> loadedSamples;
    std::map<int, InstrumentParams> instrumentParams;

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

    auto err = runProjectRoundTrip ("tracker_adjust_tests_symbolic_fx",
                                    source, 120.0, 4,
                                    loadedSamples, instrumentParams,
                                    arrangement, trackLayout, mixerState,
                                    delayParams, reverbParams, 0, {},
                                    loaded, loadedBpm, loadedRpb,
                                    loadedSamplesOut, instrumentParamsOut,
                                    arrangementOut, trackLayoutOut,
                                    mixerStateOut, delayOut, reverbOut);
    if (err.isNotEmpty())
    {
        std::cerr << "symbolic FX round-trip failed: " << err << "\n";
        return false;
    }

    const auto loadedCell = loaded.getPattern (0).getCell (1, 0);
    if (loadedCell.getNumFxSlots() < 2)
    {
        std::cerr << "expected 2 FX lanes after symbolic round-trip\n";
        return false;
    }

    if (loadedCell.getFxSlot (0).fxCommand != 'T' || loadedCell.getFxSlot (0).fxParam != 0xF8
        || loadedCell.getFxSlot (1).fxCommand != 'G' || loadedCell.getFxSlot (1).fxParam != 0x14)
    {
        std::cerr << "symbolic FX token mismatch after round-trip\n";
        return false;
    }

    return true;
}

bool testMasterLaneRoundTrip()
{
    PatternData source;
    source.getPattern (0).resize (16);
    source.getPattern (0).ensureMasterFxSlots (3);
    source.getPattern (0).getMasterFxSlot (0, 0).setSymbolicCommand ('F', 130);
    source.getPattern (0).getMasterFxSlot (4, 2).setSymbolicCommand ('F', 176);

    TrackLayout trackLayout;
    trackLayout.setMasterFxLaneCount (3);

    Arrangement arrangement;
    MixerState mixerState;
    DelayParams delayParams;
    ReverbParams reverbParams;
    std::map<int, juce::File> loadedSamples;
    std::map<int, InstrumentParams> instrumentParams;

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

    auto err = runProjectRoundTrip ("tracker_adjust_tests_master_lane",
                                    source, 120.0, 4,
                                    loadedSamples, instrumentParams,
                                    arrangement, trackLayout, mixerState,
                                    delayParams, reverbParams, 0, {},
                                    loaded, loadedBpm, loadedRpb,
                                    loadedSamplesOut, instrumentParamsOut,
                                    arrangementOut, trackLayoutOut,
                                    mixerStateOut, delayOut, reverbOut);
    if (err.isNotEmpty())
    {
        std::cerr << "master lane round-trip failed: " << err << "\n";
        return false;
    }

    if (trackLayoutOut.getMasterFxLaneCount() != 3)
    {
        std::cerr << "master lane count mismatch after round-trip\n";
        return false;
    }

    const auto& pat = loaded.getPattern (0);
    if (pat.getMasterFxSlot (0, 0).fxCommand != 'F' || pat.getMasterFxSlot (0, 0).fxParam != 130
        || pat.getMasterFxSlot (4, 2).fxCommand != 'F' || pat.getMasterFxSlot (4, 2).fxParam != 176)
    {
        std::cerr << "master FX content mismatch after round-trip\n";
        return false;
    }

    return true;
}

bool testMixerMuteSoloRoundTrip()
{
    // Issue #6 / #20: Mute/solo state persists through save/load
    PatternData source;
    Arrangement arrangement;
    TrackLayout trackLayout;
    DelayParams delayParams;
    ReverbParams reverbParams;
    std::map<int, juce::File> loadedSamples;
    std::map<int, InstrumentParams> instrumentParams;

    MixerState mixerState;
    mixerState.tracks[0].muted = true;
    mixerState.tracks[0].soloed = false;
    mixerState.tracks[1].muted = false;
    mixerState.tracks[1].soloed = true;
    mixerState.tracks[2].muted = true;
    mixerState.tracks[2].soloed = true;

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

    auto err = runProjectRoundTrip ("tracker_adjust_tests_mute_solo",
                                    source, 120.0, 4,
                                    loadedSamples, instrumentParams,
                                    arrangement, trackLayout, mixerState,
                                    delayParams, reverbParams, 0, {},
                                    loaded, loadedBpm, loadedRpb,
                                    loadedSamplesOut, instrumentParamsOut,
                                    arrangementOut, trackLayoutOut,
                                    mixerStateOut, delayOut, reverbOut);
    if (err.isNotEmpty())
    {
        std::cerr << "mute/solo round-trip failed: " << err << "\n";
        return false;
    }

    if (! mixerStateOut.tracks[0].muted || mixerStateOut.tracks[0].soloed)
    {
        std::cerr << "track 0 mute/solo state mismatch\n";
        return false;
    }
    if (mixerStateOut.tracks[1].muted || ! mixerStateOut.tracks[1].soloed)
    {
        std::cerr << "track 1 mute/solo state mismatch\n";
        return false;
    }
    if (! mixerStateOut.tracks[2].muted || ! mixerStateOut.tracks[2].soloed)
    {
        std::cerr << "track 2 mute/solo state mismatch\n";
        return false;
    }

    return true;
}

bool testArrangementRemapPreservesRepeats()
{
    // Issue #8: Remap should preserve repeat counts, not just indices
    Arrangement arrangement;
    arrangement.addEntry (0, 5);
    arrangement.addEntry (2, 3);
    arrangement.addEntry (4, 7);

    arrangement.remapAfterPatternRemoved (1, 4);

    if (arrangement.getEntry (0).repeats != 5
        || arrangement.getEntry (1).repeats != 3
        || arrangement.getEntry (2).repeats != 7)
    {
        std::cerr << "remap altered repeat counts\n";
        return false;
    }

    // Verify indices: 0 stays 0, 2->1, 4->3
    if (arrangement.getEntry (0).patternIndex != 0
        || arrangement.getEntry (1).patternIndex != 1
        || arrangement.getEntry (2).patternIndex != 3)
    {
        std::cerr << "remap indices incorrect\n";
        return false;
    }

    return true;
}

bool testGranularCenterUsesAbsolutePosition()
{
    InstrumentParams params;
    params.startPos = 0.25;
    params.endPos = 0.75;
    params.granularPosition = 0.60;

    const double center = SamplePlaybackLayout::getGranularCenterNorm (params);
    if (! doublesClose (center, 0.60))
    {
        std::cerr << "granular center should use absolute position; got " << center << "\n";
        return false;
    }

    return true;
}

bool testGranularCenterClampsToRegion()
{
    InstrumentParams params;
    params.startPos = 0.25;
    params.endPos = 0.75;
    params.granularPosition = 0.05;

    const double center = SamplePlaybackLayout::getGranularCenterNorm (params);
    if (! doublesClose (center, 0.25))
    {
        std::cerr << "granular center should clamp to region start; got " << center << "\n";
        return false;
    }

    return true;
}

bool testSliceBoundariesUseAbsolutePositions()
{
    InstrumentParams params;
    params.startPos = 0.2;
    params.endPos = 0.8;
    params.slicePoints = { 0.3, 0.5, 0.7 };

    const auto boundaries = SamplePlaybackLayout::getSliceBoundariesNorm (params);
    const std::vector<double> expected { 0.2, 0.3, 0.5, 0.7, 0.8 };

    if (! vectorsClose (boundaries, expected))
    {
        std::cerr << "slice boundaries mismatch for absolute points\n";
        return false;
    }

    return true;
}

bool testSliceBoundariesClampAndDeduplicate()
{
    InstrumentParams params;
    params.startPos = 0.2;
    params.endPos = 0.8;
    params.slicePoints = { 0.1, 0.2, 0.2000000001, 0.4, 1.0, 0.4 };

    const auto boundaries = SamplePlaybackLayout::getSliceBoundariesNorm (params);
    const std::vector<double> expected { 0.2, 0.4, 0.8 };

    if (! vectorsClose (boundaries, expected))
    {
        std::cerr << "slice boundaries should clamp + dedupe\n";
        return false;
    }

    return true;
}

bool testEqualSlicePointGenerationUsesRegionCount()
{
    const auto points = SamplePlaybackLayout::makeEqualSlicePointsNorm (0.2, 0.8, 4);
    const std::vector<double> expected { 0.35, 0.5, 0.65 };
    if (! vectorsClose (points, expected))
    {
        std::cerr << "equal slice generation should create N-1 points for N regions\n";
        return false;
    }

    const auto singleRegionPoints = SamplePlaybackLayout::makeEqualSlicePointsNorm (0.1, 0.9, 1);
    if (! singleRegionPoints.empty())
    {
        std::cerr << "single-region equal slice generation should produce no points\n";
        return false;
    }

    return true;
}

bool testBeatSliceRegionCountDefaultsAndPointCount()
{
    InstrumentParams params;
    params.playMode = InstrumentParams::PlayMode::BeatSlice;

    if (SamplePlaybackLayout::getBeatSliceRegionCount (params) != 16)
    {
        std::cerr << "BeatSlice with no points should default to 16 regions\n";
        return false;
    }

    params.slicePoints = { 0.25, 0.5, 0.75 };
    if (SamplePlaybackLayout::getBeatSliceRegionCount (params) != 4)
    {
        std::cerr << "BeatSlice region count should be slicePoints + 1\n";
        return false;
    }

    return true;
}

bool testNoteLaneSerializationRoundTrip()
{
    // Phase 1: Verify that note lane data and note lane counts survive save/load
    PatternData source;
    source.getPattern (0).name = "NoteLanes";
    source.getPattern (0).resize (16);

    // Track 0: set up 3 note lanes with data on lane 0 and lane 2
    {
        Cell cell;
        cell.note = 60;
        cell.instrument = 1;
        cell.volume = 100;
        // Lane 1 (extra lane 0)
        NoteSlot lane1;
        lane1.note = 64;
        lane1.instrument = 2;
        lane1.volume = 80;
        cell.setNoteLane (1, lane1);
        // Lane 2 (extra lane 1)
        NoteSlot lane2;
        lane2.note = 67;
        lane2.instrument = 3;
        lane2.volume = 60;
        cell.setNoteLane (2, lane2);
        source.getPattern (0).setCell (0, 0, cell);
    }

    // Track 0 row 4: only lane 1 has data (lane 0 and 2 empty)
    {
        Cell cell;
        NoteSlot lane1;
        lane1.note = 72;
        lane1.instrument = 5;
        lane1.volume = 90;
        cell.setNoteLane (1, lane1);
        source.getPattern (0).setCell (4, 0, cell);
    }

    // Track 3: 2 note lanes, lane 0 has OFF, lane 1 has KILL
    {
        Cell cell;
        cell.note = 255; // OFF
        NoteSlot lane1;
        lane1.note = 254; // KILL
        cell.setNoteLane (1, lane1);
        source.getPattern (0).setCell (2, 3, cell);
    }

    TrackLayout trackLayout;
    trackLayout.setTrackNoteLaneCount (0, 3);
    trackLayout.setTrackNoteLaneCount (3, 2);
    trackLayout.setTrackNoteLaneCount (7, 8); // max

    Arrangement arrangement;
    MixerState mixerState;
    DelayParams delayParams;
    ReverbParams reverbParams;
    std::map<int, juce::File> loadedSamples;
    std::map<int, InstrumentParams> instrumentParams;

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

    auto err = runProjectRoundTrip ("tracker_adjust_tests_note_lanes",
                                    source, 120.0, 4,
                                    loadedSamples, instrumentParams,
                                    arrangement, trackLayout, mixerState,
                                    delayParams, reverbParams, 0, {},
                                    loaded, loadedBpm, loadedRpb,
                                    loadedSamplesOut, instrumentParamsOut,
                                    arrangementOut, trackLayoutOut,
                                    mixerStateOut, delayOut, reverbOut);
    if (err.isNotEmpty())
    {
        std::cerr << "note lane round-trip failed: " << err << "\n";
        return false;
    }

    // Verify note lane counts in TrackLayout
    if (trackLayoutOut.getTrackNoteLaneCount (0) != 3)
    {
        std::cerr << "track 0 note lane count mismatch: expected 3, got "
                  << trackLayoutOut.getTrackNoteLaneCount (0) << "\n";
        return false;
    }
    if (trackLayoutOut.getTrackNoteLaneCount (3) != 2)
    {
        std::cerr << "track 3 note lane count mismatch: expected 2, got "
                  << trackLayoutOut.getTrackNoteLaneCount (3) << "\n";
        return false;
    }
    if (trackLayoutOut.getTrackNoteLaneCount (7) != 8)
    {
        std::cerr << "track 7 note lane count mismatch: expected 8, got "
                  << trackLayoutOut.getTrackNoteLaneCount (7) << "\n";
        return false;
    }
    // Track with default (1 lane) should remain 1
    if (trackLayoutOut.getTrackNoteLaneCount (1) != 1)
    {
        std::cerr << "track 1 note lane count should be 1, got "
                  << trackLayoutOut.getTrackNoteLaneCount (1) << "\n";
        return false;
    }

    // Verify pattern cell data - track 0, row 0
    {
        const auto& cell = loaded.getPattern (0).getCell (0, 0);
        auto lane0 = cell.getNoteLane (0);
        if (lane0.note != 60 || lane0.instrument != 1 || lane0.volume != 100)
        {
            std::cerr << "track 0 row 0 lane 0 data mismatch\n";
            return false;
        }
        auto lane1 = cell.getNoteLane (1);
        if (lane1.note != 64 || lane1.instrument != 2 || lane1.volume != 80)
        {
            std::cerr << "track 0 row 0 lane 1 data mismatch\n";
            return false;
        }
        auto lane2 = cell.getNoteLane (2);
        if (lane2.note != 67 || lane2.instrument != 3 || lane2.volume != 60)
        {
            std::cerr << "track 0 row 0 lane 2 data mismatch\n";
            return false;
        }
    }

    // Verify track 0, row 4 - only lane 1 has data
    {
        const auto& cell = loaded.getPattern (0).getCell (4, 0);
        auto lane0 = cell.getNoteLane (0);
        if (lane0.note != -1 || lane0.instrument != -1 || lane0.volume != -1)
        {
            std::cerr << "track 0 row 4 lane 0 should be empty\n";
            return false;
        }
        auto lane1 = cell.getNoteLane (1);
        if (lane1.note != 72 || lane1.instrument != 5 || lane1.volume != 90)
        {
            std::cerr << "track 0 row 4 lane 1 data mismatch\n";
            return false;
        }
    }

    // Verify track 3, row 2 - OFF on lane 0, KILL on lane 1
    {
        const auto& cell = loaded.getPattern (0).getCell (2, 3);
        auto lane0 = cell.getNoteLane (0);
        if (lane0.note != 255)
        {
            std::cerr << "track 3 row 2 lane 0 should be OFF (255), got " << lane0.note << "\n";
            return false;
        }
        auto lane1 = cell.getNoteLane (1);
        if (lane1.note != 254)
        {
            std::cerr << "track 3 row 2 lane 1 should be KILL (254), got " << lane1.note << "\n";
            return false;
        }
    }

    return true;
}

bool testMultiLaneNoteDataSanity()
{
    // Phase 1: Verify multi-lane NoteSlot accessors, ensureNoteLanes, and lane independence
    Cell cell;

    // Default cell should have 1 note lane
    if (cell.getNumNoteLanes() != 1)
    {
        std::cerr << "default cell should have 1 note lane, got " << cell.getNumNoteLanes() << "\n";
        return false;
    }

    // Lane 0 should map to the main note/instrument/volume fields
    cell.note = 60;
    cell.instrument = 5;
    cell.volume = 100;
    auto lane0 = cell.getNoteLane (0);
    if (lane0.note != 60 || lane0.instrument != 5 || lane0.volume != 100)
    {
        std::cerr << "lane 0 should reflect main cell fields\n";
        return false;
    }

    // Setting lane 0 via setNoteLane should update main fields
    NoteSlot newLane0;
    newLane0.note = 72;
    newLane0.instrument = 10;
    newLane0.volume = 50;
    cell.setNoteLane (0, newLane0);
    if (cell.note != 72 || cell.instrument != 10 || cell.volume != 50)
    {
        std::cerr << "setNoteLane(0) should update main cell fields\n";
        return false;
    }

    // Reading non-existent lane should return empty NoteSlot
    auto emptyLane = cell.getNoteLane (5);
    if (! emptyLane.isEmpty())
    {
        std::cerr << "non-existent lane should return empty NoteSlot\n";
        return false;
    }

    // ensureNoteLanes should expand the cell
    cell.ensureNoteLanes (4);
    if (cell.getNumNoteLanes() < 4)
    {
        std::cerr << "ensureNoteLanes(4) should give at least 4 lanes, got " << cell.getNumNoteLanes() << "\n";
        return false;
    }

    // Lanes should be independent
    NoteSlot slot1, slot2, slot3;
    slot1.note = 60; slot1.instrument = 1; slot1.volume = 100;
    slot2.note = 64; slot2.instrument = 2; slot2.volume = 80;
    slot3.note = 67; slot3.instrument = 3; slot3.volume = 60;
    cell.setNoteLane (1, slot1);
    cell.setNoteLane (2, slot2);
    cell.setNoteLane (3, slot3);

    auto readSlot1 = cell.getNoteLane (1);
    auto readSlot2 = cell.getNoteLane (2);
    auto readSlot3 = cell.getNoteLane (3);

    if (readSlot1.note != 60 || readSlot1.instrument != 1 || readSlot1.volume != 100)
    {
        std::cerr << "lane 1 data corrupted\n";
        return false;
    }
    if (readSlot2.note != 64 || readSlot2.instrument != 2 || readSlot2.volume != 80)
    {
        std::cerr << "lane 2 data corrupted\n";
        return false;
    }
    if (readSlot3.note != 67 || readSlot3.instrument != 3 || readSlot3.volume != 60)
    {
        std::cerr << "lane 3 data corrupted\n";
        return false;
    }

    // Modifying one lane should not affect others
    NoteSlot modified;
    modified.note = 48;
    modified.instrument = 99;
    modified.volume = 127;
    cell.setNoteLane (2, modified);

    auto rereadSlot1 = cell.getNoteLane (1);
    auto rereadSlot3 = cell.getNoteLane (3);
    if (rereadSlot1.note != 60 || rereadSlot3.note != 67)
    {
        std::cerr << "modifying lane 2 corrupted lane 1 or 3\n";
        return false;
    }

    // NoteSlot isEmpty test
    NoteSlot emptySlot;
    if (! emptySlot.isEmpty())
    {
        std::cerr << "default NoteSlot should be empty\n";
        return false;
    }
    emptySlot.note = 60;
    if (emptySlot.isEmpty())
    {
        std::cerr << "NoteSlot with note should not be empty\n";
        return false;
    }

    // NoteSlot hasNote test
    NoteSlot slotWithInst;
    slotWithInst.instrument = 5;
    if (slotWithInst.hasNote())
    {
        std::cerr << "NoteSlot without note should return hasNote() false\n";
        return false;
    }

    // NoteSlot clear test
    NoteSlot filledSlot;
    filledSlot.note = 60;
    filledSlot.instrument = 1;
    filledSlot.volume = 100;
    filledSlot.clear();
    if (! filledSlot.isEmpty())
    {
        std::cerr << "cleared NoteSlot should be empty\n";
        return false;
    }

    // Cell isEmpty should check extra note lanes
    Cell cellWithExtraLane;
    NoteSlot extraSlot;
    extraSlot.note = 60;
    cellWithExtraLane.setNoteLane (1, extraSlot);
    if (cellWithExtraLane.isEmpty())
    {
        std::cerr << "cell with non-empty extra lane should not be isEmpty()\n";
        return false;
    }

    // Cell clear should also clear extra lanes
    cellWithExtraLane.clear();
    if (! cellWithExtraLane.isEmpty())
    {
        std::cerr << "cell after clear() should be empty\n";
        return false;
    }
    if (cellWithExtraLane.getNumNoteLanes() != 1)
    {
        std::cerr << "cell after clear() should have 1 note lane\n";
        return false;
    }

    // TrackLayout note lane count clamping
    TrackLayout layout;
    layout.setTrackNoteLaneCount (0, 0);  // should clamp to 1
    if (layout.getTrackNoteLaneCount (0) != 1)
    {
        std::cerr << "note lane count should clamp to min 1\n";
        return false;
    }
    layout.setTrackNoteLaneCount (0, 99); // should clamp to 8
    if (layout.getTrackNoteLaneCount (0) != 8)
    {
        std::cerr << "note lane count should clamp to max 8\n";
        return false;
    }

    // addNoteLane / removeNoteLane
    layout.setTrackNoteLaneCount (5, 1);
    layout.addNoteLane (5);
    if (layout.getTrackNoteLaneCount (5) != 2)
    {
        std::cerr << "addNoteLane should increment from 1 to 2\n";
        return false;
    }
    layout.removeNoteLane (5);
    if (layout.getTrackNoteLaneCount (5) != 1)
    {
        std::cerr << "removeNoteLane should decrement from 2 to 1\n";
        return false;
    }
    layout.removeNoteLane (5); // should not go below 1
    if (layout.getTrackNoteLaneCount (5) != 1)
    {
        std::cerr << "removeNoteLane should not go below 1\n";
        return false;
    }

    return true;
}

//==============================================================================
// Plugin instrument ownership and slot info tests
//==============================================================================

bool testInstrumentSlotInfoSetAndClear()
{
    InstrumentSlotInfo info;

    // Default state: sample mode, no owner
    if (info.isPlugin())
    {
        std::cerr << "Default InstrumentSlotInfo should not be a plugin\n";
        return false;
    }
    if (! info.isSample())
    {
        std::cerr << "Default InstrumentSlotInfo should be a sample\n";
        return false;
    }
    if (info.hasOwner())
    {
        std::cerr << "Default InstrumentSlotInfo should not have an owner\n";
        return false;
    }

    // Set as plugin instrument
    juce::PluginDescription desc;
    desc.name = "TestSynth";
    desc.pluginFormatName = "VST3";
    desc.fileOrIdentifier = "test-id-123";
    desc.uniqueId = 42;
    desc.isInstrument = true;

    info.setPlugin (desc, 3);

    if (! info.isPlugin())
    {
        std::cerr << "After setPlugin, should be a plugin\n";
        return false;
    }
    if (info.isSample())
    {
        std::cerr << "After setPlugin, should not be a sample\n";
        return false;
    }
    if (! info.hasOwner())
    {
        std::cerr << "After setPlugin with track 3, should have owner\n";
        return false;
    }
    if (info.ownerTrack != 3)
    {
        std::cerr << "Owner track should be 3, got " << info.ownerTrack << "\n";
        return false;
    }
    if (info.pluginDescription.name != "TestSynth")
    {
        std::cerr << "Plugin name should be TestSynth\n";
        return false;
    }

    // Clear back to sample mode
    info.clear();

    if (info.isPlugin())
    {
        std::cerr << "After clear, should not be a plugin\n";
        return false;
    }
    if (info.hasOwner())
    {
        std::cerr << "After clear, should not have owner\n";
        return false;
    }

    return true;
}

bool testPluginInstrumentSlotSerializationRoundTrip()
{
    PatternData source;
    source.getPattern (0).name = "PluginTest";
    source.getPattern (0).resize (16);

    Cell c;
    c.note = 60;
    c.instrument = 5;
    source.getPattern (0).setCell (0, 2, c);

    Arrangement arr;
    TrackLayout layout;
    MixerState mixer;
    DelayParams delay;
    ReverbParams reverb;
    std::map<int, juce::File> samples;
    std::map<int, InstrumentParams> params;

    // Create plugin instrument slot infos
    std::map<int, InstrumentSlotInfo> pluginSlots;
    {
        InstrumentSlotInfo info;
        juce::PluginDescription desc;
        desc.name = "TestSynth";
        desc.pluginFormatName = "VST3";
        desc.fileOrIdentifier = "test-vst3-id";
        desc.uniqueId = 1234;
        desc.deprecatedUid = 5678;
        desc.manufacturerName = "TestMfg";
        desc.category = "Synth";
        desc.isInstrument = true;
        info.setPlugin (desc, 2);
        pluginSlots[5] = info;
    }
    {
        InstrumentSlotInfo info;
        juce::PluginDescription desc;
        desc.name = "AnotherSynth";
        desc.pluginFormatName = "AudioUnit";
        desc.fileOrIdentifier = "au-id-456";
        desc.uniqueId = 9999;
        desc.manufacturerName = "OtherMfg";
        desc.isInstrument = true;
        info.setPlugin (desc, 7);
        pluginSlots[0x0A] = info;
    }

    // Save
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getNonexistentChildFile ("plugin_slot_roundtrip", ".tkadj", false);

    auto saveErr = ProjectSerializer::saveToFile (file, source, 120.0, 4,
                                                   samples, params, arr, layout, mixer,
                                                   delay, reverb, 0, {}, &pluginSlots);
    if (saveErr.isNotEmpty())
    {
        std::cerr << "Save failed: " << saveErr << "\n";
        file.deleteFile();
        return false;
    }

    // Load
    PatternData loaded;
    double bpm = 0.0;
    int rpb = 0;
    std::map<int, juce::File> loadedSamples;
    std::map<int, InstrumentParams> loadedParams;
    Arrangement loadedArr;
    TrackLayout loadedLayout;
    MixerState loadedMixer;
    DelayParams loadedDelay;
    ReverbParams loadedReverb;
    std::map<int, InstrumentSlotInfo> loadedPluginSlots;

    auto loadErr = ProjectSerializer::loadFromFile (file, loaded, bpm, rpb,
                                                     loadedSamples, loadedParams, loadedArr,
                                                     loadedLayout, loadedMixer, loadedDelay,
                                                     loadedReverb, nullptr, nullptr,
                                                     &loadedPluginSlots);
    file.deleteFile();

    if (loadErr.isNotEmpty())
    {
        std::cerr << "Load failed: " << loadErr << "\n";
        return false;
    }

    // Verify plugin slots round-tripped correctly
    if (loadedPluginSlots.size() != 2)
    {
        std::cerr << "Expected 2 plugin slots, got " << loadedPluginSlots.size() << "\n";
        return false;
    }

    // Check slot 5
    auto it5 = loadedPluginSlots.find (5);
    if (it5 == loadedPluginSlots.end())
    {
        std::cerr << "Plugin slot 5 not found after load\n";
        return false;
    }
    if (! it5->second.isPlugin())
    {
        std::cerr << "Slot 5 should be a plugin\n";
        return false;
    }
    if (it5->second.ownerTrack != 2)
    {
        std::cerr << "Slot 5 owner track should be 2, got " << it5->second.ownerTrack << "\n";
        return false;
    }
    if (it5->second.pluginDescription.name != "TestSynth")
    {
        std::cerr << "Slot 5 plugin name should be TestSynth\n";
        return false;
    }
    if (it5->second.pluginDescription.pluginFormatName != "VST3")
    {
        std::cerr << "Slot 5 format should be VST3\n";
        return false;
    }
    if (it5->second.pluginDescription.uniqueId != 1234)
    {
        std::cerr << "Slot 5 uniqueId should be 1234\n";
        return false;
    }

    // Check slot 0x0A
    auto itA = loadedPluginSlots.find (0x0A);
    if (itA == loadedPluginSlots.end())
    {
        std::cerr << "Plugin slot 0x0A not found after load\n";
        return false;
    }
    if (! itA->second.isPlugin())
    {
        std::cerr << "Slot 0x0A should be a plugin\n";
        return false;
    }
    if (itA->second.ownerTrack != 7)
    {
        std::cerr << "Slot 0x0A owner track should be 7, got " << itA->second.ownerTrack << "\n";
        return false;
    }
    if (itA->second.pluginDescription.name != "AnotherSynth")
    {
        std::cerr << "Slot 0x0A plugin name should be AnotherSynth\n";
        return false;
    }
    if (itA->second.pluginDescription.pluginFormatName != "AudioUnit")
    {
        std::cerr << "Slot 0x0A format should be AudioUnit\n";
        return false;
    }

    return true;
}

bool testPluginInstrumentOwnershipValidation()
{
    // This test validates the ownership logic at the data model level.
    // TrackerEngine::validateNoteEntry uses this logic but is not available
    // in the test binary. We test the underlying InstrumentSlotInfo behavior.

    std::map<int, InstrumentSlotInfo> slotInfos;

    // Set up: instrument 5 is a plugin on track 2
    {
        InstrumentSlotInfo info;
        juce::PluginDescription desc;
        desc.name = "Synth";
        desc.isInstrument = true;
        info.setPlugin (desc, 2);
        slotInfos[5] = info;
    }

    // Validate: instrument 5 on track 2 should be allowed
    {
        auto it = slotInfos.find (5);
        if (it == slotInfos.end() || ! it->second.isPlugin())
        {
            std::cerr << "Instrument 5 should be a plugin\n";
            return false;
        }
        if (it->second.ownerTrack != 2)
        {
            std::cerr << "Instrument 5 should be owned by track 2\n";
            return false;
        }
        // Ownership check: entry on owner track is OK
        bool allowed = (it->second.ownerTrack == 2);
        if (! allowed)
        {
            std::cerr << "Instrument 5 on track 2 should be allowed\n";
            return false;
        }
    }

    // Validate: instrument 5 on track 3 should be BLOCKED
    {
        auto it = slotInfos.find (5);
        bool allowed = (it->second.ownerTrack == 3);
        if (allowed)
        {
            std::cerr << "Instrument 5 on track 3 should be blocked\n";
            return false;
        }
    }

    // Validate: track content mode check - sample instrument on plugin track
    {
        // Track 2 has a plugin instrument, so sample instrument 10 should be blocked
        bool trackHasPlugin = false;
        for (const auto& [instIdx, info] : slotInfos)
        {
            if (info.isPlugin() && info.ownerTrack == 2)
            {
                trackHasPlugin = true;
                break;
            }
        }
        if (! trackHasPlugin)
        {
            std::cerr << "Track 2 should be in plugin mode\n";
            return false;
        }

        // A sample instrument (not in slotInfos as plugin) on track 2 should be blocked
        auto it10 = slotInfos.find (10);
        bool isSampleInstrument = (it10 == slotInfos.end());
        if (isSampleInstrument && trackHasPlugin)
        {
            // This is the correct blocking condition
        }
        else
        {
            std::cerr << "Sample instrument 10 on plugin track 2 should be blocked\n";
            return false;
        }
    }

    // Validate: sample instrument on non-plugin track should be allowed
    {
        bool track0HasPlugin = false;
        for (const auto& [instIdx, info] : slotInfos)
        {
            if (info.isPlugin() && info.ownerTrack == 0)
            {
                track0HasPlugin = true;
                break;
            }
        }
        if (track0HasPlugin)
        {
            std::cerr << "Track 0 should not have a plugin instrument\n";
            return false;
        }
        // Sample instrument on non-plugin track: allowed
    }

    return true;
}

bool testPluginSlotSerializationEmptyRoundTrip()
{
    // Test that saving/loading with no plugin slots works correctly
    PatternData source;
    source.getPattern (0).resize (16);

    Arrangement arr;
    TrackLayout layout;
    MixerState mixer;
    DelayParams delay;
    ReverbParams reverb;
    std::map<int, juce::File> samples;
    std::map<int, InstrumentParams> params;

    // Empty plugin slots
    std::map<int, InstrumentSlotInfo> emptySlots;

    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getNonexistentChildFile ("empty_plugin_roundtrip", ".tkadj", false);

    auto saveErr = ProjectSerializer::saveToFile (file, source, 120.0, 4,
                                                   samples, params, arr, layout, mixer,
                                                   delay, reverb, 0, {}, &emptySlots);
    if (saveErr.isNotEmpty())
    {
        file.deleteFile();
        std::cerr << "Save failed: " << saveErr << "\n";
        return false;
    }

    PatternData loaded;
    double bpm = 0;
    int rpb = 0;
    std::map<int, juce::File> loadedSamples;
    std::map<int, InstrumentParams> loadedParams;
    Arrangement loadedArr;
    TrackLayout loadedLayout;
    MixerState loadedMixer;
    DelayParams loadedDelay;
    ReverbParams loadedReverb;
    std::map<int, InstrumentSlotInfo> loadedPluginSlots;

    auto loadErr = ProjectSerializer::loadFromFile (file, loaded, bpm, rpb,
                                                     loadedSamples, loadedParams, loadedArr,
                                                     loadedLayout, loadedMixer, loadedDelay,
                                                     loadedReverb, nullptr, nullptr,
                                                     &loadedPluginSlots);
    file.deleteFile();

    if (loadErr.isNotEmpty())
    {
        std::cerr << "Load failed: " << loadErr << "\n";
        return false;
    }

    if (! loadedPluginSlots.empty())
    {
        std::cerr << "Expected empty plugin slots, got " << loadedPluginSlots.size() << "\n";
        return false;
    }

    return true;
}

//==============================================================================
// Phase 5: Automation data tests
//==============================================================================

bool testAutomationLaneInterpolation()
{
    AutomationLane lane;
    lane.pluginId = "test:0";
    lane.parameterId = 0;
    lane.owningTrack = 0;

    // Empty lane returns default
    if (std::abs (lane.getValueAtRow (0.0f, 0.5f) - 0.5f) > 1.0e-6f)
    {
        std::cerr << "Empty lane should return default value\n";
        return false;
    }

    // Add points
    lane.setPoint (0, 0.0f);
    lane.setPoint (8, 1.0f);
    lane.setPoint (16, 0.5f);

    // At exact points
    if (std::abs (lane.getValueAtRow (0.0f) - 0.0f) > 1.0e-6f)
    {
        std::cerr << "Value at row 0 should be 0.0\n";
        return false;
    }
    if (std::abs (lane.getValueAtRow (8.0f) - 1.0f) > 1.0e-6f)
    {
        std::cerr << "Value at row 8 should be 1.0\n";
        return false;
    }
    if (std::abs (lane.getValueAtRow (16.0f) - 0.5f) > 1.0e-6f)
    {
        std::cerr << "Value at row 16 should be 0.5\n";
        return false;
    }

    // Linear interpolation midpoint
    float midVal = lane.getValueAtRow (4.0f);
    if (std::abs (midVal - 0.5f) > 1.0e-6f)
    {
        std::cerr << "Midpoint interpolation failed: expected 0.5, got " << midVal << "\n";
        return false;
    }

    // Before first point: hold at first point value
    if (std::abs (lane.getValueAtRow (-1.0f) - 0.0f) > 1.0e-6f)
    {
        std::cerr << "Before first point should hold at first value\n";
        return false;
    }

    // After last point: hold at last point value
    if (std::abs (lane.getValueAtRow (20.0f) - 0.5f) > 1.0e-6f)
    {
        std::cerr << "After last point should hold at last value\n";
        return false;
    }

    return true;
}

bool testAutomationLanePointOperations()
{
    AutomationLane lane;
    lane.pluginId = "test:0";
    lane.parameterId = 1;
    lane.owningTrack = 0;

    // Add points
    lane.setPoint (4, 0.3f);
    lane.setPoint (8, 0.7f);
    lane.setPoint (12, 0.1f);

    if (lane.points.size() != 3)
    {
        std::cerr << "Expected 3 points, got " << lane.points.size() << "\n";
        return false;
    }

    // Update existing point
    lane.setPoint (8, 0.9f);
    if (lane.points.size() != 3)
    {
        std::cerr << "Updating existing point should not add new one\n";
        return false;
    }
    if (std::abs (lane.points[1].value - 0.9f) > 1.0e-6f)
    {
        std::cerr << "Point value not updated correctly\n";
        return false;
    }

    // Remove point
    bool removed = lane.removePoint (8);
    if (! removed || lane.points.size() != 2)
    {
        std::cerr << "Point removal failed\n";
        return false;
    }

    // Remove non-existent point
    removed = lane.removePoint (99);
    if (removed)
    {
        std::cerr << "Should not have removed non-existent point\n";
        return false;
    }

    return true;
}

bool testAutomationDataSerializationRoundTrip()
{
    PatternData source;
    auto& pat = source.getCurrentPattern();

    // Add automation data
    auto& lane1 = pat.automationData.getOrCreateLane ("inst:0", 3, 0);
    lane1.setPoint (0, 0.0f);
    lane1.setPoint (16, 1.0f);
    lane1.setPoint (32, 0.5f);

    auto& lane2 = pat.automationData.getOrCreateLane ("insert:0:1", 7, 0);
    lane2.setPoint (4, 0.25f, AutomationCurveType::Step);
    lane2.setPoint (12, 0.75f);

    Arrangement arrangement;
    TrackLayout trackLayout;
    MixerState mixerState;
    DelayParams delayParams;
    ReverbParams reverbParams;
    std::map<int, juce::File> loadedSamples;
    std::map<int, InstrumentParams> instrumentParams;

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

    auto err = runProjectRoundTrip ("tracker_adjust_tests_automation",
                                    source, 120.0, 4,
                                    loadedSamples, instrumentParams,
                                    arrangement, trackLayout, mixerState,
                                    delayParams, reverbParams, 0, {},
                                    loaded, loadedBpm, loadedRpb,
                                    loadedSamplesOut, instrumentParamsOut,
                                    arrangementOut, trackLayoutOut,
                                    mixerStateOut, delayOut, reverbOut);
    if (err.isNotEmpty())
    {
        std::cerr << "Automation round-trip save/load failed: " << err << "\n";
        return false;
    }

    auto& loadedPat = loaded.getCurrentPattern();

    // Verify lane 1
    auto* loadedLane1 = loadedPat.automationData.findLane ("inst:0", 3);
    if (loadedLane1 == nullptr)
    {
        std::cerr << "Lane 1 not found after round-trip\n";
        return false;
    }
    if (loadedLane1->owningTrack != 0)
    {
        std::cerr << "Lane 1 owning track mismatch\n";
        return false;
    }
    if (loadedLane1->points.size() != 3)
    {
        std::cerr << "Lane 1 point count mismatch: expected 3, got " << loadedLane1->points.size() << "\n";
        return false;
    }
    if (loadedLane1->points[0].row != 0 || std::abs (loadedLane1->points[0].value - 0.0f) > 1.0e-5f)
    {
        std::cerr << "Lane 1 point 0 value mismatch\n";
        return false;
    }
    if (loadedLane1->points[1].row != 16 || std::abs (loadedLane1->points[1].value - 1.0f) > 1.0e-5f)
    {
        std::cerr << "Lane 1 point 1 value mismatch\n";
        return false;
    }
    if (loadedLane1->points[2].row != 32 || std::abs (loadedLane1->points[2].value - 0.5f) > 1.0e-5f)
    {
        std::cerr << "Lane 1 point 2 value mismatch\n";
        return false;
    }

    // Verify lane 2
    auto* loadedLane2 = loadedPat.automationData.findLane ("insert:0:1", 7);
    if (loadedLane2 == nullptr)
    {
        std::cerr << "Lane 2 not found after round-trip\n";
        return false;
    }
    if (loadedLane2->points.size() != 2)
    {
        std::cerr << "Lane 2 point count mismatch: expected 2, got " << loadedLane2->points.size() << "\n";
        return false;
    }
    if (loadedLane2->points[0].curveType != AutomationCurveType::Step)
    {
        std::cerr << "Lane 2 point 0 curve type mismatch (expected Step)\n";
        return false;
    }

    return true;
}

bool testPatternDuplicateClonesAutomation()
{
    PatternData patternData;

    // Add automation to pattern 0
    auto& pat = patternData.getCurrentPattern();
    auto& lane = pat.automationData.getOrCreateLane ("inst:0", 5, 0);
    lane.setPoint (0, 0.2f);
    lane.setPoint (32, 0.8f);

    // Duplicate the pattern
    patternData.duplicatePattern (0);

    if (patternData.getNumPatterns() != 2)
    {
        std::cerr << "Expected 2 patterns after duplicate, got " << patternData.getNumPatterns() << "\n";
        return false;
    }

    auto& copy = patternData.getPattern (1);

    // Verify automation was cloned
    auto* clonedLane = copy.automationData.findLane ("inst:0", 5);
    if (clonedLane == nullptr)
    {
        std::cerr << "Cloned pattern does not have automation lane\n";
        return false;
    }

    if (clonedLane->points.size() != 2)
    {
        std::cerr << "Cloned lane point count mismatch: expected 2, got " << clonedLane->points.size() << "\n";
        return false;
    }

    // Verify values
    if (clonedLane->points[0].row != 0 || std::abs (clonedLane->points[0].value - 0.2f) > 1.0e-6f)
    {
        std::cerr << "Cloned lane point 0 value mismatch\n";
        return false;
    }

    // Modify the cloned pattern's automation and verify original is unaffected
    clonedLane->setPoint (16, 0.5f);
    auto* origLane = patternData.getPattern (0).automationData.findLane ("inst:0", 5);
    if (origLane == nullptr || origLane->points.size() != 2)
    {
        std::cerr << "Modifying cloned automation affected original\n";
        return false;
    }

    return true;
}

bool testAutomationEmptySerializationRoundTrip()
{
    // Verify that patterns without automation survive round-trip without gaining automation
    PatternData source;
    // Don't add any automation data

    Arrangement arrangement;
    TrackLayout trackLayout;
    MixerState mixerState;
    DelayParams delayParams;
    ReverbParams reverbParams;
    std::map<int, juce::File> loadedSamples;
    std::map<int, InstrumentParams> instrumentParams;

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

    auto err = runProjectRoundTrip ("tracker_adjust_tests_empty_automation",
                                    source, 120.0, 4,
                                    loadedSamples, instrumentParams,
                                    arrangement, trackLayout, mixerState,
                                    delayParams, reverbParams, 0, {},
                                    loaded, loadedBpm, loadedRpb,
                                    loadedSamplesOut, instrumentParamsOut,
                                    arrangementOut, trackLayoutOut,
                                    mixerStateOut, delayOut, reverbOut);
    if (err.isNotEmpty())
    {
        std::cerr << "Empty automation round-trip failed: " << err << "\n";
        return false;
    }

    if (! loaded.getCurrentPattern().automationData.isEmpty())
    {
        std::cerr << "Empty automation should remain empty after round-trip\n";
        return false;
    }

    return true;
}

//==============================================================================
// Phase 6: Regression and stabilization tests
//==============================================================================

bool testInsertSlotStateRoundTrip()
{
    // Verify insert plugin slots (name, identifier, format, bypassed, pluginState)
    // survive a full save/load round-trip via MixerState.
    PatternData source;
    source.getPattern (0).resize (8);

    Arrangement arrangement;
    TrackLayout trackLayout;
    DelayParams delayParams;
    ReverbParams reverbParams;
    std::map<int, juce::File> loadedSamples;
    std::map<int, InstrumentParams> instrumentParams;

    MixerState mixerState;

    // Track 0: one insert slot with plugin state and bypassed=false
    {
        InsertSlotState slot;
        slot.pluginName = "CompressorX";
        slot.pluginIdentifier = "com.test.compressorx";
        slot.pluginFormatName = "VST3";
        slot.bypassed = false;
        // Create a simple ValueTree as plugin state
        juce::ValueTree state ("PluginState");
        state.setProperty ("threshold", -12.0, nullptr);
        state.setProperty ("ratio", 4.0, nullptr);
        slot.pluginState = state;
        mixerState.insertSlots[0].push_back (std::move (slot));
    }

    // Track 0: second insert slot, bypassed
    {
        InsertSlotState slot;
        slot.pluginName = "DelayFX";
        slot.pluginIdentifier = "com.test.delayfx";
        slot.pluginFormatName = "AudioUnit";
        slot.bypassed = true;
        mixerState.insertSlots[0].push_back (std::move (slot));
    }

    // Track 5: one insert slot
    {
        InsertSlotState slot;
        slot.pluginName = "ReverbPlus";
        slot.pluginIdentifier = "com.test.reverbplus";
        slot.pluginFormatName = "VST3";
        slot.bypassed = false;
        mixerState.insertSlots[5].push_back (std::move (slot));
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

    auto err = runProjectRoundTrip ("tracker_adjust_insert_slot_rt",
                                    source, 120.0, 4,
                                    loadedSamples, instrumentParams,
                                    arrangement, trackLayout, mixerState,
                                    delayParams, reverbParams, 0, {},
                                    loaded, loadedBpm, loadedRpb,
                                    loadedSamplesOut, instrumentParamsOut,
                                    arrangementOut, trackLayoutOut,
                                    mixerStateOut, delayOut, reverbOut);
    if (err.isNotEmpty())
    {
        std::cerr << "Insert slot round-trip failed: " << err << "\n";
        return false;
    }

    // Track 0 should have 2 insert slots
    if (mixerStateOut.insertSlots[0].size() != 2)
    {
        std::cerr << "Track 0 insert slot count mismatch: expected 2, got "
                  << mixerStateOut.insertSlots[0].size() << "\n";
        return false;
    }

    // Verify first slot
    {
        auto& slot = mixerStateOut.insertSlots[0][0];
        if (slot.pluginName != "CompressorX")
        {
            std::cerr << "Slot 0 name mismatch: " << slot.pluginName << "\n";
            return false;
        }
        if (slot.pluginIdentifier != "com.test.compressorx")
        {
            std::cerr << "Slot 0 identifier mismatch\n";
            return false;
        }
        if (slot.pluginFormatName != "VST3")
        {
            std::cerr << "Slot 0 format mismatch\n";
            return false;
        }
        if (slot.bypassed)
        {
            std::cerr << "Slot 0 should not be bypassed\n";
            return false;
        }
        if (! slot.pluginState.isValid())
        {
            std::cerr << "Slot 0 plugin state should be valid\n";
            return false;
        }
        double threshold = slot.pluginState.getProperty ("threshold", 0.0);
        double ratio = slot.pluginState.getProperty ("ratio", 0.0);
        if (std::abs (threshold - (-12.0)) > 1.0e-6 || std::abs (ratio - 4.0) > 1.0e-6)
        {
            std::cerr << "Slot 0 plugin state values mismatch\n";
            return false;
        }
    }

    // Verify second slot (bypassed)
    {
        auto& slot = mixerStateOut.insertSlots[0][1];
        if (slot.pluginName != "DelayFX")
        {
            std::cerr << "Slot 1 name mismatch\n";
            return false;
        }
        if (! slot.bypassed)
        {
            std::cerr << "Slot 1 should be bypassed\n";
            return false;
        }
        if (slot.pluginFormatName != "AudioUnit")
        {
            std::cerr << "Slot 1 format mismatch\n";
            return false;
        }
    }

    // Track 5 should have 1 insert slot
    if (mixerStateOut.insertSlots[5].size() != 1)
    {
        std::cerr << "Track 5 insert slot count mismatch\n";
        return false;
    }
    if (mixerStateOut.insertSlots[5][0].pluginName != "ReverbPlus")
    {
        std::cerr << "Track 5 slot 0 name mismatch\n";
        return false;
    }

    // Tracks without inserts should remain empty
    if (! mixerStateOut.insertSlots[1].empty()
        || ! mixerStateOut.insertSlots[2].empty())
    {
        std::cerr << "Tracks without inserts should have empty slot vectors\n";
        return false;
    }

    return true;
}

bool testAutomationStepCurveInterpolation()
{
    // Verify that Step curve type holds the value flat until the next point
    AutomationLane lane;
    lane.pluginId = "test:step";
    lane.parameterId = 0;
    lane.owningTrack = 0;

    lane.setPoint (0, 0.2f, AutomationCurveType::Step);
    lane.setPoint (8, 0.8f, AutomationCurveType::Step);
    lane.setPoint (16, 0.4f);

    // At row 0: should be 0.2
    if (std::abs (lane.getValueAtRow (0.0f) - 0.2f) > 1.0e-6f)
    {
        std::cerr << "Step: value at row 0 should be 0.2\n";
        return false;
    }

    // Between 0 and 8 (Step mode): should hold at 0.2 (the value of point A)
    if (std::abs (lane.getValueAtRow (4.0f) - 0.2f) > 1.0e-6f)
    {
        std::cerr << "Step: value at row 4 should hold at 0.2, got "
                  << lane.getValueAtRow (4.0f) << "\n";
        return false;
    }

    // At row 7.99 (still in step region): should hold at 0.2
    if (std::abs (lane.getValueAtRow (7.99f) - 0.2f) > 1.0e-6f)
    {
        std::cerr << "Step: value at row 7.99 should hold at 0.2\n";
        return false;
    }

    // At row 8: should be 0.8
    if (std::abs (lane.getValueAtRow (8.0f) - 0.8f) > 1.0e-6f)
    {
        std::cerr << "Step: value at row 8 should be 0.8\n";
        return false;
    }

    // Between 8 and 16 (Step mode): should hold at 0.8
    if (std::abs (lane.getValueAtRow (12.0f) - 0.8f) > 1.0e-6f)
    {
        std::cerr << "Step: value at row 12 should hold at 0.8, got "
                  << lane.getValueAtRow (12.0f) << "\n";
        return false;
    }

    // At row 16: should be 0.4 (linear point, but it's the last point)
    if (std::abs (lane.getValueAtRow (16.0f) - 0.4f) > 1.0e-6f)
    {
        std::cerr << "Step: value at row 16 should be 0.4\n";
        return false;
    }

    return true;
}

bool testAutomationRemovePointNearBehavior()
{
    AutomationLane lane;
    lane.pluginId = "test:near";
    lane.parameterId = 0;
    lane.owningTrack = 0;

    lane.setPoint (0, 0.0f);
    lane.setPoint (4, 0.5f);
    lane.setPoint (8, 1.0f);
    lane.setPoint (16, 0.3f);

    // Remove near row 5 with tolerance 1: should remove row 4
    bool removed = lane.removePointNear (5, 1);
    if (! removed)
    {
        std::cerr << "removePointNear(5, 1) should have removed a point\n";
        return false;
    }
    if (lane.points.size() != 3)
    {
        std::cerr << "Expected 3 points after removePointNear, got " << lane.points.size() << "\n";
        return false;
    }
    // Verify that the row-4 point was removed
    for (const auto& p : lane.points)
    {
        if (p.row == 4)
        {
            std::cerr << "Row 4 should have been removed by removePointNear(5, 1)\n";
            return false;
        }
    }

    // Remove near row 100 with tolerance 1: nothing nearby, should fail
    removed = lane.removePointNear (100, 1);
    if (removed)
    {
        std::cerr << "removePointNear(100, 1) should not have removed anything\n";
        return false;
    }

    // Remove exact match: row 8 is within tolerance 0
    removed = lane.removePointNear (8, 0);
    if (removed)
    {
        // tolerance 0 means bestDist must be < 1 (i.e., exactly 0)
        // removePointNear uses bestDist < tolerance + 1, so tolerance=0 means dist < 1
        // dist=0 for exact match qualifies
    }
    else
    {
        std::cerr << "removePointNear(8, 0) should have removed exact match\n";
        return false;
    }

    return true;
}

bool testPatternAutomationDataOperations()
{
    PatternAutomationData data;

    // getOrCreateLane should create new lanes
    auto& lane1 = data.getOrCreateLane ("plug:A", 0, 0);
    lane1.setPoint (0, 0.5f);
    lane1.setPoint (8, 0.9f);

    auto& lane2 = data.getOrCreateLane ("plug:A", 1, 0);
    lane2.setPoint (4, 0.3f);

    auto& lane3 = data.getOrCreateLane ("plug:B", 0, 1);
    lane3.setPoint (0, 0.1f);

    data.getOrCreateLane ("plug:C", 2, 2);
    // Leave this lane empty (no points)

    if (data.lanes.size() != 4)
    {
        std::cerr << "Expected 4 lanes, got " << data.lanes.size() << "\n";
        return false;
    }

    // getOrCreateLane should return existing lane, not create new
    // (Note: we cannot compare pointers across getOrCreateLane calls because
    // the internal vector may have reallocated. Instead, verify by lane count
    // and by checking the returned lane has the expected data.)
    auto& existingLane = data.getOrCreateLane ("plug:A", 0, 0);
    if (data.lanes.size() != 4)
    {
        std::cerr << "getOrCreateLane on existing should not add new lane\n";
        return false;
    }
    // The returned lane should be the same as the one we created earlier
    if (existingLane.points.size() != 2
        || existingLane.pluginId != "plug:A"
        || existingLane.parameterId != 0
        || existingLane.owningTrack != 0)
    {
        std::cerr << "getOrCreateLane should return the existing lane with original data\n";
        return false;
    }

    // findLane
    auto* found = data.findLane ("plug:B", 0);
    if (found == nullptr || found->owningTrack != 1)
    {
        std::cerr << "findLane should find plug:B param 0 on track 1\n";
        return false;
    }
    auto* notFound = data.findLane ("plug:Z", 99);
    if (notFound != nullptr)
    {
        std::cerr << "findLane should return nullptr for non-existent lane\n";
        return false;
    }

    // removeEmptyLanes: lane4 is empty, should be removed
    data.removeEmptyLanes();
    if (data.lanes.size() != 3)
    {
        std::cerr << "removeEmptyLanes should have removed 1 empty lane, got "
                  << data.lanes.size() << " lanes remaining\n";
        return false;
    }
    // Verify lane4 (plug:C, param 2) is gone
    if (data.findLane ("plug:C", 2) != nullptr)
    {
        std::cerr << "Empty lane plug:C should have been removed\n";
        return false;
    }

    // removeAllLanesForTrack: remove track 0 lanes (lane1 and lane2)
    data.removeAllLanesForTrack (0);
    if (data.lanes.size() != 1)
    {
        std::cerr << "removeAllLanesForTrack(0) should leave 1 lane, got "
                  << data.lanes.size() << "\n";
        return false;
    }
    if (data.findLane ("plug:A", 0) != nullptr || data.findLane ("plug:A", 1) != nullptr)
    {
        std::cerr << "Track 0 lanes should have been removed\n";
        return false;
    }
    if (data.findLane ("plug:B", 0) == nullptr)
    {
        std::cerr << "Track 1 lane plug:B should still exist\n";
        return false;
    }

    // removeLane
    bool removedLane = data.removeLane ("plug:B", 0);
    if (! removedLane || ! data.isEmpty())
    {
        std::cerr << "removeLane should have removed the last lane\n";
        return false;
    }

    // removeLane on non-existent
    removedLane = data.removeLane ("plug:Z", 0);
    if (removedLane)
    {
        std::cerr << "removeLane should return false for non-existent lane\n";
        return false;
    }

    return true;
}

bool testCombinedNoteLaneAutomationInsertRoundTrip()
{
    // Full roundtrip: note lanes + automation data + insert slots + plugin instrument slots
    PatternData source;
    source.getPattern (0).name = "Combined";
    source.getPattern (0).resize (32);

    // Set up multi-lane note data on track 0
    {
        Cell cell;
        cell.note = 60;
        cell.instrument = 1;
        cell.volume = 100;
        NoteSlot lane1;
        lane1.note = 64;
        lane1.instrument = 2;
        lane1.volume = 80;
        cell.setNoteLane (1, lane1);
        cell.getFxSlot (0).setSymbolicCommand ('T', 0x04);
        source.getPattern (0).setCell (0, 0, cell);
    }

    // Set up note data on track 3 with note-off on lane 1
    {
        Cell cell;
        cell.note = 48;
        cell.instrument = 0;
        NoteSlot lane1;
        lane1.note = 255; // OFF
        cell.setNoteLane (1, lane1);
        source.getPattern (0).setCell (8, 3, cell);
    }

    // Set up automation data
    auto& autoLane1 = source.getPattern (0).automationData.getOrCreateLane ("inst:0", 3, 0);
    autoLane1.setPoint (0, 0.1f);
    autoLane1.setPoint (16, 0.9f, AutomationCurveType::Step);
    autoLane1.setPoint (31, 0.5f);

    auto& autoLane2 = source.getPattern (0).automationData.getOrCreateLane ("insert:3:0", 7, 3);
    autoLane2.setPoint (4, 0.25f);
    autoLane2.setPoint (28, 0.75f);

    TrackLayout trackLayout;
    trackLayout.setTrackNoteLaneCount (0, 3);
    trackLayout.setTrackNoteLaneCount (3, 2);
    trackLayout.setTrackFxLaneCount (0, 2);

    // Insert slots
    MixerState mixerState;
    {
        InsertSlotState slot;
        slot.pluginName = "TestEQ";
        slot.pluginIdentifier = "com.test.eq";
        slot.pluginFormatName = "VST3";
        slot.bypassed = false;
        juce::ValueTree state ("EQState");
        state.setProperty ("lowGain", 3.5, nullptr);
        slot.pluginState = state;
        mixerState.insertSlots[3].push_back (std::move (slot));
    }
    mixerState.tracks[0].volume = -3.0;
    mixerState.tracks[0].pan = 15;

    // Plugin instrument slots
    std::map<int, InstrumentSlotInfo> pluginSlots;
    {
        InstrumentSlotInfo info;
        juce::PluginDescription desc;
        desc.name = "CombinedSynth";
        desc.pluginFormatName = "VST3";
        desc.fileOrIdentifier = "com.test.combinedsynth";
        desc.uniqueId = 7777;
        desc.isInstrument = true;
        info.setPlugin (desc, 5);
        pluginSlots[10] = info;
    }

    Arrangement arrangement;
    arrangement.addEntry (0, 2);

    DelayParams delayParams;
    ReverbParams reverbParams;
    std::map<int, juce::File> loadedSamples;
    std::map<int, InstrumentParams> instrumentParams;

    // Save
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getNonexistentChildFile ("combined_roundtrip", ".tkadj", false);

    auto saveErr = ProjectSerializer::saveToFile (file, source, 135.0, 6,
                                                   loadedSamples, instrumentParams,
                                                   arrangement, trackLayout, mixerState,
                                                   delayParams, reverbParams, 1, {},
                                                   &pluginSlots);
    if (saveErr.isNotEmpty())
    {
        file.deleteFile();
        std::cerr << "Combined save failed: " << saveErr << "\n";
        return false;
    }

    // Load
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
    int loadedFollowMode = 0;
    std::map<int, InstrumentSlotInfo> loadedPluginSlots;

    auto loadErr = ProjectSerializer::loadFromFile (file, loaded, loadedBpm, loadedRpb,
                                                     loadedSamplesOut, instrumentParamsOut,
                                                     arrangementOut, trackLayoutOut,
                                                     mixerStateOut, delayOut, reverbOut,
                                                     &loadedFollowMode, nullptr,
                                                     &loadedPluginSlots);
    file.deleteFile();

    if (loadErr.isNotEmpty())
    {
        std::cerr << "Combined load failed: " << loadErr << "\n";
        return false;
    }

    // Verify BPM/RPB
    if (std::abs (loadedBpm - 135.0) > 1.0e-6 || loadedRpb != 6)
    {
        std::cerr << "Combined: BPM/RPB mismatch\n";
        return false;
    }

    // Verify follow mode
    if (loadedFollowMode != 1)
    {
        std::cerr << "Combined: follow mode mismatch\n";
        return false;
    }

    // Verify note lane counts
    if (trackLayoutOut.getTrackNoteLaneCount (0) != 3
        || trackLayoutOut.getTrackNoteLaneCount (3) != 2)
    {
        std::cerr << "Combined: note lane counts mismatch\n";
        return false;
    }

    // Verify FX lane counts
    if (trackLayoutOut.getTrackFxLaneCount (0) != 2)
    {
        std::cerr << "Combined: FX lane counts mismatch\n";
        return false;
    }

    // Verify note data
    {
        const auto& cell = loaded.getPattern (0).getCell (0, 0);
        auto lane0 = cell.getNoteLane (0);
        if (lane0.note != 60 || lane0.instrument != 1 || lane0.volume != 100)
        {
            std::cerr << "Combined: track 0 row 0 lane 0 mismatch\n";
            return false;
        }
        auto lane1 = cell.getNoteLane (1);
        if (lane1.note != 64 || lane1.instrument != 2 || lane1.volume != 80)
        {
            std::cerr << "Combined: track 0 row 0 lane 1 mismatch\n";
            return false;
        }
        if (cell.getFxSlot (0).fxCommand != 'T' || cell.getFxSlot (0).fxParam != 0x04)
        {
            std::cerr << "Combined: track 0 row 0 FX mismatch\n";
            return false;
        }
    }

    // Verify track 3 note-off on lane 1
    {
        const auto& cell = loaded.getPattern (0).getCell (8, 3);
        if (cell.note != 48)
        {
            std::cerr << "Combined: track 3 row 8 lane 0 note mismatch\n";
            return false;
        }
        auto lane1 = cell.getNoteLane (1);
        if (lane1.note != 255)
        {
            std::cerr << "Combined: track 3 row 8 lane 1 OFF not preserved\n";
            return false;
        }
    }

    // Verify automation data
    auto* loadedAutoLane1 = loaded.getPattern (0).automationData.findLane ("inst:0", 3);
    if (loadedAutoLane1 == nullptr || loadedAutoLane1->points.size() != 3)
    {
        std::cerr << "Combined: automation lane 1 missing or wrong point count\n";
        return false;
    }
    if (loadedAutoLane1->points[1].curveType != AutomationCurveType::Step)
    {
        std::cerr << "Combined: automation lane 1 point 1 curve type mismatch\n";
        return false;
    }
    if (std::abs (loadedAutoLane1->points[0].value - 0.1f) > 1.0e-5f
        || std::abs (loadedAutoLane1->points[1].value - 0.9f) > 1.0e-5f
        || std::abs (loadedAutoLane1->points[2].value - 0.5f) > 1.0e-5f)
    {
        std::cerr << "Combined: automation lane 1 values mismatch\n";
        return false;
    }

    auto* loadedAutoLane2 = loaded.getPattern (0).automationData.findLane ("insert:3:0", 7);
    if (loadedAutoLane2 == nullptr || loadedAutoLane2->points.size() != 2)
    {
        std::cerr << "Combined: automation lane 2 missing or wrong point count\n";
        return false;
    }

    // Verify insert slots
    if (mixerStateOut.insertSlots[3].size() != 1
        || mixerStateOut.insertSlots[3][0].pluginName != "TestEQ")
    {
        std::cerr << "Combined: insert slot mismatch\n";
        return false;
    }
    if (! mixerStateOut.insertSlots[3][0].pluginState.isValid())
    {
        std::cerr << "Combined: insert slot state should be valid\n";
        return false;
    }
    double lowGain = mixerStateOut.insertSlots[3][0].pluginState.getProperty ("lowGain", 0.0);
    if (std::abs (lowGain - 3.5) > 1.0e-6)
    {
        std::cerr << "Combined: insert slot state value mismatch\n";
        return false;
    }

    // Verify mixer state
    if (std::abs (mixerStateOut.tracks[0].volume - (-3.0)) > 1.0e-6
        || mixerStateOut.tracks[0].pan != 15)
    {
        std::cerr << "Combined: mixer state mismatch\n";
        return false;
    }

    // Verify plugin instrument slots
    if (loadedPluginSlots.size() != 1)
    {
        std::cerr << "Combined: expected 1 plugin slot, got " << loadedPluginSlots.size() << "\n";
        return false;
    }
    auto itPlugin = loadedPluginSlots.find (10);
    if (itPlugin == loadedPluginSlots.end()
        || ! itPlugin->second.isPlugin()
        || itPlugin->second.ownerTrack != 5
        || itPlugin->second.pluginDescription.name != "CombinedSynth")
    {
        std::cerr << "Combined: plugin instrument slot mismatch\n";
        return false;
    }

    // Verify arrangement
    if (arrangementOut.getNumEntries() != 1
        || arrangementOut.getEntry (0).patternIndex != 0
        || arrangementOut.getEntry (0).repeats != 2)
    {
        std::cerr << "Combined: arrangement mismatch\n";
        return false;
    }

    return true;
}

bool testVersionMigrationPreV6LoadsSafely()
{
    // Simulate loading a pre-v6 file (v4 format) that has no note lanes,
    // no insert plugins, no plugin instruments, and no automation data.
    // The file should load with all defaults for those features.

    // Manually create a v4-style XML file
    juce::ValueTree root ("TrackerAdjustProject");
    root.setProperty ("version", 4, nullptr);

    juce::ValueTree settings ("Settings");
    settings.setProperty ("bpm", 140.0, nullptr);
    settings.setProperty ("rowsPerBeat", 4, nullptr);
    settings.setProperty ("currentPattern", 0, nullptr);
    root.addChild (settings, -1, nullptr);

    // Mixer state (V4 feature)
    juce::ValueTree mixTree ("Mixer");
    {
        juce::ValueTree trackTree ("Track");
        trackTree.setProperty ("index", 0, nullptr);
        trackTree.setProperty ("volume", -6.0, nullptr);
        trackTree.setProperty ("pan", 25, nullptr);
        trackTree.setProperty ("muted", true, nullptr);
        trackTree.setProperty ("eqLow", 2.0, nullptr);
        trackTree.setProperty ("eqMid", -1.5, nullptr);
        trackTree.setProperty ("eqHigh", 3.0, nullptr);
        trackTree.setProperty ("eqMidFreq", 2500.0, nullptr);
        trackTree.setProperty ("compThresh", -20.0, nullptr);
        trackTree.setProperty ("compRatio", 4.0, nullptr);
        trackTree.setProperty ("compAttack", 5.0, nullptr);
        trackTree.setProperty ("compRelease", 200.0, nullptr);
        trackTree.setProperty ("reverbSend", -12.0, nullptr);
        trackTree.setProperty ("delaySend", -18.0, nullptr);
        mixTree.addChild (trackTree, -1, nullptr);
    }
    root.addChild (mixTree, -1, nullptr);

    // Pattern with basic note data (no note lanes, no automation)
    juce::ValueTree patterns ("Patterns");
    {
        juce::ValueTree patTree ("Pattern");
        patTree.setProperty ("name", "OldPattern", nullptr);
        patTree.setProperty ("numRows", 32, nullptr);

        juce::ValueTree rowTree ("Row");
        rowTree.setProperty ("index", 0, nullptr);

        juce::ValueTree cellTree ("Cell");
        cellTree.setProperty ("track", 0, nullptr);
        cellTree.setProperty ("note", 60, nullptr);
        cellTree.setProperty ("inst", 5, nullptr);
        cellTree.setProperty ("vol", 100, nullptr);
        cellTree.setProperty ("fxc", "T", nullptr);
        cellTree.setProperty ("fxp", 0x04, nullptr);
        rowTree.addChild (cellTree, -1, nullptr);

        patTree.addChild (rowTree, -1, nullptr);
        patterns.addChild (patTree, -1, nullptr);
    }
    root.addChild (patterns, -1, nullptr);

    // Send effects (V4 feature)
    juce::ValueTree sendTree ("SendEffects");
    {
        juce::ValueTree delayTree ("Delay");
        delayTree.setProperty ("time", 300.0, nullptr);
        delayTree.setProperty ("feedback", 55.0, nullptr);
        delayTree.setProperty ("wet", 40.0, nullptr);
        sendTree.addChild (delayTree, -1, nullptr);

        juce::ValueTree reverbTree ("Reverb");
        reverbTree.setProperty ("roomSize", 60.0, nullptr);
        reverbTree.setProperty ("wet", 35.0, nullptr);
        sendTree.addChild (reverbTree, -1, nullptr);
    }
    root.addChild (sendTree, -1, nullptr);

    // Write this V4 file to disk
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getNonexistentChildFile ("v4_migration_test", ".tkadj", false);

    auto xml = root.createXml();
    if (xml == nullptr || ! xml->writeTo (file))
    {
        file.deleteFile();
        std::cerr << "Failed to write v4 migration test file\n";
        return false;
    }

    // Load as current version
    PatternData loaded;
    double loadedBpm = 0.0;
    int loadedRpb = 0;
    std::map<int, juce::File> loadedSamples;
    std::map<int, InstrumentParams> instrumentParams;
    Arrangement arrangement;
    TrackLayout trackLayout;
    MixerState mixerState;
    DelayParams delayParams;
    ReverbParams reverbParams;
    std::map<int, InstrumentSlotInfo> pluginSlots;

    auto loadErr = ProjectSerializer::loadFromFile (file, loaded, loadedBpm, loadedRpb,
                                                     loadedSamples, instrumentParams,
                                                     arrangement, trackLayout, mixerState,
                                                     delayParams, reverbParams, nullptr,
                                                     nullptr, &pluginSlots);
    file.deleteFile();

    if (loadErr.isNotEmpty())
    {
        std::cerr << "V4 migration load failed: " << loadErr << "\n";
        return false;
    }

    // Verify basic data loaded correctly
    if (std::abs (loadedBpm - 140.0) > 1.0e-6)
    {
        std::cerr << "V4 migration: BPM mismatch\n";
        return false;
    }
    if (loadedRpb != 4)
    {
        std::cerr << "V4 migration: RPB mismatch\n";
        return false;
    }

    // Verify pattern data
    if (loaded.getNumPatterns() != 1)
    {
        std::cerr << "V4 migration: expected 1 pattern\n";
        return false;
    }
    if (loaded.getPattern (0).name != "OldPattern")
    {
        std::cerr << "V4 migration: pattern name mismatch\n";
        return false;
    }
    if (loaded.getPattern (0).numRows != 32)
    {
        std::cerr << "V4 migration: pattern row count mismatch\n";
        return false;
    }

    auto cell = loaded.getPattern (0).getCell (0, 0);
    if (cell.note != 60 || cell.instrument != 5 || cell.volume != 100)
    {
        std::cerr << "V4 migration: cell data mismatch\n";
        return false;
    }
    if (cell.getFxSlot (0).fxCommand != 'T' || cell.getFxSlot (0).fxParam != 0x04)
    {
        std::cerr << "V4 migration: FX data mismatch\n";
        return false;
    }

    // Verify mixer state loaded correctly (V4 feature)
    if (std::abs (mixerState.tracks[0].volume - (-6.0)) > 1.0e-6
        || mixerState.tracks[0].pan != 25
        || ! mixerState.tracks[0].muted)
    {
        std::cerr << "V4 migration: mixer state mismatch\n";
        return false;
    }

    // V6+ features should have defaults:
    // Note lane counts should all be 1
    for (int i = 0; i < kNumTracks; ++i)
    {
        if (trackLayout.getTrackNoteLaneCount (i) != 1)
        {
            std::cerr << "V4 migration: track " << i << " note lane count should default to 1\n";
            return false;
        }
    }

    // V7+ features: no insert plugins
    for (int i = 0; i < kNumTracks; ++i)
    {
        if (! mixerState.insertSlots[static_cast<size_t> (i)].empty())
        {
            std::cerr << "V4 migration: track " << i << " should have no insert slots\n";
            return false;
        }
    }

    // V7+ features: no plugin instruments
    if (! pluginSlots.empty())
    {
        std::cerr << "V4 migration: should have no plugin instrument slots\n";
        return false;
    }

    // V8+ features: no automation data
    if (! loaded.getPattern (0).automationData.isEmpty())
    {
        std::cerr << "V4 migration: should have no automation data\n";
        return false;
    }

    // Send effects should have loaded
    if (std::abs (delayParams.feedback - 55.0) > 1.0e-6
        || std::abs (reverbParams.roomSize - 60.0) > 1.0e-6)
    {
        std::cerr << "V4 migration: send effects params mismatch\n";
        return false;
    }

    return true;
}

bool testAutomationCloneIsDeepCopy()
{
    // Verify that PatternAutomationData::clone() creates a true deep copy
    PatternAutomationData original;
    auto& lane = original.getOrCreateLane ("plug:A", 0, 0);
    lane.setPoint (0, 0.3f);
    lane.setPoint (8, 0.7f);

    auto copy = original.clone();

    // Verify copy has the same data
    if (copy.lanes.size() != 1 || copy.lanes[0].points.size() != 2)
    {
        std::cerr << "Clone should have same structure as original\n";
        return false;
    }

    // Modify original and verify copy is unaffected
    lane.setPoint (16, 1.0f);
    original.getOrCreateLane ("plug:B", 1, 1);

    if (copy.lanes.size() != 1)
    {
        std::cerr << "Modifying original should not affect clone (lane count)\n";
        return false;
    }
    if (copy.lanes[0].points.size() != 2)
    {
        std::cerr << "Modifying original should not affect clone (point count)\n";
        return false;
    }

    // Modify copy and verify original is unaffected
    copy.lanes[0].setPoint (4, 0.5f);
    if (original.lanes[0].points.size() != 3)
    {
        std::cerr << "Modifying clone should not affect original\n";
        return false;
    }

    return true;
}

bool testAutomationLaneValueClamping()
{
    // Verify that setPoint clamps values to [0.0, 1.0]
    AutomationLane lane;
    lane.pluginId = "test:clamp";
    lane.parameterId = 0;
    lane.owningTrack = 0;

    lane.setPoint (0, -0.5f);   // Should clamp to 0.0
    lane.setPoint (8, 1.5f);    // Should clamp to 1.0
    lane.setPoint (16, 0.5f);   // Normal value

    if (lane.points.size() != 3)
    {
        std::cerr << "Expected 3 points after clamped setPoint calls\n";
        return false;
    }

    if (std::abs (lane.points[0].value - 0.0f) > 1.0e-6f)
    {
        std::cerr << "Negative value should be clamped to 0.0, got " << lane.points[0].value << "\n";
        return false;
    }

    if (std::abs (lane.points[1].value - 1.0f) > 1.0e-6f)
    {
        std::cerr << "Value > 1.0 should be clamped to 1.0, got " << lane.points[1].value << "\n";
        return false;
    }

    if (std::abs (lane.points[2].value - 0.5f) > 1.0e-6f)
    {
        std::cerr << "Normal value should be preserved\n";
        return false;
    }

    return true;
}

bool testMultiPatternAutomationRoundTrip()
{
    // Verify that automation data on multiple patterns survives round-trip
    PatternData source;
    source.getPattern (0).name = "Pat0";
    source.getPattern (0).resize (16);
    source.addPattern (32);
    source.getPattern (1).name = "Pat1";

    // Pattern 0: one automation lane
    auto& lane0 = source.getPattern (0).automationData.getOrCreateLane ("inst:0", 1, 0);
    lane0.setPoint (0, 0.0f);
    lane0.setPoint (15, 1.0f);

    // Pattern 1: two automation lanes
    auto& lane1a = source.getPattern (1).automationData.getOrCreateLane ("inst:1", 0, 1);
    lane1a.setPoint (0, 0.5f);
    lane1a.setPoint (31, 0.5f);

    auto& lane1b = source.getPattern (1).automationData.getOrCreateLane ("insert:0:0", 2, 0);
    lane1b.setPoint (8, 0.3f, AutomationCurveType::Step);
    lane1b.setPoint (24, 0.9f);

    Arrangement arrangement;
    arrangement.addEntry (0, 1);
    arrangement.addEntry (1, 2);

    TrackLayout trackLayout;
    MixerState mixerState;
    DelayParams delayParams;
    ReverbParams reverbParams;
    std::map<int, juce::File> loadedSamples;
    std::map<int, InstrumentParams> instrumentParams;

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

    auto err = runProjectRoundTrip ("tracker_adjust_multi_pat_auto",
                                    source, 120.0, 4,
                                    loadedSamples, instrumentParams,
                                    arrangement, trackLayout, mixerState,
                                    delayParams, reverbParams, 0, {},
                                    loaded, loadedBpm, loadedRpb,
                                    loadedSamplesOut, instrumentParamsOut,
                                    arrangementOut, trackLayoutOut,
                                    mixerStateOut, delayOut, reverbOut);
    if (err.isNotEmpty())
    {
        std::cerr << "Multi-pattern automation round-trip failed: " << err << "\n";
        return false;
    }

    if (loaded.getNumPatterns() != 2)
    {
        std::cerr << "Expected 2 patterns, got " << loaded.getNumPatterns() << "\n";
        return false;
    }

    // Verify pattern 0 automation
    auto* loadedLane0 = loaded.getPattern (0).automationData.findLane ("inst:0", 1);
    if (loadedLane0 == nullptr || loadedLane0->points.size() != 2)
    {
        std::cerr << "Pattern 0 automation lane missing or wrong point count\n";
        return false;
    }

    // Verify pattern 1 automation
    if (loaded.getPattern (1).automationData.lanes.size() != 2)
    {
        std::cerr << "Pattern 1 should have 2 automation lanes, got "
                  << loaded.getPattern (1).automationData.lanes.size() << "\n";
        return false;
    }

    auto* loadedLane1a = loaded.getPattern (1).automationData.findLane ("inst:1", 0);
    if (loadedLane1a == nullptr || loadedLane1a->owningTrack != 1)
    {
        std::cerr << "Pattern 1 lane 1a missing or wrong owning track\n";
        return false;
    }

    auto* loadedLane1b = loaded.getPattern (1).automationData.findLane ("insert:0:0", 2);
    if (loadedLane1b == nullptr || loadedLane1b->points.size() != 2)
    {
        std::cerr << "Pattern 1 lane 1b missing or wrong point count\n";
        return false;
    }
    if (loadedLane1b->points[0].curveType != AutomationCurveType::Step)
    {
        std::cerr << "Pattern 1 lane 1b point 0 curve type should be Step\n";
        return false;
    }

    return true;
}

bool testInsertSlotMaxCapacity()
{
    // Verify that loading more than kMaxInsertSlots (8) per track is handled safely
    MixerState mixerState;

    // Add exactly kMaxInsertSlots to track 0
    for (int i = 0; i < kMaxInsertSlots; ++i)
    {
        InsertSlotState slot;
        slot.pluginName = "Plugin" + juce::String (i);
        slot.pluginIdentifier = "com.test.plugin" + juce::String (i);
        slot.pluginFormatName = "VST3";
        mixerState.insertSlots[0].push_back (std::move (slot));
    }

    PatternData source;
    Arrangement arrangement;
    TrackLayout trackLayout;
    DelayParams delayParams;
    ReverbParams reverbParams;
    std::map<int, juce::File> loadedSamples;
    std::map<int, InstrumentParams> instrumentParams;

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

    auto err = runProjectRoundTrip ("tracker_adjust_max_inserts",
                                    source, 120.0, 4,
                                    loadedSamples, instrumentParams,
                                    arrangement, trackLayout, mixerState,
                                    delayParams, reverbParams, 0, {},
                                    loaded, loadedBpm, loadedRpb,
                                    loadedSamplesOut, instrumentParamsOut,
                                    arrangementOut, trackLayoutOut,
                                    mixerStateOut, delayOut, reverbOut);
    if (err.isNotEmpty())
    {
        std::cerr << "Max inserts round-trip failed: " << err << "\n";
        return false;
    }

    // All 8 slots should have survived
    if (static_cast<int> (mixerStateOut.insertSlots[0].size()) != kMaxInsertSlots)
    {
        std::cerr << "Expected " << kMaxInsertSlots << " insert slots, got "
                  << mixerStateOut.insertSlots[0].size() << "\n";
        return false;
    }

    // Verify all names
    for (int i = 0; i < kMaxInsertSlots; ++i)
    {
        if (mixerStateOut.insertSlots[0][static_cast<size_t> (i)].pluginName
            != "Plugin" + juce::String (i))
        {
            std::cerr << "Insert slot " << i << " name mismatch\n";
            return false;
        }
    }

    return true;
}

bool testAutomationLaneEquality()
{
    // Verify AutomationLane and PatternAutomationData equality operators
    AutomationLane a, b;
    a.pluginId = "plug:test";
    a.parameterId = 3;
    a.owningTrack = 1;
    a.setPoint (0, 0.5f);
    a.setPoint (8, 0.8f, AutomationCurveType::Step);

    b = a; // copy

    if (! (a == b))
    {
        std::cerr << "Identical lanes should be equal\n";
        return false;
    }

    // Modify b
    b.setPoint (16, 0.3f);
    if (a == b)
    {
        std::cerr << "Different lanes should not be equal\n";
        return false;
    }

    // PatternAutomationData equality
    PatternAutomationData d1, d2;
    d1.getOrCreateLane ("plug:x", 0, 0).setPoint (0, 0.5f);
    d2.getOrCreateLane ("plug:x", 0, 0).setPoint (0, 0.5f);

    if (! (d1 == d2))
    {
        std::cerr << "Identical automation data should be equal\n";
        return false;
    }

    d2.getOrCreateLane ("plug:y", 1, 1);
    if (d1 == d2)
    {
        std::cerr << "Different automation data should not be equal\n";
        return false;
    }

    return true;
}

bool testPluginAutomationSetAvailablePluginsIsNotReentrant()
{
    TrackerLookAndFeel lnf;
    PluginAutomationComponent automationComponent (lnf);

    AutomatablePluginInfo pluginInfo;
    pluginInfo.pluginId = "insert:0:0";
    pluginInfo.displayName = "Test Insert";
    pluginInfo.owningTrack = 0;
    pluginInfo.parameters.push_back ({ 0, "Gain" });
    std::vector<AutomatablePluginInfo> plugins { pluginInfo };

    int callbackCount = 0;
    automationComponent.onPluginSelected = [&] (const juce::String&)
    {
        ++callbackCount;

        // This mirrors MainComponent's previous callback behaviour.
        // Re-entering setAvailablePlugins here must not recurse indefinitely.
        if (callbackCount < 4)
            automationComponent.setAvailablePlugins (plugins);
    };

    automationComponent.setAvailablePlugins (plugins);

    if (callbackCount != 0)
    {
        std::cerr << "setAvailablePlugins should not dispatch reentrant plugin selection callbacks\n";
        return false;
    }

    return true;
}

bool testPluginAutomationPreservesParameterSelection()
{
    TrackerLookAndFeel lnf;
    PluginAutomationComponent automationComponent (lnf);

    // Set up a plugin with multiple parameters
    AutomatablePluginInfo pluginInfo;
    pluginInfo.pluginId = "inst:1";
    pluginInfo.displayName = "Synth (Inst 1)";
    pluginInfo.owningTrack = 0;
    pluginInfo.parameters.push_back ({ 0, "Cutoff 1" });
    pluginInfo.parameters.push_back ({ 1, "Cutoff 2" });
    pluginInfo.parameters.push_back ({ 2, "Resonance" });

    std::vector<AutomatablePluginInfo> plugins { pluginInfo };
    automationComponent.setAvailablePlugins (plugins);

    // Verify first param is auto-selected
    if (automationComponent.getSelectedPluginId() != "inst:1")
    {
        std::cerr << "Expected plugin inst:1 selected initially\n";
        return false;
    }

    if (automationComponent.getSelectedParameterIndex() != 0)
    {
        std::cerr << "Expected param 0 selected initially, got "
                  << automationComponent.getSelectedParameterIndex() << "\n";
        return false;
    }

    // Navigate to param index 1 (Cutoff 2)
    automationComponent.navigateToParam ("inst:1", 1);

    if (automationComponent.getSelectedParameterIndex() != 1)
    {
        std::cerr << "Expected param 1 after navigateToParam, got "
                  << automationComponent.getSelectedParameterIndex() << "\n";
        return false;
    }

    // Re-populate with the same plugin list — param 1 should be preserved
    automationComponent.setAvailablePlugins (plugins);

    if (automationComponent.getSelectedPluginId() != "inst:1")
    {
        std::cerr << "Plugin selection not preserved after setAvailablePlugins\n";
        return false;
    }

    if (automationComponent.getSelectedParameterIndex() != 1)
    {
        std::cerr << "Parameter selection not preserved, expected 1 got "
                  << automationComponent.getSelectedParameterIndex() << "\n";
        return false;
    }

    return true;
}

bool testPluginAutomationMultiPluginTrack()
{
    TrackerLookAndFeel lnf;
    PluginAutomationComponent automationComponent (lnf);

    // Set up two plugins on the same track
    AutomatablePluginInfo plugin1;
    plugin1.pluginId = "inst:1";
    plugin1.displayName = "Synth A (Inst 1)";
    plugin1.owningTrack = 0;
    plugin1.isInstrument = true;
    plugin1.parameters.push_back ({ 0, "Cutoff" });
    plugin1.parameters.push_back ({ 1, "Resonance" });

    AutomatablePluginInfo plugin2;
    plugin2.pluginId = "inst:2";
    plugin2.displayName = "Synth B (Inst 2)";
    plugin2.owningTrack = 0;
    plugin2.isInstrument = true;
    plugin2.parameters.push_back ({ 0, "Volume" });
    plugin2.parameters.push_back ({ 1, "Pan" });

    std::vector<AutomatablePluginInfo> plugins { plugin1, plugin2 };
    automationComponent.setAvailablePlugins (plugins);

    // Navigate to plugin 2, param 1 (Pan)
    automationComponent.navigateToParam ("inst:2", 1);

    if (automationComponent.getSelectedPluginId() != "inst:2")
    {
        std::cerr << "Expected inst:2 selected after navigateToParam, got "
                  << automationComponent.getSelectedPluginId() << "\n";
        return false;
    }

    if (automationComponent.getSelectedParameterIndex() != 1)
    {
        std::cerr << "Expected param 1 (Pan) after navigate, got "
                  << automationComponent.getSelectedParameterIndex() << "\n";
        return false;
    }

    // Re-populate (simulating cache hit with same data) — should preserve inst:2 param 1
    automationComponent.setAvailablePlugins (plugins);

    if (automationComponent.getSelectedPluginId() != "inst:2")
    {
        std::cerr << "Plugin 2 not preserved after re-populate\n";
        return false;
    }

    if (automationComponent.getSelectedParameterIndex() != 1)
    {
        std::cerr << "Param 1 not preserved on plugin 2, got "
                  << automationComponent.getSelectedParameterIndex() << "\n";
        return false;
    }

    // Now simulate removing plugin 2 (only plugin 1 remains)
    std::vector<AutomatablePluginInfo> singlePlugin { plugin1 };
    automationComponent.setAvailablePlugins (singlePlugin);

    // Should fall back to first available plugin/param
    if (automationComponent.getSelectedPluginId() != "inst:1")
    {
        std::cerr << "Expected fallback to inst:1 after plugin 2 removed, got "
                  << automationComponent.getSelectedPluginId() << "\n";
        return false;
    }

    // Empty list: no plugin selected
    automationComponent.setAvailablePlugins ({});
    if (automationComponent.getSelectedPluginId().isNotEmpty())
    {
        std::cerr << "Expected empty selection with no plugins\n";
        return false;
    }

    return true;
}

bool testTrackerGridClampsCursorNoteLaneOnTrackChange()
{
    PatternData patternData;
    TrackLayout trackLayout;
    TrackerLookAndFeel lnf;
    TrackerGrid grid (patternData, lnf, trackLayout);

    trackLayout.setTrackNoteLaneCount (0, 3);
    trackLayout.setTrackNoteLaneCount (1, 1);
    grid.setCursorPosition (0, 0);

    const juce::KeyPress tabKey (juce::KeyPress::tabKey);
    for (int i = 0; i < 6; ++i)
        grid.keyPressed (tabKey);

    if (grid.getCursorNoteLane() != 2)
    {
        std::cerr << "Expected cursor note lane to advance to 2 on 3-lane track, got "
                  << grid.getCursorNoteLane() << "\n";
        return false;
    }

    grid.setCursorPosition (0, 1);
    if (grid.getCursorNoteLane() != 0)
    {
        std::cerr << "Cursor note lane should clamp to 0 when moving to single-lane track, got "
                  << grid.getCursorNoteLane() << "\n";
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
        { "SendBuffersAutoResizeForLargeWrites", &testSendBuffersAutoResizeForLargeWrites },
        { "PanMappingCenterAndExtremes", &testPanMappingCenterAndExtremes },
        { "InstrumentRoutingRoundTripFullRange", &testInstrumentRoutingRoundTripFullRange },
        { "InstrumentRoutingClampsOutOfRange", &testInstrumentRoutingClampsOutOfRange },
        { "ArrangementRemapAfterPatternRemoved", &testArrangementRemapAfterPatternRemoved },
        { "ArrangementRemapNoOpWhenPatternCountInvalid", &testArrangementRemapNoOpWhenPatternCountInvalid },
        { "ProjectRoundTripKeepsHighInstrumentAndFxSlots", &testProjectRoundTripKeepsHighInstrumentAndFxSlots },
        { "ProjectRoundTripKeepsFollowModeAndBrowserDir", &testProjectRoundTripKeepsFollowModeAndBrowserDir },
        { "ProjectRoundTripKeepsMixerLayoutAndInstrumentParams", &testProjectRoundTripKeepsMixerLayoutAndInstrumentParams },
        { "ArrangementDeleteNotifiesChangeCallback", &testArrangementDeleteNotifiesChangeCallback },
        { "BpmBoundaryRoundTrip", &testBpmBoundaryRoundTrip },
        { "RpbBoundaryRoundTrip", &testRpbBoundaryRoundTrip },
        { "ArrangementRemapEntryAtRemovedIndex", &testArrangementRemapEntryAtRemovedIndex },
        { "ArrangementRemapClampsAboveNewCount", &testArrangementRemapClampsAboveNewCount },
        { "PatternMixedInstrumentIdsRoundTrip", &testPatternMixedInstrumentIdsRoundTrip },
        { "CellEdgeValuesRoundTrip", &testCellEdgeValuesRoundTrip },
        { "ArrangementInsertMoveRemoveIntegrity", &testArrangementInsertMoveRemoveIntegrity },
        { "MultiPatternArrangementRoundTrip", &testMultiPatternArrangementRoundTrip },
        { "SendBuffersMultipleAddAccumulates", &testSendBuffersMultipleAddAccumulates },
        { "SendBuffersZeroLengthSlice", &testSendBuffersZeroLengthSlice },
        { "InstrumentRoutingBankProgramSplit", &testInstrumentRoutingBankProgramSplit },
        { "FxParamTransportByteRoundTrip", &testFxParamTransportByteRoundTrip },
        { "FxParamTransportSequenceOrdering", &testFxParamTransportSequenceOrdering },
        { "EmptyArrangementRoundTrip", &testEmptyArrangementRoundTrip },
        { "PatternMultiFxSlotRoundTrip", &testPatternMultiFxSlotRoundTrip },
        { "TrackLayoutFxLaneCountRoundTrip", &testTrackLayoutFxLaneCountRoundTrip },
        { "SymbolicFxTokenRoundTrip", &testSymbolicFxTokenRoundTrip },
        { "MasterLaneRoundTrip", &testMasterLaneRoundTrip },
        { "MixerMuteSoloRoundTrip", &testMixerMuteSoloRoundTrip },
        { "ArrangementRemapPreservesRepeats", &testArrangementRemapPreservesRepeats },
        { "GranularCenterUsesAbsolutePosition", &testGranularCenterUsesAbsolutePosition },
        { "GranularCenterClampsToRegion", &testGranularCenterClampsToRegion },
        { "SliceBoundariesUseAbsolutePositions", &testSliceBoundariesUseAbsolutePositions },
        { "SliceBoundariesClampAndDeduplicate", &testSliceBoundariesClampAndDeduplicate },
        { "EqualSlicePointGenerationUsesRegionCount", &testEqualSlicePointGenerationUsesRegionCount },
        { "BeatSliceRegionCountDefaultsAndPointCount", &testBeatSliceRegionCountDefaultsAndPointCount },
        { "NoteLaneSerializationRoundTrip", &testNoteLaneSerializationRoundTrip },
        { "MultiLaneNoteDataSanity", &testMultiLaneNoteDataSanity },
        { "InstrumentSlotInfoSetAndClear", &testInstrumentSlotInfoSetAndClear },
        { "PluginInstrumentSlotSerializationRoundTrip", &testPluginInstrumentSlotSerializationRoundTrip },
        { "PluginInstrumentOwnershipValidation", &testPluginInstrumentOwnershipValidation },
        { "PluginSlotSerializationEmptyRoundTrip", &testPluginSlotSerializationEmptyRoundTrip },
        { "AutomationLaneInterpolation", &testAutomationLaneInterpolation },
        { "AutomationLanePointOperations", &testAutomationLanePointOperations },
        { "AutomationDataSerializationRoundTrip", &testAutomationDataSerializationRoundTrip },
        { "PatternDuplicateClonesAutomation", &testPatternDuplicateClonesAutomation },
        { "AutomationEmptySerializationRoundTrip", &testAutomationEmptySerializationRoundTrip },
        // Phase 6: Regression and stabilization tests
        { "InsertSlotStateRoundTrip", &testInsertSlotStateRoundTrip },
        { "AutomationStepCurveInterpolation", &testAutomationStepCurveInterpolation },
        { "AutomationRemovePointNearBehavior", &testAutomationRemovePointNearBehavior },
        { "PatternAutomationDataOperations", &testPatternAutomationDataOperations },
        { "CombinedNoteLaneAutomationInsertRoundTrip", &testCombinedNoteLaneAutomationInsertRoundTrip },
        { "VersionMigrationPreV6LoadsSafely", &testVersionMigrationPreV6LoadsSafely },
        { "AutomationCloneIsDeepCopy", &testAutomationCloneIsDeepCopy },
        { "AutomationLaneValueClamping", &testAutomationLaneValueClamping },
        { "MultiPatternAutomationRoundTrip", &testMultiPatternAutomationRoundTrip },
        { "InsertSlotMaxCapacity", &testInsertSlotMaxCapacity },
        { "AutomationLaneEquality", &testAutomationLaneEquality },
        { "PluginAutomationSetAvailablePluginsIsNotReentrant", &testPluginAutomationSetAvailablePluginsIsNotReentrant },
        { "PluginAutomationPreservesParameterSelection", &testPluginAutomationPreservesParameterSelection },
        { "PluginAutomationMultiPluginTrack", &testPluginAutomationMultiPluginTrack },
        { "TrackerGridClampsCursorNoteLaneOnTrackChange", &testTrackerGridClampsCursorNoteLaneOnTrackChange },
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
