#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include <utility>

namespace IDs
{
    // global
    inline constexpr const char* quality = "quality";
    inline constexpr const char* input   = "input";
    inline constexpr const char* output  = "output";
    inline constexpr const char* mix     = "mix";
    inline constexpr const char* x1      = "xover1";
    inline constexpr const char* x2      = "xover2";
    inline constexpr const char* clip    = "clip";
    inline constexpr const char* compare = "compare";
    inline constexpr const char* delta   = "delta";
    inline constexpr const char* duck    = "duck";
    inline constexpr const char* duckRel = "duck_rel";
    inline constexpr const char* duckMid = "duck_mid";
    // sub
    inline constexpr const char* subOn    = "sub_on";
    inline constexpr const char* subSolo  = "sub_solo";
    inline constexpr const char* subMono  = "sub_mono";
    inline constexpr const char* subTight = "sub_tight";
    inline constexpr const char* subGen   = "sub_gen";
    inline constexpr const char* subGenMode = "sub_genmode";
    inline constexpr const char* subPhone = "sub_phone";
    inline constexpr const char* subChar  = "sub_char";
    inline constexpr const char* subLevel = "sub_level";
}

// BODY ("mid") and AIR ("top") share the same parameter set
inline juce::String bandId (const char* band, const char* p) { return juce::String (band) + "_" + p; }

inline juce::StringArray driveTypeNames() { return { "Tube", "Tape", "Fuzz", "Fold", "Rectify", "Crush" }; }
inline juce::StringArray qualityNames()   { return { "Eco (1x)", "Good (2x)", "High (4x)", "Ultra (8x)" }; }

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto hz  = [] (float v, int) { return v < 1000.0f ? String (roundToInt (v)) + " Hz" : String (v / 1000.0f, 2) + " kHz"; };
    auto pct = [] (float v, int) { return String (roundToInt (v)) + " %"; };
    auto db  = [] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; };
    auto ms  = [] (float v, int) { return String (roundToInt (v)) + " ms"; };
    auto tone = [] (float v, int) { return std::abs (v) < 0.5f ? String ("Neutral") : v < 0 ? "Dark " + String (roundToInt (-v)) : "Bright " + String (roundToInt (v)); };
    auto chr = [] (float v, int) { return v < 34.0f ? "Warm " + String (roundToInt (v)) : v > 66.0f ? "Edgy " + String (roundToInt (v)) : "Mixed " + String (roundToInt (v)); };
    auto skewed = [] (float lo, float hi, float c) { NormalisableRange<float> r (lo, hi); r.setSkewForCentre (c); return r; };

    auto group = [&] (const char* gid, const char* gname) { return std::make_unique<AudioProcessorParameterGroup> (gid, gname, "|"); };
    auto fl = [] (const String& id, const String& name, NormalisableRange<float> r, float def, std::function<String (float, int)> f)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 2 }, name, r, def, AudioParameterFloatAttributes().withStringFromValueFunction (std::move (f)));
    };
    auto bl = [] (const String& id, const String& name, bool def) { return std::make_unique<AudioParameterBool> (ParameterID { id, 2 }, name, def); };
    auto ch = [] (const String& id, const String& name, StringArray c, int def) { return std::make_unique<AudioParameterChoice> (ParameterID { id, 2 }, name, c, def); };

    auto g = group ("global", "Global");
    g->addChild (ch (IDs::quality, "Quality", qualityNames(), 2));
    g->addChild (fl (IDs::input,  "Input",  { -24.0f, 24.0f, 0.1f }, 0.0f, db));
    g->addChild (fl (IDs::output, "Output", { -24.0f, 12.0f, 0.1f }, 0.0f, db));
    g->addChild (fl (IDs::mix,    "Mix",    { 0.0f, 100.0f, 0.1f }, 100.0f, pct));
    g->addChild (fl (IDs::x1,     "Sub / Body Split", skewed (40.0f, 250.0f, 110.0f), 110.0f, hz));
    g->addChild (fl (IDs::x2,     "Body / Air Split", skewed (500.0f, 8000.0f, 2200.0f), 2500.0f, hz));
    g->addChild (bl (IDs::clip,   "Soft Clip", true));
    g->addChild (bl (IDs::compare, "Compare (Equal Loudness)", false));
    g->addChild (bl (IDs::delta,  "Delta Listen", false));
    g->addChild (fl (IDs::duck,   "Kick Duck", { 0.0f, 24.0f, 0.1f }, 0.0f, [] (float v, int) { return v < 0.05f ? juce::String ("Off") : "-" + juce::String (v, 1) + " dB"; }));
    g->addChild (fl (IDs::duckRel, "Duck Release", skewed (20.0f, 500.0f, 120.0f), 140.0f, ms));
    g->addChild (bl (IDs::duckMid, "Duck Body Too", false));
    layout.add (std::move (g));

    auto s = group ("sub", "Sub");
    s->addChild (bl (IDs::subOn,   "Sub On", true));
    s->addChild (bl (IDs::subSolo, "Sub Solo", false));
    s->addChild (bl (IDs::subMono, "Sub Mono", true));
    s->addChild (fl (IDs::subTight, "Sub Tight", { 0.0f, 100.0f, 0.1f }, 35.0f, pct));
    s->addChild (fl (IDs::subGen,   "Sub Generator", { 0.0f, 100.0f, 0.1f }, 0.0f, pct));
    s->addChild (ch (IDs::subGenMode, "Sub Gen Mode", { "Unison", "Octave Down" }, 0));
    s->addChild (fl (IDs::subPhone, "Phone Harmonics", { 0.0f, 100.0f, 0.1f }, 40.0f, pct));
    s->addChild (fl (IDs::subChar,  "Harmonic Character", { 0.0f, 100.0f, 0.1f }, 40.0f, chr));
    s->addChild (fl (IDs::subLevel, "Sub Level", { -12.0f, 12.0f, 0.1f }, 0.0f, db));
    layout.add (std::move (s));

    struct BandDef { const char* id; const char* name; int type; float drive, squash, width; };
    for (auto bd : { BandDef { "mid", "Body", 0, 35.0f, 25.0f, 100.0f }, BandDef { "top", "Air", 1, 20.0f, 10.0f, 120.0f } })
    {
        const String n (bd.name);
        auto b = group (bd.id, bd.name);
        b->addChild (bl (bandId (bd.id, "on"),   n + " On", true));
        b->addChild (bl (bandId (bd.id, "solo"), n + " Solo", false));
        b->addChild (ch (bandId (bd.id, "type"), n + " Drive Type", driveTypeNames(), bd.type));
        b->addChild (fl (bandId (bd.id, "drive"),  n + " Drive",  { 0.0f, 100.0f, 0.1f }, bd.drive, pct));
        b->addChild (fl (bandId (bd.id, "tone"),   n + " Tone",   { -100.0f, 100.0f, 0.1f }, 0.0f, tone));
        b->addChild (fl (bandId (bd.id, "mix"),    n + " Drive Mix", { 0.0f, 100.0f, 0.1f }, 100.0f, pct));
        b->addChild (fl (bandId (bd.id, "squash"), n + " Squash", { 0.0f, 100.0f, 0.1f }, bd.squash, pct));
        b->addChild (fl (bandId (bd.id, "width"),  n + " Width",  { 0.0f, 200.0f, 0.1f }, bd.width, pct));
        b->addChild (fl (bandId (bd.id, "level"),  n + " Level",  { -12.0f, 12.0f, 0.1f }, 0.0f, db));
        layout.add (std::move (b));
    }
    return layout;
}

//==============================================================================
// Factory presets: values (in parameter units) applied on top of the defaults.
struct BassPreset { const char* name; std::vector<std::pair<juce::String, float>> v; };

inline const std::vector<BassPreset>& factoryPresets()
{
    enum { Tube, Tape, Fuzz, Fold, Rect, Crush };
    static const std::vector<BassPreset> p {
        { "Default - Ready to Go", {} },
        { "Neutral (Transparent)", { { "sub_tight", 0 }, { "sub_phone", 0 }, { "sub_mono", 0 }, { "mid_drive", 0 }, { "mid_squash", 0 },
                                     { "top_drive", 0 }, { "top_squash", 0 }, { "top_width", 100 }, { "clip", 0 } } },
        // ---- sub
        { "SUB Clean Mono Sub",        { { "xover1", 100 }, { "sub_tight", 45 }, { "sub_phone", 0 }, { "mid_drive", 0 }, { "mid_squash", 0 }, { "top_drive", 0 }, { "top_squash", 0 }, { "top_width", 100 } } },
        { "SUB Tight & Even (Glue)",   { { "sub_tight", 75 }, { "sub_phone", 15 }, { "mid_drive", 10 }, { "mid_squash", 30 } } },
        { "SUB Phone & Laptop Ready",  { { "sub_phone", 70 }, { "sub_char", 30 }, { "sub_tight", 50 }, { "mid_drive", 20 } } },
        { "SUB Restore (Weak Sub Fix)", { { "sub_gen", 60 }, { "sub_genmode", 0 }, { "sub_tight", 50 }, { "sub_level", 2 } } },
        { "SUB Octave Down Rumble",    { { "sub_gen", 55 }, { "sub_genmode", 1 }, { "sub_tight", 40 }, { "xover1", 90 } } },
        // ---- 808
        { "808 Trap Hard Hitter",      { { "xover1", 90 }, { "sub_tight", 55 }, { "sub_phone", 50 }, { "mid_type", Tube }, { "mid_drive", 55 }, { "mid_squash", 25 },
                                         { "top_type", Tape }, { "top_drive", 30 }, { "top_level", -3 } } },
        { "808 Distorted (Rap / Drill)", { { "sub_phone", 40 }, { "mid_type", Fuzz }, { "mid_drive", 65 }, { "mid_tone", 15 }, { "mid_squash", 30 },
                                         { "top_type", Fuzz }, { "top_drive", 50 }, { "top_level", -4 } } },
        { "808 Phonk Crush",           { { "sub_tight", 50 }, { "mid_type", Crush }, { "mid_drive", 55 }, { "mid_mix", 70 }, { "top_type", Crush }, { "top_drive", 60 }, { "top_level", -2 } } },
        // ---- genres
        { "REESE DnB Wide",            { { "xover1", 120 }, { "xover2", 3000 }, { "sub_tight", 50 }, { "mid_type", Tape }, { "mid_drive", 40 }, { "mid_squash", 45 }, { "mid_width", 140 },
                                         { "top_type", Tape }, { "top_drive", 35 }, { "top_squash", 30 }, { "top_width", 170 } } },
        { "NEURO Growl",               { { "xover1", 100 }, { "xover2", 2200 }, { "sub_tight", 60 }, { "mid_type", Fold }, { "mid_drive", 60 }, { "mid_tone", 20 }, { "mid_squash", 55 }, { "mid_width", 120 },
                                         { "top_type", Fuzz }, { "top_drive", 55 }, { "top_squash", 40 }, { "top_width", 150 } } },
        { "DUBSTEP Wobble Grit",       { { "sub_tight", 55 }, { "mid_type", Rect }, { "mid_drive", 50 }, { "mid_squash", 50 }, { "mid_width", 120 },
                                         { "top_type", Fuzz }, { "top_drive", 45 }, { "top_width", 160 } } },
        { "RIDDIM Crushed",            { { "xover1", 90 }, { "sub_tight", 60 }, { "mid_type", Crush }, { "mid_drive", 45 }, { "mid_squash", 60 }, { "top_type", Fuzz }, { "top_drive", 50 }, { "top_width", 130 } } },
        { "FUTURE BASS Fat & Wide",    { { "sub_phone", 30 }, { "mid_type", Tube }, { "mid_drive", 35 }, { "mid_tone", -10 }, { "mid_squash", 30 }, { "mid_width", 150 },
                                         { "top_type", Tape }, { "top_drive", 25 }, { "top_width", 180 } } },
        { "TECH HOUSE Rolling Bass",   { { "xover1", 100 }, { "sub_tight", 50 }, { "sub_phone", 35 }, { "mid_drive", 30 }, { "mid_squash", 25 }, { "top_drive", 15 }, { "top_width", 110 }, { "duck", 6 } } },
        { "BASS HOUSE Wobble",         { { "sub_tight", 55 }, { "mid_type", Rect }, { "mid_drive", 45 }, { "mid_squash", 40 }, { "top_type", Fuzz }, { "top_drive", 40 }, { "top_width", 140 }, { "duck", 6 } } },
        { "BIG ROOM Punch",            { { "sub_tight", 60 }, { "sub_phone", 40 }, { "mid_drive", 40 }, { "mid_squash", 35 }, { "top_type", Tape }, { "top_drive", 25 }, { "top_width", 130 }, { "duck", 8 } } },
        { "MELODIC TECHNO Warm",       { { "sub_tight", 40 }, { "sub_phone", 20 }, { "mid_type", Tape }, { "mid_drive", 30 }, { "mid_tone", -20 }, { "mid_squash", 15 }, { "mid_width", 110 },
                                         { "top_type", Tape }, { "top_drive", 15 }, { "top_tone", -25 }, { "top_width", 120 } } },
        { "DEEP HOUSE Round",          { { "sub_tight", 40 }, { "sub_phone", 30 }, { "mid_type", Tape }, { "mid_drive", 20 }, { "mid_tone", -30 }, { "top_drive", 5 }, { "top_tone", -40 }, { "top_level", -4 } } },
        { "PSYTRANCE Tight Roll",      { { "xover1", 120 }, { "sub_tight", 70 }, { "mid_drive", 25 }, { "mid_squash", 40 }, { "top_drive", 20 }, { "top_width", 100 }, { "duck", 10 }, { "duck_rel", 90 } } },
        { "UK GARAGE Sub",             { { "sub_gen", 30 }, { "sub_tight", 45 }, { "sub_phone", 35 }, { "mid_drive", 20 }, { "top_level", -3 } } },
        { "HARDSTYLE Reverse Bass",    { { "sub_tight", 65 }, { "sub_phone", 40 }, { "mid_type", Fuzz }, { "mid_drive", 70 }, { "mid_squash", 40 }, { "top_type", Fuzz }, { "top_drive", 60 } } },
        { "SYNTHWAVE Analog",          { { "sub_tight", 35 }, { "mid_type", Tape }, { "mid_drive", 35 }, { "mid_tone", -15 }, { "top_type", Tape }, { "top_drive", 25 }, { "top_tone", -20 } } },
        { "LO-FI Bass",                { { "sub_tight", 30 }, { "mid_type", Crush }, { "mid_drive", 40 }, { "mid_tone", -40 }, { "mid_mix", 60 }, { "top_tone", -50 }, { "top_level", -8 } } },
        // ---- mix tools
        { "MIX Tuck Under Kick (Side-chain)", { { "duck", 9 }, { "duck_rel", 140 }, { "sub_tight", 45 }, { "sub_phone", 25 }, { "mid_drive", 15 } } },
        { "MIX Growl Only - Clean Sub", { { "sub_tight", 50 }, { "sub_phone", 0 }, { "mid_type", Fold }, { "mid_drive", 60 }, { "mid_squash", 50 }, { "mid_width", 130 },
                                         { "top_type", Fuzz }, { "top_drive", 50 }, { "top_width", 150 } } },
        { "MIX Parallel Smash",        { { "sub_tight", 70 }, { "mid_drive", 60 }, { "mid_mix", 45 }, { "mid_squash", 70 }, { "top_type", Tape }, { "top_drive", 40 }, { "top_mix", 45 }, { "top_squash", 60 } } },
        { "MIX Bass Guitar DI to Amp", { { "xover1", 90 }, { "xover2", 1800 }, { "mid_drive", 40 }, { "mid_tone", 10 }, { "mid_squash", 20 }, { "mid_width", 100 },
                                         { "top_type", Tape }, { "top_drive", 25 }, { "top_tone", -30 }, { "top_width", 100 }, { "top_level", -3 } } },
        { "MIX Wide Top, Mono Bottom", { { "mid_width", 130 }, { "top_drive", 10 }, { "top_width", 200 } } },
    };
    return p;
}

inline juce::String hzText (float v) { return v < 1000.0f ? juce::String (juce::roundToInt (v)) + " Hz" : juce::String (v / 1000.0f, 2) + " kHz"; }
