#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace IDs
{
    inline constexpr const char* trigger  = "trigger";
    inline constexpr const char* rate     = "rate";
    inline constexpr const char* lowHz    = "lowhz";
    inline constexpr const char* highHz   = "highhz";
    inline constexpr const char* depth    = "depth";
    inline constexpr const char* outside  = "outside";
    inline constexpr const char* smooth   = "smooth";
    inline constexpr const char* offset   = "offset";
    inline constexpr const char* scThresh = "scthresh";
    inline constexpr const char* solo     = "solo";
    inline constexpr const char* output   = "output";
}

inline const juce::StringArray& triggerChoices()
{
    static const juce::StringArray c { "Sync", "Sidechain", "MIDI" };
    return c;
}
inline const juce::StringArray& rateChoices()
{
    static const juce::StringArray c { "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2", "1 Bar", "2 Bars" };
    return c;
}
inline constexpr double kRateBeats[] = { 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0, 2.0, 4.0, 8.0 };
inline constexpr int kNumRates = 8;

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto hz   = [] (float v, int) { return v < 1000.0f ? String (roundToInt (v)) + " Hz" : String (v / 1000.0f, 1) + " kHz"; };
    auto pct  = [] (float v, int) { return String (roundToInt (v)) + " %"; };
    auto spct = [] (float v, int) { return (v > 0.5f ? "+" : "") + String (roundToInt (v)) + " %"; };
    auto db   = [] (float v, int) { return String (v, 1) + " dB"; };

    auto add = [&] (const char* id, const char* name, NormalisableRange<float> range, float def,
                    std::function<String (float, int)> toText)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, def,
                        AudioParameterFloatAttributes().withStringFromValueFunction (std::move (toText))));
    };
    auto skewed = [] (float lo, float hi, float centre)
    {
        NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    };

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { IDs::trigger, 1 }, "Trigger", triggerChoices(), 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { IDs::rate, 1 }, "Rate", rateChoices(), 4));
    add (IDs::lowHz,    "Band Low",      skewed (20.0f, 20000.0f, 500.0f), 20.0f,  hz);
    add (IDs::highHz,   "Band High",     skewed (20.0f, 20000.0f, 500.0f), 400.0f, hz);
    add (IDs::depth,    "Depth",         { 0.0f, 100.0f }, 100.0f, pct);
    add (IDs::outside,  "Outside Depth", { 0.0f, 100.0f }, 0.0f, pct);
    add (IDs::smooth,   "Smooth",        { 0.0f, 100.0f }, 50.0f, pct);
    add (IDs::offset,   "Offset",        { -50.0f, 50.0f }, 0.0f, spct);
    add (IDs::scThresh, "SC Threshold",  { -60.0f, 0.0f }, -30.0f, db);
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { IDs::solo, 1 }, "Solo Band", false));
    add (IDs::output,   "Output",        { -24.0f, 12.0f }, 0.0f, db);
    return layout;
}
