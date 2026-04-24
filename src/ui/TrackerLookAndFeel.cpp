#include "TrackerLookAndFeel.h"

#include <cmath>
#include <vector>

namespace
{
struct ColourSchemeDefinition
{
    juce::String name;
    juce::Colour background;
    juce::Colour gridLine;
    juce::Colour text;
    juce::Colour cursorRow;
    juce::Colour cursorCell;
    juce::Colour beatMarker;
    juce::Colour header;
    juce::Colour playbackCursor;
    juce::Colour note;
    juce::Colour instrument;
    juce::Colour volume;
    juce::Colour fx;
    juce::Colour selection;
    juce::Colour mute;
    juce::Colour solo;
    juce::Colour groupHeader;
    std::vector<juce::Colour> instrumentPalette;
};

const std::vector<ColourSchemeDefinition>& getColourSchemeDefinitions()
{
    static const std::vector<ColourSchemeDefinition> schemes {
        {
            "Classic",
            juce::Colour (0xff1a1a1a),
            juce::Colour (0xff333333),
            juce::Colour (0xffcccccc),
            juce::Colour (0xff1e3a5a),
            juce::Colour (0xff2a4a6a),
            juce::Colour (0xff222222),
            juce::Colour (0xff252525),
            juce::Colour (0xff4a6a2a),
            juce::Colour (0xffcccccc),
            juce::Colour (0xffd4a843),
            juce::Colour (0xff5cba5c),
            juce::Colour (0xff5c8abf),
            juce::Colour (0x44ffffff),
            juce::Colour (0xffcc4444),
            juce::Colour (0xffd4d444),
            juce::Colour (0xff383848),
            {
                juce::Colour (0xffd4a843), juce::Colour (0xff5c8abf),
                juce::Colour (0xff5cba5c), juce::Colour (0xffcc6a6a),
                juce::Colour (0xffa178c8), juce::Colour (0xffd4d444),
                juce::Colour (0xff56b3a5), juce::Colour (0xffc9845f)
            }
        },
        {
            "Calm",
            juce::Colour (0xff181a1d),
            juce::Colour (0xff30343a),
            juce::Colour (0xffd0d3d7),
            juce::Colour (0xff203443),
            juce::Colour (0xff284959),
            juce::Colour (0xff23272d),
            juce::Colour (0xff24282e),
            juce::Colour (0xff2f4738),
            juce::Colour (0xffd0d3d7),
            juce::Colour (0xffd8b56a),
            juce::Colour (0xff78b986),
            juce::Colour (0xff78a6d8),
            juce::Colour (0x3fffffff),
            juce::Colour (0xffd16a6a),
            juce::Colour (0xffd5c36a),
            juce::Colour (0xff343844),
            {
                juce::Colour (0xff7aa6dc), juce::Colour (0xffd69d5c),
                juce::Colour (0xff74b38a), juce::Colour (0xffc783a7),
                juce::Colour (0xffd5c96a), juce::Colour (0xff6fb5b0),
                juce::Colour (0xffa18bd0), juce::Colour (0xffcd7d6f),
                juce::Colour (0xff8dbd69), juce::Colour (0xffc4956d),
                juce::Colour (0xff68a0b7), juce::Colour (0xffb5b0a1)
            }
        },
        {
            "Solarized",
            juce::Colour (0xff002b36),
            juce::Colour (0xff0b3d49),
            juce::Colour (0xff93a1a1),
            juce::Colour (0xff073642),
            juce::Colour (0xff164b59),
            juce::Colour (0xff07313d),
            juce::Colour (0xff073642),
            juce::Colour (0xff31533c),
            juce::Colour (0xffeee8d5),
            juce::Colour (0xffb58900),
            juce::Colour (0xff859900),
            juce::Colour (0xff268bd2),
            juce::Colour (0x44eee8d5),
            juce::Colour (0xffdc322f),
            juce::Colour (0xffb58900),
            juce::Colour (0xff123d48),
            {
                juce::Colour (0xff268bd2), juce::Colour (0xff2aa198),
                juce::Colour (0xff859900), juce::Colour (0xffb58900),
                juce::Colour (0xffcb4b16), juce::Colour (0xffdc322f),
                juce::Colour (0xffd33682), juce::Colour (0xff6c71c4)
            }
        },
        {
            "Nord",
            juce::Colour (0xff222832),
            juce::Colour (0xff3b4352),
            juce::Colour (0xffd8dee9),
            juce::Colour (0xff2d4155),
            juce::Colour (0xff34516a),
            juce::Colour (0xff2a313d),
            juce::Colour (0xff2e3440),
            juce::Colour (0xff3f5643),
            juce::Colour (0xffeceff4),
            juce::Colour (0xffebcb8b),
            juce::Colour (0xffa3be8c),
            juce::Colour (0xff88c0d0),
            juce::Colour (0x44eceff4),
            juce::Colour (0xffbf616a),
            juce::Colour (0xffebcb8b),
            juce::Colour (0xff3b4252),
            {
                juce::Colour (0xff88c0d0), juce::Colour (0xff81a1c1),
                juce::Colour (0xff8fbcbb), juce::Colour (0xffa3be8c),
                juce::Colour (0xffebcb8b), juce::Colour (0xffd08770),
                juce::Colour (0xffb48ead), juce::Colour (0xffbf616a),
                juce::Colour (0xff5e81ac), juce::Colour (0xffc0a36e)
            }
        }
    };

    return schemes;
}
}

TrackerLookAndFeel::TrackerLookAndFeel()
{
    setColourScheme (getDefaultColourSchemeIndex());
}

int TrackerLookAndFeel::getDefaultColourSchemeIndex()
{
    return 1; // Calm
}

int TrackerLookAndFeel::getColourSchemeCount()
{
    return static_cast<int> (getColourSchemeDefinitions().size());
}

juce::String TrackerLookAndFeel::getColourSchemeName (int schemeIndex)
{
    const auto& schemes = getColourSchemeDefinitions();
    return schemes[static_cast<size_t> (clampColourSchemeIndex (schemeIndex))].name;
}

int TrackerLookAndFeel::clampColourSchemeIndex (int schemeIndex)
{
    return juce::jlimit (0, getColourSchemeCount() - 1, schemeIndex);
}

juce::Colour TrackerLookAndFeel::getInstrumentColourForScheme (int schemeIndex, int instrument)
{
    const auto& schemes = getColourSchemeDefinitions();
    const auto& palette = schemes[static_cast<size_t> (clampColourSchemeIndex (schemeIndex))].instrumentPalette;
    if (palette.empty())
        return juce::Colour (0xffd8b56a);

    const int safeInstrument = juce::jlimit (0, 255, instrument);
    const int paletteIndex = safeInstrument % static_cast<int> (palette.size());
    const int cycle = safeInstrument / static_cast<int> (palette.size());
    auto base = palette[static_cast<size_t> (paletteIndex)];

    if (cycle == 0)
        return base;

    const float hue = std::fmod (base.getHue() + 0.018f * static_cast<float> (cycle % 5), 1.0f);
    const float saturation = juce::jlimit (0.42f, 0.82f,
                                           base.getSaturation() * (1.0f - 0.045f * static_cast<float> (cycle % 4)));
    const float brightness = juce::jlimit (0.58f, 0.95f,
                                           base.getBrightness() * (cycle % 2 == 0 ? 0.92f : 1.04f));
    return juce::Colour::fromHSV (hue, saturation, brightness, 1.0f);
}

void TrackerLookAndFeel::setColourScheme (int schemeIndex)
{
    const auto& schemes = getColourSchemeDefinitions();
    colourSchemeIndex = clampColourSchemeIndex (schemeIndex);
    const auto& scheme = schemes[static_cast<size_t> (colourSchemeIndex)];

    setColour (juce::ResizableWindow::backgroundColourId, scheme.background);
    setColour (juce::Label::textColourId, scheme.text);

    setColour (backgroundColourId,     scheme.background);
    setColour (gridLineColourId,       scheme.gridLine);
    setColour (textColourId,           scheme.text);
    setColour (cursorRowColourId,      scheme.cursorRow);
    setColour (cursorCellColourId,     scheme.cursorCell);
    setColour (beatMarkerColourId,     scheme.beatMarker);
    setColour (headerColourId,         scheme.header);
    setColour (playbackCursorColourId, scheme.playbackCursor);
    setColour (noteColourId,           scheme.note);
    setColour (instrumentColourId,     scheme.instrument);
    setColour (volumeColourId,         scheme.volume);
    setColour (fxColourId,             scheme.fx);
    setColour (selectionColourId,      scheme.selection);
    setColour (muteColourId,           scheme.mute);
    setColour (soloColourId,           scheme.solo);
    setColour (groupHeaderColourId,    scheme.groupHeader);
}

juce::Colour TrackerLookAndFeel::getInstrumentColour (int instrument) const
{
    return getInstrumentColourForScheme (colourSchemeIndex, instrument);
}

juce::Font TrackerLookAndFeel::getMonoFont (float height) const
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
}

juce::Font TrackerLookAndFeel::getUIFont (float height, int styleFlags) const
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), height, styleFlags));
}
