#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace IDs
{
    inline constexpr const char* threshold = "threshold";
    inline constexpr const char* ratio     = "ratio";
    inline constexpr const char* knee      = "knee";
    inline constexpr const char* attack    = "attack";
    inline constexpr const char* release   = "release";
    inline constexpr const char* autoRel   = "autorel";
    inline constexpr const char* range     = "range";
    inline constexpr const char* lookahead = "lookahead";
    inline constexpr const char* link      = "link";
    inline constexpr const char* scHp      = "schp";
    inline constexpr const char* scLp      = "sclp";
    inline constexpr const char* scSource  = "scsource";
    inline constexpr const char* scListen  = "sclisten";
    inline constexpr const char* style     = "style";
    inline constexpr const char* channel   = "channel";
    inline constexpr const char* drive     = "drive";
    inline constexpr const char* makeup    = "makeup";
    inline constexpr const char* autoGain  = "autogain";
    inline constexpr const char* mix       = "mix";
    inline constexpr const char* output    = "output";
    inline constexpr const char* phaseOn   = "phaseon";
    inline constexpr const char* phaseRot  = "phaserot";
    inline constexpr const char* phaseDly  = "phasedelay";
    inline constexpr const char* phaseFoc  = "phasefocus";
}

inline const juce::StringArray& styleChoices()
{
    static const juce::StringArray c { "Clean", "Opto", "FET", "Vari-Mu" };
    return c;
}
inline const juce::StringArray& channelChoices()
{
    static const juce::StringArray c { "Stereo", "Mid/Side" };
    return c;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto hz   = [] (float v, int) { return v < 1000.0f ? String (roundToInt (v)) + " Hz" : String (v / 1000.0f, 1) + " kHz"; };
    auto hpHz = [hz] (float v, int n) { return v <= 21.0f ? String ("Off") : hz (v, n); };
    auto lpHz = [hz] (float v, int n) { return v >= 19900.0f ? String ("Off") : hz (v, n); };
    auto ms   = [] (float v, int) { return v < 1.0f ? String (v, 2) + " ms" : v < 10.0f ? String (v, 1) + " ms"
                                         : v < 1000.0f ? String (roundToInt (v)) + " ms" : String (v / 1000.0f, 2) + " s"; };
    auto pct  = [] (float v, int) { return String (roundToInt (v)) + " %"; };
    auto db   = [] (float v, int) { return String (v, 1) + " dB"; };
    auto rat  = [] (float v, int) { return v >= 19.99f ? String (CharPointer_UTF8 ("\xe2\x88\x9e : 1")) : String (v, v < 10.0f ? 1 : 0) + " : 1"; };
    auto rng  = [] (float v, int) { return "-" + String (v, 0) + " dB"; };

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

    add (IDs::threshold, "Threshold", { -60.0f, 0.0f }, -18.0f, db);
    add (IDs::ratio,     "Ratio",     skewed (1.0f, 20.0f, 4.0f), 4.0f, rat);
    add (IDs::knee,      "Knee",      { 0.0f, 24.0f }, 6.0f, db);
    add (IDs::attack,    "Attack",    skewed (0.02f, 200.0f, 10.0f), 10.0f, ms);
    add (IDs::release,   "Release",   skewed (5.0f, 2000.0f, 150.0f), 120.0f, ms);
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { IDs::autoRel, 1 }, "Auto Release", false));
    add (IDs::range,     "Range",     { 0.0f, 60.0f }, 40.0f, rng);
    add (IDs::lookahead, "Lookahead", { 0.0f, 10.0f }, 0.0f, ms);
    add (IDs::link,      "Stereo Link", { 0.0f, 100.0f }, 100.0f, pct);
    add (IDs::scHp,      "SC High-Pass", skewed (20.0f, 1000.0f, 120.0f), 20.0f, hpHz);
    add (IDs::scLp,      "SC Low-Pass",  skewed (1000.0f, 20000.0f, 6000.0f), 20000.0f, lpHz);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { IDs::scSource, 1 }, "SC Source", StringArray { "Internal", "External" }, 0));
    layout.add (std::make_unique<AudioParameterBool>   (ParameterID { IDs::scListen, 1 }, "SC Listen", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { IDs::style, 1 }, "Style", styleChoices(), 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { IDs::channel, 1 }, "Channel Mode", channelChoices(), 0));
    add (IDs::drive,     "Drive",     { 0.0f, 100.0f }, 0.0f, pct);
    add (IDs::makeup,    "Makeup",    { -12.0f, 24.0f }, 0.0f, db);
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { IDs::autoGain, 1 }, "Auto Gain", false));
    add (IDs::mix,       "Mix",       { 0.0f, 100.0f }, 100.0f, pct);
    add (IDs::output,    "Output",    { -24.0f, 12.0f }, 0.0f, db);

    // Kick / bass phase alignment (bass = main input, kick = sidechain)
    auto deg = [] (float v, int) { return String (roundToInt (v)) + String (CharPointer_UTF8 ("\xc2\xb0")); };
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { IDs::phaseOn, 1 }, "Phase Align", false));
    add (IDs::phaseRot, "Phase Rotation", { 0.0f, 360.0f }, 0.0f, deg);
    add (IDs::phaseDly, "Phase Delay",    { 0.0f, 10.0f }, 0.0f, ms);
    add (IDs::phaseFoc, "Phase Focus",    skewed (40.0f, 400.0f, 120.0f), 150.0f, hz);
    return layout;
}
