#pragma once

#include <algorithm>
#include <cmath>
#include <vector>
#include <JuceHeader.h>

struct PluginModulatorSource
{
    enum class Type { LFO = 0, Envelope = 1 };
    enum class LfoShape { Sine = 0, Triangle = 1, Saw = 2, Square = 3, Random = 4 };
    enum class LfoRateMode { Steps = 0, Hz = 1 };
    enum class EnvelopeTriggerMode { NoteGate = 0, StepFxOnly = 1 };

    Type type = Type::LFO;
    juce::String name;
    bool enabled = true;

    LfoShape lfoShape = LfoShape::Triangle;
    LfoRateMode lfoRateMode = LfoRateMode::Steps;
    double lfoRateSteps = 16.0;
    double lfoRateHz = 1.0;

    EnvelopeTriggerMode envelopeTriggerMode = EnvelopeTriggerMode::NoteGate;
    double attackS = 0.020;
    double decayS = 0.120;
    double sustain = 0.700;
    double releaseS = 0.180;

    bool isDefault() const
    {
        return type == Type::LFO
            && name.isEmpty()
            && enabled
            && lfoShape == LfoShape::Triangle
            && lfoRateMode == LfoRateMode::Steps
            && std::abs (lfoRateSteps - 16.0) < 1.0e-9
            && std::abs (lfoRateHz - 1.0) < 1.0e-9
            && envelopeTriggerMode == EnvelopeTriggerMode::NoteGate
            && std::abs (attackS - 0.020) < 1.0e-9
            && std::abs (decayS - 0.120) < 1.0e-9
            && std::abs (sustain - 0.700) < 1.0e-9
            && std::abs (releaseS - 0.180) < 1.0e-9;
    }
};

struct PluginModulationRoute
{
    int sourceIndex = 0;
    int parameterIndex = -1;
    juce::String parameterName;
    float amount = 0.0f; // -1.0..1.0 normalized parameter travel
    bool enabled = true;

    bool isDefault() const
    {
        return sourceIndex == 0
            && parameterIndex < 0
            && parameterName.isEmpty()
            && std::abs (amount) < 1.0e-6f
            && enabled;
    }
};

struct PluginInstrumentModulation
{
    std::vector<PluginModulatorSource> sources;
    std::vector<PluginModulationRoute> routes;

    bool isDefault() const { return sources.empty() && routes.empty(); }

    int addLfo()
    {
        PluginModulatorSource source;
        source.type = PluginModulatorSource::Type::LFO;
        source.name = "LFO " + juce::String (static_cast<int> (sources.size()) + 1);
        sources.push_back (source);
        return static_cast<int> (sources.size()) - 1;
    }

    int addEnvelope()
    {
        PluginModulatorSource source;
        source.type = PluginModulatorSource::Type::Envelope;
        source.name = "Env " + juce::String (static_cast<int> (sources.size()) + 1);
        source.envelopeTriggerMode = PluginModulatorSource::EnvelopeTriggerMode::NoteGate;
        sources.push_back (source);
        return static_cast<int> (sources.size()) - 1;
    }

    void removeSource (int index)
    {
        if (index < 0 || index >= static_cast<int> (sources.size()))
            return;

        sources.erase (sources.begin() + index);

        routes.erase (std::remove_if (routes.begin(), routes.end(),
                                      [index] (const PluginModulationRoute& route)
                                      {
                                          return route.sourceIndex == index;
                                      }),
                      routes.end());

        for (auto& route : routes)
            if (route.sourceIndex > index)
                --route.sourceIndex;
    }

    void removeRoute (int index)
    {
        if (index >= 0 && index < static_cast<int> (routes.size()))
            routes.erase (routes.begin() + index);
    }
};

namespace PluginInstrumentModulationSerializer
{
inline juce::ValueTree toValueTree (const PluginInstrumentModulation& modulation)
{
    juce::ValueTree tree ("PluginModulation");

    for (const auto& source : modulation.sources)
    {
        juce::ValueTree sourceTree ("Source");
        sourceTree.setProperty ("type", static_cast<int> (source.type), nullptr);
        sourceTree.setProperty ("name", source.name, nullptr);
        if (! source.enabled)
            sourceTree.setProperty ("enabled", false, nullptr);
        sourceTree.setProperty ("lfoShape", static_cast<int> (source.lfoShape), nullptr);
        sourceTree.setProperty ("lfoRateMode", static_cast<int> (source.lfoRateMode), nullptr);
        sourceTree.setProperty ("lfoRateSteps", source.lfoRateSteps, nullptr);
        sourceTree.setProperty ("lfoRateHz", source.lfoRateHz, nullptr);
        sourceTree.setProperty ("envTrigger", static_cast<int> (source.envelopeTriggerMode), nullptr);
        sourceTree.setProperty ("attackS", source.attackS, nullptr);
        sourceTree.setProperty ("decayS", source.decayS, nullptr);
        sourceTree.setProperty ("sustain", source.sustain, nullptr);
        sourceTree.setProperty ("releaseS", source.releaseS, nullptr);
        tree.addChild (sourceTree, -1, nullptr);
    }

    for (const auto& route : modulation.routes)
    {
        juce::ValueTree routeTree ("Route");
        routeTree.setProperty ("source", route.sourceIndex, nullptr);
        routeTree.setProperty ("param", route.parameterIndex, nullptr);
        routeTree.setProperty ("paramName", route.parameterName, nullptr);
        routeTree.setProperty ("amount", route.amount, nullptr);
        if (! route.enabled)
            routeTree.setProperty ("enabled", false, nullptr);
        tree.addChild (routeTree, -1, nullptr);
    }

    return tree;
}

inline PluginInstrumentModulation fromValueTree (const juce::ValueTree& tree)
{
    PluginInstrumentModulation modulation;
    if (! tree.isValid() || ! tree.hasType ("PluginModulation"))
        return modulation;

    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto child = tree.getChild (i);
        if (child.hasType ("Source"))
        {
            PluginModulatorSource source;

            const int type = static_cast<int> (child.getProperty ("type", 0));
            if (type >= 0 && type <= static_cast<int> (PluginModulatorSource::Type::Envelope))
                source.type = static_cast<PluginModulatorSource::Type> (type);

            source.name = child.getProperty ("name", "").toString();
            source.enabled = child.getProperty ("enabled", true);

            const int shape = static_cast<int> (child.getProperty ("lfoShape", 1));
            if (shape >= 0 && shape <= static_cast<int> (PluginModulatorSource::LfoShape::Random))
                source.lfoShape = static_cast<PluginModulatorSource::LfoShape> (shape);

            const int rateMode = static_cast<int> (child.getProperty ("lfoRateMode", 0));
            if (rateMode >= 0 && rateMode <= static_cast<int> (PluginModulatorSource::LfoRateMode::Hz))
                source.lfoRateMode = static_cast<PluginModulatorSource::LfoRateMode> (rateMode);

            source.lfoRateSteps = juce::jlimit (1.0, 256.0, static_cast<double> (child.getProperty ("lfoRateSteps", 16.0)));
            source.lfoRateHz = juce::jlimit (0.01, 40.0, static_cast<double> (child.getProperty ("lfoRateHz", 1.0)));

            const int trigger = static_cast<int> (child.getProperty ("envTrigger", 0));
            if (trigger >= 0 && trigger <= static_cast<int> (PluginModulatorSource::EnvelopeTriggerMode::StepFxOnly))
                source.envelopeTriggerMode = static_cast<PluginModulatorSource::EnvelopeTriggerMode> (trigger);

            source.attackS = juce::jlimit (0.0, 30.0, static_cast<double> (child.getProperty ("attackS", 0.020)));
            source.decayS = juce::jlimit (0.0, 30.0, static_cast<double> (child.getProperty ("decayS", 0.120)));
            source.sustain = juce::jlimit (0.0, 1.0, static_cast<double> (child.getProperty ("sustain", 0.700)));
            source.releaseS = juce::jlimit (0.0, 30.0, static_cast<double> (child.getProperty ("releaseS", 0.180)));
            modulation.sources.push_back (source);
        }
        else if (child.hasType ("Route"))
        {
            PluginModulationRoute route;
            route.sourceIndex = child.getProperty ("source", 0);
            route.parameterIndex = child.getProperty ("param", -1);
            route.parameterName = child.getProperty ("paramName", "").toString();
            route.amount = juce::jlimit (-1.0f, 1.0f, static_cast<float> (child.getProperty ("amount", 0.0f)));
            route.enabled = child.getProperty ("enabled", true);
            modulation.routes.push_back (route);
        }
    }

    modulation.routes.erase (std::remove_if (modulation.routes.begin(), modulation.routes.end(),
                                             [&modulation] (const PluginModulationRoute& route)
                                             {
                                                 return route.sourceIndex < 0
                                                     || route.sourceIndex >= static_cast<int> (modulation.sources.size())
                                                     || route.parameterIndex < 0;
                                             }),
                             modulation.routes.end());

    return modulation;
}
}
