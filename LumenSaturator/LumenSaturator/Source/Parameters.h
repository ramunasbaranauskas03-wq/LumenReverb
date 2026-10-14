#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace IDs
{
    inline juce::String mode (int b)  { return "mode"  + juce::String (b); }
    inline juce::String drive (int b) { return "drive" + juce::String (b); }
    inline juce::String mix (int b)   { return "mix"   + juce::String (b); }
    inline juce::String level (int b) { return "level" + juce::String (b); }
    inline juce::String solo (int b)  { return "solo"  + juce::String (b); }
    inline constexpr const char* xLow      = "xlow";
    inline constexpr const char* xHigh     = "xhigh";
    inline constexpr const char* output    = "output";
    inline constexpr const char* autoGain  = "autogain";
    inline constexpr const char* oversample= "oversample";
}

inline const juce::StringArray& modeChoices()
{
    static const juce::StringArray c { "Tape", "Tube", "Soft Clip", "Hard Clip", "Wavefold" };
    return c;
}
inline const juce::StringArray& bandNames()
{
    static const juce::StringArray c { "LOW", "MID", "HIGH" };
    return c;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto hz  = [] (float v, int) { return v < 1000.0f ? String (roundToInt (v)) + " Hz" : String (v / 1000.0f, 1) + " kHz"; };
    auto pct = [] (float v, int) { return String (roundToInt (v)) + " %"; };
    auto db  = [] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; };
    auto add = [&] (const String& id, const String& name, NormalisableRange<float> r, float def, std::function<String (float, int)> f)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
                        AudioParameterFloatAttributes().withStringFromValueFunction (std::move (f))));
    };
    auto skewed = [] (float lo, float hi, float c) { NormalisableRange<float> r (lo, hi); r.setSkewForCentre (c); return r; };

    const int   defMode[3]  = { 0, 1, 0 };
    const float defDrive[3] = { 6.0f, 6.0f, 3.0f };
    for (int b = 0; b < 3; ++b)
    {
        const String n = bandNames()[b].toLowerCase().substring (0, 1).toUpperCase() + bandNames()[b].toLowerCase().substring (1);
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { IDs::mode (b), 1 }, n + " Mode", modeChoices(), defMode[b]));
        add (IDs::drive (b), n + " Drive", { 0.0f, 36.0f, 0.1f }, defDrive[b], db);
        add (IDs::mix (b),   n + " Mix",   { 0.0f, 100.0f }, 100.0f, pct);
        add (IDs::level (b), n + " Level", { -24.0f, 12.0f, 0.1f }, 0.0f, db);
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { IDs::solo (b), 1 }, n + " Solo", false));
    }
    add (IDs::xLow,   "Crossover Low",  skewed (40.0f, 1000.0f, 200.0f), 150.0f, hz);
    add (IDs::xHigh,  "Crossover High", skewed (1000.0f, 12000.0f, 3500.0f), 3500.0f, hz);
    add (IDs::output, "Output", { -24.0f, 12.0f, 0.1f }, 0.0f, db);
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { IDs::autoGain, 1 }, "Auto Gain", true));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { IDs::oversample, 1 }, "Oversampling",
                                                        StringArray { "1x", "2x", "4x", "8x", "16x" }, 2));
    return layout;
}
