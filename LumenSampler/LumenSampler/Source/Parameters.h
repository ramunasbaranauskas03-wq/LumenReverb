#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/SamplerEngine.h"
#include "dsp/Analysis.h"

namespace IDs
{
    inline const juce::String mode = "mode", root = "root", tune = "tune", fine = "fine", warp = "warp", sampleBpm = "sample_bpm",
        start = "start", end = "end", reverse = "reverse", loop = "loop", loopMode = "loop_mode", loopStart = "loop_start", loopEnd = "loop_end", xfade = "xfade",
        attack = "attack", decay = "decay", sustain = "sustain", release = "release", velSens = "vel_sens",
        fOn = "filter_on", fType = "filter_type", cutoff = "cutoff", res = "resonance", fEnv = "filter_env", fA = "f_attack", fD = "f_decay", fS = "f_sustain", fR = "f_release",
        keyTrack = "key_track", velCut = "vel_cutoff",
        poly = "voices", legato = "legato", glide = "glide", bend = "bend_range", volume = "volume", pan = "pan", width = "width",
        sliceMode = "slice_mode", sliceSens = "slice_sens", sliceGrid = "slice_grid",
        pEnvAmt = "penv_amt", pEnvDecay = "penv_decay", uniVoices = "uni_voices", uniDetune = "uni_detune", uniSpread = "uni_spread",
        chord = "chord", humStart = "hum_start", humPitch = "hum_pitch", humVol = "hum_vol", fadeIn = "fade_in", fadeOut = "fade_out",
        choke = "choke", drive = "drive", vintage = "vintage", normalize = "normalize";
    inline juce::String lfo (int l, const char* w) { return "lfo" + juce::String (l + 1) + "_" + w; }
}
inline juce::StringArray chordNames() { return { "Off", "Major", "Minor", "Sus4", "Maj7", "Min7", "Dom7", "Add9", "Power (5th)", "Octave", "Min9 (house)" }; }
inline std::vector<int> chordIntervals (int i)
{
    switch (i) { case 1: return { 0, 4, 7 }; case 2: return { 0, 3, 7 }; case 3: return { 0, 5, 7 }; case 4: return { 0, 4, 7, 11 }; case 5: return { 0, 3, 7, 10 };
                 case 6: return { 0, 4, 7, 10 }; case 7: return { 0, 4, 7, 14 }; case 8: return { 0, 7, 12 }; case 9: return { 0, 12 }; case 10: return { 0, 3, 7, 10, 14 }; default: return { 0 }; }
}
inline juce::StringArray divNames() { return { "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }; }
inline double divBeats (int i) { static const double b[] { 16, 8, 4, 2, 1, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 }; return b[juce::jlimit (0, 9, i)]; }

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const String& id) { return ParameterID { id, 1 }; };
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v * 100.0f, 1) + "%"; });
    auto sec = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 1 ? String (roundToInt (v * 1000)) + " ms" : String (v, 2) + " s"; });
    auto db  = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v <= -59.9f ? String ("-inf") : String (v, 1) + " dB"; });
    auto c = [&] (const String& id, const String& nm, StringArray ch, int d) { l.add (std::make_unique<AudioParameterChoice> (pid (id), nm, ch, d)); };
    auto b = [&] (const String& id, const String& nm, bool d) { l.add (std::make_unique<AudioParameterBool> (pid (id), nm, d)); };
    auto f = [&] (const String& id, const String& nm, float lo, float hi, float d, AudioParameterFloatAttributes a, float centre = -1)
    { NormalisableRange<float> r (lo, hi); if (centre > lo) r.setSkewForCentre (centre); l.add (std::make_unique<AudioParameterFloat> (pid (id), nm, r, d, a)); };
    auto in = [&] (const String& id, const String& nm, int lo, int hi, int d) { l.add (std::make_unique<AudioParameterInt> (pid (id), nm, lo, hi, d)); };

    c (IDs::mode, "Mode", StringArray { "Classic (keys)", "One-Shot", "Slice" }, 0);
    l.add (std::make_unique<AudioParameterInt> (pid (IDs::root), "Root Note", 0, 127, 60, AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return MidiMessage::getMidiNoteName (v, true, true, 3); })));
    f (IDs::tune, "Tune", -24.0f, 24.0f, 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + String (v, 2) + " st"; }));
    f (IDs::fine, "Fine", -100.0f, 100.0f, 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " ct"; }));
    c (IDs::warp, "Warp", StringArray { "Off (repitch)", "Key-lock", "Tempo sync" }, 0);
    f (IDs::sampleBpm, "Sample BPM", 40.0f, 240.0f, 120.0f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " bpm"; }));
    f (IDs::start, "Start", 0.0f, 1.0f, 0.0f, pct);
    f (IDs::end, "End", 0.0f, 1.0f, 1.0f, pct);
    b (IDs::reverse, "Reverse", false);
    b (IDs::loop, "Loop", false);
    c (IDs::loopMode, "Loop Mode", StringArray { "Forward", "Ping-Pong" }, 0);
    f (IDs::loopStart, "Loop Start", 0.0f, 1.0f, 0.0f, pct);
    f (IDs::loopEnd, "Loop End", 0.0f, 1.0f, 1.0f, pct);
    f (IDs::xfade, "Loop Crossfade", 0.0f, 1000.0f, 10.0f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ms"; }), 100.0f);
    f (IDs::attack, "Attack", 0.0f, 10.0f, 0.0f, sec, 0.5f);
    f (IDs::decay, "Decay", 0.001f, 10.0f, 0.3f, sec, 0.8f);
    f (IDs::sustain, "Sustain", 0.0f, 1.0f, 1.0f, pct);
    f (IDs::release, "Release", 0.0f, 10.0f, 0.01f, sec, 0.8f);
    f (IDs::velSens, "Velocity > Volume", 0.0f, 1.0f, 0.0f, pct);
    b (IDs::fOn, "Filter On", false);
    c (IDs::fType, "Filter Type", StringArray { "LP 12", "LP 24", "HP 12", "HP 24", "Band", "Notch" }, 1);
    f (IDs::cutoff, "Cutoff", 20.0f, 20000.0f, 20000.0f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v >= 1000 ? String (v / 1000, 2) + " kHz" : String (roundToInt (v)) + " Hz"; }), 1000.0f);
    f (IDs::res, "Resonance", 0.0f, 1.0f, 0.1f, pct);
    f (IDs::fEnv, "Filter Env", -1.0f, 1.0f, 0.0f, pct);
    f (IDs::fA, "Filter Attack", 0.0f, 10.0f, 0.0f, sec, 0.5f);
    f (IDs::fD, "Filter Decay", 0.001f, 10.0f, 0.3f, sec, 0.8f);
    f (IDs::fS, "Filter Sustain", 0.0f, 1.0f, 1.0f, pct);
    f (IDs::fR, "Filter Release", 0.0f, 10.0f, 0.1f, sec, 0.8f);
    f (IDs::keyTrack, "Key Track", 0.0f, 1.0f, 0.0f, pct);
    f (IDs::velCut, "Velocity > Cutoff", 0.0f, 1.0f, 0.0f, pct);
    for (int i = 0; i < 2; ++i)
    {
        const String N = "LFO " + String (i + 1) + " ";
        c (IDs::lfo (i, "shape"), N + "Shape", StringArray { "Sine", "Triangle", "Saw", "Square", "Random" }, 0);
        f (IDs::lfo (i, "rate"), N + "Rate", 0.05f, 30.0f, 5.0f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " Hz"; }), 3.0f);
        b (IDs::lfo (i, "sync"), N + "Sync", false);
        c (IDs::lfo (i, "div"), N + "Division", divNames(), 4);
        f (IDs::lfo (i, "pitch"), N + "> Pitch", -2.0f, 2.0f, 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " st"; }));
        f (IDs::lfo (i, "filter"), N + "> Filter", -4.0f, 4.0f, 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " oct"; }));
        f (IDs::lfo (i, "vol"), N + "> Volume", 0.0f, 1.0f, 0.0f, pct);
    }
    in (IDs::poly, "Voices", 1, 32, 16);
    b (IDs::legato, "Legato", false);
    f (IDs::glide, "Glide", 0.0f, 2.0f, 0.0f, sec, 0.3f);
    in (IDs::bend, "Bend Range", 0, 24, 2);
    f (IDs::volume, "Volume", -60.0f, 12.0f, 0.0f, db);
    f (IDs::pan, "Pan", -1.0f, 1.0f, 0.0f, pct);
    f (IDs::width, "Width", 0.0f, 2.0f, 1.0f, pct);
    c (IDs::sliceMode, "Slice By", StringArray { "Transients", "Grid" }, 0);
    f (IDs::sliceSens, "Slice Sensitivity", 0.0f, 1.0f, 0.6f, pct);
    c (IDs::sliceGrid, "Slice Grid", StringArray { "4", "8", "16", "32", "64" }, 2);
    // ---- extras (all neutral by default)
    f (IDs::pEnvAmt, "Pitch Env Amount", -48.0f, 48.0f, 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " st"; }));
    f (IDs::pEnvDecay, "Pitch Env Decay", 0.005f, 5.0f, 0.15f, sec, 0.3f);
    in (IDs::uniVoices, "Unison Voices", 1, 8, 1);
    f (IDs::uniDetune, "Unison Detune", 0.0f, 100.0f, 15.0f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " ct"; }));
    f (IDs::uniSpread, "Unison Spread", 0.0f, 1.0f, 0.6f, pct);
    c (IDs::chord, "Chord", chordNames(), 0);
    f (IDs::humStart, "Humanize Start", 0.0f, 50.0f, 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " ms"; }));
    f (IDs::humPitch, "Humanize Pitch", 0.0f, 50.0f, 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " ct"; }));
    f (IDs::humVol, "Humanize Volume", 0.0f, 1.0f, 0.0f, pct);
    f (IDs::fadeIn, "Fade In", 0.0f, 500.0f, 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " ms"; }), 50.0f);
    f (IDs::fadeOut, "Fade Out", 0.0f, 500.0f, 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " ms"; }), 50.0f);
    b (IDs::choke, "Slice Choke", false);
    f (IDs::drive, "Drive", 0.0f, 1.0f, 0.0f, pct);
    c (IDs::vintage, "Vintage", StringArray { "Clean", "SP-1200", "MPC60", "S950", "Lo-Fi 8-bit" }, 0);
    b (IDs::normalize, "Normalize", false);
    return l;
}
