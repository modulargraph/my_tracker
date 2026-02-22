#include "PluginInstrumentEditorContent.h"
#include "TrackerEngine.h"

//==============================================================================
// PluginEditorContent
//==============================================================================

PluginEditorContent::PluginEditorContent (juce::AudioProcessorEditor* ed,
                                          juce::AudioPluginInstance* api,
                                          TrackerEngine& eng,
                                          int instIdx)
    : vstEditor (ed), pluginInstance (api), engine (eng), instrumentIndex (instIdx)
{
    addAndMakeVisible (vstEditor);
    addKeyHookToComponentTree (*vstEditor);

    previewKbButton.setButtonText ("Preview KB");
    previewKbButton.setClickingTogglesState (true);
    previewKbButton.setWantsKeyboardFocus (false);
    previewKbButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::steelblue);
    previewKbButton.onClick = [this]
    {
        setPreviewKeyboardEnabled (previewKbButton.getToggleState());
    };
    addAndMakeVisible (previewKbButton);

    autoLearnButton.setButtonText ("Auto Learn");
    autoLearnButton.setClickingTogglesState (true);
    autoLearnButton.setWantsKeyboardFocus (false);
    autoLearnButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::orange);
    autoLearnButton.onClick = [this]
    {
        bool enabled = autoLearnButton.getToggleState();
        autoLearnEnabled = enabled;
        lastDispatchedAutoLearnParam = -1;

        if (enabled)
            captureAutoLearnSnapshot();

        updatePollingTimerState();
    };
    addAndMakeVisible (autoLearnButton);

    octaveLabel.setText ("Oct: " + juce::String (currentOctave), juce::dontSendNotification);
    octaveLabel.setWantsKeyboardFocus (false);
    octaveLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (octaveLabel);

    setWantsKeyboardFocus (true);
    previewKbButton.setToggleState (true, juce::dontSendNotification);
    setPreviewKeyboardEnabled (true);

    auto edW = vstEditor->getWidth();
    auto edH = vstEditor->getHeight();
    setSize (juce::jmax (edW, 300), edH + kToolbarHeight);
}

PluginEditorContent::~PluginEditorContent()
{
    stopTimer();
    releaseHeldPreviewNotes();
    removeKeyHookFromComponentTree (*vstEditor);
}

void PluginEditorContent::resized()
{
    addKeyHookToComponentTree (*vstEditor);

    auto area = getLocalBounds();
    auto toolbar = area.removeFromBottom (kToolbarHeight);

    vstEditor->setBounds (area);

    previewKbButton.setBounds (toolbar.removeFromLeft (100).reduced (4));
    octaveLabel.setBounds (toolbar.removeFromLeft (60).reduced (4));
    autoLearnButton.setBounds (toolbar.removeFromLeft (100).reduced (4));
}

bool PluginEditorContent::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    if (! previewKbButton.getToggleState())
        return false;

    if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown() || key.getModifiers().isAltDown())
        return false;

    // Octave change: F1-F8
    auto keyCode = key.getKeyCode();
    if (keyCode >= juce::KeyPress::F1Key && keyCode <= juce::KeyPress::F8Key)
    {
        currentOctave = keyCode - juce::KeyPress::F1Key;
        octaveLabel.setText ("Oct: " + juce::String (currentOctave), juce::dontSendNotification);
        return true;
    }

    int note = getMappedNoteForKeyCode (keyCode);
    if (note < 0 || note > 127)
        return false;

    auto pressedKeyCode = (keyCode >= 'a' && keyCode <= 'z') ? keyCode - ('a' - 'A') : keyCode;
    if (heldNotesByKeyCode.find (pressedKeyCode) == heldNotesByKeyCode.end())
    {
        engine.previewNote (0, instrumentIndex, note, false);
        heldNotesByKeyCode[pressedKeyCode] = note;
    }
    return true;
}

bool PluginEditorContent::keyStateChanged (bool isKeyDown, juce::Component*)
{
    if (! previewKbButton.getToggleState())
        return false;

    // Check which held notes are no longer pressed
    bool handled = false;
    auto it = heldNotesByKeyCode.begin();
    while (it != heldNotesByKeyCode.end())
    {
        bool stillDown = juce::KeyPress::isKeyCurrentlyDown (it->first);
        if (! stillDown)
        {
            engine.stopPreview();
            it = heldNotesByKeyCode.erase (it);
            handled = true;
        }
        else
        {
            ++it;
        }
    }

    juce::ignoreUnused (isKeyDown);
    return handled;
}

void PluginEditorContent::timerCallback()
{
    pollAutoLearnParameterChanges();

    if (! previewKeyboardEnabled)
        return;

    // Don't keep sounding notes if this editor window loses focus.
    if (auto* topLevel = findParentComponentOfClass<juce::TopLevelWindow>())
    {
        if (! topLevel->isActiveWindow())
        {
            releaseHeldPreviewNotes();
            return;
        }
    }

    pollOctaveKeys();
    pollMappedNoteKeys();
}

void PluginEditorContent::flushAutoLearnNavigation (int parameterIndex)
{
    if (! autoLearnEnabled)
        return;

    if (parameterIndex < 0)
        return;
    if (parameterIndex == lastDispatchedAutoLearnParam)
        return;

    lastDispatchedAutoLearnParam = parameterIndex;

    if (engine.onNavigateToAutomation)
    {
        auto pluginId = "inst:" + juce::String (instrumentIndex);
        engine.onNavigateToAutomation (pluginId, parameterIndex);
    }

    // One-shot learn: after capturing a parameter, return to idle mode.
    if (autoLearnButton.getToggleState())
    {
        autoLearnButton.setToggleState (false, juce::dontSendNotification);
        autoLearnEnabled = false;
        updatePollingTimerState();
    }
}

void PluginEditorContent::captureAutoLearnSnapshot()
{
    autoLearnParamSnapshot.clear();

    if (pluginInstance == nullptr)
        return;

    // tryEnter: audio thread may hold the callback lock (playInStopEnabled).
    auto& lock = pluginInstance->getCallbackLock();
    if (! lock.tryEnter())
        return;

    auto& params = pluginInstance->getParameters();
    autoLearnParamSnapshot.reserve (static_cast<size_t> (params.size()));

    for (int i = 0; i < params.size(); ++i)
    {
        auto* p = params[i];
        autoLearnParamSnapshot.push_back (p != nullptr ? p->getValue() : 0.0f);
    }

    lock.exit();
}

void PluginEditorContent::pollAutoLearnParameterChanges()
{
    if (! autoLearnEnabled || pluginInstance == nullptr)
        return;

    // tryEnter: audio thread may hold the callback lock (playInStopEnabled).
    // If we can't get the lock, skip this poll cycle — the next timer
    // tick will try again.
    auto& lock = pluginInstance->getCallbackLock();
    if (! lock.tryEnter())
        return;

    auto& params = pluginInstance->getParameters();
    if (params.isEmpty())
    {
        lock.exit();
        return;
    }

    if (autoLearnParamSnapshot.size() != static_cast<size_t> (params.size()))
    {
        lock.exit();
        captureAutoLearnSnapshot();
        return;
    }

    constexpr float kLearnThreshold = 0.004f;
    int changedParam = -1;
    float maxDelta = kLearnThreshold;

    for (int i = 0; i < params.size(); ++i)
    {
        auto* p = params[i];
        if (p == nullptr)
            continue;

        float current = p->getValue();
        float delta = std::abs (current - autoLearnParamSnapshot[static_cast<size_t> (i)]);
        autoLearnParamSnapshot[static_cast<size_t> (i)] = current;

        if (delta > maxDelta)
        {
            maxDelta = delta;
            changedParam = i;
        }
    }

    lock.exit();

    if (changedParam >= 0)
        flushAutoLearnNavigation (changedParam);
}

int PluginEditorContent::getMappedNoteForKeyCode (int keyCode) const
{
    return NoteUtils::keyCodeToNote (keyCode, currentOctave);
}

void PluginEditorContent::releaseHeldPreviewNotes()
{
    if (! heldNotesByKeyCode.empty())
        engine.stopPreview();

    heldNotesByKeyCode.clear();
}

void PluginEditorContent::setPreviewKeyboardEnabled (bool enabled)
{
    previewKeyboardEnabled = enabled;

    if (! enabled)
    {
        releaseHeldPreviewNotes();
        for (bool& down : octaveKeysDown)
            down = false;
    }
    else
    {
        grabKeyboardFocus();
    }

    updatePollingTimerState();
}

void PluginEditorContent::updatePollingTimerState()
{
    bool shouldPoll = previewKeyboardEnabled
                      || autoLearnEnabled;

    if (shouldPoll)
        startTimerHz (75);
    else
        stopTimer();
}

void PluginEditorContent::pollOctaveKeys()
{
    for (int i = 0; i < 8; ++i)
    {
        int keyCode = juce::KeyPress::F1Key + i;
        bool down = juce::KeyPress::isKeyCurrentlyDown (keyCode);

        if (down && ! octaveKeysDown[i])
        {
            currentOctave = i;
            octaveLabel.setText ("Oct: " + juce::String (currentOctave), juce::dontSendNotification);
        }

        octaveKeysDown[i] = down;
    }
}

void PluginEditorContent::pollMappedNoteKeys()
{
    for (auto keyCode : NoteUtils::kMappedKeyCodes)
    {
        bool down = juce::KeyPress::isKeyCurrentlyDown (keyCode);
        auto it = heldNotesByKeyCode.find (keyCode);

        if (down && it == heldNotesByKeyCode.end())
        {
            int note = getMappedNoteForKeyCode (keyCode);
            if (note >= 0 && note <= 127)
            {
                engine.previewNote (0, instrumentIndex, note, false);
                heldNotesByKeyCode[keyCode] = note;
            }
        }
        else if (! down && it != heldNotesByKeyCode.end())
        {
            engine.stopPreview();
            heldNotesByKeyCode.erase (it);
        }
    }
}

void PluginEditorContent::addKeyHookToComponentTree (juce::Component& component)
{
    component.addKeyListener (this);
    for (int i = 0; i < component.getNumChildComponents(); ++i)
        addKeyHookToComponentTree (*component.getChildComponent (i));
}

void PluginEditorContent::removeKeyHookFromComponentTree (juce::Component& component)
{
    component.removeKeyListener (this);
    for (int i = 0; i < component.getNumChildComponents(); ++i)
        removeKeyHookFromComponentTree (*component.getChildComponent (i));
}

//==============================================================================
// PluginInstrumentEditorWindow
//==============================================================================

PluginInstrumentEditorWindow::PluginInstrumentEditorWindow (const juce::String& name)
    : juce::DocumentWindow (name, juce::Colours::darkgrey,
                            juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
{
}

void PluginInstrumentEditorWindow::closeButtonPressed()
{
    // Hide instead of destroy to avoid repeated editor teardown races.
    setVisible (false);
}
