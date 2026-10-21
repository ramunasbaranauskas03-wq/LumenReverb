#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/MasterChain.h"

namespace IDs
{
    inline const juce::String inGain = "in_gain", outGain = "out_gain", lowLat = "low_latency", genre = "genre", platform = "platform", customLufs = "custom_lufs",
        bypass = "bypass", match = "match", mono = "mono_check",
        eqOn = "eq_on", eqMode = "eq_mode",
        shOn = "sh_on", shAmount = "sh_amount", shTame = "sh_tame", shBoost = "sh_boost", shSpeed = "sh_speed", shLow = "sh_low", shHigh = "sh_high",
        bassOn = "bass_on", bassFreq = "bass_freq", bassPunch = "bass_punch", bassSustain = "bass_sustain", bassGain = "bass_gain", bassMono = "bass_mono",
        mbOn = "mb_on", mbX1 = "mb_x1", mbX2 = "mb_x2", mbX3 = "mb_x3", mbAttack = "mb_attack", mbRelease = "mb_release", mbMix = "mb_mix",
        glueOn = "glue_on", glueThr = "glue_thr", glueRatio = "glue_ratio", glueAttack = "glue_attack", glueRelease = "glue_release", glueMakeup = "glue_makeup", glueMix = "glue_mix", glueSc = "glue_sc",
        satOn = "sat_on", satMode = "sat_mode", satDrive = "sat_drive", satTone = "sat_tone", satExciter = "sat_exciter", satMix = "sat_mix",
        imgOn = "img_on", imgX1 = "img_x1", imgX2 = "img_x2", imgLow = "img_low", imgMid = "img_mid", imgHigh = "img_high",
        clipOn = "clip_on", clipDrive = "clip_drive", clipSoft = "clip_soft",
        maxOn = "max_on", maxDrive = "max_drive", maxCeiling = "max_ceiling", maxRelease = "max_release", maxMode = "max_mode", maxLink = "max_link", maxDelta = "max_delta",
        dither = "dither";
    inline juce::String eq (int b, const char* w) { return "eq" + juce::String (b + 1) + "_" + w; }
    inline juce::String mb (int b, const char* w) { return "mb" + juce::String (b + 1) + "_" + w; }
}

inline juce::StringArray genreNames()
{
    juce::StringArray s; for (auto& g : lumen::master::genres()) s.add (g.name);
    s.add ("Reference track");
    return s;
}
inline juce::StringArray platformNames() { juce::StringArray s; for (auto& p : lumen::master::platforms()) s.add (p.name); return s; }
inline const float* glueRatios()   { static const float v[] { 1.5f, 2.0f, 4.0f, 10.0f }; return v; }
inline const float* glueAttacks()  { static const float v[] { 0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f }; return v; }
inline const float* glueReleases() { static const float v[] { 0.1f, 0.3f, 0.6f, 1.2f, -1.0f }; return v; }
inline const float* glueScs()      { static const float v[] { 0.0f, 60.0f, 90.0f, 150.0f }; return v; }
inline const char* moduleName (int m) { static const char* n[] { "EQ", "SMART BALANCE", "BASS", "MULTIBAND", "GLUE", "SATURATE", "IMAGER", "CLIPPER", "MAXIMIZER", "DITHER" }; return n[m]; }
inline juce::String moduleOnId (int m)
{
    using namespace IDs;
    static const juce::String* ids[] { &eqOn, &shOn, &bassOn, &mbOn, &glueOn, &satOn, &imgOn, &clipOn, &maxOn, &dither };
    return *ids[m];
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const String& id) { return ParameterID { id, 1 }; };
    auto fmt = [] (int dec, const char* unit) { return AudioParameterFloatAttributes().withStringFromValueFunction ([dec, unit] (float v, int) { return String (v, dec) + unit; }); };
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; });
    auto bip = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + String (roundToInt (v * 100.0f)) + " %"; });
    auto hz = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v >= 1000 ? String (v / 1000.0f, v >= 10000 ? 1 : 2) + " kHz" : String (roundToInt (v)) + " Hz"; });
    auto db = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " dB"; });
    auto ms = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 10 ? String (v, 1) + " ms" : String (roundToInt (v)) + " ms"; });
    auto f = [&] (const String& id, const String& nm, float lo, float hi, float d, AudioParameterFloatAttributes a, float centre = -1)
    { NormalisableRange<float> r (lo, hi); if (centre > lo && centre < hi) r.setSkewForCentre (centre); l.add (std::make_unique<AudioParameterFloat> (pid (id), nm, r, d, a)); };
    auto b = [&] (const String& id, const String& nm, bool d) { l.add (std::make_unique<AudioParameterBool> (pid (id), nm, d)); };
    auto c = [&] (const String& id, const String& nm, StringArray ch, int d) { l.add (std::make_unique<AudioParameterChoice> (pid (id), nm, ch, d)); };

    // ---- global
    f (IDs::inGain, "Input Gain", -12.0f, 12.0f, 0.0f, db);
    f (IDs::outGain, "Output Gain", -12.0f, 12.0f, 0.0f, db);
    b (IDs::lowLat, "Low Latency", false);
    c (IDs::genre, "Target", genreNames(), 0);
    c (IDs::platform, "Destination", platformNames(), 0);
    f (IDs::customLufs, "Custom LUFS", -20.0f, -4.0f, -9.0f, fmt (1, " LUFS"));
    b (IDs::bypass, "Bypass (compare)", false);
    b (IDs::match, "Level Match", true);
    b (IDs::mono, "Mono Check", false);

    // ---- EQ
    b (IDs::eqOn, "EQ On", false);
    c (IDs::eqMode, "EQ Phase", StringArray { "Natural", "Linear" }, 1);
    static const float dfreq[] { 60, 180, 500, 1800, 4500, 11000 };
    static const int dtype[] { 1, 0, 0, 0, 0, 2 };
    for (int i = 0; i < lumen::master::kEqBands; ++i)
    {
        const String N = "EQ " + String (i + 1) + " ";
        b (IDs::eq (i, "on"), N + "On", true);
        c (IDs::eq (i, "type"), N + "Type", StringArray { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Tilt" }, dtype[i]);
        f (IDs::eq (i, "freq"), N + "Freq", 20.0f, 20000.0f, dfreq[i], hz, 1000.0f);
        f (IDs::eq (i, "gain"), N + "Gain", -12.0f, 12.0f, 0.0f, db);
        f (IDs::eq (i, "q"), N + "Q", 0.1f, 10.0f, 0.7f, fmt (2, ""), 1.0f);
        c (IDs::eq (i, "slope"), N + "Slope", StringArray { "12 dB/oct", "24 dB/oct", "48 dB/oct" }, 1);
        c (IDs::eq (i, "ch"), N + "Channel", StringArray { "Stereo", "Mid", "Side" }, 0);
    }
    // ---- Smart balance
    b (IDs::shOn, "Smart Balance On", false);
    f (IDs::shAmount, "Balance Amount", 0.0f, 1.0f, 0.4f, pct);
    f (IDs::shTame, "Resonance Tame", 0.0f, 1.0f, 0.3f, pct);
    f (IDs::shBoost, "Recover (boost)", 0.0f, 1.0f, 0.5f, pct);
    f (IDs::shSpeed, "Balance Speed", 0.0f, 1.0f, 0.5f, pct);
    f (IDs::shLow, "Balance Low Limit", 20.0f, 500.0f, 30.0f, hz, 80.0f);
    f (IDs::shHigh, "Balance High Limit", 2000.0f, 20000.0f, 18000.0f, hz, 8000.0f);
    // ---- Bass
    b (IDs::bassOn, "Bass On", false);
    f (IDs::bassFreq, "Bass Freq", 40.0f, 250.0f, 110.0f, hz, 110.0f);
    f (IDs::bassPunch, "Bass Punch", -1.0f, 1.0f, 0.0f, bip);
    f (IDs::bassSustain, "Bass Sustain", -1.0f, 1.0f, 0.0f, bip);
    f (IDs::bassGain, "Bass Gain", -6.0f, 6.0f, 0.0f, db);
    b (IDs::bassMono, "Mono Bass", true);
    // ---- Multiband
    b (IDs::mbOn, "Multiband On", false);
    f (IDs::mbX1, "MB Crossover 1", 40.0f, 400.0f, 120.0f, hz, 120.0f);
    f (IDs::mbX2, "MB Crossover 2", 400.0f, 4000.0f, 1000.0f, hz, 1200.0f);
    f (IDs::mbX3, "MB Crossover 3", 3000.0f, 14000.0f, 6000.0f, hz, 6500.0f);
    for (int i = 0; i < 4; ++i)
    {
        const String N = "MB " + String (i + 1) + " ";
        f (IDs::mb (i, "thr"), N + "Threshold", -40.0f, 0.0f, -18.0f, db);
        f (IDs::mb (i, "ratio"), N + "Ratio", 1.0f, 10.0f, 2.0f, fmt (1, ":1"), 3.0f);
        f (IDs::mb (i, "gain"), N + "Gain", -12.0f, 12.0f, 0.0f, db);
    }
    f (IDs::mbAttack, "MB Attack", 0.1f, 100.0f, 10.0f, ms, 10.0f);
    f (IDs::mbRelease, "MB Release", 10.0f, 1000.0f, 120.0f, ms, 150.0f);
    f (IDs::mbMix, "MB Mix", 0.0f, 1.0f, 1.0f, pct);
    // ---- Glue
    b (IDs::glueOn, "Glue On", false);
    f (IDs::glueThr, "Glue Threshold", -30.0f, 0.0f, -12.0f, db);
    c (IDs::glueRatio, "Glue Ratio", StringArray { "1.5:1", "2:1", "4:1", "10:1" }, 1);
    c (IDs::glueAttack, "Glue Attack", StringArray { "0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms" }, 4);
    c (IDs::glueRelease, "Glue Release", StringArray { "0.1 s", "0.3 s", "0.6 s", "1.2 s", "Auto" }, 4);
    f (IDs::glueMakeup, "Glue Makeup", 0.0f, 12.0f, 0.0f, db);
    f (IDs::glueMix, "Glue Mix", 0.0f, 1.0f, 1.0f, pct);
    c (IDs::glueSc, "Glue SC HPF", StringArray { "Off", "60 Hz", "90 Hz", "150 Hz" }, 2);
    // ---- Saturation
    b (IDs::satOn, "Saturate On", false);
    c (IDs::satMode, "Saturate Mode", StringArray { "Tape", "Tube", "Warm", "Hard" }, 0);
    f (IDs::satDrive, "Saturate Drive", 0.0f, 18.0f, 3.0f, db, 6.0f);
    f (IDs::satTone, "Saturate Tone", -1.0f, 1.0f, 0.0f, bip);
    f (IDs::satExciter, "Exciter", 0.0f, 1.0f, 0.0f, pct);
    f (IDs::satMix, "Saturate Mix", 0.0f, 1.0f, 1.0f, pct);
    // ---- Imager
    b (IDs::imgOn, "Imager On", false);
    f (IDs::imgX1, "Imager Low X", 80.0f, 800.0f, 250.0f, hz, 250.0f);
    f (IDs::imgX2, "Imager High X", 2000.0f, 12000.0f, 5000.0f, hz, 5000.0f);
    f (IDs::imgLow, "Width Low", 0.0f, 2.0f, 1.0f, pct);
    f (IDs::imgMid, "Width Mid", 0.0f, 2.0f, 1.0f, pct);
    f (IDs::imgHigh, "Width High", 0.0f, 2.0f, 1.0f, pct);
    // ---- Clipper
    b (IDs::clipOn, "Clipper On", false);
    f (IDs::clipDrive, "Clipper Drive", 0.0f, 12.0f, 0.0f, db);
    f (IDs::clipSoft, "Clipper Softness", 0.0f, 1.0f, 0.5f, pct);
    // ---- Maximizer
    b (IDs::maxOn, "Maximizer On", true);
    f (IDs::maxDrive, "Maximizer Gain", 0.0f, 24.0f, 0.0f, db, 6.0f);
    f (IDs::maxCeiling, "Ceiling (dBTP)", -3.0f, 0.0f, -1.0f, fmt (1, " dBTP"));
    f (IDs::maxRelease, "Release", 1.0f, 1000.0f, 100.0f, ms, 100.0f);
    c (IDs::maxMode, "Character", StringArray { "Transparent", "Punchy", "Loud", "Smooth" }, 0);
    f (IDs::maxLink, "Stereo Link", 0.0f, 1.0f, 1.0f, pct);
    b (IDs::maxDelta, "Delta (hear what's removed)", false);
    // ---- Dither
    c (IDs::dither, "Dither", StringArray { "Off", "16-bit TPDF", "16-bit Shaped", "24-bit TPDF" }, 0);
    return l;
}
