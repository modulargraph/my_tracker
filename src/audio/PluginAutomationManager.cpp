#include "PluginAutomationManager.h"
#include "TrackerEngine.h"

namespace te = tracktion;

PluginAutomationManager::PluginAutomationManager (TrackerEngine& e)
    : engine (e)
{
}

juce::AudioPluginInstance* PluginAutomationManager::resolvePluginInstance (const juce::String& pluginId) const
{
    if (pluginId.startsWith ("inst:"))
    {
        int instIdx = pluginId.substring (5).getIntValue();
        auto* plugin = engine.getPluginInstrumentInstance (instIdx);
        if (plugin != nullptr)
        {
            if (auto* ext = dynamic_cast<te::ExternalPlugin*> (plugin))
                return ext->getAudioPluginInstance();
        }
    }
    else if (pluginId.startsWith ("insert:"))
    {
        // Format: "insert:trackIndex:slotIndex"
        auto parts = juce::StringArray::fromTokens (pluginId.substring (7), ":", "");
        if (parts.size() >= 2)
        {
            int trackIdx = parts[0].getIntValue();
            int slotIdx = parts[1].getIntValue();
            auto* plugin = engine.getInsertPlugin (trackIdx, slotIdx);
            if (plugin != nullptr)
            {
                if (auto* ext = dynamic_cast<te::ExternalPlugin*> (plugin))
                    return ext->getAudioPluginInstance();
            }
        }
    }

    return nullptr;
}

PluginAutomationManager::AutomatedParam* PluginAutomationManager::findAutomatedParam (const juce::String& pluginId, int paramIndex)
{
    for (auto& ap : lastAutomatedParams)
    {
        if (ap.pluginId == pluginId && ap.paramIndex == paramIndex)
            return &ap;
    }

    return nullptr;
}

const PluginAutomationManager::AutomatedParam* PluginAutomationManager::findAutomatedParam (const juce::String& pluginId,
                                                                                             int paramIndex) const
{
    for (const auto& ap : lastAutomatedParams)
    {
        if (ap.pluginId == pluginId && ap.paramIndex == paramIndex)
            return &ap;
    }

    return nullptr;
}

void PluginAutomationManager::applyPatternAutomation (const PatternAutomationData& automationData,
                                                       int /*patternLength*/, int /*rpb*/)
{
    // Clear previous tracking without touching plugin parameters synchronously.
    // resetAutomationParameters() used to call param->setValue() for every
    // tracked param, which deadlocks when the audio thread is processing the
    // plugin (playInStopEnabled = true means the graph is always live).
    lastAutomatedParams.clear();

    if (automationData.isEmpty())
        return;

    for (const auto& lane : automationData.lanes)
    {
        if (lane.isEmpty())
            continue;

        auto* audioPlugin = resolvePluginInstance (lane.pluginId);
        if (audioPlugin == nullptr)
            continue;

        auto& params = audioPlugin->getParameters();
        if (lane.parameterId < 0 || lane.parameterId >= params.size())
            continue;

        auto* param = params[lane.parameterId];
        if (param == nullptr)
            continue;

        // Store baseline for later row-wise playback updates.
        // tryEnter: audio thread may hold the callback lock (playInStopEnabled).
        float baseline = 0.5f;
        auto& lock = audioPlugin->getCallbackLock();
        if (lock.tryEnter())
        {
            baseline = param->getValue();
            lock.exit();
        }
        lastAutomatedParams.push_back ({ lane.pluginId, lane.parameterId, baseline });
    }

    // Prime row-0 value immediately so playback starts from correct automation state.
    applyAutomationForPlaybackRow (automationData, 0);
}

void PluginAutomationManager::applyAutomationForPlaybackRow (const PatternAutomationData& automationData, int row)
{
    if (automationData.isEmpty())
        return;

    const float rowPosition = static_cast<float> (juce::jmax (0, row));

    for (const auto& lane : automationData.lanes)
    {
        if (lane.isEmpty())
            continue;

        auto* audioPlugin = resolvePluginInstance (lane.pluginId);
        if (audioPlugin == nullptr)
            continue;

        auto& params = audioPlugin->getParameters();
        if (lane.parameterId < 0 || lane.parameterId >= params.size())
            continue;

        auto* param = params[lane.parameterId];
        if (param == nullptr)
            continue;

        auto* tracked = findAutomatedParam (lane.pluginId, lane.parameterId);
        if (tracked == nullptr)
        {
            lastAutomatedParams.push_back ({ lane.pluginId, lane.parameterId, param->getValue() });
            tracked = &lastAutomatedParams.back();
        }

        const float value = lane.getValueAtRow (rowPosition, tracked->baselineValue);

        // Use tryEnter on the plugin's callback lock to avoid deadlocking
        // with the audio thread.  playInStopEnabled = true means the
        // playback graph is always live, so processBlock() can hold the
        // lock at any time.  If we can't get the lock we skip this tick;
        // the next timer callback (30 Hz) will try again.
        auto& lock = audioPlugin->getCallbackLock();
        if (lock.tryEnter())
        {
            param->setValue (value);
            lock.exit();
        }
    }
}

void PluginAutomationManager::resetAutomationParameters()
{
    for (auto& ap : lastAutomatedParams)
    {
        auto* audioPlugin = resolvePluginInstance (ap.pluginId);
        if (audioPlugin == nullptr)
            continue;

        auto& params = audioPlugin->getParameters();
        if (ap.paramIndex < 0 || ap.paramIndex >= params.size())
            continue;

        auto* param = params[ap.paramIndex];
        if (param == nullptr)
            continue;

        // Try-lock to avoid deadlocking with the audio thread.
        auto& lock = audioPlugin->getCallbackLock();
        if (lock.tryEnter())
        {
            param->setValue (ap.baselineValue);
            lock.exit();
        }
    }

    lastAutomatedParams.clear();
}
