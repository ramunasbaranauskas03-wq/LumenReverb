#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// =====================================================================================
// Lumen Synth parameter table. Everything (layout, presets, GENERATE / MUTATE) is driven
// from this single list so that ids, ranges and defaults can never drift apart.
// =====================================================================================
namespace lumen::synth
{

inline const juce::StringArray& tableNames()
{ static const juce::StringArray s { "Basic Shapes", "Saw Stack", "PWM", "Vox Formant", "Hard Sync", "FM Growl", "Phase Res", "Organ", "Fold" }; return s; }
inline const juce::StringArray& warpNames()   { static const juce::StringArray s { "Off", "Sync", "Bend", "Squeeze", "FM <" }; return s; }
inline const juce::StringArray& subShapes()   { static const juce::StringArray s { "Sine", "Triangle", "Saw", "Square" }; return s; }
inline const juce::StringArray& subOcts()     { static const juce::StringArray s { "-2 Oct", "-1 Oct", "0 Oct" }; return s; }
inline const juce::StringArray& filterTypes() { static const juce::StringArray s { "Ladder LP24", "LP12", "LP24", "HP12", "HP24", "Band", "Notch" }; return s; }
inline const juce::StringArray& lfoShapes()   { static const juce::StringArray s { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Pump", "S&H", "Smooth Rnd" }; return s; }
inline const juce::StringArray& divNames()    { static const juce::StringArray s { "4/1", "2/1", "1/1", "1/2", "1/2D", "1/2T", "1/4", "1/4D", "1/4T", "1/8", "1/8D", "1/8T", "1/16", "1/16D", "1/16T", "1/32" }; return s; }
inline double divBeats (int i)
{
    static const double b[] = { 16, 8, 4, 2, 3, 4.0 / 3.0, 1, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0, 0.25, 0.375, 1.0 / 6.0, 0.125 };
    return b[juce::jlimit (0, 15, i)];
}
inline const juce::StringArray& modSources()
{ static const juce::StringArray s { "Off", "LFO 1", "LFO 2", "Env 2", "Env 3", "Velocity", "Mod Wheel", "Aftertouch", "Key Track", "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Note Random" }; return s; }
inline const juce::StringArray& modDests()
{ static const juce::StringArray s { "Off", "A Position", "A Pitch", "A Level", "A Detune", "A Warp", "B Position", "B Pitch", "B Level", "B Detune", "B Warp",
                                     "Osc Pitch", "Cutoff", "Resonance", "Filter Drive", "Sub Level", "Noise Level", "Pan", "Volume", "LFO 1 Rate", "LFO 2 Rate", "Unison Width" }; return s; }
inline const juce::StringArray& distModes()   { static const juce::StringArray s { "Warm", "Hard", "Fold", "Crush" }; return s; }
inline const juce::StringArray& pumpDivs()    { static const juce::StringArray s { "1/1", "1/2", "1/4", "1/8", "1/16" }; return s; }
inline const juce::StringArray& categories()  { static const juce::StringArray s { "Bass", "Lead", "Pluck", "Pad", "Keys", "FX" }; return s; }

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

        for (auto o : { juce::String ("a"), juce::String ("b") })
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

        for (int e = 1; e <= 3; ++e)
        {
            const auto p = "e" + juce::String (e) + "_";
            const auto N = "Env " + juce::String (e) + " ";
            F (p + "a", N + "Attack", 0, 10, e == 1 ? 0.002f : 0.001f, 0.25f, "s");
            F (p + "d", N + "Decay", 0, 10, 0.4f, 0.6f, "s");
            F (p + "s", N + "Sustain", 0, 1, e == 1 ? 1.0f : 0.0f);
            F (p + "r", N + "Release", 0, 15, 0.12f, 0.8f, "s");
        }
        for (int l = 1; l <= 2; ++l)
        {
            const auto p = "l" + juce::String (l) + "_";
            const auto N = "LFO " + juce::String (l) + " ";
            C (p + "shape", N + "Shape", lfoShapes(), 0);
            F (p + "rate", N + "Rate", 0.02f, 40, 2, 3, "Hz");
            B (p + "sync", N + "Sync", true);
            C (p + "div", N + "Division", divNames(), 6);
            B (p + "retrig", N + "Retrigger", true);
        }
        for (int m = 1; m <= 8; ++m)
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

        B ("dist_on", "Dist On", false);
        C ("dist_mode", "Dist Mode", distModes(), 0);
        F ("dist_drive", "Dist Drive", 0, 1, 0.3f);
        F ("dist_mix", "Dist Mix", 0, 1, 1);
        B ("ott_on", "OTT On", false);
        F ("ott_depth", "OTT Depth", 0, 1, 0.5f);
        F ("ott_time", "OTT Time", 0, 1, 0.5f);
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
        B ("rev_on", "Reverb On", false);
        F ("rev_size", "Reverb Size", 0, 1, 0.6f);
        F ("rev_decay", "Reverb Decay", 0.2f, 12, 2.5f, 2.5f, "s");
        F ("rev_damp", "Reverb Damp", 0, 1, 0.5f);
        F ("rev_mix", "Reverb Mix", 0, 1, 0.2f);
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

} // namespace lumen::synth
