#pragma once

#include <JuceHeader.h>
#include "TrackerLookAndFeel.h"

enum class Tab { Tracker, InstrumentEdit, InstrumentType, Effects, Browser };

class TabBarComponent : public juce::Component
{
public:
    TabBarComponent (TrackerLookAndFeel& lnf);

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& event) override;

    void setActiveTab (Tab tab);
    Tab getActiveTab() const { return activeTab; }

    std::function<void (Tab)> onTabChanged;

    static constexpr int kTabBarHeight = 26;

private:
    TrackerLookAndFeel& lookAndFeel;
    Tab activeTab = Tab::Tracker;

    struct TabInfo
    {
        juce::String label;
        Tab tab;
        juce::Rectangle<int> bounds;
    };
    std::array<TabInfo, 5> tabs;

    static constexpr int kTabWidth = 100;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TabBarComponent)
};
