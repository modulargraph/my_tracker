#include "TabBarComponent.h"

TabBarComponent::TabBarComponent (TrackerLookAndFeel& lnf)
    : lookAndFeel (lnf)
{
    tabs[0] = { "TRACKER",     Tab::Tracker,        {} };
    tabs[1] = { "INST EDIT",   Tab::InstrumentEdit, {} };
    tabs[2] = { "INST TYPE",   Tab::InstrumentType, {} };
    tabs[3] = { "BROWSER",     Tab::Browser,        {} };
}

void TabBarComponent::paint (juce::Graphics& g)
{
    auto bg = lookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId).brighter (0.05f);
    g.fillAll (bg);

    // Bottom border
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (getHeight() - 1, 0.0f, static_cast<float> (getWidth()));

    auto textCol = lookAndFeel.findColour (TrackerLookAndFeel::textColourId);
    auto accentCol = lookAndFeel.findColour (TrackerLookAndFeel::fxColourId);

    g.setFont (lookAndFeel.getMonoFont (11.0f));

    int x = 8;
    for (size_t i = 0; i < tabs.size(); ++i)
    {
        auto& tab = tabs[i];
        tab.bounds = { x, 0, kTabWidth, getHeight() };

        bool isActive = (tab.tab == activeTab);

        // Tab label
        g.setColour (isActive ? textCol : textCol.withAlpha (0.4f));
        g.drawText (tab.label, tab.bounds.withTrimmedBottom (3), juce::Justification::centred);

        // Active underline
        if (isActive)
        {
            g.setColour (accentCol);
            g.fillRect (tab.bounds.getX() + 4, getHeight() - 2, tab.bounds.getWidth() - 8, 2);
        }

        x += kTabWidth + 4;
    }
}

void TabBarComponent::mouseDown (const juce::MouseEvent& event)
{
    for (auto& tab : tabs)
    {
        if (tab.bounds.contains (event.getPosition()))
        {
            setActiveTab (tab.tab);
            return;
        }
    }
}

void TabBarComponent::setActiveTab (Tab tab)
{
    if (activeTab == tab) return;
    activeTab = tab;
    repaint();
    if (onTabChanged)
        onTabChanged (activeTab);
}
