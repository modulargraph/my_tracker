#pragma once

#include <JuceHeader.h>

namespace GlobalPreferences
{
    juce::File getPrefsFile();
    void saveBrowserDir (const juce::String& dir);
    juce::String loadBrowserDir();
    void savePluginScanPaths (const juce::StringArray& paths);
    juce::StringArray loadPluginScanPaths();
}
