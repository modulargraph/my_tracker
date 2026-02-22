#pragma once

#include <JuceHeader.h>
#include "NoteUtils.h"

class TrackerEngine;

//==============================================================================
// Content component: wraps the VST editor + toolbar at the bottom.
//==============================================================================
struct PluginEditorContent : public juce::Component,
                             public juce::KeyListener,
                             private juce::Timer
{
    using juce::Component::keyPressed;
    using juce::Component::keyStateChanged;

    PluginEditorContent (juce::AudioProcessorEditor* ed,
                         juce::AudioPluginInstance* api,
                         TrackerEngine& eng,
                         int instIdx);

    ~PluginEditorContent() override;

    void resized() override;
    bool keyPressed (const juce::KeyPress& key, juce::Component*) override;
    bool keyStateChanged (bool isKeyDown, juce::Component*) override;
    void timerCallback() override;

private:
    enum { kToolbarHeight = 32 };

    juce::AudioProcessorEditor* vstEditor;
    juce::AudioPluginInstance* pluginInstance;
    TrackerEngine& engine;
    int instrumentIndex;
    int currentOctave = 4;

    juce::TextButton previewKbButton;
    juce::TextButton autoLearnButton;
    juce::Label octaveLabel;

    bool autoLearnEnabled = false;
    int lastDispatchedAutoLearnParam = -1;
    std::vector<float> autoLearnParamSnapshot;
    bool previewKeyboardEnabled = false;

    std::map<int, int> heldNotesByKeyCode;
    bool octaveKeysDown[8] = { false, false, false, false, false, false, false, false };

    void flushAutoLearnNavigation (int parameterIndex);
    void captureAutoLearnSnapshot();
    void pollAutoLearnParameterChanges();
    int getMappedNoteForKeyCode (int keyCode) const;
    void releaseHeldPreviewNotes();
    void setPreviewKeyboardEnabled (bool enabled);
    void updatePollingTimerState();
    void pollOctaveKeys();
    void pollMappedNoteKeys();
    void addKeyHookToComponentTree (juce::Component& component);
    void removeKeyHookFromComponentTree (juce::Component& component);
};

//==============================================================================
// Window wrapper
//==============================================================================
struct PluginInstrumentEditorWindow : public juce::DocumentWindow
{
    PluginInstrumentEditorWindow (const juce::String& name);
    void closeButtonPressed() override;
};
