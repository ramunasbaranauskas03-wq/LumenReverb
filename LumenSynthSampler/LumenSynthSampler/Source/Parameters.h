#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Engine.h"

namespace ssp
{
inline juce::String L (int layer, const char* w) { return juce::String::charToString ((juce::juce_wchar) ('a' + layer)) + "_" + w; }
inline juce::String lfo (int l, const char* w) { return "lfo" + juce::String (l + 1) + "_" + w; }
inline juce::String mod (int s, const char* w) { return "mod" + juce::String (s + 1) + "_" + w; }
inline juce::String env (int e, const char* w) { static const char* p[] { "amp", "fenv", "menv" }; return juce::String (p[e]) + "_" + w; }
inline juce::String macro (int m) { return "macro" + juce::String (m + 1); }

inline const juce::StringArray& modeNames()   { static const juce::StringArray s { "SYNTH (keeps length)", "CLASSIC (repitch)", "STRETCH (grains)" }; return s; }
inline const juce::StringArray& filterTypes() { static const juce::StringArray s { "Ladder LP24", "LP12", "LP24", "HP12", "HP24", "Band", "Notch", "Comb +", "Comb -", "Formant", "Phaser", "MS-20", "Diode 303", "Juno LP24" }; return s; }
inline const juce::StringArray& lfoShapes()   { static const juce::StringArray s { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H", "Smooth Rnd" }; return s; }
inline const juce::StringArray& divNames()    { static const juce::StringArray s { "4/1", "2/1", "1/1", "1/2", "1/2D", "1/2T", "1/4", "1/4D", "1/4T", "1/8", "1/8D", "1/8T", "1/16", "1/16D", "1/16T", "1/32" }; return s; }
inline double divBeats (int i) { static const double b[] = { 16, 8, 4, 2, 3, 4.0 / 3.0, 1, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0, 0.25, 0.375, 1.0 / 6.0, 0.125 }; return b[juce::jlimit (0, 15, i)]; }
inline const juce::StringArray& srcNames()    { static const juce::StringArray s { "Off", "LFO 1", "LFO 2", "Filter Env", "Mod Env", "Velocity", "Key Track", "Mod Wheel", "Aftertouch", "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Note Random" }; return s; }
inline const juce::StringArray& dstNames()    { static const juce::StringArray s { "Off", "Pitch", "A Level", "B Level", "C Level", "A Position", "B Position", "C Position", "Speed", "Cutoff", "Resonance", "Filter Drive", "Sub Level", "Noise Level", "Pan", "Volume", "Unison Detune", "LFO 1 Rate", "LFO 2 Rate" }; return s; }
inline const juce::StringArray& arpModes()    { static const juce::StringArray s { "Up", "Down", "Up/Down", "Random", "As Played", "Chord" }; return s; }
inline const juce::StringArray& arpRates()    { static const juce::StringArray s { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }; return s; }
inline double arpBeats (int i) { static const double b[] = { 1, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 }; return b[juce::jlimit (0, 5, i)]; }
inline const juce::StringArray& chordNames()  { static const juce::StringArray s { "Off", "Major", "Minor", "Maj7", "Min7", "Sus2", "Sus4", "Power", "Octaves", "Add9", "Min9" }; return s; }
inline const juce::StringArray& distModes()   { static const juce::StringArray s { "Warm", "Hard", "Fold", "Crush" }; return s; }
inline const juce::StringArray& pumpDivs()    { static const juce::StringArray s { "1/1", "1/2", "1/4", "1/8", "1/16" }; return s; }
inline const juce::StringArray& fxIds()       { static const juce::StringArray s { "hyp", "dist", "fla", "pha", "cho", "dly", "cmp", "rev", "eq", "ott", "pump" }; return s; }
inline const juce::StringArray& fxNames()     { static const juce::StringArray s { "HYPER/DIM", "DISTORTION", "FLANGER", "PHASER", "CHORUS", "DELAY", "COMPRESSOR", "REVERB", "EQ", "OTT", "PUMP" }; return s; }

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const String& id) { return ParameterID { id, 1 }; };
    auto F = [&] (const String& id, const String& nm, float lo, float hi, float d, float centre = -1.0f, const String& unit = {})
    {
        NormalisableRange<float> r (lo, hi); if (centre > lo && centre < hi) r.setSkewForCentre (centre);
        auto at = AudioParameterFloatAttributes().withLabel (unit);
        if (unit.isEmpty() && lo >= 0.0f && hi <= 1.0f) at = at.withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; });
        else if (unit.isEmpty() && lo >= -1.0f && hi <= 1.0f) at = at.withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + String (roundToInt (v * 100.0f)) + " %"; });
        else at = at.withStringFromValueFunction ([] (float v, int) { return std::abs (v) >= 100 ? String (roundToInt (v)) : String (v, std::abs (v) < 10 ? 2 : 1); });
        l.add (std::make_unique<AudioParameterFloat> (pid (id), nm, r, d, at));
    };
    auto B = [&] (const String& id, const String& nm, bool d) { l.add (std::make_unique<AudioParameterBool> (pid (id), nm, d)); };
    auto C = [&] (const String& id, const String& nm, const StringArray& ch, int d) { l.add (std::make_unique<AudioParameterChoice> (pid (id), nm, ch, d)); };
    auto I = [&] (const String& id, const String& nm, int lo, int hi, int d) { l.add (std::make_unique<AudioParameterInt> (pid (id), nm, lo, hi, d)); };

    for (int k = 0; k < lumen::ss::kLayers; ++k)
    {
        const String N = String ("Layer ") + String::charToString ((juce_wchar) ('A' + k)) + " ";
        B (L (k, "on"), N + "On", k == 0);
        C (L (k, "mode"), N + "Mode", modeNames(), 0);
        l.add (std::make_unique<AudioParameterInt> (pid (L (k, "root")), N + "Root", 0, 127, 60, AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return MidiMessage::getMidiNoteName (v, true, true, 3); })));
        I (L (k, "oct"), N + "Octave", -3, 3, 0);
        I (L (k, "semi"), N + "Semitone", -12, 12, 0);
        F (L (k, "fine"), N + "Fine", -100, 100, 0, -1, "ct");
        F (L (k, "level"), N + "Level", -36, 6, 0, -1, "dB");
        F (L (k, "pan"), N + "Pan", -1, 1, 0);
        F (L (k, "start"), N + "Start / Position", 0, 1, 0);
        F (L (k, "speed"), N + "Speed", 0, 2, 1, 1.0f, "x");
        B (L (k, "loop"), N + "Loop", false);
        F (L (k, "lstart"), N + "Loop Start", 0, 1, 0.3f);
        F (L (k, "lend"), N + "Loop End", 0, 1, 0.8f);
        F (L (k, "xfade"), N + "Loop X-Fade", 0, 500, 40, 80, "ms");
        B (L (k, "exact"), N + "Exact at Root", true);
        B (L (k, "rev"), N + "Reverse", false);
        F (L (k, "trans"), N + "Keep Transient", 0, 80, 0, 20, "ms");
    }
    // sub + noise
    B ("sub_on", "Sub On", false); C ("sub_shape", "Sub Shape", StringArray { "Sine", "Triangle", "Square" }, 0);
    C ("sub_oct", "Sub Octave", StringArray { "0 Oct", "-1 Oct", "-2 Oct" }, 1); F ("sub_level", "Sub Level", 0, 1, 0.5f);
    B ("noise_on", "Noise On", false); F ("noise_level", "Noise Level", 0, 1, 0.2f); F ("noise_color", "Noise Color", 0, 1, 0.5f);
    // filter
    B ("f_on", "Filter On", false); C ("f_type", "Filter Type", filterTypes(), 2);
    F ("cutoff", "Cutoff", 20, 20000, 20000, 1000, "Hz"); F ("res", "Resonance", 0, 1, 0.1f); F ("drive", "Filter Drive", 0, 1, 0);
    F ("f_env", "Filter Env Amount", -1, 1, 0); F ("key_track", "Key Track", 0, 1, 0);
    // envelopes
    const float defs[3][4] { { 0, 0.4f, 1, 0.02f }, { 0.001f, 0.5f, 0, 0.3f }, { 0.001f, 0.5f, 0, 0.3f } };
    const char* en[3] { "Amp", "Filter Env", "Mod Env" };
    for (int e = 0; e < 3; ++e)
    {
        F (env (e, "a"), String (en[e]) + " Attack", 0, 10, defs[e][0], 0.5f, "s");
        F (env (e, "d"), String (en[e]) + " Decay", 0.001f, 10, defs[e][1], 0.8f, "s");
        F (env (e, "s"), String (en[e]) + " Sustain", 0, 1, defs[e][2]);
        F (env (e, "r"), String (en[e]) + " Release", 0.0005f, 10, defs[e][3], 0.8f, "s");
    }
    for (int k = 0; k < 2; ++k)
    {
        const String N = "LFO " + String (k + 1) + " ";
        C (lfo (k, "shape"), N + "Shape", lfoShapes(), 0); F (lfo (k, "rate"), N + "Rate", 0.02f, 30, 2, 3, "Hz");
        B (lfo (k, "sync"), N + "Sync", true); C (lfo (k, "div"), N + "Division", divNames(), 6); B (lfo (k, "retrig"), N + "Retrigger", true);
    }
    for (int s = 0; s < lumen::ss::kSlots; ++s)
    {
        const String N = "Mod " + String (s + 1) + " ";
        C (mod (s, "src"), N + "Source", srcNames(), 0); C (mod (s, "dst"), N + "Destination", dstNames(), 0); F (mod (s, "amt"), N + "Amount", -1, 1, 0);
    }
    for (int m = 0; m < 4; ++m) F (macro (m), "Macro " + String (m + 1), 0, 1, 0);
    // voice
    I ("poly", "Voices", 1, 32, 16); B ("legato", "Legato", false); F ("glide", "Glide", 0, 2, 0, 0.3f, "s"); I ("bend", "Bend Range", 0, 24, 2);
    F ("vel_sens", "Velocity Sens", 0, 1, 0);
    I ("uni", "Unison Voices", 1, lumen::ss::kMaxUni, 1); F ("uni_det", "Unison Detune", 0, 100, 15, -1, "ct"); F ("uni_spread", "Unison Spread", 0, 1, 0.7f);
    // arp
    B ("arp_on", "Arp On", false); C ("arp_mode", "Arp Mode", arpModes(), 0); C ("arp_rate", "Arp Rate", arpRates(), 3);
    I ("arp_oct", "Arp Octaves", 1, 4, 1); F ("arp_gate", "Arp Gate", 0.05f, 1, 0.6f); F ("arp_swing", "Arp Swing", 0, 0.75f, 0); C ("arp_chord", "Chord", chordNames(), 0);
    // FX (Lumen Synth rack)
    B ("hyp_on", "Hyper On", false); F ("hyp_detune", "Hyper Detune", 0, 1, 0.35f); F ("hyp_dim", "Dimension", 0, 1, 0.5f); F ("hyp_mix", "Hyper Mix", 0, 1, 0.4f);
    B ("dist_on", "Dist On", false); C ("dist_mode", "Dist Mode", distModes(), 0); F ("dist_drive", "Dist Drive", 0, 1, 0.3f); F ("dist_mix", "Dist Mix", 0, 1, 1);
    B ("fla_on", "Flanger On", false); F ("fla_rate", "Flanger Rate", 0.02f, 5, 0.2f, 0.6f, "Hz"); F ("fla_depth", "Flanger Depth", 0, 1, 0.6f); F ("fla_fb", "Flanger Feedback", -0.95f, 0.95f, 0.5f); F ("fla_mix", "Flanger Mix", 0, 1, 0.5f);
    B ("pha_on", "Phaser On", false); F ("pha_rate", "Phaser Rate", 0.02f, 5, 0.3f, 0.6f, "Hz"); F ("pha_depth", "Phaser Depth", 0, 1, 0.7f); F ("pha_fb", "Phaser Feedback", 0, 0.95f, 0.5f); F ("pha_mix", "Phaser Mix", 0, 1, 0.5f);
    B ("cho_on", "Chorus On", false); F ("cho_rate", "Chorus Rate", 0.05f, 5, 0.6f, 0.8f, "Hz"); F ("cho_depth", "Chorus Depth", 0, 1, 0.5f); F ("cho_mix", "Chorus Mix", 0, 1, 0.4f);
    B ("dly_on", "Delay On", false); C ("dly_div", "Delay Time", divNames(), 10); F ("dly_fb", "Delay Feedback", 0, 0.95f, 0.35f); F ("dly_tone", "Delay Tone", 0, 1, 0.6f); F ("dly_mix", "Delay Mix", 0, 1, 0.2f); B ("dly_ping", "Ping Pong", true);
    B ("cmp_on", "Comp On", false); F ("cmp_thresh", "Comp Threshold", -48, 0, -18, -1, "dB"); F ("cmp_ratio", "Comp Ratio", 1, 20, 4, 4); F ("cmp_attack", "Comp Attack", 0.1f, 100, 5, 10, "ms"); F ("cmp_release", "Comp Release", 10, 1000, 120, 150, "ms"); F ("cmp_gain", "Comp Gain", 0, 24, 3, -1, "dB");
    B ("rev_on", "Reverb On", false); F ("rev_size", "Reverb Size", 0, 1, 0.6f); F ("rev_decay", "Reverb Decay", 0.2f, 12, 2.5f, 2.5f, "s"); F ("rev_damp", "Reverb Damp", 0, 1, 0.5f); F ("rev_mix", "Reverb Mix", 0, 1, 0.2f);
    B ("eq_on", "EQ On", false); F ("eq_low", "EQ Low", -15, 15, 0, -1, "dB"); F ("eq_mid", "EQ Mid", -15, 15, 0, -1, "dB"); F ("eq_mfreq", "EQ Mid Freq", 200, 8000, 1200, 1200, "Hz"); F ("eq_high", "EQ High", -15, 15, 0, -1, "dB");
    B ("ott_on", "OTT On", false); F ("ott_depth", "OTT Depth", 0, 1, 0.5f); F ("ott_time", "OTT Time", 0, 1, 0.5f);
    B ("pump_on", "Pump On", false); C ("pump_div", "Pump Rate", pumpDivs(), 2); F ("pump_depth", "Pump Depth", 0, 1, 0.7f); F ("pump_curve", "Pump Release", 0, 1, 0.5f);
    F ("vol", "Volume", -36, 6, 0, -1, "dB"); B ("clip", "Soft Clip", false);
    return l;
}
} // namespace ssp
