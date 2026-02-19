#pragma once

#include <JuceHeader.h>
#include "TrackerLookAndFeel.h"
#include "SimpleSampler.h"

class InstrumentPanel : public juce::Component
{
public:
    InstrumentPanel (TrackerLookAndFeel& lnf);

    void paint (juce::Graphics& g) override;
    bool keyPressed (const juce::KeyPress& key) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDoubleClick (const juce::MouseEvent& event) override;
    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    void setSelectedInstrument (int inst) { selectedInstrument = inst; repaint(); }
    int getSelectedInstrument() const { return selectedInstrument; }

    // Call to update the sample info shown
    void updateSampleInfo (const std::map<int, juce::File>& loadedSamples);

    // Callbacks
    std::function<void (int instrument)> onInstrumentSelected;
    std::function<void (int instrument)> onLoadSampleRequested;
    std::function<void (int instrument)> onEditSampleRequested;
    std::function<void (int instrument)> onClearSampleRequested;

    static constexpr int kPanelWidth = 180;

private:
    TrackerLookAndFeel& lookAndFeel;

    int selectedInstrument = 0;

    struct InstrumentSlot
    {
        juce::String sampleName;
        bool hasData = false;
    };
    std::array<InstrumentSlot, 256> slots {};

    static constexpr int kHeaderHeight = 28;
    static constexpr int kSlotHeight = 20;

    int scrollOffset = 0;
    int getVisibleSlotCount() const;

    void showContextMenu (int instrument, juce::Point<int> screenPos);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstrumentPanel)
};
