#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace IDs
{
    inline const juce::String mode = "mode", variant = "variant", shape = "shape", rate = "rate", sync = "sync", div = "division",
        depth = "depth", delay = "delay", feedback = "feedback", voices = "voices", width = "width", mix = "mix", tone = "tone",
        character = "character", drift = "drift", bassMono = "bass_mono", duck = "duck", env = "env", out = "out";
}

inline juce::StringArray modeNames() { return { "Chorus", "Juno", "Dimension", "Ensemble", "Flanger", "Phaser", "Vibrato" }; }
inline juce::StringArray variantNames (int mode)
{
    switch (mode)
    {
        case 0: return { "Modern", "Tri", "Lush 8", "Detune" };
        case 1: return { "I", "II", "I + II", "Deep" };
        case 2: return { "1", "2", "3", "4" };
        case 3: return { "Strings", "Organ", "Soft", "Wide" };
        case 4: return { "Classic", "Negative", "Jet", "Wide" };
        case 5: return { "4 Stage", "8 Stage", "12 Stage", "16 Stage" };
        default: return { "Vibrato", "Rotary", "Tape Wow", "Warble" };
    }
}
inline juce::StringArray divNames() { return { "4 Bars", "2 Bars", "1 Bar", "1/2", "1/4", "1/8", "1/16", "1/2 D", "1/4 D", "1/8 D", "1/2 T", "1/4 T", "1/8 T" }; }
inline double divBeats (int i) { static const double b[] { 16, 8, 4, 2, 1, 0.5, 0.25, 3, 1.5, 0.75, 4.0 / 3.0, 2.0 / 3.0, 1.0 / 3.0 }; return b[juce::jlimit (0, 12, i)]; }

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const String& id) { return ParameterID { id, 1 }; };
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v * 100.0f, 0) + "%"; });
    auto bip = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.004f ? "+" : "") + String (v * 100.0f, 0) + "%"; });
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::mode), "Mode", modeNames(), 0));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::variant), "Variant", StringArray { "A", "B", "C", "D" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::shape), "LFO Shape", StringArray { "Sine", "Triangle", "Random" }, 0));
    NormalisableRange<float> rr (0.02f, 20.0f); rr.setSkewForCentre (1.0f);
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::rate), "Rate", rr, 0.8f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 1.0f ? String (v, 2) + " Hz" : String (v, 1) + " Hz"; })));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::sync), "Tempo Sync", false));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::div), "Sync Division", divNames(), 2));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::depth), "Depth", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::delay), "Delay / Freq", NormalisableRange<float> (0.0f, 1.0f), 0.4f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::feedback), "Feedback", NormalisableRange<float> (-0.95f, 0.95f), 0.0f, bip));
    l.add (std::make_unique<AudioParameterInt> (pid (IDs::voices), "Voices", 1, 8, 3));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::width), "Width", NormalisableRange<float> (0.0f, 1.0f), 0.8f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::mix), "Mix", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::tone), "Tone", NormalisableRange<float> (-1.0f, 1.0f), 0.0f, bip));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::character), "BBD Character", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::drift), "Analog Drift", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct));
    NormalisableRange<float> bm (0.0f, 500.0f); bm.setSkewForCentre (120.0f);
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::bassMono), "Bass Mono", bm, 120.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 20.0f ? String ("Off") : String ((int) std::round (v)) + " Hz"; })));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::duck), "Duck", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::env), "Envelope > Depth", NormalisableRange<float> (-1.0f, 1.0f), 0.0f, bip));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::out), "Output", NormalisableRange<float> (-18.0f, 12.0f, 0.1f), 0.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.04f ? "+" : "") + String (v, 1) + " dB"; })));
    return l;
}

struct MPreset { const char* name; std::vector<std::pair<const char*, float>> v; };
inline const std::vector<MPreset>& factoryPresets()
{
    // mode: 0 Chorus 1 Juno 2 Dimension 3 Ensemble 4 Flanger 5 Phaser 6 Vibrato
    static const std::vector<MPreset> p {
        { "Init: Modern Chorus", {} },
        { "Juno I (Pads)", { { "mode", 1 }, { "variant", 0 }, { "shape", 1 }, { "rate", 0.51f }, { "depth", 0.6f }, { "character", 0.4f }, { "mix", 0.5f } } },
        { "Juno II (Strings)", { { "mode", 1 }, { "variant", 1 }, { "shape", 1 }, { "rate", 0.51f }, { "depth", 0.6f }, { "character", 0.4f }, { "mix", 0.5f } } },
        { "Juno I+II Shimmer", { { "mode", 1 }, { "variant", 2 }, { "shape", 1 }, { "rate", 0.65f }, { "depth", 0.7f }, { "character", 0.35f }, { "mix", 0.5f } } },
        { "Dimension 2 (Mix Wide)", { { "mode", 2 }, { "variant", 1 }, { "shape", 1 }, { "rate", 0.5f }, { "depth", 0.5f }, { "mix", 0.5f } } },
        { "Dimension 4 (Lush)", { { "mode", 2 }, { "variant", 3 }, { "shape", 1 }, { "rate", 0.5f }, { "depth", 0.7f }, { "mix", 0.5f } } },
        { "Supersaw Thickener", { { "mode", 0 }, { "variant", 2 }, { "rate", 0.35f }, { "depth", 0.45f }, { "delay", 0.5f }, { "width", 1.0f }, { "mix", 0.4f }, { "bass_mono", 180.0f } } },
        { "Pluck Width (Tri)", { { "mode", 0 }, { "variant", 1 }, { "rate", 0.9f }, { "depth", 0.35f }, { "width", 1.0f }, { "mix", 0.45f }, { "duck", 0.4f } } },
        { "Vocal Doubler (Detune)", { { "mode", 0 }, { "variant", 3 }, { "rate", 0.25f }, { "depth", 0.3f }, { "delay", 0.6f }, { "width", 1.0f }, { "mix", 0.35f }, { "drift", 0.5f } } },
        { "String Ensemble", { { "mode", 3 }, { "variant", 0 }, { "rate", 0.6f }, { "depth", 0.6f }, { "character", 0.3f }, { "mix", 0.55f } } },
        { "Organ Ensemble", { { "mode", 3 }, { "variant", 1 }, { "rate", 0.8f }, { "depth", 0.55f }, { "mix", 0.5f } } },
        { "Classic Flanger", { { "mode", 4 }, { "variant", 0 }, { "shape", 1 }, { "rate", 0.25f }, { "depth", 0.8f }, { "delay", 0.45f }, { "feedback", 0.6f }, { "mix", 0.5f } } },
        { "Jet Flanger (Drums)", { { "mode", 4 }, { "variant", 2 }, { "shape", 1 }, { "rate", 0.12f }, { "depth", 0.9f }, { "delay", 0.5f }, { "feedback", 0.85f }, { "mix", 0.5f } } },
        { "Flanger Riser (1 Bar)", { { "mode", 4 }, { "variant", 3 }, { "sync", 1 }, { "division", 2 }, { "depth", 0.85f }, { "feedback", 0.7f }, { "mix", 0.5f } } },
        { "Phaser 4 (Vintage)", { { "mode", 5 }, { "variant", 0 }, { "rate", 0.4f }, { "depth", 0.7f }, { "delay", 0.45f }, { "feedback", 0.3f }, { "mix", 0.5f } } },
        { "Phaser 12 (Deep Swirl)", { { "mode", 5 }, { "variant", 2 }, { "rate", 0.2f }, { "depth", 0.85f }, { "delay", 0.5f }, { "feedback", 0.65f }, { "width", 1.0f }, { "mix", 0.5f } } },
        { "Phaser 1/4 Synced", { { "mode", 5 }, { "variant", 1 }, { "sync", 1 }, { "division", 4 }, { "depth", 0.75f }, { "feedback", 0.5f }, { "mix", 0.5f } } },
        { "Vibrato (Lead)", { { "mode", 6 }, { "variant", 0 }, { "rate", 5.5f }, { "depth", 0.25f }, { "mix", 1.0f } } },
        { "Rotary Speaker", { { "mode", 6 }, { "variant", 1 }, { "rate", 6.0f }, { "depth", 0.35f }, { "mix", 1.0f } } },
        { "Tape Wow (Lo-fi)", { { "mode", 6 }, { "variant", 2 }, { "shape", 2 }, { "rate", 1.5f }, { "depth", 0.3f }, { "drift", 0.8f }, { "mix", 1.0f } } },
        { "EDM Lead: Chorus + Duck", { { "mode", 0 }, { "variant", 0 }, { "voices", 4 }, { "rate", 0.7f }, { "depth", 0.5f }, { "width", 1.0f }, { "mix", 0.5f }, { "duck", 0.6f }, { "bass_mono", 200.0f } } },
    };
    return p;
}
