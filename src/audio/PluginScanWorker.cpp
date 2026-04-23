#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace
{
constexpr int kUsageError = 1;
constexpr int kFormatNotFound = 2;
constexpr int kScanError = 3;
constexpr int kWriteError = 4;

struct Arguments
{
    juce::String formatName;
    juce::String pluginIdentifier;
    juce::File outputFile;
};

juce::String getArgValue (int argc, char* argv[], const juce::String& name)
{
    for (int i = 1; i + 1 < argc; ++i)
        if (juce::String (argv[i]) == name)
            return juce::String (argv[i + 1]);

    return {};
}

std::optional<Arguments> parseArguments (int argc, char* argv[])
{
    Arguments args;
    args.formatName = getArgValue (argc, argv, "--format");
    args.pluginIdentifier = getArgValue (argc, argv, "--plugin");
    args.outputFile = juce::File (getArgValue (argc, argv, "--output"));

    if (args.formatName.isEmpty()
        || args.pluginIdentifier.isEmpty()
        || args.outputFile.getFullPathName().isEmpty())
        return std::nullopt;

    return args;
}

juce::AudioPluginFormat* findFormat (juce::AudioPluginFormatManager& manager,
                                     const juce::String& formatName)
{
    for (int i = 0; i < manager.getNumFormats(); ++i)
        if (auto* format = manager.getFormat (i))
            if (format->getName() == formatName)
                return format;

    return nullptr;
}
} // namespace

int main (int argc, char* argv[])
{
    auto args = parseArguments (argc, argv);

    if (! args.has_value())
        return kUsageError;

    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    try
    {
        juce::AudioPluginFormatManager formatManager;
        juce::addDefaultFormatsToManager (formatManager);

        auto* format = findFormat (formatManager, args->formatName);

        if (format == nullptr)
            return kFormatNotFound;

        juce::OwnedArray<juce::PluginDescription> foundTypes;
        format->findAllTypesForFile (foundTypes, args->pluginIdentifier);

        juce::KnownPluginList resultList;

        for (auto* description : foundTypes)
            if (description != nullptr)
                resultList.addType (*description);

        auto xml = resultList.createXml();
        xml->setAttribute ("format", args->formatName);
        xml->setAttribute ("plugin", args->pluginIdentifier);

        args->outputFile.getParentDirectory().createDirectory();

        if (! xml->writeTo (args->outputFile))
            return kWriteError;

        return 0;
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog ("PluginScanWorker exception: " + juce::String (e.what()));
    }
    catch (...)
    {
        juce::Logger::writeToLog ("PluginScanWorker unknown exception");
    }

    return kScanError;
}
