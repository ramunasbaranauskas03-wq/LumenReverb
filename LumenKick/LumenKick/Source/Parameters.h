#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include <utility>

namespace IDs
{
    inline const juce::String root = "root", keyTrack = "keytrack", velSens = "velsens", pitch = "pitch", x1 = "xover1", x2 = "xover2";
    inline const juce::String drvType = "drv_type", drvAmt = "drv_amt", eqLow = "eq_low", eqMidF = "eq_midf", eqMidG = "eq_midg", eqHigh = "eq_high", clip = "clip", out = "out";
    // per layer: prefix "t_", "m_", "l_"
    inline juce::String L (int layer, const char* p) { static const char* pre[] { "t_", "m_", "l_" }; return juce::String (pre[layer]) + p; }
}

inline juce::StringArray layerNames() { return { "TOP", "MID", "LOW" }; }
inline juce::StringArray topTypeNames() { return { "Click", "Noise", "Snap", "Tick" }; }
inline juce::StringArray driveNames() { return { "Drive Off", "Tube", "Hard Clip", "Fold", "Crush" }; }
inline juce::StringArray keyTrackNames() { return { "Key: Off", "Key: Sub", "Key: All" }; }
inline juce::String noteText (int midi)
{
    static const char* n[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (n[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
}
inline juce::String hzNote (float hz)
{
    if (hz <= 0) return "--";
    const double m = 69.0 + 12.0 * std::log2 (hz / 440.0);
    const int mi = (int) std::lround (m), cents = (int) std::lround ((m - mi) * 100.0);
    return noteText (mi) + (cents >= 0 ? " +" : " ") + juce::String (cents) + "c";
}
inline juce::StringArray rootNames() { juce::StringArray s; for (int m = 21; m <= 47; ++m) s.add (noteText (m)); return s; }   // A0 .. B2
constexpr int kRootBase = 21;

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const String& id) { return ParameterID { id, 2 }; };
    auto fmt = [] (std::function<String (float)> f) { return AudioParameterFloatAttributes().withStringFromValueFunction ([f] (float v, int) { return f (v); }); };
    auto db = fmt ([] (float v) { return v <= -59.9f ? String ("Off") : String (v, 1) + " dB"; });
    auto ms = fmt ([] (float v) { return v < 10.0f ? String (v, 1) + " ms" : String (roundToInt (v)) + " ms"; });
    auto st = fmt ([] (float v) { return String (v, 1) + " st"; });
    auto pct = fmt ([] (float v) { return String (roundToInt (v * 100.0f)) + " %"; });
    auto hz = fmt ([] (float v) { return v < 1000.0f ? String (roundToInt (v)) + " Hz" : String (v / 1000.0f, 2) + " kHz"; });
    auto rng = [] (float a, float b, float c) { NormalisableRange<float> r (a, b); r.setSkewForCentre (c); return r; };
    auto f = [&] (const String& id, const String& nm, NormalisableRange<float> r, float d, AudioParameterFloatAttributes at) { l.add (std::make_unique<AudioParameterFloat> (pid (id), nm, r, d, at)); };
    auto b = [&] (const String& id, const String& nm, bool d) { l.add (std::make_unique<AudioParameterBool> (pid (id), nm, d)); };
    auto c = [&] (const String& id, const String& nm, StringArray ch, int d) { l.add (std::make_unique<AudioParameterChoice> (pid (id), nm, ch, d)); };

    c (IDs::root, "Root Note", rootNames(), 29 - kRootBase);       // F1
    c (IDs::keyTrack, "Key Track", keyTrackNames(), 1);
    f (IDs::velSens, "Velocity", { 0.0f, 1.0f }, 0.3f, pct);
    f (IDs::pitch, "Global Pitch", { -12.0f, 12.0f, 0.01f }, 0.0f, st);
    f (IDs::x1, "Split Low / Mid", rng (50.0f, 400.0f, 150.0f), 150.0f, hz);
    f (IDs::x2, "Split Mid / Top", rng (800.0f, 8000.0f, 2500.0f), 2500.0f, hz);

    struct Def { float syn, dec, curve; };
    const Def defs[3] { { -10.0f, 30.0f, 0.5f }, { -6.0f, 120.0f, 0.5f }, { 0.0f, 450.0f, 0.35f } };
    for (int k = 0; k < 3; ++k)
    {
        const String n = layerNames()[k];
        b (IDs::L (k, "on"), n + " On", true);
        b (IDs::L (k, "solo"), n + " Solo", false);
        b (IDs::L (k, "inv"), n + " Phase Invert", false);
        b (IDs::L (k, "filt"), n + " Band Filter", true);
        f (IDs::L (k, "syn"), n + " Synth Level", { -60.0f, 6.0f, 0.1f }, defs[k].syn, db);
        f (IDs::L (k, "smp"), n + " Sample Level", { -60.0f, 6.0f, 0.1f }, 0.0f, db);
        f (IDs::L (k, "spitch"), n + " Sample Pitch", { -24.0f, 24.0f, 0.01f }, 0.0f, st);
        f (IDs::L (k, "sstart"), n + " Sample Start", rng (0.0f, 100.0f, 10.0f), 0.0f, ms);
        f (IDs::L (k, "nudge"), n + " Nudge", rng (0.0f, 20.0f, 4.0f), 0.0f, ms);
        f (IDs::L (k, "att"), n + " Attack", rng (0.0f, 20.0f, 3.0f), 0.0f, ms);
        f (IDs::L (k, "dec"), n + " Decay", rng (5.0f, 3000.0f, 300.0f), defs[k].dec, ms);
        f (IDs::L (k, "curve"), n + " Curve", { 0.0f, 1.0f }, defs[k].curve, pct);
    }
    // LOW synth (sub)
    f (IDs::L (2, "start"), "Sub Pitch Amount", { 0.0f, 60.0f, 0.1f }, 36.0f, st);
    f (IDs::L (2, "pdec"), "Sub Pitch Decay", rng (1.0f, 300.0f, 40.0f), 35.0f, ms);
    f (IDs::L (2, "drive"), "Sub Drive", { 0.0f, 1.0f }, 0.15f, pct);
    // MID synth (body)
    f (IDs::L (1, "freq"), "Body Freq", rng (60.0f, 600.0f, 180.0f), 160.0f, hz);
    f (IDs::L (1, "start"), "Body Pitch Amount", { 0.0f, 48.0f, 0.1f }, 24.0f, st);
    f (IDs::L (1, "pdec"), "Body Pitch Decay", rng (1.0f, 150.0f, 15.0f), 12.0f, ms);
    f (IDs::L (1, "wave"), "Body Wave", { 0.0f, 1.0f }, 0.3f, fmt ([] (float v) { return v < 0.25f ? "Sine " + String (roundToInt (v * 100)) : v < 0.75f ? "Tri " + String (roundToInt (v * 100)) : "Square " + String (roundToInt (v * 100)); }));
    f (IDs::L (1, "drive"), "Body Drive", { 0.0f, 1.0f }, 0.2f, pct);
    // TOP synth (click)
    c (IDs::L (0, "type"), "Click Type", topTypeNames(), 0);
    f (IDs::L (0, "tone"), "Click Tone", rng (800.0f, 12000.0f, 4000.0f), 4500.0f, hz);
    f (IDs::L (0, "tdec"), "Click Decay", rng (0.3f, 80.0f, 8.0f), 6.0f, ms);
    // master
    c (IDs::drvType, "Drive Type", driveNames(), 1);
    f (IDs::drvAmt, "Drive", { 0.0f, 1.0f }, 0.15f, pct);
    f (IDs::eqLow, "EQ Low", { -12.0f, 12.0f, 0.1f }, 0.0f, fmt ([] (float v) { return String (v, 1) + " dB"; }));
    f (IDs::eqMidF, "EQ Mid Freq", rng (100.0f, 6000.0f, 800.0f), 800.0f, hz);
    f (IDs::eqMidG, "EQ Mid Gain", { -12.0f, 12.0f, 0.1f }, 0.0f, fmt ([] (float v) { return String (v, 1) + " dB"; }));
    f (IDs::eqHigh, "EQ High", { -12.0f, 12.0f, 0.1f }, 0.0f, fmt ([] (float v) { return String (v, 1) + " dB"; }));
    b (IDs::clip, "Clipper", true);
    f (IDs::out, "Output", { -24.0f, 6.0f, 0.1f }, -3.0f, fmt ([] (float v) { return String (v, 1) + " dB"; }));
    return l;
}

//==============================================================================
// synth-only factory kicks (values in parameter units, applied on top of the defaults)
struct KickPreset { const char* name; std::vector<std::pair<juce::String, float>> v; };
inline const std::vector<KickPreset>& factoryPresets()
{
    using P = std::vector<std::pair<juce::String, float>>;
    auto root = [] (int midi) { return (float) (midi - kRootBase); };
    static const std::vector<KickPreset> p {
        { "Default - Modern Punch", {} },
        { "TECHNO Rumble-Ready", P { { "root", root (31) }, { "l_start", 30 }, { "l_pdec", 28 }, { "l_dec", 380 }, { "l_curve", 0.3f }, { "l_drive", 0.25f }, { "m_syn", -8 }, { "m_freq", 140 }, { "m_dec", 90 },
                                     { "t_syn", -14 }, { "t_type", 1 }, { "t_tone", 3500 }, { "t_dec", 25 }, { "drv_type", 1 }, { "drv_amt", 0.3f }, { "eq_midf", 350 }, { "eq_midg", -3 } } },
        { "HARD TECHNO Distorted", P { { "root", root (29) }, { "l_start", 40 }, { "l_pdec", 22 }, { "l_dec", 520 }, { "l_curve", 0.25f }, { "l_drive", 0.6f }, { "m_syn", -4 }, { "m_drive", 0.6f }, { "m_wave", 0.6f },
                                       { "t_syn", -10 }, { "t_type", 2 }, { "drv_type", 2 }, { "drv_amt", 0.6f }, { "eq_high", 3 } } },
        { "HOUSE 909-ish", P { { "root", root (31) }, { "l_start", 28 }, { "l_pdec", 30 }, { "l_dec", 320 }, { "l_curve", 0.45f }, { "l_drive", 0.1f }, { "m_syn", -9 }, { "m_freq", 200 }, { "m_dec", 70 },
                               { "t_syn", -12 }, { "t_type", 1 }, { "t_tone", 6000 }, { "t_tdec", 10 }, { "t_dec", 40 }, { "drv_amt", 0.2f } } },
        { "DEEP HOUSE Round", P { { "root", root (29) }, { "l_start", 24 }, { "l_pdec", 40 }, { "l_dec", 420 }, { "l_curve", 0.5f }, { "l_drive", 0.05f }, { "m_syn", -14 }, { "t_syn", -22 }, { "drv_amt", 0.1f }, { "eq_high", -4 } } },
        { "TECH HOUSE Tight", P { { "root", root (32) }, { "l_start", 30 }, { "l_pdec", 25 }, { "l_dec", 260 }, { "l_curve", 0.5f }, { "m_syn", -6 }, { "m_freq", 190 }, { "m_dec", 80 }, { "t_syn", -10 }, { "t_tone", 5000 }, { "drv_amt", 0.25f } } },
        { "BIG ROOM Long Tail", P { { "root", root (29) }, { "l_start", 38 }, { "l_pdec", 30 }, { "l_dec", 900 }, { "l_curve", 0.15f }, { "l_drive", 0.35f }, { "m_syn", -5 }, { "m_freq", 170 }, { "m_drive", 0.4f },
                                    { "t_syn", -8 }, { "t_type", 2 }, { "t_tone", 5000 }, { "drv_type", 1 }, { "drv_amt", 0.4f } } },
        { "PSYTRANCE Short & Snappy", P { { "root", root (33) }, { "l_start", 36 }, { "l_pdec", 18 }, { "l_dec", 180 }, { "l_curve", 0.6f }, { "m_syn", -6 }, { "m_freq", 220 }, { "m_dec", 60 },
                                          { "t_syn", -8 }, { "t_type", 0 }, { "t_tone", 6500 }, { "drv_amt", 0.3f } } },
        { "TRANCE Clean Punch", P { { "root", root (31) }, { "l_start", 32 }, { "l_pdec", 26 }, { "l_dec", 330 }, { "m_syn", -7 }, { "m_freq", 180 }, { "t_syn", -11 }, { "t_tone", 5500 } } },
        { "HARDSTYLE Pitched Kick", P { { "root", root (34) }, { "l_start", 48 }, { "l_pdec", 60 }, { "l_dec", 650 }, { "l_curve", 0.2f }, { "l_drive", 0.85f }, { "m_syn", -2 }, { "m_freq", 220 }, { "m_drive", 0.8f }, { "m_wave", 0.8f },
                                        { "t_syn", -8 }, { "t_type", 2 }, { "drv_type", 2 }, { "drv_amt", 0.75f } } },
        { "DnB Punchy", P { { "root", root (33) }, { "l_start", 34 }, { "l_pdec", 20 }, { "l_dec", 220 }, { "l_curve", 0.55f }, { "m_syn", -5 }, { "m_freq", 210 }, { "m_dec", 70 },
                            { "t_syn", -7 }, { "t_type", 2 }, { "t_tone", 6000 }, { "drv_amt", 0.35f } } },
        { "DUBSTEP Heavy", P { { "root", root (29) }, { "l_start", 36 }, { "l_pdec", 30 }, { "l_dec", 420 }, { "l_drive", 0.4f }, { "m_syn", -4 }, { "m_drive", 0.5f }, { "t_syn", -7 }, { "t_type", 2 }, { "drv_type", 2 }, { "drv_amt", 0.45f } } },
        { "TRAP 808 Kick", P { { "root", root (31) }, { "l_start", 20 }, { "l_pdec", 45 }, { "l_dec", 1400 }, { "l_curve", 0.2f }, { "l_drive", 0.3f }, { "m_syn", -18 }, { "t_syn", -16 }, { "t_type", 3 }, { "drv_amt", 0.25f } } },
        { "PHONK Crushed", P { { "root", root (30) }, { "l_start", 30 }, { "l_dec", 700 }, { "l_drive", 0.5f }, { "m_syn", -6 }, { "drv_type", 4 }, { "drv_amt", 0.45f }, { "eq_high", -3 } } },
        { "FUTURE BASS Soft", P { { "root", root (31) }, { "l_start", 26 }, { "l_pdec", 35 }, { "l_dec", 380 }, { "m_syn", -12 }, { "t_syn", -16 }, { "t_type", 1 }, { "drv_amt", 0.08f } } },
        { "MELODIC TECHNO Warm", P { { "root", root (31) }, { "l_start", 26 }, { "l_pdec", 32 }, { "l_dec", 400 }, { "l_drive", 0.15f }, { "m_syn", -10 }, { "m_freq", 150 }, { "t_syn", -18 }, { "t_type", 1 }, { "eq_high", -2 } } },
        { "MINIMAL Click", P { { "root", root (33) }, { "l_start", 30 }, { "l_pdec", 20 }, { "l_dec", 200 }, { "l_curve", 0.6f }, { "m_syn", -12 }, { "t_syn", -6 }, { "t_type", 3 }, { "t_tone", 7000 } } },
        { "AFRO HOUSE Organic", P { { "root", root (31) }, { "l_start", 22 }, { "l_pdec", 40 }, { "l_dec", 300 }, { "m_syn", -8 }, { "m_freq", 130 }, { "m_wave", 0.0f }, { "t_syn", -14 }, { "t_type", 1 }, { "t_tone", 2500 }, { "t_tdec", 15 } } },
        { "UK GARAGE Thump", P { { "root", root (32) }, { "l_start", 28 }, { "l_pdec", 30 }, { "l_dec", 280 }, { "m_syn", -7 }, { "m_freq", 160 }, { "t_syn", -12 }, { "t_type", 0 } } },
        { "LO-FI Dusty", P { { "root", root (31) }, { "l_start", 20 }, { "l_dec", 260 }, { "m_syn", -8 }, { "t_syn", -14 }, { "t_type", 1 }, { "drv_type", 4 }, { "drv_amt", 0.3f }, { "eq_high", -6 } } },
        { "SUB Only (layer under a sample)", P { { "l_start", 24 }, { "l_dec", 450 }, { "m_syn", -60 }, { "t_syn", -60 }, { "drv_amt", 0.0f } } },
    };
    return p;
}
