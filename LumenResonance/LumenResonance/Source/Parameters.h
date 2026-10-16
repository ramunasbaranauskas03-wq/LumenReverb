#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace IDs
{
    inline const juce::String depth = "depth", sharp = "sharpness", select = "selectivity", attack = "attack", release = "release",
        mode = "mode", res = "resolution", stereo = "stereo", midAmt = "mid_amount", sideAmt = "side_amount", protect = "transient_protect",
        lowF = "low_freq", highF = "high_freq", focus = "resonance_focus", toneKeep = "tone_keep", recover = "recover", ear = "perceptual", sc = "sidechain", delta = "delta", autoGain = "auto_gain", mix = "mix", out = "out";
    inline juce::String bf (int i) { return "b" + juce::String (i + 1) + "_freq"; }
    inline juce::String bg (int i) { return "b" + juce::String (i + 1) + "_sens"; }
}

inline juce::StringArray modeNames()   { return { "Soft", "Hard" }; }
inline juce::StringArray resNames()    { return { "Low Latency", "Standard", "High Res" }; }
inline juce::StringArray stereoNames() { return { "Stereo Linked", "Mid / Side", "Left / Right" }; }
inline const float* bandDefaults() { static const float f[6] { 120, 400, 1500, 4000, 8000, 12000 }; return f; }

inline juce::String hzText (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String ((int) std::round (v)) + " Hz"; }

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const String& id) { return ParameterID { id, 1 }; };
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });
    auto db = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; });
    auto one = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1); });
    auto ms = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 10.0f ? String (v, 1) + " ms" : String (roundToInt (v)) + " ms"; });
    auto hz = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return hzText (v); });
    auto range = [] (float a, float b, float centre) { NormalisableRange<float> r (a, b); r.setSkewForCentre (centre); return r; };

    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::depth), "Depth", NormalisableRange<float> (0.0f, 20.0f, 0.1f), 5.0f, db));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::sharp), "Sharpness", NormalisableRange<float> (0.0f, 10.0f, 0.1f), 4.0f, one));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::select), "Selectivity", NormalisableRange<float> (0.0f, 10.0f, 0.1f), 3.5f, one));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::attack), "Attack", range (0.5f, 100.0f, 8.0f), 5.0f, ms));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::release), "Release", range (5.0f, 500.0f, 60.0f), 50.0f, ms));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::mode), "Mode", modeNames(), 0));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::res), "Resolution", resNames(), 1));
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::stereo), "Stereo", stereoNames(), 0));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::midAmt), "Mid Amount", NormalisableRange<float> (0.0f, 2.0f, 0.01f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::sideAmt), "Side Amount", NormalisableRange<float> (0.0f, 2.0f, 0.01f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::protect), "Transient Protect", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::focus), "Resonance Focus", NormalisableRange<float> (0.0f, 1.0f), 0.3f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::toneKeep), "Tone Keep", NormalisableRange<float> (0.0f, 1.0f), 0.3f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::recover), "Recover", NormalisableRange<float> (0.0f, 6.0f, 0.1f), 0.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 0.05f ? String ("Off") : "+" + String (v, 1) + " dB"; })));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::ear), "Perceptual", true));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::lowF), "Range Low", range (20.0f, 2000.0f, 200.0f), 20.0f, hz));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::highF), "Range High", range (1000.0f, 20000.0f, 8000.0f), 20000.0f, hz));
    for (int i = 0; i < 6; ++i)
    {
        const String nm = i == 0 ? "Low Shelf" : i == 5 ? "High Shelf" : "Band " + String (i + 1);
        l.add (std::make_unique<AudioParameterFloat> (pid (IDs::bf (i)), nm + " Freq", range (20.0f, 20000.0f, 1000.0f), bandDefaults()[i], hz));
        l.add (std::make_unique<AudioParameterFloat> (pid (IDs::bg (i)), nm + " Sensitivity", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, db));
    }
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::sc), "Key", StringArray { "Internal", "External Sidechain" }, 0));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::delta), "Delta", false));
    l.add (std::make_unique<AudioParameterBool> (pid (IDs::autoGain), "Auto Gain", false));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::mix), "Mix", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::out), "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, db));
    return l;
}

struct RPreset { const char* name; std::vector<std::pair<const char*, float>> v; };
inline const std::vector<RPreset>& factoryPresets()
{
    // mode 0 soft 1 hard   resolution 0 low lat 1 std 2 high   stereo 0 linked 1 M/S 2 L/R
    static const std::vector<RPreset> p {
        { "Init", {} },
        // ---------------- VOCALS ----------------
        { "VOX Gentle Smooth", { { "tone_keep", 0.5f }, { "depth", 3.5f }, { "sharpness", 4.5f }, { "selectivity", 4 }, { "low_freq", 150 } } },
        { "VOX Harsh Upper Mids", { { "tone_keep", 0.4f }, { "depth", 6 }, { "sharpness", 5 }, { "b3_freq", 2500 }, { "b3_sens", 6 }, { "b4_sens", 4 }, { "b1_sens", -18 } } },
        { "VOX De-Ess Natural", { { "depth", 7 }, { "sharpness", 3 }, { "selectivity", 2.5f }, { "attack", 1 }, { "release", 40 }, { "low_freq", 4000 }, { "b5_freq", 7000 }, { "b5_sens", 6 }, { "transient_protect", 0.1f } } },
        { "VOX De-Ess Strong", { { "depth", 12 }, { "sharpness", 2.5f }, { "selectivity", 2 }, { "attack", 0.5f }, { "release", 30 }, { "low_freq", 4500 }, { "mode", 1 }, { "transient_protect", 0 } } },
        { "VOX Nasal Fix", { { "resonance_focus", 0.6f }, { "depth", 6 }, { "sharpness", 6 }, { "b3_freq", 1000 }, { "b3_sens", 8 }, { "low_freq", 400 }, { "high_freq", 3000 } } },
        { "VOX Mud & Boxiness", { { "depth", 5 }, { "sharpness", 4 }, { "b2_freq", 350 }, { "b2_sens", 8 }, { "low_freq", 150 }, { "high_freq", 1200 } } },
        { "VOX Rap / Aggressive", { { "depth", 8 }, { "sharpness", 6 }, { "selectivity", 4.5f }, { "attack", 2 }, { "release", 35 }, { "b4_sens", 5 }, { "transient_protect", 0.7f } } },
        { "VOX Backing Vocals Blend", { { "depth", 9 }, { "sharpness", 3 }, { "selectivity", 2 }, { "release", 90 }, { "stereo", 1 }, { "side_amount", 1.4f } } },
        { "VOX Whistle Tones (Sharp)", { { "resonance_focus", 0.8f }, { "depth", 10 }, { "sharpness", 9 }, { "selectivity", 7 }, { "low_freq", 1500 }, { "resolution", 2 } } },
        // ---------------- INSTRUMENTS ----------------
        { "SYNTH Lead Tame Fizz", { { "depth", 6 }, { "sharpness", 5 }, { "low_freq", 2000 }, { "b5_sens", 5 } } },
        { "SYNTH Supersaw Smooth", { { "depth", 5 }, { "sharpness", 3.5f }, { "selectivity", 3 }, { "low_freq", 800 }, { "stereo", 1 }, { "side_amount", 1.3f } } },
        { "SYNTH Pluck Resonant Peaks", { { "depth", 8 }, { "sharpness", 8 }, { "selectivity", 6 }, { "attack", 3 }, { "transient_protect", 0.9f } } },
        { "SYNTH Pad Air Soften", { { "depth", 4 }, { "sharpness", 3 }, { "low_freq", 5000 }, { "release", 150 } } },
        { "GTR Acoustic Strings", { { "depth", 5 }, { "sharpness", 6 }, { "low_freq", 200 }, { "b2_freq", 250 }, { "b2_sens", 4 }, { "transient_protect", 0.8f } } },
        { "GTR Distorted Fizz", { { "depth", 8 }, { "sharpness", 5 }, { "low_freq", 2500 }, { "mode", 1 } } },
        { "PIANO Ringing Notes", { { "resonance_focus", 0.8f }, { "tone_keep", 0.6f }, { "depth", 5 }, { "sharpness", 8 }, { "selectivity", 6 }, { "release", 120 }, { "resolution", 2 }, { "transient_protect", 0.8f } } },
        { "BRASS / Strings Edge", { { "depth", 5 }, { "sharpness", 5 }, { "low_freq", 1200 }, { "high_freq", 8000 } } },
        // ---------------- DRUMS ----------------
        { "DRUM Snare Ring", { { "resonance_focus", 0.9f }, { "tone_keep", 0 }, { "depth", 9 }, { "sharpness", 9 }, { "selectivity", 6 }, { "low_freq", 150 }, { "high_freq", 1500 }, { "release", 80 }, { "transient_protect", 1 } } },
        { "DRUM Hi-Hat Harshness", { { "depth", 7 }, { "sharpness", 4 }, { "low_freq", 3000 }, { "attack", 1 }, { "transient_protect", 0.6f } } },
        { "DRUM Cymbal Wash", { { "depth", 6 }, { "sharpness", 3 }, { "low_freq", 2000 }, { "release", 120 }, { "stereo", 1 } } },
        { "DRUM Room Resonances", { { "resonance_focus", 1.0f }, { "tone_keep", 0 }, { "depth", 6 }, { "sharpness", 8 }, { "selectivity", 5 }, { "low_freq", 80 }, { "high_freq", 800 }, { "resolution", 2 }, { "transient_protect", 1 } } },
        { "DRUM Bus Smooth", { { "depth", 3 }, { "sharpness", 4 }, { "transient_protect", 1 }, { "release", 60 } } },
        // ---------------- BASS ----------------
        { "BASS Boomy Notes", { { "tone_keep", 0.5f }, { "resonance_focus", 0.5f }, { "depth", 6 }, { "sharpness", 7 }, { "selectivity", 5 }, { "low_freq", 30 }, { "high_freq", 300 }, { "resolution", 2 }, { "release", 150 } } },
        { "BASS Growl Fizz Control", { { "depth", 6 }, { "sharpness", 4 }, { "low_freq", 1500 }, { "mode", 1 } } },
        // ---------------- MIX / MASTER ----------------
        { "MIX Bus Gentle Glue", { { "recover", 1.0f }, { "depth", 2.5f }, { "sharpness", 3 }, { "selectivity", 4 }, { "release", 100 }, { "transient_protect", 0.8f }, { "low_freq", 60 } } },
        { "MASTER Transparent Polish", { { "recover", 1.5f }, { "tone_keep", 0.5f }, { "depth", 2 }, { "sharpness", 4 }, { "selectivity", 5 }, { "release", 120 }, { "resolution", 2 }, { "transient_protect", 0.9f }, { "low_freq", 100 } } },
        { "MASTER Harsh Top Control", { { "depth", 3 }, { "sharpness", 3.5f }, { "low_freq", 2500 }, { "release", 90 }, { "transient_protect", 0.8f } } },
        { "MASTER Side Harshness Only", { { "depth", 4 }, { "stereo", 1 }, { "mid_amount", 0.2f }, { "side_amount", 1.5f }, { "low_freq", 1500 }, { "transient_protect", 0.8f } } },
        { "MASTER Low-Mid Mud", { { "depth", 3 }, { "sharpness", 4 }, { "low_freq", 120 }, { "high_freq", 900 }, { "resolution", 2 }, { "release", 150 } } },
        // ---------------- SIDECHAIN / CREATIVE ----------------
        { "SC Unmask: Duck Music Under Vocal", { { "sidechain", 1 }, { "depth", 6 }, { "sharpness", 3 }, { "selectivity", 0.5f }, { "low_freq", 300 }, { "high_freq", 6000 }, { "attack", 5 }, { "release", 80 } } },
        { "SC Unmask: Bass Under Kick", { { "sidechain", 1 }, { "depth", 8 }, { "sharpness", 3 }, { "selectivity", 0 }, { "low_freq", 30 }, { "high_freq", 250 }, { "attack", 1 }, { "release", 60 } } },
        { "FX Spectral Flatten (Extreme)", { { "recover", 6 }, { "tone_keep", 0 }, { "resonance_focus", 0 }, { "depth", 20 }, { "sharpness", 2 }, { "selectivity", 0 }, { "mode", 1 }, { "transient_protect", 0 } } },
        { "FX Listen to Removed (Delta)", { { "depth", 8 }, { "delta", 1 } } },
        { "UTIL Low Latency Tracking", { { "resolution", 0 }, { "depth", 5 } } },
        // ---------------- BALANCE (suppress + recover) ----------------
        { "BAL Mix Clarity (Cut + Recover)", { { "depth", 3 }, { "recover", 3 }, { "sharpness", 3 }, { "selectivity", 3 }, { "release", 150 }, { "transient_protect", 0.8f }, { "low_freq", 80 } } },
        { "BAL Vocal Presence Recover", { { "depth", 4 }, { "recover", 2.5f }, { "tone_keep", 0.6f }, { "low_freq", 200 }, { "b4_freq", 3000 }, { "b4_sens", 3 } } },
        { "BAL Master Even Tone", { { "depth", 2 }, { "recover", 2 }, { "sharpness", 2.5f }, { "selectivity", 4 }, { "resolution", 2 }, { "release", 200 }, { "transient_protect", 0.9f }, { "low_freq", 60 } } },
        { "BAL Dull Recording Revive", { { "depth", 1.5f }, { "recover", 5 }, { "sharpness", 2 }, { "low_freq", 2000 }, { "release", 250 } } },
    };
    return p;
}
