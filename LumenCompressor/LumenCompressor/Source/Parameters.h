#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include <utility>

namespace IDs
{
    inline constexpr const char* style     = "style";
    inline constexpr const char* threshold = "threshold";
    inline constexpr const char* ratio     = "ratio";
    inline constexpr const char* knee      = "knee";
    inline constexpr const char* attack    = "attack";
    inline constexpr const char* release   = "release";
    inline constexpr const char* autoRel   = "autorel";
    inline constexpr const char* hold      = "hold";
    inline constexpr const char* range     = "range";
    inline constexpr const char* lookahead = "lookahead";
    inline constexpr const char* autoThr   = "autothr";
    inline constexpr const char* chanMode  = "chanmode";
    inline constexpr const char* link      = "link";
    inline constexpr const char* scSource  = "scsource";
    inline constexpr const char* scHp      = "schp";
    inline constexpr const char* scLp      = "sclp";
    inline constexpr const char* scBellF   = "scbellf";
    inline constexpr const char* scBellG   = "scbellg";
    inline constexpr const char* scListen  = "sclisten";
    inline constexpr const char* division  = "division";
    inline constexpr const char* offset    = "offset";
    inline constexpr const char* character = "character";
    inline constexpr const char* drive     = "drive";
    inline constexpr const char* charPost  = "charpost";
    inline constexpr const char* quality   = "quality";
    inline constexpr const char* makeup    = "makeup";
    inline constexpr const char* autoGain  = "autogain";
    inline constexpr const char* mix       = "mix";
    inline constexpr const char* output    = "output";
    inline constexpr const char* delta     = "delta";
    inline constexpr const char* phaseOn   = "phaseon";
    inline constexpr const char* phaseRot  = "phaserot";
    inline constexpr const char* phaseDly  = "phasedelay";
    inline constexpr const char* phaseFoc  = "phasefocus";
}

inline juce::StringArray styleNames()    { return { "Clean", "Glue", "Punch", "Opto", "Vari-Mu", "Vocal", "Master", "Pump", "Upward", "Smash" }; }
inline juce::StringArray chanNames()     { return { "Stereo", "Mid / Side", "Mid only", "Side only", "Left only", "Right only" }; }
inline juce::StringArray sourceNames()   { return { "Internal", "External SC", "Tempo Sync" }; }
inline juce::StringArray divisionNames() { return { "1/1", "1/2", "1/4", "1/8", "1/16", "1/2 T", "1/4 T", "1/8 T", "1/2 D", "1/4 D", "1/8 D" }; }
inline float divisionQuarters (int i)
{
    static const float q[] { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 4.0f / 3.0f, 2.0f / 3.0f, 1.0f / 3.0f, 3.0f, 1.5f, 0.75f };
    return q[juce::jlimit (0, 10, i)];
}
inline juce::StringArray characterNames() { return { "Off", "Tube", "Tape", "Transformer", "Diode", "Bright" }; }
inline juce::StringArray qualityNames()   { return { "1x", "2x", "4x", "8x" }; }

inline juce::String styleInfo (int s)
{
    static const char* t[] {
        "Clean: transparent peak VCA - precise, no colour",
        "Glue: SSL-style bus compressor - program-dependent release, glues mixes and drum buses",
        "Punch: FET (1176-style) feedback - fast, aggressive, brings drums and vocals forward",
        "Opto: LA-2A style - smooth two-stage release that slows down the longer it works",
        "Vari-Mu: Fairchild style - ratio grows with level, thick and gentle glue",
        "Vocal: smooth and even - keeps a vocal steady without pumping",
        "Master: transparent RMS for the master bus - gentle, wide knee",
        "Pump: EDM side-chain pumping - snaps down and swells back (use External SC or Tempo Sync)",
        "Upward: lifts the quiet parts instead of squashing the loud ones - detail and sustain",
        "Smash: 3-band upward + downward compression (OTT-style) - huge, dense, loud",
    };
    return t[juce::jlimit (0, 9, s)];
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto hz  = [] (float v, int) { return v < 1000.0f ? String (roundToInt (v)) + " Hz" : String (v / 1000.0f, 2) + " kHz"; };
    auto pct = [] (float v, int) { return String (roundToInt (v)) + " %"; };
    auto db  = [] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; };
    auto ms  = [] (float v, int) { return v < 1.0f ? String (v, 2) + " ms" : v < 10.0f ? String (v, 1) + " ms" : v < 1000.0f ? String (roundToInt (v)) + " ms" : String (v / 1000.0f, 2) + " s"; };
    auto skewed = [] (float lo, float hi, float c) { NormalisableRange<float> r (lo, hi); r.setSkewForCentre (c); return r; };

    auto fl = [] (const String& id, const String& name, NormalisableRange<float> r, float def, std::function<String (float, int)> f)
    { return std::make_unique<AudioParameterFloat> (ParameterID { id, 2 }, name, r, def, AudioParameterFloatAttributes().withStringFromValueFunction (std::move (f))); };
    auto bl = [] (const String& id, const String& name, bool def) { return std::make_unique<AudioParameterBool> (ParameterID { id, 2 }, name, def); };
    auto ch = [] (const String& id, const String& name, StringArray c, int def) { return std::make_unique<AudioParameterChoice> (ParameterID { id, 2 }, name, c, def); };

    auto g = std::make_unique<AudioProcessorParameterGroup> ("dyn", "Dynamics", "|");
    g->addChild (ch (IDs::style, "Style", styleNames(), 0));
    g->addChild (fl (IDs::threshold, "Threshold", { -60.0f, 0.0f, 0.1f }, -18.0f, db));
    g->addChild (fl (IDs::ratio, "Ratio", skewed (1.0f, 100.0f, 4.0f), 4.0f, [] (float v, int) { return v >= 99.0f ? String (CharPointer_UTF8 ("\xe2\x88\x9e:1")) : String (v, v < 10 ? 1 : 0) + ":1"; }));
    g->addChild (fl (IDs::knee, "Knee", { 0.0f, 24.0f, 0.1f }, 6.0f, db));
    g->addChild (fl (IDs::attack, "Attack", skewed (0.01f, 250.0f, 10.0f), 10.0f, ms));
    g->addChild (fl (IDs::release, "Release", skewed (5.0f, 3000.0f, 150.0f), 120.0f, ms));
    g->addChild (bl (IDs::autoRel, "Auto Release", false));
    g->addChild (fl (IDs::hold, "Hold", skewed (0.0f, 500.0f, 50.0f), 0.0f, ms));
    g->addChild (fl (IDs::range, "Range", { 0.0f, 60.0f, 0.1f }, 40.0f, db));
    g->addChild (fl (IDs::lookahead, "Lookahead", { 0.0f, 20.0f, 0.01f }, 0.0f, ms));
    g->addChild (bl (IDs::autoThr, "Auto Threshold", false));
    layout.add (std::move (g));

    auto s = std::make_unique<AudioProcessorParameterGroup> ("sc", "Side-chain", "|");
    s->addChild (ch (IDs::chanMode, "Channel Mode", chanNames(), 0));
    s->addChild (fl (IDs::link, "Stereo Link", { 0.0f, 100.0f, 0.1f }, 100.0f, pct));
    s->addChild (ch (IDs::scSource, "SC Source", sourceNames(), 0));
    s->addChild (fl (IDs::scHp, "SC High-Pass", skewed (10.0f, 2000.0f, 100.0f), 10.0f, [] (float v, int) { return v <= 12.0f ? String ("Off") : (v < 1000.0f ? String (roundToInt (v)) + " Hz" : String (v / 1000.0f, 2) + " kHz"); }));
    s->addChild (fl (IDs::scLp, "SC Low-Pass", skewed (500.0f, 20000.0f, 4000.0f), 20000.0f, [] (float v, int) { return v >= 19000.0f ? String ("Off") : (v < 1000.0f ? String (roundToInt (v)) + " Hz" : String (v / 1000.0f, 1) + " kHz"); }));
    s->addChild (fl (IDs::scBellF, "SC Bell Freq", skewed (40.0f, 12000.0f, 1000.0f), 1000.0f, hz));
    s->addChild (fl (IDs::scBellG, "SC Bell Gain", { -18.0f, 18.0f, 0.1f }, 0.0f, db));
    s->addChild (bl (IDs::scListen, "SC Listen", false));
    s->addChild (ch (IDs::division, "Tempo Division", divisionNames(), 2));
    s->addChild (fl (IDs::offset, "Tempo Offset", { -50.0f, 50.0f, 0.1f }, 0.0f, pct));
    layout.add (std::move (s));

    auto c = std::make_unique<AudioProcessorParameterGroup> ("out", "Character & Output", "|");
    c->addChild (ch (IDs::character, "Character", characterNames(), 0));
    c->addChild (fl (IDs::drive, "Drive", { 0.0f, 100.0f, 0.1f }, 0.0f, pct));
    c->addChild (bl (IDs::charPost, "Character After Compression", false));
    c->addChild (ch (IDs::quality, "Oversampling", qualityNames(), 1));
    c->addChild (fl (IDs::makeup, "Makeup", { -12.0f, 24.0f, 0.1f }, 0.0f, db));
    c->addChild (bl (IDs::autoGain, "Auto Gain", false));
    c->addChild (fl (IDs::mix, "Mix", { 0.0f, 100.0f, 0.1f }, 100.0f, pct));
    c->addChild (fl (IDs::output, "Output", { -24.0f, 12.0f, 0.1f }, 0.0f, db));
    c->addChild (bl (IDs::delta, "Delta Listen", false));
    layout.add (std::move (c));

    auto ph = std::make_unique<AudioProcessorParameterGroup> ("phase", "Phase Align", "|");
    ph->addChild (bl (IDs::phaseOn, "Phase Align", false));
    ph->addChild (fl (IDs::phaseRot, "Phase Rotation", { 0.0f, 360.0f, 0.1f }, 0.0f, [] (float v, int) { return String (roundToInt (v)) + String (CharPointer_UTF8 ("\xc2\xb0")); }));
    ph->addChild (fl (IDs::phaseDly, "Phase Delay", { 0.0f, 10.0f, 0.001f }, 0.0f, [] (float v, int) { return String (v, 2) + " ms"; }));
    ph->addChild (fl (IDs::phaseFoc, "Phase Focus", skewed (40.0f, 400.0f, 120.0f), 150.0f, hz));
    layout.add (std::move (ph));
    return layout;
}

//==============================================================================
struct CompPreset { const char* name; std::vector<std::pair<juce::String, float>> v; };

inline const std::vector<CompPreset>& factoryPresets()
{
    enum { Clean, Glue, Punch, Opto, VariMu, Vocal, Master, Pump, Upward, Smash };
    enum { Off, Tube, Tape, Trafo, Diode, Bright };
    enum { Int, Ext, Tempo };
    enum { St, MS, MidO, SideO };
    static const std::vector<CompPreset> p {
        { "Default", {} },
        // ---- drums
        { "DRUMS Bus Glue (SSL)",       { { "style", Glue }, { "threshold", -20 }, { "ratio", 4 }, { "attack", 10 }, { "release", 100 }, { "autorel", 1 }, { "schp", 80 }, { "mix", 100 } } },
        { "DRUMS Parallel NY Smash",    { { "style", Punch }, { "threshold", -30 }, { "ratio", 10 }, { "attack", 1 }, { "release", 80 }, { "character", Tube }, { "drive", 35 }, { "mix", 35 }, { "autogain", 1 } } },
        { "KICK Punch & Click",         { { "style", Punch }, { "threshold", -16 }, { "ratio", 4 }, { "attack", 12 }, { "release", 60 }, { "knee", 3 } } },
        { "KICK Tight Short",           { { "style", Clean }, { "threshold", -14 }, { "ratio", 6 }, { "attack", 2 }, { "release", 35 }, { "hold", 10 } } },
        { "SNARE Crack",                { { "style", Punch }, { "threshold", -20 }, { "ratio", 5 }, { "attack", 6 }, { "release", 90 }, { "character", Tape }, { "drive", 25 } } },
        { "SNARE Fat Body",             { { "style", Opto }, { "threshold", -22 }, { "ratio", 4 }, { "attack", 20 }, { "release", 200 } } },
        { "HATS & PERC Tame",           { { "style", Clean }, { "threshold", -24 }, { "ratio", 3 }, { "attack", 0.5f }, { "release", 40 }, { "schp", 300 } } },
        { "ROOM Crush",                 { { "style", Punch }, { "threshold", -40 }, { "ratio", 100 }, { "attack", 0.05f }, { "release", 150 }, { "character", Diode }, { "drive", 40 }, { "mix", 40 }, { "autogain", 1 } } },
        { "TRANSIENT Snap (Upward)",    { { "style", Upward }, { "threshold", -30 }, { "ratio", 2 }, { "attack", 5 }, { "release", 80 }, { "range", 12 } } },
        // ---- bass
        { "BASS Even Level",            { { "style", Opto }, { "threshold", -24 }, { "ratio", 4 }, { "attack", 15 }, { "release", 150 } } },
        { "808 Control",                { { "style", Clean }, { "threshold", -12 }, { "ratio", 6 }, { "attack", 5 }, { "release", 250 }, { "knee", 6 }, { "lookahead", 2 } } },
        { "BASS Duck Under Kick (Ext SC)", { { "style", Pump }, { "scsource", Ext }, { "threshold", -30 }, { "ratio", 8 }, { "attack", 0.5f }, { "release", 150 }, { "range", 12 }, { "sclp", 200 } } },
        { "BASS Growl Glue",            { { "style", Glue }, { "threshold", -20 }, { "ratio", 4 }, { "attack", 3 }, { "release", 80 }, { "character", Trafo }, { "drive", 30 } } },
        // ---- side-chain pumping
        { "PUMP 1/4 Tempo (no kick needed)", { { "style", Pump }, { "scsource", Tempo }, { "division", 2 }, { "threshold", -30 }, { "ratio", 20 }, { "attack", 1 }, { "release", 180 }, { "range", 18 } } },
        { "PUMP 1/8 Tempo Fast",        { { "style", Pump }, { "scsource", Tempo }, { "division", 3 }, { "threshold", -30 }, { "ratio", 20 }, { "attack", 1 }, { "release", 90 }, { "range", 12 } } },
        { "PUMP Kick Side-chain Hard",  { { "style", Pump }, { "scsource", Ext }, { "threshold", -32 }, { "ratio", 20 }, { "attack", 0.3f }, { "release", 200 }, { "range", 24 }, { "sclp", 150 } } },
        { "PUMP Subtle Breathing",      { { "style", Pump }, { "scsource", Ext }, { "threshold", -26 }, { "ratio", 4 }, { "attack", 2 }, { "release", 160 }, { "range", 6 }, { "sclp", 150 } } },
        // ---- vocals
        { "VOCAL Lead Smooth (Opto)",   { { "style", Opto }, { "threshold", -24 }, { "ratio", 3 }, { "attack", 8 }, { "release", 160 }, { "schp", 100 } } },
        { "VOCAL Lead Forward (FET)",   { { "style", Punch }, { "threshold", -22 }, { "ratio", 4 }, { "attack", 3 }, { "release", 70 }, { "schp", 100 }, { "character", Tube }, { "drive", 20 } } },
        { "VOCAL Rap Tight",            { { "style", Vocal }, { "threshold", -26 }, { "ratio", 6 }, { "attack", 1.5f }, { "release", 60 }, { "schp", 120 }, { "lookahead", 1 } } },
        { "VOCAL Backing Glue",         { { "style", VariMu }, { "threshold", -26 }, { "ratio", 3 }, { "attack", 20 }, { "release", 300 }, { "mix", 80 } } },
        { "VOCAL Upward Detail",        { { "style", Upward }, { "threshold", -35 }, { "ratio", 2.5f }, { "attack", 3 }, { "release", 120 }, { "range", 10 } } },
        { "VOCAL De-Ess Style",         { { "style", Clean }, { "threshold", -30 }, { "ratio", 6 }, { "attack", 0.5f }, { "release", 50 }, { "schp", 4500 }, { "lookahead", 1 } } },
        // ---- synths
        { "LEAD Synth Even",            { { "style", Vocal }, { "threshold", -22 }, { "ratio", 3 }, { "attack", 5 }, { "release", 100 } } },
        { "PAD Glue (Vari-Mu)",         { { "style", VariMu }, { "threshold", -24 }, { "ratio", 2.5f }, { "attack", 30 }, { "release", 400 }, { "character", Tube }, { "drive", 15 } } },
        { "PLUCK Snap",                 { { "style", Punch }, { "threshold", -24 }, { "ratio", 4 }, { "attack", 15 }, { "release", 50 } } },
        { "SYNTH Wide Side Control",    { { "style", Glue }, { "chanmode", SideO }, { "threshold", -24 }, { "ratio", 3 }, { "attack", 10 }, { "release", 120 } } },
        // ---- mix / master
        { "MIX BUS Glue 2 dB",          { { "style", Glue }, { "threshold", -16 }, { "ratio", 2 }, { "attack", 30 }, { "release", 200 }, { "autorel", 1 }, { "schp", 90 } } },
        { "MASTER Transparent",         { { "style", Master }, { "threshold", -14 }, { "ratio", 1.5f }, { "attack", 30 }, { "release", 300 }, { "knee", 12 }, { "schp", 60 }, { "quality", 2 } } },
        { "MASTER Vari-Mu Warmth",      { { "style", VariMu }, { "threshold", -16 }, { "ratio", 2 }, { "attack", 40 }, { "release", 400 }, { "character", Tube }, { "drive", 12 }, { "quality", 2 } } },
        { "MASTER M/S Tight Low End",   { { "style", Master }, { "chanmode", MS }, { "link", 30 }, { "threshold", -18 }, { "ratio", 2 }, { "attack", 20 }, { "release", 250 }, { "quality", 2 } } },
        { "MASTER Tape Glue",           { { "style", Glue }, { "threshold", -14 }, { "ratio", 2 }, { "attack", 30 }, { "release", 150 }, { "autorel", 1 }, { "character", Tape }, { "drive", 20 }, { "charpost", 1 }, { "quality", 2 } } },
        // ---- FX
        { "SMASH OTT Everything",       { { "style", Smash }, { "threshold", -20 }, { "ratio", 6 }, { "attack", 5 }, { "release", 80 }, { "range", 24 }, { "autogain", 1 } } },
        { "SMASH Half (Parallel)",      { { "style", Smash }, { "threshold", -20 }, { "ratio", 6 }, { "attack", 5 }, { "release", 80 }, { "range", 24 }, { "mix", 50 }, { "autogain", 1 } } },
        { "LIMIT Peak Catcher",         { { "style", Clean }, { "threshold", -6 }, { "ratio", 100 }, { "attack", 0.01f }, { "release", 80 }, { "lookahead", 2 }, { "knee", 0 } } },
    };
    return p;
}
