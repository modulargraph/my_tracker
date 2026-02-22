#include "InsertPluginManager.h"
#include "TrackerEngine.h"
#include "ChannelStripPlugin.h"
#include "TrackOutputPlugin.h"
#include "PatternMidiBuilder.h"

using PatternMidiBuilder::findInsertPluginForSlot;

InsertPluginManager::InsertPluginManager (TrackerEngine& e)
    : engine (e)
{
}

InsertPluginManager::~InsertPluginManager()
{
    pluginEditorWindows.clear();
}

bool InsertPluginManager::addInsertPlugin (int trackIndex, const juce::PluginDescription& desc)
{
    if (engine.getEdit() == nullptr || mixerStatePtr == nullptr)
        return false;
    if (trackIndex < 0 || trackIndex >= kNumTracks)
        return false;

    auto& slots = mixerStatePtr->insertSlots[static_cast<size_t> (trackIndex)];
    if (static_cast<int> (slots.size()) >= kMaxInsertSlots)
        return false;

    auto* track = engine.getTrack (trackIndex);
    if (track == nullptr)
        return false;

    // Create the plugin instance via Tracktion's plugin cache
    auto& formatManager = engine.getEngine().getPluginManager().pluginFormatManager;
    juce::String errorMessage;

    auto instance = formatManager.createPluginInstance (desc, 44100.0, 512, errorMessage);
    if (instance == nullptr)
    {
        DBG ("Failed to create insert plugin: " + errorMessage);
        return false;
    }

    // Create a Tracktion ExternalPlugin wrapper
    auto externalPlugin = track->edit.getPluginCache().createNewPlugin (
        te::ExternalPlugin::xmlTypeName, desc);

    if (externalPlugin == nullptr)
        return false;

    // Find insertion position: after ChannelStripPlugin + existing inserts, before TrackOutputPlugin
    int insertPos = -1;
    auto& pluginList = track->pluginList;
    for (int i = 0; i < pluginList.size(); ++i)
    {
        if (dynamic_cast<TrackOutputPlugin*> (pluginList[i]) != nullptr)
        {
            insertPos = i;
            break;
        }
    }

    if (insertPos < 0)
        insertPos = pluginList.size(); // Fallback: insert at end

    pluginList.insertPlugin (*externalPlugin, insertPos, nullptr);

    // Add to state model
    InsertSlotState newSlot;
    newSlot.pluginName = desc.name;
    newSlot.pluginIdentifier = desc.createIdentifierString();
    newSlot.pluginFormatName = desc.pluginFormatName;
    newSlot.bypassed = false;
    slots.push_back (std::move (newSlot));

    if (onInsertStateChanged)
        onInsertStateChanged();

    return true;
}

void InsertPluginManager::removeInsertPlugin (int trackIndex, int slotIndex)
{
    if (engine.getEdit() == nullptr || mixerStatePtr == nullptr)
        return;
    if (trackIndex < 0 || trackIndex >= kNumTracks)
        return;

    auto& slots = mixerStatePtr->insertSlots[static_cast<size_t> (trackIndex)];
    if (slotIndex < 0 || slotIndex >= static_cast<int> (slots.size()))
        return;

    // Close any editor window
    closePluginEditor (trackIndex, slotIndex);

    // Find and remove the plugin from the track's plugin list
    auto* track = engine.getTrack (trackIndex);
    if (track != nullptr)
    {
        if (auto* plugin = findInsertPluginForSlot (*track, slotIndex))
            plugin->removeFromParent();
    }

    slots.erase (slots.begin() + slotIndex);

    if (onInsertStateChanged)
        onInsertStateChanged();
}

void InsertPluginManager::setInsertBypassed (int trackIndex, int slotIndex, bool bypassed)
{
    if (mixerStatePtr == nullptr)
        return;
    if (trackIndex < 0 || trackIndex >= kNumTracks)
        return;

    auto& slots = mixerStatePtr->insertSlots[static_cast<size_t> (trackIndex)];
    if (slotIndex < 0 || slotIndex >= static_cast<int> (slots.size()))
        return;

    slots[static_cast<size_t> (slotIndex)].bypassed = bypassed;

    // Find the corresponding external plugin and toggle its enabled state
    auto* track = engine.getTrack (trackIndex);
    if (track != nullptr)
    {
        if (auto* plugin = findInsertPluginForSlot (*track, slotIndex))
            plugin->setEnabled (! bypassed);
    }

    if (onInsertStateChanged)
        onInsertStateChanged();
}

te::Plugin* InsertPluginManager::getInsertPlugin (int trackIndex, int slotIndex)
{
    if (engine.getEdit() == nullptr || trackIndex < 0 || trackIndex >= kNumTracks)
        return nullptr;

    auto* track = engine.getTrack (trackIndex);
    if (track == nullptr)
        return nullptr;

    return findInsertPluginForSlot (*track, slotIndex);
}

void InsertPluginManager::rebuildInsertChain (int trackIndex)
{
    if (engine.getEdit() == nullptr || mixerStatePtr == nullptr)
        return;
    if (trackIndex < 0 || trackIndex >= kNumTracks)
        return;

    auto* track = engine.getTrack (trackIndex);
    if (track == nullptr)
        return;

    // Remove all external plugins between ChannelStrip and TrackOutput
    std::vector<te::Plugin*> toRemove;
    bool pastChannelStrip = false;
    for (int i = 0; i < track->pluginList.size(); ++i)
    {
        auto* plugin = track->pluginList[i];
        if (dynamic_cast<ChannelStripPlugin*> (plugin) != nullptr)
        {
            pastChannelStrip = true;
            continue;
        }
        if (dynamic_cast<TrackOutputPlugin*> (plugin) != nullptr)
            break;
        if (pastChannelStrip && dynamic_cast<te::ExternalPlugin*> (plugin) != nullptr)
            toRemove.push_back (plugin);
    }

    for (auto* p : toRemove)
        p->removeFromParent();

    // Re-add inserts from state
    auto& slots = mixerStatePtr->insertSlots[static_cast<size_t> (trackIndex)];
    for (auto& slot : slots)
    {
        if (slot.isEmpty())
            continue;

        // Find the matching PluginDescription from the known plugin list
        auto& knownList = engine.getEngine().getPluginManager().knownPluginList;
        const juce::PluginDescription* matchedDesc = nullptr;

        for (auto& desc : knownList.getTypes())
        {
            if (desc.createIdentifierString() == slot.pluginIdentifier)
            {
                matchedDesc = &desc;
                break;
            }
        }

        if (matchedDesc == nullptr)
            continue;

        auto externalPlugin = track->edit.getPluginCache().createNewPlugin (
            te::ExternalPlugin::xmlTypeName, *matchedDesc);

        if (externalPlugin == nullptr)
            continue;

        // Find insertion position before TrackOutputPlugin
        int insertPos = -1;
        for (int i = 0; i < track->pluginList.size(); ++i)
        {
            if (dynamic_cast<TrackOutputPlugin*> (track->pluginList[i]) != nullptr)
            {
                insertPos = i;
                break;
            }
        }

        if (insertPos < 0)
            insertPos = track->pluginList.size();

        track->pluginList.insertPlugin (*externalPlugin, insertPos, nullptr);

        // Restore plugin state if available
        if (slot.pluginState.isValid())
        {
            if (auto* ext = dynamic_cast<te::ExternalPlugin*> (externalPlugin.get()))
                ext->restorePluginStateFromValueTree (slot.pluginState);
        }

        // Apply bypass state
        externalPlugin->setEnabled (! slot.bypassed);
    }
}

void InsertPluginManager::snapshotInsertPluginStates()
{
    if (engine.getEdit() == nullptr || mixerStatePtr == nullptr)
        return;

    for (int trackIndex = 0; trackIndex < kNumTracks; ++trackIndex)
    {
        auto& slots = mixerStatePtr->insertSlots[static_cast<size_t> (trackIndex)];
        for (int slotIndex = 0; slotIndex < static_cast<int> (slots.size()); ++slotIndex)
        {
            auto& slot = slots[static_cast<size_t> (slotIndex)];
            if (slot.isEmpty())
                continue;

            if (auto* ext = dynamic_cast<te::ExternalPlugin*> (getInsertPlugin (trackIndex, slotIndex)))
            {
                ext->flushPluginStateToValueTree();
                slot.pluginState = ext->state.createCopy();
            }
            else
            {
                slot.pluginState = {};
            }
        }
    }
}

void InsertPluginManager::openPluginEditor (int trackIndex, int slotIndex)
{
    auto* plugin = getInsertPlugin (trackIndex, slotIndex);
    if (plugin == nullptr)
        return;

    juce::String key = juce::String (trackIndex) + ":" + juce::String (slotIndex);

    // Check if window already exists
    if (pluginEditorWindows.count (key) > 0 && pluginEditorWindows[key] != nullptr)
    {
        pluginEditorWindows[key]->toFront (true);
        return;
    }

    auto* externalPlugin = dynamic_cast<te::ExternalPlugin*> (plugin);
    if (externalPlugin == nullptr)
        return;

    auto audioPlugin = externalPlugin->getAudioPluginInstance();
    if (audioPlugin == nullptr)
        return;

    auto editor = audioPlugin->createEditorIfNeeded();
    if (editor == nullptr)
        return;

    struct PluginEditorWindow : public juce::DocumentWindow
    {
        PluginEditorWindow (const juce::String& name,
                            std::map<juce::String, std::unique_ptr<juce::DocumentWindow>>& windowMap,
                            const juce::String& mapKey)
            : juce::DocumentWindow (name, juce::Colours::darkgrey,
                                    juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
              windows (windowMap), key (mapKey)
        {
        }

        void closeButtonPressed() override
        {
            windows.erase (key);  // destroys this window
        }

    private:
        std::map<juce::String, std::unique_ptr<juce::DocumentWindow>>& windows;
        juce::String key;
    };

    auto window = std::make_unique<PluginEditorWindow> (
        externalPlugin->getName(), pluginEditorWindows, key);

    window->setContentOwned (editor, true);
    window->setResizable (true, false);
    window->centreWithSize (editor->getWidth(), editor->getHeight());
    window->setVisible (true);
    window->setAlwaysOnTop (true);

    pluginEditorWindows[key] = std::move (window);
}

void InsertPluginManager::closePluginEditor (int trackIndex, int slotIndex)
{
    juce::String key = juce::String (trackIndex) + ":" + juce::String (slotIndex);
    pluginEditorWindows.erase (key);
}
