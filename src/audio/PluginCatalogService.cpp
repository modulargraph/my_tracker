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

            // Pre-validate AU bundles out-of-process before scanning
            prevalidatePluginBundles (knownList, *format, defaultPaths);

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

            // Pre-validate VST3 bundles out-of-process before scanning
            prevalidatePluginBundles (knownList, *format, searchPath);

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

//==============================================================================
// Out-of-process plugin pre-validation
//==============================================================================

void PluginCatalogService::prevalidatePluginBundles (
    juce::KnownPluginList& knownList,
    juce::AudioPluginFormat& format,
    const juce::FileSearchPath& searchPath)
{
   #if JUCE_MAC
    // Locate the PluginValidator helper binary inside the app bundle
    auto appFile = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    auto validatorPath = appFile.getChildFile ("Contents/MacOS/PluginValidator");

    if (! validatorPath.existsAsFile())
    {
        // Fallback: next to the executable (development builds)
        auto exeFile = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        validatorPath = exeFile.getSiblingFile ("PluginValidator");
    }

    if (! validatorPath.existsAsFile())
    {
        DBG ("PluginValidator binary not found — skipping pre-validation");
        return;
    }

    // Get all plugin file paths the scanner would enumerate
    auto pluginFiles = format.searchPathsForPlugins (searchPath, true);

    // Skip already-known (successfully scanned) plugins
    for (auto& known : knownList.getTypes())
        pluginFiles.removeString (known.fileOrIdentifier);

    // Skip already-blacklisted plugins
    for (auto& bl : knownList.getBlacklistedFiles())
        pluginFiles.removeString (bl);

    if (pluginFiles.isEmpty())
        return;

    DBG ("Pre-validating " + juce::String (pluginFiles.size()) + " plugin bundles...");

    auto validatorCmd = validatorPath.getFullPathName();

    for (auto& pluginPath : pluginFiles)
    {
        juce::ChildProcess child;
        auto command = validatorCmd.quoted() + " " + juce::String (pluginPath).quoted();

        if (child.start (command))
        {
            bool finished = child.waitForProcessToFinish (10000); // 10 s timeout

            if (! finished)
            {
                child.kill();
                knownList.addToBlacklist (pluginPath);
                DBG ("Plugin pre-validation TIMEOUT — blacklisted: " + pluginPath);
            }
            else
            {
                auto exitCode = child.getExitCode();

                if (exitCode == 42)
                {
                    // dlopen succeeded — safe to scan
                }
                else if (exitCode == 99 || exitCode == 0)
                {
                    // 99 = signal handler caught crash
                    //  0 = killed by uncaught signal (JUCE returns 0 for signal deaths)
                    knownList.addToBlacklist (pluginPath);
                    DBG ("Plugin pre-validation CRASHED (exit "
                         + juce::String ((int) exitCode) + ") — blacklisted: " + pluginPath);
                }
                else
                {
                    // 1 = usage error, 2 = bundle not found, 3 = dlopen failed gracefully
                    // Let the real scanner try its own loading path
                    DBG ("Plugin pre-validation: dlopen issue (exit "
                         + juce::String ((int) exitCode) + "): " + pluginPath);
                }
            }
        }
        else
        {
            DBG ("Failed to start PluginValidator for: " + pluginPath);
        }
    }

    DBG ("Pre-validation complete");
   #else
    juce::ignoreUnused (knownList, format, searchPath);
   #endif
}
