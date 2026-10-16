#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace IDs
{
    inline const juce::String depth = "depth", time = "time", upward = "upward", downward = "downward", in = "in", out = "out",
        x1 = "x1", x2 = "x2", style = "style", detect = "detect", knee = "knee", link = "link", maxBoost = "max_boost", floor = "noise_floor",
        lock = "level_lock", autoGain = "auto_gain", os = "os", clip = "clip";
    inline juce::String bp (int b, const char* what) { static const char* pre[] { "l_", "m_", "h_" }; return juce::String (pre[b]) + what; }
    inline juce::String dthr (int b) { return bp (b, "down"); }
    inline juce::String uthr (int b) { return bp (b, "up"); }
    inline juce::String bin (int b)  { return bp (b, "in"); }
    inline juce::String bout (int b) { return bp (b, "out"); }
    inline juce::String solo (int b) { return bp (b, "solo"); }
    inline juce::String byp (int b)  { return bp (b, "bypass"); }
}

// classic OTT (Ableton Multiband Dynamics "OTT" preset)
struct BandDefaults { float down, up, in, out, ratio; };
inline const BandDefaults& bandDefaults (int b)
{
    static const BandDefaults d[3] { { -33.8f, -40.8f, 5.2f, 10.3f, 66.7f }, { -30.2f, -41.8f, 5.2f, 5.7f, 66.7f }, { -35.8f, -40.8f, 5.2f, 10.3f, 1000.0f } };
    return d[b];
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const String& id) { return ParameterID { id, 1 }; };
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v * 100.0f, 0) + "%"; });
    auto db = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.04f ? "+" : "") + String (v, 1) + " dB"; });
    auto hz = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (v, 1) + " Hz"; });

    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::depth), "Depth", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::time), "Time", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::upward), "Upward", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::downward), "Downward", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::in), "Input", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, db));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::out), "Output", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, db));
    NormalisableRange<float> r1 (40.0f, 600.0f); r1.setSkewForCentre (120.0f);
    NormalisableRange<float> r2 (600.0f, 12000.0f); r2.setSkewForCentre (2500.0f);
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::x1), "Low / Mid", r1, 88.3f, hz));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::x2), "Mid / High", r2, 2500.0f, hz));
    const char* bn[] { "Low", "Mid", "High" };
    for (int b = 0; b < 3; ++b)
    {
        const auto& d = bandDefaults (b);
        const String n (bn[b]);
        l.add (std::make_unique<AudioParameterFloat> (pid (IDs::dthr (b)), n + " Down Threshold", NormalisableRange<float> (-60.0f, 0.0f, 0.1f), d.down, db));
        l.add (std::make_unique<AudioParameterFloat> (pid (IDs::uthr (b)), n + " Up Threshold", NormalisableRange<float> (-80.0f, 0.0f, 0.1f), d.up, db));
        l.add (std::make_unique<AudioParameterFloat> (pid (IDs::bin (b)), n + " Input", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), d.in, db));
        l.add (std::make_unique<AudioParameterFloat> (pid (IDs::bout (b)), n + " Output", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), d.out, db));
        l.add (std::make_unique<AudioParameterBool> (pid (IDs::solo (b)), n + " Solo", false));
        l.add (std::make_unique<AudioParameterBool> (pid (IDs::byp (b)), n + " Bypass", false));
    }
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::style), "Timing Style", StringArray { "Classic", "Smack", "Smooth" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::detect), "Detection", StringArray { "Peak", "RMS" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::knee), "Knee", NormalisableRange<float> (0.0f, 1.0f), 0.3f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::link), "Stereo Link", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::maxBoost), "Max Boost", NormalisableRange<float> (0.0f, 40.0f, 0.5f), 30.0f, db));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::floor), "Noise Guard", NormalisableRange<float> (-100.0f, -40.0f, 0.5f), -75.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v <= -99.5f ? String ("Off") : String (v, 0) + " dB"; })));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::lock), "Level Lock", false));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::autoGain), "Auto Gain", false));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::os), "Oversampling", StringArray { "1x", "2x", "4x" }, 0));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::clip), "Soft Clip", false));
    return l;
}

struct OPreset { const char* name; std::vector<std::pair<const char*, float>> v; };
inline const std::vector<OPreset>& factoryPresets()
{
    static const std::vector<OPreset> p {
        { "Classic OTT", {} },
        { "Classic OTT 30%", { { "depth", 0.3f } } },
        { "Master Glue 12%", { { "depth", 0.12f }, { "style", 2 }, { "noise_floor", -60.0f }, { "auto_gain", 1 } } },
        { "Synth Lead: Bright & Loud", { { "depth", 0.45f }, { "h_out", 12.0f }, { "time", 0.4f }, { "clip", 1 } } },
        { "Supersaw Wall", { { "depth", 0.6f }, { "time", 0.6f }, { "l_out", 6.0f }, { "h_out", 11.0f } } },
        { "Pluck: More Tail", { { "depth", 0.5f }, { "upward", 1.0f }, { "downward", 0.5f }, { "time", 0.35f } } },
        { "Bass: Tight & Present", { { "depth", 0.5f }, { "x1", 120.0f }, { "x2", 1500.0f }, { "l_out", 7.0f }, { "m_out", 8.0f }, { "h_out", 4.0f }, { "style", 1 } } },
        { "808: Sustain", { { "depth", 0.4f }, { "x1", 110.0f }, { "upward", 1.0f }, { "downward", 0.3f }, { "time", 0.7f } } },
        { "Drums: Smack Bus", { { "depth", 0.35f }, { "style", 1 }, { "time", 0.3f }, { "clip", 1 } } },
        { "Drums: Room Explode", { { "depth", 0.6f }, { "upward", 1.0f }, { "downward", 0.4f }, { "time", 0.6f } } },
        { "Vocal: Air & Presence", { { "depth", 0.25f }, { "style", 2 }, { "detect", 1 }, { "h_out", 12.0f }, { "l_out", 4.0f }, { "noise_floor", -60.0f } } },
        { "FX / Risers: Huge", { { "depth", 1.0f }, { "time", 0.6f }, { "max_boost", 36.0f } } },
        { "Downward Only (Tame)", { { "depth", 1.0f }, { "upward", 0.0f }, { "l_out", 4.0f }, { "m_out", 2.0f }, { "h_out", 4.0f } } },
        { "Upward Only (Detail)", { { "depth", 0.6f }, { "downward", 0.0f }, { "l_out", 0.0f }, { "m_out", 0.0f }, { "h_out", 0.0f }, { "noise_floor", -60.0f } } },
        { "Level-Locked OTT 40%", { { "depth", 0.4f }, { "level_lock", 1 } } },
    };
    return p;
}
