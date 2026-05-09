#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

/**
 * Wraps Tracktion's PluginManager to expose filtered plugin lists
 * (effects, instruments/synths, by format) for the settings UI and
 * future plugin picker dialogs.
 */
class PluginCatalogService
{
public:
    explicit PluginCatalogService (te::Engine& engine);

    struct ScanProgress
    {
        int completed = 0;
        int total = 0;
        juce::String formatName;
        juce::String pluginName;
    };

    using ScanProgressCallback = std::function<void (const ScanProgress&)>;

    /** Trigger a scan for the given formats (VST3, AudioUnit) using the
     *  paths currently registered in scanPaths. */
    void scanForPlugins (const juce::StringArray& scanPaths,
                         ScanProgressCallback progressCallback = {});

    /** Returns true while a scan is in progress. */
    bool isScanning() const { return scanning.load(); }

    /** Ask the active scan to stop. Safe to call from the message thread before
     *  joining the UI-owned scan thread. */
    void requestScanCancellation();

    //==============================================================================
    // Filtered lists (rebuilt after each scan)

    /** All discovered plugins (excluding built-in Tracktion types). */
    juce::Array<juce::PluginDescription> getAllPlugins() const;

    /** Only effect plugins. */
    juce::Array<juce::PluginDescription> getEffects() const;

    /** Only instrument/synth plugins. */
    juce::Array<juce::PluginDescription> getInstruments() const;

    /** Plugins matching a specific format name (e.g. "VST3", "AudioUnit"). */
    juce::Array<juce::PluginDescription> getPluginsByFormat (const juce::String& formatName) const;

    //==============================================================================
    /** Returns the known plugin list (for display in UI). */
    juce::KnownPluginList& getKnownPluginList();

    /** Returns the format manager (needed by AudioPluginFormatManager for scanning). */
    juce::AudioPluginFormatManager& getFormatManager();

    /** Callback invoked on the message thread when a scan completes. */
    std::function<void()> onScanComplete;

    //==============================================================================
    /** Default macOS VST3 scan paths. */
    static juce::StringArray getDefaultScanPaths();

    /** File used to track plugins that crashed during scanning.
     *  After a crash, the next scan will skip the offending plugin. */
    static juce::File getDeadPluginsFile();

    /** Persisted plugin catalogue, including the scanner blacklist. */
    static juce::File getKnownPluginsFile();

    /** Plain-text list of plugin identifiers that failed isolated scanning. */
    static juce::File getFailedPluginsFile();

private:
    te::Engine& engine;
    std::atomic<bool> scanning { false };
    std::atomic<bool> scanCancelRequested { false };

    void loadPersistedKnownPluginList();
    void savePersistedKnownPluginList();
    juce::File findPluginScanWorker() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginCatalogService)
};
