#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace IDs
{
    inline constexpr const char* monoOn    = "monoon";
    inline constexpr const char* monoHz    = "monohz";
    inline constexpr const char* width     = "width";
    inline constexpr const char* sub       = "sub";
    inline constexpr const char* output    = "output";
    inline constexpr const char* harmAmt   = "harmamt";
    inline constexpr const char* harmFocus = "harmfocus";
    inline constexpr const char* harmChar  = "harmchar";
    inline constexpr const char* harmTone  = "harmtone";
    inline constexpr const char* listen    = "listen";
    inline constexpr const char* phaseOn   = "phaseon";
    inline constexpr const char* phaseRot  = "phaserot";
    inline constexpr const char* phaseDly  = "phasedelay";
    inline constexpr const char* phaseFoc  = "phasefocus";
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto hz  = [] (float v, int) { return v < 1000.0f ? String (roundToInt (v)) + " Hz" : String (v / 1000.0f, 1) + " kHz"; };
    auto pct = [] (float v, int) { return String (roundToInt (v)) + " %"; };
    auto db  = [] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; };
    auto ms  = [] (float v, int) { return String (v, 2) + " ms"; };
    auto deg = [] (float v, int) { return String (roundToInt (v)) + String (CharPointer_UTF8 ("\xc2\xb0")); };
    auto chr = [] (float v, int) { return v < 34.0f ? "Warm " + String (roundToInt (v)) : v > 66.0f ? "Edgy " + String (roundToInt (v)) : "Mixed " + String (roundToInt (v)); };

    auto add = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, std::function<String (float, int)> f)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
                        AudioParameterFloatAttributes().withStringFromValueFunction (std::move (f))));
    };
    auto skewed = [] (float lo, float hi, float c) { NormalisableRange<float> r (lo, hi); r.setSkewForCentre (c); return r; };

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { IDs::monoOn, 1 }, "Mono Bass", true));
    add (IDs::monoHz,    "Mono Below",  skewed (20.0f, 500.0f, 120.0f), 120.0f, hz);
    add (IDs::width,     "Width",       { 0.0f, 200.0f }, 100.0f, pct);
    add (IDs::sub,       "Sub",         { -12.0f, 12.0f, 0.1f }, 0.0f, db);
    add (IDs::output,    "Output",      { -18.0f, 12.0f, 0.1f }, 0.0f, db);
    add (IDs::harmAmt,   "Harmonics",   { 0.0f, 100.0f }, 0.0f, pct);
    add (IDs::harmFocus, "Harm Focus",  skewed (40.0f, 300.0f, 100.0f), 100.0f, hz);
    add (IDs::harmChar,  "Character",   { 0.0f, 100.0f }, 40.0f, chr);
    add (IDs::harmTone,  "Harm Tone",   skewed (300.0f, 6000.0f, 1500.0f), 1500.0f, hz);
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { IDs::listen, 1 }, "Listen Harmonics", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { IDs::phaseOn, 1 }, "Phase Align", false));
    add (IDs::phaseRot,  "Phase Rotation", { 0.0f, 360.0f }, 0.0f, deg);
    add (IDs::phaseDly,  "Phase Delay",    { 0.0f, 10.0f }, 0.0f, ms);
    add (IDs::phaseFoc,  "Phase Focus",    skewed (40.0f, 400.0f, 120.0f), 150.0f, hz);
    return layout;
}
