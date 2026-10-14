#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace IDs
{
    inline constexpr const char* size      = "size";
    inline constexpr const char* decay     = "decay";
    inline constexpr const char* preDelay  = "predelay";
    inline constexpr const char* diffusion = "diffusion";
    inline constexpr const char* early     = "early";
    inline constexpr const char* damping   = "damping";
    inline constexpr const char* lowCut    = "lowcut";
    inline constexpr const char* highCut   = "highcut";
    inline constexpr const char* width     = "width";
    inline constexpr const char* modDepth  = "moddepth";
    inline constexpr const char* modRate   = "modrate";
    inline constexpr const char* duck      = "duck";
    inline constexpr const char* adapt     = "adapt";
    inline constexpr const char* mix       = "mix";
    inline constexpr const char* output    = "output";
    inline constexpr const char* freeze    = "freeze";
    inline constexpr const char* sync      = "sync";
}

inline const juce::StringArray& syncChoices()
{
    static const juce::StringArray c { "Off", "1/4", "1/2", "1 Bar", "2 Bars", "4 Bars" };
    return c;
}
inline constexpr double kSyncBeats[] = { 0.0, 1.0, 2.0, 4.0, 8.0, 16.0 };

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto hz  = [] (float v, int) { return v < 1000.0f ? String (roundToInt (v)) + " Hz" : String (v / 1000.0f, 1) + " kHz"; };
    auto ms  = [] (float v, int) { return String (v, v < 10.0f ? 1 : 0) + " ms"; };
    auto sec = [] (float v, int) { return String (v, v < 10.0f ? 2 : 1) + " s"; };
    auto pct = [] (float v, int) { return String (roundToInt (v)) + " %"; };
    auto db  = [] (float v, int) { return String (v, 1) + " dB"; };
    auto rate= [] (float v, int) { return String (v, 2) + " Hz"; };

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

    add (IDs::size,      "Size",       { 0.0f, 100.0f }, 60.0f, pct);
    add (IDs::decay,     "Decay",      skewed (0.2f, 20.0f, 2.5f), 2.5f, sec);
    add (IDs::preDelay,  "Pre-Delay",  skewed (0.0f, 250.0f, 40.0f), 20.0f, ms);
    add (IDs::diffusion, "Diffusion",  { 0.0f, 100.0f }, 75.0f, pct);
    add (IDs::early,     "Early",      { 0.0f, 100.0f }, 50.0f, pct);
    add (IDs::damping,   "Damping",    skewed (1000.0f, 20000.0f, 6000.0f), 8000.0f, hz);
    add (IDs::lowCut,    "Low Cut",    skewed (20.0f, 1000.0f, 200.0f), 120.0f, hz);
    add (IDs::highCut,   "High Cut",   skewed (1000.0f, 20000.0f, 6000.0f), 12000.0f, hz);
    add (IDs::width,     "Width",      { 0.0f, 150.0f }, 100.0f, pct);
    add (IDs::modDepth,  "Mod Depth",  { 0.0f, 100.0f }, 30.0f, pct);
    add (IDs::modRate,   "Mod Rate",   skewed (0.05f, 5.0f, 0.8f), 0.6f, rate);
    add (IDs::duck,      "Ducking",    { 0.0f, 100.0f }, 0.0f, pct);
    add (IDs::adapt,     "Adapt",      { 0.0f, 100.0f }, 50.0f, pct);
    add (IDs::mix,       "Mix",        { 0.0f, 100.0f }, 30.0f, pct);
    add (IDs::output,    "Output",     { -24.0f, 12.0f }, 0.0f, db);

    layout.add (std::make_unique<AudioParameterBool>   (ParameterID { IDs::freeze, 1 }, "Freeze", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { IDs::sync, 1 }, "Tempo Sync", syncChoices(), 0));
    return layout;
}
