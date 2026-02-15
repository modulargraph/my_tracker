#include "ProjectSerializer.h"

juce::String ProjectSerializer::saveToFile (const juce::File& file, const PatternData& patternData,
                                            double bpm, int rowsPerBeat,
                                            const std::map<int, juce::File>& loadedSamples)
{
    juce::ValueTree root ("TrackerAdjustProject");
    root.setProperty ("version", 1, nullptr);

    // Settings
    juce::ValueTree settings ("Settings");
    settings.setProperty ("bpm", bpm, nullptr);
    settings.setProperty ("rowsPerBeat", rowsPerBeat, nullptr);
    settings.setProperty ("currentPattern", patternData.getCurrentPatternIndex(), nullptr);
    root.addChild (settings, -1, nullptr);

    // Samples
    juce::ValueTree samples ("Samples");
    for (auto& [index, sampleFile] : loadedSamples)
    {
        juce::ValueTree sample ("Sample");
        sample.setProperty ("index", index, nullptr);
        sample.setProperty ("path", sampleFile.getRelativePathFrom (file.getParentDirectory()), nullptr);
        sample.setProperty ("absPath", sampleFile.getFullPathName(), nullptr);
        samples.addChild (sample, -1, nullptr);
    }
    root.addChild (samples, -1, nullptr);

    // Patterns
    juce::ValueTree patterns ("Patterns");
    for (int i = 0; i < patternData.getNumPatterns(); ++i)
        patterns.addChild (patternToValueTree (patternData.getPattern (i), i), -1, nullptr);
    root.addChild (patterns, -1, nullptr);

    // Write to file
    auto xml = root.createXml();
    if (xml == nullptr)
        return "Failed to create XML";

    if (! xml->writeTo (file))
        return "Failed to write file: " + file.getFullPathName();

    return {};
}

juce::String ProjectSerializer::loadFromFile (const juce::File& file, PatternData& patternData,
                                              double& bpm, int& rowsPerBeat,
                                              std::map<int, juce::File>& loadedSamples)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
        return "Failed to parse XML file";

    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("TrackerAdjustProject"))
        return "Not a valid Tracker Adjust project file";

    // Settings
    auto settings = root.getChildWithName ("Settings");
    if (settings.isValid())
    {
        bpm = settings.getProperty ("bpm", 120.0);
        rowsPerBeat = settings.getProperty ("rowsPerBeat", 4);
    }

    // Samples
    loadedSamples.clear();
    auto samples = root.getChildWithName ("Samples");
    if (samples.isValid())
    {
        for (int i = 0; i < samples.getNumChildren(); ++i)
        {
            auto sample = samples.getChild (i);
            int index = sample.getProperty ("index", -1);
            juce::String absPath = sample.getProperty ("absPath", "");
            juce::String relPath = sample.getProperty ("path", "");

            juce::File sampleFile (absPath);
            if (! sampleFile.existsAsFile())
                sampleFile = file.getParentDirectory().getChildFile (relPath);

            if (sampleFile.existsAsFile() && index >= 0)
                loadedSamples[index] = sampleFile;
        }
    }

    // Patterns
    patternData.clearAllPatterns();
    auto patterns = root.getChildWithName ("Patterns");
    if (patterns.isValid() && patterns.getNumChildren() > 0)
    {
        // Remove the default pattern added by clearAllPatterns
        patternData.removePattern (0);

        for (int i = 0; i < patterns.getNumChildren(); ++i)
        {
            auto patTree = patterns.getChild (i);
            int numRows = patTree.getProperty ("numRows", 64);
            patternData.addPattern (numRows);
            auto& pat = patternData.getPattern (patternData.getNumPatterns() - 1);
            valueTreeToPattern (patTree, pat);
        }
    }

    int currentPat = settings.isValid() ? static_cast<int> (settings.getProperty ("currentPattern", 0)) : 0;
    patternData.setCurrentPattern (juce::jlimit (0, patternData.getNumPatterns() - 1, currentPat));

    return {};
}

juce::ValueTree ProjectSerializer::patternToValueTree (const Pattern& pattern, int /*index*/)
{
    juce::ValueTree patTree ("Pattern");
    patTree.setProperty ("name", pattern.name, nullptr);
    patTree.setProperty ("numRows", pattern.numRows, nullptr);

    for (int r = 0; r < pattern.numRows; ++r)
    {
        bool hasData = false;
        for (int t = 0; t < kNumTracks; ++t)
        {
            if (! pattern.getCell (r, t).isEmpty())
            {
                hasData = true;
                break;
            }
        }
        if (! hasData) continue;

        juce::ValueTree rowTree ("Row");
        rowTree.setProperty ("index", r, nullptr);

        for (int t = 0; t < kNumTracks; ++t)
        {
            const auto& cell = pattern.getCell (r, t);
            if (cell.isEmpty()) continue;

            juce::ValueTree cellTree ("Cell");
            cellTree.setProperty ("track", t, nullptr);
            cellTree.setProperty ("note", cell.note, nullptr);
            cellTree.setProperty ("inst", cell.instrument, nullptr);
            cellTree.setProperty ("vol", cell.volume, nullptr);
            cellTree.setProperty ("fx", cell.fx, nullptr);
            cellTree.setProperty ("fxp", cell.fxParam, nullptr);
            rowTree.addChild (cellTree, -1, nullptr);
        }

        patTree.addChild (rowTree, -1, nullptr);
    }

    return patTree;
}

void ProjectSerializer::valueTreeToPattern (const juce::ValueTree& tree, Pattern& pattern)
{
    pattern.name = tree.getProperty ("name", "Pattern").toString();
    int numRows = tree.getProperty ("numRows", 64);
    pattern.resize (numRows);
    pattern.clear();

    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto rowTree = tree.getChild (i);
        if (! rowTree.hasType ("Row")) continue;

        int row = rowTree.getProperty ("index", -1);
        if (row < 0 || row >= numRows) continue;

        for (int j = 0; j < rowTree.getNumChildren(); ++j)
        {
            auto cellTree = rowTree.getChild (j);
            if (! cellTree.hasType ("Cell")) continue;

            int track = cellTree.getProperty ("track", -1);
            if (track < 0 || track >= kNumTracks) continue;

            Cell cell;
            cell.note = cellTree.getProperty ("note", -1);
            cell.instrument = cellTree.getProperty ("inst", -1);
            cell.volume = cellTree.getProperty ("vol", -1);
            cell.fx = cellTree.getProperty ("fx", 0);
            cell.fxParam = cellTree.getProperty ("fxp", 0);
            pattern.setCell (row, track, cell);
        }
    }
}
