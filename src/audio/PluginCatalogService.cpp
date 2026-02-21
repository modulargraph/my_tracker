#include "PluginCatalogService.h"

PluginCatalogService::PluginCatalogService (te::Engine& e)
    : engine (e)
{
}

juce::File PluginCatalogService::getDeadPluginsFile()
{
    auto dataDir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("Tracker Adjust");
    dataDir.createDirectory();
    return dataDir.getChildFile ("dead-plugins.txt");
}

void PluginCatalogService::scanForPlugins (const juce::StringArray& scanPaths)
{
    if (scanning.load())
        return;

    scanning.store (true);

    auto& formatManager = engine.getPluginManager().pluginFormatManager;
    auto& knownList = engine.getPluginManager().knownPluginList;
    auto deadPluginsFile = getDeadPluginsFile();

    // Scan each format
    for (int i = 0; i < formatManager.getNumFormats(); ++i)
    {
        auto* format = formatManager.getFormat (i);
        if (format == nullptr)
            continue;

        auto formatName = format->getName();

        // Only scan VST3 and AudioUnit formats
        if (formatName != "VST3" && formatName != "AudioUnit")
            continue;

        // For AudioUnit, scan paths are not user-configurable — the OS provides them
        if (formatName == "AudioUnit")
        {
            auto defaultPaths = format->getDefaultLocationsToSearch();
            juce::PluginDirectoryScanner scanner (knownList, *format, defaultPaths,
                                                   true,   // recursive
                                                   deadPluginsFile,
                                                   true);  // allow plugins that require ASIO

            juce::String pluginName;

            try
            {
                while (scanner.scanNextFile (true, pluginName))
                {
                    // scanning...
                }
            }
            catch (const std::exception& e)
            {
                DBG ("Plugin scan exception (AudioUnit): " + juce::String (e.what()));
            }
            catch (...)
            {
                DBG ("Plugin scan unknown exception (AudioUnit)");
            }
        }
        else
        {
            // VST3: use user-provided scan paths plus defaults
            juce::FileSearchPath searchPath;
            for (auto& path : scanPaths)
                searchPath.add (juce::File (path));

            // Also add the format's default locations
            auto defaultPaths = format->getDefaultLocationsToSearch();
            for (int p = 0; p < defaultPaths.getNumPaths(); ++p)
                searchPath.addIfNotAlreadyThere (defaultPaths[p]);

            juce::PluginDirectoryScanner scanner (knownList, *format, searchPath,
                                                   true,   // recursive
                                                   deadPluginsFile,
                                                   true);  // allow plugins that require ASIO

            juce::String pluginName;

            try
            {
                while (scanner.scanNextFile (true, pluginName))
                {
                    // scanning...
                }
            }
            catch (const std::exception& e)
            {
                DBG ("Plugin scan exception (VST3): " + juce::String (e.what()));
            }
            catch (...)
            {
                DBG ("Plugin scan unknown exception (VST3)");
            }
        }
    }

    scanning.store (false);

    // Notify on the message thread
    if (onScanComplete != nullptr)
    {
        juce::MessageManager::callAsync ([this]
        {
            if (onScanComplete)
                onScanComplete();
        });
    }
}

juce::Array<juce::PluginDescription> PluginCatalogService::getAllPlugins() const
{
    juce::Array<juce::PluginDescription> result;

    for (auto& desc : engine.getPluginManager().knownPluginList.getTypes())
    {
        // Exclude built-in Tracktion plugins
        if (! te::PluginManager::isBuiltInPlugin (desc))
            result.add (desc);
    }

    return result;
}

juce::Array<juce::PluginDescription> PluginCatalogService::getEffects() const
{
    juce::Array<juce::PluginDescription> result;

    for (auto& desc : engine.getPluginManager().knownPluginList.getTypes())
    {
        if (te::PluginManager::isBuiltInPlugin (desc))
            continue;

        if (! desc.isInstrument)
            result.add (desc);
    }

    return result;
}

juce::Array<juce::PluginDescription> PluginCatalogService::getInstruments() const
{
    juce::Array<juce::PluginDescription> result;

    for (auto& desc : engine.getPluginManager().knownPluginList.getTypes())
    {
        if (te::PluginManager::isBuiltInPlugin (desc))
            continue;

        if (desc.isInstrument)
            result.add (desc);
    }

    return result;
}

juce::Array<juce::PluginDescription> PluginCatalogService::getPluginsByFormat (const juce::String& formatName) const
{
    juce::Array<juce::PluginDescription> result;

    for (auto& desc : engine.getPluginManager().knownPluginList.getTypes())
    {
        if (te::PluginManager::isBuiltInPlugin (desc))
            continue;

        if (desc.pluginFormatName == formatName)
            result.add (desc);
    }

    return result;
}

juce::KnownPluginList& PluginCatalogService::getKnownPluginList()
{
    return engine.getPluginManager().knownPluginList;
}

juce::AudioPluginFormatManager& PluginCatalogService::getFormatManager()
{
    return engine.getPluginManager().pluginFormatManager;
}

juce::StringArray PluginCatalogService::getDefaultScanPaths()
{
    juce::StringArray paths;

   #if JUCE_MAC
    // User-level VST3 folder
    paths.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                   .getChildFile ("Library/Audio/Plug-Ins/VST3")
                   .getFullPathName());

    // System-level VST3 folder
    paths.add ("/Library/Audio/Plug-Ins/VST3");
   #endif

    return paths;
}
