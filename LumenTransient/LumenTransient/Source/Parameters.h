#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace IDs
{
    inline const juce::String bands = "bands", lookahead = "lookahead", mix = "mix", out = "out", link = "link",
                              clip = "clip", autoGain = "auto_gain", sc = "sidechain", delta = "delta", os = "os";
    inline juce::String x (int k)    { return "x" + juce::String (k + 1); }
    inline juce::String bp (const char* what, int b) { return juce::String (what) + juce::String (b + 1); }
    inline juce::String punch (int b)  { return bp ("pk", b); }
    inline juce::String body (int b)   { return bp ("bd", b); }
    inline juce::String sus (int b)    { return bp ("su", b); }
    inline juce::String gate (int b)   { return bp ("gt", b); }
    inline juce::String drive (int b)  { return bp ("dr", b); }
    inline juce::String time (int b)   { return bp ("tm", b); }
    inline juce::String gain (int b)   { return bp ("gn", b); }
    inline juce::String amount (int b) { return bp ("am", b); }
    inline juce::String solo (int b)   { return bp ("solo", b); }
    inline juce::String byp (int b)    { return bp ("byp", b); }
}

inline const char* bandName (int b, int nb)
{
    static const char* n4[] = { "LOW", "LO MID", "HI MID", "HIGH" };
    static const char* n3[] = { "LOW", "MID", "HIGH", "" };
    static const char* n2[] = { "LOW", "HIGH", "", "" };
    return nb >= 4 ? n4[b] : nb == 3 ? n3[b] : nb == 2 ? n2[b] : (b == 0 ? "FULL BAND" : "");
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const String& id) { return ParameterID { id, 1 }; };
    auto bip = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.004f ? "+" : "") + String (v * 100.0f, 0) + "%"; });
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v * 100.0f, 0) + "%"; });
    auto hz = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : String ((int) std::round (v)) + " Hz"; });
    auto db = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.04f ? "+" : "") + String (v, 1) + " dB"; });
    auto tm = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v < 0.25f ? "Snappy " : v < 0.6f ? "Medium " : "Slow ") + String (v * 100.0f, 0) + "%"; });

    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::bands), "Bands", StringArray { "1 Band", "2 Bands", "3 Bands", "4 Bands" }, 3));
    const float xd[] = { 120.0f, 1000.0f, 6000.0f }, xlo[] = { 20.0f, 150.0f, 1000.0f }, xhi[] = { 1000.0f, 8000.0f, 18000.0f };
    const char* xn[] = { "Crossover 1", "Crossover 2", "Crossover 3" };
    for (int k = 0; k < 3; ++k)
    {
        NormalisableRange<float> r (xlo[k], xhi[k]); r.setSkewForCentre (std::sqrt (xlo[k] * xhi[k]));
        l.add (std::make_unique<AudioParameterFloat> (pid (IDs::x (k)), xn[k], r, xd[k], hz));
    }
    const char* bn[] = { "Low", "Lo Mid", "Hi Mid", "High" };
    for (int b = 0; b < 4; ++b)
    {
        const String n (bn[b]);
        l.add (std::make_unique<AudioParameterFloat> (pid (IDs::punch (b)), n + " Punch", NormalisableRange<float> (-1.0f, 1.0f), 0.0f, bip));
        l.add (std::make_unique<AudioParameterFloat> (pid (IDs::body (b)), n + " Body", NormalisableRange<float> (-1.0f, 1.0f), 0.0f, bip));
        l.add (std::make_unique<AudioParameterFloat> (pid (IDs::sus (b)), n + " Sustain", NormalisableRange<float> (-1.0f, 1.0f), 0.0f, bip));
        l.add (std::make_unique<AudioParameterFloat> (pid (IDs::gate (b)), n + " Gate", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct));
        l.add (std::make_unique<AudioParameterFloat> (pid (IDs::drive (b)), n + " Drive", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct));
        l.add (std::make_unique<AudioParameterFloat> (pid (IDs::time (b)), n + " Time", NormalisableRange<float> (0.0f, 1.0f), b == 0 ? 0.5f : b == 3 ? 0.25f : 0.4f, tm));
        l.add (std::make_unique<AudioParameterFloat> (pid (IDs::gain (b)), n + " Gain", NormalisableRange<float> (-18.0f, 12.0f, 0.1f), 0.0f, db));
        l.add (std::make_unique<AudioParameterFloat> (pid (IDs::amount (b)), n + " Amount", NormalisableRange<float> (0.0f, 2.0f), 1.0f, pct));
        l.add (std::make_unique<AudioParameterBool> (pid (IDs::solo (b)), n + " Solo", false));
        l.add (std::make_unique<AudioParameterBool> (pid (IDs::byp (b)), n + " Bypass", false));
    }
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::lookahead), "Lookahead", NormalisableRange<float> (0.0f, 5.0f, 0.1f), 0.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 0.05f ? String ("Off") : String (v, 1) + " ms"; })));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::link), "Stereo Link", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::mix), "Mix", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::out), "Output", NormalisableRange<float> (-18.0f, 12.0f, 0.1f), 0.0f, db));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::clip), "Soft Clip", false));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::autoGain), "Auto Gain", false));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::sc), "Sidechain", false));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::delta), "Delta", false));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::os), "Oversampling", StringArray { "Off", "2x", "4x" }, 0));
    return l;
}

struct TPreset { const char* name; std::vector<std::pair<const char*, float>> v; };
inline const std::vector<TPreset>& factoryPresets()
{
    static const std::vector<TPreset> p {
        { "Init", {} },
        { "Kick: Punch + Knock", { { "pk1", 0.55f }, { "bd1", 0.25f }, { "su1", -0.15f }, { "pk2", 0.35f }, { "pk3", 0.2f } } },
        { "Kick: Short & Tight", { { "pk1", 0.3f }, { "su1", -0.55f }, { "su2", -0.5f }, { "gt2", 0.3f }, { "su3", -0.3f } } },
        { "Kick: Boom Tail", { { "su1", 0.5f }, { "bd1", 0.3f }, { "pk3", 0.2f }, { "tm1", 0.7f } } },
        { "Snare: Crack", { { "pk2", 0.4f }, { "pk3", 0.6f }, { "bd2", 0.3f }, { "su4", -0.15f }, { "lookahead", 0.5f } } },
        { "Snare: Fat Body", { { "bd1", 0.4f }, { "bd2", 0.55f }, { "su2", 0.2f }, { "dr2", 0.25f } } },
        { "Clap: Snap & Dry", { { "pk2", 0.4f }, { "pk3", 0.5f }, { "su2", -0.5f }, { "su3", -0.4f }, { "gt3", 0.25f } } },
        { "Hats: Bite", { { "pk4", 0.55f }, { "su4", -0.3f }, { "pk3", 0.25f }, { "tm4", 0.1f } } },
        { "Hats: Smooth", { { "pk4", -0.45f }, { "pk3", -0.25f }, { "lookahead", 1.5f } } },
        { "Drum Bus: Punch", { { "pk1", 0.35f }, { "pk2", 0.3f }, { "pk3", 0.25f }, { "pk4", 0.15f }, { "su1", -0.1f }, { "lookahead", 1.0f }, { "clip", 1 } } },
        { "Drum Bus: Glue & Tight", { { "pk1", 0.2f }, { "su1", -0.3f }, { "su2", -0.25f }, { "su3", -0.15f }, { "dr1", 0.15f }, { "auto_gain", 1 } } },
        { "Drum Bus: Room Killer", { { "su1", -0.4f }, { "su2", -0.7f }, { "su3", -0.7f }, { "su4", -0.5f }, { "gt2", 0.4f }, { "gt3", 0.4f } } },
        { "Drum Bus: Big Room", { { "su2", 0.5f }, { "su3", 0.5f }, { "su4", 0.35f }, { "bd1", 0.3f } } },
        { "Bass: Tighten Lows", { { "bands", 1 }, { "x1", 140 }, { "su1", -0.45f }, { "pk2", 0.3f } } },
        { "Bass: Pluck Definition", { { "pk2", 0.45f }, { "pk3", 0.35f }, { "su1", -0.2f } } },
        { "Synth Pluck: Snap", { { "pk2", 0.4f }, { "pk3", 0.5f }, { "pk4", 0.3f }, { "su2", -0.25f }, { "su3", -0.25f } } },
        { "Pads: Remove Attack", { { "pk1", -0.4f }, { "pk2", -0.5f }, { "pk3", -0.6f }, { "pk4", -0.6f }, { "su2", 0.3f }, { "lookahead", 3.0f } } },
        { "Guitar: Pick Attack", { { "pk3", 0.5f }, { "pk4", 0.3f }, { "bd2", 0.2f } } },
        { "Vocal: De-Plosive", { { "bands", 1 }, { "x1", 200 }, { "pk1", -0.7f }, { "bd1", -0.4f }, { "lookahead", 3.0f } } },
        { "Loop: Make It Hit", { { "pk1", 0.4f }, { "pk2", 0.4f }, { "pk3", 0.4f }, { "pk4", 0.3f }, { "su1", -0.2f }, { "su2", -0.2f }, { "su3", -0.2f }, { "su4", -0.2f }, { "clip", 1 }, { "lookahead", 1.0f } } },
        { "Parallel Smash", { { "pk1", 0.9f }, { "pk2", 0.9f }, { "pk3", 0.8f }, { "pk4", 0.6f }, { "dr1", 0.4f }, { "dr2", 0.4f }, { "mix", 0.45f } } },
    };
    return p;
}
