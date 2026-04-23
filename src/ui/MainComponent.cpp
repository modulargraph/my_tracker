#include <regex>
#include "MainComponent.h"
#include "Pattern.h"
#include "PluginAutomationData.h"
#include "ArrangementComponent.h"
#include "AudioPluginSettingsComponent.h"
#include "Clipboard.h"
#include "FileBrowserComponent.h"
#include "InstrumentPanel.h"
#include "MixerComponent.h"
#include "PatternEditUtils.h"
#include "ProjectSerializer.h"
#include "SampleEditorComponent.h"
#include "SendEffectsComponent.h"
#include "TabBarComponent.h"
#include "ToolbarComponent.h"
#include "TrackerGrid.h"

namespace
{
constexpr int kMinUiScalePercent = 80;
constexpr int kMaxUiScalePercent = 150;
constexpr int kUiScaleStepPercent = 5;

int snapUiScalePercent (int scalePercent)
{
    auto clamped = juce::jlimit (kMinUiScalePercent, kMaxUiScalePercent, scalePercent);
    auto steps = (clamped - kMinUiScalePercent + kUiScaleStepPercent / 2) / kUiScaleStepPercent;
    return kMinUiScalePercent + steps * kUiScaleStepPercent;
}

void sortPluginsByManufacturerAndName (juce::Array<juce::PluginDescription>& plugins)
{
    std::sort (plugins.begin(), plugins.end(),
               [] (const juce::PluginDescription& a, const juce::PluginDescription& b)
               {
                   int cmp = a.manufacturerName.compareIgnoreCase (b.manufacturerName);
                   if (cmp != 0) return cmp < 0;
                   return a.name.compareIgnoreCase (b.name) < 0;
               });
}

juce::PopupMenu buildPluginMenuByManufacturer (const juce::Array<juce::PluginDescription>& plugins)
{
    juce::PopupMenu menu;
    juce::String currentMfr;
    juce::PopupMenu currentSubMenu;

    for (int i = 0; i < plugins.size(); ++i)
    {
        auto& desc = plugins.getReference (i);
        auto mfr = desc.manufacturerName.isEmpty() ? juce::String ("Unknown") : desc.manufacturerName;

        if (mfr != currentMfr)
        {
            if (currentMfr.isNotEmpty())
                menu.addSubMenu (currentMfr, currentSubMenu);
            currentSubMenu = juce::PopupMenu();
            currentMfr = mfr;
        }

        currentSubMenu.addItem (i + 1, desc.name + " (" + desc.pluginFormatName + ")");
    }

    if (currentMfr.isNotEmpty())
        menu.addSubMenu (currentMfr, currentSubMenu);

    return menu;
}

bool remapInsertPluginIdAfterSlotRemoved (juce::String& pluginId, int trackIndex, int removedSlotIndex)
{
    const auto prefix = "insert:" + juce::String (trackIndex) + ":";
    if (! pluginId.startsWith (prefix))
        return false;

    int slotIndex = pluginId.substring (prefix.length()).getIntValue();
    if (slotIndex == removedSlotIndex)
    {
        pluginId.clear();
        return true;
    }

    if (slotIndex > removedSlotIndex)
    {
        pluginId = prefix + juce::String (slotIndex - 1);
        return true;
    }

    return false;
}

constexpr int kMenuGenerateMidi = 30;
constexpr int kMenuTransposeSelectionUpSemitone = 40;
constexpr int kMenuTransposeSelectionDownSemitone = 41;
constexpr int kMenuTransposeSelectionUpOctave = 42;
constexpr int kMenuTransposeSelectionDownOctave = 43;
constexpr int kMenuTransposeTrackUpSemitone = 44;
constexpr int kMenuTransposeTrackDownSemitone = 45;
constexpr int kMenuTransposeTrackUpOctave = 46;
constexpr int kMenuTransposeTrackDownOctave = 47;

std::array<int, 7> getScaleIntervals (int scale)
{
    if (scale == 1)
        return { 0, 2, 3, 5, 7, 8, 10 };

    return { 0, 2, 4, 5, 7, 9, 11 };
}

int floorDiv7 (int value)
{
    return value >= 0 ? value / 7 : (value - 6) / 7;
}

int positiveMod7 (int value)
{
    auto mod = value % 7;
    return mod < 0 ? mod + 7 : mod;
}

int noteFromScaleStep (int rootPitchClass, const std::array<int, 7>& scaleIntervals,
                       int scaleStep, int octave)
{
    auto octaveOffset = floorDiv7 (scaleStep);
    auto degree = positiveMod7 (scaleStep);
    return octave * 12 + rootPitchClass + scaleIntervals[static_cast<size_t> (degree)] + octaveOffset * 12;
}

std::vector<int> getProgressionDegrees (int progression, int bars, juce::Random& random)
{
    switch (progression)
    {
        case 0: return { 0, 4, 5, 3 }; // I-V-vi-IV
        case 1: return { 5, 3, 0, 4 }; // vi-IV-I-V
        case 2: return { 0, 5, 3, 4 }; // I-vi-IV-V
        case 3: return { 1, 4, 0, 0 }; // ii-V-I-I
        case 4: return { 0, 3, 4, 3 }; // I-IV-V-IV
        case 5: return { 0, 0, 3, 4 }; // I-I-IV-V
        case 6: return { 0, 5, 2, 6 }; // i-VI-III-VII
        case 7: return { 0, 3, 5, 4 }; // i-iv-VI-V
        case 8: return { 0, 0, 0, 0, 3, 3, 0, 0, 4, 3, 0, 4 }; // 12-bar blues
        case 9:
        {
            static constexpr int weightedDegrees[] = { 0, 0, 0, 3, 3, 4, 4, 5, 1, 2, 6 };
            std::vector<int> degrees;
            degrees.reserve (static_cast<size_t> (juce::jmax (1, bars)));
            for (int i = 0; i < juce::jmax (1, bars); ++i)
                degrees.push_back (weightedDegrees[random.nextInt (static_cast<int> (sizeof (weightedDegrees) / sizeof (weightedDegrees[0])))]);
            return degrees;
        }
        default: break;
    }

    return { 0, 4, 5, 3 };
}

std::vector<int> getChordScaleSteps (int degree, int chordStyle)
{
    switch (chordStyle)
    {
        case 1: return { degree, degree + 2, degree + 4, degree + 6 };
        case 2: return { degree, degree + 4, degree + 7, degree + 9 };
        case 3: return { degree, degree + 4, degree + 7 };
        default: break;
    }

    return { degree, degree + 2, degree + 4 };
}

std::vector<int> getChordHitOffsets (int chordRhythm, int barRows, int rowsPerBeat)
{
    std::vector<int> offsets;
    const int halfBeat = juce::jmax (1, rowsPerBeat / 2);

    switch (chordRhythm)
    {
        case 1:
            offsets = { 0, barRows / 2 };
            break;
        case 2:
            for (int beat = 0; beat < 4; ++beat)
                offsets.push_back (beat * rowsPerBeat);
            break;
        case 3:
            offsets = { 0, rowsPerBeat + halfBeat, 2 * rowsPerBeat + halfBeat };
            break;
        default:
            offsets = { 0 };
            break;
    }

    offsets.erase (std::remove_if (offsets.begin(), offsets.end(),
                                   [barRows] (int offset) { return offset < 0 || offset >= barRows; }),
                   offsets.end());
    if (offsets.empty())
        offsets.push_back (0);
    return offsets;
}

int randomizedVelocity (juce::Random& random, int baseVelocity, int randomAmount)
{
    const int spread = juce::roundToInt (static_cast<float> (randomAmount) * 0.24f);
    if (spread <= 0)
        return juce::jlimit (1, 127, baseVelocity);

    return juce::jlimit (1, 127, baseVelocity + random.nextInt (spread * 2 + 1) - spread);
}

void randomizeChordVoicing (std::vector<int>& notes, juce::Random& random, int randomAmount)
{
    if (notes.size() < 3 || randomAmount <= 0)
        return;

    const float chance = juce::jlimit (0.0f, 0.75f, static_cast<float> (randomAmount) / 140.0f);
    if (random.nextFloat() >= chance)
        return;

    std::sort (notes.begin(), notes.end());
    const int moves = 1 + random.nextInt (juce::jmin (2, static_cast<int> (notes.size()) - 1));
    for (int i = 0; i < moves; ++i)
    {
        notes.front() += 12;
        std::sort (notes.begin(), notes.end());
    }
}

class MidiGeneratorComponent : public juce::Component
{
public:
    MidiGeneratorComponent (TrackerLookAndFeel& lnf, int trackIndex)
        : lookAndFeel (lnf)
    {
        titleLabel.setText ("Track " + juce::String (trackIndex + 1) + " MIDI generator", juce::dontSendNotification);
        titleLabel.setFont (lookAndFeel.getMonoFont (15.0f));
        titleLabel.setColour (juce::Label::textColourId, lookAndFeel.findColour (TrackerLookAndFeel::textColourId));
        addAndMakeVisible (titleLabel);

        addCombo (keyLabel, keyBox, "Key");
        static const char* keys[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        for (int i = 0; i < 12; ++i)
            keyBox.addItem (keys[i], i + 1);
        keyBox.setSelectedId (1);

        addCombo (scaleLabel, scaleBox, "Scale");
        scaleBox.addItem ("Major", 1);
        scaleBox.addItem ("Natural Minor", 2);
        scaleBox.setSelectedId (1);

        addCombo (progressionLabel, progressionBox, "Progression");
        progressionBox.addItem ("I-V-vi-IV", 1);
        progressionBox.addItem ("vi-IV-I-V", 2);
        progressionBox.addItem ("I-vi-IV-V", 3);
        progressionBox.addItem ("ii-V-I-I", 4);
        progressionBox.addItem ("I-IV-V-IV", 5);
        progressionBox.addItem ("I-I-IV-V", 6);
        progressionBox.addItem ("i-VI-III-VII", 7);
        progressionBox.addItem ("i-iv-VI-V", 8);
        progressionBox.addItem ("12-bar blues", 9);
        progressionBox.addItem ("Random diatonic", 10);
        progressionBox.setSelectedId (1);

        addCombo (outputLabel, outputBox, "Output");
        outputBox.addItem ("Chords", 1);
        outputBox.addItem ("Bass", 2);
        outputBox.addItem ("Chords + Bass", 3);
        outputBox.setSelectedId (1);

        addCombo (chordStyleLabel, chordStyleBox, "Chord voicing");
        chordStyleBox.addItem ("Triads", 1);
        chordStyleBox.addItem ("Sevenths", 2);
        chordStyleBox.addItem ("Open", 3);
        chordStyleBox.addItem ("Power", 4);
        chordStyleBox.setSelectedId (1);

        addCombo (chordRhythmLabel, chordRhythmBox, "Chord rhythm");
        chordRhythmBox.addItem ("Sustained", 1);
        chordRhythmBox.addItem ("Half notes", 2);
        chordRhythmBox.addItem ("Quarter pulses", 3);
        chordRhythmBox.addItem ("Syncopated", 4);
        chordRhythmBox.setSelectedId (1);

        addCombo (bassPatternLabel, bassPatternBox, "Bass pattern");
        bassPatternBox.addItem ("Root", 1);
        bassPatternBox.addItem ("Root + fifth", 2);
        bassPatternBox.addItem ("Octave pulse", 3);
        bassPatternBox.addItem ("Eighth pulse", 4);
        bassPatternBox.addItem ("Walking", 5);
        bassPatternBox.addItem ("Offbeat", 6);
        bassPatternBox.setSelectedId (2);

        addSlider (barsLabel, barsSlider, "Bars", 1.0, 32.0, 1.0, 8.0);
        addSlider (transposeLabel, transposeSlider, "Transpose", -24.0, 24.0, 1.0, 0.0);
        addSlider (randomLabel, randomSlider, "Randomize", 0.0, 100.0, 1.0, 20.0);

        startAtCursorToggle.setButtonText ("Start at cursor row");
        startAtCursorToggle.setColour (juce::ToggleButton::textColourId,
                                       lookAndFeel.findColour (TrackerLookAndFeel::textColourId));
        addAndMakeVisible (startAtCursorToggle);

        generateButton.setButtonText ("Generate");
        generateButton.onClick = [this]
        {
            if (onGenerate)
                onGenerate (getSettings());
            closeDialog();
        };
        addAndMakeVisible (generateButton);

        cancelButton.setButtonText ("Cancel");
        cancelButton.onClick = [this] { closeDialog(); };
        addAndMakeVisible (cancelButton);

        setSize (460, 420);
    }

    std::function<void (const MidiGeneratorSettings&)> onGenerate;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (lookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (18);
        titleLabel.setBounds (area.removeFromTop (24));
        area.removeFromTop (8);

        layoutRow (area, keyLabel, keyBox);
        layoutRow (area, scaleLabel, scaleBox);
        layoutRow (area, progressionLabel, progressionBox);
        layoutRow (area, outputLabel, outputBox);
        layoutRow (area, chordStyleLabel, chordStyleBox);
        layoutRow (area, chordRhythmLabel, chordRhythmBox);
        layoutRow (area, bassPatternLabel, bassPatternBox);
        layoutRow (area, barsLabel, barsSlider);
        layoutRow (area, transposeLabel, transposeSlider);
        layoutRow (area, randomLabel, randomSlider);

        startAtCursorToggle.setBounds (area.removeFromTop (28).removeFromRight (280));
        area.removeFromTop (10);

        auto buttons = area.removeFromBottom (32);
        cancelButton.setBounds (buttons.removeFromRight (92));
        buttons.removeFromRight (8);
        generateButton.setBounds (buttons.removeFromRight (110));
    }

private:
    void addCombo (juce::Label& label, juce::ComboBox& box, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centredRight);
        label.setColour (juce::Label::textColourId,
                         lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.72f));
        addAndMakeVisible (label);
        addAndMakeVisible (box);
    }

    void addSlider (juce::Label& label, juce::Slider& slider, const juce::String& text,
                    double min, double max, double interval, double value)
    {
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centredRight);
        label.setColour (juce::Label::textColourId,
                         lookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.72f));
        addAndMakeVisible (label);

        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 54, 20);
        slider.setRange (min, max, interval);
        slider.setValue (value, juce::dontSendNotification);
        addAndMakeVisible (slider);
    }

    void layoutRow (juce::Rectangle<int>& area, juce::Label& label, juce::Component& control)
    {
        auto row = area.removeFromTop (30);
        label.setBounds (row.removeFromLeft (126));
        row.removeFromLeft (10);
        control.setBounds (row);
        area.removeFromTop (4);
    }

    MidiGeneratorSettings getSettings() const
    {
        MidiGeneratorSettings settings;
        settings.keyRoot = juce::jmax (0, keyBox.getSelectedId() - 1);
        settings.scale = juce::jmax (0, scaleBox.getSelectedId() - 1);
        settings.progression = juce::jmax (0, progressionBox.getSelectedId() - 1);
        settings.outputMode = juce::jmax (0, outputBox.getSelectedId() - 1);
        settings.chordStyle = juce::jmax (0, chordStyleBox.getSelectedId() - 1);
        settings.chordRhythm = juce::jmax (0, chordRhythmBox.getSelectedId() - 1);
        settings.bassPattern = juce::jmax (0, bassPatternBox.getSelectedId() - 1);
        settings.bars = juce::jlimit (1, 32, juce::roundToInt (barsSlider.getValue()));
        settings.transpose = juce::jlimit (-24, 24, juce::roundToInt (transposeSlider.getValue()));
        settings.randomAmount = juce::jlimit (0, 100, juce::roundToInt (randomSlider.getValue()));
        settings.startAtCursor = startAtCursorToggle.getToggleState();
        return settings;
    }

    void closeDialog()
    {
        if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
            dialog->exitModalState (0);
    }

    TrackerLookAndFeel& lookAndFeel;
    juce::Label titleLabel;
    juce::Label keyLabel, scaleLabel, progressionLabel, outputLabel, chordStyleLabel, chordRhythmLabel, bassPatternLabel;
    juce::Label barsLabel, transposeLabel, randomLabel;
    juce::ComboBox keyBox, scaleBox, progressionBox, outputBox, chordStyleBox, chordRhythmBox, bassPatternBox;
    juce::Slider barsSlider, transposeSlider, randomSlider;
    juce::ToggleButton startAtCursorToggle;
    juce::TextButton generateButton, cancelButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiGeneratorComponent)
};
}

MainComponent::MainComponent()
{
    setLookAndFeel (&trackerLookAndFeel);

    // Initialise the engine
    trackerEngine.initialise();
    trackerEngine.setMixerState (&mixerState);

    // Create tab bar
    tabBar = std::make_unique<TabBarComponent> (trackerLookAndFeel);
    addAndMakeVisible (*tabBar);
    tabBar->onTabChanged = [this] (Tab tab) { switchToTab (tab); };

    // Create toolbar
    toolbar = std::make_unique<ToolbarComponent> (trackerLookAndFeel);
    addAndMakeVisible (*toolbar);

    toolbar->onAddPattern = [this]
    {
        patternData.addPattern (patternData.getCurrentPattern().numRows);
        switchToPattern (patternData.getNumPatterns() - 1);
        markDirty();
    };
    toolbar->onDuplicatePattern = [this]
    {
        int idx = patternData.getCurrentPatternIndex();
        patternData.duplicatePattern (idx);
        arrangement.remapAfterPatternInserted (idx + 1);
        switchToPattern (idx + 1);
        if (trackerEngine.isPlaying() && songMode)
            syncArrangementToEdit();
        markDirty();
    };
    toolbar->onRemovePattern = [this]
    {
        int idx = patternData.getCurrentPatternIndex();
        auto& pat = patternData.getCurrentPattern();

        // Only delete when the current pattern is empty.
        // Otherwise this behaves like "previous pattern".
        bool hasData = pat.hasAnyData (trackLayout.getMasterFxLaneCount());

        if (hasData)
        {
            if (idx > 0)
                switchToPattern (idx - 1);
            return;
        }

        if (patternData.getNumPatterns() <= 1)
            return;

        removePatternAndRepairArrangement (idx);
        if (idx > 0)
            switchToPattern (idx - 1);
        else
        {
            switchToPattern (0);
        }
        markDirty();
    };
    toolbar->onNextPattern = [this]
    {
        int idx = patternData.getCurrentPatternIndex();
        if (idx + 1 >= patternData.getNumPatterns())
        {
            // At end — create a new pattern
            patternData.addPattern (patternData.getCurrentPattern().numRows);
            markDirty();
        }
        switchToPattern (idx + 1);
    };
    toolbar->onPrevPattern = [this]
    {
        int idx = patternData.getCurrentPatternIndex();
        if (idx > 0)
        {
            // If on last pattern and it's empty, remove it
            if (idx == patternData.getNumPatterns() - 1)
            {
                auto& pat = patternData.getCurrentPattern();
                bool hasData = pat.hasAnyData (trackLayout.getMasterFxLaneCount());
                if (! hasData)
                {
                    removePatternAndRepairArrangement (idx);
                    markDirty();
                }
            }
            switchToPattern (idx - 1);
        }
    };
    toolbar->onPatternLengthClick = [this] { showPatternLengthEditor(); };

    toolbar->onLengthDrag = [this] (int delta)
    {
        if (patternData.getNumPatterns() == 0) return;
        auto& pat = patternData.getCurrentPattern();
        int newLen = juce::jlimit (1, 256, pat.numRows + delta);
        pat.resize (newLen);
        trackerGrid->setCursorPosition (
            juce::jmin (trackerGrid->getCursorRow(), newLen - 1),
            trackerGrid->getCursorTrack());

        if (trackerEngine.isPlaying())
        {
            if (songMode)
                syncArrangementToEdit();
            else
            {
                trackerEngine.syncPatternToEdit (pat, getReleaseModes());
                trackerEngine.updateLoopRangeForPatternLength (pat.numRows);
            }
        }

        updateToolbar();
        markDirty();
    };

    toolbar->onBpmDrag = [this] (double delta)
    {
        trackerEngine.setBpm (juce::jlimit (20.0, 999.0, trackerEngine.getBpm() + delta));
        updateStatusBar();
        updateToolbar();
        markDirty();
    };

    toolbar->onRpbDrag = [this] (int delta)
    {
        int rpb = juce::jlimit (1, 16, trackerEngine.getRowsPerBeat() + delta);
        trackerEngine.setRowsPerBeat (rpb);
        trackerGrid->setRowsPerBeat (rpb);

        if (trackerEngine.isPlaying())
            resyncPlaybackForCurrentMode();

        updateToolbar();
        markDirty();
    };

    toolbar->onStepDrag = [this] (int delta)
    {
        trackerGrid->setEditStep (juce::jlimit (0, 16, trackerGrid->getEditStep() + delta));
        updateStatusBar();
        updateToolbar();
    };

    toolbar->onOctaveDrag = [this] (int delta)
    {
        int oct = juce::jlimit (0, 9, trackerGrid->getOctave() + delta);
        trackerGrid->setOctave (oct);
        sampleEditor->setOctave (oct);
        updateStatusBar();
        updateToolbar();
    };

    toolbar->onModeToggle = [this]
    {
        toggleSongMode();
    };

    toolbar->onPatternNameDoubleClick = [this]
    {
        showPatternNameEditor();
    };

    toolbar->onToggleArrangement = [this]
    {
        toggleArrangementPanel();
        toolbar->setArrangementVisible (arrangementVisible);
    };

    toolbar->onToggleInstrumentPanel = [this]
    {
        instrumentPanelVisible = ! instrumentPanelVisible;
        toolbar->setInstrumentPanelVisible (instrumentPanelVisible);
        resized();
    };

    toolbar->onToggleAutomation = [this]
    {
        automationPanelVisible = ! automationPanelVisible;
        toolbar->setAutomationPanelVisible (automationPanelVisible);
        if (automationPanelVisible)
            refreshAutomationPanel();
        resized();
    };

    toolbar->onInstrumentDrag = [this] (int delta)
    {
        int inst = juce::jlimit (0, 255, trackerGrid->getCurrentInstrument() + delta);
        trackerGrid->setCurrentInstrument (inst);
        instrumentPanel->setSelectedInstrument (inst);
        updateStatusBar();
        updateToolbar();
    };

    toolbar->onFollowToggle = [this]
    {
        // Cycle: Off → Center → Page → Off
        if (followMode == FollowMode::Off)
            followMode = FollowMode::Center;
        else if (followMode == FollowMode::Center)
            followMode = FollowMode::Page;
        else
            followMode = FollowMode::Off;
        toolbar->setFollowMode (static_cast<int> (followMode));
        markDirty();
    };

    toolbar->onMetronomeToggle = [this]
    {
        bool enabled = ! trackerEngine.isMetronomeEnabled();
        trackerEngine.setMetronomeEnabled (enabled);
        toolbar->setMetronomeEnabled (enabled);
    };

    toolbar->onShowFxReference = [this]
    {
        auto mousePos = juce::Desktop::getInstance().getMousePosition();
        trackerGrid->showFxCommandPopupAt (mousePos);
    };

    toolbar->onShowMidiGenerator = [this]
    {
        showMidiGeneratorDialog();
    };

    previewVolumeLabel.setText ("Preview", juce::dontSendNotification);
    previewVolumeLabel.setJustificationType (juce::Justification::centredRight);
    previewVolumeLabel.setColour (juce::Label::textColourId,
                                  trackerLookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.7f));
    addAndMakeVisible (previewVolumeLabel);

    previewVolumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    previewVolumeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    previewVolumeSlider.setRange (0.0, 1.0, 0.01);
    previewVolumeSlider.setValue (trackerEngine.getPreviewVolume(), juce::dontSendNotification);
    previewVolumeSlider.onValueChange = [this]
    {
        trackerEngine.setPreviewVolume (static_cast<float> (previewVolumeSlider.getValue()));
    };
    addAndMakeVisible (previewVolumeSlider);

    uiScaleLabel.setText ("Scale", juce::dontSendNotification);
    uiScaleLabel.setJustificationType (juce::Justification::centredRight);
    uiScaleLabel.setColour (juce::Label::textColourId,
                            trackerLookAndFeel.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.7f));
    uiScaleLabel.setFont (trackerLookAndFeel.getMonoFont (12.0f));
    addAndMakeVisible (uiScaleLabel);

    uiScaleSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    uiScaleSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 20);
    uiScaleSlider.setRange (kMinUiScalePercent, kMaxUiScalePercent, kUiScaleStepPercent);
    uiScaleSlider.setDoubleClickReturnValue (true, 100.0);
    uiScaleSlider.textFromValueFunction = [] (double value)
    {
        return juce::String (juce::roundToInt (value)) + "%";
    };
    uiScaleSlider.valueFromTextFunction = [] (const juce::String& text)
    {
        auto digits = text.retainCharacters ("0123456789");
        return static_cast<double> (digits.isEmpty() ? 100 : digits.getIntValue());
    };
    uiScaleSlider.onValueChange = [this]
    {
        setUiScalePercent (juce::roundToInt (uiScaleSlider.getValue()), true);
    };
    addAndMakeVisible (uiScaleSlider);

    // Create arrangement panel (hidden by default)
    arrangementComponent = std::make_unique<ArrangementComponent> (arrangement, patternData, trackerLookAndFeel);
    addChildComponent (*arrangementComponent);
    arrangementComponent->onSwitchToPattern = [this] (int patIdx)
    {
        switchToPattern (patIdx);
    };
    arrangementComponent->onAddEntryRequested = [this]
    {
        int patIdx = patternData.getCurrentPatternIndex();
        int pos = (arrangementComponent->getSelectedEntry() >= 0)
                      ? arrangementComponent->getSelectedEntry() + 1
                      : arrangement.getNumEntries();
        arrangement.insertEntry (pos, patIdx);
        arrangementComponent->setSelectedEntry (pos);

        if (trackerEngine.isPlaying() && songMode)
            syncArrangementToEdit();

        markDirty();
    };
    arrangementComponent->onArrangementChanged = [this]
    {
        if (trackerEngine.isPlaying() && songMode)
            syncArrangementToEdit();

        markDirty();
    };

    // Create instrument panel (right side, visible by default)
    instrumentPanel = std::make_unique<InstrumentPanel> (trackerLookAndFeel);
    addAndMakeVisible (*instrumentPanel);
    instrumentPanel->onLoadSampleRequested = [this] (int inst)
    {
        loadSampleForInstrument (inst);
    };
    instrumentPanel->onClearSampleRequested = [this] (int inst)
    {
        clearSampleForInstrument (inst);
    };
    instrumentPanel->onEditSampleRequested = [this] (int inst)
    {
        trackerGrid->setCurrentInstrument (inst);
        instrumentPanel->setSelectedInstrument (inst);
        switchToTab (Tab::InstrumentEdit);
    };
    instrumentPanel->onInstrumentSelected = [this] (int inst)
    {
        trackerGrid->setCurrentInstrument (inst);
        updateStatusBar();
        updateToolbar();
        // Refresh editor if on an edit/type tab
        if (activeTab == Tab::InstrumentEdit || activeTab == Tab::InstrumentType)
            updateSampleEditorForCurrentInstrument();
    };
    instrumentPanel->onSetPluginInstrumentRequested = [this] (int inst)
    {
        // Show plugin picker from scanned instruments list
        auto instruments = trackerEngine.getPluginCatalog().getInstruments();
        if (instruments.isEmpty())
        {
            setTemporaryStatus ("No plugin instruments found. Scan for plugins first.", true, 3000);
            return;
        }

        sortPluginsByManufacturerAndName (instruments);
        auto menu = buildPluginMenuByManufacturer (instruments);

        int cursorTrack = trackerGrid->getCursorTrack();
        if (cursorTrack >= kNumTracks)
            cursorTrack = 0;

        menu.showMenuAsync (juce::PopupMenu::Options(),
                            [this, inst, cursorTrack, instruments] (int result)
                            {
                                if (result > 0 && result <= instruments.size())
                                {
                                    auto& desc = instruments.getReference (result - 1);

                                    // Clear stale automation for this instrument slot (param indices change with new plugin)
                                    auto pluginId = "inst:" + juce::String (inst);
                                    for (int p = 0; p < patternData.getNumPatterns(); ++p)
                                        patternData.getPattern (p).getAutomationData().removeAllLanesForPlugin (pluginId);

                                    trackerEngine.setPluginInstrument (inst, desc, cursorTrack);
                                    invalidateAutomationPluginCache (cursorTrack);
                                    if (trackerEngine.isPlaying())
                                        resyncPlaybackForCurrentMode();
                                    updateInstrumentPanel();
                                    if (automationPanelVisible)
                                        refreshAutomationPanel();
                                    markDirty();
                                    setTemporaryStatus ("Plugin instrument slot "
                                                        + juce::String::formatted ("%02X", inst)
                                                        + " set to " + desc.name
                                                        + " (owner track " + juce::String (cursorTrack + 1) + ")",
                                                        false, 3000);
                                }
                            });
    };
    instrumentPanel->onClearPluginInstrumentRequested = [this] (int inst)
    {
        trackerEngine.clearPluginInstrument (inst);
        invalidateAutomationPluginCache();
        if (trackerEngine.isPlaying())
            resyncPlaybackForCurrentMode();
        updateInstrumentPanel();
        if (automationPanelVisible)
            refreshAutomationPanel();
        markDirty();
    };
    instrumentPanel->onOpenPluginEditorRequested = [this] (int inst)
    {
        trackerEngine.openPluginInstrumentEditor (inst);
    };

    // Create sample editor (always present, shown in edit/type tabs)
    sampleEditor = std::make_unique<SampleEditorComponent> (trackerLookAndFeel);
    addAndMakeVisible (*sampleEditor);

    sampleEditor->onParamsChanged = [this] (int inst, const InstrumentParams& params)
    {
        trackerEngine.getSampler().setParams (inst, params);

        // Apply to all tracks that currently use this instrument
        bool applied = false;
        for (int t = 0; t < kNumTracks; ++t)
        {
            if (trackerEngine.getTrackInstrument (t) == inst)
            {
                auto* track = trackerEngine.getTrack (t);
                if (track != nullptr)
                {
                    trackerEngine.getSampler().applyParams (*track, inst);
                    applied = true;
                }
            }
        }

        // Fallback: apply to the instrument's home track (before first playback sync)
        if (! applied && inst >= 0 && inst < kNumTracks)
        {
            auto* track = trackerEngine.getTrack (inst);
            if (track != nullptr)
                trackerEngine.getSampler().applyParams (*track, inst);
        }
        markDirty();
    };
    sampleEditor->onRealtimeParamsChanged = [this] (int inst, const InstrumentParams& params)
    {
        // Lightweight path: update params map only — InstrumentEffectsPlugin reads
        // from the params map each audio block, so no applyParams() needed
        trackerEngine.getSampler().setParams (inst, params);
        markDirty();
    };
    sampleEditor->onPreviewRequested = [this] (int inst, int note)
    {
        // Preview through dedicated preview track (no auto-stop; key release stops it).
        int previewTrack = trackerGrid->isCursorInMasterLane() ? 0 : trackerGrid->getCursorTrack();
        trackerEngine.previewNote (previewTrack, inst, note, false);
    };
    sampleEditor->onPreviewStopped = [this]()
    {
        trackerEngine.stopPreview();
    };
    sampleEditor->onGetPreviewPosition = [this]() -> float
    {
        return trackerEngine.getPreviewPlaybackPosition();
    };
    sampleEditor->onOpenPluginEditorRequested = [this] (int inst)
    {
        trackerEngine.openPluginInstrumentEditor (inst);
    };

    // Create mixer component (hidden by default)
    mixerComponent = std::make_unique<MixerComponent> (trackerLookAndFeel, mixerState, trackLayout);
    addChildComponent (*mixerComponent);

    mixerComponent->onMuteChanged = [this] (int track, bool muted)
    {
        auto* t = trackerEngine.getTrack (track);
        if (t != nullptr)
        {
            t->setMute (muted);
            updateMuteSoloState();
        }
        markDirty();
    };
    mixerComponent->onSoloChanged = [this] (int track, bool soloed)
    {
        auto* t = trackerEngine.getTrack (track);
        if (t != nullptr)
        {
            t->setSolo (soloed);
            updateMuteSoloState();
        }
        markDirty();
    };
    mixerComponent->onMixStateChanged = [this]
    {
        trackerEngine.refreshMixerPlugins();
        markDirty();
    };

    // Insert plugin callbacks
    mixerComponent->onAddInsertClicked = [this] (int track)
    {
        auto effects = trackerEngine.getPluginCatalog().getEffects();
        if (effects.isEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::InfoIcon,
                "No Plugins", "No effect plugins found. Scan for plugins in Audio Plugin Settings first.");
            return;
        }

        sortPluginsByManufacturerAndName (effects);
        auto menu = buildPluginMenuByManufacturer (effects);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (mixerComponent.get()),
            [this, track, effects] (int result)
            {
                if (result > 0)
                {
                    auto desc = effects[result - 1];
                    if (trackerEngine.addInsertPlugin (track, desc))
                    {
                        invalidateAutomationPluginCache (track);
                        mixerComponent->repaint();
                        if (automationPanelVisible)
                            refreshAutomationPanel();
                        markDirty();
                    }
                }
            });
    };

    mixerComponent->onRemoveInsertClicked = [this] (int track, int slotIndex)
    {
        saveAutomationSelection();
        trackerEngine.removeInsertPlugin (track, slotIndex);

        bool automationChanged = false;
        for (int p = 0; p < patternData.getNumPatterns(); ++p)
        {
            if (patternData.getPattern (p).getAutomationData().remapInsertLanesAfterSlotRemoved (track, slotIndex))
                automationChanged = true;
        }

        for (auto it = automationSelectionPerTrack.begin(); it != automationSelectionPerTrack.end(); )
        {
            auto pluginId = it->second.first;
            if (remapInsertPluginIdAfterSlotRemoved (pluginId, track, slotIndex))
            {
                if (pluginId.isEmpty())
                {
                    it = automationSelectionPerTrack.erase (it);
                    continue;
                }

                it->second.first = pluginId;
            }

            ++it;
        }

        invalidateAutomationPluginCache (track);
        mixerComponent->repaint();
        if (automationPanelVisible)
            refreshAutomationPanel();
        if (automationChanged && trackerEngine.isPlaying())
            resyncPlaybackForCurrentMode();
        markDirty();
    };

    mixerComponent->onInsertBypassToggled = [this] (int track, int slotIndex, bool bypassed)
    {
        trackerEngine.setInsertBypassed (track, slotIndex, bypassed);
        mixerComponent->repaint();
        markDirty();
    };

    mixerComponent->onOpenInsertEditor = [this] (int track, int slotIndex)
    {
        trackerEngine.openPluginEditor (track, slotIndex);
    };

    mixerComponent->onAddMasterInsertClicked = [this]
    {
        auto effects = trackerEngine.getPluginCatalog().getEffects();
        if (effects.isEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::InfoIcon,
                "No Plugins", "No effect plugins found. Scan for plugins in Audio Plugin Settings first.");
            return;
        }

        sortPluginsByManufacturerAndName (effects);
        auto menu = buildPluginMenuByManufacturer (effects);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (mixerComponent.get()),
            [this, effects] (int result)
            {
                if (result > 0)
                {
                    auto desc = effects[result - 1];
                    if (trackerEngine.addMasterInsertPlugin (desc))
                    {
                        mixerComponent->repaint();
                        markDirty();
                    }
                }
            });
    };

    mixerComponent->onRemoveMasterInsertClicked = [this] (int slotIndex)
    {
        trackerEngine.removeMasterInsertPlugin (slotIndex);
        mixerComponent->repaint();
        markDirty();
    };

    mixerComponent->onMasterInsertBypassToggled = [this] (int slotIndex, bool bypassed)
    {
        trackerEngine.setMasterInsertBypassed (slotIndex, bypassed);
        mixerComponent->repaint();
        markDirty();
    };

    mixerComponent->onOpenMasterInsertEditor = [this] (int slotIndex)
    {
        trackerEngine.openMasterPluginEditor (slotIndex);
    };

    // Callback from engine when insert state changes (e.g. after addInsertPlugin modifies the state model)
    trackerEngine.onInsertStateChanged = [this]
    {
        invalidateAutomationPluginCache();
        mixerComponent->repaint();
    };

    // Wire peak level metering from engine to mixer UI
    mixerComponent->setPeakLevelCallback ([this] (int track) -> float
    {
        return trackerEngine.getTrackPeakLevel (track);
    });
    mixerComponent->startMetering();

    // Create file browser (hidden by default)
    fileBrowser = std::make_unique<SampleBrowserComponent> (trackerLookAndFeel);
    addChildComponent (*fileBrowser);

    // Restore last browser directory from global prefs
    {
        auto savedDir = ProjectSerializer::loadGlobalBrowserDir();
        if (savedDir.isNotEmpty())
        {
            juce::File dir (savedDir);
            if (dir.isDirectory())
                fileBrowser->setCurrentDirectory (dir);
        }
    }

    fileBrowser->onDirectoryChanged = [] (const juce::File& dir)
    {
        ProjectSerializer::saveGlobalBrowserDir (dir.getFullPathName());
    };
    fileBrowser->onLoadSample = [this] (int instrument, const juce::File& file)
    {
        auto error = trackerEngine.loadSampleForInstrument (instrument, file);
        if (error.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Load Error", error);
        else
        {
            if (trackerEngine.isPlaying())
            {
                if (songMode)
                    syncArrangementToEdit();
                else
                    trackerEngine.refreshTracksForInstrument (instrument, patternData.getCurrentPattern());
            }

            // Auto-select the loaded instrument in the tracker and instrument panel
            trackerGrid->setCurrentInstrument (instrument);
            instrumentPanel->setSelectedInstrument (instrument);

            trackerGrid->repaint();
            updateToolbar();
            updateStatusBar();
            updateInstrumentPanel();
            fileBrowser->updateInstrumentSlots (trackerEngine.getSampler().getLoadedSamples());
            fileBrowser->advanceToNextEmptySlot();
            markDirty();
        }
    };
    fileBrowser->onPreviewFile = [this] (const juce::File& file)
    {
        trackerEngine.previewAudioFile (file);
    };
    fileBrowser->onPreviewInstrument = [this] (int instrumentIndex)
    {
        trackerEngine.previewInstrument (instrumentIndex);
    };
    fileBrowser->onStopPreview = [this]()
    {
        trackerEngine.stopPreview();
    };

    // Create send effects component
    sendEffectsComponent = std::make_unique<SendEffectsComponent> (trackerLookAndFeel);
    addChildComponent (*sendEffectsComponent);

    sendEffectsComponent->setDelayParams (trackerEngine.getDelayParams());
    sendEffectsComponent->setReverbParams (trackerEngine.getReverbParams());

    sendEffectsComponent->onParamsChanged = [this] (const DelayParams& dp, const ReverbParams& rp)
    {
        trackerEngine.setDelayParams (dp);
        trackerEngine.setReverbParams (rp);
        markDirty();
    };

    // Create automation panel (bottom panel in tracker tab)
    automationPanel = std::make_unique<PluginAutomationComponent> (trackerLookAndFeel);
    addChildComponent (*automationPanel);
    automationPanel->onAutomationChanged = [this]
    {
        if (trackerEngine.isPlaying())
        {
            int playRow = -1;
            int playPatternIndex = -1;

            if (songMode && arrangement.getNumEntries() > 0)
            {
                auto info = getArrangementPlaybackPosition (trackerEngine.getPlaybackBeatPosition());
                playRow = info.rowInPattern;
                playPatternIndex = info.patternIndex;
            }
            else
            {
                playRow = trackerEngine.getPlaybackRow (patternData.getCurrentPattern().numRows);
                playPatternIndex = patternData.getCurrentPatternIndex();
            }

            applyAutomationAtPlaybackPosition (playPatternIndex, playRow);
        }
        markDirty();
    };
    automationPanel->onPluginSelected = [] (const juce::String& /*pluginId*/)
    {
        // Selection should not trigger plugin-list repopulation.
    };
    automationPanel->onParameterSelected = [] (const juce::String& pluginId, int paramIndex)
    {
        // Update baseline from current parameter value
        juce::ignoreUnused (pluginId, paramIndex);
        // Baseline is set when parameters are populated
    };
    automationPanel->onPanelHeightChanged = [this] (int /*newHeight*/)
    {
        resized();
    };
    automationPanel->onGetCurrentParameterValue = [this]() -> float
    {
        auto pluginId = automationPanel->getSelectedPluginId();
        int paramIdx = automationPanel->getSelectedParameterIndex();
        if (pluginId.isEmpty() || paramIdx < 0)
            return 0.5f;

        if (auto* audioPlugin = trackerEngine.resolvePluginInstance (pluginId))
        {
            // tryEnter to avoid deadlocking with the audio thread.
            auto& lock = audioPlugin->getCallbackLock();
            if (lock.tryEnter())
            {
                auto& params = audioPlugin->getParameters();
                float val = 0.5f;
                if (paramIdx >= 0 && paramIdx < params.size())
                    val = params[paramIdx]->getValue();
                lock.exit();
                return val;
            }
        }
        return 0.5f;
    };

    // Create the grid
    trackerGrid = std::make_unique<TrackerGrid> (patternData, trackerLookAndFeel, trackLayout);
    trackerGrid->setVelocityLanesVisible (ProjectSerializer::loadGlobalVelocityLanesVisible());
    trackerGrid->setRowsPerBeat (trackerEngine.getRowsPerBeat());
    trackerGrid->setUndoManager (&undoManager);
    addAndMakeVisible (*trackerGrid);

    // Note entry validation callback (ownership/track mode check)
    trackerGrid->onValidateNoteEntry = [this] (int instrumentIndex, int trackIndex) -> juce::String
    {
        auto error = trackerEngine.validateNoteEntry (instrumentIndex, trackIndex);
        if (error.isNotEmpty())
            setTemporaryStatus (error, true, 3000);
        return error;
    };

    // Note preview callback
    trackerGrid->onNoteEntered = [this] (int note, int instrument)
    {
        int previewTrack = trackerGrid->isCursorInMasterLane() ? 0 : trackerGrid->getCursorTrack();
        trackerEngine.previewNote (previewTrack, instrument, note);
        markDirty();
    };

    // Cursor moved callback
    trackerGrid->onCursorMoved = [this]
    {
        // Sync instrument from cell under cursor (standard tracker behaviour)
        auto& pat = patternData.getCurrentPattern();
        int row = trackerGrid->getCursorRow();
        int track = trackerGrid->getCursorTrack();
        if (! trackerGrid->isCursorInMasterLane() && row >= 0 && row < pat.numRows
            && track >= 0 && track < kNumTracks)
        {
            auto cell = pat.getCell (row, track);
            auto slot = cell.getNoteLane (trackerGrid->getCursorNoteLane());
            if (slot.instrument >= 0)
                trackerGrid->setCurrentInstrument (slot.instrument);
        }

        updateStatusBar();
        updateToolbar();
        instrumentPanel->setSelectedInstrument (trackerGrid->getCurrentInstrument());
        if (automationPanelVisible)
            refreshAutomationPanel (false);
    };

    // Pattern data changed — re-sync during playback
    trackerGrid->onPatternDataChanged = [this]
    {
        if (trackerEngine.isPlaying())
            resyncPlaybackForCurrentMode();
        markDirty();
        commandManager.commandStatusChanged();
    };

    // Track header right-click
    trackerGrid->onTrackHeaderRightClick = [this] (int track, juce::Point<int> screenPos)
    {
        showTrackHeaderMenu (track, screenPos);
    };

    // Grid right-click (context menu on cells)
    trackerGrid->onGridRightClick = [this] (int track, juce::Point<int> screenPos)
    {
        showTrackHeaderMenu (track, screenPos);
    };

    // Double-click on track header to rename
    trackerGrid->onTrackHeaderDoubleClick = [this] (int track, juce::Point<int> /*screenPos*/)
    {
        showRenameTrackDialog (track);
    };

    // Header drag-drop reorder complete
    trackerGrid->onTrackHeaderDragged = [this] (int, int)
    {
        markDirty();
    };

    // File drop on track
    trackerGrid->onFileDroppedOnTrack = [this] (int track, const juce::File& file)
    {
        int inst = resolveInstrumentForTrackDrop (track);
        trackerGrid->setCurrentInstrument (inst);
        instrumentPanel->setSelectedInstrument (inst);

        auto error = trackerEngine.loadSampleForInstrument (inst, file);
        if (error.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Load Error", error);
        else
        {
            if (trackerEngine.isPlaying())
            {
                if (songMode)
                    syncArrangementToEdit();
                else
                    trackerEngine.refreshTracksForInstrument (inst, patternData.getCurrentPattern());
            }

            trackerGrid->repaint();
            updateToolbar();
            updateInstrumentPanel();
            markDirty();
        }
    };

    trackerGrid->onNoteModeToggled = [this] (int /*track*/)
    {
        markDirty();
        if (trackerEngine.isPlaying())
            resyncPlaybackForCurrentMode();
    };

    // Transport change callback
    trackerEngine.onTransportChanged = [this]
    {
        updateStatusBar();
        updateToolbar();
    };

    // Status message callback (for ownership violations etc.)
    trackerEngine.onStatusMessage = [this] (const juce::String& message, bool isError, int timeoutMs)
    {
        setTemporaryStatus (message, isError, timeoutMs);
    };

    trackerEngine.onNavigateToAutomation = [this] (const juce::String& pluginId, int paramIndex)
    {
        navigateToAutomationParam (pluginId, paramIndex);
    };

    trackerEngine.onPluginInstrumentCleared = [this] (const juce::String& pluginId)
    {
        // Remove automation lanes for this plugin from all patterns
        for (int p = 0; p < patternData.getNumPatterns(); ++p)
            patternData.getPattern (p).getAutomationData().removeAllLanesForPlugin (pluginId);

        // Refresh automation panel if visible
        if (automationPanelVisible)
            refreshAutomationPanel();
    };

    // Status bar
    addAndMakeVisible (statusLabel);
    statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xffcccccc));
    statusLabel.setFont (trackerLookAndFeel.getMonoFont (12.0f));

    addAndMakeVisible (octaveLabel);
    octaveLabel.setColour (juce::Label::textColourId, juce::Colour (0xffcccccc));
    octaveLabel.setFont (trackerLookAndFeel.getMonoFont (12.0f));

    addAndMakeVisible (bpmLabel);
    bpmLabel.setColour (juce::Label::textColourId, juce::Colour (0xffcccccc));
    bpmLabel.setFont (trackerLookAndFeel.getMonoFont (12.0f));

    setUiScalePercent (ProjectSerializer::loadGlobalUiScalePercent(), false);
    updateStatusBar();
    updateToolbar();

    // Set up application command manager for Cmd shortcuts (macOS needs this)
    commandManager.registerAllCommandsForTarget (this);
    commandManager.setFirstCommandTarget (this);
    addKeyListener (commandManager.getKeyMappings());

    // Register as mac menu bar so Cmd+O goes through the native menu system
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (this);
   #endif

    // Playback cursor update timer
    startTimerHz (30);

    // Keep global navigation shortcuts available from every focusable content area.
    trackerGrid->addKeyListener (this);
    trackerGrid->addKeyListener (commandManager.getKeyMappings());
    sampleEditor->addKeyListener (this);
    sampleEditor->addKeyListener (commandManager.getKeyMappings());
    fileBrowser->addKeyListener (this);
    fileBrowser->addKeyListener (commandManager.getKeyMappings());
    mixerComponent->addKeyListener (this);
    mixerComponent->addKeyListener (commandManager.getKeyMappings());
    sendEffectsComponent->addKeyListener (this);
    sendEffectsComponent->addKeyListener (commandManager.getKeyMappings());
    instrumentPanel->addKeyListener (this);
    instrumentPanel->addKeyListener (commandManager.getKeyMappings());
    automationPanel->addKeyListener (this);
    automationPanel->addKeyListener (commandManager.getKeyMappings());
    arrangementComponent->addKeyListener (this);
    arrangementComponent->addKeyListener (commandManager.getKeyMappings());

    setSize (1280, 720);
    setWantsKeyboardFocus (true);
    trackerGrid->grabKeyboardFocus();
}

MainComponent::~MainComponent()
{
    // Prevent any late engine callbacks from touching a partially-destroyed UI.
    trackerEngine.onTransportChanged = nullptr;
    trackerEngine.onStatusMessage = nullptr;
    trackerEngine.onNavigateToAutomation = nullptr;
    trackerEngine.onPluginInstrumentCleared = nullptr;
    trackerEngine.onInsertStateChanged = nullptr;

   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
   #endif
    arrangementComponent->removeKeyListener (commandManager.getKeyMappings());
    arrangementComponent->removeKeyListener (this);
    automationPanel->removeKeyListener (commandManager.getKeyMappings());
    automationPanel->removeKeyListener (this);
    instrumentPanel->removeKeyListener (commandManager.getKeyMappings());
    instrumentPanel->removeKeyListener (this);
    sendEffectsComponent->removeKeyListener (commandManager.getKeyMappings());
    mixerComponent->removeKeyListener (this);
    mixerComponent->removeKeyListener (commandManager.getKeyMappings());
    sendEffectsComponent->removeKeyListener (this);
    fileBrowser->removeKeyListener (commandManager.getKeyMappings());
    fileBrowser->removeKeyListener (this);
    sampleEditor->removeKeyListener (commandManager.getKeyMappings());
    sampleEditor->removeKeyListener (this);
    trackerGrid->removeKeyListener (commandManager.getKeyMappings());
    trackerGrid->removeKeyListener (this);
    setLookAndFeel (nullptr);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (trackerLookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId));
}

void MainComponent::resized()
{
    auto r = getLocalBounds();

    // Tab bar at top
    tabBar->setBounds (r.removeFromTop (TabBarComponent::kTabBarHeight));

    // Toolbar below tab bar
    toolbar->setBounds (r.removeFromTop (ToolbarComponent::kToolbarHeight));
    auto toolbarBounds = toolbar->getBounds();
    constexpr int kPreviewSliderWidth = 96;
    constexpr int kPreviewLabelWidth = 56;
    int previewY = toolbarBounds.getY() + 8;
    int sliderRight = toolbarBounds.getRight() - 44; // keep clear of INS toggle
    previewVolumeSlider.setBounds (sliderRight - kPreviewSliderWidth, previewY, kPreviewSliderWidth, 20);
    previewVolumeLabel.setBounds (previewVolumeSlider.getX() - kPreviewLabelWidth - 4, previewY, kPreviewLabelWidth, 20);

    // Status bar at bottom
    auto statusBar = r.removeFromBottom (24);
    statusLabel.setBounds (statusBar.removeFromLeft (statusBar.getWidth() / 2));

    auto rightStatus = statusBar;
    constexpr int kScaleStatusWidth = 180;
    constexpr int kScaleStatusGap = 8;
    constexpr int kScaleLabelWidth = 44;
    auto scaleStatus = rightStatus.removeFromRight (juce::jmin (kScaleStatusWidth, rightStatus.getWidth()));
    scaleStatus.removeFromLeft (juce::jmin (kScaleStatusGap, scaleStatus.getWidth()));
    uiScaleLabel.setBounds (scaleStatus.removeFromLeft (juce::jmin (kScaleLabelWidth, scaleStatus.getWidth())));
    uiScaleSlider.setBounds (scaleStatus.reduced (0, 2));

    octaveLabel.setBounds (rightStatus.removeFromLeft (rightStatus.getWidth() / 2));
    bpmLabel.setBounds (rightStatus);

    // Hide everything first
    arrangementComponent->setVisible (false);
    instrumentPanel->setVisible (false);
    trackerGrid->setVisible (false);
    sampleEditor->setVisible (false);
    fileBrowser->setVisible (false);
    automationPanel->setVisible (false);
    mixerComponent->setVisible (false);
    sendEffectsComponent->setVisible (false);

    switch (activeTab)
    {
        case Tab::Tracker:
        {
            // Arrangement panel (left side)
            if (arrangementVisible)
            {
                arrangementComponent->setBounds (r.removeFromLeft (ArrangementComponent::kPanelWidth));
                arrangementComponent->setVisible (true);
            }

            // Instrument panel (right side)
            if (instrumentPanelVisible)
            {
                instrumentPanel->setBounds (r.removeFromRight (InstrumentPanel::kPanelWidth));
                instrumentPanel->setVisible (true);
            }

            // Automation panel (bottom, above status bar)
            if (automationPanelVisible)
            {
                automationPanel->setBounds (r.removeFromBottom (automationPanel->getPanelHeight()));
                automationPanel->setVisible (true);
            }

            // Grid fills the rest
            trackerGrid->setBounds (r);
            trackerGrid->setVisible (true);
            break;
        }
        case Tab::InstrumentEdit:
        {
            // Instrument panel (right side, optional)
            if (instrumentPanelVisible)
            {
                instrumentPanel->setBounds (r.removeFromRight (InstrumentPanel::kPanelWidth));
                instrumentPanel->setVisible (true);
            }

            sampleEditor->setDisplayMode (SampleEditorComponent::DisplayMode::InstrumentEdit);
            sampleEditor->setBounds (r);
            sampleEditor->setVisible (true);
            break;
        }
        case Tab::InstrumentType:
        {
            // Instrument panel (right side, optional)
            if (instrumentPanelVisible)
            {
                instrumentPanel->setBounds (r.removeFromRight (InstrumentPanel::kPanelWidth));
                instrumentPanel->setVisible (true);
            }

            sampleEditor->setDisplayMode (SampleEditorComponent::DisplayMode::InstrumentType);
            sampleEditor->setBounds (r);
            sampleEditor->setVisible (true);
            break;
        }
        case Tab::Mixer:
        {
            mixerComponent->setBounds (r);
            mixerComponent->setVisible (true);
            break;
        }
        case Tab::Effects:
        {
            sendEffectsComponent->setBounds (r);
            sendEffectsComponent->setVisible (true);
            break;
        }
        case Tab::Browser:
        {
            fileBrowser->setBounds (r);
            fileBrowser->setVisible (true);
            break;
        }
    }
}

void MainComponent::setUiScalePercent (int scalePercent, bool persist)
{
    uiScalePercent = snapUiScalePercent (scalePercent);

    juce::Desktop::getInstance().setGlobalScaleFactor (static_cast<float> (uiScalePercent) / 100.0f);
    uiScaleSlider.setValue (uiScalePercent, juce::dontSendNotification);
    updateUiScaleLabel();

    if (persist)
        ProjectSerializer::saveGlobalUiScalePercent (uiScalePercent);

    resized();
    repaint();
}

void MainComponent::setVelocityLanesVisible (bool visible, bool persist)
{
    trackerGrid->setVelocityLanesVisible (visible);

    if (persist)
        ProjectSerializer::saveGlobalVelocityLanesVisible (visible);

    resized();
    updateStatusBar();
    commandManager.commandStatusChanged();
}

void MainComponent::toggleVelocityLanes()
{
    setVelocityLanesVisible (! trackerGrid->areVelocityLanesVisible(), true);
}

void MainComponent::updateUiScaleLabel()
{
    uiScaleLabel.setText ("Scale", juce::dontSendNotification);
    uiScaleSlider.setTooltip ("Interface scale: " + juce::String (uiScalePercent) + "%");
}

bool MainComponent::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    auto keyCode = key.getKeyCode();
    bool cmd = key.getModifiers().isCommandDown();
    bool shift = key.getModifiers().isShiftDown();
    auto textChar = key.getTextCharacter();
    bool alt = key.getModifiers().isAltDown();

    // Option+Left/Right: cycle through top-level tabs
    if (alt && ! cmd && keyCode == juce::KeyPress::rightKey)
    {
        cycleTab (1);
        return true;
    }
    if (alt && ! cmd && keyCode == juce::KeyPress::leftKey)
    {
        cycleTab (-1);
        return true;
    }

    // F1-F6: switch tabs — but on the Tracker tab with no modifiers, let F-keys
    // pass through to TrackerGrid (which uses F1-F8 for octave setting)
    bool onTrackerTab = (tabBar != nullptr && tabBar->getActiveTab() == Tab::Tracker);
    if (!onTrackerTab || shift || cmd || alt)
    {
        if (keyCode == juce::KeyPress::F1Key) { switchToTab (Tab::Tracker); return true; }
        if (keyCode == juce::KeyPress::F2Key) { switchToTab (Tab::InstrumentEdit); return true; }
        if (keyCode == juce::KeyPress::F3Key) { switchToTab (Tab::InstrumentType); return true; }
        if (keyCode == juce::KeyPress::F4Key) { switchToTab (Tab::Mixer); return true; }
        if (keyCode == juce::KeyPress::F5Key) { switchToTab (Tab::Effects); return true; }
        if (keyCode == juce::KeyPress::F6Key) { switchToTab (Tab::Browser); return true; }
    }

    // Escape in non-Tracker tabs: return to Tracker
    if (keyCode == juce::KeyPress::escapeKey && activeTab != Tab::Tracker)
    {
        switchToTab (Tab::Tracker);
        return true;
    }

    // When on non-Tracker tabs, only handle global shortcuts (Space, Cmd+S, etc.)
    if (activeTab != Tab::Tracker)
    {
        // Space: toggle play/stop (global) -- but not when sample editor has focus (it uses Space for preview)
        if (keyCode == juce::KeyPress::spaceKey
            && activeTab != Tab::InstrumentEdit && activeTab != Tab::InstrumentType)
        {
            if (! trackerEngine.isPlaying())
            {
                if (songMode)
                    syncArrangementToEdit();
                else
                    trackerEngine.syncPatternToEdit (patternData.getCurrentPattern(), getReleaseModes());
            }
            trackerEngine.togglePlayStop();
            updateStatusBar();
            updateToolbar();
            return true;
        }
        // Let Cmd shortcuts fall through to ApplicationCommandTarget
        return false;
    }

    // Space: toggle play/stop
    if (keyCode == juce::KeyPress::spaceKey)
    {
        if (! trackerEngine.isPlaying())
        {
            if (songMode)
                syncArrangementToEdit();
            else
                trackerEngine.syncPatternToEdit (patternData.getCurrentPattern(), getReleaseModes());
        }

        trackerEngine.togglePlayStop();
        updateStatusBar();
        updateToolbar();
        return true;
    }

    // Cmd+Shift+Right/Left: next/prev pattern
    if (cmd && shift && keyCode == juce::KeyPress::rightKey)
    {
        if (alt)
        {
            // Cmd+Shift+Alt+Right: add new pattern and switch to it
            patternData.addPattern (patternData.getCurrentPattern().numRows);
            switchToPattern (patternData.getNumPatterns() - 1);
        }
        else
        {
            switchToPattern (patternData.getCurrentPatternIndex() + 1);
        }
        return true;
    }
    if (cmd && shift && keyCode == juce::KeyPress::leftKey)
    {
        switchToPattern (patternData.getCurrentPatternIndex() - 1);
        return true;
    }

    // Cmd+M: toggle mute
    if (cmd && ! shift && textChar == 'm')
    {
        int track = trackerGrid->getCursorTrack();
        if (track >= kNumTracks)
            return true;
        auto* t = trackerEngine.getTrack (track);
        if (t != nullptr)
        {
            t->setMute (! t->isMuted (false));
            updateMuteSoloState();
            markDirty();
        }
        return true;
    }

    // Cmd+Shift+M: toggle solo
    if (cmd && shift && textChar == 'M')
    {
        int track = trackerGrid->getCursorTrack();
        if (track >= kNumTracks)
            return true;
        auto* t = trackerEngine.getTrack (track);
        if (t != nullptr)
        {
            t->setSolo (! t->isSolo (false));
            updateMuteSoloState();
            markDirty();
        }
        return true;
    }

    // Cmd+Up/Down: change instrument
    if (cmd && ! shift && keyCode == juce::KeyPress::upKey)
    {
        int inst = juce::jlimit (0, 255, trackerGrid->getCurrentInstrument() - 1);
        trackerGrid->setCurrentInstrument (inst);
        updateStatusBar();
        updateToolbar();
        instrumentPanel->setSelectedInstrument (inst);
        return true;
    }
    if (cmd && ! shift && keyCode == juce::KeyPress::downKey)
    {
        int inst = juce::jlimit (0, 255, trackerGrid->getCurrentInstrument() + 1);
        trackerGrid->setCurrentInstrument (inst);
        updateStatusBar();
        updateToolbar();
        instrumentPanel->setSelectedInstrument (inst);
        return true;
    }

    // Cmd+Shift+Up/Down: change keyboard octave
    if (cmd && shift && keyCode == juce::KeyPress::upKey)
    {
        int oct = juce::jmin (9, trackerGrid->getOctave() + 1);
        trackerGrid->setOctave (oct);
        sampleEditor->setOctave (oct);
        updateStatusBar();
        updateToolbar();
        return true;
    }
    if (cmd && shift && keyCode == juce::KeyPress::downKey)
    {
        int oct = juce::jmax (0, trackerGrid->getOctave() - 1);
        trackerGrid->setOctave (oct);
        sampleEditor->setOctave (oct);
        updateStatusBar();
        updateToolbar();
        return true;
    }

    // Cmd+1 through Cmd+8: set octave 0-7 (MacBook-friendly alternative to F1-F8)
    if (cmd && ! shift && (keyCode >= '1' && keyCode <= '8'))
    {
        trackerGrid->setOctave (keyCode - '1');
        sampleEditor->setOctave (keyCode - '1');
        updateStatusBar();
        updateToolbar();
        return true;
    }

    // Cmd+[ / Cmd+]: decrease/increase BPM (MacBook-friendly alternative to F9/F10)
    // NOTE: use keyCode, not textChar — macOS zeroes textChar when Cmd is held for punctuation keys
    if (cmd && ! shift && keyCode == '[')
    {
        trackerEngine.setBpm (juce::jlimit (20.0, 999.0, trackerEngine.getBpm() - 1.0));
        updateStatusBar();
        updateToolbar();
        markDirty();
        return true;
    }
    if (cmd && ! shift && keyCode == ']')
    {
        trackerEngine.setBpm (juce::jlimit (20.0, 999.0, trackerEngine.getBpm() + 1.0));
        updateStatusBar();
        updateToolbar();
        markDirty();
        return true;
    }

    // Cmd+- / Cmd+=: decrease/increase edit step (MacBook-friendly alternative to F11/F12)
    if (cmd && ! shift && keyCode == '-')
    {
        trackerGrid->setEditStep (juce::jmax (0, trackerGrid->getEditStep() - 1));
        updateStatusBar();
        updateToolbar();
        return true;
    }
    if (cmd && ! shift && keyCode == '=')
    {
        trackerGrid->setEditStep (juce::jmin (16, trackerGrid->getEditStep() + 1));
        updateStatusBar();
        updateToolbar();
        return true;
    }

    // Cmd+Shift+B: toggle automation panel
    if (cmd && shift && textChar == 'B')
    {
        automationPanelVisible = ! automationPanelVisible;
        toolbar->setAutomationPanelVisible (automationPanelVisible);
        if (automationPanelVisible)
            refreshAutomationPanel();
        resized();
        return true;
    }

    // Shift+F-key alternatives (plain F1-F8 are reserved for tracker octave entry)
    if (shift && keyCode == juce::KeyPress::F7Key)  { toggleArrangementPanel(); return true; }
    if (shift && keyCode == juce::KeyPress::F8Key)  { toggleSongMode(); return true; }

    if (keyCode == juce::KeyPress::F9Key)
    {
        trackerEngine.setBpm (juce::jlimit (20.0, 999.0, trackerEngine.getBpm() - 1.0));
        updateStatusBar();
        updateToolbar();
        markDirty();
        return true;
    }
    if (keyCode == juce::KeyPress::F10Key)
    {
        trackerEngine.setBpm (juce::jlimit (20.0, 999.0, trackerEngine.getBpm() + 1.0));
        updateStatusBar();
        updateToolbar();
        markDirty();
        return true;
    }
    if (keyCode == juce::KeyPress::F11Key)
    {
        trackerGrid->setEditStep (juce::jmax (0, trackerGrid->getEditStep() - 1));
        updateStatusBar(); updateToolbar(); return true;
    }
    if (keyCode == juce::KeyPress::F12Key)
    {
        trackerGrid->setEditStep (juce::jmin (16, trackerGrid->getEditStep() + 1));
        updateStatusBar(); updateToolbar(); return true;
    }

    return false;
}

//==============================================================================
// ApplicationCommandTarget
//==============================================================================

void MainComponent::getAllCommands (juce::Array<juce::CommandID>& commands)
{
    commands.add (loadSample);
    commands.add (nextPattern);
    commands.add (prevPattern);
    commands.add (addPattern);
    commands.add (muteTrack);
    commands.add (soloTrack);
    commands.add (cmdCopy);
    commands.add (cmdPaste);
    commands.add (cmdCut);
    commands.add (cmdUndo);
    commands.add (cmdRedo);
    commands.add (cmdNewProject);
    commands.add (cmdOpen);
    commands.add (cmdSave);
    commands.add (cmdSaveAs);
    commands.add (cmdShowHelp);
    commands.add (cmdToggleArrangement);
    commands.add (cmdToggleSongMode);
    commands.add (cmdToggleInstrumentPanel);
    commands.add (cmdToggleMetronome);
    commands.add (cmdToggleVelocityLanes);
    commands.add (cmdAudioPluginSettings);
}

void MainComponent::getCommandInfo (juce::CommandID commandID, juce::ApplicationCommandInfo& result)
{
    switch (commandID)
    {
        case loadSample:
            result.setInfo ("Load Sample", "Load a sample for the current track", "File", 0);
            result.addDefaultKeypress ('O', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            break;
        case nextPattern:
            result.setInfo ("Next Pattern", "Switch to next pattern", "Pattern", 0);
            break;
        case prevPattern:
            result.setInfo ("Previous Pattern", "Switch to previous pattern", "Pattern", 0);
            break;
        case addPattern:
            result.setInfo ("Add Pattern", "Add a new pattern", "Pattern", 0);
            break;
        case muteTrack:
            result.setInfo ("Mute Track", "Toggle mute on current track", "Track", 0);
            break;
        case soloTrack:
            result.setInfo ("Solo Track", "Toggle solo on current track", "Track", 0);
            break;
        case cmdCopy:
            result.setInfo ("Copy", "Copy selection", "Edit", 0);
            result.addDefaultKeypress ('C', juce::ModifierKeys::commandModifier);
            break;
        case cmdPaste:
            result.setInfo ("Paste", "Paste at cursor", "Edit", 0);
            result.addDefaultKeypress ('V', juce::ModifierKeys::commandModifier);
            break;
        case cmdCut:
            result.setInfo ("Cut", "Cut selection", "Edit", 0);
            result.addDefaultKeypress ('X', juce::ModifierKeys::commandModifier);
            break;
        case cmdUndo:
            result.setInfo ("Undo", "Undo last action", "Edit", 0);
            result.addDefaultKeypress ('Z', juce::ModifierKeys::commandModifier);
            result.setActive (undoManager.canUndo());
            break;
        case cmdRedo:
            result.setInfo ("Redo", "Redo last undone action", "Edit", 0);
            result.addDefaultKeypress ('Z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            result.addDefaultKeypress ('Y', juce::ModifierKeys::commandModifier);
            result.setActive (undoManager.canRedo());
            break;
        case cmdNewProject:
            result.setInfo ("New Project", "Create a new project", "File", 0);
            result.addDefaultKeypress ('N', juce::ModifierKeys::commandModifier);
            break;
        case cmdOpen:
            result.setInfo ("Open Project...", "Open a project file", "File", 0);
            result.addDefaultKeypress ('O', juce::ModifierKeys::commandModifier);
            break;
        case cmdSave:
            result.setInfo ("Save", "Save current project", "File", 0);
            result.addDefaultKeypress ('S', juce::ModifierKeys::commandModifier);
            result.setActive (true);
            break;
        case cmdSaveAs:
            result.setInfo ("Save As...", "Save project to a new file", "File", 0);
            result.addDefaultKeypress ('S', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            result.setActive (true);
            break;
        case cmdShowHelp:
            result.setInfo ("Keyboard Shortcuts", "Show all keyboard shortcuts", "Help", 0);
            result.addDefaultKeypress ('/', juce::ModifierKeys::commandModifier);
            break;
        case cmdToggleArrangement:
            result.setInfo ("Toggle Arrangement", "Show/hide arrangement panel", "View", 0);
            result.addDefaultKeypress ('A', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            break;
        case cmdToggleSongMode:
            result.setInfo ("Toggle Song Mode", "Switch between PAT and SONG playback", "View", 0);
            result.addDefaultKeypress ('P', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            break;
        case cmdToggleInstrumentPanel:
            result.setInfo ("Toggle Instruments", "Show/hide instrument panel", "View", 0);
            result.addDefaultKeypress ('I', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            break;
        case cmdToggleMetronome:
            result.setInfo ("Toggle Metronome", "Toggle the metronome on/off", "View", 0);
            result.addDefaultKeypress ('K', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            break;
        case cmdToggleVelocityLanes:
            result.setInfo ("Show Velocity Lanes", "Show/hide per-note velocity columns", "View", 0);
            result.setTicked (trackerGrid == nullptr || trackerGrid->areVelocityLanesVisible());
            break;
        case cmdAudioPluginSettings:
            result.setInfo ("Audio & Plugin Settings...", "Configure audio output and plugin scan paths", "File", 0);
            result.addDefaultKeypress (',', juce::ModifierKeys::commandModifier);
            break;
        default: break;
    }
}

bool MainComponent::perform (const InvocationInfo& info)
{
    switch (info.commandID)
    {
        case loadSample:
            loadSampleForCurrentTrack();
            return true;
        case nextPattern:
            switchToPattern (patternData.getCurrentPatternIndex() + 1);
            return true;
        case prevPattern:
            switchToPattern (patternData.getCurrentPatternIndex() - 1);
            return true;
        case addPattern:
            patternData.addPattern (patternData.getCurrentPattern().numRows);
            switchToPattern (patternData.getNumPatterns() - 1);
            markDirty();
            return true;
        case muteTrack:
        {
            int track = trackerGrid->getCursorTrack();
            if (track >= kNumTracks)
                return true;
            auto* t = trackerEngine.getTrack (track);
            if (t) { t->setMute (! t->isMuted (false)); updateMuteSoloState(); markDirty(); }
            return true;
        }
        case soloTrack:
        {
            int track = trackerGrid->getCursorTrack();
            if (track >= kNumTracks)
                return true;
            auto* t = trackerEngine.getTrack (track);
            if (t) { t->setSolo (! t->isSolo (false)); updateMuteSoloState(); markDirty(); }
            return true;
        }
        case cmdCopy:
            doCopy();
            return true;
        case cmdPaste:
            doPaste();
            return true;
        case cmdCut:
            doCut();
            return true;
        case cmdUndo:
            if (undoManager.undo())
            {
                if (trackerGrid->onPatternDataChanged)
                    trackerGrid->onPatternDataChanged();
                trackerGrid->repaint();
                commandManager.commandStatusChanged();
            }
            return true;
        case cmdRedo:
            if (undoManager.redo())
            {
                if (trackerGrid->onPatternDataChanged)
                    trackerGrid->onPatternDataChanged();
                trackerGrid->repaint();
                commandManager.commandStatusChanged();
            }
            return true;
        case cmdNewProject:
            newProject();
            return true;
        case cmdOpen:
            openProject();
            return true;
        case cmdSave:
            saveProject();
            return true;
        case cmdSaveAs:
            saveProjectAs();
            return true;
        case cmdShowHelp:
            showHelpOverlay();
            return true;
        case cmdToggleArrangement:
            toggleArrangementPanel();
            return true;
        case cmdToggleSongMode:
            toggleSongMode();
            return true;
        case cmdToggleInstrumentPanel:
            instrumentPanelVisible = ! instrumentPanelVisible;
            toolbar->setInstrumentPanelVisible (instrumentPanelVisible);
            resized();
            return true;
        case cmdToggleMetronome:
        {
            bool enabled = ! trackerEngine.isMetronomeEnabled();
            trackerEngine.setMetronomeEnabled (enabled);
            toolbar->setMetronomeEnabled (enabled);
            return true;
        }
        case cmdToggleVelocityLanes:
            toggleVelocityLanes();
            return true;
        case cmdAudioPluginSettings:
            showAudioPluginSettings();
            return true;
        default: return false;
    }
}

//==============================================================================
// MenuBarModel
//==============================================================================

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Edit", "View", "Help" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int menuIndex, const juce::String&)
{
    juce::PopupMenu menu;
    if (menuIndex == 0)
    {
        menu.addCommandItem (&commandManager, cmdNewProject);
        menu.addCommandItem (&commandManager, cmdOpen);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, cmdSave);
        menu.addCommandItem (&commandManager, cmdSaveAs);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, loadSample);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, cmdAudioPluginSettings);
    }
    else if (menuIndex == 1)
    {
        menu.addCommandItem (&commandManager, cmdUndo);
        menu.addCommandItem (&commandManager, cmdRedo);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, cmdCopy);
        menu.addCommandItem (&commandManager, cmdCut);
        menu.addCommandItem (&commandManager, cmdPaste);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, muteTrack);
        menu.addCommandItem (&commandManager, soloTrack);
    }
    else if (menuIndex == 2)
    {
        menu.addCommandItem (&commandManager, cmdToggleArrangement);
        menu.addCommandItem (&commandManager, cmdToggleInstrumentPanel);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, cmdToggleSongMode);
        menu.addCommandItem (&commandManager, cmdToggleMetronome);
        menu.addCommandItem (&commandManager, cmdToggleVelocityLanes);
    }
    else if (menuIndex == 3)
    {
        menu.addCommandItem (&commandManager, cmdShowHelp);
    }
    return menu;
}

//==============================================================================

void MainComponent::timerCallback()
{
    if (trackerEngine.isPlaying())
    {
        int playRow = -1;
        int playPatternIndex = -1;

        if (songMode && arrangement.getNumEntries() > 0)
        {
            // Song mode: compute which pattern/row from the beat position
            double beatPos = trackerEngine.getPlaybackBeatPosition();
            auto info = getArrangementPlaybackPosition (beatPos);

            if (info.entryIndex >= 0)
            {
                // Auto-switch pattern if needed
                if (info.patternIndex != patternData.getCurrentPatternIndex())
                    switchToPattern (info.patternIndex);

                // Highlight current entry in arrangement panel
                if (arrangementVisible)
                    arrangementComponent->setPlayingEntry (info.entryIndex);

                playRow = info.rowInPattern;
                playPatternIndex = info.patternIndex;
            }
        }
        else
        {
            // Pattern mode: simple row from beat position
            playRow = trackerEngine.getPlaybackRow (patternData.getCurrentPattern().numRows);
            playPatternIndex = patternData.getCurrentPatternIndex();
        }

        applyAutomationAtPlaybackPosition (playPatternIndex, playRow);

        trackerGrid->setPlaybackRow (playRow);
        trackerGrid->setPlaying (true);

        // Update automation panel playback position and recording
        if (automationPanelVisible && automationPanel != nullptr)
        {
            automationPanel->setPlaybackRow (playRow);

            // Automation recording: poll current parameter value and record it
            if (automationPanel->isRecording() && playRow >= 0)
            {
                if (automationPanel->onGetCurrentParameterValue)
                {
                    float currentValue = automationPanel->onGetCurrentParameterValue();
                    automationPanel->recordParameterValue (playRow, currentValue);
                }
            }
        }

        // Follow mode
        if (followMode != FollowMode::Off && playRow >= 0)
        {
            int visibleRows = trackerGrid->getVisibleRowCount();

            if (followMode == FollowMode::Center)
            {
                // Keep playback row centered
                trackerGrid->setScrollOffset (playRow - visibleRows / 2);
            }
            else if (followMode == FollowMode::Page)
            {
                // Page-style: scroll when playback is near the bottom
                int scrollOff = trackerGrid->getScrollOffset();
                int margin = juce::jmax (4, visibleRows / 6);
                if (playRow >= scrollOff + visibleRows - margin)
                    trackerGrid->setScrollOffset (playRow - margin);
                else if (playRow < scrollOff)
                    trackerGrid->setScrollOffset (playRow - margin);
            }
        }
    }
    else
    {
        trackerGrid->setPlaying (false);
        if (arrangementVisible)
            arrangementComponent->setPlayingEntry (-1);
        if (automationPanelVisible && automationPanel != nullptr)
            automationPanel->setPlaybackRow (-1);
    }
}

MainComponent::ArrangementPlaybackInfo MainComponent::getArrangementPlaybackPosition (double beatPos) const
{
    ArrangementPlaybackInfo info;
    if (beatPos < 0.0) return info;

    int rpb = trackerEngine.getRowsPerBeat();
    double accBeats = 0.0;

    for (int i = 0; i < arrangement.getNumEntries(); ++i)
    {
        auto& entry = arrangement.getEntry (i);
        if (entry.patternIndex < 0 || entry.patternIndex >= patternData.getNumPatterns())
            continue;

        auto& pat = patternData.getPattern (entry.patternIndex);
        double patBeats = static_cast<double> (pat.numRows) / static_cast<double> (rpb);
        double entryBeats = patBeats * entry.repeats;

        if (beatPos < accBeats + entryBeats)
        {
            // We're in this entry
            info.entryIndex = i;
            info.patternIndex = entry.patternIndex;
            double beatsIntoEntry = beatPos - accBeats;
            // Handle repeats: get position within a single pattern
            double beatsIntoPattern = std::fmod (beatsIntoEntry, patBeats);
            info.rowInPattern = static_cast<int> (beatsIntoPattern * static_cast<double> (rpb));
            info.rowInPattern = juce::jlimit (0, pat.numRows - 1, info.rowInPattern);
            return info;
        }

        accBeats += entryBeats;
    }

    return info; // past the end
}

void MainComponent::updateStatusBar()
{
    // Check if a temporary status message is active
    if (temporaryStatusMessage.isNotEmpty())
    {
        auto now = juce::Time::getMillisecondCounter();
        if (now < temporaryStatusExpiry)
        {
            statusLabel.setText (temporaryStatusMessage, juce::dontSendNotification);
            statusLabel.setColour (juce::Label::textColourId,
                                   temporaryStatusIsError ? juce::Colour (0xffff4444)
                                                          : juce::Colour (0xffffcc44));
            octaveLabel.setText ("Oct:" + juce::String (trackerGrid->getOctave()),
                                 juce::dontSendNotification);
            bpmLabel.setText ("BPM:" + juce::String (trackerEngine.getBpm(), 1),
                              juce::dontSendNotification);
            return;
        }
        // Expired -- clear it
        temporaryStatusMessage.clear();
        statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xffcccccc));
    }

    auto playState = trackerEngine.isPlaying() ? "PLAYING" : "STOPPED";
    auto row = juce::String::formatted ("%02X", trackerGrid->getCursorRow());
    auto track = trackerGrid->isCursorInMasterLane()
                     ? juce::String ("MASTER")
                     : juce::String::formatted ("%02d", trackerGrid->getCursorTrack() + 1);

    const char* subColNames[] = { "Note", "Inst", "Vel", "FX" };
    auto subCol = subColNames[static_cast<int> (trackerGrid->getCursorSubColumn())];

    statusLabel.setText (juce::String (playState) + "  Row:" + row + "  Track:" + track
                             + " [" + subCol + "]"
                             + "  Step:" + juce::String (trackerGrid->getEditStep()),
                         juce::dontSendNotification);

    octaveLabel.setText ("Oct:" + juce::String (trackerGrid->getOctave()),
                         juce::dontSendNotification);

    bpmLabel.setText ("BPM:" + juce::String (trackerEngine.getBpm(), 1),
                      juce::dontSendNotification);
}

void MainComponent::setTemporaryStatus (const juce::String& message, bool isError, int timeoutMs)
{
    temporaryStatusMessage = message;
    temporaryStatusIsError = isError;
    temporaryStatusExpiry = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);
    updateStatusBar();
}

void MainComponent::updateToolbar()
{
    auto& pat = patternData.getCurrentPattern();
    toolbar->setPatternInfo (patternData.getCurrentPatternIndex() + 1, patternData.getNumPatterns(), pat.name);
    toolbar->setPatternLength (pat.numRows);
    toolbar->setInstrument (trackerGrid->getCurrentInstrument());
    toolbar->setOctave (trackerGrid->getOctave());
    toolbar->setEditStep (trackerGrid->getEditStep());
    toolbar->setBpm (trackerEngine.getBpm());
    toolbar->setRowsPerBeat (trackerEngine.getRowsPerBeat());
    toolbar->setPlayState (trackerEngine.isPlaying());
    toolbar->setPlaybackMode (songMode);

    toolbar->setAutomationPanelVisible (automationPanelVisible);

    // Show sample name for current instrument
    auto sampleFile = trackerEngine.getSampler().getSampleFile (trackerGrid->getCurrentInstrument());
    toolbar->setSampleName (sampleFile.existsAsFile() ? sampleFile.getFileNameWithoutExtension() : "");
}

void MainComponent::loadSampleForCurrentTrack()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        "Load Sample",
        juce::File::getSpecialLocation (juce::File::userHomeDirectory),
        "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (file.existsAsFile())
                              {
                                  int inst = trackerGrid->getCurrentInstrument();
                                  auto error = trackerEngine.loadSampleForInstrument (inst, file);
                                  if (error.isNotEmpty())
                                      juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                                              "Load Error", error);
                                  else
                                  {
                                      if (trackerEngine.isPlaying())
                                      {
                                          if (songMode)
                                              syncArrangementToEdit();
                                          else
                                              trackerEngine.refreshTracksForInstrument (inst, patternData.getCurrentPattern());
                                      }

                                      trackerGrid->repaint();
                                      updateToolbar();
                                      updateInstrumentPanel();
                                      fileBrowser->updateInstrumentSlots (trackerEngine.getSampler().getLoadedSamples());
                                      markDirty();
                                  }
                              }
                          });
}

void MainComponent::switchToPattern (int index)
{
    index = juce::jlimit (0, patternData.getNumPatterns() - 1, index);
    patternData.setCurrentPattern (index);

    // Clear any selection from the previous pattern
    trackerGrid->clearSelection();

    // Clamp cursor row to new pattern length
    auto& pat = patternData.getCurrentPattern();
    trackerGrid->setCursorPosition (
        juce::jmin (trackerGrid->getCursorRow(), pat.numRows - 1),
        trackerGrid->getCursorTrack());

    // Re-sync edit if playing in pattern mode (not song mode)
    if (trackerEngine.isPlaying() && ! songMode)
        trackerEngine.syncPatternToEdit (pat, getReleaseModes());

    trackerGrid->repaint();
    updateTrackSampleMarkers();
    updateStatusBar();
    updateToolbar();
    if (automationPanelVisible)
        refreshAutomationPanel();
}

void MainComponent::showPatternLengthEditor()
{
    auto* aw = new juce::AlertWindow ("Pattern Length", "Enter new pattern length (1-256):", juce::AlertWindow::NoIcon);
    aw->addTextEditor ("length", juce::String (patternData.getCurrentPattern().numRows));
    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw] (int result)
    {
        if (result == 1)
        {
            int newLen = aw->getTextEditorContents ("length").getIntValue();
            newLen = juce::jlimit (1, 256, newLen);
            auto& pat = patternData.getCurrentPattern();
            pat.resize (newLen);
            trackerGrid->setCursorPosition (
                juce::jmin (trackerGrid->getCursorRow(), newLen - 1),
                trackerGrid->getCursorTrack());

            // Re-sync edit while playing.
            if (trackerEngine.isPlaying())
            {
                if (songMode)
                    syncArrangementToEdit();
                else
                {
                    trackerEngine.syncPatternToEdit (pat, getReleaseModes());
                    trackerEngine.updateLoopRangeForPatternLength (pat.numRows);
                }
            }

            trackerGrid->repaint();
            updateToolbar();
            markDirty();
        }
        delete aw;
    }), true);
}

void MainComponent::showPatternNameEditor()
{
    auto& pat = patternData.getCurrentPattern();
    auto* aw = new juce::AlertWindow ("Pattern Name", "Enter a name for this pattern:", juce::AlertWindow::NoIcon);
    aw->addTextEditor ("name", pat.name);
    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw] (int result)
    {
        if (result == 1)
        {
            patternData.getCurrentPattern().name = aw->getTextEditorContents ("name");
            updateToolbar();
            markDirty();
        }
        delete aw;
    }), true);
}

void MainComponent::showMidiGeneratorDialog (int targetTrack)
{
    if (targetTrack < 0)
        targetTrack = trackerGrid->getCursorTrack();

    if (targetTrack < 0 || targetTrack >= kNumTracks)
    {
        setTemporaryStatus ("Select a regular track before generating MIDI.", true, 3000);
        return;
    }

    auto* content = new MidiGeneratorComponent (trackerLookAndFeel, targetTrack);
    content->onGenerate = [this, targetTrack] (const MidiGeneratorSettings& settings)
    {
        applyGeneratedMidiToTrack (targetTrack, settings);
    };

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (content);
    opts.dialogTitle = "Generate MIDI";
    opts.dialogBackgroundColour = trackerLookAndFeel.findColour (TrackerLookAndFeel::backgroundColourId);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = false;
    opts.resizable = false;
    opts.launchAsync();
}

void MainComponent::applyGeneratedMidiToTrack (int targetTrack, const MidiGeneratorSettings& settings)
{
    if (targetTrack < 0 || targetTrack >= kNumTracks)
        return;

    const int instrument = trackerGrid->getCurrentInstrument();
    if (auto error = trackerEngine.validateNoteEntry (instrument, targetTrack); error.isNotEmpty())
    {
        setTemporaryStatus (error, true, 3000);
        return;
    }

    auto& pat = patternData.getCurrentPattern();
    const int startRow = settings.startAtCursor ? trackerGrid->getCursorRow() : 0;
    if (startRow < 0 || startRow >= pat.numRows)
        return;

    const int rowsPerBeat = juce::jmax (1, trackerEngine.getRowsPerBeat());
    const int barRows = rowsPerBeat * 4;
    const int requestedRows = juce::jmax (1, settings.bars) * barRows;
    const int endRow = juce::jmin (pat.numRows, startRow + requestedRows);
    if (endRow <= startRow)
        return;

    juce::Random random;
    auto scaleIntervals = getScaleIntervals (settings.scale);
    auto progression = getProgressionDegrees (settings.progression, settings.bars, random);
    if (progression.empty())
        progression = { 0 };

    std::vector<Cell> newCells;
    newCells.reserve (static_cast<size_t> (endRow - startRow));
    for (int row = startRow; row < endRow; ++row)
    {
        auto cell = pat.getCell (row, targetTrack);
        cell.note = -1;
        cell.instrument = -1;
        cell.volume = -1;
        cell.extraNoteLanes.clear();
        newCells.push_back (cell);
    }

    int maxWrittenLane = 0;
    const int chordOctave = juce::jlimit (1, 8, trackerGrid->getOctave());
    const int bassOctave = juce::jmax (0, chordOctave - 1);
    const bool generateChords = settings.outputMode == 0 || settings.outputMode == 2;
    const bool generateBass = settings.outputMode == 1 || settings.outputMode == 2;
    const int chordLaneOffset = generateBass && generateChords ? 1 : 0;

    auto writeNote = [&] (int row, int lane, int note, int velocity)
    {
        if (row < startRow || row >= endRow || lane < 0)
            return;

        note = juce::jlimit (0, 127, note + settings.transpose);
        auto& cell = newCells[static_cast<size_t> (row - startRow)];
        NoteSlot slot;
        slot.note = note;
        slot.instrument = instrument;
        slot.volume = juce::jlimit (1, 127, velocity);
        cell.setNoteLane (lane, slot);
        maxWrittenLane = juce::jmax (maxWrittenLane, lane + 1);
    };

    auto addBassPattern = [&] (int barStart, int degree, int nextDegree)
    {
        auto bassNote = [&] (int scaleStep)
        {
            return noteFromScaleStep (settings.keyRoot, scaleIntervals, scaleStep, bassOctave);
        };

        auto add = [&] (int offset, int scaleStep, int baseVelocity = 112)
        {
            writeNote (barStart + offset, 0, bassNote (scaleStep),
                       randomizedVelocity (random, baseVelocity, settings.randomAmount));
        };

        switch (settings.bassPattern)
        {
            case 0:
                add (0, degree);
                break;
            case 1:
                add (0, degree);
                add (barRows / 2, degree + 4, 108);
                break;
            case 2:
                for (int beat = 0; beat < 4; ++beat)
                    add (beat * rowsPerBeat, beat % 2 == 0 ? degree : degree + 7, 110);
                break;
            case 3:
            {
                const int stepRows = juce::jmax (1, rowsPerBeat / 2);
                for (int offset = 0, i = 0; offset < barRows; offset += stepRows, ++i)
                {
                    const int sequence[] = { degree, degree, degree + 4, degree + 7 };
                    add (offset, sequence[i % 4], i % 2 == 0 ? 108 : 98);
                }
                break;
            }
            case 4:
            {
                add (0, degree, 114);
                add (rowsPerBeat, degree + 2, 104);
                add (2 * rowsPerBeat, degree + 4, 108);
                add (3 * rowsPerBeat, nextDegree >= degree ? nextDegree - 1 : nextDegree + 1, 100);
                break;
            }
            case 5:
            {
                const int halfBeat = juce::jmax (1, rowsPerBeat / 2);
                for (int beat = 0; beat < 4; ++beat)
                    add (beat * rowsPerBeat + halfBeat, beat % 2 == 0 ? degree : degree + 4, 104);
                break;
            }
            default:
                add (0, degree);
                break;
        }

        if (settings.randomAmount > 55 && random.nextFloat() < static_cast<float> (settings.randomAmount) / 180.0f)
        {
            const int pickupOffset = juce::jmax (0, barRows - juce::jmax (1, rowsPerBeat / 2));
            add (pickupOffset, nextDegree >= degree ? nextDegree - 1 : nextDegree + 1, 90);
        }
    };

    const auto chordHitOffsets = getChordHitOffsets (settings.chordRhythm, barRows, rowsPerBeat);
    const int totalBarsToWrite = juce::jmax (1, (endRow - startRow + barRows - 1) / barRows);

    for (int bar = 0; bar < totalBarsToWrite; ++bar)
    {
        const int barStart = startRow + bar * barRows;
        if (barStart >= endRow)
            break;

        const int degree = progression[static_cast<size_t> (bar % static_cast<int> (progression.size()))];
        const int nextDegree = progression[static_cast<size_t> ((bar + 1) % static_cast<int> (progression.size()))];

        if (generateBass)
            addBassPattern (barStart, degree, nextDegree);

        if (generateChords)
        {
            auto scaleSteps = getChordScaleSteps (degree, settings.chordStyle);
            std::vector<int> chordNotes;
            chordNotes.reserve (scaleSteps.size());
            for (auto step : scaleSteps)
                chordNotes.push_back (noteFromScaleStep (settings.keyRoot, scaleIntervals, step, chordOctave));

            randomizeChordVoicing (chordNotes, random, settings.randomAmount);

            for (auto offset : chordHitOffsets)
            {
                const int row = barStart + offset;
                if (row >= endRow)
                    continue;

                for (int lane = 0; lane < static_cast<int> (chordNotes.size()); ++lane)
                    writeNote (row, chordLaneOffset + lane, chordNotes[static_cast<size_t> (lane)],
                               randomizedVelocity (random, 104, settings.randomAmount));
            }
        }
    }

    std::vector<MultiCellEditAction::CellRecord> records;
    for (int row = startRow; row < endRow; ++row)
    {
        const auto oldCell = pat.getCell (row, targetTrack);
        const auto& newCell = newCells[static_cast<size_t> (row - startRow)];
        if (! PatternEditUtils::sameCell (oldCell, newCell))
            records.push_back ({ row, targetTrack, oldCell, newCell });
    }

    if (maxWrittenLane > trackLayout.getTrackNoteLaneCount (targetTrack))
    {
        performUndoableTrackLayoutChange ([this, targetTrack, maxWrittenLane]
        {
            trackLayout.setTrackNoteLaneCount (targetTrack, maxWrittenLane);
        });
    }

    const bool changed = PatternEditUtils::applyPatternEdit (patternData, &undoManager,
                                                             patternData.getCurrentPatternIndex(),
                                                             std::move (records), {});
    if (changed)
    {
        trackerGrid->setCursorPosition (startRow, targetTrack);
        if (trackerGrid->onPatternDataChanged)
            trackerGrid->onPatternDataChanged();
        trackerGrid->repaint();
        commandManager.commandStatusChanged();
        setTemporaryStatus ("Generated MIDI on Track " + juce::String (targetTrack + 1)
                                + " (" + juce::String (endRow - startRow) + " rows)",
                            false, 3000);
    }
}

void MainComponent::transposeNotesInRange (int startRow, int endRow, int startVisualTrack,
                                           int endVisualTrack, int semitones)
{
    if (semitones == 0)
        return;

    auto& pat = patternData.getCurrentPattern();
    startRow = juce::jlimit (0, pat.numRows - 1, startRow);
    endRow = juce::jlimit (0, pat.numRows - 1, endRow);
    if (startRow > endRow)
        std::swap (startRow, endRow);

    startVisualTrack = juce::jlimit (0, trackLayout.getTrackLaneCount() - 1, startVisualTrack);
    endVisualTrack = juce::jlimit (0, trackLayout.getTrackLaneCount() - 1, endVisualTrack);
    if (startVisualTrack > endVisualTrack)
        std::swap (startVisualTrack, endVisualTrack);

    std::vector<MultiCellEditAction::CellRecord> records;
    for (int row = startRow; row <= endRow; ++row)
    {
        for (int vi = startVisualTrack; vi <= endVisualTrack; ++vi)
        {
            const int track = trackLayout.visualToPhysical (vi);
            auto oldCell = pat.getCell (row, track);
            auto newCell = oldCell;
            bool changed = false;
            const int laneCount = newCell.getNumNoteLanes();
            for (int lane = 0; lane < laneCount; ++lane)
            {
                auto slot = newCell.getNoteLane (lane);
                if (slot.note >= 0 && slot.note < 128)
                {
                    const int newNote = juce::jlimit (0, 127, slot.note + semitones);
                    if (newNote != slot.note)
                    {
                        slot.note = newNote;
                        newCell.setNoteLane (lane, slot);
                        changed = true;
                    }
                }
            }

            if (changed && ! PatternEditUtils::sameCell (oldCell, newCell))
                records.push_back ({ row, track, oldCell, newCell });
        }
    }

    const bool applied = PatternEditUtils::applyPatternEdit (patternData, &undoManager,
                                                             patternData.getCurrentPatternIndex(),
                                                             std::move (records), {});
    if (applied)
    {
        if (trackerGrid->onPatternDataChanged)
            trackerGrid->onPatternDataChanged();
        trackerGrid->repaint();
        commandManager.commandStatusChanged();
        setTemporaryStatus ("Transposed notes " + juce::String (semitones > 0 ? "+" : "")
                                + juce::String (semitones),
                            false, 2000);
    }
}

void MainComponent::showTrackHeaderMenu (int track, juce::Point<int> screenPos)
{
    const bool isMasterColumn = (track == TrackerGrid::kMasterLaneTrack);
    juce::PopupMenu menu;

    if (isMasterColumn)
    {
        int trackLanes = trackLayout.getTrackLaneCount();
        menu.addItem (26, "Add Track (" + juce::String (trackLanes) + " -> "
                         + juce::String (trackLanes + 1) + ")", trackLanes < kNumTracks);
        menu.addItem (27, "Remove Last Track (" + juce::String (trackLanes) + " -> "
                         + juce::String (trackLanes - 1) + ")", trackLanes > 1);
        menu.addSeparator();

        int masterFxLanes = trackLayout.getMasterFxLaneCount();
        menu.addItem (24, "Add Master FX Lane (" + juce::String (masterFxLanes) + " -> "
                         + juce::String (masterFxLanes + 1) + ")", masterFxLanes < 8);
        menu.addItem (25, "Remove Master FX Lane (" + juce::String (masterFxLanes) + " -> "
                         + juce::String (masterFxLanes - 1) + ")", masterFxLanes > 1);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }),
                            [this] (int result)
                            {
                                if (result == 26)
                                {
                                    performUndoableTrackLayoutChange ([this]
                                    {
                                        trackLayout.addTrackLane();
                                    });
                                }
                                else if (result == 27)
                                {
                                    performUndoableTrackLayoutChange ([this]
                                    {
                                        trackLayout.removeTrackLane();
                                    });
                                    trackerGrid->setCursorPosition (trackerGrid->getCursorRow(),
                                                                    TrackerGrid::kMasterLaneTrack);
                                }
                                else if (result == 24)
                                {
                                    performUndoableTrackLayoutChange ([this]
                                    {
                                        trackLayout.setMasterFxLaneCount (trackLayout.getMasterFxLaneCount() + 1);
                                    });
                                }
                                else if (result == 25)
                                {
                                    performUndoableTrackLayoutChange ([this]
                                    {
                                        trackLayout.setMasterFxLaneCount (trackLayout.getMasterFxLaneCount() - 1);
                                    });
                                }
                            });
        return;
    }

    if (track < 0 || track >= kNumTracks)
        return;

    auto* t = trackerEngine.getTrack (track);
    if (t != nullptr)
    {
        bool muted = t->isMuted (false);
        bool soloed = t->isSolo (false);

        menu.addItem (1, muted ? "Unmute" : "Mute");
        menu.addItem (2, soloed ? "Unsolo" : "Solo");
        menu.addSeparator();
    }
    menu.addItem (3, "Load Sample...");
    menu.addItem (4, "Rename Track...");
    menu.addItem (kMenuGenerateMidi, "Generate MIDI...");
    menu.addSeparator();

    // Selection bounds are in visual space; get visual range
    int rangeStart, rangeEnd;
    int transposeStartRow = 0;
    int transposeEndRow = patternData.getCurrentPattern().numRows - 1;
    int transposeStartVisual = 0;
    int transposeEndVisual = 0;
    bool canTransposeSelection = false;
    const int trackLaneCount = trackLayout.getTrackLaneCount();
    if (trackerGrid->hasSelection)
    {
        int minRow, maxRow, minViTrack, maxViTrack;
        trackerGrid->getSelectionBounds (minRow, maxRow, minViTrack, maxViTrack);
        rangeStart = juce::jlimit (0, trackLaneCount - 1, minViTrack);
        rangeEnd = juce::jlimit (0, trackLaneCount - 1, maxViTrack);
        transposeStartRow = minRow;
        transposeEndRow = maxRow;
        transposeStartVisual = rangeStart;
        transposeEndVisual = rangeEnd;
        canTransposeSelection = minViTrack <= trackLaneCount - 1 && maxViTrack >= 0;
    }
    else
    {
        rangeStart = trackLayout.physicalToVisual (track);
        rangeEnd = rangeStart;
        transposeStartVisual = rangeStart;
        transposeEndVisual = rangeEnd;
    }

    juce::PopupMenu transposeSelectionMenu;
    transposeSelectionMenu.addItem (kMenuTransposeSelectionUpSemitone, "Up Semitone");
    transposeSelectionMenu.addItem (kMenuTransposeSelectionDownSemitone, "Down Semitone");
    transposeSelectionMenu.addItem (kMenuTransposeSelectionUpOctave, "Up Octave");
    transposeSelectionMenu.addItem (kMenuTransposeSelectionDownOctave, "Down Octave");
    menu.addSubMenu ("Transpose Selection", transposeSelectionMenu, canTransposeSelection);

    juce::PopupMenu transposeTrackMenu;
    transposeTrackMenu.addItem (kMenuTransposeTrackUpSemitone, "Up Semitone");
    transposeTrackMenu.addItem (kMenuTransposeTrackDownSemitone, "Down Semitone");
    transposeTrackMenu.addItem (kMenuTransposeTrackUpOctave, "Up Octave");
    transposeTrackMenu.addItem (kMenuTransposeTrackDownOctave, "Down Octave");
    menu.addSubMenu ("Transpose Track", transposeTrackMenu);
    menu.addSeparator();

    menu.addItem (10, "Move Track Left", rangeStart > 0);
    menu.addItem (11, "Move Track Right", rangeEnd < trackLaneCount - 1);
    menu.addItem (26, "Add Track (" + juce::String (trackLaneCount) + " -> "
                     + juce::String (trackLaneCount + 1) + ")", trackLaneCount < kNumTracks);

    // Group selected tracks (if selection spans multiple tracks)
    if (trackerGrid->hasSelection)
    {
        if (rangeStart != rangeEnd && trackLayout.canCreateGroup())
            menu.addItem (12, "Group Selected Tracks...");
    }

    // Note lanes
    menu.addSeparator();
    int noteLanes = trackLayout.getTrackNoteLaneCount (track);
    menu.addItem (22, "Add Note Lane (" + juce::String (noteLanes) + " -> " + juce::String (noteLanes + 1) + ")", noteLanes < 8);
    menu.addItem (23, "Remove Note Lane (" + juce::String (noteLanes) + " -> " + juce::String (noteLanes - 1) + ")", noteLanes > 1);

    // FX lanes
    menu.addSeparator();
    int fxLanes = trackLayout.getTrackFxLaneCount (track);
    menu.addItem (20, "Add FX Lane (" + juce::String (fxLanes) + " -> " + juce::String (fxLanes + 1) + ")", fxLanes < 8);
    menu.addItem (21, "Remove FX Lane (" + juce::String (fxLanes) + " -> " + juce::String (fxLanes - 1) + ")", fxLanes > 1);

    int groupIdx = trackLayout.getGroupForTrack (track);
    if (groupIdx >= 0)
    {
        menu.addSeparator();
        menu.addItem (13, "Remove from Group");
        menu.addItem (14, "Dissolve Group");
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }),
                        [this, track, t, rangeStart, rangeEnd, groupIdx,
                         transposeStartRow, transposeEndRow, transposeStartVisual, transposeEndVisual] (int result)
                        {
                            if (result == 1 && t)
                            {
                                t->setMute (! t->isMuted (false));
                                updateMuteSoloState();
                                markDirty();
                            }
                            else if (result == 2 && t)
                            {
                                t->setSolo (! t->isSolo (false));
                                updateMuteSoloState();
                                markDirty();
                            }
                            else if (result == 3)
                            {
                                trackerGrid->setCursorPosition (trackerGrid->getCursorRow(), track);
                                loadSampleForCurrentTrack();
                            }
                            else if (result == 4)
                            {
                                showRenameTrackDialog (track);
                            }
                            else if (result == kMenuGenerateMidi)
                            {
                                trackerGrid->setCursorPosition (trackerGrid->getCursorRow(), track);
                                showMidiGeneratorDialog (track);
                            }
                            else if (result == kMenuTransposeSelectionUpSemitone)
                            {
                                transposeNotesInRange (transposeStartRow, transposeEndRow,
                                                       transposeStartVisual, transposeEndVisual, 1);
                            }
                            else if (result == kMenuTransposeSelectionDownSemitone)
                            {
                                transposeNotesInRange (transposeStartRow, transposeEndRow,
                                                       transposeStartVisual, transposeEndVisual, -1);
                            }
                            else if (result == kMenuTransposeSelectionUpOctave)
                            {
                                transposeNotesInRange (transposeStartRow, transposeEndRow,
                                                       transposeStartVisual, transposeEndVisual, 12);
                            }
                            else if (result == kMenuTransposeSelectionDownOctave)
                            {
                                transposeNotesInRange (transposeStartRow, transposeEndRow,
                                                       transposeStartVisual, transposeEndVisual, -12);
                            }
                            else if (result == kMenuTransposeTrackUpSemitone)
                            {
                                auto vi = trackLayout.physicalToVisual (track);
                                transposeNotesInRange (0, patternData.getCurrentPattern().numRows - 1, vi, vi, 1);
                            }
                            else if (result == kMenuTransposeTrackDownSemitone)
                            {
                                auto vi = trackLayout.physicalToVisual (track);
                                transposeNotesInRange (0, patternData.getCurrentPattern().numRows - 1, vi, vi, -1);
                            }
                            else if (result == kMenuTransposeTrackUpOctave)
                            {
                                auto vi = trackLayout.physicalToVisual (track);
                                transposeNotesInRange (0, patternData.getCurrentPattern().numRows - 1, vi, vi, 12);
                            }
                            else if (result == kMenuTransposeTrackDownOctave)
                            {
                                auto vi = trackLayout.physicalToVisual (track);
                                transposeNotesInRange (0, patternData.getCurrentPattern().numRows - 1, vi, vi, -12);
                            }
                            else if (result == 10)
                            {
                                performUndoableTrackLayoutChange ([this, rangeStart, rangeEnd]
                                {
                                    trackLayout.moveVisualRange (rangeStart, rangeEnd, -1);
                                });
                            }
                            else if (result == 11)
                            {
                                performUndoableTrackLayoutChange ([this, rangeStart, rangeEnd]
                                {
                                    trackLayout.moveVisualRange (rangeStart, rangeEnd, +1);
                                });
                            }
                            else if (result == 12)
                            {
                                // Group selected tracks — prompt for name
                                auto* aw = new juce::AlertWindow ("Group Tracks", "Enter a name for this group:", juce::AlertWindow::NoIcon);
                                aw->addTextEditor ("name", "Group");
                                aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
                                aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

                                aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw, rangeStart, rangeEnd] (int res)
                                {
                                    if (res == 1)
                                    {
                                        auto name = aw->getTextEditorContents ("name");
                                        if (name.isEmpty()) name = "Group";
                                        performUndoableTrackLayoutChange ([this, name, rangeStart, rangeEnd]
                                        {
                                            trackLayout.createGroup (name, rangeStart, rangeEnd);
                                        });
                                    }
                                    delete aw;
                                }), true);
                            }
                            else if (result == 26)
                            {
                                performUndoableTrackLayoutChange ([this]
                                {
                                    trackLayout.addTrackLane();
                                });
                            }
                            else if (result == 13 && groupIdx >= 0)
                            {
                                performUndoableTrackLayoutChange ([this, track]
                                {
                                    int currentGroupIdx = trackLayout.getGroupForTrack (track);
                                    if (currentGroupIdx < 0 || currentGroupIdx >= trackLayout.getNumGroups())
                                        return;

                                    // Remove this track from its group.
                                    auto& group = trackLayout.getGroup (currentGroupIdx);
                                    auto [groupFirst, groupLast] = trackLayout.getGroupVisualRange (currentGroupIdx);
                                    int trackVisual = trackLayout.physicalToVisual (track);
                                    if (trackVisual != groupFirst && trackVisual != groupLast)
                                        return;

                                    group.trackIndices.erase (
                                        std::remove (group.trackIndices.begin(), group.trackIndices.end(), track),
                                        group.trackIndices.end());
                                    trackLayout.normalizeGroups();
                                });
                            }
                            else if (result == 14 && groupIdx >= 0)
                            {
                                performUndoableTrackLayoutChange ([this, groupIdx]
                                {
                                    if (groupIdx >= 0 && groupIdx < trackLayout.getNumGroups())
                                        trackLayout.removeGroup (groupIdx);
                                });
                            }
                            else if (result == 20)
                            {
                                performUndoableTrackLayoutChange ([this, track]
                                {
                                    trackLayout.addFxLane (track);
                                });
                            }
                            else if (result == 21)
                            {
                                performUndoableTrackLayoutChange ([this, track]
                                {
                                    trackLayout.removeFxLane (track);
                                });
                            }
                            else if (result == 22)
                            {
                                performUndoableTrackLayoutChange ([this, track]
                                {
                                    trackLayout.addNoteLane (track);
                                });
                            }
                            else if (result == 23)
                            {
                                performUndoableTrackLayoutChange ([this, track]
                                {
                                    trackLayout.removeNoteLane (track);
                                });
                            }
                        });
}

void MainComponent::showRenameTrackDialog (int track)
{
    auto currentName = trackLayout.getTrackName (track);
    auto defaultText = currentName.isNotEmpty() ? currentName
                                                 : juce::String::formatted ("T%02d", track + 1);

    auto* aw = new juce::AlertWindow ("Rename Track",
        "Enter a name for Track " + juce::String (track + 1) + ":",
        juce::AlertWindow::NoIcon);
    aw->addTextEditor ("name", defaultText);
    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw, track] (int result)
    {
        if (result == 1)
        {
            auto name = aw->getTextEditorContents ("name").trim();
            // If name matches default "T##" pattern, clear it
            if (name == juce::String::formatted ("T%02d", track + 1))
                name.clear();
            trackLayout.setTrackName (track, name);
            markDirty();
            trackerGrid->repaint();
        }
        delete aw;
    }), true);
}

void MainComponent::performUndoableTrackLayoutChange (const std::function<void()>& changeFn)
{
    auto before = trackLayout.createSnapshot();
    changeFn();
    auto after = trackLayout.createSnapshot();

    if (TrackLayout::snapshotsEqual (before, after))
    {
        trackerGrid->repaint();
        return;
    }

    undoManager.perform (new TrackLayoutEditAction (trackLayout, std::move (before), std::move (after)));

    trackerGrid->setCursorPosition (trackerGrid->getCursorRow(), trackerGrid->getCursorTrack());
    trackerGrid->repaint();
    if (mixerComponent != nullptr)
    {
        mixerComponent->resized();
        mixerComponent->repaint();
    }
    if (trackerGrid->onPatternDataChanged)
        trackerGrid->onPatternDataChanged();
}

void MainComponent::markDirty()
{
    isDirty = true;
    updateWindowTitle();
}

void MainComponent::updateWindowTitle()
{
    auto name = currentProjectFile.existsAsFile() ? currentProjectFile.getFileName() : "Untitled";
    auto title = "VCTracker - " + name + (isDirty ? " *" : "");
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
        window->setName (title);
}

bool MainComponent::confirmDiscardChanges()
{
    if (! isDirty) return true;
    return juce::AlertWindow::showOkCancelBox (juce::AlertWindow::QuestionIcon,
                                                "Unsaved Changes",
                                                "You have unsaved changes. Discard them?",
                                                "Discard", "Cancel");
}

void MainComponent::newProject()
{
    if (! confirmDiscardChanges()) return;

    trackerEngine.stop();
    patternData.clearAllPatterns();
    arrangement.clear();
    trackLayout.resetToDefault();
    trackerEngine.getSampler().clearLoadedSamples();
    arrangementComponent->setSelectedEntry (-1);
    trackerGrid->setCursorPosition (0, 0);
    trackerGrid->clearSelection();
    for (int i = 0; i < kNumTracks; ++i)
    {
        trackerGrid->trackMuted[static_cast<size_t> (i)] = false;
        trackerGrid->trackSoloed[static_cast<size_t> (i)] = false;
    }
    trackerEngine.setBpm (120.0);
    trackerEngine.setRowsPerBeat (4);
    trackerGrid->setRowsPerBeat (trackerEngine.getRowsPerBeat());
    trackerEngine.invalidateTrackInstruments();
    trackerEngine.setInstrumentSlotInfos ({});
    mixerState.reset();
    trackerEngine.rebuildMixerPluginChains();
    invalidateAutomationPluginCache();
    undoManager.clearUndoHistory();
    currentProjectFile = juce::File();
    isDirty = false;
    updateWindowTitle();
    updateStatusBar();
    updateToolbar();
    updateInstrumentPanel();
    if (mixerComponent != nullptr)
    {
        mixerComponent->resized();
        mixerComponent->repaint();
    }
    fileBrowser->updateInstrumentSlots (trackerEngine.getSampler().getLoadedSamples());
    trackerGrid->repaint();
    if (automationPanelVisible)
        refreshAutomationPanel();
}

void MainComponent::openProject()
{
    if (! confirmDiscardChanges()) return;

    auto chooser = std::make_shared<juce::FileChooser> (
        "Open Project",
        juce::File::getSpecialLocation (juce::File::userHomeDirectory),
        "*.tkadj");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (! file.existsAsFile()) return;

                              trackerEngine.stop();
                              arrangement.clear();

                              double bpm = 120.0;
                              int rpb = 4;
                              std::map<int, juce::File> samples;
                              std::map<int, InstrumentParams> instParams;

                              DelayParams loadedDelay;
                              ReverbParams loadedReverb;
                              int loadedFollowMode = 0;
                              juce::String browserDir;
                              std::map<int, InstrumentSlotInfo> loadedPluginSlots;
                              auto error = ProjectSerializer::loadFromFile (file, patternData, bpm, rpb, samples, instParams, arrangement, trackLayout, mixerState, loadedDelay, loadedReverb, &loadedFollowMode, &browserDir, &loadedPluginSlots);
                              if (error.isNotEmpty())
                              {
                                  juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                                          "Load Error", error);
                                  return;
                              }

                              trackerEngine.setBpm (bpm);
                              trackerEngine.setRowsPerBeat (rpb);
                              trackerGrid->setRowsPerBeat (trackerEngine.getRowsPerBeat());

                              // Reload samples
                              trackerEngine.getSampler().clearLoadedSamples();

                              for (auto& [index, sampleFile] : samples)
                                  trackerEngine.loadSampleForInstrument (index, sampleFile);

                              // Restore instrument params
                              for (auto& [index, params] : instParams)
                                  trackerEngine.getSampler().setParams (index, params);

                              // Restore plugin instrument slots
                              trackerEngine.setInstrumentSlotInfos (loadedPluginSlots);

                              // Restore send effects params
                              trackerEngine.setDelayParams (loadedDelay);
                              trackerEngine.setReverbParams (loadedReverb);

                              // Restore follow mode
                              followMode = static_cast<FollowMode> (juce::jlimit (0, 2, loadedFollowMode));
                              toolbar->setFollowMode (static_cast<int> (followMode));

                              // Restore mute/solo from mixer state
                              for (int i = 0; i < kNumTracks; ++i)
                              {
                                  auto* t = trackerEngine.getTrack (i);
                                  if (t != nullptr)
                                  {
                                      t->setMute (mixerState.tracks[static_cast<size_t> (i)].muted);
                                      t->setSolo (mixerState.tracks[static_cast<size_t> (i)].soloed);
                                  }
                              }

                              // Refresh mixer plugins with loaded state
                              trackerEngine.rebuildMixerPluginChains();

                              // Invalidate track instrument cache so next sync re-loads correctly
                              trackerEngine.invalidateTrackInstruments();
                              invalidateAutomationPluginCache();

                              arrangementComponent->setSelectedEntry (arrangement.getNumEntries() > 0 ? 0 : -1);

                              trackerGrid->setCursorPosition (0, 0);
                              trackerGrid->clearSelection();
                              undoManager.clearUndoHistory();
                              currentProjectFile = file;
                              isDirty = false;
                              updateWindowTitle();
                              updateStatusBar();
                              updateToolbar();
                              updateInstrumentPanel();
                              if (mixerComponent != nullptr)
                              {
                                  mixerComponent->resized();
                                  mixerComponent->repaint();
                              }
                              fileBrowser->updateInstrumentSlots (trackerEngine.getSampler().getLoadedSamples());
                              if (automationPanelVisible)
                                  refreshAutomationPanel();

                              // Restore browser directory from project
                              if (browserDir.isNotEmpty())
                              {
                                  juce::File dir (browserDir);
                                  if (dir.isDirectory())
                                      fileBrowser->setCurrentDirectory (dir);
                              }

                              trackerGrid->repaint();
                          });
}

void MainComponent::saveProject()
{
    if (currentProjectFile.existsAsFile())
    {
        trackerEngine.snapshotInsertPluginStates();
        trackerEngine.snapshotPluginInstrumentStates();
        auto& slotInfos = trackerEngine.getAllInstrumentSlotInfos();
        auto error = ProjectSerializer::saveToFile (currentProjectFile, patternData,
                                                     trackerEngine.getBpm(),
                                                     trackerEngine.getRowsPerBeat(),
                                                     trackerEngine.getSampler().getLoadedSamples(),
                                                     trackerEngine.getSampler().getAllParams(),
                                                     arrangement,
                                                     trackLayout,
                                                     mixerState,
                                                     trackerEngine.getDelayParams(),
                                                     trackerEngine.getReverbParams(),
                                                     static_cast<int> (followMode),
                                                     fileBrowser->getCurrentDirectory().getFullPathName(),
                                                     &slotInfos);
        if (error.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Save Error", error);
        else
        {
            isDirty = false;
            updateWindowTitle();
        }
    }
    else
    {
        saveProjectAs();
    }
}

void MainComponent::saveProjectAs()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        "Save Project As",
        currentProjectFile.existsAsFile() ? currentProjectFile.getParentDirectory()
                                           : juce::File::getSpecialLocation (juce::File::userHomeDirectory),
        "*.tkadj");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (file == juce::File()) return;

                              auto f = file.withFileExtension ("tkadj");
                              trackerEngine.snapshotInsertPluginStates();
                              trackerEngine.snapshotPluginInstrumentStates();
                              auto& slotInfos = trackerEngine.getAllInstrumentSlotInfos();
                              auto error = ProjectSerializer::saveToFile (f, patternData,
                                                                          trackerEngine.getBpm(),
                                                                          trackerEngine.getRowsPerBeat(),
                                                                          trackerEngine.getSampler().getLoadedSamples(),
                                                                          trackerEngine.getSampler().getAllParams(),
                                                                          arrangement,
                                                                          trackLayout,
                                                                          mixerState,
                                                                          trackerEngine.getDelayParams(),
                                                                          trackerEngine.getReverbParams(),
                                                                          static_cast<int> (followMode),
                                                                          fileBrowser->getCurrentDirectory().getFullPathName(),
                                                                          &slotInfos);
                              if (error.isNotEmpty())
                                  juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                                          "Save Error", error);
                              else
                              {
                                  currentProjectFile = f;
                                  isDirty = false;
                                  updateWindowTitle();
                              }
                          });
}

void MainComponent::showHelpOverlay()
{
    struct HelpComponent : public juce::Component
    {
        struct Section
        {
            juce::String title;
            juce::StringArray shortcuts;
        };

        std::array<std::vector<Section>, 3> columns;

        HelpComponent()
        {
            // Column 1: Navigation + Notes
            columns[0] = {
                { "TRACKER NAVIGATION", {
                    "Arrow keys        Navigate grid",
                    "Tab / Shift+Tab   Cycle sub-columns",
                    "Fn+Up / Fn+Down   Page Up / Down",
                    "Fn+Left / Right   First / Last row",
                    "Mouse wheel       Scroll (Shift=horiz)" }},
                { "NOTE ENTRY", {
                    "Z-M, Q-U keys    Enter notes",
                    "F1-F8            Set octave 0-7",
                    "Cmd+1 to Cmd+8   Set octave 0-7",
                    "= (note col)     Note-off (OFF)",
                    "- (note col)     Note-kill (KILL)",
                    "FX: letter+2 hex  (e.g. T0C, P80)",
                    "/ or ? (FX col)   FX command list",
                    "Backspace         Clear cell" }},
                { "PLAYBACK", {
                    "Space             Play / Stop",
                    "Cmd+[ / Cmd+]     BPM down / up",
                    "Cmd+- / Cmd+=     Step down / up" }}
            };

            // Column 2: Pattern + Editing + File
            columns[1] = {
                { "PATTERN & TRACKS", {
                    "Cmd+Shift+Left   Prev pattern",
                    "Cmd+Shift+Right  Next pattern",
                    "Cmd+Opt+Shift+Right Add pattern",
                    "Ctrl+Up/Down     Semitone note",
                    "Ctrl+Left/Right  Octave note",
                    "Right-click      Generate / transpose",
                    "Cmd+Down/Up       Instrument +/-",
                    "Cmd+M             Mute track",
                    "Cmd+Shift+M       Solo track" }},
                { "EDITING", {
                    "Cmd+C / X / V     Copy / Cut / Paste",
                    "Cmd+Z             Undo",
                    "Cmd+Shift+Z / Y   Redo",
                    "Shift+Arrow       Select region" }},
                { "FILE", {
                    "Cmd+N             New project",
                    "Cmd+O             Open project",
                    "Cmd+S             Save",
                    "Cmd+Shift+S       Save As",
                    "Cmd+Shift+O       Load sample" }}
            };

            // Column 3: Tabs + Mixer + Browser + View
            columns[2] = {
                { "TABS", {
                    "Opt+Left/Right    Cycle tabs",
                    "Shift+F1          Tracker",
                    "Shift+F2          Inst Edit",
                    "Shift+F3          Inst Type",
                    "Shift+F4          Mixer",
                    "Shift+F5          Effects",
                    "Shift+F6          Browser",
                    "Escape            Return to Tracker",
                    "` (in edit tabs)  Params / Mod" }},
                { "MIXER", {
                    "Left / Right      Navigate params",
                    "Up / Down         Adjust value",
                    "Shift+Up/Down     Large adjust",
                    "Tab / Shift+Tab   Switch strip",
                    "M / S             Mute / Solo" }},
                { "VIEW", {
                    "Cmd+Shift+A       Arrangement",
                    "Cmd+Shift+I       Instruments",
                    "Cmd+Shift+P       PAT / SONG mode",
                    "Cmd+Shift+K       Metronome",
                    "Cmd+Shift+B       Automation",
                    "Cmd+/             Show this help" }}
            };
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff1e1e2e));

            auto area = getLocalBounds().reduced (16);
            int colWidth = area.getWidth() / 3;
            auto font = juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
            auto titleFont = juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold));

            for (int c = 0; c < 3; ++c)
            {
                auto colArea = area.removeFromLeft (colWidth);
                if (c < 2)
                    colArea.removeFromRight (8); // gap between columns

                int y = colArea.getY();

                for (auto& section : columns[static_cast<size_t> (c)])
                {
                    g.setFont (titleFont);
                    g.setColour (juce::Colour (0xffcba6f7));
                    g.drawText (section.title, colArea.getX(), y, colArea.getWidth(), 18,
                                juce::Justification::centredLeft);
                    y += 20;

                    g.setFont (font);
                    g.setColour (juce::Colour (0xffcdd6f4));
                    for (auto& shortcut : section.shortcuts)
                    {
                        g.drawText ("  " + shortcut, colArea.getX(), y, colArea.getWidth(), 16,
                                    juce::Justification::centredLeft);
                        y += 16;
                    }
                    y += 10; // gap between sections
                }
            }

            // Footer
            g.setFont (font);
            g.setColour (juce::Colour (0xff6c7086));
            g.drawText ("Drag audio files onto track headers to load samples.",
                        getLocalBounds().reduced (16).removeFromBottom (20),
                        juce::Justification::centred);
        }
    };

    auto* content = new HelpComponent();
    content->setSize (720, 480);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (content);
    opts.dialogTitle = "Keyboard Shortcuts";
    opts.dialogBackgroundColour = juce::Colour (0xff1e1e2e);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = false;
    opts.resizable = false;
    opts.launchAsync();
}

void MainComponent::toggleArrangementPanel()
{
    arrangementVisible = ! arrangementVisible;
    toolbar->setArrangementVisible (arrangementVisible);
    resized();

    // Restore keyboard focus to the active content component
    if (! arrangementVisible && activeTab == Tab::Tracker)
        trackerGrid->grabKeyboardFocus();
}

void MainComponent::toggleSongMode()
{
    songMode = ! songMode;
    toolbar->setPlaybackMode (songMode);

    if (trackerEngine.isPlaying())
        resyncPlaybackForCurrentMode();

    updateToolbar();
    markDirty();
}

void MainComponent::removePatternAndRepairArrangement (int index)
{
    patternData.removePattern (index);
    arrangement.remapAfterPatternRemoved (index, patternData.getNumPatterns());

    if (arrangementComponent != nullptr && arrangementComponent->getSelectedEntry() >= arrangement.getNumEntries())
        arrangementComponent->setSelectedEntry (arrangement.getNumEntries() - 1);

    if (trackerEngine.isPlaying() && songMode)
        syncArrangementToEdit();
}

int MainComponent::resolveInstrumentForTrackDrop (int track) const
{
    track = juce::jlimit (0, kNumTracks - 1, track);

    int trackInst = trackerEngine.getTrackInstrument (track);
    if (trackInst >= 0)
        return juce::jlimit (0, 255, trackInst);

    auto& pat = patternData.getCurrentPattern();
    for (int row = 0; row < pat.numRows; ++row)
    {
        int inst = pat.getCell (row, track).instrument;
        if (inst >= 0)
            return juce::jlimit (0, 255, inst);
    }

    return juce::jlimit (0, 255, track);
}

void MainComponent::resyncPlaybackForCurrentMode()
{
    if (! trackerEngine.isPlaying())
        return;

    if (songMode)
    {
        syncArrangementToEdit();
    }
    else
    {
        auto& pat = patternData.getCurrentPattern();
        trackerEngine.syncPatternToEdit (pat, getReleaseModes());
        trackerEngine.updateLoopRangeForPatternLength (pat.numRows);
    }
}

void MainComponent::syncArrangementToEdit()
{
    if (arrangement.getNumEntries() == 0)
    {
        // Fall back to current pattern
        trackerEngine.syncPatternToEdit (patternData.getCurrentPattern(), getReleaseModes());
        return;
    }

    // Build sequence of (pattern*, repeats) pairs
    std::vector<std::pair<const Pattern*, int>> sequence;
    for (auto& entry : arrangement.getEntries())
    {
        if (entry.patternIndex >= 0 && entry.patternIndex < patternData.getNumPatterns())
            sequence.emplace_back (&patternData.getPattern (entry.patternIndex), entry.repeats);
    }

    if (sequence.empty())
    {
        trackerEngine.syncPatternToEdit (patternData.getCurrentPattern(), getReleaseModes());
        return;
    }

    trackerEngine.syncArrangementToEdit (sequence, trackerEngine.getRowsPerBeat(), getReleaseModes());
}

void MainComponent::applyAutomationAtPlaybackPosition (int playPatternIndex, int playRow)
{
    if (playPatternIndex < 0 || playPatternIndex >= patternData.getNumPatterns() || playRow < 0)
        return;

    const auto& automationData = patternData.getPattern (playPatternIndex).getAutomationData();
    juce::String recordingPluginId;
    int recordingParamIdx = -1;
    if (automationPanelVisible && automationPanel != nullptr && automationPanel->isRecording())
    {
        recordingPluginId = automationPanel->getSelectedPluginId();
        recordingParamIdx = automationPanel->getSelectedParameterIndex();
    }

    trackerEngine.applyAutomationForPlaybackRow (automationData, playRow, recordingPluginId, recordingParamIdx);
}

void MainComponent::doCopy()
{
    auto& pat = patternData.getCurrentPattern();
    auto& clip = getClipboard();

    if (trackerGrid->hasSelection)
    {
        // Selection bounds are in visual space — copy visual columns
        int minRow, maxRow, minViTrack, maxViTrack;
        trackerGrid->getSelectionBounds (minRow, maxRow, minViTrack, maxViTrack);
        int copyStartVi = juce::jmax (0, minViTrack);
        int copyEndVi = juce::jmin (trackLayout.getTrackLaneCount() - 1, maxViTrack);
        if (copyStartVi > copyEndVi)
        {
            clip.numRows = 0;
            clip.numTracks = 0;
            clip.cells.clear();
            return;
        }
        clip.numRows = maxRow - minRow + 1;
        clip.numTracks = copyEndVi - copyStartVi + 1;
        clip.cells.resize (static_cast<size_t> (clip.numRows));
        for (int r = 0; r < clip.numRows; ++r)
        {
            clip.cells[static_cast<size_t> (r)].resize (static_cast<size_t> (clip.numTracks));
            for (int t = 0; t < clip.numTracks; ++t)
            {
                int phys = trackLayout.visualToPhysical (copyStartVi + t);
                clip.cells[static_cast<size_t> (r)][static_cast<size_t> (t)] =
                    pat.getCell (minRow + r, phys);
            }
        }
    }
    else
    {
        if (trackerGrid->isCursorInMasterLane())
        {
            clip.numRows = 0;
            clip.numTracks = 0;
            clip.cells.clear();
            return;
        }

        // Copy single cell at cursor
        clip.copyFromPattern (pat, trackerGrid->getCursorRow(), trackerGrid->getCursorRow(),
                              trackerGrid->getCursorTrack(), trackerGrid->getCursorTrack());
    }
}

void MainComponent::doPaste()
{
    auto& clip = getClipboard();
    if (clip.isEmpty()) return;
    if (trackerGrid->isCursorInMasterLane()) return;

    auto& pat = patternData.getCurrentPattern();
    int destRow = trackerGrid->getCursorRow();
    int destViTrack = trackLayout.physicalToVisual (trackerGrid->getCursorTrack());

    // Build undo records — paste to visual columns
    std::vector<MultiCellEditAction::CellRecord> records;
    for (int r = 0; r < clip.numRows; ++r)
    {
        int row = destRow + r;
        if (row >= pat.numRows) break;
        for (int t = 0; t < clip.numTracks; ++t)
        {
            int vi = destViTrack + t;
            if (vi >= trackLayout.getTrackLaneCount()) break;
            int phys = trackLayout.visualToPhysical (vi);
            MultiCellEditAction::CellRecord rec;
            rec.row = row;
            rec.track = phys;
            rec.oldCell = pat.getCell (row, phys);
            rec.newCell = clip.cells[static_cast<size_t> (r)][static_cast<size_t> (t)];
            records.push_back (rec);
        }
    }

    if (! records.empty())
    {
        undoManager.perform (new MultiCellEditAction (patternData, patternData.getCurrentPatternIndex(), std::move (records)));
        if (trackerGrid->onPatternDataChanged)
            trackerGrid->onPatternDataChanged();
        commandManager.commandStatusChanged();
    }
    trackerGrid->repaint();
}

void MainComponent::doCut()
{
    doCopy();

    auto& pat = patternData.getCurrentPattern();
    int patIdx = patternData.getCurrentPatternIndex();
    std::vector<MultiCellEditAction::CellRecord> cellRecords;
    std::vector<MultiCellEditAction::MasterFxRecord> masterFxRecords;

    auto sameFx = [] (const FxSlot& a, const FxSlot& b)
    {
        return a.fx == b.fx && a.fxParam == b.fxParam && a.fxCommand == b.fxCommand;
    };

    auto sameCell = [&sameFx] (const Cell& a, const Cell& b)
    {
        if (a.note != b.note || a.instrument != b.instrument || a.volume != b.volume)
            return false;
        if (a.fxSlots.size() != b.fxSlots.size())
            return false;
        for (size_t i = 0; i < a.fxSlots.size(); ++i)
            if (! sameFx (a.fxSlots[i], b.fxSlots[i]))
                return false;
        return true;
    };

    if (trackerGrid->hasSelection)
    {
        // Selection bounds are in visual space
        int minRow, maxRow, minViTrack, maxViTrack;
        trackerGrid->getSelectionBounds (minRow, maxRow, minViTrack, maxViTrack);

        for (int r = minRow; r <= maxRow; ++r)
        {
            for (int vi = minViTrack; vi <= maxViTrack; ++vi)
            {
                if (vi == trackLayout.getTrackLaneCount())
                {
                    for (int lane = 0; lane < trackLayout.getMasterFxLaneCount(); ++lane)
                    {
                        auto oldSlot = pat.getMasterFxSlot (r, lane);
                        auto newSlot = oldSlot;
                        newSlot.clear();
                        if (! sameFx (oldSlot, newSlot))
                        masterFxRecords.push_back ({ r, lane, oldSlot, newSlot });
                    }
                }
                else if (vi >= 0 && vi < trackLayout.getTrackLaneCount())
                {
                    int phys = trackLayout.visualToPhysical (vi);
                    MultiCellEditAction::CellRecord rec;
                    rec.row = r;
                    rec.track = phys;
                    rec.oldCell = pat.getCell (r, phys);
                    rec.newCell = Cell{}; // cleared
                    if (! sameCell (rec.oldCell, rec.newCell))
                        cellRecords.push_back (rec);
                }
            }
        }
    }
    else
    {
        int r = trackerGrid->getCursorRow();
        int t = trackerGrid->getCursorTrack();
        if (t >= kNumTracks)
        {
            int lane = trackerGrid->getCursorFxLane();
            auto oldSlot = pat.getMasterFxSlot (r, lane);
            auto newSlot = oldSlot;
            newSlot.clear();
            if (! sameFx (oldSlot, newSlot))
                masterFxRecords.push_back ({ r, lane, oldSlot, newSlot });
        }
        else
        {
            MultiCellEditAction::CellRecord rec;
            rec.row = r;
            rec.track = t;
            rec.oldCell = pat.getCell (r, t);
            rec.newCell = Cell{};
            if (! sameCell (rec.oldCell, rec.newCell))
                cellRecords.push_back (rec);
        }
    }

    if (! cellRecords.empty() || ! masterFxRecords.empty())
    {
        undoManager.perform (new MultiCellEditAction (patternData, patIdx,
                                                      std::move (cellRecords),
                                                      std::move (masterFxRecords)));
        if (trackerGrid->onPatternDataChanged)
            trackerGrid->onPatternDataChanged();
        commandManager.commandStatusChanged();
    }

    if (trackerGrid->hasSelection)
        trackerGrid->clearSelection();

    trackerGrid->repaint();
}

void MainComponent::updateInstrumentPanel()
{
    auto loadedSamples = trackerEngine.getSampler().getLoadedSamples();
    auto& pluginSlotInfos = trackerEngine.getAllInstrumentSlotInfos();

    instrumentPanel->updateSampleInfo (loadedSamples);
    instrumentPanel->updatePluginInfo (pluginSlotInfos);
    instrumentPanel->setSelectedInstrument (trackerGrid->getCurrentInstrument());

    // Keep file browser in sync with plugin instrument state
    fileBrowser->updatePluginSlots (pluginSlotInfos);

    updateTrackSampleMarkers();
}

void MainComponent::updateTrackSampleMarkers()
{
    auto loadedSamples = trackerEngine.getSampler().getLoadedSamples();
    for (int i = 0; i < kNumTracks; ++i)
    {
        bool hasSample = false;
        int trackInst = trackerEngine.getTrackInstrument (i);
        if (trackInst >= 0)
            hasSample = loadedSamples.find (trackInst) != loadedSamples.end();
        else
            hasSample = loadedSamples.find (i) != loadedSamples.end();
        trackerGrid->trackHasSample[static_cast<size_t> (i)] = hasSample;
    }
    trackerGrid->repaint();
}

void MainComponent::loadSampleForInstrument (int instrument)
{
    auto chooser = std::make_shared<juce::FileChooser> (
        "Load Sample for Instrument " + juce::String::formatted ("%02X", instrument),
        juce::File::getSpecialLocation (juce::File::userHomeDirectory),
        "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser, instrument] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (file.existsAsFile())
                              {
                                  auto error = trackerEngine.loadSampleForInstrument (instrument, file);
                                  if (error.isNotEmpty())
                                      juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                                              "Load Error", error);
                                  else
                                  {
                                      if (trackerEngine.isPlaying())
                                      {
                                          if (songMode)
                                              syncArrangementToEdit();
                                          else
                                              trackerEngine.refreshTracksForInstrument (instrument, patternData.getCurrentPattern());
                                      }

                                      trackerGrid->repaint();
                                      updateToolbar();
                                      updateInstrumentPanel();
                                      fileBrowser->updateInstrumentSlots (trackerEngine.getSampler().getLoadedSamples());
                                      markDirty();
                                  }
                              }
                          });
}

void MainComponent::clearSampleForInstrument (int instrument)
{
    trackerEngine.clearSampleForInstrument (instrument);
    trackerEngine.invalidateTrackInstruments();

    if (trackerEngine.isPlaying())
        resyncPlaybackForCurrentMode();

    if (trackerGrid->getCurrentInstrument() == instrument)
        updateSampleEditorForCurrentInstrument();

    updateToolbar();
    updateInstrumentPanel();
    fileBrowser->updateInstrumentSlots (trackerEngine.getSampler().getLoadedSamples());
    markDirty();
}

void MainComponent::updateSampleEditorForCurrentInstrument()
{
    int inst = trackerGrid->getCurrentInstrument();

    // Check if this is a plugin instrument
    if (trackerEngine.isPluginInstrument (inst))
    {
        auto& info = trackerEngine.getInstrumentSlotInfo (inst);
        sampleEditor->setPluginInstrument (inst, info.pluginDescription.name, info.ownerTrack);
        return;
    }

    auto sampleFile = trackerEngine.getSampler().getSampleFile (inst);
    auto params = trackerEngine.getSampler().getParams (inst);

    if (sampleFile.existsAsFile())
        sampleEditor->setInstrument (inst, sampleFile, params);
    else
        sampleEditor->setInstrument (inst, juce::File(), params);
}

std::array<bool, kNumTracks> MainComponent::getReleaseModes() const
{
    std::array<bool, kNumTracks> modes {};
    for (int i = 0; i < kNumTracks; ++i)
        modes[static_cast<size_t> (i)] = (trackLayout.getTrackNoteMode (i) == NoteMode::Release);
    return modes;
}

void MainComponent::cycleTab (int direction)
{
    static constexpr Tab allTabs[] = {
        Tab::Tracker, Tab::InstrumentEdit, Tab::InstrumentType,
        Tab::Mixer, Tab::Effects, Tab::Browser
    };
    static constexpr int numTabs = 6;

    int current = 0;
    for (int i = 0; i < numTabs; ++i)
        if (allTabs[i] == activeTab) { current = i; break; }

    int next = ((current + direction) % numTabs + numTabs) % numTabs;
    switchToTab (allTabs[next]);
}

void MainComponent::switchToTab (Tab tab)
{
    if (activeTab == tab)
    {
        focusContentForTab (tab);
        return;
    }

    // Stop file preview when leaving browser tab
    if (activeTab == Tab::Browser)
        trackerEngine.stopPreview();

    activeTab = tab;
    tabBar->setActiveTab (tab);

    // Refresh browser data when switching to it
    if (tab == Tab::Browser)
    {
        fileBrowser->updateInstrumentSlots (trackerEngine.getSampler().getLoadedSamples());
        fileBrowser->setSelectedInstrument (trackerGrid->getCurrentInstrument());
    }

    // Update instrument panel and editor when switching to edit/type tabs
    if (tab == Tab::InstrumentEdit || tab == Tab::InstrumentType)
    {
        updateInstrumentPanel();
        updateSampleEditorForCurrentInstrument();
        sampleEditor->setOctave (trackerGrid->getOctave());
    }

    // Sync mute/solo state when switching to mixer
    if (tab == Tab::Mixer)
    {
        for (int i = 0; i < kNumTracks; ++i)
        {
            auto* t = trackerEngine.getTrack (i);
            if (t != nullptr)
            {
                mixerComponent->setTrackMuteState (i, t->isMuted (false));
                mixerComponent->setTrackSoloState (i, t->isSolo (false));
            }
        }
    }

    resized();

    // Refresh effects params when switching to effects tab
    if (tab == Tab::Effects)
    {
        sendEffectsComponent->setDelayParams (trackerEngine.getDelayParams());
        sendEffectsComponent->setReverbParams (trackerEngine.getReverbParams());
    }

    // Refresh automation panel when returning to tracker tab
    if (tab == Tab::Tracker && automationPanelVisible)
        refreshAutomationPanel();

    focusContentForTab (tab);
}

void MainComponent::focusContentForTab (Tab tab)
{
    switch (tab)
    {
        case Tab::Tracker:
            trackerGrid->grabKeyboardFocus();
            break;
        case Tab::InstrumentEdit:
        case Tab::InstrumentType:
            sampleEditor->grabKeyboardFocus();
            break;
        case Tab::Mixer:
            mixerComponent->grabKeyboardFocus();
            break;
        case Tab::Effects:
            sendEffectsComponent->grabKeyboardFocus();
            break;
        case Tab::Browser:
            fileBrowser->grabKeyboardFocus();
            break;
    }
}

void MainComponent::updateMuteSoloState()
{
    for (int i = 0; i < kNumTracks; ++i)
    {
        auto* t = trackerEngine.getTrack (i);
        if (t != nullptr)
        {
            bool muted = t->isMuted (false);
            bool soloed = t->isSolo (false);
            trackerGrid->trackMuted[static_cast<size_t> (i)] = muted;
            trackerGrid->trackSoloed[static_cast<size_t> (i)] = soloed;
            mixerComponent->setTrackMuteState (i, muted);
            mixerComponent->setTrackSoloState (i, soloed);
            mixerState.tracks[static_cast<size_t> (i)].muted = muted;
            mixerState.tracks[static_cast<size_t> (i)].soloed = soloed;
        }
    }
    trackerGrid->repaint();
    if (activeTab == Tab::Mixer)
        mixerComponent->repaint();
}

void MainComponent::showAudioPluginSettings()
{
    auto* content = new AudioPluginSettingsComponent (trackerEngine.getEngine(),
                                                      trackerEngine.getPluginCatalog(),
                                                      trackerLookAndFeel);

    // Load persisted scan paths, or use defaults if none saved
    auto savedPaths = ProjectSerializer::loadGlobalPluginScanPaths();
    if (savedPaths.isEmpty())
        savedPaths = PluginCatalogService::getDefaultScanPaths();

    content->setScanPaths (savedPaths);

    // Persist scan paths when they change
    content->onScanPathsChanged = [] (const juce::StringArray& paths)
    {
        ProjectSerializer::saveGlobalPluginScanPaths (paths);
    };

    content->setSize (AudioPluginSettingsComponent::kPreferredWidth,
                      AudioPluginSettingsComponent::kPreferredHeight);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (content);
    opts.dialogTitle = "Audio & Plugin Settings";
    opts.dialogBackgroundColour = juce::Colour (0xff1e1e2e);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = false;
    opts.resizable = true;
    opts.launchAsync();
}

void MainComponent::refreshAutomationPanel (bool forcePopulate)
{
    if (automationPanel == nullptr)
        return;

    auto& pat = patternData.getCurrentPattern();
    automationPanel->setAutomationData (&pat.getAutomationData());
    automationPanel->setPatternLength (pat.numRows);

    int curTrack = trackerGrid->getCursorTrack();
    automationPanel->setCurrentTrack (curTrack);

    // Only re-enumerate plugin parameters when the track actually changes
    // (or when explicitly forced).
    if (forcePopulate || curTrack != lastAutomationPopulateTrack)
    {
        // Save selection for the track we're leaving
        if (lastAutomationPopulateTrack >= 0)
            saveAutomationSelection();
        lastAutomationPopulateTrack = curTrack;
        populateAutomationPlugins();
        restoreAutomationSelection (curTrack);
    }
    else
    {
        automationPanel->repaint();
    }
}

void MainComponent::invalidateAutomationPluginCache (int trackIndex)
{
    if (trackIndex < 0)
        automationPluginCache.clear();
    else
        automationPluginCache.erase (trackIndex);
    lastAutomationPopulateTrack = -1;
}

void MainComponent::saveAutomationSelection()
{
    if (automationPanel == nullptr || lastAutomationPopulateTrack < 0)
        return;
    auto pluginId = automationPanel->getSelectedPluginId();
    auto paramIdx = automationPanel->getSelectedParameterIndex();
    if (pluginId.isNotEmpty())
        automationSelectionPerTrack[lastAutomationPopulateTrack] = { pluginId, paramIdx };
}

void MainComponent::restoreAutomationSelection (int trackIndex)
{
    if (automationPanel == nullptr)
        return;
    auto it = automationSelectionPerTrack.find (trackIndex);
    if (it != automationSelectionPerTrack.end())
        automationPanel->navigateToParam (it->second.first, it->second.second);
}

void MainComponent::populateAutomationPlugins()
{
    if (automationPanel == nullptr)
        return;

    int cursorTrack = trackerGrid->getCursorTrack();
    if (cursorTrack >= kNumTracks)
        cursorTrack = 0;

    // Return cached result if available — avoids expensive getName
    // on every parameter each time the user switches to this track.
    // Refresh hasAutomation flags from current pattern since automation
    // data may have changed since the cache was built.
    auto cacheIt = automationPluginCache.find (cursorTrack);
    if (cacheIt != automationPluginCache.end())
    {
        auto& cachedPlugins = cacheIt->second;
        const auto& cachedAutoData = patternData.getCurrentPattern().getAutomationData();
        for (auto& plugin : cachedPlugins)
            for (auto& param : plugin.parameters)
                param.hasAutomation = cachedAutoData.findLane (plugin.pluginId, param.index) != nullptr;
        automationPanel->setAvailablePlugins (cachedPlugins);
        return;
    }

    // Get current automation data so we can mark already-automated parameters.
    auto& pat = patternData.getCurrentPattern();
    const auto& automationData = pat.getAutomationData();

    // Helper: collect parameters from a JUCE AudioPluginInstance.
    // getName() is safe without the callback lock — it just reads stored strings,
    // not audio-thread state.  Removing tryEnter from this path eliminates the
    // main source of latency (lock contention with the always-live audio graph).
    auto collectParams = [&] (juce::AudioPluginInstance* audioPlugin,
                              const juce::String& pluginId,
                              AutomatablePluginInfo& pluginInfo)
    {
        if (audioPlugin == nullptr)
            return;

        auto& params = audioPlugin->getParameters();
        pluginInfo.parameters.reserve (static_cast<size_t> (params.size()));
        for (int pi = 0; pi < params.size(); ++pi)
        {
            AutomatablePluginInfo::ParamInfo paramInfo;
            paramInfo.index = pi;
            paramInfo.name = params[pi]->getName (40);
            paramInfo.hasAutomation = automationData.findLane (pluginId, pi) != nullptr;
            pluginInfo.parameters.push_back (paramInfo);
        }
    };

    std::vector<AutomatablePluginInfo> plugins;

    // 1. Add instrument plugins owned by the current track
    for (const auto& [instIdx, info] : trackerEngine.getAllInstrumentSlotInfos())
    {
        if (info.isPlugin() && info.ownerTrack == cursorTrack)
        {
            AutomatablePluginInfo pluginInfo;
            pluginInfo.pluginId = "inst:" + juce::String (instIdx);
            pluginInfo.displayName = info.pluginDescription.name + " (Inst " + juce::String (instIdx) + ")";
            pluginInfo.owningTrack = cursorTrack;
            pluginInfo.isInstrument = true;

            if (auto* pluginInstance = trackerEngine.getPluginInstrumentInstance (instIdx))
            {
                if (auto* external = dynamic_cast<te::ExternalPlugin*> (pluginInstance))
                    collectParams (external->getAudioPluginInstance(), pluginInfo.pluginId, pluginInfo);
            }

            if (! pluginInfo.parameters.empty())
                plugins.push_back (std::move (pluginInfo));
        }
    }

    // 2. Add insert plugins on the current track
    if (trackerEngine.getTrack (cursorTrack) != nullptr)
    {
        auto& insertSlots = mixerState.insertSlots[static_cast<size_t> (cursorTrack)];
        for (int si = 0; si < static_cast<int> (insertSlots.size()); ++si)
        {
            auto& slot = insertSlots[static_cast<size_t> (si)];
            if (slot.isEmpty())
                continue;

            AutomatablePluginInfo pluginInfo;
            pluginInfo.pluginId = "insert:" + juce::String (cursorTrack) + ":" + juce::String (si);
            pluginInfo.displayName = slot.pluginName + " (Insert " + juce::String (si + 1) + ")";
            pluginInfo.owningTrack = cursorTrack;
            pluginInfo.isInstrument = false;

            if (auto* plugin = trackerEngine.getInsertPlugin (cursorTrack, si))
            {
                if (auto* external = dynamic_cast<te::ExternalPlugin*> (plugin))
                    collectParams (external->getAudioPluginInstance(), pluginInfo.pluginId, pluginInfo);
            }

            if (! pluginInfo.parameters.empty())
                plugins.push_back (std::move (pluginInfo));
        }
    }

    // Sort parameters: already-automated first (marked with *), then by
    // relevance tier (common automation targets), then alphabetical.
    // Uses fast containsIgnoreCase instead of regex for speed.
    auto getParamPriority = [] (const juce::String& name) -> int
    {
        auto lo = name.toLowerCase();
        if (lo.contains ("macro") || lo.contains ("mmod"))                       return 0;
        if (lo.contains ("cutoff") || (lo.contains ("filter") && lo.contains ("freq"))) return 1;
        if (lo.contains ("reson") || lo.contains ("emphasis"))                   return 2;
        if (lo.contains ("volume") || lo.contains ("gain") || lo.contains ("level")
            || lo.contains ("amplitude") || lo.contains ("output"))              return 3;
        if (lo.contains ("mix") || lo.contains ("dry") || lo.contains ("wet")
            || lo.contains ("blend"))                                            return 4;
        if (lo.contains ("pan") || lo.contains ("balance") || lo.contains ("width")
            || lo.contains ("stereo") || lo.contains ("spread"))                 return 5;
        if (lo.contains ("lfo") && (lo.contains ("rate") || lo.contains ("speed"))) return 6;
        if (lo.contains ("attack") || lo.contains ("decay") || lo.contains ("sustain")
            || lo.contains ("release") || lo.contains ("adsr"))                  return 7;
        if (lo.contains ("pitch") || lo.contains ("tune") || lo.contains ("detune")
            || lo.contains ("semi") || lo.contains ("coarse") || lo.contains ("fine")
            || lo.contains ("transpose") || lo.contains ("cent"))                return 8;
        if (lo.contains ("drive") || lo.contains ("distort") || lo.contains ("saturat")
            || lo.contains ("overdrive"))                                        return 9;
        if (lo.contains ("feedback") || (lo.contains ("delay") && lo.contains ("time"))) return 10;
        if (lo.contains ("reverb") || lo.contains ("room") || lo.contains ("damping")) return 11;
        if (lo.contains ("chorus") || lo.contains ("flanger") || lo.contains ("phaser")) return 12;
        return 99;
    };

    for (auto& pluginInfo : plugins)
    {
        std::stable_sort (pluginInfo.parameters.begin(), pluginInfo.parameters.end(),
                          [&] (const AutomatablePluginInfo::ParamInfo& a,
                               const AutomatablePluginInfo::ParamInfo& b)
                          {
                              // Already-automated params always come first
                              if (a.hasAutomation != b.hasAutomation)
                                  return a.hasAutomation;
                              int pa = getParamPriority (a.name);
                              int pb = getParamPriority (b.name);
                              if (pa != pb) return pa < pb;
                              return a.name.compareIgnoreCase (b.name) < 0;
                          });
    }

    // Cache the result so subsequent visits to this track are instant.
    automationPluginCache[cursorTrack] = plugins;
    automationPanel->setAvailablePlugins (plugins);
}

void MainComponent::navigateToAutomationParam (const juce::String& pluginId, int paramIndex)
{
    // Show automation panel if hidden
    if (! automationPanelVisible)
    {
        automationPanelVisible = true;
        toolbar->setAutomationPanelVisible (true);
        if (automationPanel != nullptr)
        {
            auto& pat = patternData.getCurrentPattern();
            automationPanel->setAutomationData (&pat.getAutomationData());
            automationPanel->setPatternLength (pat.numRows);
            automationPanel->setCurrentTrack (trackerGrid->getCursorTrack());

            // Lightweight population for auto-learn navigation: avoid expensive
            // full plugin scans and parameter-name queries while a plugin UI is active.
            std::vector<AutomatablePluginInfo> plugins;
            AutomatablePluginInfo pluginInfo;
            pluginInfo.pluginId = pluginId;
            pluginInfo.owningTrack = trackerGrid->getCursorTrack();

            if (pluginId.startsWith ("inst:"))
            {
                int instIdx = pluginId.substring (5).getIntValue();
                const auto& slotInfo = trackerEngine.getInstrumentSlotInfo (instIdx);
                pluginInfo.owningTrack = slotInfo.ownerTrack;
                pluginInfo.isInstrument = true;
                auto name = slotInfo.pluginDescription.name;
                if (name.isEmpty())
                    name = "Instrument";
                pluginInfo.displayName = name + " (Inst " + juce::String (instIdx) + ")";
            }
            else if (pluginId.startsWith ("insert:"))
            {
                auto parts = juce::StringArray::fromTokens (pluginId.substring (7), ":", "");
                int trackIdx = parts.size() > 0 ? parts[0].getIntValue() : trackerGrid->getCursorTrack();
                int slotIdx = parts.size() > 1 ? parts[1].getIntValue() : 0;
                pluginInfo.owningTrack = trackIdx;
                pluginInfo.isInstrument = false;

                juce::String name = "Insert";
                if (trackIdx >= 0 && trackIdx < static_cast<int> (mixerState.insertSlots.size()))
                {
                    auto& slots = mixerState.insertSlots[static_cast<size_t> (trackIdx)];
                    if (slotIdx >= 0 && slotIdx < static_cast<int> (slots.size())
                        && slots[static_cast<size_t> (slotIdx)].pluginName.isNotEmpty())
                    {
                        name = slots[static_cast<size_t> (slotIdx)].pluginName;
                    }
                }
                pluginInfo.displayName = name + " (Insert " + juce::String (slotIdx + 1) + ")";
            }
            else
            {
                pluginInfo.displayName = "Plugin";
            }

            if (paramIndex >= 0)
            {
                AutomatablePluginInfo::ParamInfo paramInfo;
                paramInfo.index = paramIndex;

                // Get the real parameter name from the plugin instance.
                // getName() is safe without the callback lock.
                juce::String realName;
                auto* resolvedPlugin = trackerEngine.resolvePluginInstance (pluginId);
                if (resolvedPlugin != nullptr)
                {
                    auto& params = resolvedPlugin->getParameters();
                    if (paramIndex < params.size() && params[paramIndex] != nullptr)
                        realName = params[paramIndex]->getName (40);
                }
                paramInfo.name = realName.isNotEmpty() ? realName
                                                       : ("Param " + juce::String (paramIndex));
                pluginInfo.parameters.push_back (paramInfo);
            }

            // Invalidate cache for this track so the next full populate
            // picks up the newly automated parameter correctly.
            invalidateAutomationPluginCache (pluginInfo.owningTrack);

            if (! pluginInfo.parameters.empty())
            {
                plugins.push_back (std::move (pluginInfo));
                automationPanel->setAvailablePlugins (plugins);
            }
            else
            {
                populateAutomationPlugins();
            }
        }
        resized();
    }

    // Navigate to the specified param
    if (automationPanel != nullptr)
        automationPanel->navigateToParam (pluginId, paramIndex);
}
