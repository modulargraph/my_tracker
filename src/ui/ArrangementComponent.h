#pragma once

#include <JuceHeader.h>
#include "Arrangement.h"
#include "PatternData.h"
#include "TrackerLookAndFeel.h"

class ArrangementComponent : public juce::Component
{
public:
    ArrangementComponent (Arrangement& arrangement, PatternData& patternData, TrackerLookAndFeel& lnf);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;
    bool keyPressed (const juce::KeyPress& key) override;

    int getSelectedEntry() const { return selectedEntry; }
    void setSelectedEntry (int idx) { selectedEntry = idx; repaint(); }

    // Highlight which entry is currently playing
    void setPlayingEntry (int idx) { playingEntry = idx; repaint(); }

    std::function<void (int patternIndex)> onSwitchToPattern;
    std::function<void()> onAddEntryRequested;

    static constexpr int kPanelWidth = 200;

private:
    Arrangement& arrangement;
    PatternData& patternData;
    TrackerLookAndFeel& lookAndFeel;

    int selectedEntry = -1;
    int playingEntry = -1;

    static constexpr int kEntryHeight = 24;
    static constexpr int kHeaderHeight = 28;

    juce::TextButton addButton { "+" };

    void showEntryContextMenu (int index, juce::Point<int> screenPos);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangementComponent)
};
