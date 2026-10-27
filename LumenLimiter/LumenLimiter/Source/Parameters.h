#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include <utility>

namespace IDs
{
    // comp
    inline constexpr const char* compOn  = "comp_on";
    inline constexpr const char* ratio   = "comp_ratio";
    inline constexpr const char* thr     = "comp_thr";
    inline constexpr const char* attack  = "comp_attack";
    inline constexpr const char* release = "comp_release";
    inline constexpr const char* autoRel = "comp_autorel";
    inline constexpr const char* scHp    = "comp_schp";
    inline constexpr const char* midSide = "comp_ms";
    inline constexpr const char* compMix = "comp_mix";
    // transients
    inline constexpr const char* loPunch = "lopunch";
    inline constexpr const char* loFreq  = "lofreq";
    inline constexpr const char* detail  = "detail";
    inline constexpr const char* detFreq = "detfreq";
    inline constexpr const char* transient = "transient";
    // colour / clip
    inline constexpr const char* color   = "color";
    inline constexpr const char* drive   = "drive";
    inline constexpr const char* clip    = "clip";
    inline constexpr const char* clipShape = "clipshape";
    inline constexpr const char* quality = "quality";
    // level
    inline constexpr const char* gain    = "gain";
    inline constexpr const char* ceiling = "ceiling";
    inline constexpr const char* mode    = "mode";
    inline constexpr const char* limRel  = "lim_release";
    inline constexpr const char* look    = "lookahead";
    inline constexpr const char* balance = "balance";
    inline constexpr const char* link    = "link";
    inline constexpr const char* truePeak = "truepeak";
    inline constexpr const char* dither  = "dither";
    // monitoring / targets
    inline constexpr const char* monitor = "monitor";
    inline constexpr const char* cgm     = "cgm";
    inline constexpr const char* target  = "target";
    inline constexpr const char* custom  = "custom_lufs";
}

inline juce::StringArray modeNames()    { return { "Clear", "Tight", "Punchy", "Loud" }; }
inline juce::StringArray colorNames()   { return { "Clean", "Warm", "Tape", "Bright" }; }
inline juce::StringArray monitorNames() { return { "Stereo", "Mid", "Side", "Left", "Right", "Diff" }; }
inline juce::StringArray ditherNames()  { return { "Dither Off", "24-bit", "16-bit", "16-bit NS" }; }
inline juce::StringArray qualityNames() { return { "1x", "2x", "4x", "8x" }; }

struct Target { const char* name; float lufs; float tp; };
inline const std::vector<Target>& targets()
{
    static const std::vector<Target> t {
        { "Spotify",        -14.0f, -1.0f },
        { "Apple Music",    -16.0f, -1.0f },
        { "YouTube",        -14.0f, -1.0f },
        { "Tidal / Amazon", -14.0f, -1.0f },
        { "SoundCloud",     -10.0f, -1.0f },
        { "Beatport / DJ",   -8.0f, -0.3f },
        { "EDM Club Loud",   -6.0f, -0.3f },
        { "Techno / House",  -8.0f, -0.5f },
        { "Hip-Hop / Trap",  -8.0f, -0.5f },
        { "Pop",             -9.0f, -1.0f },
        { "Broadcast EBU",  -23.0f, -1.0f },
        { "Custom",           0.0f, -1.0f },
    };
    return t;
}
inline juce::StringArray targetNames() { juce::StringArray s; for (auto& t : targets()) s.add (t.name); return s; }

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto hz  = [] (float v, int) { return v < 1000.0f ? String (roundToInt (v)) + " Hz" : String (v / 1000.0f, 1) + " kHz"; };
    auto pct = [] (float v, int) { return String (roundToInt (v)) + " %"; };
    auto db  = [] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; };
    auto ms  = [] (float v, int) { return v < 10.0f ? String (v, 1) + " ms" : String (roundToInt (v)) + " ms"; };
    auto bip = [] (float v, int) { return std::abs (v) < 0.5f ? String ("0") : (v > 0 ? "+" : "") + String (roundToInt (v)); };
    auto skewed = [] (float lo, float hi, float c) { NormalisableRange<float> r (lo, hi); r.setSkewForCentre (c); return r; };
    auto fl = [] (const String& id, const String& name, NormalisableRange<float> r, float def, std::function<String (float, int)> f)
    { return std::make_unique<AudioParameterFloat> (ParameterID { id, 2 }, name, r, def, AudioParameterFloatAttributes().withStringFromValueFunction (std::move (f))); };
    auto bl = [] (const String& id, const String& name, bool def) { return std::make_unique<AudioParameterBool> (ParameterID { id, 2 }, name, def); };
    auto ch = [] (const String& id, const String& name, StringArray c, int def) { return std::make_unique<AudioParameterChoice> (ParameterID { id, 2 }, name, c, def); };

    auto c = std::make_unique<AudioProcessorParameterGroup> ("comp", "Comp", "|");
    c->addChild (bl (IDs::compOn, "Comp On", false));
    c->addChild (fl (IDs::ratio, "Comp Ratio", skewed (1.0f, 10.0f, 2.5f), 2.0f, [] (float v, int) { return String (v, 2) + ":1"; }));
    c->addChild (fl (IDs::thr, "Comp Threshold", { -40.0f, 0.0f, 0.1f }, -18.0f, db));
    c->addChild (fl (IDs::attack, "Comp Attack", skewed (0.1f, 100.0f, 10.0f), 10.0f, ms));
    c->addChild (fl (IDs::release, "Comp Release", skewed (10.0f, 1500.0f, 150.0f), 150.0f, ms));
    c->addChild (bl (IDs::autoRel, "Comp Auto Release", true));
    c->addChild (fl (IDs::scHp, "Comp SC High-Pass", skewed (20.0f, 300.0f, 80.0f), 60.0f, hz));
    c->addChild (bl (IDs::midSide, "Comp Mid/Side", false));
    c->addChild (fl (IDs::compMix, "Comp Mix", { 0.0f, 100.0f, 0.1f }, 100.0f, pct));
    layout.add (std::move (c));

    auto t = std::make_unique<AudioProcessorParameterGroup> ("trans", "Transients", "|");
    t->addChild (fl (IDs::loPunch, "Lo Punch", { 0.0f, 100.0f, 0.1f }, 0.0f, pct));
    t->addChild (fl (IDs::loFreq, "Lo Punch Freq", skewed (40.0f, 250.0f, 100.0f), 100.0f, hz));
    t->addChild (fl (IDs::detail, "Detail", { 0.0f, 100.0f, 0.1f }, 0.0f, pct));
    t->addChild (fl (IDs::detFreq, "Detail Freq", skewed (1500.0f, 12000.0f, 4500.0f), 5000.0f, hz));
    t->addChild (fl (IDs::transient, "Transient", { -100.0f, 100.0f, 0.1f }, 0.0f, bip));
    layout.add (std::move (t));

    auto o = std::make_unique<AudioProcessorParameterGroup> ("color", "Colour & Clip", "|");
    o->addChild (ch (IDs::color, "Colour", colorNames(), 0));
    o->addChild (fl (IDs::drive, "Drive", { 0.0f, 100.0f, 0.1f }, 0.0f, pct));
    o->addChild (fl (IDs::clip, "Clip", { 0.0f, 100.0f, 0.1f }, 0.0f, pct));
    o->addChild (fl (IDs::clipShape, "Clip Shape", { 0.0f, 100.0f, 0.1f }, 30.0f, [] (float v, int) { return v < 34 ? "Soft " + String (roundToInt (v)) : v > 66 ? "Hard " + String (roundToInt (v)) : "Medium " + String (roundToInt (v)); }));
    o->addChild (ch (IDs::quality, "Oversampling", qualityNames(), 2));
    layout.add (std::move (o));

    auto l = std::make_unique<AudioProcessorParameterGroup> ("level", "Level", "|");
    l->addChild (fl (IDs::gain, "Gain", { 0.0f, 24.0f, 0.01f }, 0.0f, db));
    l->addChild (fl (IDs::ceiling, "Ceiling", { -12.0f, 0.0f, 0.01f }, -1.0f, [] (float v, int) { return String (v, 2) + " dB"; }));
    l->addChild (ch (IDs::mode, "Mode", modeNames(), 1));
    l->addChild (fl (IDs::limRel, "Limiter Release", skewed (5.0f, 1000.0f, 100.0f), 100.0f, ms));
    l->addChild (fl (IDs::look, "Lookahead", { 0.2f, 10.0f, 0.01f }, 3.0f, ms));
    l->addChild (fl (IDs::balance, "Balance", { -100.0f, 100.0f, 0.1f }, 0.0f, [] (float v, int) { return std::abs (v) < 0.5f ? String ("Even") : v < 0 ? "Lows " + String (roundToInt (-v)) : "Highs " + String (roundToInt (v)); }));
    l->addChild (fl (IDs::link, "Stereo Link", { 0.0f, 100.0f, 0.1f }, 100.0f, pct));
    l->addChild (bl (IDs::truePeak, "True Peak", true));
    l->addChild (ch (IDs::dither, "Dither", ditherNames(), 0));
    layout.add (std::move (l));

    auto m = std::make_unique<AudioProcessorParameterGroup> ("mon", "Monitor", "|");
    m->addChild (ch (IDs::monitor, "Monitor", monitorNames(), 0));
    m->addChild (bl (IDs::cgm, "Equal Loudness Monitoring", false));
    m->addChild (ch (IDs::target, "Loudness Target", targetNames(), 0));
    m->addChild (fl (IDs::custom, "Custom Target", { -24.0f, -4.0f, 0.1f }, -9.0f, [] (float v, int) { return String (v, 1) + " LUFS"; }));
    layout.add (std::move (m));
    return layout;
}

//==============================================================================
struct LimPreset { const char* name; std::vector<std::pair<juce::String, float>> v; };
inline const std::vector<LimPreset>& factoryPresets()
{
    enum { Clear, Tight, Punchy, Loud };
    enum { Clean, Warm, Tape, Bright };
    static const std::vector<LimPreset> p {
        { "Default", {} },
        // ---- streaming
        { "STREAMING Spotify / YouTube -14",   { { "gain", 4 }, { "ceiling", -1 }, { "mode", Clear }, { "target", 0 } } },
        { "STREAMING Apple Music -16",          { { "gain", 2 }, { "ceiling", -1 }, { "mode", Clear }, { "target", 1 } } },
        { "STREAMING Transparent Master",       { { "comp_on", 1 }, { "comp_ratio", 1.5f }, { "comp_thr", -16 }, { "comp_attack", 30 }, { "comp_release", 250 }, { "gain", 5 }, { "mode", Clear }, { "lim_release", 150 } } },
        // ---- EDM
        { "EDM Festival Loud",                  { { "comp_on", 1 }, { "comp_ratio", 2 }, { "comp_thr", -14 }, { "comp_attack", 20 }, { "lopunch", 35 }, { "detail", 25 }, { "color", Warm }, { "drive", 20 }, { "clip", 45 }, { "gain", 10 }, { "ceiling", -0.3f }, { "mode", Loud }, { "target", 6 } } },
        { "EDM Big Room Punch",                 { { "lopunch", 50 }, { "transient", 20 }, { "clip", 35 }, { "gain", 9 }, { "ceiling", -0.3f }, { "mode", Punchy }, { "target", 6 } } },
        { "TECHNO Club Weight",                 { { "comp_on", 1 }, { "comp_ratio", 2 }, { "comp_thr", -16 }, { "comp_attack", 30 }, { "comp_schp", 90 }, { "lopunch", 30 }, { "color", Tape }, { "drive", 20 }, { "clip", 25 }, { "gain", 8 }, { "mode", Tight }, { "balance", -30 }, { "target", 7 } } },
        { "HOUSE Warm & Bouncy",                { { "comp_on", 1 }, { "comp_ratio", 1.8f }, { "comp_thr", -16 }, { "comp_attack", 25 }, { "lopunch", 25 }, { "color", Warm }, { "drive", 25 }, { "gain", 7 }, { "mode", Punchy }, { "target", 7 } } },
        { "TECH HOUSE Groove",                  { { "lopunch", 40 }, { "detail", 20 }, { "transient", 15 }, { "clip", 25 }, { "gain", 8 }, { "mode", Punchy }, { "target", 7 } } },
        { "DnB Hard & Clear",                   { { "comp_on", 1 }, { "comp_ratio", 2.5f }, { "comp_thr", -16 }, { "comp_attack", 10 }, { "detail", 30 }, { "transient", 20 }, { "clip", 40 }, { "gain", 9 }, { "mode", Tight }, { "target", 5 } } },
        { "DUBSTEP Heavy Low End",              { { "lopunch", 30 }, { "clip", 50 }, { "clipshape", 50 }, { "gain", 10 }, { "ceiling", -0.3f }, { "mode", Loud }, { "balance", -50 }, { "target", 6 } } },
        { "TRAP / HIP-HOP 808 Safe",            { { "comp_on", 1 }, { "comp_ratio", 2 }, { "comp_thr", -18 }, { "comp_schp", 120 }, { "clip", 35 }, { "gain", 8 }, { "mode", Tight }, { "balance", -45 }, { "target", 8 } } },
        { "FUTURE BASS Wide & Bright",          { { "detail", 35 }, { "color", Bright }, { "drive", 20 }, { "clip", 25 }, { "gain", 8 }, { "mode", Clear }, { "target", 5 } } },
        { "MELODIC TECHNO Deep",                { { "comp_on", 1 }, { "comp_ratio", 1.6f }, { "comp_thr", -16 }, { "comp_attack", 30 }, { "comp_release", 300 }, { "color", Tape }, { "drive", 15 }, { "gain", 7 }, { "mode", Clear }, { "target", 7 } } },
        { "HARDSTYLE Max",                      { { "transient", 25 }, { "color", Warm }, { "drive", 35 }, { "clip", 60 }, { "clipshape", 60 }, { "gain", 12 }, { "ceiling", -0.3f }, { "mode", Loud }, { "target", 6 } } },
        // ---- other genres
        { "POP Polished",                       { { "comp_on", 1 }, { "comp_ratio", 2 }, { "comp_thr", -18 }, { "comp_attack", 20 }, { "detail", 25 }, { "gain", 7 }, { "mode", Tight }, { "target", 9 } } },
        { "ROCK / LIVE Energy",                 { { "comp_on", 1 }, { "comp_ratio", 3 }, { "comp_thr", -18 }, { "comp_attack", 15 }, { "transient", 20 }, { "color", Tape }, { "drive", 25 }, { "gain", 7 }, { "mode", Punchy }, { "target", 9 } } },
        { "LO-FI / CHILL Soft",                 { { "color", Tape }, { "drive", 35 }, { "transient", -30 }, { "gain", 4 }, { "mode", Clear }, { "target", 0 } } },
        { "AMBIENT Dynamic",                    { { "gain", 2 }, { "mode", Clear }, { "lim_release", 300 }, { "target", 1 } } },
        { "PODCAST / VOICE -16",                { { "comp_on", 1 }, { "comp_ratio", 3 }, { "comp_thr", -24 }, { "comp_schp", 100 }, { "gain", 6 }, { "mode", Tight }, { "target", 1 } } },
        // ---- tools
        { "TOOL Bass-Heavy Mix (limit the lows)", { { "gain", 7 }, { "mode", Tight }, { "balance", -60 } } },
        { "TOOL Harsh Mix (limit the highs)",   { { "gain", 6 }, { "mode", Clear }, { "balance", 50 } } },
        { "TOOL Clip First, Then Limit",        { { "clip", 70 }, { "clipshape", 40 }, { "gain", 9 }, { "mode", Tight } } },
        { "TOOL Glue Only (no limiting)",       { { "comp_on", 1 }, { "comp_ratio", 2 }, { "comp_thr", -18 }, { "comp_attack", 30 }, { "gain", 0 }, { "ceiling", 0 } } },
        { "TOOL Safety Ceiling -1 dBTP",        { { "gain", 0 }, { "ceiling", -1 }, { "mode", Clear } } },
        { "MAXIMUM Loud (-5 LUFS)",             { { "comp_on", 1 }, { "comp_ratio", 2.5f }, { "comp_thr", -14 }, { "lopunch", 30 }, { "detail", 20 }, { "color", Warm }, { "drive", 30 }, { "clip", 70 }, { "clipshape", 55 }, { "gain", 14 }, { "ceiling", -0.3f }, { "mode", Loud } } },
    };
    return p;
}
