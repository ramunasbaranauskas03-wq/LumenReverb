#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace IDs
{
    inline const juce::String mode = "mode", shape = "shape", asym = "asym", drive = "drive", ceiling = "ceiling", out = "out", mix = "mix",
                              delta = "delta", os = "os", adaa = "adaa", tp = "true_peak", match = "gain_match", stereo = "stereo", side = "side",
                              bands = "bands";
    inline juce::String x (int k)     { return "x" + juce::String (k + 1); }
    inline juce::String bmode (int b) { return "bm" + juce::String (b + 1); }
    inline juce::String trim (int b)  { return "bt" + juce::String (b + 1); }
    inline juce::String byp (int b)   { return "bb" + juce::String (b + 1); }
    inline juce::String solo (int b)  { return "bs" + juce::String (b + 1); }
}

inline juce::StringArray modeNames() { return { "Hard", "Classic", "Smooth", "Crisp", "Tape", "Tube", "Diode" }; }

inline const char* bandName (int b, int nb)
{
    static const char* n4[] = { "LOW", "LO MID", "HI MID", "HIGH" };
    static const char* n3[] = { "LOW", "MID", "HIGH", "" };
    static const char* n2[] = { "LOW", "HIGH", "", "" };
    return nb >= 4 ? n4[b] : nb == 3 ? n3[b] : nb == 2 ? n2[b] : "";
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const String& id) { return ParameterID { id, 1 }; };
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v * 100.0f, 0) + "%"; });
    auto bip = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.004f ? "+" : "") + String (v * 100.0f, 0) + "%"; });
    auto db = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.04f ? "+" : "") + String (v, 1) + " dB"; });
    auto hz = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : String ((int) std::round (v)) + " Hz"; });

    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::mode), "Mode", modeNames(), 1));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::shape), "Shape", NormalisableRange<float> (0.0f, 1.0f), 0.7f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::asym), "Asymmetry", NormalisableRange<float> (-1.0f, 1.0f), 0.0f, bip));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::drive), "Drive", NormalisableRange<float> (0.0f, 24.0f, 0.05f), 0.0f, db));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::ceiling), "Ceiling", NormalisableRange<float> (-12.0f, 0.0f, 0.05f), 0.0f, db));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::out), "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.05f), 0.0f, db));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::mix), "Mix", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::delta), "Delta", false));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::os), "Oversampling", StringArray { "1x", "2x", "4x", "8x", "16x", "32x" }, 2));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::adaa), "ADAA", true));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::tp), "True Peak Ceiling", false));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::match), "Gain Match", false));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::stereo), "Stereo Mode", StringArray { "L / R", "Mid / Side" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::side), "Side Drive", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, db));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::bands), "Bands", StringArray { "Single Band", "2 Bands", "3 Bands", "4 Bands" }, 0));
    const float xd[] = { 120.0f, 1000.0f, 6000.0f }, xlo[] = { 20.0f, 150.0f, 1000.0f }, xhi[] = { 1000.0f, 8000.0f, 18000.0f };
    const char* xn[] = { "Crossover 1", "Crossover 2", "Crossover 3" };
    for (int k = 0; k < 3; ++k)
    {
        NormalisableRange<float> r (xlo[k], xhi[k]); r.setSkewForCentre (std::sqrt (xlo[k] * xhi[k]));
        l.add (std::make_unique<AudioParameterFloat> (pid (IDs::x (k)), xn[k], r, xd[k], hz));
    }
    const char* bn[] = { "Low", "Lo Mid", "Hi Mid", "High" };
    auto bm = modeNames(); bm.insert (0, "Main");
    for (int b = 0; b < 4; ++b)
    {
        const String n (bn[b]);
        l.add (std::make_unique<AudioParameterChoice> (pid (IDs::bmode (b)), n + " Mode", bm, 0));
        l.add (std::make_unique<AudioParameterFloat> (pid (IDs::trim (b)), n + " Drive", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, db));
        l.add (std::make_unique<AudioParameterBool> (pid (IDs::byp (b)), n + " No Clip", false));
        l.add (std::make_unique<AudioParameterBool> (pid (IDs::solo (b)), n + " Solo", false));
    }
    return l;
}

struct CPreset { const char* name; std::vector<std::pair<const char*, float>> v; };
inline const std::vector<CPreset>& factoryPresets()
{
    // mode index: 0 Hard 1 Classic 2 Smooth 3 Crisp 4 Tape 5 Tube 6 Diode
    static const std::vector<CPreset> p {
        { "Init", {} },
        { "Master: Transparent +2 dB", { { "mode", 1 }, { "shape", 0.4f }, { "drive", 2.0f }, { "ceiling", -0.3f }, { "true_peak", 1 } } },
        { "Master: Loud EDM", { { "mode", 1 }, { "shape", 0.55f }, { "drive", 4.5f }, { "ceiling", -0.3f }, { "true_peak", 1 }, { "os", 4 } } },
        { "Master: Club Smash", { { "mode", 0 }, { "drive", 6.5f }, { "ceiling", -0.1f }, { "true_peak", 1 }, { "os", 4 } } },
        { "Master: Multiband Loud", { { "bands", 2 }, { "drive", 4.0f }, { "bm1", 2 }, { "bm2", 2 }, { "bm3", 4 }, { "bt1", -1.5f }, { "bt3", 1.0f }, { "ceiling", -0.3f }, { "true_peak", 1 } } },
        { "Master: Keep Sub Clean", { { "bands", 1 }, { "x1", 90 }, { "bb1", 1 }, { "drive", 3.5f }, { "ceiling", -0.3f }, { "true_peak", 1 } } },
        { "Drums: Snappy Bus", { { "mode", 0 }, { "drive", 5.0f }, { "os", 3 } } },
        { "Drums: Crisp Punch", { { "mode", 3 }, { "shape", 0.8f }, { "drive", 6.0f } } },
        { "Kick: Hard Clip", { { "mode", 0 }, { "drive", 8.0f }, { "os", 4 } } },
        { "Kick: Warm Round", { { "mode", 5 }, { "shape", 0.9f }, { "drive", 7.0f }, { "asym", 0.25f } } },
        { "808: Grit", { { "mode", 6 }, { "drive", 10.0f }, { "shape", 1.0f }, { "asym", 0.3f } } },
        { "Snare: Crack", { { "mode", 3 }, { "drive", 7.0f }, { "shape", 0.6f } } },
        { "Bass: Tape Glue", { { "mode", 4 }, { "drive", 5.0f }, { "shape", 1.0f } } },
        { "Vocal: Peak Tamer", { { "mode", 2 }, { "drive", 3.0f }, { "shape", 1.0f }, { "mix", 0.7f } } },
        { "Synths: Parallel Dirt", { { "mode", 6 }, { "drive", 14.0f }, { "mix", 0.35f }, { "gain_match", 1 } } },
        { "Stereo: Clip Mid Only", { { "stereo", 1 }, { "side", -12.0f }, { "drive", 4.0f } } },
        { "Listen: Delta (what is cut)", { { "drive", 4.0f }, { "delta", 1 } } },
    };
    return p;
}
