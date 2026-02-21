#include "AudioPluginSettingsComponent.h"

AudioPluginSettingsComponent::AudioPluginSettingsComponent (te::Engine& e,
                                                            PluginCatalogService& catalog,
                                                            TrackerLookAndFeel& lnf)
    : engine (e),
      catalogService (catalog),
      lookAndFeel (lnf)
{
    // --- Audio device section ---
    audioSectionLabel.setText ("Audio Output", juce::dontSendNotification);
    audioSectionLabel.setFont (lnf.getMonoFont (14.0f));
    audioSectionLabel.setColour (juce::Label::textColourId, juce::Colour (0xffcba6f7));
    addAndMakeVisible (audioSectionLabel);

    // Create the audio device selector:
    //   showMidiInputOptions = false
    //   showMidiOutputSelector = false
    //   showChannelsAsStereoPairs = true
    //   hideAdvancedOptions = false
    // The min/max input channels are 0 (no input controls)
    audioDeviceSelector = std::make_unique<juce::AudioDeviceSelectorComponent> (
        engine.getDeviceManager().deviceManager,
        0, 0,       // min/max input channels (disabled)
        1, 256,     // min/max output channels
        false,      // show MIDI input options
        false,      // show MIDI output selector
        true,       // show channels as stereo pairs
        false);     // hide advanced options

    addAndMakeVisible (*audioDeviceSelector);

    // --- Plugin section ---
    pluginSectionLabel.setText ("Plugin Settings", juce::dontSendNotification);
    pluginSectionLabel.setFont (lnf.getMonoFont (14.0f));
    pluginSectionLabel.setColour (juce::Label::textColourId, juce::Colour (0xffcba6f7));
    addAndMakeVisible (pluginSectionLabel);

    // Scan paths
    scanPathsLabel.setText ("Scan Paths:", juce::dontSendNotification);
    scanPathsLabel.setFont (lnf.getMonoFont (12.0f));
    scanPathsLabel.setColour (juce::Label::textColourId, juce::Colour (0xffcccccc));
    addAndMakeVisible (scanPathsLabel);

    scanPathsList.setModel (&scanPathListModel);
    scanPathsList.setRowHeight (20);
    scanPathsList.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff2a2a3a));
    scanPathsList.setColour (juce::ListBox::outlineColourId, juce::Colour (0xff444466));
    addAndMakeVisible (scanPathsList);

    addAndMakeVisible (addPathButton);
    addAndMakeVisible (removePathButton);
    addAndMakeVisible (scanButton);

    addPathButton.onClick = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Select Plugin Scan Directory");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this, chooser] (const juce::FileChooser& fc)
                              {
                                  auto dir = fc.getResult();
                                  if (dir.isDirectory())
                                  {
                                      scanPaths.addIfNotAlreadyThere (dir.getFullPathName());
                                      scanPathsList.updateContent();
                                      scanPathsList.repaint();
                                      if (onScanPathsChanged)
                                          onScanPathsChanged (scanPaths);
                                  }
                              });
    };

    removePathButton.onClick = [this]
    {
        int selected = scanPathsList.getSelectedRow();
        if (selected >= 0 && selected < scanPaths.size())
        {
            scanPaths.remove (selected);
            scanPathsList.updateContent();
            scanPathsList.repaint();
            if (onScanPathsChanged)
                onScanPathsChanged (scanPaths);
        }
    };

    scanButton.onClick = [this]
    {
        startPluginScan();
    };

    // Discovered plugins table
    discoveredPluginsLabel.setText ("Discovered Plugins:", juce::dontSendNotification);
    discoveredPluginsLabel.setFont (lnf.getMonoFont (12.0f));
    discoveredPluginsLabel.setColour (juce::Label::textColourId, juce::Colour (0xffcccccc));
    addAndMakeVisible (discoveredPluginsLabel);

    pluginTable.setModel (&pluginTableModel);
    pluginTable.setRowHeight (20);
    pluginTable.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff2a2a3a));
    pluginTable.setColour (juce::ListBox::outlineColourId, juce::Colour (0xff444466));

    auto& header = pluginTable.getHeader();
    header.addColumn ("Name",   1, 250, 100, 400);
    header.addColumn ("Format", 2, 80,  60,  120);
    header.addColumn ("Type",   3, 80,  60,  120);
    header.addColumn ("Manufacturer", 4, 150, 80, 250);
    addAndMakeVisible (pluginTable);

    // Listen for changes to the known plugin list
    catalogService.getKnownPluginList().addChangeListener (this);

    // Populate initial data
    refreshPluginList();
}

AudioPluginSettingsComponent::~AudioPluginSettingsComponent()
{
    catalogService.getKnownPluginList().removeChangeListener (this);

    if (scanThread.joinable())
        scanThread.join();
}

void AudioPluginSettingsComponent::resized()
{
    auto r = getLocalBounds().reduced (12);

    // Audio section
    audioSectionLabel.setBounds (r.removeFromTop (22));
    r.removeFromTop (4);

    // Give audio device selector a decent height
    int audioSelectorHeight = juce::jmin (200, r.getHeight() / 3);
    audioDeviceSelector->setBounds (r.removeFromTop (audioSelectorHeight));
    r.removeFromTop (12);

    // Plugin section
    pluginSectionLabel.setBounds (r.removeFromTop (22));
    r.removeFromTop (4);

    // Scan paths
    scanPathsLabel.setBounds (r.removeFromTop (18));
    r.removeFromTop (2);

    auto scanPathArea = r.removeFromTop (80);
    auto scanPathButtons = scanPathArea.removeFromRight (100);
    scanPathsList.setBounds (scanPathArea);

    addPathButton.setBounds (scanPathButtons.removeFromTop (26));
    scanPathButtons.removeFromTop (2);
    removePathButton.setBounds (scanPathButtons.removeFromTop (26));
    scanPathButtons.removeFromTop (2);
    scanButton.setBounds (scanPathButtons.removeFromTop (26));

    r.removeFromTop (8);

    // Discovered plugins
    discoveredPluginsLabel.setBounds (r.removeFromTop (18));
    r.removeFromTop (2);
    pluginTable.setBounds (r);
}

void AudioPluginSettingsComponent::setScanPaths (const juce::StringArray& paths)
{
    scanPaths = paths;
    scanPathsList.updateContent();
    scanPathsList.repaint();
}

juce::StringArray AudioPluginSettingsComponent::getScanPaths() const
{
    return scanPaths;
}

void AudioPluginSettingsComponent::startPluginScan()
{
    if (scanInProgress.exchange (true))
        return;

    scanButton.setEnabled (false);
    scanButton.setButtonText ("Scanning...");

    if (scanThread.joinable())
        scanThread.join();

    auto pathsCopy = scanPaths;
    auto* catalog = &catalogService;
    juce::Component::SafePointer<AudioPluginSettingsComponent> safeThis (this);

    scanThread = std::thread ([catalog, safeThis, pathsCopy]
    {
        catalog->scanForPlugins (pathsCopy);

        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis == nullptr)
                return;

            safeThis->scanInProgress.store (false);
            safeThis->scanButton.setEnabled (true);
            safeThis->scanButton.setButtonText ("Scan / Rescan");
            safeThis->refreshPluginList();
        });
    });
}

void AudioPluginSettingsComponent::refreshPluginList()
{
    pluginTableModel.plugins = catalogService.getAllPlugins();
    pluginTable.updateContent();
    pluginTable.repaint();
}

void AudioPluginSettingsComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // KnownPluginList changed — refresh the table
    refreshPluginList();
}

//==============================================================================
// ScanPathListModel
//==============================================================================

void AudioPluginSettingsComponent::ScanPathListModel::paintListBoxItem (
    int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowNumber < 0 || rowNumber >= owner.scanPaths.size())
        return;

    if (rowIsSelected)
        g.fillAll (juce::Colour (0xff444466));
    else
        g.fillAll (rowNumber % 2 == 0 ? juce::Colour (0xff2a2a3a) : juce::Colour (0xff252535));

    g.setColour (juce::Colour (0xffcccccc));
    g.setFont (owner.lookAndFeel.getMonoFont (12.0f));
    g.drawText (owner.scanPaths[rowNumber], 6, 0, width - 12, height,
                juce::Justification::centredLeft);
}

void AudioPluginSettingsComponent::ScanPathListModel::listBoxItemClicked (int, const juce::MouseEvent&)
{
    // Selection is handled by the ListBox itself
}

//==============================================================================
// PluginTableModel
//==============================================================================

void AudioPluginSettingsComponent::PluginTableModel::paintRowBackground (
    juce::Graphics& g, int rowNumber, int /*width*/, int /*height*/, bool rowIsSelected)
{
    if (rowIsSelected)
        g.fillAll (juce::Colour (0xff444466));
    else
        g.fillAll (rowNumber % 2 == 0 ? juce::Colour (0xff2a2a3a) : juce::Colour (0xff252535));
}

void AudioPluginSettingsComponent::PluginTableModel::paintCell (
    juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool /*rowIsSelected*/)
{
    if (rowNumber < 0 || rowNumber >= plugins.size())
        return;

    auto& desc = plugins.getReference (rowNumber);

    g.setColour (juce::Colour (0xffcccccc));
    g.setFont (owner.lookAndFeel.getMonoFont (12.0f));

    juce::String text;
    switch (columnId)
    {
        case 1: text = desc.name; break;
        case 2: text = desc.pluginFormatName; break;
        case 3: text = desc.isInstrument ? "Instrument" : "Effect"; break;
        case 4: text = desc.manufacturerName; break;
        default: break;
    }

    g.drawText (text, 6, 0, width - 12, height, juce::Justification::centredLeft);
}

void AudioPluginSettingsComponent::PluginTableModel::sortOrderChanged (int newSortColumnId, bool isForwards)
{
    auto comparator = [newSortColumnId, isForwards] (const juce::PluginDescription& a, const juce::PluginDescription& b)
    {
        int result = 0;
        switch (newSortColumnId)
        {
            case 1: result = a.name.compareIgnoreCase (b.name); break;
            case 2: result = a.pluginFormatName.compareIgnoreCase (b.pluginFormatName); break;
            case 3: result = (a.isInstrument ? 1 : 0) - (b.isInstrument ? 1 : 0); break;
            case 4: result = a.manufacturerName.compareIgnoreCase (b.manufacturerName); break;
            default: break;
        }
        return isForwards ? result < 0 : result > 0;
    };

    std::sort (plugins.begin(), plugins.end(), comparator);
    owner.pluginTable.updateContent();
    owner.pluginTable.repaint();
}
