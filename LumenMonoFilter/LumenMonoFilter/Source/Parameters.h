#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace IDs
{
    inline const juce::String source = "source", lowF = "low_freq", lowW = "low_width", highF = "high_freq", highW = "high_width",
        hpf = "hpf", mode = "mode", hq = "hq", align = "phase_align", autoAlign = "auto_align", alignMs = "align_time", flip = "polarity",
        midTrim = "mid_trim", sideTrim = "side_trim", autoTrim = "auto_trim", monitor = "monitor", out = "out";
}

inline juce::StringArray sourceNames()  { return { "Stereo", "Left", "Right" }; }
inline juce::StringArray modeNames()    { return { "Zero Latency", "Linear Phase" }; }
inline juce::StringArray monitorNames() { return { "Full", "Low Band", "High Band", "Mono Sum", "Side" }; }

inline juce::String hzText (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String ((int) std::round (v)) + " Hz"; }

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const String& id) { return ParameterID { id, 1 }; };
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });
    auto db = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; });
    auto hz = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return hzText (v); });
    auto range = [] (float a, float b, float centre) { NormalisableRange<float> r (a, b); r.setSkewForCentre (centre); return r; };

    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::source), "Source", sourceNames(), 0));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::lowF), "Low Node Freq", range (20.0f, 2000.0f, 150.0f), 100.0f, hz));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::lowW), "Low Width", NormalisableRange<float> (0.0f, 2.0f, 0.01f), 0.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::highF), "High Node Freq", range (30.0f, 8000.0f, 300.0f), 220.0f, hz));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::highW), "High Width", NormalisableRange<float> (0.0f, 2.0f, 0.01f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::hpf), "High-Pass", range (0.0f, 200.0f, 40.0f), 0.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 15.0f ? String ("Off") : hzText (v); })));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::mode), "Phase Mode", modeNames(), 0));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::hq), "HQ (long FIR)", false));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::align), "Phase Align", false));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::autoAlign), "Auto Align", true));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::alignMs), "Align Time", NormalisableRange<float> (-3.0f, 3.0f, 0.001f), 0.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " ms"; })));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::flip), "Low Polarity Flip (R)", false));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::midTrim), "Mid Trim", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, db));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::sideTrim), "Side Trim", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, db));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::autoTrim), "Auto Trim", false));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::monitor), "Monitor", monitorNames(), 0));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::out), "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, db));
    return l;
}

struct MPreset { const char* name; std::vector<std::pair<const char*, float>> v; };
inline const std::vector<MPreset>& factoryPresets()
{
    // mode: 0 zero latency, 1 linear phase   source: 0 stereo 1 left 2 right
    static const std::vector<MPreset> p {
        { "Init: Mono Below 100 Hz", {} },
        { "Bypass-Flat (100% Everywhere)", { { "low_width", 1.0f }, { "high_width", 1.0f } } },
        // ---- mix bus / master ----
        { "MASTER Club Safe 120 Hz", { { "low_freq", 110 }, { "high_freq", 260 }, { "mode", 1 } } },
        { "MASTER Gentle Mono Bass", { { "low_freq", 70 }, { "low_width", 0.15f }, { "high_freq", 300 }, { "mode", 1 } } },
        { "MASTER Vinyl Cut Safe", { { "low_freq", 150 }, { "high_freq", 400 }, { "hpf", 25 }, { "mode", 1 }, { "hq", 1 } } },
        { "MASTER Tight + Wide Top", { { "low_freq", 100 }, { "high_freq", 2500 }, { "high_width", 1.2f }, { "mode", 1 } } },
        { "MASTER Rumble Clean 30 Hz", { { "low_freq", 90 }, { "high_freq", 220 }, { "hpf", 30 }, { "mode", 1 } } },
        { "MASTER Streaming Mono-Safe", { { "low_freq", 160 }, { "low_width", 0.1f }, { "high_freq", 600 }, { "high_width", 0.9f }, { "mode", 1 } } },
        // ---- bass / 808 / kick ----
        { "BASS Sub Mono, Top Stereo", { { "low_freq", 120 }, { "high_freq", 200 }, { "phase_align", 1 } } },
        { "BASS Reese Focus", { { "low_freq", 180 }, { "high_freq", 500 }, { "high_width", 1.1f }, { "phase_align", 1 } } },
        { "BASS 808 Pure Mono", { { "low_freq", 300 }, { "high_freq", 800 }, { "phase_align", 1 } } },
        { "BASS Supersaw Bass Fix", { { "low_freq", 150 }, { "high_freq", 350 }, { "phase_align", 1 }, { "mode", 1 } } },
        { "KICK Mono Body", { { "low_freq", 200 }, { "high_freq", 600 }, { "hpf", 25 } } },
        // ---- synths / pads ----
        { "SYNTH Wide Pad, Mono Lows", { { "low_freq", 200 }, { "high_freq", 800 }, { "high_width", 1.3f } } },
        { "SYNTH Lead Centred Lows", { { "low_freq", 250 }, { "low_width", 0.3f }, { "high_freq", 1200 } } },
        { "SYNTH Pluck Spread", { { "low_freq", 150 }, { "high_freq", 1500 }, { "high_width", 1.4f } } },
        { "SYNTH Chord Stab Club", { { "low_freq", 180 }, { "high_freq", 500 }, { "hpf", 60 } } },
        // ---- recordings / mic ----
        { "REC Spaced Pair Fix (Align)", { { "low_freq", 150 }, { "low_width", 0.4f }, { "high_freq", 500 }, { "phase_align", 1 } } },
        { "REC Drum Overheads", { { "low_freq", 120 }, { "low_width", 0.3f }, { "high_freq", 400 }, { "phase_align", 1 }, { "hpf", 40 } } },
        { "REC Piano Stereo Mics", { { "low_freq", 100 }, { "low_width", 0.5f }, { "high_freq", 350 }, { "phase_align", 1 } } },
        { "REC Left Mic Only", { { "source", 1 } } },
        { "REC Right Mic Only", { { "source", 2 } } },
        // ---- sound design / creative ----
        { "FX Super Wide Highs", { { "low_freq", 150 }, { "high_freq", 3000 }, { "high_width", 1.8f } } },
        { "FX Mono Mids, Wide Air", { { "low_freq", 200 }, { "high_freq", 6000 }, { "high_width", 1.6f } } },
        { "FX Full Mono Check", { { "low_width", 0.0f }, { "high_width", 0.0f } } },
        { "FX Narrow Lo-Fi", { { "low_freq", 400 }, { "high_freq", 2000 }, { "high_width", 0.5f }, { "hpf", 80 } } },
        { "FX Inverse (Wide Lows)", { { "low_freq", 150 }, { "low_width", 1.4f }, { "high_freq", 1000 }, { "high_width", 0.6f } } },
        // ---- utility ----
        { "UTIL Listen: Low Band", { { "monitor", 1 } } },
        { "UTIL Listen: Side Only", { { "monitor", 4 }, { "low_width", 1.0f } } },
        { "UTIL Mono Sum Check", { { "monitor", 3 }, { "low_width", 1.0f } } },
    };
    return p;
}
