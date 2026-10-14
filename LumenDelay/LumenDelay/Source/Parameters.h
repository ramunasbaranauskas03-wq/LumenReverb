#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace IDs
{
    inline constexpr const char* sync      = "sync";
    inline constexpr const char* time      = "time";
    inline constexpr const char* division  = "division";
    inline constexpr const char* offset    = "offset";
    inline constexpr const char* mode      = "mode";
    inline constexpr const char* feedback  = "feedback";
    inline constexpr const char* diffusion = "diffusion";
    inline constexpr const char* width     = "width";
    inline constexpr const char* lowCut    = "lowcut";
    inline constexpr const char* highCut   = "highcut";
    inline constexpr const char* drive     = "drive";
    inline constexpr const char* wow       = "wow";
    inline constexpr const char* modRate   = "modrate";
    inline constexpr const char* duck      = "duck";
    inline constexpr const char* adapt     = "adapt";
    inline constexpr const char* mix       = "mix";
    inline constexpr const char* output    = "output";
    inline constexpr const char* freeze    = "freeze";
    inline constexpr const char* duckSrc   = "ducksrc";
}

inline const juce::StringArray& divisionChoices()
{
    static const juce::StringArray c { "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D",
                                       "1/4T", "1/4", "1/4D", "1/2", "1/2D", "1 Bar" };
    return c;
}
inline constexpr double kDivisionBeats[] = { 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75,
                                             2.0 / 3.0, 1.0, 1.5, 2.0, 3.0, 4.0 };
inline constexpr int kNumDivisions = 13;

inline const juce::StringArray& modeChoices()
{
    static const juce::StringArray c { "Stereo", "Ping-Pong", "Mono" };
    return c;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto hz   = [] (float v, int) { return v < 1000.0f ? String (roundToInt (v)) + " Hz" : String (v / 1000.0f, 1) + " kHz"; };
    auto ms   = [] (float v, int) { return v < 1000.0f ? String (v, v < 10.0f ? 1 : 0) + " ms" : String (v / 1000.0f, 2) + " s"; };
    auto pct  = [] (float v, int) { return String (roundToInt (v)) + " %"; };
    auto spct = [] (float v, int) { return (v > 0.5f ? "+" : "") + String (roundToInt (v)) + " %"; };
    auto db   = [] (float v, int) { return String (v, 1) + " dB"; };
    auto rate = [] (float v, int) { return String (v, 2) + " Hz"; };

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

    layout.add (std::make_unique<AudioParameterBool>   (ParameterID { IDs::sync, 1 }, "Tempo Sync", true));
    add (IDs::time, "Time", skewed (1.0f, 2000.0f, 300.0f), 375.0f, ms);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { IDs::division, 1 }, "Division", divisionChoices(), 6));
    add (IDs::offset,    "Stereo Offset", { -50.0f, 50.0f }, 0.0f, spct);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { IDs::mode, 1 }, "Mode", modeChoices(), 1));
    add (IDs::feedback,  "Feedback",  { 0.0f, 100.0f }, 40.0f, pct);
    add (IDs::diffusion, "Diffusion", { 0.0f, 100.0f }, 0.0f, pct);
    add (IDs::width,     "Width",     { 0.0f, 150.0f }, 100.0f, pct);
    add (IDs::lowCut,    "Low Cut",   skewed (20.0f, 2000.0f, 250.0f), 150.0f, hz);
    add (IDs::highCut,   "High Cut",  skewed (1000.0f, 20000.0f, 6000.0f), 7000.0f, hz);
    add (IDs::drive,     "Drive",     { 0.0f, 100.0f }, 20.0f, pct);
    add (IDs::wow,       "Wow",       { 0.0f, 100.0f }, 15.0f, pct);
    add (IDs::modRate,   "Mod Rate",  skewed (0.05f, 5.0f, 0.8f), 0.5f, rate);
    add (IDs::duck,      "Ducking",   { 0.0f, 100.0f }, 0.0f, pct);
    add (IDs::adapt,     "Adapt",     { 0.0f, 100.0f }, 50.0f, pct);
    add (IDs::mix,       "Mix",       { 0.0f, 100.0f }, 30.0f, pct);
    add (IDs::output,    "Output",    { -24.0f, 12.0f }, 0.0f, db);
    layout.add (std::make_unique<AudioParameterBool>   (ParameterID { IDs::freeze, 1 }, "Freeze", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { IDs::duckSrc, 1 }, "Duck Source",
                                                        StringArray { "Input", "Sidechain" }, 0));
    return layout;
}
