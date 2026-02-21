#include "PluginCatalogService.h"

PluginCatalogService::PluginCatalogService (te::Engine& e)
    : engine (e)
{
}

void PluginCatalogService::validatePluginBundles (juce::AudioPluginFormat& format,
                                                   const juce::FileSearchPath& searchPath)
{
   #if JUCE_MAC
    auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    auto deadFile = getDeadPluginsFile();

    // Read existing dead plugins so we don't re-test them
    juce::StringArray alreadyDead;
    if (deadFile.existsAsFile())
        alreadyDead.addLines (deadFile.loadFileAsString());

    // Enumerate plugin files/identifiers for this format
    auto files = format.searchPathsForPlugins (searchPath, true, true);

    for (auto& pluginPath : files)
    {
        if (alreadyDead.contains (pluginPath))
            continue;

        // Resolve to the actual bundle path on disk
        // VST3 identifiers and AU identifiers may differ, but for VST3
        // the identifier is the file path. For AU, it's a component ID.
        juce::File bundleFile (pluginPath);

        if (! bundleFile.exists())
            continue; // AU identifiers aren't file paths; skip validation for those

        juce::ChildProcess child;
        juce::StringArray args;
        args.add (exe.getFullPathName());
        args.add ("--validate-bundle");
        args.add (bundleFile.getFullPathName());

        if (! child.start (args))
        {
            DBG ("Failed to launch validator for: " + pluginPath);
            continue;
        }

        // Wait up to 15 seconds for the child
        if (! child.waitForProcessToFinish (15000))
        {
            child.kill();
            DBG ("Plugin validation timed out: " + pluginPath);

            // Record as dead
            deadFile.appendText (pluginPath + "\n");
            continue;
        }

        auto exitCode = child.getExitCode();

        if (exitCode != 0)
        {
            DBG ("Plugin validation failed (exit " + juce::String (exitCode) + "): " + pluginPath);

            // Record as dead so PluginDirectoryScanner will skip it
            deadFile.appendText (pluginPath + "\n");
        }
    }
   #else
    juce::ignoreUnused (format, searchPath);
   #endif
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

            // Pre-validate bundles in child processes to avoid crashing on bad plugins
            validatePluginBundles (*format, defaultPaths);

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

            // Pre-validate bundles in child processes to avoid crashing on bad plugins
            validatePluginBundles (*format, searchPath);

            juce::PluginDirectoryScanner scanner (knownList, *format, searchPath,
                                                   true,   // recursive
                                                   deadPluginsFile,
                                                   true);  // allow plugins that require ASIO

            juce::String pluginName;

            // VST3 plugins may call macOS APIs (e.g. TSMGetInputSourceProperty)
            // during DLL loading that assert they're on the main dispatch queue.
            // Dispatch each scanNextFile call to the message thread to avoid
            // dispatch_assert_queue_fail crashes (e.g. NI Vari Comp).
            bool hasMore = true;

            while (hasMore)
            {
                juce::WaitableEvent done;
                bool scanResult = false;
                juce::String stepName;

                juce::MessageManager::callAsync ([&]
                {
                    try
                    {
                        scanResult = scanner.scanNextFile (true, stepName);
                    }
                    catch (const std::exception& e)
                    {
                        DBG ("Plugin scan exception (VST3): " + juce::String (e.what()));
                        scanResult = false;
                    }
                    catch (...)
                    {
                        DBG ("Plugin scan unknown exception (VST3)");
                        scanResult = false;
                    }

                    done.signal();
                });

                done.wait (-1);
                pluginName = stepName;
                hasMore = scanResult;
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
