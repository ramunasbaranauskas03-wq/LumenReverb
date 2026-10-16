#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace IDs
{
    inline const juce::String in = "in", lowCut = "low_cut", tune = "tune", key = "key", scale = "scale", retune = "retune", amount = "amount",
        transpose = "transpose", formant = "formant", deess = "deess", deessFreq = "deess_freq", comp = "comp", body = "body", presence = "presence",
        air = "air", warmth = "warmth", dbl = "double", width = "width", h1 = "harm1", h2 = "harm2", h1Lvl = "harm1_lvl", h2Lvl = "harm2_lvl",
        dly = "delay", dlyDiv = "delay_div", dlyFb = "delay_fb", verb = "reverb", verbSize = "reverb_size", duck = "duck", out = "out";
}

inline juce::StringArray keyNames() { return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; }
inline juce::StringArray scaleNames() { return { "Chromatic", "Major", "Minor", "Harmonic Minor", "Dorian", "Major Pentatonic", "Minor Pentatonic" }; }
inline juce::StringArray intervalNames() { return { "Off", "-Octave", "-5th", "-4th", "-3rd", "+3rd", "+4th", "+5th", "+Octave" }; }

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const String& id) { return ParameterID { id, 1 }; };
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v * 100.0f, 0) + "%"; });
    auto db = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.04f ? "+" : "") + String (v, 1) + " dB"; });
    auto hz = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String ((int) std::round (v)) + " Hz"; });
    auto st = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.04f ? "+" : "") + String (v, 1) + " st"; });

    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::in), "Input", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, db));
    NormalisableRange<float> lc (20.0f, 400.0f); lc.setSkewForCentre (100.0f);
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::lowCut), "Low Cut", lc, 90.0f, hz));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::tune), "Tune", true));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::key), "Key", keyNames(), 0));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::scale), "Scale", scaleNames(), 0));
    NormalisableRange<float> rt (0.0f, 400.0f); rt.setSkewForCentre (50.0f);
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::retune), "Retune Speed", rt, 40.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 1.0f ? String ("HARD") : String ((int) std::round (v)) + " ms"; })));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::amount), "Correction", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::transpose), "Transpose", NormalisableRange<float> (-12.0f, 12.0f, 1.0f), 0.0f, st));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::formant), "Formant", NormalisableRange<float> (-6.0f, 6.0f, 0.1f), 0.0f, st));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::deess), "De-Ess", NormalisableRange<float> (0.0f, 1.0f), 0.3f, pct));
    NormalisableRange<float> df (3000.0f, 12000.0f); df.setSkewForCentre (6500.0f);
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::deessFreq), "De-Ess Freq", df, 6500.0f, hz));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::comp), "Compress", NormalisableRange<float> (0.0f, 1.0f), 0.4f, pct));
    for (auto [id, name] : { std::pair<const String&, const char*> { IDs::body, "Body" }, { IDs::presence, "Presence" }, { IDs::air, "Air" } })
        l.add (std::make_unique<AudioParameterFloat> (pid (id), name, NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, db));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::warmth), "Warmth", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::dbl), "Double", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::width), "Width", NormalisableRange<float> (0.0f, 1.0f), 0.7f, pct));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::h1), "Harmony 1", intervalNames(), 0));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::h1Lvl), "Harmony 1 Level", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::h2), "Harmony 2", intervalNames(), 0));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::h2Lvl), "Harmony 2 Level", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::dly), "Delay", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::dlyDiv), "Delay Time", StringArray { "1/16", "1/8", "1/8 D", "1/4", "1/4 D", "1/2" }, 3));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::dlyFb), "Delay Feedback", NormalisableRange<float> (0.0f, 0.9f), 0.35f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::verb), "Reverb", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::verbSize), "Reverb Size", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::duck), "FX Ducking", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::out), "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, db));
    return l;
}

struct VPreset { const char* name; std::vector<std::pair<const char*, float>> v; };
inline const std::vector<VPreset>& factoryPresets()
{
    // scale: 0 Chrom 1 Maj 2 Min 3 HMin 4 Dor 5 MajP 6 MinP   harmony: 0 off 1 -oct 2 -5 3 -4 4 -3 5 +3 6 +4 7 +5 8 +oct
    static const std::vector<VPreset> p {
        { "Init", {} },
        { "Natural Pop", { { "retune", 60 }, { "amount", 0.8f }, { "deess", 0.35f }, { "comp", 0.45f }, { "presence", 1.5f }, { "air", 2.5f }, { "reverb", 0.18f }, { "reverb_size", 0.4f } } },
        { "Modern EDM Lead", { { "retune", 12 }, { "deess", 0.45f }, { "comp", 0.65f }, { "body", -1.5f }, { "presence", 2.5f }, { "air", 4.0f }, { "warmth", 0.25f }, { "double", 0.45f }, { "delay", 0.22f }, { "delay_div", 2 }, { "reverb", 0.3f }, { "reverb_size", 0.7f }, { "duck", 0.7f } } },
        { "Hard Tune (T-Pain)", { { "retune", 0 }, { "comp", 0.6f }, { "presence", 2.0f }, { "air", 3.0f }, { "delay", 0.15f }, { "reverb", 0.15f } } },
        { "Festival Anthem", { { "retune", 20 }, { "comp", 0.7f }, { "presence", 2.0f }, { "air", 4.5f }, { "double", 0.6f }, { "harm1", 8 }, { "harm1_lvl", 0.35f }, { "harm2", 1 }, { "harm2_lvl", 0.25f }, { "reverb", 0.4f }, { "reverb_size", 0.85f }, { "delay", 0.2f }, { "duck", 0.75f } } },
        { "Rap / Spoken", { { "tune", 0 }, { "deess", 0.4f }, { "comp", 0.75f }, { "low_cut", 120 }, { "body", -2.0f }, { "presence", 3.0f }, { "air", 2.0f }, { "warmth", 0.3f }, { "double", 0.2f } } },
        { "Ballad", { { "retune", 120 }, { "amount", 0.7f }, { "comp", 0.35f }, { "body", 1.0f }, { "air", 2.0f }, { "warmth", 0.15f }, { "reverb", 0.4f }, { "reverb_size", 0.8f }, { "delay", 0.12f }, { "delay_div", 3 } } },
        { "Airy Female Vox", { { "retune", 35 }, { "deess", 0.55f }, { "deess_freq", 7500 }, { "comp", 0.5f }, { "body", -2.0f }, { "air", 5.0f }, { "reverb", 0.3f }, { "reverb_size", 0.6f } } },
        { "Deep Male Vox", { { "retune", 45 }, { "deess", 0.3f }, { "deess_freq", 5500 }, { "comp", 0.55f }, { "body", 2.0f }, { "presence", 1.5f }, { "warmth", 0.35f }, { "reverb", 0.2f } } },
        { "3rd Harmony Stack", { { "retune", 25 }, { "harm1", 5 }, { "harm1_lvl", 0.55f }, { "harm2", 7 }, { "harm2_lvl", 0.4f }, { "width", 0.9f }, { "reverb", 0.25f } } },
        { "Octave Down (Dark)", { { "harm1", 1 }, { "harm1_lvl", 0.6f }, { "formant", -1.0f }, { "warmth", 0.3f } } },
        { "Chipmunk / Pitched Up", { { "transpose", 7 }, { "formant", 3.0f }, { "retune", 15 } } },
        { "Monster / Pitched Down", { { "transpose", -7 }, { "formant", -3.0f }, { "warmth", 0.4f } } },
        { "Wide Doubled Chorus", { { "double", 0.8f }, { "width", 1.0f }, { "comp", 0.55f }, { "reverb", 0.25f } } },
        { "Telephone Hook", { { "low_cut", 380 }, { "body", -6.0f }, { "presence", 8.0f }, { "air", -10.0f }, { "warmth", 0.7f }, { "delay", 0.25f }, { "delay_div", 1 } } },
        { "Dry Clean (Mix Ready)", { { "tune", 0 }, { "deess", 0.3f }, { "comp", 0.4f }, { "air", 1.5f } } },
    };
    return p;
}
