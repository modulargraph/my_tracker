#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include <atomic>
#include <thread>
#include "PluginCatalogService.h"
#include "TrackerLookAndFeel.h"

namespace te = tracktion;

/**
 * Settings dialog component containing:
 *   1. Audio Output device selection (sample rate, block size, output device)
 *   2. Plugin scan paths list (editable) with scan/rescan button
 *   3. Discovered plugin list
 */
class AudioPluginSettingsComponent : public juce::Component,
                                     private juce::ChangeListener
{
public:
    AudioPluginSettingsComponent (te::Engine& engine,
                                  PluginCatalogService& catalog,
                                  TrackerLookAndFeel& lnf);
    ~AudioPluginSettingsComponent() override;

    void resized() override;

    /** Set the current scan paths to display. */
    void setScanPaths (const juce::StringArray& paths);

    /** Get the current scan paths from the list. */
    juce::StringArray getScanPaths() const;

    /** Callback when scan paths change (for persistence). */
    std::function<void (const juce::StringArray&)> onScanPathsChanged;

    /** Refresh the discovered plugin table. */
    void refreshPluginList();

    static constexpr int kPreferredWidth = 700;
    static constexpr int kPreferredHeight = 560;

private:
    te::Engine& engine;
    PluginCatalogService& catalogService;
    TrackerLookAndFeel& lookAndFeel;

    //==============================================================================
    // Audio device section
    juce::Label audioSectionLabel;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> audioDeviceSelector;

    //==============================================================================
    // Plugin section
    juce::Label pluginSectionLabel;

    // Scan paths
    juce::Label scanPathsLabel;
    juce::ListBox scanPathsList;
    juce::TextButton addPathButton { "Add Path..." };
    juce::TextButton removePathButton { "Remove" };
    juce::TextButton scanButton { "Scan / Rescan" };

    // Discovered plugins
    juce::Label discoveredPluginsLabel;
    juce::TableListBox pluginTable;

    // Internal data
    juce::StringArray scanPaths;
    std::atomic<bool> scanInProgress { false };
    std::thread scanThread;

    //==============================================================================
    // ListBoxModel for scan paths
    class ScanPathListModel : public juce::ListBoxModel
    {
    public:
        ScanPathListModel (AudioPluginSettingsComponent& o) : owner (o) {}
        int getNumRows() override { return owner.scanPaths.size(); }
        void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    private:
        AudioPluginSettingsComponent& owner;
    };
    ScanPathListModel scanPathListModel { *this };

    //==============================================================================
    // TableListBoxModel for discovered plugins
    class PluginTableModel : public juce::TableListBoxModel
    {
    public:
        PluginTableModel (AudioPluginSettingsComponent& o) : owner (o) {}
        int getNumRows() override { return plugins.size(); }
        void paintRowBackground (juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
        void paintCell (juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
        void sortOrderChanged (int newSortColumnId, bool isForwards) override;

        juce::Array<juce::PluginDescription> plugins;
    private:
        AudioPluginSettingsComponent& owner;
    };
    PluginTableModel pluginTableModel { *this };

    //==============================================================================
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void startPluginScan();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginSettingsComponent)
};
