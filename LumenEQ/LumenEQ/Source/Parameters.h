#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace IDs
{
    inline juce::String on (int b)    { return "on"    + juce::String (b); }
    inline juce::String type (int b)  { return "type"  + juce::String (b); }
    inline juce::String freq (int b)  { return "freq"  + juce::String (b); }
    inline juce::String gain (int b)  { return "gain"  + juce::String (b); }
    inline juce::String q (int b)     { return "q"     + juce::String (b); }
    inline juce::String dyn (int b)   { return "dyn"   + juce::String (b); }
    inline juce::String thr (int b)   { return "thr"   + juce::String (b); }
    inline juce::String sc (int b)    { return "sc"    + juce::String (b); }
    inline constexpr const char* attack  = "attack";
    inline constexpr const char* release = "release";
    inline constexpr const char* output  = "output";
}

inline const juce::StringArray& typeChoices()
{
    static const juce::StringArray c { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch" };
    return c;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto hz  = [] (float v, int) { return v < 1000.0f ? String (roundToInt (v)) + " Hz" : String (v / 1000.0f, 2) + " kHz"; };
    auto db  = [] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; };
    auto qq  = [] (float v, int) { return String (v, 2); };
    auto ms  = [] (float v, int) { return v < 10.0f ? String (v, 1) + " ms" : String (roundToInt (v)) + " ms"; };
    auto add = [&] (const String& id, const String& name, NormalisableRange<float> r, float def, std::function<String (float, int)> f)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
                        AudioParameterFloatAttributes().withStringFromValueFunction (std::move (f))));
    };
    auto skewed = [] (float lo, float hi, float c) { NormalisableRange<float> r (lo, hi); r.setSkewForCentre (c); return r; };

    const float defF[6] = { 80.0f, 250.0f, 800.0f, 2500.0f, 6000.0f, 12000.0f };
    const int   defT[6] = { 1, 0, 0, 0, 0, 2 };
    for (int b = 0; b < 6; ++b)
    {
        const String n = "Band " + String (b + 1) + " ";
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { IDs::on (b), 1 }, n + "On", true));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { IDs::type (b), 1 }, n + "Type", typeChoices(), defT[b]));
        add (IDs::freq (b), n + "Freq", skewed (20.0f, 20000.0f, 1000.0f), defF[b], hz);
        add (IDs::gain (b), n + "Gain", { -24.0f, 24.0f, 0.1f }, 0.0f, db);
        add (IDs::q (b),    n + "Q",    skewed (0.1f, 18.0f, 1.0f), 0.71f, qq);
        add (IDs::dyn (b),  n + "Dyn Range", { -24.0f, 24.0f, 0.1f }, 0.0f, db);
        add (IDs::thr (b),  n + "Threshold", { -60.0f, 0.0f, 0.1f }, -30.0f, db);
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { IDs::sc (b), 1 }, n + "Detector", StringArray { "Internal", "Sidechain" }, 0));
    }
    add (IDs::attack,  "Attack",  skewed (0.5f, 100.0f, 10.0f), 5.0f, ms);
    add (IDs::release, "Release", skewed (10.0f, 1000.0f, 120.0f), 120.0f, ms);
    add (IDs::output,  "Output",  { -24.0f, 12.0f, 0.1f }, 0.0f, db);
    return layout;
}
