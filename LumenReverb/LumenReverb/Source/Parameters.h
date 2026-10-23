#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Reverb2.h"

// =====================================================================================
// Lumen Reverb 2 — parameters
// =====================================================================================
namespace IDs
{
    inline const juce::String algo = "algo", mix = "mix", preDelay = "predelay", preSync = "pre_sync", decay = "decay", size = "size",
        diffusion = "diffusion", early = "early", modDepth = "mod_depth", modRate = "mod_rate",
        bassMult = "bass_mult", trebleMult = "treble_mult", xLow = "x_low", xHigh = "x_high",
        lowCut = "low_cut", highCut = "high_cut", width = "width", bassMono = "bass_mono",
        duck = "duck", clarity = "clarity", shimmer = "shimmer", shimmerPitch = "shimmer_pitch",
        freeze = "freeze", adapt = "adapt", out = "out";
}

inline juce::StringArray algoNames() { juce::StringArray a; for (auto& x : lumen::rv2::algos()) a.add (x.name); return a; }
inline juce::StringArray preSyncNames() { return { "Free", "1/64", "1/32", "1/16", "1/8", "1/4", "1/2" }; }
inline double preSyncBeats (int i) { static const double b[] { 0, 1.0 / 16, 1.0 / 8, 1.0 / 4, 1.0 / 2, 1.0, 2.0 }; return b[juce::jlimit (0, 6, i)]; }
inline juce::StringArray pitchNames() { return { "-12", "+5", "+7", "+12", "+19", "+24" }; }
inline float pitchSemis (int i) { static const float s[] { -12, 5, 7, 12, 19, 24 }; return s[juce::jlimit (0, 5, i)]; }

inline juce::String hzText (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String ((int) std::round (v)) + " Hz"; }

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const String& id) { return ParameterID { id, 2 }; };
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; });
    auto hz = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return hzText (v); });
    auto mult = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return "x" + String (v, 2); });
    auto range = [] (float a, float b, float centre) { NormalisableRange<float> r (a, b); r.setSkewForCentre (centre); return r; };

    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::algo), "Algorithm", algoNames(), 0));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::mix), "Mix", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.3f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::preDelay), "Pre-Delay", range (0.0f, 500.0f, 60.0f), 20.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, v < 10 ? 1 : 0) + " ms"; })));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::preSync), "Pre-Delay Sync", preSyncNames(), 0));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::decay), "Decay", range (0.1f, 30.0f, 2.5f), 2.5f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, v < 10 ? 2 : 1) + " s"; })));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::size), "Size", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.6f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::diffusion), "Diffusion", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.8f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::early), "Early Reflections", NormalisableRange<float> (0.0f, 1.5f, 0.001f), 0.5f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::modDepth), "Mod Depth", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.35f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::modRate), "Mod Rate", range (0.05f, 5.0f, 0.8f), 0.6f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " Hz"; })));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::bassMult), "Bass Decay", range (0.25f, 2.5f, 1.0f), 1.1f, mult));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::trebleMult), "Treble Decay", range (0.05f, 1.2f, 0.5f), 0.5f, mult));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::xLow), "Bass Crossover", range (60.0f, 1000.0f, 250.0f), 250.0f, hz));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::xHigh), "Treble Crossover", range (1000.0f, 12000.0f, 4000.0f), 4000.0f, hz));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::lowCut), "Low Cut", range (20.0f, 1000.0f, 150.0f), 120.0f, hz));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::highCut), "High Cut", range (1000.0f, 20000.0f, 8000.0f), 14000.0f, hz));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::width), "Width", NormalisableRange<float> (0.0f, 1.5f, 0.001f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::bassMono), "Bass Mono", range (0.0f, 400.0f, 120.0f), 120.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 1.0f ? String ("Off") : hzText (v); })));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::duck), "Ducking", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::clarity), "Clarity", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.3f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::shimmer), "Shimmer", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f, pct));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::shimmerPitch), "Shimmer Pitch", pitchNames(), 3));
    l.add (std::make_unique<AudioParameterBool>  (pid (IDs::freeze), "Freeze", false));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::adapt), "Adapt", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::out), "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.04f ? "+" : "") + String (v, 1) + " dB"; })));
    return l;
}

struct RPreset { const char* name; std::vector<std::pair<const char*, float>> v; };
inline const std::vector<RPreset>& factoryPresets()
{
    // algo: 0 Hall 1 Plate 2 Room 3 Chamber 4 Cathedral 5 Bloom 6 Shimmer 7 Ambience
    static const std::vector<RPreset> p {
        { "Init: Concert Hall", {} },
        // ---- vocals / leads
        { "VOX Silky Plate", { { "algo", 1 }, { "decay", 1.8f }, { "predelay", 35 }, { "treble_mult", 0.6f }, { "low_cut", 250 }, { "clarity", 0.45f }, { "mix", 0.22f } } },
        { "VOX Big Pop Hall", { { "algo", 0 }, { "decay", 2.8f }, { "predelay", 60 }, { "size", 0.75f }, { "low_cut", 220 }, { "clarity", 0.5f }, { "duck", 0.3f }, { "mix", 0.22f } } },
        { "VOX Tight Chamber", { { "algo", 3 }, { "decay", 1.2f }, { "predelay", 15 }, { "size", 0.45f }, { "low_cut", 300 }, { "mix", 0.18f } } },
        { "VOX Dreamy Bloom", { { "algo", 5 }, { "decay", 6.0f }, { "size", 0.85f }, { "low_cut", 250 }, { "duck", 0.45f }, { "mix", 0.3f } } },
        { "LEAD Wide Hall Ducked", { { "algo", 0 }, { "decay", 3.2f }, { "predelay", 40 }, { "width", 1.3f }, { "duck", 0.55f }, { "low_cut", 300 }, { "mix", 0.28f } } },
        { "LEAD Supersaw Plate", { { "algo", 1 }, { "decay", 2.2f }, { "predelay", 25 }, { "low_cut", 350 }, { "clarity", 0.5f }, { "mix", 0.25f } } },
        // ---- drums
        { "DRUM Snare Plate", { { "algo", 1 }, { "decay", 1.4f }, { "predelay", 5 }, { "treble_mult", 0.7f }, { "low_cut", 200 }, { "mix", 0.22f } } },
        { "DRUM Clap Big Room", { { "algo", 2 }, { "decay", 1.6f }, { "size", 0.7f }, { "early", 0.9f }, { "low_cut", 250 }, { "mix", 0.25f } } },
        { "DRUM Kit Room", { { "algo", 2 }, { "decay", 0.8f }, { "size", 0.4f }, { "early", 1.0f }, { "low_cut", 150 }, { "mix", 0.15f } } },
        { "DRUM Gated Ambience", { { "algo", 7 }, { "decay", 0.35f }, { "early", 1.3f }, { "duck", 0.0f }, { "low_cut", 200 }, { "mix", 0.3f } } },
        { "DRUM Hats Air", { { "algo", 1 }, { "decay", 0.9f }, { "low_cut", 2000 }, { "treble_mult", 0.9f }, { "mix", 0.18f } } },
        // ---- synths / pads / fx
        { "PAD Endless Cathedral", { { "algo", 4 }, { "decay", 12.0f }, { "size", 0.95f }, { "mod_depth", 0.6f }, { "low_cut", 200 }, { "mix", 0.4f } } },
        { "PAD Shimmer Heaven", { { "algo", 6 }, { "decay", 8.0f }, { "shimmer", 0.55f }, { "size", 0.85f }, { "low_cut", 250 }, { "mix", 0.4f } } },
        { "PAD Fifth Shimmer", { { "algo", 6 }, { "decay", 7.0f }, { "shimmer", 0.45f }, { "shimmer_pitch", 2 }, { "low_cut", 250 }, { "mix", 0.35f } } },
        { "PLUCK Ducked Hall", { { "algo", 0 }, { "decay", 2.4f }, { "predelay", 30 }, { "duck", 0.6f }, { "low_cut", 300 }, { "mix", 0.3f } } },
        { "PLUCK Bright Plate", { { "algo", 1 }, { "decay", 1.6f }, { "treble_mult", 0.8f }, { "low_cut", 300 }, { "mix", 0.25f } } },
        { "BASS Short Room (Mono Safe)", { { "algo", 2 }, { "decay", 0.6f }, { "size", 0.3f }, { "low_cut", 400 }, { "bass_mono", 300 }, { "mix", 0.12f } } },
        { "FX Riser Wash", { { "algo", 5 }, { "decay", 9.0f }, { "size", 1.0f }, { "mod_depth", 0.7f }, { "mix", 0.5f } } },
        { "FX Infinite Freeze", { { "algo", 4 }, { "decay", 20.0f }, { "size", 1.0f }, { "mix", 0.5f } } },
        { "FX Octave Down Drone", { { "algo", 6 }, { "decay", 10.0f }, { "shimmer", 0.5f }, { "shimmer_pitch", 0 }, { "low_cut", 80 }, { "mix", 0.45f } } },
        // ---- mix / bus
        { "BUS Glue Ambience", { { "algo", 7 }, { "decay", 0.7f }, { "early", 1.0f }, { "low_cut", 300 }, { "mix", 0.08f } } },
        { "BUS Club Space", { { "algo", 3 }, { "decay", 1.5f }, { "size", 0.6f }, { "low_cut", 300 }, { "bass_mono", 200 }, { "clarity", 0.5f }, { "mix", 0.12f } } },
        { "SEND Studio Hall 100% Wet", { { "algo", 0 }, { "decay", 2.2f }, { "predelay", 25 }, { "low_cut", 200 }, { "mix", 1.0f } } },
        { "SEND Studio Plate 100% Wet", { { "algo", 1 }, { "decay", 1.6f }, { "predelay", 20 }, { "low_cut", 250 }, { "mix", 1.0f } } },
        // ---- classic EDM
        { "EDM Festival Lead Hall", { { "algo", 0 }, { "decay", 3.5f }, { "size", 0.85f }, { "predelay", 45 }, { "width", 1.35f }, { "duck", 0.5f }, { "low_cut", 350 }, { "mix", 0.3f } } },
        { "EDM Breakdown Cathedral", { { "algo", 4 }, { "decay", 7.0f }, { "predelay", 60 }, { "low_cut", 250 }, { "mix", 0.35f } } },
        { "EDM Future Bass Bloom", { { "algo", 5 }, { "decay", 4.5f }, { "duck", 0.6f }, { "low_cut", 300 }, { "mix", 0.35f } } },
        { "EDM Tech House Room", { { "algo", 2 }, { "decay", 0.9f }, { "size", 0.5f }, { "low_cut", 300 }, { "mix", 0.15f } } },
        { "EDM Trance Pluck Shimmer", { { "algo", 6 }, { "decay", 4.0f }, { "shimmer", 0.3f }, { "duck", 0.4f }, { "low_cut", 300 }, { "mix", 0.3f } } },
    };
    return p;
}
