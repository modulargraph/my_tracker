#include <array>
#include <cmath>
#include <iostream>
#include <map>
#include <vector>

#include <JuceHeader.h>

#include "Arrangement.h"
#include "ArrangementComponent.h"
#include "InstrumentRouting.h"
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
    cell.getFxSlot (0).fx = 0x8;
    cell.getFxSlot (0).fxParam = 0xFF;
    cell.getFxSlot (1).fx = 0xF;
    cell.getFxSlot (1).fxParam = 0x1F;
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

    if (loadedCell.getFxSlot (0).fx != 0x8 || loadedCell.getFxSlot (0).fxParam != 0xFF
        || loadedCell.getFxSlot (1).fx != 0xF || loadedCell.getFxSlot (1).fxParam != 0x1F)
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
    maxCell.getFxSlot (0).fx = 0xF;
    maxCell.getFxSlot (0).fxParam = 0xFF;
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
    if (c1.getFxSlot (0).fx != 0xF || c1.getFxSlot (0).fxParam != 0xFF)
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
    cell.getFxSlot (0).fx = 0x0;
    cell.getFxSlot (0).fxParam = 0x37;  // Arpeggio
    cell.getFxSlot (1).fx = 0xF;
    cell.getFxSlot (1).fxParam = 0x80;  // Speed/Tempo
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
    if (c.getFxSlot (0).fx != 0x0 || c.getFxSlot (0).fxParam != 0x37
        || c.getFxSlot (1).fx != 0xF || c.getFxSlot (1).fxParam != 0x80)
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
        { "EmptyArrangementRoundTrip", &testEmptyArrangementRoundTrip },
        { "PatternMultiFxSlotRoundTrip", &testPatternMultiFxSlotRoundTrip },
        { "TrackLayoutFxLaneCountRoundTrip", &testTrackLayoutFxLaneCountRoundTrip },
        { "MixerMuteSoloRoundTrip", &testMixerMuteSoloRoundTrip },
        { "ArrangementRemapPreservesRepeats", &testArrangementRemapPreservesRepeats },
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
