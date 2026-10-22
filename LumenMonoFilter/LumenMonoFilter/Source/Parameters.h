#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// =====================================================================================
// Lumen MonoFilter v2 — parameters
// =====================================================================================
namespace IDs
{
    inline const juce::String source = "source", polarity = "polarity", mode = "mode",
        monoOn = "mono_on", monoFreq = "mono_freq", monoSlope = "mono_slope", lowWidth = "low_width",
        align = "align", alignAuto = "align_auto", alignMs = "align_ms",
        stAmt = "st_amount", stMode = "st_mode", stFreq = "st_freq", stTime = "st_time",
        x1 = "x1", x2 = "x2", x3 = "x3", width = "width",
        balance = "balance", mix = "mix", out = "out", autoGain = "auto_gain", monitor = "monitor";
    inline juce::String w (int b) { return "w" + juce::String (b + 1); }
}

inline juce::StringArray sourceNames()   { return { "Stereo", "Swap L/R", "Left Only", "Right Only", "Mono Sum" }; }
inline juce::StringArray polarityNames() { return { "Normal", "Invert L", "Invert R" }; }
inline juce::StringArray modeNames()     { return { "Zero Latency", "Linear Phase" }; }
inline juce::StringArray slopeNames()    { return { "6 dB/oct", "12 dB/oct", "24 dB/oct", "48 dB/oct" }; }
inline juce::StringArray stModeNames()   { return { "Diffuse", "Haas" }; }
inline juce::StringArray monitorNames()  { return { "Stereo", "Mono", "Side", "Mono Lows", "Band 1", "Band 2", "Band 3", "Band 4", "Delta" }; }
enum Monitor { MonStereo, MonMono, MonSide, MonLows, MonB1, MonB2, MonB3, MonB4, MonDelta };

inline juce::String hzText (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String ((int) std::round (v)) + " Hz"; }

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const String& id) { return ParameterID { id, 2 }; };
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; })
                                              .withValueFromStringFunction ([] (const String& s) { return s.getFloatValue() / 100.0f; });
    auto db = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.04f ? "+" : "") + String (v, 1) + " dB"; });
    auto hz = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return hzText (v); })
                                             .withValueFromStringFunction ([] (const String& s) { return s.containsIgnoreCase ("k") ? s.getFloatValue() * 1000.0f : s.getFloatValue(); });
    auto ms = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " ms"; });
    auto range = [] (float a, float b, float centre) { NormalisableRange<float> r (a, b); r.setSkewForCentre (centre); return r; };

    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::source), "Source", sourceNames(), 0));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::polarity), "Polarity", polarityNames(), 0));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::mode), "Phase Mode", modeNames(), 0));

    l.add (std::make_unique<AudioParameterBool>  (pid (IDs::monoOn), "Mono Maker", true));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::monoFreq), "Mono Below", range (20.0f, 1000.0f, 150.0f), 120.0f, hz));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::monoSlope), "Mono Slope", slopeNames(), 2));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::lowWidth), "Bass Width", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f, pct));
    l.add (std::make_unique<AudioParameterBool>  (pid (IDs::align), "Bass Phase Align", false));
    l.add (std::make_unique<AudioParameterBool>  (pid (IDs::alignAuto), "Align Auto", true));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::alignMs), "Align Time", NormalisableRange<float> (-3.0f, 3.0f, 0.001f), 0.0f, ms));

    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::stAmt), "Stereoize", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f, pct));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::stMode), "Stereoize Mode", stModeNames(), 0));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::stFreq), "Stereoize From", range (80.0f, 4000.0f, 400.0f), 300.0f, hz));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::stTime), "Haas Time", NormalisableRange<float> (1.0f, 30.0f, 0.01f), 12.0f, ms));

    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::x1), "Crossover 1", range (40.0f, 1000.0f, 200.0f), 200.0f, hz));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::x2), "Crossover 2", range (250.0f, 6000.0f, 1500.0f), 2000.0f, hz));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::x3), "Crossover 3", range (1500.0f, 16000.0f, 6000.0f), 8000.0f, hz));
    const char* bandNames[4] { "Low Width", "Low-Mid Width", "High-Mid Width", "High Width" };
    for (int b = 0; b < 4; ++b)
        l.add (std::make_unique<AudioParameterFloat> (pid (IDs::w (b)), bandNames[b], NormalisableRange<float> (0.0f, 2.0f, 0.001f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::width), "Global Width", NormalisableRange<float> (0.0f, 2.0f, 0.001f), 1.0f, pct));

    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::balance), "Balance", NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                                                  { const int p = roundToInt (std::abs (v) * 100.0f); return p == 0 ? String ("C") : String (p) + (v < 0 ? " L" : " R"); })));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::mix), "Mix", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::out), "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, db));
    l.add (std::make_unique<AudioParameterBool>  (pid (IDs::autoGain), "Auto Gain", false));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::monitor), "Monitor", monitorNames(), 0));
    return l;
}

struct MPreset { const char* name; std::vector<std::pair<const char*, float>> v; };
inline const std::vector<MPreset>& factoryPresets()
{
    // slope: 0=6 1=12 2=24 3=48   mode: 0 zero latency, 1 linear   st_mode: 0 diffuse 1 haas
    static const std::vector<MPreset> p {
        { "Init: Mono Below 120 Hz", {} },
        { "Clean (Nothing Changed)", { { "mono_on", 0 } } },
        // ---- master / mix bus
        { "MASTER Club Safe 120 Hz", { { "mono_freq", 120 }, { "mode", 1 } } },
        { "MASTER Gentle Mono 80 Hz", { { "mono_freq", 80 }, { "mono_slope", 1 }, { "low_width", 0.15f }, { "mode", 1 } } },
        { "MASTER Vinyl Safe 150 Hz", { { "mono_freq", 150 }, { "mono_slope", 3 }, { "mode", 1 } } },
        { "MASTER Wide Top, Tight Bottom", { { "mono_freq", 110 }, { "w2", 1.0f }, { "w3", 1.15f }, { "w4", 1.3f }, { "mode", 1 } } },
        { "MASTER Streaming Mono Safe", { { "mono_freq", 140 }, { "w1", 0.85f }, { "w4", 1.1f }, { "mode", 1 } } },
        { "MASTER Festival Big", { { "mono_freq", 130 }, { "w2", 1.1f }, { "w3", 1.25f }, { "w4", 1.4f }, { "mode", 1 } } },
        // ---- bass / kick / 808
        { "BASS Sub Mono, Top Stereo", { { "mono_freq", 150 }, { "align", 1 } } },
        { "BASS Reese / Supersaw Fix", { { "mono_freq", 220 }, { "align", 1 }, { "w2", 1.15f } } },
        { "BASS 808 Pure Mono", { { "mono_freq", 400 }, { "mono_slope", 3 }, { "align", 1 } } },
        { "BASS Wide Mid-Bass Growl", { { "mono_freq", 100 }, { "x1", 300 }, { "w2", 1.4f }, { "w3", 1.4f }, { "st_amount", 0.25f }, { "st_freq", 400 } } },
        { "KICK Dead Centre", { { "mono_freq", 300 }, { "mono_slope", 3 } } },
        // ---- synths / leads / pads
        { "SYNTH Mono Lead to Wide", { { "mono_freq", 150 }, { "st_amount", 0.55f }, { "st_freq", 350 } } },
        { "SYNTH Pluck Spread", { { "mono_freq", 180 }, { "st_amount", 0.35f }, { "st_freq", 600 }, { "w3", 1.3f }, { "w4", 1.4f } } },
        { "SYNTH Huge Pad", { { "mono_freq", 200 }, { "st_amount", 0.4f }, { "w2", 1.3f }, { "w3", 1.5f }, { "w4", 1.5f } } },
        { "SYNTH Chords Club Ready", { { "mono_freq", 250 }, { "w2", 1.2f }, { "w3", 1.2f } } },
        { "SYNTH Haas Widener", { { "mono_freq", 150 }, { "st_amount", 0.5f }, { "st_mode", 1 }, { "st_time", 14 }, { "st_freq", 500 } } },
        // ---- vocals / fx
        { "VOX Centre Focus", { { "mono_freq", 200 }, { "w2", 0.7f }, { "w3", 0.85f } } },
        { "VOX Backing Wide", { { "mono_freq", 250 }, { "st_amount", 0.45f }, { "st_freq", 500 }, { "w3", 1.3f } } },
        { "FX Riser Super Wide", { { "mono_freq", 300 }, { "st_amount", 0.7f }, { "width", 1.4f } } },
        { "DRUMS Overheads Tame", { { "mono_freq", 150 }, { "low_width", 0.3f }, { "align", 1 }, { "w4", 1.15f } } },
        // ---- recordings / utility
        { "REC Spaced Pair Fix", { { "mono_freq", 180 }, { "low_width", 0.4f }, { "align", 1 } } },
        { "UTIL Left Channel Only", { { "source", 2 }, { "mono_on", 0 } } },
        { "UTIL Right Channel Only", { { "source", 3 }, { "mono_on", 0 } } },
        { "UTIL Swap L/R", { { "source", 1 }, { "mono_on", 0 } } },
        { "UTIL Full Mono", { { "width", 0.0f } } },
        { "UTIL Mono Check", { { "monitor", 1 } } },
        { "CREATIVE Narrow Lo-Fi", { { "mono_freq", 300 }, { "width", 0.5f }, { "w4", 0.6f } } },
        { "CREATIVE Extreme Wide 200%", { { "mono_freq", 200 }, { "width", 2.0f } } },
    };
    return p;
}
