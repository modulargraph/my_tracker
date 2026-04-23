#include "GlobalPreferences.h"

namespace
{
constexpr auto kPrefsDirName = "VCTracker";
constexpr auto kLegacyPrefsDirName = "TrackerAdjust";
constexpr auto kPrefsRootName = "VCTrackerPrefs";
constexpr auto kLegacyPrefsRootName = "TrackerAdjustPrefs";

juce::File getCurrentPrefsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile (kPrefsDirName)
               .getChildFile ("prefs.xml");
}

juce::File getLegacyPrefsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile (kLegacyPrefsDirName)
               .getChildFile ("prefs.xml");
}

juce::File getPrefsFileForLoad()
{
    auto currentPrefsFile = getCurrentPrefsFile();
    if (currentPrefsFile.existsAsFile())
        return currentPrefsFile;

    return getLegacyPrefsFile();
}

juce::ValueTree loadPrefsTree()
{
    juce::ValueTree root (kPrefsRootName);
    auto prefsFile = getPrefsFileForLoad();

    if (! prefsFile.existsAsFile())
        return root;

    auto xml = juce::XmlDocument::parse (prefsFile);
    if (xml == nullptr)
        return root;

    auto loaded = juce::ValueTree::fromXml (*xml);
    if (! loaded.isValid())
        return root;

    if (loaded.hasType (kLegacyPrefsRootName))
    {
        root.copyPropertiesAndChildrenFrom (loaded, nullptr);
        return root;
    }

    return loaded;
}
} // namespace

namespace GlobalPreferences
{

//==============================================================================
// Global browser directory persistence
//==============================================================================

juce::File getPrefsFile()
{
    return getCurrentPrefsFile();
}

void saveBrowserDir (const juce::String& dir)
{
    auto prefsFile = getPrefsFile();
    if (! prefsFile.getParentDirectory().createDirectory())
        return;

    auto root = loadPrefsTree();

    root.setProperty ("browserDir", dir, nullptr);

    if (auto xml = root.createXml())
        xml->writeTo (prefsFile);
}

juce::String loadBrowserDir()
{
    auto root = loadPrefsTree();
    return root.getProperty ("browserDir", "").toString();
}

//==============================================================================
// Global plugin scan path persistence
//==============================================================================

void savePluginScanPaths (const juce::StringArray& paths)
{
    auto prefsFile = getPrefsFile();
    if (! prefsFile.getParentDirectory().createDirectory())
        return;

    auto root = loadPrefsTree();

    // Remove any existing scan paths child
    auto existing = root.getChildWithName ("PluginScanPaths");
    if (existing.isValid())
        root.removeChild (existing, nullptr);

    // Add new scan paths
    juce::ValueTree scanPathsTree ("PluginScanPaths");
    for (auto& path : paths)
    {
        juce::ValueTree pathTree ("Path");
        pathTree.setProperty ("dir", path, nullptr);
        scanPathsTree.addChild (pathTree, -1, nullptr);
    }
    root.addChild (scanPathsTree, -1, nullptr);

    if (auto xml = root.createXml())
        xml->writeTo (prefsFile);
}

juce::StringArray loadPluginScanPaths()
{
    auto root = loadPrefsTree();
    auto scanPathsTree = root.getChildWithName ("PluginScanPaths");
    if (! scanPathsTree.isValid())
        return {};

    juce::StringArray paths;
    for (int i = 0; i < scanPathsTree.getNumChildren(); ++i)
    {
        auto pathTree = scanPathsTree.getChild (i);
        auto dir = pathTree.getProperty ("dir", "").toString();
        if (dir.isNotEmpty())
            paths.add (dir);
    }

    return paths;
}

} // namespace GlobalPreferences
