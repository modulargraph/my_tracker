#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>

namespace
{
constexpr int kUsageError = 1;
constexpr int kFormatNotFound = 2;
constexpr int kPluginNotFound = 3;
constexpr int kInstantiationFailed = 4;
constexpr int kEditorUnavailable = 5;
constexpr int kTimedOut = 124;
constexpr int kDefaultTimeoutMs = 30000;

juce::String getArgValue (int argc, char* argv[], const juce::String& name)
{
    for (int i = 1; i + 1 < argc; ++i)
        if (juce::String (argv[i]) == name)
            return juce::String (argv[i + 1]);

    return {};
}

int getIntArgValue (int argc, char* argv[], const juce::String& name, int fallback)
{
    const auto value = getArgValue (argc, argv, name);
    return value.isEmpty() ? fallback : value.getIntValue();
}

class SmokeWatchdog final
{
public:
    explicit SmokeWatchdog (int timeoutMs)
    {
        if (timeoutMs <= 0)
            return;

        worker = std::thread ([this, timeoutMs]
        {
            std::unique_lock lock (mutex);

            if (! condition.wait_for (lock, std::chrono::milliseconds (timeoutMs), [this] { return finished; }))
            {
                juce::Logger::writeToLog ("PluginEditorSmoke timed out after "
                                          + juce::String (timeoutMs)
                                          + " ms");
                std::fflush (stdout);
                std::fflush (stderr);
                std::_Exit (kTimedOut);
            }
        });
    }

    ~SmokeWatchdog()
    {
        {
            const std::lock_guard lock (mutex);
            finished = true;
        }

        condition.notify_all();

        if (worker.joinable())
            worker.join();
    }

private:
    std::mutex mutex;
    std::condition_variable condition;
    bool finished = false;
    std::thread worker;
};

juce::AudioPluginFormat* findFormat (juce::AudioPluginFormatManager& manager,
                                     const juce::String& formatName)
{
    for (int i = 0; i < manager.getNumFormats(); ++i)
        if (auto* format = manager.getFormat (i))
            if (format->getName() == formatName)
                return format;

    return nullptr;
}

const juce::PluginDescription* findPluginDescription (const juce::OwnedArray<juce::PluginDescription>& descriptions,
                                                      const juce::String& nameNeedle)
{
    if (descriptions.isEmpty())
        return nullptr;

    if (nameNeedle.isEmpty())
        return descriptions[0];

    for (const auto* desc : descriptions)
        if (desc != nullptr && desc->name.containsIgnoreCase (nameNeedle))
            return desc;

    return nullptr;
}

struct SmokeWindow final : juce::DocumentWindow
{
    explicit SmokeWindow (const juce::String& name)
        : juce::DocumentWindow (name,
                                juce::Colours::darkgrey,
                                juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
    {
    }

    void closeButtonPressed() override
    {
        setVisible (false);
    }
};

int runSmoke (const juce::String& formatName,
              const juce::String& pluginIdentifier,
              const juce::String& pluginName,
              int cycleCount,
              int visibleMs,
              int hiddenMs,
              int holdMs)
{
    const auto startedAtMs = juce::Time::getMillisecondCounterHiRes();
    auto logStage = [startedAtMs] (const juce::String& message)
    {
        juce::Logger::writeToLog ("PluginEditorSmoke " + message + " at "
                                  + juce::String (juce::Time::getMillisecondCounterHiRes() - startedAtMs, 1)
                                  + " ms");
    };

    juce::AudioPluginFormatManager formatManager;
    juce::addDefaultFormatsToManager (formatManager);
    logStage ("created format manager");

    auto* format = findFormat (formatManager, formatName);
    if (format == nullptr)
        return kFormatNotFound;
    logStage ("found format");

    juce::OwnedArray<juce::PluginDescription> descriptions;
    format->findAllTypesForFile (descriptions, pluginIdentifier);
    logStage ("scanned plugin file");

    const auto* description = findPluginDescription (descriptions, pluginName);
    if (description == nullptr)
        return kPluginNotFound;
    logStage ("selected plugin description");

    const auto selectedPluginName = description->name;

    juce::String error;
    auto plugin = formatManager.createPluginInstance (*description, 44100.0, 512, error);
    if (plugin == nullptr)
    {
        juce::Logger::writeToLog ("Plugin instantiation failed: " + error);
        return kInstantiationFailed;
    }
    logStage ("instantiated plugin");

    auto* editor = plugin->createEditorIfNeeded();
    if (editor == nullptr)
        return kEditorUnavailable;
    logStage ("created editor");

    {
        cycleCount = juce::jmax (1, cycleCount);
        visibleMs = juce::jmax (0, visibleMs);
        hiddenMs = juce::jmax (0, hiddenMs);
        holdMs = juce::jmax (0, holdMs);

        SmokeWindow window (selectedPluginName);
        window.setContentOwned (editor, true);
        window.setResizable (true, false);
        window.centreWithSize (juce::jmax (240, window.getWidth()),
                               juce::jmax (160, window.getHeight()));

        for (int cycle = 0; cycle < cycleCount; ++cycle)
        {
            window.setVisible (true);
            window.toFront (true);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (visibleMs);
            logStage ("completed visible editor cycle " + juce::String (cycle + 1)
                      + "/" + juce::String (cycleCount));

            if (cycle < cycleCount - 1)
            {
                window.setVisible (false);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (hiddenMs);
            }
        }

        if (holdMs > 0)
        {
            logStage ("holding visible editor for screenshot/manual inspection");
            juce::MessageManager::getInstance()->runDispatchLoopUntil (holdMs);
        }

        window.setVisible (false);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (hiddenMs);
        window.clearContentComponent();
    }

    juce::MessageManager::getInstance()->runDispatchLoopUntil (250);
    plugin.reset();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (250);
    logStage ("destroyed editor and plugin");

    juce::Logger::writeToLog ("PluginEditorSmoke opened and reused editor for: "
                              + selectedPluginName
                              + " across "
                              + juce::String (cycleCount)
                              + " visible cycle(s)");
    return 0;
}
}

int main (int argc, char* argv[])
{
    const auto formatName = getArgValue (argc, argv, "--format");
    const auto pluginIdentifier = getArgValue (argc, argv, "--plugin");
    const auto pluginName = getArgValue (argc, argv, "--name");
    const auto timeoutMs = getIntArgValue (argc, argv, "--timeout-ms", kDefaultTimeoutMs);
    const auto cycleCount = getIntArgValue (argc, argv, "--cycles", 2);
    const auto visibleMs = getIntArgValue (argc, argv, "--visible-ms", 1000);
    const auto hiddenMs = getIntArgValue (argc, argv, "--hidden-ms", 250);
    const auto holdMs = getIntArgValue (argc, argv, "--hold-ms", 0);

    if (formatName.isEmpty() || pluginIdentifier.isEmpty())
    {
        juce::Logger::writeToLog ("Usage: PluginEditorSmoke --format VST3 --plugin /path/to/plugin.vst3 [--name Name] [--timeout-ms 30000] [--cycles 2] [--visible-ms 1000] [--hidden-ms 250] [--hold-ms 0]");
        return kUsageError;
    }

    SmokeWatchdog watchdog (timeoutMs);
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    return runSmoke (formatName, pluginIdentifier, pluginName, cycleCount, visibleMs, hiddenMs, holdMs);
}
