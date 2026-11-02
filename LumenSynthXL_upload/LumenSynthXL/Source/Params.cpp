#include "Params.h"
#include "Wavetable.h"

namespace lxl
{
using juce::String;
using juce::StringArray;
using Range = juce::NormalisableRange<float>;

const StringArray& oscModeNames()
{
    static StringArray a { "Standard", "Bend +", "Bend -", "Mirror", "PWM", "Hard Sync", "Formant", "Jump", "Quantize", "Fold" };
    return a;
}
const StringArray& filterTypeNames()
{
    static StringArray a { "Lowpass 12", "Lowpass 24", "Analog LP", "Highpass 12", "Highpass 24", "Bandpass", "Notch",
                           "Double Notch", "Comb +", "Comb -", "Formant" };
    return a;
}
const StringArray& insertTypeNames()
{
    static StringArray a { "Off", "Drive", "Tube", "Wavefold", "Rectify", "Bitcrush", "Downsample", "Hard Clip" };
    return a;
}
const StringArray& noiseTypeNames()   { static StringArray a { "White", "Pink", "Crackle", "Metal", "Vinyl" }; return a; }
const StringArray& subShapeNames()    { static StringArray a { "Sine", "Triangle", "Square" }; return a; }
const StringArray& routeNames()       { static StringArray a { "Filter A", "Filter B", "A + B", "Direct" }; return a; }
const StringArray& modTypeNames()     { static StringArray a { "Envelope", "LFO", "Performer", "Random" }; return a; }
const StringArray& lfoShapeNames()    { static StringArray a { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Pump", "Stairs", "Smooth Rnd" }; return a; }
const StringArray& distTypeNames()    { static StringArray a { "Soft", "Hard", "Tube", "Fold" }; return a; }

const StringArray& syncDivNames()
{
    static StringArray a { "8 Bar", "4 Bar", "2 Bar", "1 Bar", "1/2", "1/2 T", "1/4 D", "1/4", "1/4 T", "1/8 D", "1/8", "1/8 T",
                           "1/16 D", "1/16", "1/16 T", "1/32", "1/64" };
    return a;
}
double syncDivBeats (int i)
{
    static const double b[] = { 32.0, 16.0, 8.0, 4.0, 2.0, 4.0 / 3.0, 1.5, 1.0, 2.0 / 3.0, 0.75, 0.5, 1.0 / 3.0,
                                0.375, 0.25, 1.0 / 6.0, 0.125, 0.0625 };
    return b[juce::jlimit (0, (int) std::size (b) - 1, i)];
}

const StringArray& tableChoiceNames()
{
    static StringArray a = []
    {
        StringArray s = WavetableFactory::names();
        s.add ("User Import");
        return s;
    }();
    return a;
}

const StringArray& sourceNames()
{
    static StringArray a = []
    {
        StringArray s;
        s.add ("None");
        for (int i = 0; i < kNumModSlots; ++i) s.add ("Mod " + String (i + 1));
        s.add ("Velocity"); s.add ("Key Track"); s.add ("Aftertouch"); s.add ("Mod Wheel"); s.add ("Pitch Bend");
        for (int i = 0; i < kNumMacros; ++i) s.add ("Macro " + String (i + 1));
        s.add ("Voice Random"); s.add ("Amp Env");
        return s;
    }();
    return a;
}

const std::vector<ModDest>& modDests()
{
    static std::vector<ModDest> d = []
    {
        std::vector<ModDest> v;
        v.push_back ({ "", "None", false });
        for (int i = 0; i < kNumOsc; ++i)
        {
            const String p = "Osc " + String (i + 1) + " ";
            v.push_back ({ oscId (i, "semi"),   p + "Pitch" });
            v.push_back ({ oscId (i, "fine"),   p + "Fine" });
            v.push_back ({ oscId (i, "pos"),    p + "WT Position" });
            v.push_back ({ oscId (i, "warp"),   p + "Warp" });
            v.push_back ({ oscId (i, "level"),  p + "Level" });
            v.push_back ({ oscId (i, "pan"),    p + "Pan" });
            v.push_back ({ oscId (i, "detune"), p + "Detune" });
            v.push_back ({ oscId (i, "fm"),     p + "FM" });
            v.push_back ({ oscId (i, "ring"),   p + "Ring" });
        }
        v.push_back ({ "sub_level",   "Sub Level" });
        v.push_back ({ "noise_level", "Noise Level" });
        v.push_back ({ "noise_color", "Noise Color" });
        for (int i = 0; i < kNumFilters; ++i)
        {
            const String p = String ("Filter ") + (i == 0 ? "A " : "B ");
            v.push_back ({ filId (i, "cutoff"), p + "Cutoff" });
            v.push_back ({ filId (i, "reso"),   p + "Resonance" });
            v.push_back ({ filId (i, "drive"),  p + "Drive" });
            v.push_back ({ filId (i, "mix"),    p + "Mix" });
        }
        v.push_back ({ "fb_amount", "Feedback" });
        for (int i = 0; i < kNumInserts; ++i)
            v.push_back ({ insId (i, "amount"), "Insert " + String (i + 1) + " Amount" });
        v.push_back ({ "amp_level", "Amp Level" });
        v.push_back ({ "amp_pan",   "Amp Pan" });
        v.push_back ({ "voice_pitch", "Voice Pitch" });
        // global (only macros / wheel / bend act on these)
        v.push_back ({ "fx_dist_drive",  "FX Dist Drive",   true });
        v.push_back ({ "fx_chorus_mix",  "FX Chorus Mix",   true });
        v.push_back ({ "fx_phaser_mix",  "FX Phaser Mix",   true });
        v.push_back ({ "fx_delay_mix",   "FX Delay Mix",    true });
        v.push_back ({ "fx_delay_fb",    "FX Delay Feedback", true });
        v.push_back ({ "fx_rev_mix",     "FX Reverb Mix",   true });
        v.push_back ({ "fx_rev_size",    "FX Reverb Size",  true });
        v.push_back ({ "fx_tape_mix",    "FX Tape Mix",     true });
        v.push_back ({ "fx_hyper_mix",   "FX Hyper Mix",    true });
        v.push_back ({ "fx_ott_depth",   "FX OTT Depth",    true });
        v.push_back ({ "fx_width",       "FX Stereo Width", true });
        v.push_back ({ "fx_eq_high",     "FX EQ High",      true });
        v.push_back ({ "amp_a",          "Amp Attack",      true });
        v.push_back ({ "amp_d",          "Amp Decay",       true });
        v.push_back ({ "amp_r",          "Amp Release",     true });
        v.push_back ({ "mod1_d",         "Mod 1 Decay",     true });
        v.push_back ({ "mod2_d",         "Mod 2 Decay",     true });
        return v;
    }();
    return d;
}
int numModDests() { return (int) modDests().size(); }
int destIndexForParam (const String& id)
{
    const auto& d = modDests();
    for (int i = 1; i < (int) d.size(); ++i)
        if (d[(size_t) i].paramId == id) return i;
    return -1;
}
StringArray destNames()
{
    StringArray s;
    for (auto& d : modDests()) s.add (d.name);
    return s;
}

// ---------- value formatting ----------
namespace fmt
{
static String pct (float v, int)    { return String (juce::roundToInt (v * 100.0f)) + "%"; }
static String bipct (float v, int)  { const int p = juce::roundToInt (v * 100.0f); return (p > 0 ? "+" : "") + String (p) + "%"; }
static String hz (float v, int)     { return v >= 1000.0f ? String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : String (v, v < 100.0f ? 1 : 0) + " Hz"; }
static String lfoHz (float v, int)  { return v < 1.0f ? String (v, 2) + " Hz" : String (v, 1) + " Hz"; }
static String secs (float v, int)   { return v < 1.0f ? String (juce::roundToInt (v * 1000.0f)) + " ms" : String (v, 2) + " s"; }
static String ms (float v, int)     { return String (juce::roundToInt (v)) + " ms"; }
static String semis (float v, int)  { const int s = juce::roundToInt (v); return (s > 0 ? "+" : "") + String (s) + " st"; }
static String cents (float v, int)  { const int s = juce::roundToInt (v); return (s > 0 ? "+" : "") + String (s) + " ct"; }
static String db (float v, int)     { return (v <= -59.5f ? String ("-inf") : String (v, 1)) + " dB"; }
static String ratio (float v, int)  { return "x" + String (v, 2); }
static String pan (float v, int)
{
    const int p = juce::roundToInt (v * 100.0f);
    return p == 0 ? String ("C") : (p < 0 ? "L" + String (-p) : "R" + String (p));
}
} // namespace fmt

using Attr = juce::AudioParameterFloatAttributes;

struct LayoutBuilder
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    void f (const String& id, const String& name, Range r, float def, String (*fn) (float, int) = fmt::pct)
    {
        params.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, def,
                                                                        Attr().withStringFromValueFunction (fn)));
    }
    void c (const String& id, const String& name, const StringArray& choices, int def)
    {
        params.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, choices, def));
    }
    void b (const String& id, const String& name, bool def)
    {
        params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
    }
    void i (const String& id, const String& name, int lo, int hi, int def)
    {
        params.push_back (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id, 1 }, name, lo, hi, def));
    }
};

static Range skewed (float lo, float hi, float centre)
{
    Range r (lo, hi);
    r.setSkewForCentre (centre);
    return r;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    LayoutBuilder L;

    // ---------------- oscillators ----------------
    for (int o = 0; o < kNumOsc; ++o)
    {
        const String n = "Osc " + String (o + 1) + " ";
        L.b (oscId (o, "on"), n + "On", o == 0);
        L.c (oscId (o, "table"), n + "Wavetable", tableChoiceNames(), o == 0 ? 0 : 2);
        L.f (oscId (o, "pos"), n + "WT Position", Range (0.0f, 1.0f), o == 0 ? 0.62f : 0.0f);
        L.c (oscId (o, "mode"), n + "Mode", oscModeNames(), 0);
        L.f (oscId (o, "warp"), n + "Warp", Range (0.0f, 1.0f), 0.0f);
        L.i (oscId (o, "oct"), n + "Octave", -3, 3, 0);
        L.f (oscId (o, "semi"), n + "Semitone", Range (-24.0f, 24.0f, 1.0f), 0.0f, fmt::semis);
        L.f (oscId (o, "fine"), n + "Fine", Range (-100.0f, 100.0f), 0.0f, fmt::cents);
        L.f (oscId (o, "level"), n + "Level", Range (0.0f, 1.0f), 0.75f);
        L.f (oscId (o, "pan"), n + "Pan", Range (-1.0f, 1.0f), 0.0f, fmt::pan);
        L.i (oscId (o, "voices"), n + "Unison", 1, kMaxUnison, 1);
        L.f (oscId (o, "detune"), n + "Detune", Range (0.0f, 1.0f), 0.25f);
        L.f (oscId (o, "spread"), n + "Stereo Spread", Range (0.0f, 1.0f), 0.7f);
        L.f (oscId (o, "blend"), n + "Unison Blend", Range (0.0f, 1.0f), 0.6f);
        L.f (oscId (o, "fm"), n + "FM Amount", Range (0.0f, 1.0f), 0.0f);
        L.f (oscId (o, "fmratio"), n + "FM Ratio", Range (0.5f, 8.0f, 0.5f), 1.0f, fmt::ratio);
        L.f (oscId (o, "ring"), n + "Ring Mod", Range (0.0f, 1.0f), 0.0f);
        L.f (oscId (o, "phase"), n + "Phase", Range (0.0f, 1.0f), 0.0f);
        L.f (oscId (o, "rndphase"), n + "Random Phase", Range (0.0f, 1.0f), 1.0f);
        L.c (oscId (o, "route"), n + "Route", routeNames(), 0);
    }

    // ---------------- sub & noise ----------------
    L.b ("sub_on", "Sub On", false);
    L.c ("sub_oct", "Sub Octave", StringArray { "-1 Oct", "-2 Oct" }, 0);
    L.c ("sub_shape", "Sub Shape", subShapeNames(), 0);
    L.f ("sub_level", "Sub Level", Range (0.0f, 1.0f), 0.5f);
    L.c ("sub_route", "Sub Route", routeNames(), RouteDirect);

    L.b ("noise_on", "Noise On", false);
    L.c ("noise_type", "Noise Type", noiseTypeNames(), 0);
    L.f ("noise_color", "Noise Color", Range (0.0f, 1.0f), 0.5f);
    L.f ("noise_level", "Noise Level", Range (0.0f, 1.0f), 0.25f);
    L.c ("noise_route", "Noise Route", routeNames(), 0);

    // ---------------- filters ----------------
    for (int f = 0; f < kNumFilters; ++f)
    {
        const String n = String ("Filter ") + (f == 0 ? "A " : "B ");
        L.b (filId (f, "on"), n + "On", f == 0);
        L.c (filId (f, "type"), n + "Type", filterTypeNames(), f == 0 ? FLp24 : FHp12);
        L.f (filId (f, "cutoff"), n + "Cutoff", skewed (20.0f, 20000.0f, 900.0f), f == 0 ? 9000.0f : 80.0f, fmt::hz);
        L.f (filId (f, "reso"), n + "Resonance", Range (0.0f, 1.0f), 0.15f);
        L.f (filId (f, "drive"), n + "Drive", Range (0.0f, 1.0f), 0.0f);
        L.f (filId (f, "key"), n + "Key Track", Range (0.0f, 1.0f), 0.0f);
        L.f (filId (f, "mix"), n + "Mix", Range (0.0f, 1.0f), 1.0f);
    }
    L.c ("filter_struct", "Filter Routing", StringArray { "Serial", "Parallel" }, 0);
    L.f ("fb_amount", "Feedback", Range (0.0f, 1.0f), 0.0f);

    // ---------------- inserts ----------------
    for (int k = 0; k < kNumInserts; ++k)
    {
        const String n = "Insert " + String (k + 1) + " ";
        L.c (insId (k, "type"), n + "Type", insertTypeNames(), 0);
        L.f (insId (k, "amount"), n + "Amount", Range (0.0f, 1.0f), 0.4f);
        L.f (insId (k, "mix"), n + "Mix", Range (0.0f, 1.0f), 1.0f);
        L.c (insId (k, "pos"), n + "Position", StringArray { "Pre Filter", "Post Filter" }, k == 0 ? 0 : 1);
    }

    // ---------------- amp / voice ----------------
    L.f ("amp_a", "Amp Attack", skewed (0.0f, 10.0f, 0.4f), 0.003f, fmt::secs);
    L.f ("amp_d", "Amp Decay", skewed (0.001f, 10.0f, 0.6f), 0.35f, fmt::secs);
    L.f ("amp_s", "Amp Sustain", Range (0.0f, 1.0f), 0.8f);
    L.f ("amp_r", "Amp Release", skewed (0.001f, 15.0f, 0.8f), 0.25f, fmt::secs);
    L.f ("amp_curve", "Amp Curve", Range (0.0f, 1.0f), 0.35f);
    L.f ("amp_level", "Amp Level", Range (0.0f, 1.0f), 0.8f);
    L.f ("amp_pan", "Amp Pan", Range (-1.0f, 1.0f), 0.0f, fmt::pan);
    L.f ("amp_vel", "Velocity Sens", Range (0.0f, 1.0f), 0.4f);

    L.i ("voice_poly", "Polyphony", 1, kMaxVoices, 16);
    L.c ("voice_mode", "Voice Mode", StringArray { "Poly", "Mono", "Legato" }, 0);
    L.f ("voice_glide", "Glide", skewed (0.0f, 2.0f, 0.25f), 0.0f, fmt::secs);
    L.f ("voice_pitch", "Transpose", Range (-48.0f, 48.0f, 1.0f), 0.0f, fmt::semis);
    L.f ("voice_drift", "Analog Drift", Range (0.0f, 1.0f), 0.25f);
    L.i ("voice_pbrange", "Bend Range", 0, 24, 2);
    L.f ("master_vol", "Master Volume", Range (-60.0f, 12.0f), -6.0f, fmt::db);

    // ---------------- modulators ----------------
    static const int defaultTypes[kNumModSlots] = { ModEnv, ModEnv, ModLfo, ModLfo, ModPerformer, ModPerformer, ModRandom, ModLfo, ModEnv };
    for (int s = 0; s < kNumModSlots; ++s)
    {
        const String n = "Mod " + String (s + 1) + " ";
        L.c (modId (s, "type"), n + "Type", modTypeNames(), defaultTypes[s]);
        L.f (modId (s, "rate"), n + "Rate", skewed (0.01f, 40.0f, 2.0f), 2.0f, fmt::lfoHz);
        L.b (modId (s, "sync"), n + "Sync", true);
        L.c (modId (s, "div"), n + "Division", syncDivNames(), defaultTypes[s] == ModPerformer ? 13 : 7);
        L.c (modId (s, "shape"), n + "Shape", lfoShapeNames(), 0);
        L.f (modId (s, "phase"), n + "Phase", Range (0.0f, 1.0f), 0.0f);
        L.b (modId (s, "retrig"), n + "Retrigger", true);
        L.b (modId (s, "bipolar"), n + "Bipolar", defaultTypes[s] == ModLfo);
        L.f (modId (s, "smooth"), n + "Smooth", Range (0.0f, 1.0f), 0.1f);
        L.f (modId (s, "a"), n + "Attack", skewed (0.0f, 10.0f, 0.4f), 0.005f, fmt::secs);
        L.f (modId (s, "d"), n + "Decay", skewed (0.001f, 10.0f, 0.6f), 0.4f, fmt::secs);
        L.f (modId (s, "s"), n + "Sustain", Range (0.0f, 1.0f), 0.0f);
        L.f (modId (s, "r"), n + "Release", skewed (0.001f, 15.0f, 0.8f), 0.3f, fmt::secs);
        L.f (modId (s, "curve"), n + "Curve", Range (0.0f, 1.0f), 0.3f);
        L.i (modId (s, "len"), n + "Steps", 1, kNumSteps, kNumSteps);
        for (int k = 0; k < kNumSteps; ++k)
        {
            static const float pat[kNumSteps] = { 1.0f, 0.25f, 0.6f, 0.1f, 0.85f, 0.3f, 0.5f, 0.15f,
                                                  1.0f, 0.4f, 0.7f, 0.2f, 0.9f, 0.35f, 0.55f, 0.0f };
            L.f (stepId (s, k), n + "Step " + String (k + 1), Range (0.0f, 1.0f), pat[k]);
        }
    }

    // ---------------- matrix ----------------
    for (int r = 0; r < kNumMatrix; ++r)
    {
        const String n = "Matrix " + String (r + 1) + " ";
        L.c (mtxId (r, "src"), n + "Source", sourceNames(), 0);
        L.c (mtxId (r, "dst"), n + "Destination", destNames(), 0);
        L.f (mtxId (r, "amt"), n + "Amount", Range (-1.0f, 1.0f), 0.0f, fmt::bipct);
    }

    for (int m = 0; m < kNumMacros; ++m)
        L.f (macroId (m), "Macro " + String (m + 1), Range (0.0f, 1.0f), 0.0f);

    // ---------------- master FX ----------------
    L.b ("fx_dist_on", "Dist On", false);
    L.c ("fx_dist_type", "Dist Type", distTypeNames(), 0);
    L.f ("fx_dist_drive", "Dist Drive", Range (0.0f, 1.0f), 0.3f);
    L.f ("fx_dist_tone", "Dist Tone", Range (0.0f, 1.0f), 0.5f);
    L.f ("fx_dist_mix", "Dist Mix", Range (0.0f, 1.0f), 1.0f);

    L.b ("fx_chorus_on", "Chorus On", false);
    L.f ("fx_chorus_rate", "Chorus Rate", skewed (0.05f, 5.0f, 0.8f), 0.6f, fmt::lfoHz);
    L.f ("fx_chorus_depth", "Chorus Depth", Range (0.0f, 1.0f), 0.5f);
    L.f ("fx_chorus_mix", "Chorus Mix", Range (0.0f, 1.0f), 0.4f);

    L.b ("fx_phaser_on", "Phaser On", false);
    L.f ("fx_phaser_rate", "Phaser Rate", skewed (0.02f, 8.0f, 0.6f), 0.3f, fmt::lfoHz);
    L.f ("fx_phaser_depth", "Phaser Depth", Range (0.0f, 1.0f), 0.6f);
    L.f ("fx_phaser_fb", "Phaser Feedback", Range (0.0f, 0.95f), 0.5f);
    L.f ("fx_phaser_mix", "Phaser Mix", Range (0.0f, 1.0f), 0.5f);

    L.b ("fx_delay_on", "Delay On", false);
    L.b ("fx_delay_sync", "Delay Sync", true);
    L.c ("fx_delay_div", "Delay Division", syncDivNames(), 9);
    L.f ("fx_delay_time", "Delay Time", skewed (5.0f, 2000.0f, 350.0f), 350.0f, fmt::ms);
    L.f ("fx_delay_fb", "Delay Feedback", Range (0.0f, 0.98f), 0.35f);
    L.b ("fx_delay_pp", "Delay Ping Pong", true);
    L.f ("fx_delay_tone", "Delay Tone", Range (0.0f, 1.0f), 0.5f);
    L.f ("fx_delay_mix", "Delay Mix", Range (0.0f, 1.0f), 0.25f);

    L.b ("fx_rev_on", "Reverb On", false);
    L.f ("fx_rev_size", "Reverb Size", Range (0.0f, 1.0f), 0.6f);
    L.f ("fx_rev_decay", "Reverb Decay", skewed (0.2f, 20.0f, 2.5f), 2.5f, fmt::secs);
    L.f ("fx_rev_damp", "Reverb Damping", Range (0.0f, 1.0f), 0.45f);
    L.f ("fx_rev_pre", "Reverb Pre-Delay", Range (0.0f, 200.0f), 12.0f, fmt::ms);
    L.f ("fx_rev_width", "Reverb Width", Range (0.0f, 1.0f), 1.0f);
    L.f ("fx_rev_mix", "Reverb Mix", Range (0.0f, 1.0f), 0.22f);

    L.b ("fx_tape_on", "Tape On", false);
    L.f ("fx_tape_wow", "Tape Wow", Range (0.0f, 1.0f), 0.3f);
    L.f ("fx_tape_flutter", "Tape Flutter", Range (0.0f, 1.0f), 0.2f);
    L.f ("fx_tape_sat", "Tape Saturation", Range (0.0f, 1.0f), 0.3f);
    L.f ("fx_tape_mix", "Tape Mix", Range (0.0f, 1.0f), 1.0f);

    L.b ("fx_hyper_on", "Hyper On", false);
    L.f ("fx_hyper_rate", "Hyper Rate", skewed (0.05f, 6.0f, 0.8f), 0.6f, fmt::lfoHz);
    L.f ("fx_hyper_detune", "Hyper Detune", Range (0.0f, 1.0f), 0.35f);
    L.f ("fx_hyper_dim", "Dimension", Range (0.0f, 1.0f), 0.3f);
    L.f ("fx_hyper_mix", "Hyper Mix", Range (0.0f, 1.0f), 0.4f);

    L.b ("fx_ott_on", "OTT On", false);
    L.f ("fx_ott_depth", "OTT Depth", Range (0.0f, 1.0f), 0.35f);
    L.f ("fx_ott_time", "OTT Time", Range (0.0f, 1.0f), 0.3f);
    L.f ("fx_ott_up", "OTT Upward", Range (0.0f, 1.0f), 1.0f);
    L.f ("fx_ott_down", "OTT Downward", Range (0.0f, 1.0f), 1.0f);
    L.f ("fx_ott_gain", "OTT Output", Range (-24.0f, 12.0f), 0.0f, fmt::db);

    L.b ("fx_eq_on", "EQ On", false);
    L.f ("fx_eq_hp", "EQ Low Cut", skewed (20.0f, 1000.0f, 120.0f), 20.0f, fmt::hz);
    L.f ("fx_eq_low", "EQ Low", Range (-12.0f, 12.0f), 0.0f, fmt::db);
    L.f ("fx_eq_midf", "EQ Mid Freq", skewed (200.0f, 8000.0f, 1200.0f), 1000.0f, fmt::hz);
    L.f ("fx_eq_mid", "EQ Mid", Range (-12.0f, 12.0f), 0.0f, fmt::db);
    L.f ("fx_eq_high", "EQ High", Range (-12.0f, 12.0f), 0.0f, fmt::db);

    L.f ("fx_width", "Stereo Width", Range (0.0f, 2.0f), 1.0f);
    L.f ("fx_bassmono", "Bass Mono", skewed (20.0f, 400.0f, 120.0f), 20.0f, [] (float v, int) { return v <= 21.0f ? juce::String ("Off") : juce::String (juce::roundToInt (v)) + " Hz"; });

    L.b ("fx_clip_on", "Output Soft Clip", true);
    L.c ("quality", "Quality", StringArray { "Standard", "High (2x)" }, 1);

    return { L.params.begin(), L.params.end() };
}

bool isMutationExcluded (const String& id)
{
    return id == "master_vol" || id == "voice_poly" || id == "voice_mode" || id == "voice_pbrange" || id.startsWith ("macro")
        || id.startsWith ("mtx") || id.endsWith ("_on") || id.endsWith ("_table") || id.endsWith ("_type")
        || id.endsWith ("_route") || id == "filter_struct" || id.endsWith ("_sync") || id.endsWith ("_div")
        || id.endsWith ("_retrig") || id.endsWith ("_bipolar") || id.endsWith ("_voices") || id.endsWith ("_oct")
        || (id.startsWith ("ins") && id.endsWith ("_pos")) || id.endsWith ("_mode") || id == "fx_clip_on"
        || id == "fx_delay_pp" || id == "quality" || id == "voice_pitch" || id == "fx_bassmono" || id == "fx_ott_gain" || id == "fx_width" || id.endsWith ("_len") || id.endsWith ("_shape") || id == "sub_oct";
}
} // namespace lxl
