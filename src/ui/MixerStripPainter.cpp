#include "MixerStripPainter.h"

namespace MixerStripPainter
{

//==============================================================================
// Generic bar/knob painting
//==============================================================================

void paintVerticalBar (juce::Graphics& g, TrackerLookAndFeel& lnf,
                       juce::Rectangle<int> area, double value,
                       double minVal, double maxVal,
                       juce::Colour colour, bool bipolar)
{
    // Background
    g.setColour (lnf.findColour (TrackerLookAndFeel::gridLineColourId).brighter (0.05f));
    g.fillRect (area);

    double range = maxVal - minVal;
    if (range <= 0.0) return;

    double norm = (value - minVal) / range;
    norm = juce::jlimit (0.0, 1.0, norm);

    if (bipolar)
    {
        // Draw from center
        double centerNorm = (0.0 - minVal) / range;
        int centerY = area.getBottom() - static_cast<int> (centerNorm * static_cast<double> (area.getHeight()));

        int valueY = area.getBottom() - static_cast<int> (norm * static_cast<double> (area.getHeight()));

        g.setColour (colour.withAlpha (0.6f));
        if (valueY < centerY)
            g.fillRect (area.getX() + 1, valueY, area.getWidth() - 2, centerY - valueY);
        else
            g.fillRect (area.getX() + 1, centerY, area.getWidth() - 2, valueY - centerY);

        // Center line
        g.setColour (lnf.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.2f));
        g.drawHorizontalLine (centerY, static_cast<float> (area.getX()),
                              static_cast<float> (area.getRight()));
    }
    else
    {
        int fillH = static_cast<int> (norm * static_cast<double> (area.getHeight()));
        g.setColour (colour.withAlpha (0.6f));
        g.fillRect (area.getX() + 1, area.getBottom() - fillH, area.getWidth() - 2, fillH);
    }

    // Border
    g.setColour (colour.withAlpha (0.3f));
    g.drawRect (area, 1);
}

void paintHorizontalBar (juce::Graphics& g, TrackerLookAndFeel& lnf,
                         juce::Rectangle<int> area, double value,
                         double minVal, double maxVal,
                         juce::Colour colour, bool bipolar)
{
    // Background
    g.setColour (lnf.findColour (TrackerLookAndFeel::gridLineColourId).brighter (0.05f));
    g.fillRect (area);

    double range = maxVal - minVal;
    if (range <= 0.0) return;

    double norm = (value - minVal) / range;
    norm = juce::jlimit (0.0, 1.0, norm);

    if (bipolar)
    {
        double centerNorm = (0.0 - minVal) / range;
        int centerX = area.getX() + static_cast<int> (centerNorm * static_cast<double> (area.getWidth()));
        int valueX = area.getX() + static_cast<int> (norm * static_cast<double> (area.getWidth()));

        g.setColour (colour.withAlpha (0.6f));
        if (valueX > centerX)
            g.fillRect (centerX, area.getY() + 1, valueX - centerX, area.getHeight() - 2);
        else
            g.fillRect (valueX, area.getY() + 1, centerX - valueX, area.getHeight() - 2);

        // Center line
        g.setColour (lnf.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.2f));
        g.drawVerticalLine (centerX, static_cast<float> (area.getY()),
                            static_cast<float> (area.getBottom()));
    }
    else
    {
        int fillW = static_cast<int> (norm * static_cast<double> (area.getWidth()));
        g.setColour (colour.withAlpha (0.6f));
        g.fillRect (area.getX(), area.getY() + 1, fillW, area.getHeight() - 2);
    }

    // Border
    g.setColour (colour.withAlpha (0.3f));
    g.drawRect (area, 1);
}

void paintKnob (juce::Graphics& g, TrackerLookAndFeel& lnf,
                juce::Rectangle<int> area, double value,
                double minVal, double maxVal,
                juce::Colour colour, const juce::String& label)
{
    // Simple arc-style knob
    auto inner = area.reduced (2, 1);
    int knobDiam = juce::jmin (inner.getWidth(), inner.getHeight() - 12);
    if (knobDiam < 8) return;

    auto knobArea = juce::Rectangle<int> (inner.getCentreX() - knobDiam / 2,
                                           inner.getY() + 1, knobDiam, knobDiam);

    // Background ring
    float cx = static_cast<float> (knobArea.getCentreX());
    float cy = static_cast<float> (knobArea.getCentreY());
    float radius = static_cast<float> (knobDiam) * 0.4f;

    g.setColour (lnf.findColour (TrackerLookAndFeel::gridLineColourId).brighter (0.1f));
    juce::Path bgArc;
    bgArc.addCentredArc (cx, cy, radius, radius, 0.0f,
                          juce::MathConstants<float>::pi * 0.75f,
                          juce::MathConstants<float>::pi * 2.25f, true);
    g.strokePath (bgArc, juce::PathStrokeType (2.0f));

    // Value arc
    double norm = (maxVal > minVal) ? juce::jlimit (0.0, 1.0, (value - minVal) / (maxVal - minVal)) : 0.0;
    float startAngle = juce::MathConstants<float>::pi * 0.75f;
    float endAngle = startAngle + static_cast<float> (norm) * juce::MathConstants<float>::pi * 1.5f;

    g.setColour (colour);
    juce::Path valArc;
    valArc.addCentredArc (cx, cy, radius, radius, 0.0f, startAngle, endAngle, true);
    g.strokePath (valArc, juce::PathStrokeType (2.5f));

    // Dot indicator
    float dotX = cx + radius * std::cos (endAngle - juce::MathConstants<float>::halfPi);
    float dotY = cy + radius * std::sin (endAngle - juce::MathConstants<float>::halfPi);
    g.fillEllipse (dotX - 1.5f, dotY - 1.5f, 3.0f, 3.0f);

    // Label
    g.setFont (lnf.getMonoFont (9.0f));
    g.setColour (colour.withAlpha (0.8f));
    g.drawText (label, inner.getX(), knobArea.getBottom() + 1, inner.getWidth(),
                inner.getBottom() - knobArea.getBottom() - 1, juce::Justification::centredTop);
}

//==============================================================================
// Section painters
//==============================================================================

void paintGenericEqSection (juce::Graphics& g, TrackerLookAndFeel& lnf,
                            double eqLow, double eqMid, double eqHigh, double midFreq,
                            juce::Rectangle<int> bounds, bool /*isSelected*/, int selectedParam)
{
    auto inner = bounds.reduced (4, 2);
    int barWidth = (inner.getWidth() - 8) / 3;
    auto volumeCol = lnf.findColour (TrackerLookAndFeel::volumeColourId);
    auto selCol = lnf.findColour (TrackerLookAndFeel::fxColourId);

    struct EqBand { const char* label; double value; int idx; };
    EqBand bands[] = {
        { "L", eqLow,  0 },
        { "M", eqMid,  1 },
        { "H", eqHigh, 2 }
    };

    for (int i = 0; i < 3; ++i)
    {
        int x = inner.getX() + i * (barWidth + 4);
        auto barArea = juce::Rectangle<int> (x, inner.getY(), barWidth, inner.getHeight() - 18);

        bool paramSelected = (selectedParam == i);
        auto col = paramSelected ? selCol : volumeCol;
        paintVerticalBar (g, lnf, barArea, bands[i].value, -12.0, 12.0, col, true);

        g.setFont (lnf.getMonoFont (10.0f));
        g.setColour (paramSelected ? selCol : lnf.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.6f));
        juce::String valStr = (bands[i].value >= 0.0 ? "+" : "") + juce::String (bands[i].value, 1);
        g.drawText (bands[i].label + juce::String (" ") + valStr, x, barArea.getBottom() + 1, barWidth, 16, juce::Justification::centred);
    }

    if (selectedParam == 3)
    {
        g.setFont (lnf.getMonoFont (10.0f));
        g.setColour (selCol);
        juce::String freqStr = juce::String (static_cast<int> (midFreq)) + "Hz";
        g.drawText (freqStr, inner.getX(), inner.getBottom() - 12, inner.getWidth(), 10,
                    juce::Justification::centred);
    }
}

void paintGenericCompSection (juce::Graphics& g, TrackerLookAndFeel& lnf,
                              double threshold, double ratio, double attack, double release,
                              juce::Rectangle<int> bounds, bool /*isSelected*/, int selectedParam)
{
    auto inner = bounds.reduced (2, 2);
    auto selCol = lnf.findColour (TrackerLookAndFeel::fxColourId);
    auto textCol = lnf.findColour (TrackerLookAndFeel::textColourId);

    int knobSize = (inner.getWidth() - 6) / 2;
    int knobH = (inner.getHeight() - 2) / 2;

    struct CompParam { const char* label; double value; double minV; double maxV; int idx; };
    CompParam params[] = {
        { "THR", threshold, -60.0, 0.0, 0 },
        { "RAT", ratio,     1.0,   20.0, 1 },
        { "ATT", attack,    0.1,   100.0, 2 },
        { "REL", release,   10.0,  1000.0, 3 }
    };

    for (int i = 0; i < 4; ++i)
    {
        int col = i % 2;
        int row = i / 2;
        int x = inner.getX() + col * (knobSize + 3);
        int y = inner.getY() + row * knobH;

        auto area = juce::Rectangle<int> (x, y, knobSize, knobH);
        bool sel = (selectedParam == i);
        auto colour = sel ? selCol : textCol.withAlpha (0.5f);

        juce::String valueStr;
        switch (i)
        {
            case 0: valueStr = juce::String (static_cast<int> (params[i].value)) + "dB"; break;
            case 1: valueStr = juce::String (params[i].value, 1) + ":1"; break;
            case 2: valueStr = juce::String (params[i].value, 1) + "ms"; break;
            case 3: valueStr = juce::String (static_cast<int> (params[i].value)) + "ms"; break;
        }

        paintKnob (g, lnf, area, params[i].value, params[i].minV, params[i].maxV, colour, valueStr);
    }
}

void paintGenericVolumeFader (juce::Graphics& g, TrackerLookAndFeel& lnf,
                              double volume, juce::Rectangle<int> bounds,
                              bool isSelected, float peakLinear)
{
    auto inner = bounds.reduced (6, 4);
    auto selCol = lnf.findColour (TrackerLookAndFeel::fxColourId);
    auto volCol = lnf.findColour (TrackerLookAndFeel::volumeColourId);

    g.setFont (lnf.getMonoFont (12.0f));
    g.setColour (isSelected ? selCol : lnf.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.7f));

    juce::String volText;
    if (volume <= -99.0)
        volText = "-inf";
    else
        volText = juce::String (volume, 1) + "dB";
    g.drawText (volText, inner.getX(), inner.getY(), inner.getWidth(), 12, juce::Justification::centred);

    auto faderArea = inner.withTrimmedTop (14).withTrimmedBottom (2);
    auto trackArea = faderArea.reduced (faderArea.getWidth() / 2 - 6, 0);
    g.setColour (lnf.findColour (TrackerLookAndFeel::gridLineColourId).brighter (0.1f));
    g.fillRect (trackArea);

    if (peakLinear > 0.0f)
    {
        float peakDb = juce::Decibels::gainToDecibels (peakLinear, -100.0f);
        double peakNorm = (static_cast<double> (peakDb) - (-100.0)) / (12.0 - (-100.0));
        peakNorm = juce::jlimit (0.0, 1.0, peakNorm);
        int meterHeight = static_cast<int> (peakNorm * static_cast<double> (faderArea.getHeight()));

        juce::Colour meterCol;
        if (peakDb > 0.0f)
            meterCol = juce::Colour (0xffcc3333);
        else if (peakDb > -6.0f)
            meterCol = juce::Colour (0xffccaa33);
        else
            meterCol = juce::Colour (0xff33aa55);

        g.setColour (meterCol.withAlpha (0.25f));
        g.fillRect (juce::Rectangle<int> (faderArea.getX() + 1, faderArea.getBottom() - meterHeight,
                                           faderArea.getWidth() - 2, meterHeight));
        g.setColour (meterCol.withAlpha (0.6f));
        g.drawHorizontalLine (faderArea.getBottom() - meterHeight,
                              static_cast<float> (faderArea.getX() + 1),
                              static_cast<float> (faderArea.getRight() - 1));
    }

    // dB scale markings
    g.setFont (lnf.getMonoFont (9.0f));
    g.setColour (lnf.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.3f));
    double markings[] = { 12.0, 6.0, 0.0, -6.0, -12.0, -24.0, -48.0 };
    for (auto dB : markings)
    {
        double norm2 = (dB - (-100.0)) / (12.0 - (-100.0));
        int y = faderArea.getBottom() - static_cast<int> (norm2 * static_cast<double> (faderArea.getHeight()));
        g.drawHorizontalLine (y, static_cast<float> (faderArea.getX()),
                              static_cast<float> (faderArea.getX() + 3));
        g.drawHorizontalLine (y, static_cast<float> (faderArea.getRight() - 3),
                              static_cast<float> (faderArea.getRight()));
    }

    double norm = (volume - (-100.0)) / (12.0 - (-100.0));
    norm = juce::jlimit (0.0, 1.0, norm);
    int fillHeight = static_cast<int> (norm * static_cast<double> (faderArea.getHeight()));

    auto fillCol = isSelected ? selCol : volCol;
    g.setColour (fillCol.withAlpha (0.7f));
    g.fillRect (trackArea.getX(), faderArea.getBottom() - fillHeight,
                trackArea.getWidth(), fillHeight);

    int handleY = faderArea.getBottom() - fillHeight;
    g.setColour (fillCol);
    g.fillRect (faderArea.getX(), handleY - 2, faderArea.getWidth(), 4);
}

void paintGenericPanSection (juce::Graphics& g, TrackerLookAndFeel& lnf,
                             int pan, juce::Rectangle<int> bounds, bool isSelected)
{
    auto inner = bounds.reduced (4, 3);
    auto selCol = lnf.findColour (TrackerLookAndFeel::fxColourId);
    auto panCol = lnf.findColour (TrackerLookAndFeel::instrumentColourId);

    juce::String panLabel;
    if (pan == 0)
        panLabel = "PAN C";
    else if (pan < 0)
        panLabel = "PAN L" + juce::String (-pan);
    else
        panLabel = "PAN R" + juce::String (pan);

    g.setFont (lnf.getMonoFont (9.0f));
    g.setColour (isSelected ? selCol : lnf.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.5f));
    g.drawText (panLabel, inner.getX(), inner.getY(), 44, inner.getHeight(), juce::Justification::centredLeft);

    auto barArea = juce::Rectangle<int> (inner.getX() + 44, inner.getY() + 2,
                                          inner.getWidth() - 46, inner.getHeight() - 4);
    paintHorizontalBar (g, lnf, barArea, static_cast<double> (pan), -50.0, 50.0,
                        isSelected ? selCol : panCol, true);
}

void paintGenericMuteSolo (juce::Graphics& g, TrackerLookAndFeel& lnf,
                           bool muted, bool soloed, juce::Rectangle<int> bounds, bool hasSolo)
{
    // Top separator
    g.setColour (lnf.findColour (TrackerLookAndFeel::gridLineColourId));
    g.drawHorizontalLine (bounds.getY(), static_cast<float> (bounds.getX()),
                          static_cast<float> (bounds.getRight()));

    if (hasSolo)
    {
        int halfW = bounds.getWidth() / 2;

        auto muteArea = juce::Rectangle<int> (bounds.getX() + 2, bounds.getY() + 2,
                                               halfW - 3, bounds.getHeight() - 4);
        auto muteCol = lnf.findColour (TrackerLookAndFeel::muteColourId);
        g.setColour (muted ? muteCol : muteCol.withAlpha (0.15f));
        g.fillRoundedRectangle (muteArea.toFloat(), 2.0f);
        g.setColour (muted ? juce::Colours::white : lnf.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.4f));
        g.setFont (lnf.getMonoFont (13.0f));
        g.drawText ("M", muteArea, juce::Justification::centred);

        auto soloArea = juce::Rectangle<int> (bounds.getX() + halfW + 1, bounds.getY() + 2,
                                               halfW - 3, bounds.getHeight() - 4);
        auto soloCol = lnf.findColour (TrackerLookAndFeel::soloColourId);
        g.setColour (soloed ? soloCol : soloCol.withAlpha (0.15f));
        g.fillRoundedRectangle (soloArea.toFloat(), 2.0f);
        g.setColour (soloed ? juce::Colours::black : lnf.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.4f));
        g.setFont (lnf.getMonoFont (13.0f));
        g.drawText ("S", soloArea, juce::Justification::centred);
    }
    else
    {
        // Mute only (full width)
        auto muteArea = bounds.reduced (2);
        auto muteCol = lnf.findColour (TrackerLookAndFeel::muteColourId);
        g.setColour (muted ? muteCol : muteCol.withAlpha (0.15f));
        g.fillRoundedRectangle (muteArea.toFloat(), 2.0f);
        g.setColour (muted ? juce::Colours::white : lnf.findColour (TrackerLookAndFeel::textColourId).withAlpha (0.4f));
        g.setFont (lnf.getMonoFont (13.0f));
        g.drawText ("M", muteArea, juce::Justification::centred);
    }
}

void paintLimiterSection (juce::Graphics& g, TrackerLookAndFeel& lnf,
                          double threshold, double release,
                          juce::Rectangle<int> bounds, bool /*isSelected*/, int selectedParam)
{
    auto inner = bounds.reduced (2, 2);
    auto selCol = lnf.findColour (TrackerLookAndFeel::fxColourId);
    auto textCol = lnf.findColour (TrackerLookAndFeel::textColourId);

    int knobSize = (inner.getWidth() - 6) / 2;
    int knobH = inner.getHeight();

    // Threshold knob
    {
        auto area = juce::Rectangle<int> (inner.getX(), inner.getY(), knobSize, knobH);
        bool sel = (selectedParam == 0);
        auto colour = sel ? selCol : textCol.withAlpha (0.5f);
        juce::String valueStr = juce::String (threshold, 1) + "dB";
        paintKnob (g, lnf, area, threshold, -24.0, 0.0, colour, valueStr);
    }

    // Release knob
    {
        auto area = juce::Rectangle<int> (inner.getX() + knobSize + 3, inner.getY(), knobSize, knobH);
        bool sel = (selectedParam == 1);
        auto colour = sel ? selCol : textCol.withAlpha (0.5f);
        juce::String valueStr = juce::String (static_cast<int> (release)) + "ms";
        paintKnob (g, lnf, area, release, 1.0, 500.0, colour, valueStr);
    }
}

void paintInsertSlots (juce::Graphics& g, TrackerLookAndFeel& lnf,
                       const std::vector<InsertSlotState>& slots,
                       int insertRowHeight, int insertAddButtonHeight,
                       juce::Rectangle<int> bounds,
                       bool isSelected, int selectedParam)
{
    auto inner = bounds.reduced (2, 1);
    auto selCol = lnf.findColour (TrackerLookAndFeel::fxColourId);
    auto textCol = lnf.findColour (TrackerLookAndFeel::textColourId);
    auto bgCol = lnf.findColour (TrackerLookAndFeel::gridLineColourId).brighter (0.05f);

    int numSlots = static_cast<int> (slots.size());

    // Draw each insert row
    for (int i = 0; i < numSlots; ++i)
    {
        auto& slot = slots[static_cast<size_t> (i)];
        auto rowArea = juce::Rectangle<int> (inner.getX(), inner.getY() + i * insertRowHeight,
                                              inner.getWidth(), insertRowHeight);
        bool isSel = isSelected && (selectedParam == i);

        // Row background
        g.setColour (isSel ? bgCol.brighter (0.1f) : bgCol);
        g.fillRect (rowArea);

        // Bypass indicator (small dot)
        auto bypassArea = rowArea.removeFromLeft (14);
        g.setColour (slot.bypassed ? textCol.withAlpha (0.2f) : juce::Colour (0xff33aa55));
        g.fillEllipse (bypassArea.getCentreX() - 3.0f, bypassArea.getCentreY() - 3.0f, 6.0f, 6.0f);

        // Remove button (X) on the right
        auto removeArea = rowArea.removeFromRight (16);
        g.setFont (lnf.getMonoFont (10.0f));
        g.setColour (textCol.withAlpha (0.5f));
        g.drawText ("x", removeArea, juce::Justification::centred);

        // Plugin name (truncated)
        g.setFont (lnf.getMonoFont (9.0f));
        g.setColour (isSel ? selCol : textCol.withAlpha (slot.bypassed ? 0.3f : 0.7f));
        auto nameText = slot.pluginName;
        if (nameText.length() > 10)
            nameText = nameText.substring (0, 9) + "~";
        g.drawText (nameText, rowArea.reduced (1, 0), juce::Justification::centredLeft);

        // Bottom border
        g.setColour (lnf.findColour (TrackerLookAndFeel::gridLineColourId));
        g.drawHorizontalLine (inner.getY() + (i + 1) * insertRowHeight - 1,
                              static_cast<float> (inner.getX()),
                              static_cast<float> (inner.getRight()));
    }

    // "+" add button at bottom
    int addY = inner.getY() + numSlots * insertRowHeight;
    auto addArea = juce::Rectangle<int> (inner.getX(), addY, inner.getWidth(), insertAddButtonHeight);

    bool canAdd = numSlots < kMaxInsertSlots;
    g.setColour (canAdd ? selCol.withAlpha (0.3f) : bgCol);
    g.fillRect (addArea);

    g.setFont (lnf.getMonoFont (12.0f));
    g.setColour (canAdd ? selCol : textCol.withAlpha (0.2f));
    g.drawText ("+", addArea, juce::Justification::centred);
}

} // namespace MixerStripPainter
