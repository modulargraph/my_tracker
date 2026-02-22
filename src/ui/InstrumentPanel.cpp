#include "InstrumentPanel.h"
#include <cmath>

InstrumentPanel::InstrumentPanel (TrackerLookAndFeel& lnf)
    : lookAndFeel (lnf)
{
    setWantsKeyboardFocus (true);
}

int InstrumentPanel::getVisibleSlotCount() const
{
    return juce::jmax (1, (getHeight() - kHeaderHeight) / kSlotHeight);
}

void InstrumentPanel::updateSampleInfo (const std::map<int, juce::File>& loadedSamples)
{
    for (auto& slot : slots)
    {
        if (! slot.isPlugin)
        {
            slot.sampleName = "";
            slot.hasData = false;
        }
    }

    for (auto& [index, file] : loadedSamples)
    {
        if (index >= 0 && index < 256)
        {
            auto& slot = slots[static_cast<size_t> (index)];
            if (! slot.isPlugin)
            {
                slot.sampleName = file.getFileNameWithoutExtension();
                slot.hasData = true;
            }
        }
    }

    repaint();
}

void InstrumentPanel::updatePluginInfo (const std::map<int, InstrumentSlotInfo>& slotInfos)
{
    // First clear all plugin flags
    for (auto& slot : slots)
    {
        slot.isPlugin = false;
        slot.pluginName.clear();
        slot.ownerTrack = -1;
    }

    // Then set plugin info
    for (auto& [index, info] : slotInfos)
    {
        if (index >= 0 && index < 256 && info.isPlugin())
        {
            auto& slot = slots[static_cast<size_t> (index)];
            slot.isPlugin = true;
            slot.pluginName = info.pluginDescription.name;
            slot.ownerTrack = info.ownerTrack;
            slot.hasData = true;
            slot.sampleName.clear(); // Not a sample
        }
    }

    repaint();
}

void InstrumentPanel::paint (juce::Graphics& g)
{
    auto bg = lookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId);
    g.fillAll (bg.brighter (0.03f));

    // Left border
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawVerticalLine (0, 0.0f, static_cast<float> (getHeight()));

    // Header
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::headerColourId));
    g.fillRect (0, 0, getWidth(), kHeaderHeight);

    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::textColourId));
    g.setFont (lookAndFeel.getMonoFont (13.0f));
    g.drawText ("Instruments", 8, 0, getWidth() - 16, kHeaderHeight, juce::Justification::centredLeft);

    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (kHeaderHeight - 1, 0.0f, static_cast<float> (getWidth()));

    // Instrument slots
    g.setFont (lookAndFeel.getMonoFont (11.0f));
    int visibleSlots = getVisibleSlotCount();

    for (int i = 0; i < visibleSlots; ++i)
    {
        int inst = scrollOffset + i;
        if (inst >= 256) break;

        int y = kHeaderHeight + i * kSlotHeight;
        auto& slot = slots[static_cast<size_t> (inst)];

        // Selected highlight
        if (inst == selectedInstrument)
        {
            g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::cursorCellColourId));
            g.fillRect (1, y, getWidth() - 1, kSlotHeight);
        }

        // Index with type indicator
        auto indexColour = lookAndFeel.findColour (TrackerLookAndFeel::instrumentColourId);
        if (slot.isPlugin)
            indexColour = juce::Colour (0xff89b4fa); // Blue tint for plugin instruments
        g.setColour (indexColour.withAlpha (slot.hasData ? 1.0f : 0.4f));
        g.drawText (juce::String::formatted ("%02X", inst), 6, y, 22, kSlotHeight,
                    juce::Justification::centredLeft);

        // Name display
        if (slot.isPlugin)
        {
            // Plugin instrument: show plugin name with type indicator
            g.setColour (juce::Colour (0xff89b4fa));
            auto truncName = slot.pluginName.substring (0, 14);
            g.drawText (truncName, 32, y, getWidth() - 38, kSlotHeight,
                        juce::Justification::centredLeft);
        }
        else if (slot.hasData)
        {
            g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::textColourId));
            auto truncName = slot.sampleName.substring (0, 16);
            g.drawText (truncName, 32, y, getWidth() - 38, kSlotHeight,
                        juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.2f));
            g.drawText ("---", 32, y, getWidth() - 38, kSlotHeight,
                        juce::Justification::centredLeft);
        }

        // Bottom line
        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId).withAlpha (0.5f));
        g.drawHorizontalLine (y + kSlotHeight - 1, 1.0f, static_cast<float> (getWidth()));
    }
}

void InstrumentPanel::mouseDown (const juce::MouseEvent& event)
{
    if (event.y < kHeaderHeight) return;

    int idx = (event.y - kHeaderHeight) / kSlotHeight + scrollOffset;
    if (idx < 0 || idx >= 256) return;

    selectedInstrument = idx;
    repaint();

    // Keep tracker/global instrument selection in sync with panel selection
    // for both left-click and right-click context-menu actions.
    if (onInstrumentSelected)
        onInstrumentSelected (idx);

    if (event.mods.isPopupMenu())
    {
        // Two-finger secondary click on trackpads can emit a tiny wheel event
        // around the same gesture. Suppress wheel briefly to keep slot stable.
        suppressWheelUntilMs = juce::Time::getMillisecondCounter() + 250;
        showContextMenu (idx, event.getScreenPosition());
    }
}

void InstrumentPanel::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (event.y < kHeaderHeight) return;

    int idx = (event.y - kHeaderHeight) / kSlotHeight + scrollOffset;
    if (idx < 0 || idx >= 256) return;

    auto& slot = slots[static_cast<size_t> (idx)];

    if (slot.isPlugin && onOpenPluginEditorRequested)
    {
        onOpenPluginEditorRequested (idx);
    }
    else if (slot.hasData && onEditSampleRequested)
    {
        onEditSampleRequested (idx);
    }
}

bool InstrumentPanel::keyPressed (const juce::KeyPress& key)
{
    auto keyCode = key.getKeyCode();

    if (keyCode == juce::KeyPress::upKey)
    {
        selectedInstrument = juce::jmax (0, selectedInstrument - 1);
        if (selectedInstrument < scrollOffset)
            scrollOffset = selectedInstrument;
        repaint();
        if (onInstrumentSelected)
            onInstrumentSelected (selectedInstrument);
        return true;
    }
    if (keyCode == juce::KeyPress::downKey)
    {
        selectedInstrument = juce::jmin (255, selectedInstrument + 1);
        int visibleSlots = getVisibleSlotCount();
        if (selectedInstrument >= scrollOffset + visibleSlots)
            scrollOffset = selectedInstrument - visibleSlots + 1;
        repaint();
        if (onInstrumentSelected)
            onInstrumentSelected (selectedInstrument);
        return true;
    }
    if (keyCode == juce::KeyPress::pageUpKey)
    {
        selectedInstrument = juce::jmax (0, selectedInstrument - getVisibleSlotCount());
        if (selectedInstrument < scrollOffset)
            scrollOffset = selectedInstrument;
        repaint();
        if (onInstrumentSelected)
            onInstrumentSelected (selectedInstrument);
        return true;
    }
    if (keyCode == juce::KeyPress::pageDownKey)
    {
        selectedInstrument = juce::jmin (255, selectedInstrument + getVisibleSlotCount());
        int visibleSlots = getVisibleSlotCount();
        if (selectedInstrument >= scrollOffset + visibleSlots)
            scrollOffset = selectedInstrument - visibleSlots + 1;
        repaint();
        if (onInstrumentSelected)
            onInstrumentSelected (selectedInstrument);
        return true;
    }
    if (keyCode == juce::KeyPress::returnKey)
    {
        auto& slot = slots[static_cast<size_t> (selectedInstrument)];
        if (slot.isPlugin && onOpenPluginEditorRequested)
            onOpenPluginEditorRequested (selectedInstrument);
        else if (slot.hasData && onEditSampleRequested)
            onEditSampleRequested (selectedInstrument);
        else if (onLoadSampleRequested)
            onLoadSampleRequested (selectedInstrument);
        return true;
    }

    return false;
}

void InstrumentPanel::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (juce::Time::getMillisecondCounter() < suppressWheelUntilMs)
        return;

    int delta = 0;
    if (wheel.isSmooth)
    {
        // Ignore micro-jitter from resting fingers on a trackpad.
        if (std::abs (wheel.deltaY) < 0.05f)
            return;

        // Accumulate smooth wheel movement; only scroll when enough motion has built up.
        smoothScrollCarry += (-wheel.deltaY * 4.0f);
        delta = static_cast<int> (smoothScrollCarry);
        smoothScrollCarry -= static_cast<float> (delta);

        if (delta == 0)
            return;
    }
    else
    {
        smoothScrollCarry = 0.0f;
        delta = (wheel.deltaY > 0) ? -3 : 3;
    }

    int maxScroll = juce::jmax (0, 256 - getVisibleSlotCount());
    scrollOffset = juce::jlimit (0, maxScroll, scrollOffset + delta);
    repaint();
}

void InstrumentPanel::showContextMenu (int instrument, juce::Point<int> screenPos)
{
    auto& slot = slots[static_cast<size_t> (instrument)];

    juce::PopupMenu menu;
    menu.addItem (1, "Load Sample...");

    if (slot.hasData && ! slot.isPlugin)
        menu.addItem (2, "Clear Sample");

    menu.addSeparator();
    menu.addItem (3, "Set Plugin Instrument...");

    if (slot.isPlugin)
    {
        menu.addItem (4, "Open Plugin Editor");
        menu.addItem (5, "Clear Plugin Instrument");
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }),
                        [this, instrument] (int result)
                        {
                            if (result == 1 && onLoadSampleRequested)
                                onLoadSampleRequested (instrument);
                            else if (result == 2 && onClearSampleRequested)
                                onClearSampleRequested (instrument);
                            else if (result == 3 && onSetPluginInstrumentRequested)
                                onSetPluginInstrumentRequested (instrument);
                            else if (result == 4 && onOpenPluginEditorRequested)
                                onOpenPluginEditorRequested (instrument);
                            else if (result == 5 && onClearPluginInstrumentRequested)
                                onClearPluginInstrumentRequested (instrument);
                        });
}
