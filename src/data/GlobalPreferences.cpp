#include "GlobalPreferences.h"

namespace GlobalPreferences
{

//==============================================================================
// Global browser directory persistence
//==============================================================================

juce::File getPrefsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("TrackerAdjust")
               .getChildFile ("prefs.xml");
}

void saveBrowserDir (const juce::String& dir)
{
    auto prefsFile = getPrefsFile();
    if (! prefsFile.getParentDirectory().createDirectory())
        return;

    juce::ValueTree root ("TrackerAdjustPrefs");

    // Load existing prefs if any
    if (prefsFile.existsAsFile())
    {
        auto xml = juce::XmlDocument::parse (prefsFile);
        if (xml != nullptr)
        {
            auto loaded = juce::ValueTree::fromXml (*xml);
            if (loaded.isValid())
                root = loaded;
        }
    }

    root.setProperty ("browserDir", dir, nullptr);

    if (auto xml = root.createXml())
        xml->writeTo (prefsFile);
}

juce::String loadBrowserDir()
{
    auto prefsFile = getPrefsFile();
    if (! prefsFile.existsAsFile())
        return {};

    auto xml = juce::XmlDocument::parse (prefsFile);
    if (xml == nullptr)
        return {};

    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.isValid())
        return {};
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

    juce::ValueTree root ("TrackerAdjustPrefs");

    // Load existing prefs if any
    if (prefsFile.existsAsFile())
    {
        auto xml = juce::XmlDocument::parse (prefsFile);
        if (xml != nullptr)
        {
            auto loaded = juce::ValueTree::fromXml (*xml);
            if (loaded.isValid())
                root = loaded;
        }
    }

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
    auto prefsFile = getPrefsFile();
    if (! prefsFile.existsAsFile())
        return {};

    auto xml = juce::XmlDocument::parse (prefsFile);
    if (xml == nullptr)
        return {};

    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.isValid())
        return {};

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
