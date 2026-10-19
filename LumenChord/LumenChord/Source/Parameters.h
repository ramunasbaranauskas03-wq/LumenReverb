#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/ChordEngine.h"

namespace IDs
{
    inline const juce::String key = "key", scale = "scale", type = "chord_type", mode = "mode", inversion = "inversion", spread = "spread", octave = "octave",
        voiceLead = "voice_leading", bass = "bass_note", strum = "strum", humanize = "humanize", velocity = "velocity", inVel = "input_velocity",
        bars = "bars_per_chord", numSlots = "progression_length",
        arpOn = "arp_on", arpMode = "arp_mode", arpRate = "arp_rate", arpOct = "arp_octaves", arpGate = "arp_gate", arpSwing = "arp_swing", latch = "latch",
        sound = "preview_sound", soundType = "sound_type", soundLvl = "sound_level";
}
inline juce::StringArray modeNames()  { return { "One-Finger Chords", "Pads (C2-G2)", "Auto Progression", "Through (your chords)" }; }
inline juce::StringArray soundNames() { return { "Supersaw", "Pluck", "Pad", "Keys" }; }

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    using namespace lumen::chord;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const String& id) { return ParameterID { id, 1 }; };
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });
    auto c = [&] (const String& id, const String& nm, StringArray ch, int d) { l.add (std::make_unique<AudioParameterChoice> (pid (id), nm, ch, d)); };
    auto b = [&] (const String& id, const String& nm, bool d) { l.add (std::make_unique<AudioParameterBool> (pid (id), nm, d)); };
    StringArray sc; for (auto& s : scales()) sc.add (s.name);
    c (IDs::key, "Key", keyNames(), 9);
    c (IDs::scale, "Scale", sc, 0);
    c (IDs::type, "Chord Type", chordTypeNames(), Seventh);
    c (IDs::mode, "Mode", modeNames(), 0);
    c (IDs::inversion, "Inversion", StringArray { "Root", "1st", "2nd", "3rd" }, 0);
    c (IDs::spread, "Spread", StringArray { "Close", "Open (drop-2)", "Wide" }, 0);
    l.add (std::make_unique<AudioParameterInt> (pid (IDs::octave), "Octave", -2, 2, 0));
    b (IDs::voiceLead, "Voice Leading", true);
    b (IDs::bass, "Bass Note", true);
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::strum), "Strum", NormalisableRange<float> (0.0f, 120.0f, 0.1f), 0.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 0.05f ? String ("Off") : String (v, 1) + " ms"; })));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::humanize), "Humanize", NormalisableRange<float> (0.0f, 1.0f), 0.15f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::velocity), "Velocity", NormalisableRange<float> (0.05f, 1.0f), 0.8f, pct));
    b (IDs::inVel, "Use Input Velocity", true);
    c (IDs::bars, "Bars per Chord", StringArray { "1/2 bar", "1 bar", "2 bars", "4 bars" }, 1);
    l.add (std::make_unique<AudioParameterInt> (pid (IDs::numSlots), "Progression Length", 1, 8, 4));
    b (IDs::arpOn, "Arp On", false);
    c (IDs::arpMode, "Arp Mode", arpModeNames(), 0);
    c (IDs::arpRate, "Arp Rate", arpRateNames(), 4);
    l.add (std::make_unique<AudioParameterInt> (pid (IDs::arpOct), "Arp Octaves", 1, 4, 1));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::arpGate), "Arp Gate", NormalisableRange<float> (0.05f, 1.0f), 0.6f, pct));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::arpSwing), "Arp Swing", NormalisableRange<float> (0.0f, 0.6f), 0.0f, pct));
    b (IDs::latch, "Latch", false);
    b (IDs::sound, "Preview Sound", true);
    c (IDs::soundType, "Sound", soundNames(), 0);
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::soundLvl), "Sound Level", NormalisableRange<float> (-36.0f, 0.0f, 0.1f), -10.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
    return l;
}

// ------------------------------------------------------------------------------------------------
// Presets: key 0=C .. 9=A ; scale 0 minor 1 major ... ; chord_type 0 triad 1 7th 2 9th 3 add9 4 sus2 5 sus4 6 power 7 EDM stack
// progression: degrees (0 = i/I) with optional quality; "5,6,0,0" etc. q: a=auto M=maj m=min 7=dom7 J=maj7 n=m7 9=m9 N=maj9 s=sus4 2=sus2
struct CPreset
{
    const char* name; std::vector<std::pair<const char*, float>> v; const char* prog; const char* steps = nullptr;
};
inline const std::vector<CPreset>& factoryPresets()
{
    static const std::vector<CPreset> p {
        { "Init: A minor 7ths", {}, "0,5,2,6" },
        // ---------------- PROGRESSIVE / BIG ROOM ----------------
        { "PROG HOUSE Euphoric vi-IV-I-V", { { "key", 0 }, { "scale", 1 }, { "chord_type", 0 }, { "mode", 2 } }, "5,3,0,4" },
        { "PROG HOUSE Anthem (F minor)", { { "key", 5 }, { "chord_type", 3 }, { "mode", 2 } }, "5,6,0,0" },
        { "BIG ROOM Drop Stabs", { { "key", 7 }, { "chord_type", 7 }, { "mode", 2 }, { "arp_on", 1 }, { "arp_mode", 8 }, { "arp_rate", 4 }, { "arp_gate", 0.35f } }, "0,0,5,6", "X.X.X..X.X.X..X." },
        { "BIG ROOM Festival Lead Chords", { { "key", 2 }, { "chord_type", 7 }, { "mode", 2 }, { "bass_note", 0 } }, "0,5,2,6" },
        // ---------------- TRANCE ----------------
        { "TRANCE Classic i-VI-III-VII", { { "key", 9 }, { "chord_type", 0 }, { "mode", 2 }, { "arp_on", 1 }, { "arp_mode", 0 }, { "arp_rate", 4 }, { "arp_octaves", 2 } }, "0,5,2,6" },
        { "TRANCE Uplifting Gate", { { "key", 1 }, { "chord_type", 3 }, { "mode", 2 }, { "arp_on", 1 }, { "arp_mode", 8 }, { "arp_rate", 4 }, { "arp_gate", 0.5f } }, "5,6,0,4", "XxXXxXXxXxXXxXXx" },
        { "TRANCE Rolling Arp (Up-Down)", { { "key", 4 }, { "mode", 2 }, { "arp_on", 1 }, { "arp_mode", 2 }, { "arp_rate", 4 }, { "arp_octaves", 3 }, { "arp_gate", 0.45f } }, "0,3,5,6" },
        { "PSY Phrygian Dark", { { "key", 4 }, { "scale", 5 }, { "chord_type", 6 }, { "mode", 2 }, { "arp_on", 1 }, { "arp_rate", 7 }, { "arp_mode", 9 } }, "0,1,0,6" },
        // ---------------- FUTURE BASS / MELODIC ----------------
        { "FUTURE BASS IV-V-iii-vi (9ths)", { { "key", 2 }, { "scale", 1 }, { "chord_type", 2 }, { "mode", 2 }, { "spread", 1 } }, "3,4,2,5" },
        { "FUTURE BASS Supersaw Chops", { { "key", 7 }, { "scale", 1 }, { "chord_type", 2 }, { "mode", 2 }, { "arp_on", 1 }, { "arp_mode", 8 }, { "arp_rate", 5 }, { "arp_gate", 0.6f } }, "3,4,5,2", "X.XX.X.XX.X.X.XX" },
        { "MELODIC DUBSTEP Emotional", { { "key", 11 }, { "chord_type", 3 }, { "mode", 2 }, { "bars_per_chord", 2 } }, "5,6,0,4" },
        { "MELODIC DUBSTEP Epic Rise", { { "key", 1 }, { "chord_type", 7 }, { "mode", 2 } }, "3,5,6,0" },
        { "MELODIC TECHNO Hypnotic", { { "key", 9 }, { "scale", 4 }, { "chord_type", 4 }, { "mode", 2 }, { "arp_on", 1 }, { "arp_mode", 6 }, { "arp_rate", 4 } }, "0,0,6,3" },
        // ---------------- HOUSE / DEEP / LOFI ----------------
        { "DEEP HOUSE ii7-V7-Imaj7", { { "key", 3 }, { "scale", 1 }, { "chord_type", 1 }, { "mode", 2 }, { "spread", 1 } }, "1,4,0,5" },
        { "DEEP HOUSE Minor 9 Stabs", { { "key", 5 }, { "chord_type", 2 }, { "mode", 2 }, { "arp_on", 1 }, { "arp_mode", 8 }, { "arp_rate", 1 }, { "arp_gate", 0.3f }, { "arp_swing", 0.2f } }, "0,3,0,4", "..X...X...X..X.." },
        { "HOUSE Piano Stab Classic", { { "key", 0 }, { "scale", 1 }, { "chord_type", 0 }, { "mode", 2 }, { "arp_on", 1 }, { "arp_mode", 8 }, { "arp_rate", 1 }, { "arp_gate", 0.25f } }, "0,4,5,3", "X.XX..X.X.XX..X." },
        { "LO-FI Jazzy Maj7 Walk", { { "key", 5 }, { "scale", 1 }, { "chord_type", 1 }, { "mode", 2 }, { "strum", 25 }, { "humanize", 0.4f } }, "3,2,1,0" },
        { "LO-FI Sad Minor 9", { { "key", 4 }, { "chord_type", 2 }, { "mode", 2 }, { "strum", 30 }, { "humanize", 0.5f }, { "sound_type", 3 } }, "0,3,6,2" },
        // ---------------- POP / TROPICAL ----------------
        { "POP Axis I-V-vi-IV", { { "key", 0 }, { "scale", 1 }, { "chord_type", 0 }, { "mode", 2 } }, "0,4,5,3" },
        { "TROPICAL House Pluck", { { "key", 9 }, { "scale", 1 }, { "chord_type", 3 }, { "mode", 2 }, { "arp_on", 1 }, { "arp_mode", 0 }, { "arp_rate", 1 }, { "sound_type", 1 } }, "0,4,5,3" },
        { "EMO Piano vi-IV-I-V (C)", { { "key", 0 }, { "scale", 1 }, { "chord_type", 3 }, { "mode", 2 }, { "strum", 15 }, { "sound_type", 3 } }, "5,3,0,4" },
        // ---------------- DARK / TECHNO / HARD ----------------
        { "DARK Techno Minor Drone", { { "key", 9 }, { "chord_type", 6 }, { "mode", 2 }, { "bars_per_chord", 2 } }, "0,0,5,6" },
        { "DARK Harmonic Minor Tension", { { "key", 9 }, { "scale", 2 }, { "chord_type", 1 }, { "mode", 2 } }, "0,3,4,0" },
        { "HARDSTYLE Epic Minor", { { "key", 4 }, { "chord_type", 7 }, { "mode", 2 } }, "0,5,2,6" },
        { "DnB Liquid Soul", { { "key", 2 }, { "scale", 4 }, { "chord_type", 2 }, { "mode", 2 }, { "bars_per_chord", 2 } }, "0,3,6,2" },
        // ---------------- PLAYING MODES ----------------
        { "PLAY One-Finger 7ths (A minor)", { { "mode", 0 } }, "0,5,2,6" },
        { "PLAY One-Finger EDM Stacks", { { "mode", 0 }, { "chord_type", 7 } }, "0,5,2,6" },
        { "PLAY Pads C2-G2 (8 chords)", { { "mode", 1 }, { "progression_length", 8 } }, "0,5,2,6,3,0,4,4" },
        { "PLAY Through: Arp my chords", { { "mode", 3 }, { "arp_on", 1 }, { "arp_mode", 2 }, { "arp_rate", 4 }, { "arp_octaves", 2 } }, "0,5,2,6" },
        { "PLAY Through: Trance Gate", { { "mode", 3 }, { "arp_on", 1 }, { "arp_mode", 8 }, { "arp_rate", 4 }, { "arp_gate", 0.5f } }, "0,5,2,6", "X.XX.XX.X.XX.XXX" },
    };
    return p;
}
