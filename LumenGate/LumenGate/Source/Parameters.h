#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace IDs
{
    inline const juce::String mode = "mode", style = "style", thr = "threshold", range = "range", ratio = "ratio", attack = "attack",
        hold = "hold", release = "release", knee = "knee", hyst = "hysteresis", lookahead = "lookahead", sc = "sidechain",
        scHp = "key_hp", scLp = "key_lp", listen = "key_listen", link = "link", midi = "midi_trigger",
        steps = "steps", rate = "rate", swing = "swing", gateLen = "gate_length", pAttack = "p_attack", pRelease = "p_release",
        depth = "depth", stereo = "stereo_offset", mix = "mix", out = "out";
    inline juce::String step (int i) { return "s" + juce::String (i + 1); }
}

inline juce::StringArray modeNames()  { return { "Dynamics", "Pattern", "Dynamics + Pattern" }; }
inline juce::StringArray styleNames() { return { "Gate", "Expander", "Smooth", "Upward", "Ducker" }; }
inline juce::StringArray rateNames()  { return { "1/4", "1/8", "1/8 T", "1/16", "1/16 T", "1/32" }; }
inline double rateBeats (int i) { static const double b[] { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 }; return b[juce::jlimit (0, 5, i)]; }
inline int stepsFor (int i) { return i == 0 ? 8 : i == 1 ? 16 : 32; }

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const String& id) { return ParameterID { id, 1 }; };
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v * 100.0f, 0) + "%"; });
    auto db = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; });
    auto ms = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 1.0f ? String (v, 2) + " ms" : v < 100.0f ? String (v, 1) + " ms" : String ((int) std::round (v)) + " ms"; });
    auto hz = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String ((int) std::round (v)) + " Hz"; });
    auto range = [] (float a, float b, float centre) { NormalisableRange<float> r (a, b); r.setSkewForCentre (centre); return r; };

    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::mode), "Mode", modeNames(), 0));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::style), "Style", styleNames(), 0));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::thr), "Threshold", NormalisableRange<float> (-80.0f, 0.0f, 0.1f), -40.0f, db));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::range), "Range", NormalisableRange<float> (0.0f, 80.0f, 0.1f), 60.0f, db));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::ratio), "Ratio", range (1.0f, 20.0f, 3.0f), 4.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return "1:" + String (v, 1); })));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::attack), "Attack", range (0.01f, 100.0f, 2.0f), 0.5f, ms));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::hold), "Hold", range (0.0f, 500.0f, 40.0f), 20.0f, ms));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::release), "Release", range (2.0f, 2000.0f, 150.0f), 120.0f, ms));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::knee), "Knee", NormalisableRange<float> (0.0f, 24.0f, 0.1f), 3.0f, db));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::hyst), "Hysteresis", NormalisableRange<float> (0.0f, 18.0f, 0.1f), 4.0f, db));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::lookahead), "Lookahead", NormalisableRange<float> (0.0f, 10.0f, 0.1f), 0.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 0.05f ? String ("Off") : String (v, 1) + " ms"; })));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::sc), "Key Source", StringArray { "Internal", "External Sidechain" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::scHp), "Key High-Pass", range (20.0f, 4000.0f, 200.0f), 20.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v <= 20.5f ? String ("Off") : (v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String ((int) std::round (v)) + " Hz"); })));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::scLp), "Key Low-Pass", range (200.0f, 20000.0f, 3000.0f), 20000.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v >= 19500.0f ? String ("Off") : (v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String ((int) std::round (v)) + " Hz"); })));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::listen), "Key Listen", false));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::link), "Stereo Link", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::midi), "MIDI Trigger", false));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::steps), "Steps", StringArray { "8", "16", "32" }, 1));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::rate), "Step Rate", rateNames(), 3));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::swing), "Swing", NormalisableRange<float> (0.0f, 0.75f), 0.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::gateLen), "Gate Length", NormalisableRange<float> (0.05f, 1.0f), 0.55f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::pAttack), "Step Attack", range (0.1f, 100.0f, 5.0f), 1.5f, ms));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::pRelease), "Step Release", range (0.1f, 300.0f, 20.0f), 12.0f, ms));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::depth), "Pattern Depth", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::stereo), "Stereo Offset", NormalisableRange<float> (0.0f, 8.0f, 1.0f), 0.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 0.5f ? String ("Off") : String ((int) v) + " step" + (v > 1.5f ? "s" : ""); })));
    for (int i = 0; i < 32; ++i)
        l.add (std::make_unique<AudioParameterFloat> (pid (IDs::step (i)), "Step " + String (i + 1), NormalisableRange<float> (0.0f, 1.0f), i % 2 == 0 ? 1.0f : 0.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::mix), "Mix", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::out), "Output", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, db));
    return l;
}

// ---------------------------------------------------------------------------------------------
// Presets. Patterns: X = 100%, x = 70%, o = 45%, . = 25%, - = off. Length sets the step count (8/16/32).
struct GPreset { const char* name; std::vector<std::pair<const char*, float>> v; const char* pattern = nullptr; };
inline const std::vector<GPreset>& factoryPresets()
{
    // style: 0 Gate 1 Expander 2 Smooth 3 Upward 4 Ducker   rate: 0 1/4 1/8 1/8T 1/16 1/16T 1/32   mode: 0 Dyn 1 Pattern 2 Both
    static const std::vector<GPreset> p {
        // ---------------- DYNAMICS ----------------
        { "DYN Init Gate", {} },
        { "DYN Kick: Tight Gate", { { "threshold", -30 }, { "range", 60 }, { "attack", 0.1f }, { "hold", 30 }, { "release", 80 }, { "lookahead", 1.0f } } },
        { "DYN Kick: Remove Bleed", { { "threshold", -26 }, { "key_hp", 40 }, { "key_lp", 180 }, { "hold", 40 }, { "release", 90 }, { "lookahead", 1.5f }, { "hysteresis", 6 } } },
        { "DYN Kick: Short & Punchy", { { "threshold", -24 }, { "attack", 0.05f }, { "hold", 10 }, { "release", 40 }, { "lookahead", 0.8f } } },
        { "DYN Snare: Clean Hits", { { "threshold", -28 }, { "key_hp", 150 }, { "hold", 40 }, { "release", 120 }, { "lookahead", 1.0f } } },
        { "DYN Snare: Fat & Ringy", { { "threshold", -30 }, { "range", 18 }, { "hold", 60 }, { "release", 300 }, { "key_hp", 150 } } },
        { "DYN Toms: Remove Spill", { { "threshold", -32 }, { "hold", 80 }, { "release", 250 }, { "hysteresis", 6 }, { "key_hp", 60 }, { "key_lp", 3000 }, { "lookahead", 2.0f } } },
        { "DYN Hi-Hat: Tight", { { "threshold", -35 }, { "key_hp", 6000 }, { "hold", 10 }, { "release", 40 }, { "range", 30 } } },
        { "DYN Overheads: Gentle Expand", { { "style", 1 }, { "threshold", -35 }, { "ratio", 2 }, { "range", 12 }, { "release", 200 } } },
        { "DYN Room Mic: Cut The Tail", { { "style", 2 }, { "threshold", -30 }, { "release", 220 }, { "range", 30 } } },
        { "DYN Drum Bus: Punch Gate", { { "threshold", -25 }, { "range", 20 }, { "hold", 20 }, { "release", 60 } } },
        { "DYN 80s Gated Snare Verb", { { "threshold", -30 }, { "range", 80 }, { "hold", 220 }, { "release", 15 }, { "attack", 0.05f } } },
        { "DYN Clap: Tight Tail", { { "threshold", -28 }, { "hold", 25 }, { "release", 60 }, { "key_hp", 800 } } },
        { "DYN 808: Clean Sustain", { { "style", 2 }, { "threshold", -45 }, { "range", 40 }, { "release", 160 }, { "key_lp", 300 } } },
        { "DYN Vocal: Breath Reducer", { { "style", 1 }, { "threshold", -45 }, { "ratio", 2.5f }, { "range", 12 }, { "hysteresis", 4 }, { "attack", 2 }, { "release", 150 } } },
        { "DYN Vocal: Noise Floor Clean", { { "style", 2 }, { "threshold", -55 }, { "range", 30 }, { "release", 200 }, { "attack", 3 } } },
        { "DYN Podcast: Room Tamer", { { "style", 1 }, { "threshold", -48 }, { "ratio", 3 }, { "range", 18 }, { "release", 250 }, { "key_hp", 120 } } },
        { "DYN Guitar: Amp Hiss Kill", { { "threshold", -50 }, { "range", 40 }, { "hold", 50 }, { "release", 150 }, { "key_lp", 6000 } } },
        { "DYN Guitar: Tight Metal Riffs", { { "threshold", -40 }, { "attack", 0.05f }, { "hold", 5 }, { "release", 25 }, { "hysteresis", 8 }, { "lookahead", 1.0f } } },
        { "DYN Bass: Tight DI", { { "threshold", -45 }, { "release", 60 }, { "hold", 15 } } },
        { "DYN Synth: Noise Gate", { { "threshold", -60 }, { "release", 200 }, { "range", 50 } } },
        { "DYN Sample: Chop Tails", { { "threshold", -24 }, { "hold", 0 }, { "release", 30 } } },
        { "DYN Transparent Lookahead", { { "threshold", -42 }, { "lookahead", 5.0f }, { "attack", 0.5f }, { "hysteresis", 6 }, { "release", 180 } } },
        { "DYN Gentle 1:2 Cleanup", { { "style", 1 }, { "threshold", -50 }, { "ratio", 2 }, { "range", 10 }, { "release", 300 } } },
        { "DYN Upward: Transient Lift", { { "style", 3 }, { "threshold", -20 }, { "ratio", 2 }, { "range", 6 }, { "attack", 1 }, { "release", 80 } } },
        { "DYN Upward: Drum Snap", { { "style", 3 }, { "threshold", -22 }, { "ratio", 3 }, { "range", 9 }, { "attack", 0.3f }, { "release", 50 } } },
        { "DYN Ducker: Sidechain Pump", { { "style", 4 }, { "sidechain", 1 }, { "threshold", -30 }, { "range", 14 }, { "ratio", 20 }, { "attack", 1 }, { "release", 180 } } },
        { "DYN Ducker: Kick Ducks Bass", { { "style", 4 }, { "sidechain", 1 }, { "key_lp", 150 }, { "threshold", -28 }, { "range", 10 }, { "ratio", 20 }, { "attack", 0.5f }, { "release", 120 } } },
        { "DYN Ducker: Vocal Over Music", { { "style", 4 }, { "sidechain", 1 }, { "threshold", -40 }, { "range", 6 }, { "ratio", 4 }, { "attack", 10 }, { "release", 400 }, { "key_hp", 150 } } },
        { "DYN MIDI Triggered Gate", { { "midi_trigger", 1 }, { "threshold", 0 }, { "attack", 1 }, { "release", 40 }, { "hold", 0 } } },
        // ---------------- PATTERN (trance gate) ----------------
        { "PAT Trance 1/16 Classic", { { "mode", 1 }, { "gate_length", 0.5f } }, "XXXXXXXXXXXXXXXX" },
        { "PAT Trance Offbeat Pump", { { "mode", 1 } }, "-XXX-XXX-XXX-XXX" },
        { "PAT Trance Stutter", { { "mode", 1 } }, "XX-XX-X-XX-XX-X-" },
        { "PAT Trance Rolling", { { "mode", 1 } }, "X-XXX-XX-XXX-XXX" },
        { "PAT Uplifting Accents", { { "mode", 1 }, { "gate_length", 0.6f } }, "XoxoXoxoXoxoXoxo" },
        { "PAT Psy Gallop", { { "mode", 1 }, { "gate_length", 0.45f } }, "-xXx-xXx-xXx-xXx" },
        { "PAT House Offbeat 1/8", { { "mode", 1 }, { "rate", 1 } }, "oXoXoXoX" },
        { "PAT Fake Sidechain 1/4", { { "mode", 1 }, { "gate_length", 1.0f }, { "p_attack", 30 }, { "p_release", 5 } }, "-.oxXXXX-.oxXXXX" },
        { "PAT Reverse Pump", { { "mode", 1 }, { "gate_length", 1.0f }, { "p_attack", 5 }, { "p_release", 20 } }, "XXXXxo.-XXXXxo.-" },
        { "PAT Half-Time Chop", { { "mode", 1 } }, "X---X---X-X-----" },
        { "PAT Triplet Gate", { { "mode", 1 }, { "rate", 2 } }, "X-XX-XX-XX-XX-XX" },
        { "PAT Triplet 1/16 Roll", { { "mode", 1 }, { "rate", 4 } }, "XX-XX-XX-XX-XXX-" },
        { "PAT Morse Code", { { "mode", 1 } }, "X-X-XXX-X-XXX---" },
        { "PAT Glitch 1/32", { { "mode", 1 }, { "rate", 5 }, { "gate_length", 0.7f } }, "XX-X--XX-X-XX---XXX-X--X-XX-X---" },
        { "PAT Stutter Build 1/32", { { "mode", 1 }, { "rate", 5 } }, "XXXXXXXX--------XXXX----XX--X-X-" },
        { "PAT Breakbeat Chop", { { "mode", 1 } }, "X--X--X---X-X---" },
        { "PAT Reggaeton / Dembow", { { "mode", 1 } }, "X--X--X-X--X--X-" },
        { "PAT Gated Pad 1/8", { { "mode", 1 }, { "rate", 1 }, { "gate_length", 0.8f }, { "p_attack", 8 }, { "p_release", 30 } }, "X-X-X-X-" },
        { "PAT Arp Gate", { { "mode", 1 } }, "X-x-X-x-X-x-X-x-" },
        { "PAT Swing 16ths", { { "mode", 1 }, { "swing", 0.5f } }, "XXXXXXXXXXXXXXXX" },
        { "PAT Swing Shuffle", { { "mode", 1 }, { "swing", 0.35f } }, "X-XX-XX-X-XX-XX-" },
        { "PAT Euclid 3 of 8", { { "mode", 1 }, { "rate", 1 } }, "X--X--X-" },
        { "PAT Euclid 5 of 16", { { "mode", 1 } }, "X--X--X--X--X---" },
        { "PAT Euclid 7 of 16", { { "mode", 1 } }, "X-X-XX-X-X-XX-X-" },
        { "PAT Stereo Ping-Pong", { { "mode", 1 }, { "stereo_offset", 1 } }, "X-X-X-X-X-X-X-X-" },
        { "PAT Stereo Wide Chop", { { "mode", 1 }, { "stereo_offset", 2 } }, "XX--X-X-XX--X-X-" },
        { "PAT Fade-In Build (2 Bars)", { { "mode", 1 }, { "gate_length", 1.0f } }, "........oooooooxxxxxxxxXXXXXXXXX" },
        { "PAT Tremolo Smooth 1/8", { { "mode", 1 }, { "rate", 1 }, { "gate_length", 0.5f }, { "p_attack", 40 }, { "p_release", 60 }, { "depth", 0.6f } }, "XXXXXXXX" },
        { "PAT Tremolo Fast 1/32", { { "mode", 1 }, { "rate", 5 }, { "gate_length", 0.5f }, { "p_attack", 3 }, { "p_release", 6 }, { "depth", 0.5f } }, "XXXXXXXXXXXXXXXX" },
        { "PAT Hard Chop 1/8T", { { "mode", 1 }, { "rate", 2 }, { "gate_length", 0.4f }, { "p_attack", 0.3f }, { "p_release", 2 } }, "XXXXXXXXXXXXXXXX" },
        { "PAT Riser Stutter", { { "mode", 1 }, { "rate", 5 } }, "X-X-X-X-XXXXXXXX--XX--XX-XXX-XXX" },
        { "PAT Vocal Chop", { { "mode", 1 }, { "gate_length", 0.9f } }, "X-----X-X---X---" },
        { "PAT Hi-Hat Groove", { { "mode", 1 } }, "X-xXX-xXX-xXX-xX" },
        { "PAT Drum & Bass", { { "mode", 1 } }, "X---X-----X-X---" },
        { "PAT Future Bass Chop", { { "mode", 1 }, { "gate_length", 0.8f } }, "X-XX--X-X--XX-X-" },
        { "PAT Dubstep Wobble 1/16T", { { "mode", 1 }, { "rate", 4 }, { "gate_length", 0.7f }, { "p_attack", 4 }, { "p_release", 15 } }, "XxoXxoXxoXxoXxoX" },
        { "PAT Bounce 1/8 Swing", { { "mode", 1 }, { "rate", 1 }, { "swing", 0.45f }, { "gate_length", 0.7f } }, "XxXxXxXx" },
        { "PAT Minimal Click", { { "mode", 1 }, { "gate_length", 0.25f }, { "p_attack", 0.2f }, { "p_release", 3 } }, "X---x---X---x-X-" },
        // ---------------- BOTH ----------------
        { "BOTH Clean + Trance Gate", { { "mode", 2 }, { "threshold", -50 }, { "range", 40 }, { "release", 150 } }, "XXXXXXXXXXXXXXXX" },
        { "BOTH Noise Gate + Offbeat", { { "mode", 2 }, { "threshold", -55 }, { "release", 200 } }, "-XXX-XXX-XXX-XXX" },
        { "BOTH Drum Gate + Stutter", { { "mode", 2 }, { "threshold", -28 }, { "hold", 20 }, { "release", 60 } }, "XX-XX-X-XX-XX-X-" },
    };
    return p;
}
