#include <JuceHeader.h>
#include "MainComponent.h"

#if JUCE_MAC
 #include <dlfcn.h>
#endif

//==============================================================================
// Plugin bundle validation: test-load a plugin bundle via CoreFoundation.
// Used by child processes launched with --validate-bundle <path>.
// If the plugin's static initializer crashes, only the child process dies.
static int validatePluginBundle (const juce::String& bundlePath)
{
   #if JUCE_MAC
    // Use JUCE's ObjC helpers to load a bundle — these call through to
    // CFBundleCreate / CFBundleLoadExecutableAndReturnError internally,
    // matching the same code path that JUCE's DLLHandle::open() uses.
    juce::File bundleFile (bundlePath);
    if (! bundleFile.exists())
        return 1;

    // Try to load the bundle as a dynamic library (same as JUCE's plugin loading)
    void* handle = dlopen (bundleFile.getFullPathName().toRawUTF8(), RTLD_LAZY | RTLD_LOCAL);
    if (handle != nullptr)
    {
        dlclose (handle);
        return 0;
    }

    return 1;
   #else
    juce::ignoreUnused (bundlePath);
    return 0;
   #endif
}

class TrackerAdjustApplication : public juce::JUCEApplication
{
public:
    TrackerAdjustApplication() {}

    const juce::String getApplicationName() override { return "Tracker Adjust"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& commandLine) override
    {
        // Child-process mode: validate a plugin bundle and exit
        if (commandLine.contains ("--validate-bundle"))
        {
            auto bundlePath = commandLine.fromFirstOccurrenceOf ("--validate-bundle", false, false).trim();
            int result = validatePluginBundle (bundlePath);
            setApplicationReturnValue (result);
            quit();
            return;
        }

        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
        {
            auto* mc = dynamic_cast<MainComponent*> (mainWindow->getContentComponent());
            if (mc != nullptr && ! mc->confirmDiscardChanges())
                return;
        }
        quit();
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (juce::String name)
            : DocumentWindow (name,
                              juce::Colour (0xff1a1a1a),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (TrackerAdjustApplication)
