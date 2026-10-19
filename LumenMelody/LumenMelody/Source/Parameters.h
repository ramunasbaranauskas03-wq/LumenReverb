#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Player.h"

namespace IDs
{
    inline const juce::String key = "key", scale = "scale", source = "chord_source", harmony = "harmony", prog = "progression", chordLen = "chord_length", bars = "pattern_length",
        leadOn = "lead_on", leadStyle = "lead_style", density = "lead_density", catchy = "lead_catchy", range = "lead_range", contour = "lead_contour", hook = "hook_length",
        leadOct = "lead_octave", leadGate = "lead_gate",
        bassOn = "bass_on", bassStyle = "bass_style", variation = "bass_variation", approach = "bass_approach", bassOct = "bass_octave", bassGate = "bass_gate",
        swing = "swing", humanize = "humanize", velocity = "velocity",
        sound = "preview_sound", leadSound = "lead_sound", bassSound = "bass_sound", leadLvl = "lead_level", bassLvl = "bass_level";
}

struct Prog { const char* name; std::vector<int> deg; };
inline const std::vector<Prog>& progressions()
{
    static const std::vector<Prog> p {
        { "1-6-3-7  epic minor", { 0, 5, 2, 6 } },
        { "1-6-7-1  anthem", { 0, 5, 6, 0 } },
        { "6-7-1-1  rise", { 5, 6, 0, 0 } },
        { "1-4-6-5  drama", { 0, 3, 5, 4 } },
        { "1-7-6-7  trance", { 0, 6, 5, 6 } },
        { "1-3-7-6  melodic", { 0, 2, 6, 5 } },
        { "1-5-6-4  pop (Major)", { 0, 4, 5, 3 } },
        { "6-4-1-5  euphoric (Major)", { 5, 3, 0, 4 } },
        { "4-5-3-6  future bass (Major)", { 3, 4, 2, 5 } },
        { "2-5-1-6  deep house (Major)", { 1, 4, 0, 5 } },
        { "1-1-6-6  two chords", { 0, 0, 5, 5 } },
        { "1-6  minimal", { 0, 5 } },
        { "1  one chord (techno)", { 0 } },
        { "1-6-3-7-1-6-4-5  8 chords", { 0, 5, 2, 6, 0, 5, 3, 4 } },
    };
    return p;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    using namespace lumen::melody;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const String& id) { return ParameterID { id, 1 }; };
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });
    auto c = [&] (const String& id, const String& nm, StringArray ch, int d) { l.add (std::make_unique<AudioParameterChoice> (pid (id), nm, ch, d)); };
    auto b = [&] (const String& id, const String& nm, bool d) { l.add (std::make_unique<AudioParameterBool> (pid (id), nm, d)); };
    auto f = [&] (const String& id, const String& nm, float lo, float hi, float d) { l.add (std::make_unique<AudioParameterFloat> (pid (id), nm, NormalisableRange<float> (lo, hi), d, pct)); };
    auto in = [&] (const String& id, const String& nm, int lo, int hi, int d) { l.add (std::make_unique<AudioParameterInt> (pid (id), nm, lo, hi, d)); };
    StringArray sc; for (auto& s : scales()) sc.add (s.name);
    StringArray pr; for (auto& p : progressions()) pr.add (p.name);
    c (IDs::key, "Key", keyNames(), 9);
    c (IDs::scale, "Scale", sc, 0);
    c (IDs::source, "Chords From", StringArray { "Progression (built-in)", "MIDI input (Lumen Chord / keys)" }, 0);
    c (IDs::harmony, "Harmony", StringArray { "Follow chords (sequence)", "Keep line (snap to chords)" }, 0);
    c (IDs::prog, "Progression", pr, 0);
    c (IDs::chordLen, "Chord Length", StringArray { "1/2 bar", "1 bar", "2 bars", "4 bars" }, 1);
    c (IDs::bars, "Pattern Length", StringArray { "1 bar", "2 bars", "4 bars", "8 bars" }, 2);
    b (IDs::leadOn, "Lead On", true);
    c (IDs::leadStyle, "Lead Style", leadStyleNames(), BigRoomHook);
    f (IDs::density, "Lead Density", 0.0f, 1.0f, 0.5f);
    f (IDs::catchy, "Catchiness", 0.0f, 1.0f, 0.7f);
    in (IDs::range, "Lead Range", 4, 14, 7);
    c (IDs::contour, "Lead Contour", contourNames(), Arch);
    c (IDs::hook, "Hook Length", StringArray { "1 bar", "2 bars", "4 bars" }, 0);
    in (IDs::leadOct, "Lead Octave", 2, 6, 4);
    f (IDs::leadGate, "Lead Gate", 0.1f, 1.0f, 0.85f);
    b (IDs::bassOn, "Bass On", true);
    c (IDs::bassStyle, "Bass Style", bassStyleNames(), Offbeat);
    f (IDs::variation, "Bass Variation", 0.0f, 1.0f, 0.25f);
    b (IDs::approach, "Approach Notes", true);
    in (IDs::bassOct, "Bass Octave", 0, 3, 1);
    f (IDs::bassGate, "Bass Gate", 0.1f, 1.0f, 0.8f);
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::swing), "Swing", NormalisableRange<float> (0.0f, 0.6f), 0.0f, pct));
    f (IDs::humanize, "Humanize", 0.0f, 1.0f, 0.1f);
    f (IDs::velocity, "Velocity", 0.1f, 1.0f, 0.9f);
    b (IDs::sound, "Preview Sound", true);
    c (IDs::leadSound, "Lead Sound", StringArray { "Supersaw", "Pluck", "Keys" }, 0);
    c (IDs::bassSound, "Bass Sound", StringArray { "Sub", "Saw Bass", "Electro Squelch" }, 1);
    auto db = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; });
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::leadLvl), "Lead Level", NormalisableRange<float> (-36.0f, 0.0f, 0.1f), -10.0f, db));
    l.add (std::make_unique<AudioParameterFloat> (pid (IDs::bassLvl), "Bass Level", NormalisableRange<float> (-36.0f, 0.0f, 0.1f), -8.0f, db));
    return l;
}

// ------------------------------------------------------------------------------------------------
// key 0=C .. 9=A; scale 0 minor 1 major 4 dorian 5 phrygian; prog index into progressions(); lead/bass style indices; seed = which variation
struct MPreset { const char* name; std::vector<std::pair<const char*, float>> v; uint32_t leadSeed = 1, bassSeed = 1; };
inline const std::vector<MPreset>& factoryPresets()
{
    static const std::vector<MPreset> p {
        { "Init: Big Room hook + offbeat bass", {} },
        // ELECTRO / BIG ROOM
        { "ELECTRO HOUSE Catchy Drop (F#m)", { { "key", 6 }, { "lead_style", 1 }, { "bass_style", 3 }, { "progression", 0 }, { "lead_catchy", 0.85f }, { "bass_sound", 2 } }, 7, 3 },
        { "ELECTRO Dirty Dutch Squelch", { { "key", 5 }, { "lead_style", 1 }, { "bass_style", 3 }, { "progression", 10 }, { "lead_density", 0.7f }, { "bass_sound", 2 }, { "bass_variation", 0.5f } }, 21, 9 },
        { "BIG ROOM Festival Hook (Gm)", { { "key", 7 }, { "lead_style", 0 }, { "bass_style", 5 }, { "progression", 1 }, { "lead_catchy", 0.9f } }, 3, 2 },
        { "BIG ROOM Stab Drop", { { "key", 2 }, { "lead_style", 0 }, { "bass_style", 5 }, { "progression", 10 }, { "lead_density", 0.3f }, { "lead_range", 5 } }, 11, 4 },
        { "MELBOURNE Bounce", { { "key", 9 }, { "lead_style", 1 }, { "bass_style", 2 }, { "progression", 11 }, { "lead_sound", 1 }, { "bass_sound", 2 } }, 5, 6 },
        // PROGRESSIVE / HOUSE
        { "PROG HOUSE Euphoric Pluck (D Maj)", { { "key", 2 }, { "scale", 1 }, { "lead_style", 2 }, { "bass_style", 0 }, { "progression", 7 }, { "lead_sound", 1 } }, 4, 1 },
        { "PROG HOUSE Anthem (A minor)", { { "lead_style", 2 }, { "bass_style", 0 }, { "progression", 0 }, { "hook_length", 1 }, { "lead_sound", 1 } }, 9, 1 },
        { "HOUSE Walking Bass + Keys", { { "key", 3 }, { "scale", 1 }, { "lead_style", 7 }, { "bass_style", 7 }, { "progression", 9 }, { "lead_sound", 2 }, { "swing", 0.25f } }, 6, 5 },
        { "DEEP HOUSE Minor Groove", { { "key", 5 }, { "scale", 4 }, { "lead_style", 6 }, { "bass_style", 3 }, { "progression", 11 }, { "lead_sound", 2 }, { "swing", 0.3f }, { "bass_sound", 0 } }, 12, 8 },
        { "DISCO HOUSE Octave Bass", { { "key", 0 }, { "scale", 4 }, { "lead_style", 7 }, { "bass_style", 2 }, { "progression", 5 }, { "swing", 0.2f } }, 2, 2 },
        // TRANCE / PSY
        { "TRANCE Uplifting Lead", { { "key", 1 }, { "lead_style", 3 }, { "bass_style", 0 }, { "progression", 4 }, { "hook_length", 1 }, { "lead_range", 10 } }, 14, 1 },
        { "TRANCE Rolling Bass", { { "key", 9 }, { "lead_style", 3 }, { "bass_style", 1 }, { "progression", 0 }, { "bass_sound", 1 } }, 8, 1 },
        { "PSY Phrygian Roller", { { "key", 4 }, { "scale", 5 }, { "lead_style", 6 }, { "bass_style", 1 }, { "progression", 12 }, { "bass_octave", 1 } }, 17, 2 },
        // FUTURE / MELODIC / POP
        { "FUTURE BASS Chop Melody (Major)", { { "key", 2 }, { "scale", 1 }, { "lead_style", 4 }, { "bass_style", 6 }, { "progression", 8 }, { "bass_sound", 0 } }, 10, 3 },
        { "MELODIC DUBSTEP Emotional", { { "key", 11 }, { "lead_style", 7 }, { "bass_style", 4 }, { "progression", 2 }, { "chord_length", 2 }, { "hook_length", 1 }, { "bass_sound", 0 } }, 13, 1 },
        { "TROPICAL HOUSE Pluck", { { "key", 9 }, { "scale", 1 }, { "lead_style", 5 }, { "bass_style", 6 }, { "progression", 6 }, { "lead_sound", 1 }, { "bass_sound", 0 } }, 4, 2 },
        { "POP EDM Topline", { { "key", 0 }, { "scale", 1 }, { "lead_style", 7 }, { "bass_style", 0 }, { "progression", 6 }, { "hook_length", 1 }, { "lead_sound", 2 } }, 15, 1 },
        { "MELODIC TECHNO Hypnotic", { { "key", 9 }, { "lead_style", 6 }, { "bass_style", 1 }, { "progression", 12 }, { "lead_catchy", 0.95f }, { "lead_range", 5 }, { "lead_sound", 1 } }, 20, 3 },
        // DARK / HARD
        { "TECHNO Dark Rolling", { { "key", 9 }, { "scale", 5 }, { "lead_style", 6 }, { "bass_style", 1 }, { "progression", 12 }, { "lead_on", 0 } }, 1, 7 },
        { "DnB Reese + Lead", { { "key", 4 }, { "lead_style", 7 }, { "bass_style", 8 }, { "progression", 3 }, { "chord_length", 2 } }, 16, 1 },
        { "HARDSTYLE Screech Hook", { { "key", 4 }, { "lead_style", 0 }, { "bass_style", 4 }, { "progression", 0 }, { "lead_range", 9 }, { "lead_catchy", 0.8f } }, 19, 1 },
        { "DUBSTEP Half-Time Sub", { { "key", 5 }, { "lead_style", 4 }, { "bass_style", 4 }, { "progression", 11 }, { "bass_octave", 0 }, { "bass_sound", 0 } }, 22, 1 },
        // WORKFLOW
        { "FOLLOW Lumen Chord (MIDI input)", { { "chord_source", 1 } } },
        { "FOLLOW Input - Bass Only", { { "chord_source", 1 }, { "lead_on", 0 }, { "bass_style", 0 } } },
        { "FOLLOW Input - Lead Only", { { "chord_source", 1 }, { "bass_on", 0 }, { "lead_style", 2 } } },
        { "KEEP LINE Topline over chords", { { "harmony", 1 }, { "lead_style", 7 }, { "lead_contour", 1 } }, 3, 1 },
        { "8-BAR Hook (2-bar motif)", { { "pattern_length", 3 }, { "hook_length", 1 }, { "progression", 13 } }, 24, 2 },
    };
    return p;
}
