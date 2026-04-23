#include "PluginCatalogService.h"

#include <vector>

namespace
{
constexpr int kPluginScanTimeoutMs = 30000;

struct PluginScanPlan
{
    juce::AudioPluginFormat* format = nullptr;
    juce::String formatName;
    juce::StringArray filesOrIdentifiers;
};

juce::File getPluginDataDirectory()
{
    auto dataRoot = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    auto dataDir = dataRoot.getChildFile ("VCTracker");

    if (! dataDir.exists() && dataRoot.getChildFile ("Tracker Adjust").exists())
        dataDir = dataRoot.getChildFile ("Tracker Adjust");

    dataDir.createDirectory();
    return dataDir;
}

bool readWorkerResult (const juce::File& resultFile,
                       juce::OwnedArray<juce::PluginDescription>& result)
{
    auto xml = juce::parseXML (resultFile);

    if (xml == nullptr || ! xml->hasTagName ("KNOWNPLUGINS"))
        return false;

    for (auto* child : xml->getChildIterator())
    {
        juce::PluginDescription description;

        if (description.loadFromXml (*child))
            result.add (new juce::PluginDescription (description));
    }

    return true;
}

class OutOfProcessPluginScanner final : public juce::KnownPluginList::CustomScanner
{
public:
    explicit OutOfProcessPluginScanner (juce::File workerToUse)
        : worker (std::move (workerToUse)),
          tempDirectory (juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("VCTrackerPluginScan"))
    {
        tempDirectory.createDirectory();
    }

    bool findPluginTypesFor (juce::AudioPluginFormat& format,
                             juce::OwnedArray<juce::PluginDescription>& result,
                             const juce::String& fileOrIdentifier) override
    {
        if (! worker.existsAsFile())
        {
            DBG ("Plugin scan worker missing: " + worker.getFullPathName());
            return false;
        }

        auto resultFile = tempDirectory.getChildFile (juce::Uuid().toString() + ".xml");
        resultFile.deleteFile();

        juce::StringArray arguments;
        arguments.add (worker.getFullPathName());
        arguments.add ("--format");
        arguments.add (format.getName());
        arguments.add ("--plugin");
        arguments.add (fileOrIdentifier);
        arguments.add ("--output");
        arguments.add (resultFile.getFullPathName());

        juce::ChildProcess child;

        if (! child.start (arguments, 0))
        {
            DBG ("Failed to start PluginScanWorker for: " + fileOrIdentifier);
            return false;
        }

        const auto finished = child.waitForProcessToFinish (kPluginScanTimeoutMs);

        if (! finished)
        {
            child.kill();
            DBG ("Plugin scan worker timed out: " + fileOrIdentifier);
            return false;
        }

        if (child.getExitCode() != 0)
        {
            DBG ("Plugin scan worker failed (exit "
                 + juce::String (child.getExitCode()) + "): " + fileOrIdentifier);
            resultFile.deleteFile();
            return false;
        }

        const auto parsed = readWorkerResult (resultFile, result);
        resultFile.deleteFile();

        if (! parsed)
            DBG ("Plugin scan worker returned invalid result: " + fileOrIdentifier);

        return parsed;
    }

private:
    juce::File worker;
    juce::File tempDirectory;
};

void notifyScanCompleteOnMessageThread (PluginCatalogService& service)
{
    if (service.onScanComplete != nullptr)
    {
        auto* servicePtr = &service;

        juce::MessageManager::callAsync ([servicePtr]
        {
            if (servicePtr->onScanComplete)
                servicePtr->onScanComplete();
        });
    }
}

void notifyScanProgress (const PluginCatalogService::ScanProgressCallback& callback,
                         int completed,
                         int total,
                         const juce::String& formatName,
                         const juce::String& pluginName)
{
    if (callback != nullptr)
        callback ({ completed, total, formatName, pluginName });
}
} // namespace

PluginCatalogService::PluginCatalogService (te::Engine& e)
    : engine (e)
{
    loadPersistedKnownPluginList();
}

juce::File PluginCatalogService::getDeadPluginsFile()
{
    return getPluginDataDirectory().getChildFile ("dead-plugins.txt");
}

juce::File PluginCatalogService::getKnownPluginsFile()
{
    return getPluginDataDirectory().getChildFile ("known-plugins.xml");
}

juce::File PluginCatalogService::getFailedPluginsFile()
{
    return getPluginDataDirectory().getChildFile ("failed-plugins.txt");
}

void PluginCatalogService::scanForPlugins (const juce::StringArray& scanPaths,
                                           ScanProgressCallback progressCallback)
{
    if (scanning.exchange (true))
        return;

    auto& formatManager = engine.getPluginManager().pluginFormatManager;
    auto& knownList = engine.getPluginManager().knownPluginList;
    auto deadPluginsFile = getDeadPluginsFile();
    auto worker = findPluginScanWorker();

    if (! worker.existsAsFile())
    {
        DBG ("PluginScanWorker binary not found; plugin scan cancelled");
        notifyScanProgress (progressCallback, 0, 0, {}, {});
        scanning.store (false);
        notifyScanCompleteOnMessageThread (*this);
        return;
    }

    juce::PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal (knownList, deadPluginsFile);
    knownList.setCustomScanner (std::make_unique<OutOfProcessPluginScanner> (worker));

    try
    {
        std::vector<PluginScanPlan> scanPlans;
        int totalPluginsToScan = 0;

        // Build the candidate list up front so the UI can show X/Y progress.
        for (int i = 0; i < formatManager.getNumFormats(); ++i)
        {
            auto* format = formatManager.getFormat (i);
            if (format == nullptr)
                continue;

            auto formatName = format->getName();

            // Only scan VST3 and AudioUnit formats
            if (formatName != "VST3" && formatName != "AudioUnit")
                continue;

            juce::FileSearchPath searchPath;

            if (formatName == "VST3")
                for (auto& path : scanPaths)
                    searchPath.add (juce::File (path));

            auto defaultPaths = format->getDefaultLocationsToSearch();
            for (int p = 0; p < defaultPaths.getNumPaths(); ++p)
                searchPath.addIfNotAlreadyThere (defaultPaths[p]);

            auto filesOrIdentifiers = format->searchPathsForPlugins (searchPath,
                                                                      true,   // recursive
                                                                      true);  // allow async instantiation

            totalPluginsToScan += filesOrIdentifiers.size();
            scanPlans.push_back ({ format, formatName, std::move (filesOrIdentifiers) });
        }

        notifyScanProgress (progressCallback, 0, totalPluginsToScan, {}, {});

        // Scan each format. The directory scanner stays in this process, but
        // every plugin binary load happens in PluginScanWorker.
        int completedPlugins = 0;
        for (auto& plan : scanPlans)
        {
            if (plan.format == nullptr)
                continue;

            juce::PluginDirectoryScanner scanner (knownList, *plan.format, {},
                                                   true,   // recursive
                                                   deadPluginsFile,
                                                   true);  // allow plugins that require async instantiation

            scanner.setFilesOrIdentifiersToScan (plan.filesOrIdentifiers);

            for (int candidate = 0; candidate < plan.filesOrIdentifiers.size(); ++candidate)
            {
                auto pluginName = scanner.getNextPluginFileThatWillBeScanned();
                notifyScanProgress (progressCallback, completedPlugins, totalPluginsToScan,
                                    plan.formatName, pluginName);

                juce::String scannedPluginName;
                scanner.scanNextFile (true, scannedPluginName);
                completedPlugins++;

                if (scannedPluginName.isNotEmpty())
                    pluginName = scannedPluginName;

                notifyScanProgress (progressCallback, completedPlugins, totalPluginsToScan,
                                    plan.formatName, pluginName);
            }

            for (auto& failedFile : scanner.getFailedFiles())
            {
                knownList.addToBlacklist (failedFile);
                DBG ("Plugin scan failed; blacklisted: " + failedFile);
            }
        }
    }
    catch (const std::exception& e)
    {
        DBG ("Plugin scan exception: " + juce::String (e.what()));
    }
    catch (...)
    {
        DBG ("Plugin scan unknown exception");
    }

    knownList.setCustomScanner (nullptr);
    savePersistedKnownPluginList();
    scanning.store (false);
    notifyScanCompleteOnMessageThread (*this);
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

void PluginCatalogService::loadPersistedKnownPluginList()
{
    auto& knownList = engine.getPluginManager().knownPluginList;

    if (auto xml = juce::parseXML (getKnownPluginsFile()))
        knownList.recreateFromXml (*xml);

    juce::PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal (knownList,
                                                                       getDeadPluginsFile());
    savePersistedKnownPluginList();
}

void PluginCatalogService::savePersistedKnownPluginList()
{
    auto& knownList = engine.getPluginManager().knownPluginList;

    if (auto xml = knownList.createXml())
        xml->writeTo (getKnownPluginsFile());

    getFailedPluginsFile().replaceWithText (knownList.getBlacklistedFiles().joinIntoString ("\n"),
                                            true,
                                            true);
}

juce::File PluginCatalogService::findPluginScanWorker() const
{
   #if JUCE_MAC
    auto appFile = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    auto bundledWorker = appFile.getChildFile ("Contents/MacOS/PluginScanWorker");

    if (bundledWorker.existsAsFile())
        return bundledWorker;
   #endif

    auto exeFile = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    auto siblingWorker = exeFile.getSiblingFile ("PluginScanWorker");

    if (siblingWorker.existsAsFile())
        return siblingWorker;

    return {};
}
