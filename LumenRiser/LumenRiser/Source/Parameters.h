#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/RiserEngine.h"

namespace IDs
{
    inline const juce::String mode = "mode", rise = "rise", length = "length", period = "period", offset = "offset", curve = "curve", gap = "gap", key = "key",
        noiseOn = "noise_on", noiseLvl = "noise_level", noiseType = "noise_type", noiseFilter = "noise_filter", noiseRes = "noise_res",
        synthOn = "synth_on", synthLvl = "synth_level", stack = "synth_stack", synthOct = "synth_octave", synthPitch = "synth_pitch", detune = "synth_detune",
        shepOn = "shepard_on", shepLvl = "shepard_level", shepSpeed = "shepard_speed",
        impactOn = "impact_on", impactLvl = "impact_level", impactDecay = "impact_decay",
        downOn = "down_on", downLvl = "down_level", downLen = "down_length",
        space = "space", size = "size", tailCut = "tail_cut",
        fxOn = "fx_on", fxFilter = "fx_filter", fxAmt = "fx_amount", fxRes = "fx_res", stutter = "stutter", roll = "roll", fxVerb = "fx_reverb", width = "width", fxMix = "fx_mix",
        levelMatch = "level_match", clubSafe = "club_safe", genLvl = "gen_level", out = "output";
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    using namespace lumen::riser;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const String& id) { return ParameterID { id, 1 }; };
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });
    auto db  = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v <= -59.9f ? String ("-inf") : String (v, 1) + " dB"; });
    auto c = [&] (const String& id, const String& nm, StringArray ch, int d) { l.add (std::make_unique<AudioParameterChoice> (pid (id), nm, ch, d)); };
    auto b = [&] (const String& id, const String& nm, bool d) { l.add (std::make_unique<AudioParameterBool> (pid (id), nm, d)); };
    auto f = [&] (const String& id, const String& nm, float lo, float hi, float d, AudioParameterFloatAttributes a, float skew = 1.0f)
    { NormalisableRange<float> r (lo, hi); r.setSkewForCentre (skew == 1.0f ? (lo + hi) * 0.5f : skew); l.add (std::make_unique<AudioParameterFloat> (pid (id), nm, r, d, a)); };
    auto lvl = [&] (const String& id, const String& nm, float d) { f (id, nm, -60.0f, 6.0f, d, db, -12.0f); };

    c (IDs::mode, "Trigger Mode", modeNames(), AutoBars);
    f (IDs::rise, "Rise", 0.0f, 1.0f, 0.0f, pct);
    c (IDs::length, "Build Length", lengthNames(), 3);
    c (IDs::period, "Drop Every", periodNames(), 2);
    l.add (std::make_unique<AudioParameterInt> (pid (IDs::offset), "Bar Offset", 0, 31, 0));
    f (IDs::curve, "Curve", -1.0f, 1.0f, 0.35f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return std::abs (v) < 0.03f ? String ("Linear") : v > 0 ? "Slow start " + String (roundToInt (v * 100)) : "Fast start " + String (roundToInt (-v * 100)); }));
    c (IDs::gap, "Pre-Drop Gap", gapNames(), 0);
    c (IDs::key, "Key", StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 9);

    b (IDs::noiseOn, "Noise On", true);
    lvl (IDs::noiseLvl, "Noise Level", -6.0f);
    c (IDs::noiseType, "Noise Color", StringArray { "White", "Pink" }, 0);
    c (IDs::noiseFilter, "Noise Sweep", noiseFilterNames(), HpSweep);
    f (IDs::noiseRes, "Noise Resonance", 0.0f, 1.0f, 0.35f, pct);
    b (IDs::synthOn, "Synth On", true);
    lvl (IDs::synthLvl, "Synth Level", -8.0f);
    c (IDs::stack, "Synth Notes", stackNames(), StOctave);
    l.add (std::make_unique<AudioParameterInt> (pid (IDs::synthOct), "Synth Octave", 1, 6, 3));
    f (IDs::synthPitch, "Synth Pitch Rise", 0.0f, 36.0f, 12.0f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return "+" + String (v, 1) + " st"; }));
    f (IDs::detune, "Synth Detune", 0.0f, 1.0f, 0.5f, pct);
    b (IDs::shepOn, "Shepard On", false);
    lvl (IDs::shepLvl, "Shepard Level", -8.0f);
    f (IDs::shepSpeed, "Shepard Speed", 0.05f, 1.0f, 0.25f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " oct/beat"; }), 0.25f);
    b (IDs::impactOn, "Impact On", true);
    lvl (IDs::impactLvl, "Impact Level", -4.0f);
    f (IDs::impactDecay, "Impact Decay", 0.2f, 4.0f, 1.2f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " s"; }), 1.0f);
    b (IDs::downOn, "Downlifter On", false);
    lvl (IDs::downLvl, "Downlifter Level", -8.0f);
    c (IDs::downLen, "Downlifter Length", downNames(), 1);
    f (IDs::space, "Space", 0.0f, 1.0f, 0.4f, pct);
    f (IDs::size, "Space Size", 0.0f, 1.0f, 0.6f, pct);
    b (IDs::tailCut, "Cut Tail At Drop", false);

    b (IDs::fxOn, "FX On", true);
    c (IDs::fxFilter, "FX Filter", fxFilterNames(), FxHp);
    f (IDs::fxAmt, "FX Filter Amount", 0.0f, 1.0f, 0.6f, pct);
    f (IDs::fxRes, "FX Resonance", 0.0f, 1.0f, 0.3f, pct);
    f (IDs::stutter, "Stutter", 0.0f, 1.0f, 0.0f, pct);
    c (IDs::roll, "Stutter Roll", rollNames(), 1);
    f (IDs::fxVerb, "FX Reverb", 0.0f, 1.0f, 0.3f, pct);
    f (IDs::width, "FX Width", 0.0f, 1.0f, 0.3f, pct);
    f (IDs::fxMix, "FX Mix", 0.0f, 1.0f, 1.0f, pct);
    b (IDs::levelMatch, "Level Match", true);
    b (IDs::clubSafe, "Club Safe", true);
    lvl (IDs::genLvl, "Generator Level", 0.0f);
    f (IDs::out, "Output", -24.0f, 12.0f, 0.0f, db);
    return l;
}

// ------------------------------------------------------------------------------------------------
struct RPreset { const char* name; std::vector<std::pair<const char*, float>> v; };
inline const std::vector<RPreset>& factoryPresets()
{
    // modes: 0 auto 1 manual 2 midi | length 0=1 1=2 2=4 3=8 4=16 bars | period 0=4 1=8 2=16 3=32 4=64 | gap 0 off 1 1/16 2 1/8 3 beat 4 2 beats 5 bar
    static const std::vector<RPreset> p {
        { "Init: 8-bar build, drop every 16", {} },
        // ---------------- BIG ROOM / FESTIVAL ----------------
        { "BIG ROOM Classic White Noise + Snare Roll", { { "synth_on", 0 }, { "noise_filter", 0 }, { "stutter", 0.9f }, { "roll", 1 }, { "gap", 3 }, { "fx_amount", 0.7f } } },
        { "BIG ROOM Supersaw Scream (+24)", { { "synth_pitch", 24 }, { "synth_stack", 2 }, { "synth_level", -5 }, { "noise_level", -10 }, { "gap", 3 }, { "curve", 0.6f } } },
        { "BIG ROOM Drop Silence 1 Beat", { { "gap", 3 }, { "stutter", 0.6f }, { "impact_level", -2 } } },
        { "FESTIVAL 16-Bar Monster Build", { { "length", 4 }, { "period", 3 }, { "synth_pitch", 24 }, { "shepard_on", 1 }, { "stutter", 0.8f }, { "roll", 2 }, { "gap", 3 }, { "space", 0.6f } } },
        { "FESTIVAL Shepard Endless", { { "synth_on", 0 }, { "shepard_on", 1 }, { "shepard_level", -4 }, { "shepard_speed", 0.4f }, { "noise_level", -12 }, { "gap", 3 } } },
        // ---------------- PROGRESSIVE / HOUSE ----------------
        { "PROG HOUSE Smooth Sweep", { { "synth_on", 0 }, { "noise_filter", 1 }, { "noise_res", 0.6f }, { "curve", 0.0f }, { "fx_amount", 0.45f }, { "space", 0.6f } } },
        { "PROG HOUSE Euphoric Lift", { { "synth_stack", 4 }, { "synth_pitch", 12 }, { "noise_level", -9 }, { "space", 0.6f }, { "size", 0.8f } } },
        { "HOUSE 4-Bar Filter Lift", { { "length", 2 }, { "period", 1 }, { "synth_on", 0 }, { "noise_level", -14 }, { "fx_amount", 0.55f }, { "impact_on", 0 } } },
        { "TECH HOUSE Tight Roll", { { "length", 1 }, { "period", 1 }, { "synth_on", 0 }, { "stutter", 1.0f }, { "roll", 1 }, { "noise_level", -12 }, { "space", 0.2f } } },
        { "DEEP HOUSE Subtle Air", { { "synth_on", 0 }, { "noise_level", -18 }, { "fx_amount", 0.3f }, { "impact_on", 0 }, { "fx_reverb", 0.4f } } },
        // ---------------- TRANCE ----------------
        { "TRANCE Uplifter + Downlifter", { { "synth_stack", 1 }, { "synth_pitch", 12 }, { "down_on", 1 }, { "down_length", 2 }, { "space", 0.7f }, { "size", 0.85f } } },
        { "TRANCE 16-Bar Euphoria", { { "length", 4 }, { "period", 3 }, { "synth_stack", 4 }, { "synth_pitch", 12 }, { "noise_filter", 1 }, { "space", 0.7f } } },
        { "PSY Fast Riser", { { "length", 1 }, { "period", 1 }, { "synth_pitch", 24 }, { "curve", 0.7f }, { "stutter", 0.7f }, { "roll", 2 }, { "key", 4 } } },
        // ---------------- FUTURE / MELODIC ----------------
        { "FUTURE BASS Wobble Lift", { { "synth_stack", 4 }, { "synth_detune", 0.8f }, { "stutter", 0.5f }, { "roll", 0 }, { "gap", 2 } } },
        { "MELODIC DUBSTEP Cinematic", { { "shepard_on", 1 }, { "synth_stack", 3 }, { "space", 0.85f }, { "size", 0.95f }, { "impact_decay", 3.0f }, { "down_on", 1 } } },
        { "MELODIC TECHNO Dark Pressure", { { "synth_stack", 0 }, { "synth_octave", 2 }, { "synth_pitch", 7 }, { "noise_filter", 1 }, { "noise_res", 0.8f }, { "fx_filter", 2 }, { "space", 0.5f } } },
        // ---------------- HARD ----------------
        { "HARDSTYLE Reverse Bass Build", { { "synth_stack", 2 }, { "synth_pitch", 24 }, { "stutter", 1.0f }, { "roll", 2 }, { "gap", 3 }, { "impact_level", 0 } } },
        { "TECHNO Rumble Riser", { { "synth_on", 0 }, { "noise_type", 1 }, { "noise_filter", 2 }, { "space", 0.8f }, { "size", 0.9f }, { "impact_on", 0 } } },
        { "DnB 8-Bar Rush", { { "synth_pitch", 19 }, { "curve", 0.8f }, { "stutter", 0.8f }, { "roll", 2 }, { "gap", 2 } } },
        { "DUBSTEP Siren Rise", { { "synth_stack", 0 }, { "synth_pitch", 36 }, { "synth_detune", 0.2f }, { "noise_level", -14 }, { "gap", 3 } } },
        // ---------------- FX ONLY (process your track) ----------------
        { "FX ONLY High-Pass Build", { { "noise_on", 0 }, { "synth_on", 0 }, { "impact_on", 0 }, { "fx_amount", 0.8f }, { "fx_reverb", 0.5f } } },
        { "FX ONLY Low-Pass Breakdown", { { "noise_on", 0 }, { "synth_on", 0 }, { "impact_on", 0 }, { "fx_filter", 1 }, { "fx_amount", 0.7f }, { "fx_res", 0.5f } } },
        { "FX ONLY Snare Roll Gate", { { "noise_on", 0 }, { "synth_on", 0 }, { "impact_on", 0 }, { "fx_amount", 0.3f }, { "stutter", 1.0f }, { "roll", 1 } } },
        { "FX ONLY Wash Out", { { "noise_on", 0 }, { "synth_on", 0 }, { "impact_on", 0 }, { "fx_amount", 0.5f }, { "fx_reverb", 0.9f }, { "width", 0.8f }, { "size", 0.9f } } },
        // ---------------- ONE-SHOTS / TOOLS ----------------
        { "IMPACT Only (drop hit)", { { "noise_on", 0 }, { "synth_on", 0 }, { "fx_on", 0 }, { "impact_level", 0 }, { "impact_decay", 2.0f }, { "space", 0.6f } } },
        { "DOWNLIFTER Only", { { "noise_on", 0 }, { "synth_on", 0 }, { "fx_on", 0 }, { "impact_on", 0 }, { "down_on", 1 }, { "down_level", -4 }, { "down_length", 2 } } },
        { "MANUAL Automate RISE", { { "mode", 1 }, { "gap", 0 } } },
        { "MIDI Hold-to-Build", { { "mode", 2 } } },
        { "MIDI Hold Shepard", { { "mode", 2 }, { "synth_on", 0 }, { "shepard_on", 1 }, { "shepard_level", -4 } } },
    };
    return p;
}
