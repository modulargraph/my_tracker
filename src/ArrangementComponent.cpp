#include "ArrangementComponent.h"

ArrangementComponent::ArrangementComponent (Arrangement& arr, PatternData& pd, TrackerLookAndFeel& lnf)
    : arrangement (arr), patternData (pd), lookAndFeel (lnf)
{
    setWantsKeyboardFocus (true);
}

void ArrangementComponent::paint (juce::Graphics& g)
{
    auto bg = lookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId);
    g.fillAll (bg.brighter (0.05f));

    // Header
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::headerColourId));
    g.fillRect (0, 0, getWidth(), kHeaderHeight);

    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::textColourId));
    g.setFont (lookAndFeel.getMonoFont (13.0f));
    g.drawText ("Arrangement", 8, 0, getWidth() - 16, kHeaderHeight, juce::Justification::centredLeft);

    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (kHeaderHeight - 1, 0.0f, static_cast<float> (getWidth()));

    // Entries
    g.setFont (lookAndFeel.getMonoFont (12.0f));

    for (int i = 0; i < arrangement.getNumEntries(); ++i)
    {
        int y = kHeaderHeight + i * kEntryHeight;
        if (y + kEntryHeight > getHeight()) break;

        auto& entry = arrangement.getEntry (i);

        // Highlight
        if (i == playingEntry)
        {
            g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::playbackCursorColourId));
            g.fillRect (0, y, getWidth(), kEntryHeight);
        }
        else if (i == selectedEntry)
        {
            g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::cursorCellColourId));
            g.fillRect (0, y, getWidth(), kEntryHeight);
        }

        // Entry text
        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::textColourId));

        juce::String patName;
        if (entry.patternIndex >= 0 && entry.patternIndex < patternData.getNumPatterns())
            patName = patternData.getPattern (entry.patternIndex).name;
        else
            patName = "???";

        auto text = juce::String::formatted ("%02d: [%s] x%d", i, patName.toRawUTF8(), entry.repeats);
        g.drawText (text, 8, y, getWidth() - 16, kEntryHeight, juce::Justification::centredLeft);

        // Separator
        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
        g.drawHorizontalLine (y + kEntryHeight - 1, 0.0f, static_cast<float> (getWidth()));
    }

    // Empty state
    if (arrangement.getNumEntries() == 0)
    {
        g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.4f));
        g.drawText ("(empty)", 8, kHeaderHeight + 8, getWidth() - 16, 20, juce::Justification::centredLeft);
        g.setFont (lookAndFeel.getMonoFont (10.0f));
        g.drawText ("Ins to add", 8, kHeaderHeight + 28, getWidth() - 16, 16, juce::Justification::centredLeft);
    }

    // Right border
    g.setColour (lookAndFeel.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawVerticalLine (getWidth() - 1, 0.0f, static_cast<float> (getHeight()));
}

void ArrangementComponent::resized()
{
}

void ArrangementComponent::mouseDown (const juce::MouseEvent& event)
{
    grabKeyboardFocus();

    if (event.y < kHeaderHeight) return;

    int idx = (event.y - kHeaderHeight) / kEntryHeight;
    if (idx >= 0 && idx < arrangement.getNumEntries())
    {
        selectedEntry = idx;
        repaint();

        if (event.mods.isPopupMenu())
            showEntryContextMenu (idx, event.getScreenPosition());
        else if (onSwitchToPattern)
            onSwitchToPattern (arrangement.getEntry (idx).patternIndex);
    }
}

bool ArrangementComponent::keyPressed (const juce::KeyPress& key)
{
    auto keyCode = key.getKeyCode();

    // Insert: add entry at selected position (or end)
    if (keyCode == 0x100000a || key.getTextCharacter() == '+') // Insert key
    {
        int patIdx = patternData.getCurrentPatternIndex();
        int pos = (selectedEntry >= 0) ? selectedEntry + 1 : arrangement.getNumEntries();
        arrangement.insertEntry (pos, patIdx);
        selectedEntry = pos;
        repaint();
        return true;
    }

    // Delete: remove selected entry
    if ((keyCode == juce::KeyPress::deleteKey || keyCode == juce::KeyPress::backspaceKey) && selectedEntry >= 0)
    {
        arrangement.removeEntry (selectedEntry);
        if (selectedEntry >= arrangement.getNumEntries())
            selectedEntry = arrangement.getNumEntries() - 1;
        repaint();
        return true;
    }

    // Up/Down to navigate
    if (keyCode == juce::KeyPress::upKey && selectedEntry > 0)
    {
        selectedEntry--;
        repaint();
        if (onSwitchToPattern)
            onSwitchToPattern (arrangement.getEntry (selectedEntry).patternIndex);
        return true;
    }
    if (keyCode == juce::KeyPress::downKey && selectedEntry < arrangement.getNumEntries() - 1)
    {
        selectedEntry++;
        repaint();
        if (onSwitchToPattern)
            onSwitchToPattern (arrangement.getEntry (selectedEntry).patternIndex);
        return true;
    }

    return false;
}

void ArrangementComponent::showEntryContextMenu (int index, juce::Point<int> screenPos)
{
    juce::PopupMenu menu;
    menu.addItem (1, "Remove");
    menu.addItem (2, "Move Up", index > 0);
    menu.addItem (3, "Move Down", index < arrangement.getNumEntries() - 1);
    menu.addSeparator();

    juce::PopupMenu repeatsMenu;
    for (int r = 1; r <= 8; ++r)
        repeatsMenu.addItem (100 + r, juce::String (r) + "x",
                             true, r == arrangement.getEntry (index).repeats);
    menu.addSubMenu ("Repeats", repeatsMenu);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }),
                        [this, index] (int result)
                        {
                            if (result == 1)
                            {
                                arrangement.removeEntry (index);
                                if (selectedEntry >= arrangement.getNumEntries())
                                    selectedEntry = arrangement.getNumEntries() - 1;
                            }
                            else if (result == 2)
                            {
                                arrangement.moveEntryUp (index);
                                selectedEntry = index - 1;
                            }
                            else if (result == 3)
                            {
                                arrangement.moveEntryDown (index);
                                selectedEntry = index + 1;
                            }
                            else if (result > 100 && result <= 108)
                            {
                                arrangement.getEntry (index).repeats = result - 100;
                            }
                            repaint();
                        });
}
