#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace IDs
{
    inline const juce::String depth = "depth", time = "time", upward = "upward", downward = "downward", in = "in", out = "out",
        x1 = "x1", x2 = "x2", style = "style", adaptive = "adaptive", maxBoost = "max_boost", floor = "noise_floor", link = "link",
        autoGain = "auto_gain", os = "os", clip = "clip";
    inline juce::String bp (int b, const char* what) { static const char* pre[] { "l_", "m_", "h_" }; return juce::String (pre[b]) + what; }
    inline juce::String dthr (int b) { return bp (b, "down"); }
    inline juce::String uthr (int b) { return bp (b, "up"); }
    inline juce::String gain (int b) { return bp (b, "gain"); }
    inline juce::String solo (int b) { return bp (b, "solo"); }
    inline juce::String byp (int b)  { return bp (b, "bypass"); }
}

// classic OTT (Ableton Multiband Dynamics "OTT"): thresholds, ratios, band output gains
struct BandDefaults { float down, up, gain, ratio; };
inline const BandDefaults& bandDefaults (int b)
{
    static const BandDefaults d[3] { { -33.8f, -40.8f, 10.3f, 66.7f }, { -30.2f, -41.8f, 5.7f, 66.7f }, { -35.8f, -40.8f, 10.3f, 1000.0f } };
    return d[b];
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const String& id) { return ParameterID { id, 2 }; };
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; });
    auto db = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.04f ? "+" : "") + String (v, 1) + " dB"; });
    auto hz = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (v, 1) + " Hz"; });
    auto F = [&] (const String& id, const String& nm, NormalisableRange<float> r, float d, AudioParameterFloatAttributes a) { l.add (std::make_unique<AudioParameterFloat> (pid (id), nm, r, d, a)); };

    F (IDs::depth, "Depth", { 0.0f, 1.0f }, 1.0f, pct);
    F (IDs::time, "Time", { 0.0f, 1.0f }, 0.5f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (std::pow (2.0f, (v - 0.5f) * 4.0f), 2) + " x"; }));
    F (IDs::upward, "Upward", { 0.0f, 1.5f }, 1.0f, pct);
    F (IDs::downward, "Downward", { 0.0f, 1.5f }, 1.0f, pct);
    F (IDs::in, "In Gain", { -24.0f, 24.0f, 0.1f }, 0.0f, db);
    F (IDs::out, "Out Gain", { -24.0f, 24.0f, 0.1f }, 0.0f, db);
    NormalisableRange<float> r1 (40.0f, 600.0f); r1.setSkewForCentre (120.0f);
    NormalisableRange<float> r2 (600.0f, 12000.0f); r2.setSkewForCentre (2500.0f);
    F (IDs::x1, "Low / Mid", r1, 88.3f, hz);
    F (IDs::x2, "Mid / High", r2, 2500.0f, hz);
    const char* bn[] { "Low", "Mid", "High" };
    for (int b = 0; b < 3; ++b)
    {
        const auto& d = bandDefaults (b); const String n (bn[b]);
        F (IDs::dthr (b), n + " Down Threshold", { -60.0f, 0.0f, 0.1f }, d.down, db);
        F (IDs::uthr (b), n + " Up Threshold", { -80.0f, 0.0f, 0.1f }, d.up, db);
        F (IDs::gain (b), n + " Gain", { -24.0f, 24.0f, 0.1f }, d.gain, db);
        l.add (std::make_unique<AudioParameterBool> (pid (IDs::solo (b)), n + " Solo", false));
        l.add (std::make_unique<AudioParameterBool> (pid (IDs::byp (b)), n + " Bypass", false));
    }
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::style), "Character", StringArray { "Clean", "Classic", "Smash" }, 0));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::adaptive), "Adaptive Thresholds", true));
    F (IDs::maxBoost, "Max Boost", { 0.0f, 36.0f, 0.5f }, 24.0f, db);
    F (IDs::floor, "Noise Guard", { -100.0f, -40.0f, 0.5f }, -65.0f,
       AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v <= -99.5f ? String ("Off") : String (v, 0) + " dB"; }));
    F (IDs::link, "Stereo Link", { 0.0f, 1.0f }, 1.0f, pct);
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::autoGain), "Auto Gain", false));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::os), "Oversampling", StringArray { "1x", "2x", "4x" }, 0));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::clip), "Soft Clip", false));
    return l;
}

struct OPreset { const char* name; std::vector<std::pair<const char*, float>> v; };
inline const std::vector<OPreset>& factoryPresets()
{
    static const std::vector<OPreset> p {
        { "OTT 100% (default)", {} },
        { "OTT 40% - everyday", { { "depth", 0.4f } } },
        { "OTT Classic (absolute)", { { "adaptive", 0 }, { "style", 1 } } },
        { "Master Glue 15%", { { "depth", 0.15f }, { "time", 0.7f }, { "auto_gain", 1 }, { "noise_floor", -55.0f } } },
        { "Synth Lead: Bright & Loud", { { "depth", 0.5f }, { "h_gain", 13.0f }, { "time", 0.4f }, { "clip", 1 } } },
        { "Supersaw Wall", { { "depth", 0.65f }, { "time", 0.6f }, { "l_gain", 7.0f }, { "h_gain", 12.0f } } },
        { "Pluck: Long Tails", { { "depth", 0.6f }, { "upward", 1.3f }, { "downward", 0.6f }, { "time", 0.35f } } },
        { "Bass: Tight & Present", { { "depth", 0.5f }, { "x1", 120.0f }, { "x2", 1500.0f }, { "l_gain", 7.0f }, { "m_gain", 8.0f }, { "h_gain", 4.0f } } },
        { "808: More Sustain", { { "depth", 0.45f }, { "x1", 110.0f }, { "downward", 0.4f }, { "upward", 1.2f }, { "time", 0.7f } } },
        { "Drums: Smack Bus", { { "depth", 0.35f }, { "style", 2 }, { "time", 0.3f }, { "clip", 1 } } },
        { "Drums: Room Explode", { { "depth", 0.6f }, { "upward", 1.4f }, { "downward", 0.4f }, { "time", 0.6f } } },
        { "Vocal: Air & Presence", { { "depth", 0.25f }, { "time", 0.7f }, { "h_gain", 12.0f }, { "l_gain", 4.0f }, { "noise_floor", -55.0f } } },
        { "FX / Risers: Huge", { { "depth", 1.0f }, { "upward", 1.5f }, { "max_boost", 36.0f } } },
        { "Downward Only (tame)", { { "upward", 0.0f }, { "l_gain", 4.0f }, { "m_gain", 2.0f }, { "h_gain", 4.0f } } },
        { "Upward Only (detail)", { { "depth", 0.6f }, { "downward", 0.0f }, { "l_gain", 0.0f }, { "m_gain", 0.0f }, { "h_gain", 0.0f }, { "noise_floor", -55.0f } } },
    };
    return p;
}
