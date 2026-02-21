#if __APPLE__
 #include <CoreFoundation/CoreFoundation.h>
#endif

#include <cstring>

//==============================================================================
// Plugin bundle validation mode: launched as a child process to test-load
// a plugin bundle. If the plugin's static initializer crashes (e.g. throws
// a C++ exception through dyld), only this child process dies.
static int validatePluginBundle (const char* bundlePath)
{
   #if __APPLE__
    auto pathStr = CFStringCreateWithCString (nullptr, bundlePath, kCFStringEncodingUTF8);
    if (pathStr == nullptr)
        return 1;

    auto url = CFURLCreateWithFileSystemPath (nullptr, pathStr, kCFURLPOSIXPathStyle, true);
    CFRelease (pathStr);
    if (url == nullptr)
        return 1;

    auto bundle = CFBundleCreate (nullptr, url);
    CFRelease (url);
    if (bundle == nullptr)
        return 1;

    CFErrorRef error = nullptr;
    Boolean loaded = CFBundleLoadExecutableAndReturnError (bundle, &error);

    if (error != nullptr)
        CFRelease (error);

    CFRelease (bundle);
    return loaded ? 0 : 1;
   #else
    (void) bundlePath;
    return 0;
   #endif
}

#include <JuceHeader.h>
#include "MainComponent.h"

class TrackerAdjustApplication : public juce::JUCEApplication
{
public:
    TrackerAdjustApplication() {}

    const juce::String getApplicationName() override { return "Tracker Adjust"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
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

// Custom main() to intercept --validate-bundle before JUCE initialisation
int main (int argc, const char* argv[])
{
    if (argc >= 3 && std::strcmp (argv[1], "--validate-bundle") == 0)
        return validatePluginBundle (argv[2]);

    return juce::JUCEApplicationBase::main (argc, argv);
}
