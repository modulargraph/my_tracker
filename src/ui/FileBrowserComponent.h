#pragma once

#include <JuceHeader.h>
#include "TrackerLookAndFeel.h"

class SampleBrowserComponent : public juce::Component
{
public:
    SampleBrowserComponent (TrackerLookAndFeel& lnf);

    void paint (juce::Graphics& g) override;
    void resized() override {}
    bool keyPressed (const juce::KeyPress& key) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDoubleClick (const juce::MouseEvent& event) override;
    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    void setCurrentDirectory (const juce::File& dir);
    void updateInstrumentSlots (const std::map<int, juce::File>& loadedSamples);
    void setSelectedInstrument (int inst);

    // Callback: instrument index + file to load
    std::function<void (int instrument, const juce::File& file)> onLoadSample;

    // Callbacks for audio file preview
    std::function<void (const juce::File& file)> onPreviewFile;
    std::function<void (int instrumentIndex)> onPreviewInstrument;
    std::function<void()> onStopPreview;

    // Callback when the browsed directory changes
    std::function<void (const juce::File& dir)> onDirectoryChanged;

    // Get the current directory path
    juce::File getCurrentDirectory() const { return currentDirectory; }

    // Auto-advance to next empty instrument slot after loading
    bool autoAdvance = true;
    void advanceToNextEmptySlot();

private:
    TrackerLookAndFeel& lookAndFeel;

    enum class Pane { Files, Instruments };
    Pane activePane = Pane::Files;

    // --- File pane ---
    struct FileEntry
    {
        juce::String name;
        juce::File file;
        bool isDirectory = false;
        bool isParent = false;
        juce::String sizeStr;
        juce::String formatStr;
    };
    std::vector<FileEntry> fileEntries;
    juce::File currentDirectory;
    int fileSelection = 0;
    int fileScrollOffset = 0;

    void refreshFileList();
    void navigateInto (const juce::File& dir);
    void loadSelectedFile();
    bool isAudioFile (const juce::File& f) const;
    juce::String formatFileSize (int64_t bytes) const;

    // --- Instrument pane ---
    struct InstrumentSlot
    {
        juce::String sampleName;
        bool hasData = false;
    };
    std::array<InstrumentSlot, 256> instrumentSlots {};
    int instrumentSelection = 0;
    int instrumentScrollOffset = 0;

    // --- Layout ---
    static constexpr int kHeaderHeight = 24;
    static constexpr int kRowHeight = 20;
    static constexpr int kInfoBarHeight = 24;
    static constexpr float kFilePaneRatio = 0.6f;

    int getFileVisibleRows() const;
    int getInstrumentVisibleRows() const;
    juce::Rectangle<int> getFilePaneBounds() const;
    juce::Rectangle<int> getInstrumentPaneBounds() const;
    juce::Rectangle<int> getInfoBarBounds() const;

    void paintFilePane (juce::Graphics& g, juce::Rectangle<int> bounds);
    void paintInstrumentPane (juce::Graphics& g, juce::Rectangle<int> bounds);
    void paintInfoBar (juce::Graphics& g, juce::Rectangle<int> bounds);

    void ensureFileSelectionVisible();
    void ensureInstrumentSelectionVisible();
    void triggerPreviewForSelection();
    void triggerInstrumentPreview();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleBrowserComponent)
};
