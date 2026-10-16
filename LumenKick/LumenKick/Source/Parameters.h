#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace IDs
{
    inline const juce::String tune = "tune", keyTrack = "key_track", pitch = "pitch_amount", pitchTime = "pitch_time", pitchCurve = "pitch_curve",
        attack = "attack", hold = "hold", decay = "decay", decayCurve = "decay_curve", shape = "shape", phase = "start_phase", subLvl = "sub_level",
        clickOn = "click_on", clickType = "click_type", clickLvl = "click_level", clickDecay = "click_decay", clickTone = "click_tone",
        smpOn = "sample_on", smpLvl = "sample_level", smpPitch = "sample_pitch", smpStart = "sample_start", smpDecay = "sample_decay", smpHp = "sample_hp", smpTrack = "sample_key_track",
        drive = "drive", punch = "punch", low = "low", highCut = "high_cut", clip = "clip", vel = "velocity", out = "out",
        splitSens = "split_sensitivity", splitSep = "split_separation";
}

inline juce::StringArray clickNames() { return { "Noise", "Tick", "Snap", "Thump" }; }
inline juce::String noteName (float hz)
{
    static const char* n[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const double m = 69.0 + 12.0 * std::log2 (std::max (1.0f, hz) / 440.0);
    const int mi = (int) std::round (m);
    const int cents = (int) std::round ((m - mi) * 100.0);
    return juce::String (n[(mi % 12 + 12) % 12]) + juce::String (mi / 12 - 1) + (cents != 0 ? (cents > 0 ? " +" : " ") + juce::String (cents) + "c" : juce::String());
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const String& id) { return ParameterID { id, 1 }; };
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });
    auto db = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; });
    auto ms = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 10.0f ? String (v, 1) + " ms" : String (roundToInt (v)) + " ms"; });
    auto st = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " st"; });
    auto one = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2); });
    auto range = [] (float a, float b, float centre) { NormalisableRange<float> r (a, b); r.setSkewForCentre (centre); return r; };
    auto f = [&] (const String& id, const String& nm, NormalisableRange<float> r, float d, AudioParameterFloatAttributes at) { l.add (std::make_unique<AudioParameterFloat> (pid (id), nm, r, d, at)); };
    auto b = [&] (const String& id, const String& nm, bool d) { l.add (std::make_unique<AudioParameterBool> (pid (id), nm, d)); };

    f (IDs::tune, "Tune", range (25.0f, 200.0f, 55.0f), 49.0f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " Hz " + noteName (v); }));
    b (IDs::keyTrack, "Key Track", true);
    f (IDs::pitch, "Pitch Amount", NormalisableRange<float> (0.0f, 60.0f, 0.1f), 36.0f, st);
    f (IDs::pitchTime, "Pitch Time", range (1.0f, 300.0f, 40.0f), 35.0f, ms);
    f (IDs::pitchCurve, "Pitch Curve", NormalisableRange<float> (0.3f, 3.0f), 1.0f, one);
    f (IDs::attack, "Attack", NormalisableRange<float> (0.0f, 20.0f, 0.1f), 0.0f, ms);
    f (IDs::hold, "Hold", range (0.0f, 500.0f, 60.0f), 50.0f, ms);
    f (IDs::decay, "Decay", range (20.0f, 3000.0f, 400.0f), 380.0f, ms);
    f (IDs::decayCurve, "Decay Curve", NormalisableRange<float> (0.3f, 3.0f), 1.0f, one);
    f (IDs::shape, "Shape", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct);
    f (IDs::phase, "Start Phase", NormalisableRange<float> (0.0f, 180.0f, 1.0f), 90.0f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " deg"; }));
    f (IDs::subLvl, "Sub Level", NormalisableRange<float> (-48.0f, 6.0f, 0.1f), 0.0f, db);
    b (IDs::clickOn, "Click On", true);
    l.add (std::make_unique<AudioParameterChoice> (pid (IDs::clickType), "Click Type", clickNames(), 0));
    f (IDs::clickLvl, "Click Level", NormalisableRange<float> (-48.0f, 6.0f, 0.1f), -14.0f, db);
    f (IDs::clickDecay, "Click Decay", range (0.3f, 60.0f, 6.0f), 5.0f, ms);
    f (IDs::clickTone, "Click Tone", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct);
    b (IDs::smpOn, "Sample On", true);
    f (IDs::smpLvl, "Sample Level", NormalisableRange<float> (-48.0f, 6.0f, 0.1f), 0.0f, db);
    f (IDs::smpPitch, "Sample Pitch", NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, st);
    f (IDs::smpStart, "Sample Start", range (0.0f, 200.0f, 20.0f), 0.0f, ms);
    f (IDs::smpDecay, "Sample Length", range (0.0f, 2000.0f, 200.0f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 0.5f ? String ("Full") : String (roundToInt (v)) + " ms"; }));
    f (IDs::smpHp, "Sample High-Pass", range (20.0f, 4000.0f, 300.0f), 20.0f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 21.0f ? String ("Off") : (v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String (roundToInt (v)) + " Hz"); }));
    b (IDs::smpTrack, "Sample Key Track", false);
    f (IDs::drive, "Drive", NormalisableRange<float> (0.0f, 30.0f, 0.1f), 0.0f, db);
    f (IDs::punch, "Punch", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct);
    f (IDs::low, "Low", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, db);
    f (IDs::highCut, "High Cut", range (1000.0f, 20000.0f, 6000.0f), 20000.0f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v > 19500.0f ? String ("Off") : String (v / 1000.0f, 1) + " kHz"; }));
    b (IDs::clip, "Clip", true);
    f (IDs::vel, "Velocity", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct);
    f (IDs::out, "Output", NormalisableRange<float> (-24.0f, 6.0f, 0.1f), -3.0f, db);
    f (IDs::splitSens, "Split Sensitivity", NormalisableRange<float> (0.0f, 1.0f), 0.6f, pct);
    f (IDs::splitSep, "Split Separation", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct);
    return l;
}

// Presets (tune in Hz: F1 43.7, G1 49, A1 55, C2 65.4) click_type 0 noise 1 tick 2 snap 3 thump
struct KPreset { const char* name; std::vector<std::pair<const char*, float>> v; };
inline const std::vector<KPreset>& factoryPresets()
{
    static const std::vector<KPreset> p {
        { "Init Kick", {} },
        // ---------------- HOUSE / TECHNO ----------------
        { "HOUSE Classic 909-ish", { { "tune", 52 }, { "pitch_amount", 30 }, { "pitch_time", 28 }, { "hold", 30 }, { "decay", 320 }, { "click_type", 1 }, { "click_level", -16 }, { "drive", 4 } } },
        { "HOUSE Deep Round", { { "tune", 46 }, { "pitch_amount", 24 }, { "pitch_time", 40 }, { "hold", 60 }, { "decay", 450 }, { "click_level", -24 }, { "high_cut", 8000 } } },
        { "HOUSE Tech-House Thud", { { "tune", 50 }, { "pitch_amount", 34 }, { "pitch_time", 22 }, { "hold", 40 }, { "decay", 260 }, { "click_type", 3 }, { "click_level", -10 }, { "punch", 0.5f } } },
        { "TECHNO Rumble Base", { { "tune", 44 }, { "pitch_amount", 30 }, { "pitch_time", 30 }, { "hold", 80 }, { "decay", 700 }, { "drive", 10 }, { "low", 3 } } },
        { "TECHNO Industrial Hit", { { "tune", 48 }, { "pitch_amount", 40 }, { "pitch_time", 18 }, { "hold", 30 }, { "decay", 300 }, { "shape", 0.5f }, { "drive", 18 }, { "click_type", 0 }, { "click_level", -8 } } },
        { "TECHNO Minimal Tick", { { "tune", 55 }, { "pitch_amount", 20 }, { "pitch_time", 12 }, { "hold", 10 }, { "decay", 180 }, { "click_type", 1 }, { "click_level", -12 }, { "click_tone", 0.8f } } },
        // ---------------- EDM / BIG ROOM / HARD ----------------
        { "EDM Big Room Punch", { { "tune", 49 }, { "pitch_amount", 42 }, { "pitch_time", 30 }, { "hold", 70 }, { "decay", 420 }, { "drive", 12 }, { "punch", 0.6f }, { "click_level", -10 } } },
        { "EDM Festival Long Tail", { { "tune", 46 }, { "pitch_amount", 40 }, { "pitch_time", 35 }, { "hold", 120 }, { "decay", 900 }, { "drive", 14 }, { "low", 2 } } },
        { "EDM Future House Bounce", { { "tune", 58 }, { "pitch_amount", 30 }, { "pitch_time", 20 }, { "hold", 40 }, { "decay", 240 }, { "click_type", 2 }, { "click_level", -12 } } },
        { "HARDSTYLE Distorted Tok", { { "tune", 55 }, { "pitch_amount", 48 }, { "pitch_time", 45 }, { "pitch_curve", 0.7f }, { "hold", 120 }, { "decay", 500 }, { "shape", 0.6f }, { "drive", 26 }, { "punch", 0.7f } } },
        { "HARDSTYLE Reverse-Bass Kick", { { "tune", 52 }, { "pitch_amount", 36 }, { "pitch_time", 60 }, { "hold", 150 }, { "decay", 350 }, { "shape", 0.8f }, { "drive", 28 } } },
        { "PSYTRANCE Tight Clicky", { { "tune", 60 }, { "pitch_amount", 36 }, { "pitch_time", 10 }, { "hold", 20 }, { "decay", 180 }, { "click_type", 1 }, { "click_level", -8 }, { "click_tone", 0.7f } } },
        { "HARDCORE Gabber", { { "tune", 60 }, { "pitch_amount", 50 }, { "pitch_time", 25 }, { "hold", 100 }, { "decay", 400 }, { "shape", 1 }, { "drive", 30 }, { "clip", 1 } } },
        { "DnB Snappy Short", { { "tune", 62 }, { "pitch_amount", 30 }, { "pitch_time", 14 }, { "hold", 15 }, { "decay", 160 }, { "click_type", 2 }, { "click_level", -10 } } },
        { "DUBSTEP Heavy Body", { { "tune", 47 }, { "pitch_amount", 38 }, { "pitch_time", 30 }, { "hold", 60 }, { "decay", 380 }, { "drive", 16 }, { "punch", 0.8f } } },
        // ---------------- HIP-HOP / TRAP ----------------
        { "808 Long Sub (tuned)", { { "tune", 49 }, { "pitch_amount", 12 }, { "pitch_time", 40 }, { "hold", 200 }, { "decay", 2200 }, { "click_level", -30 }, { "start_phase", 0 } } },
        { "808 Distorted Trap", { { "tune", 44 }, { "pitch_amount", 14 }, { "pitch_time", 50 }, { "hold", 250 }, { "decay", 2000 }, { "drive", 18 }, { "shape", 0.3f } } },
        { "808 Short Bounce", { { "tune", 55 }, { "pitch_amount", 16 }, { "pitch_time", 30 }, { "hold", 60 }, { "decay", 600 } } },
        { "BOOM BAP Dusty Kick", { { "tune", 58 }, { "pitch_amount", 26 }, { "pitch_time", 25 }, { "hold", 30 }, { "decay", 260 }, { "drive", 8 }, { "high_cut", 6000 }, { "click_type", 3 } } },
        { "TRAP Punchy Knock", { { "tune", 60 }, { "pitch_amount", 32 }, { "pitch_time", 18 }, { "hold", 25 }, { "decay", 220 }, { "punch", 0.7f }, { "click_type", 2 } } },
        // ---------------- POP / ACOUSTIC-ISH ----------------
        { "POP Tight Modern", { { "tune", 56 }, { "pitch_amount", 28 }, { "pitch_time", 20 }, { "hold", 30 }, { "decay", 250 }, { "click_type", 1 }, { "click_level", -14 }, { "punch", 0.4f } } },
        { "ACOUSTIC-ish Felt Kick", { { "tune", 62 }, { "pitch_amount", 14 }, { "pitch_time", 15 }, { "hold", 10 }, { "decay", 200 }, { "click_type", 3 }, { "click_level", -8 }, { "high_cut", 5000 } } },
        { "LOFI Soft Thump", { { "tune", 54 }, { "pitch_amount", 20 }, { "pitch_time", 25 }, { "hold", 20 }, { "decay", 240 }, { "high_cut", 3500 }, { "drive", 6 } } },
        // ---------------- SAMPLE LAYER IDEAS ----------------
        { "LAYER Synth Sub + Sample Top", { { "sample_hp", 250 }, { "sample_level", -2 }, { "click_on", 0 }, { "decay", 450 } } },
        { "LAYER Sample Only (as loaded)", { { "sub_level", -48 }, { "click_on", 0 } } },
        { "LAYER Sample Transient Only", { { "sample_decay", 25 }, { "sample_hp", 150 }, { "click_on", 0 } } },
        { "LAYER Tuned Sample (key track)", { { "sample_key_track", 1 }, { "sub_level", -48 }, { "click_on", 0 } } },
        // ---------------- FX ----------------
        { "FX Laser Zap", { { "tune", 80 }, { "pitch_amount", 60 }, { "pitch_time", 120 }, { "pitch_curve", 0.6f }, { "hold", 50 }, { "decay", 300 } } },
        { "FX Sub Drop Boom", { { "tune", 32 }, { "pitch_amount", 24 }, { "pitch_time", 250 }, { "hold", 400 }, { "decay", 2500 }, { "click_on", 0 } } },
        { "FX Tom Kick", { { "tune", 110 }, { "pitch_amount", 12 }, { "pitch_time", 60 }, { "hold", 20 }, { "decay", 400 }, { "click_type", 3 } } },
    };
    return p;
}
