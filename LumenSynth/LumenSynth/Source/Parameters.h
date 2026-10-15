#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// =====================================================================================
// Lumen Synth 2 parameter table. Everything (layout, presets, GENERATE / MUTATE, mod targets)
// is driven from this single list so ids, ranges and defaults can never drift apart.
// =====================================================================================
namespace lumen::synth
{

constexpr int kNumOsc = 3, kNumLfo = 4, kNumSlots = 16, kNumFactoryTables = 12;

inline const juce::StringArray& tableNames()
{ static const juce::StringArray s { "Basic Shapes", "Saw Stack", "PWM", "Vox Formant", "Hard Sync", "FM Growl", "Phase Res", "Organ", "Fold",
                                     "Spectral", "Dirty Saw", "Metal FM", "User WT" }; return s; }
inline const juce::StringArray& warpNames()   { static const juce::StringArray s { "Off", "Sync", "Bend", "Squeeze", "FM <", "Mirror", "Asym", "Quantize", "Remap" }; return s; }
inline const juce::StringArray& subShapes()   { static const juce::StringArray s { "Sine", "Triangle", "Saw", "Square" }; return s; }
inline const juce::StringArray& subOcts()     { static const juce::StringArray s { "-2 Oct", "-1 Oct", "0 Oct" }; return s; }
inline const juce::StringArray& filterTypes() { static const juce::StringArray s { "Ladder LP24", "LP12", "LP24", "HP12", "HP24", "Band", "Notch", "Comb +", "Comb -", "Formant", "Phaser" }; return s; }
inline const juce::StringArray& routeNames()  { static const juce::StringArray s { "Serial", "Parallel" }; return s; }
inline const juce::StringArray& lfoShapes()   { static const juce::StringArray s { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Pump", "S&H", "Smooth Rnd", "Custom" }; return s; }
inline const juce::StringArray& lfoModes()    { static const juce::StringArray s { "Free", "Retrig", "Envelope" }; return s; }
inline const juce::StringArray& divNames()    { static const juce::StringArray s { "4/1", "2/1", "1/1", "1/2", "1/2D", "1/2T", "1/4", "1/4D", "1/4T", "1/8", "1/8D", "1/8T", "1/16", "1/16D", "1/16T", "1/32" }; return s; }
inline double divBeats (int i)
{
    static const double b[] = { 16, 8, 4, 2, 3, 4.0 / 3.0, 1, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0, 0.25, 0.375, 1.0 / 6.0, 0.125 };
    return b[juce::jlimit (0, 15, i)];
}
inline const juce::StringArray& modSources()
{ static const juce::StringArray s { "Off", "LFO 1", "LFO 2", "LFO 3", "LFO 4", "Env 1", "Env 2", "Env 3", "Velocity", "Mod Wheel", "Aftertouch",
                                     "Key Track", "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Note Random", "Pitch Bend" }; return s; }
inline const juce::StringArray& modDests()
{ static const juce::StringArray s { "Off",
    "A Position", "A Pitch", "A Level", "A Detune", "A Warp", "A Pan",
    "B Position", "B Pitch", "B Level", "B Detune", "B Warp", "B Pan",
    "C Position", "C Pitch", "C Level", "C Detune", "C Warp", "C Pan",
    "Osc Pitch", "Cutoff", "Resonance", "Filter Drive", "F2 Cutoff", "F2 Res",
    "Sub Level", "Noise Level", "Pan", "Volume", "LFO 1 Rate", "LFO 2 Rate", "LFO 3 Rate", "LFO 4 Rate", "Unison Width", "Filter Env",
    "FX Dist Drive", "FX Dist Mix", "FX Hyper Mix", "FX Chorus Mix", "FX Phaser Mix", "FX Flanger Mix",
    "FX Delay Mix", "FX Delay Fdbk", "FX Reverb Mix", "FX Reverb Size", "FX Pump Depth" }; return s; }
inline const juce::StringArray& distModes()   { static const juce::StringArray s { "Warm", "Hard", "Fold", "Crush" }; return s; }
inline const juce::StringArray& pumpDivs()    { static const juce::StringArray s { "1/1", "1/2", "1/4", "1/8", "1/16" }; return s; }
inline const juce::StringArray& categories()  { static const juce::StringArray s { "Bass", "Lead", "Pluck", "Pad", "Keys", "FX" }; return s; }
inline const juce::StringArray& arpModes()    { static const juce::StringArray s { "Up", "Down", "Up/Down", "Random", "As Played", "Chord" }; return s; }
inline const juce::StringArray& arpRates()    { static const juce::StringArray s { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }; return s; }
inline double arpBeats (int i) { static const double b[] = { 1, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 }; return b[juce::jlimit (0, 5, i)]; }
inline const juce::StringArray& chordNames()  { static const juce::StringArray s { "Off", "Major", "Minor", "Maj7", "Min7", "Sus2", "Sus4", "Power", "Octaves", "Add9", "Min9" }; return s; }

// FX rack ids (the order is user-definable and saved with the patch)
inline const juce::StringArray& fxIds()   { static const juce::StringArray s { "hyp", "dist", "fla", "pha", "cho", "dly", "cmp", "rev", "eq", "ott", "pump" }; return s; }
inline const juce::StringArray& fxNames() { static const juce::StringArray s { "HYPER/DIM", "DISTORTION", "FLANGER", "PHASER", "CHORUS", "DELAY", "COMPRESSOR", "REVERB", "EQ", "OTT", "PUMP" }; return s; }

enum class Kind { Float, Int, Bool, Choice };

struct Spec
{
    juce::String id, name;
    Kind kind;
    float lo, hi, def, skewCentre;          // skewCentre <= 0 -> linear
    const juce::StringArray* choices = nullptr;
    juce::String unit;
};

inline const std::vector<Spec>& specs()
{
    static const std::vector<Spec> s = []
    {
        std::vector<Spec> v;
        auto F = [&] (juce::String id, juce::String nm, float lo, float hi, float d, float skew = -1.0f, juce::String unit = {})
        { v.push_back ({ id, nm, Kind::Float, lo, hi, d, skew, nullptr, unit }); };
        auto I = [&] (juce::String id, juce::String nm, int lo, int hi, int d)
        { v.push_back ({ id, nm, Kind::Int, (float) lo, (float) hi, (float) d, -1.0f, nullptr, {} }); };
        auto B = [&] (juce::String id, juce::String nm, bool d)
        { v.push_back ({ id, nm, Kind::Bool, 0, 1, d ? 1.0f : 0.0f, -1.0f, nullptr, {} }); };
        auto C = [&] (juce::String id, juce::String nm, const juce::StringArray& c, int d)
        { v.push_back ({ id, nm, Kind::Choice, 0, (float) (c.size() - 1), (float) d, -1.0f, &c, {} }); };

        for (auto o : { juce::String ("a"), juce::String ("b"), juce::String ("c") })
        {
            const auto N = "Osc " + o.toUpperCase() + " ";
            B (o + "_on",     N + "On", o == "a");
            C (o + "_table",  N + "Table", tableNames(), 1);
            F (o + "_pos",    N + "Position", 0, 1, 0);
            C (o + "_warpm",  N + "Warp Mode", warpNames(), 0);
            F (o + "_warp",   N + "Warp", 0, 1, 0);
            I (o + "_oct",    N + "Octave", -3, 3, 0);
            I (o + "_semi",   N + "Semi", -12, 12, 0);
            F (o + "_fine",   N + "Fine", -100, 100, 0, -1.0f, "ct");
            F (o + "_level",  N + "Level", 0, 1, 0.75f);
            F (o + "_pan",    N + "Pan", -1, 1, 0);
            I (o + "_uni",    N + "Unison", 1, 16, 1);
            F (o + "_det",    N + "Detune", 0, 1, 0.25f);
            F (o + "_blend",  N + "Blend", 0, 1, 0.75f);
            F (o + "_width",  N + "Width", 0, 1, 0.8f);
            F (o + "_rand",   N + "Phase Rand", 0, 1, 1);
        }
        B ("sub_on", "Sub On", false);
        C ("sub_shape", "Sub Shape", subShapes(), 0);
        C ("sub_oct", "Sub Octave", subOcts(), 1);
        F ("sub_level", "Sub Level", 0, 1, 0.6f);
        B ("noise_on", "Noise On", false);
        F ("noise_level", "Noise Level", 0, 1, 0.25f);
        F ("noise_color", "Noise Color", 0, 1, 0.5f);

        B ("flt_on", "Filter On", true);
        C ("flt_type", "Filter Type", filterTypes(), 0);
        F ("flt_cut", "Cutoff", 20, 20000, 20000, 800, "Hz");
        F ("flt_res", "Resonance", 0, 1, 0.1f);
        F ("flt_drive", "Filter Drive", 0, 1, 0);
        F ("flt_env", "Filter Env", -1, 1, 0);
        F ("flt_key", "Key Track", 0, 1, 0);
        B ("f2_on", "Filter 2 On", false);
        C ("f2_type", "Filter 2 Type", filterTypes(), 3);
        F ("f2_cut", "F2 Cutoff", 20, 20000, 200, 800, "Hz");
        F ("f2_res", "F2 Resonance", 0, 1, 0.1f);
        F ("f2_drive", "F2 Drive", 0, 1, 0);
        F ("f2_env", "F2 Env", -1, 1, 0);
        F ("f2_key", "F2 Key Track", 0, 1, 0);
        C ("flt_route", "Filter Routing", routeNames(), 0);

        for (int e = 1; e <= 3; ++e)
        {
            const auto p = "e" + juce::String (e) + "_";
            const auto N = "Env " + juce::String (e) + " ";
            F (p + "a", N + "Attack", 0, 10, e == 1 ? 0.002f : 0.001f, 0.25f, "s");
            F (p + "d", N + "Decay", 0, 10, 0.4f, 0.6f, "s");
            F (p + "s", N + "Sustain", 0, 1, e == 1 ? 1.0f : 0.0f);
            F (p + "r", N + "Release", 0, 15, 0.12f, 0.8f, "s");
        }
        for (int l = 1; l <= kNumLfo; ++l)
        {
            const auto p = "l" + juce::String (l) + "_";
            const auto N = "LFO " + juce::String (l) + " ";
            C (p + "shape", N + "Shape", lfoShapes(), 0);
            F (p + "rate", N + "Rate", 0.02f, 40, 2, 3, "Hz");
            B (p + "sync", N + "Sync", true);
            C (p + "div", N + "Division", divNames(), 6);
            C (p + "mode", N + "Mode", lfoModes(), 1);
        }
        for (int m = 1; m <= kNumSlots; ++m)
        {
            const auto p = "m" + juce::String (m) + "_";
            const auto N = "Mod " + juce::String (m) + " ";
            C (p + "src", N + "Source", modSources(), 0);
            C (p + "dst", N + "Dest", modDests(), 0);
            F (p + "amt", N + "Amount", -1, 1, 0);
        }
        for (int m = 1; m <= 4; ++m) F ("macro" + juce::String (m), "Macro " + juce::String (m), 0, 1, 0);

        I ("voices", "Voices", 1, 16, 8);
        B ("legato", "Legato", false);
        F ("glide", "Glide", 0, 2, 0, 0.25f, "s");
        I ("bend", "Bend Range", 0, 24, 2);
        F ("velsens", "Velocity", 0, 1, 0.5f);
        B ("hq", "HQ Mode", false);

        B ("arp_on", "Arp On", false);
        C ("arp_mode", "Arp Mode", arpModes(), 0);
        C ("arp_rate", "Arp Rate", arpRates(), 3);
        I ("arp_oct", "Arp Octaves", 1, 4, 1);
        F ("arp_gate", "Arp Gate", 0.05f, 1, 0.6f);
        F ("arp_swing", "Arp Swing", 0, 0.75f, 0);
        C ("chord", "Chord", chordNames(), 0);

        B ("hyp_on", "Hyper On", false);
        F ("hyp_detune", "Hyper Detune", 0, 1, 0.35f);
        F ("hyp_dim", "Dimension", 0, 1, 0.5f);
        F ("hyp_mix", "Hyper Mix", 0, 1, 0.4f);
        B ("dist_on", "Dist On", false);
        C ("dist_mode", "Dist Mode", distModes(), 0);
        F ("dist_drive", "Dist Drive", 0, 1, 0.3f);
        F ("dist_mix", "Dist Mix", 0, 1, 1);
        B ("fla_on", "Flanger On", false);
        F ("fla_rate", "Flanger Rate", 0.02f, 5, 0.2f, 0.6f, "Hz");
        F ("fla_depth", "Flanger Depth", 0, 1, 0.6f);
        F ("fla_fb", "Flanger Feedback", -0.95f, 0.95f, 0.5f);
        F ("fla_mix", "Flanger Mix", 0, 1, 0.5f);
        B ("pha_on", "Phaser On", false);
        F ("pha_rate", "Phaser Rate", 0.02f, 5, 0.3f, 0.6f, "Hz");
        F ("pha_depth", "Phaser Depth", 0, 1, 0.7f);
        F ("pha_fb", "Phaser Feedback", 0, 0.95f, 0.5f);
        F ("pha_mix", "Phaser Mix", 0, 1, 0.5f);
        B ("cho_on", "Chorus On", false);
        F ("cho_rate", "Chorus Rate", 0.05f, 5, 0.6f, 0.8f, "Hz");
        F ("cho_depth", "Chorus Depth", 0, 1, 0.5f);
        F ("cho_mix", "Chorus Mix", 0, 1, 0.4f);
        B ("dly_on", "Delay On", false);
        C ("dly_div", "Delay Time", divNames(), 10);
        F ("dly_fb", "Delay Feedback", 0, 0.95f, 0.35f);
        F ("dly_tone", "Delay Tone", 0, 1, 0.6f);
        F ("dly_mix", "Delay Mix", 0, 1, 0.2f);
        B ("dly_ping", "Ping Pong", true);
        B ("cmp_on", "Comp On", false);
        F ("cmp_thresh", "Comp Threshold", -48, 0, -18, -1.0f, "dB");
        F ("cmp_ratio", "Comp Ratio", 1, 20, 4, 4);
        F ("cmp_attack", "Comp Attack", 0.1f, 100, 5, 10, "ms");
        F ("cmp_release", "Comp Release", 10, 1000, 120, 150, "ms");
        F ("cmp_gain", "Comp Gain", 0, 24, 3, -1.0f, "dB");
        B ("rev_on", "Reverb On", false);
        F ("rev_size", "Reverb Size", 0, 1, 0.6f);
        F ("rev_decay", "Reverb Decay", 0.2f, 12, 2.5f, 2.5f, "s");
        F ("rev_damp", "Reverb Damp", 0, 1, 0.5f);
        F ("rev_mix", "Reverb Mix", 0, 1, 0.2f);
        B ("eq_on", "EQ On", false);
        F ("eq_low", "EQ Low", -15, 15, 0, -1.0f, "dB");
        F ("eq_mid", "EQ Mid", -15, 15, 0, -1.0f, "dB");
        F ("eq_mfreq", "EQ Mid Freq", 200, 8000, 1200, 1200, "Hz");
        F ("eq_high", "EQ High", -15, 15, 0, -1.0f, "dB");
        B ("ott_on", "OTT On", false);
        F ("ott_depth", "OTT Depth", 0, 1, 0.5f);
        F ("ott_time", "OTT Time", 0, 1, 0.5f);
        B ("pump_on", "Pump On", false);
        C ("pump_div", "Pump Rate", pumpDivs(), 2);
        F ("pump_depth", "Pump Depth", 0, 1, 0.7f);
        F ("pump_curve", "Pump Release", 0, 1, 0.5f);
        F ("vol", "Volume", -36, 6, -3, -1.0f, "dB");
        B ("clip", "Soft Clip", true);
        return v;
    }();
    return s;
}

inline const Spec* findSpec (const juce::String& id)
{
    for (auto& s : specs()) if (s.id == id) return &s;
    return nullptr;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (auto& s : specs())
    {
        const juce::ParameterID pid { s.id, 1 };
        switch (s.kind)
        {
            case Kind::Float:
            {
                juce::NormalisableRange<float> r (s.lo, s.hi);
                if (s.skewCentre > 0.0f) r.setSkewForCentre (s.skewCentre);
                auto attrs = juce::AudioParameterFloatAttributes().withLabel (s.unit);
                layout.add (std::make_unique<juce::AudioParameterFloat> (pid, s.name, r, s.def, attrs));
                break;
            }
            case Kind::Int:    layout.add (std::make_unique<juce::AudioParameterInt> (pid, s.name, (int) s.lo, (int) s.hi, (int) s.def)); break;
            case Kind::Bool:   layout.add (std::make_unique<juce::AudioParameterBool> (pid, s.name, s.def > 0.5f)); break;
            case Kind::Choice: layout.add (std::make_unique<juce::AudioParameterChoice> (pid, s.name, *s.choices, (int) s.def)); break;
        }
    }
    return layout;
}

// Knob id -> modulation destination index (for drag & drop modulation and mod rings)
inline int destForParam (const juce::String& id)
{
    static const std::vector<std::pair<juce::String, int>> map {
        { "a_pos", 1 }, { "a_semi", 2 }, { "a_level", 3 }, { "a_det", 4 }, { "a_warp", 5 }, { "a_pan", 6 },
        { "b_pos", 7 }, { "b_semi", 8 }, { "b_level", 9 }, { "b_det", 10 }, { "b_warp", 11 }, { "b_pan", 12 },
        { "c_pos", 13 }, { "c_semi", 14 }, { "c_level", 15 }, { "c_det", 16 }, { "c_warp", 17 }, { "c_pan", 18 },
        { "flt_cut", 20 }, { "flt_res", 21 }, { "flt_drive", 22 }, { "f2_cut", 23 }, { "f2_res", 24 },
        { "sub_level", 25 }, { "noise_level", 26 }, { "l1_rate", 29 }, { "l2_rate", 30 }, { "l3_rate", 31 }, { "l4_rate", 32 },
        { "a_width", 33 }, { "b_width", 33 }, { "c_width", 33 }, { "flt_env", 34 },
        { "dist_drive", 35 }, { "dist_mix", 36 }, { "hyp_mix", 37 }, { "cho_mix", 38 }, { "pha_mix", 39 }, { "fla_mix", 40 },
        { "dly_mix", 41 }, { "dly_fb", 42 }, { "rev_mix", 43 }, { "rev_size", 44 }, { "pump_depth", 45 } };
    for (auto& kv : map) if (kv.first == id) return kv.second;
    return 0;
}

} // namespace lumen::synth
