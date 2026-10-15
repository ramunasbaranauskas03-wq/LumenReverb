#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace IDs
{
    inline const juce::String bands = "bands", mode = "mode", width = "width", monoBelow = "mono_below", safe = "safe", mix = "mix", out = "out",
                              monoCheck = "mono_check", sideOnly = "side_only";
    inline juce::String x (int k)      { return "x" + juce::String (k + 1); }
    inline juce::String w (int b)      { return "w" + juce::String (b + 1); }
    inline juce::String spread (int b) { return "sp" + juce::String (b + 1); }
    inline juce::String gain (int b)   { return "g" + juce::String (b + 1); }
    inline juce::String solo (int b)   { return "solo" + juce::String (b + 1); }
    inline juce::String mute (int b)   { return "mute" + juce::String (b + 1); }
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
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v * 100.0f, 0) + "%"; });
    auto hz = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : String ((int) std::round (v)) + " Hz"; });
    auto db = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.04f ? "+" : "") + String (v, 1) + " dB"; });

    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::bands), "Bands", StringArray { "1 Band", "2 Bands", "3 Bands", "4 Bands" }, 3));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::mode), "Crossover Mode", StringArray { "Min Phase", "Linear Phase" }, 0));
    const float xd[] = { 150.0f, 1500.0f, 7000.0f }, xlo[] = { 20.0f, 150.0f, 1000.0f }, xhi[] = { 1000.0f, 8000.0f, 18000.0f };
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
        l.add (std::make_unique<AudioParameterFloat> (pid (IDs::w (b)), n + " Width", NormalisableRange<float> (0.0f, 2.0f, 0.005f), 1.0f, pct));
        l.add (std::make_unique<AudioParameterFloat> (pid (IDs::spread (b)), n + " Spread", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct));
        l.add (std::make_unique<AudioParameterFloat> (pid (IDs::gain (b)), n + " Gain", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, db));
        l.add (std::make_unique<AudioParameterBool> (pid (IDs::solo (b)), n + " Solo", false));
        l.add (std::make_unique<AudioParameterBool> (pid (IDs::mute (b)), n + " Mute", false));
    }
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::width), "Global Width", NormalisableRange<float> (0.0f, 2.0f, 0.005f), 1.0f, pct));
    NormalisableRange<float> mb (0.0f, 500.0f); mb.setSkewForCentre (120.0f);
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::monoBelow), "Mono Below", mb, 0.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 20.0f ? String ("Off") : String ((int) std::round (v)) + " Hz"; })));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::safe), "Phase Safe", NormalisableRange<float> (-1.0f, 0.5f, 0.01f), -1.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v <= -0.99f ? String ("Off") : "> " + String (v, 2); })));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::mix), "Mix", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::out), "Output", NormalisableRange<float> (-18.0f, 12.0f, 0.1f), 0.0f, db));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::monoCheck), "Mono Check", false));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::sideOnly), "Side Only", false));
    return l;
}

struct SPreset { const char* name; std::vector<std::pair<const char*, float>> v; };
inline const std::vector<SPreset>& factoryPresets()
{
    static const std::vector<SPreset> p {
        { "Init", {} },
        { "Master: Clean Wide", { { "w1", 0.0f }, { "w2", 1.0f }, { "w3", 1.15f }, { "w4", 1.25f }, { "mono_below", 110 }, { "safe", 0.0f } } },
        { "Master: Club Safe", { { "w1", 0.0f }, { "w2", 0.85f }, { "w3", 1.05f }, { "w4", 1.15f }, { "mono_below", 140 }, { "safe", 0.2f } } },
        { "Master: Huge Festival", { { "w1", 0.0f }, { "w2", 1.15f }, { "w3", 1.35f }, { "w4", 1.5f }, { "sp3", 0.2f }, { "sp4", 0.25f }, { "mono_below", 120 }, { "safe", 0.05f } } },
        { "Mono Bass Only", { { "w1", 0.0f }, { "mono_below", 120 } } },
        { "Supersaw: Wider", { { "w1", 0.3f }, { "w2", 1.3f }, { "w3", 1.5f }, { "w4", 1.5f }, { "safe", 0.0f } } },
        { "Lead: Mono to Wide", { { "w1", 0.0f }, { "sp2", 0.35f }, { "sp3", 0.55f }, { "sp4", 0.6f }, { "mono_below", 150 } } },
        { "Pluck: Air Spread", { { "sp3", 0.3f }, { "sp4", 0.55f }, { "w4", 1.3f } } },
        { "Vocal: Focus Center", { { "w2", 0.6f }, { "w3", 0.75f }, { "w4", 1.1f } } },
        { "Pad: Ultra Wide", { { "w1", 0.2f }, { "w2", 1.6f }, { "w3", 1.8f }, { "w4", 1.8f }, { "sp2", 0.2f }, { "sp3", 0.3f }, { "safe", -0.2f } } },
        { "Drums: Tight Center, Wide Air", { { "w1", 0.0f }, { "w2", 0.7f }, { "w3", 1.0f }, { "w4", 1.4f }, { "mono_below", 150 } } },
        { "Hats: Spread", { { "bands", 1 }, { "x1", 5000 }, { "w2", 1.5f }, { "sp2", 0.4f } } },
        { "Fix: Phasey Stereo", { { "safe", 0.3f }, { "w1", 0.0f }, { "w2", 0.7f }, { "mono_below", 160 } } },
        { "Narrow: Make It Mono-ish", { { "width", 0.35f } } },
        { "Linear: Mastering Wide", { { "mode", 1 }, { "w1", 0.0f }, { "w2", 1.05f }, { "w3", 1.2f }, { "w4", 1.3f }, { "mono_below", 100 }, { "safe", 0.0f } } },
        { "Side Check (Solo Side)", { { "side_only", 1 } } },
    };
    return p;
}
