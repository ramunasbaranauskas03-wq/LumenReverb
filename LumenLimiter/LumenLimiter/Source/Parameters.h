#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace IDs
{
    inline constexpr const char* gain      = "gain";
    inline constexpr const char* ceiling   = "ceiling";
    inline constexpr const char* release   = "release";
    inline constexpr const char* autoRel   = "autorel";
    inline constexpr const char* lookahead = "lookahead";
    inline constexpr const char* link      = "link";
    inline constexpr const char* style     = "style";
    inline constexpr const char* clipOn    = "clipon";
    inline constexpr const char* clipLevel = "cliplevel";
    inline constexpr const char* clipShape = "clipshape";
    inline constexpr const char* dither    = "dither";
    inline constexpr const char* target    = "target";
    inline constexpr const char* customLufs= "customlufs";
    inline constexpr const char* delta     = "delta";
    inline constexpr const char* gainMatch = "gainmatch";
    inline constexpr const char* oversample= "oversample";
}

struct LoudnessTarget { const char* name; float lufs; float tp; bool normalises; };
inline const std::vector<LoudnessTarget>& targets()
{
    static const std::vector<LoudnessTarget> t {
        { "Spotify",         -14.0f, -1.0f, true  },
        { "Apple Music",     -16.0f, -1.0f, true  },
        { "YouTube",         -14.0f, -1.0f, true  },
        { "Amazon Music",    -14.0f, -2.0f, true  },
        { "Tidal",           -14.0f, -1.0f, true  },
        { "SoundCloud",       -9.0f, -1.0f, false },
        { "Club / Beatport",  -7.0f, -0.3f, false },
        { "Custom",            0.0f,  0.0f, false },
    };
    return t;
}
inline juce::StringArray targetNames() { juce::StringArray a; for (auto& t : targets()) a.add (t.name); return a; }

inline const juce::StringArray& styleChoices()
{
    static const juce::StringArray c { "Transparent", "Balanced", "Loud" };
    return c;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto dbtp = [] (float v, int) { return String (v, 1) + " dBTP"; };
    auto pdb  = [] (float v, int) { return "+" + String (v, 1) + " dB"; };
    auto lu   = [] (float v, int) { return String (v, 1) + " LUFS"; };
    auto ms   = [] (float v, int) { return v < 10.0f ? String (v, 1) + " ms" : v < 1000.0f ? String (roundToInt (v)) + " ms" : String (v / 1000.0f, 2) + " s"; };
    auto pct  = [] (float v, int) { return String (roundToInt (v)) + " %"; };

    auto add = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, std::function<String (float, int)> f)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
                        AudioParameterFloatAttributes().withStringFromValueFunction (std::move (f))));
    };
    auto skewed = [] (float lo, float hi, float c) { NormalisableRange<float> r (lo, hi); r.setSkewForCentre (c); return r; };

    add (IDs::gain,      "Gain",      { 0.0f, 24.0f, 0.01f }, 0.0f, pdb);
    add (IDs::ceiling,   "Ceiling",   { -12.0f, 0.0f, 0.01f }, -1.0f, dbtp);
    add (IDs::release,   "Release",   skewed (1.0f, 1000.0f, 80.0f), 80.0f, ms);
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { IDs::autoRel, 1 }, "Auto Release", true));
    add (IDs::lookahead, "Lookahead", { 0.5f, 5.0f, 0.1f }, 1.5f, ms);
    add (IDs::link,      "Stereo Link", { 0.0f, 100.0f }, 100.0f, pct);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { IDs::style, 1 }, "Style", styleChoices(), 1));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { IDs::clipOn, 1 }, "Clipper", false));
    add (IDs::clipLevel, "Clip Level", { 0.0f, 6.0f, 0.01f }, 2.0f, pdb);
    add (IDs::clipShape, "Clip Shape", { 0.0f, 100.0f }, 60.0f, pct);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { IDs::dither, 1 }, "Dither", StringArray { "Off", "24-bit", "16-bit" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { IDs::target, 1 }, "Target", targetNames(), 0));
    add (IDs::customLufs, "Custom Target", { -24.0f, -4.0f, 0.1f }, -9.0f, lu);
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { IDs::delta, 1 }, "Delta", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { IDs::gainMatch, 1 }, "Gain Match", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { IDs::oversample, 1 }, "Oversampling",
                                                        StringArray { "1x", "2x", "4x", "8x", "16x" }, 2));
    return layout;
}
